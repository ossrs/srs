// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package utils

import (
	"context"
	"crypto/tls"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"syscall"
	"testing"

	"srsx/internal/errors"
)

// errReadCloser always fails on Read.
type errReadCloser struct{ closed bool }

func (e *errReadCloser) Read(p []byte) (int, error) { return 0, io.ErrUnexpectedEOF }
func (e *errReadCloser) Close() error               { e.closed = true; return nil }

func TestApiResponse_EncodesJSON(t *testing.T) {
	rec := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/", nil)
	ApiResponse(context.Background(), rec, req, map[string]int{"a": 1})

	if rec.Code != http.StatusOK {
		t.Fatalf("code = %d, want 200", rec.Code)
	}
	if got := rec.Header().Get("Content-Type"); got != "application/json" {
		t.Fatalf("Content-Type = %q", got)
	}
	if rec.Header().Get("Server") == "" {
		t.Fatal("Server header empty")
	}
	if got := strings.TrimSpace(rec.Body.String()); got != `{"a":1}` {
		t.Fatalf("body = %q", got)
	}
}

func TestApiResponse_MarshalErrorFallsBackToApiError(t *testing.T) {
	rec := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/", nil)
	// Channels are not JSON-marshalable.
	ApiResponse(context.Background(), rec, req, make(chan int))

	if rec.Code != http.StatusInternalServerError {
		t.Fatalf("code = %d, want 500", rec.Code)
	}
	if ct := rec.Header().Get("Content-Type"); !strings.HasPrefix(ct, "text/plain") {
		t.Fatalf("Content-Type = %q", ct)
	}
}

func TestApiError_WritesPlainText500(t *testing.T) {
	rec := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/", nil)
	ApiError(context.Background(), rec, req, errors.New("boom"))

	if rec.Code != http.StatusInternalServerError {
		t.Fatalf("code = %d", rec.Code)
	}
	if got := strings.TrimSpace(rec.Body.String()); got != "boom" {
		t.Fatalf("body = %q", got)
	}
}

func TestApiCORS_OptionsPreflightReturnsTrue(t *testing.T) {
	rec := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodOptions, "/", nil)
	if !ApiCORS(context.Background(), rec, req) {
		t.Fatal("OPTIONS should return true")
	}
	if rec.Code != http.StatusOK {
		t.Fatalf("code = %d", rec.Code)
	}
	if rec.Header().Get("Access-Control-Allow-Origin") != "*" {
		t.Fatal("missing Allow-Origin")
	}
}

func TestApiCORS_NonOptionsReturnsFalse(t *testing.T) {
	rec := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/", nil)
	if ApiCORS(context.Background(), rec, req) {
		t.Fatal("GET should return false")
	}
	if rec.Header().Get("Access-Control-Allow-Methods") != "*" {
		t.Fatal("missing Allow-Methods")
	}
}

func TestParseBody_Success(t *testing.T) {
	var v struct {
		Name string `json:"name"`
	}
	body := io.NopCloser(strings.NewReader(`{"name":"alice"}`))
	if err := ParseBody(body, &v); err != nil {
		t.Fatalf("unexpected err: %v", err)
	}
	if v.Name != "alice" {
		t.Fatalf("name = %q", v.Name)
	}
}

func TestParseBody_EmptyBodyIsNoOp(t *testing.T) {
	var v struct{ Name string }
	if err := ParseBody(io.NopCloser(strings.NewReader("")), &v); err != nil {
		t.Fatalf("unexpected err: %v", err)
	}
}

func TestParseBody_ReadError(t *testing.T) {
	if err := ParseBody(&errReadCloser{}, &struct{}{}); err == nil {
		t.Fatal("want error")
	}
}

// TestParseBody_CloseCalledOnReadError verifies that r.Close() is called even
// when ReadAll fails - this prevents the resource leak fixed by moving defer
// to the top of the function.
func TestParseBody_CloseCalledOnReadError(t *testing.T) {
	rc := &errReadCloser{}
	if err := ParseBody(rc, &struct{}{}); err == nil {
		t.Fatal("want error")
	}
	if !rc.closed {
		t.Fatal("Close() was not called on read error - resource leak")
	}
}

func TestParseBody_UnmarshalError(t *testing.T) {
	var v struct{ Name string }
	err := ParseBody(io.NopCloser(strings.NewReader("not json")), &v)
	if err == nil {
		t.Fatal("want error")
	}
	if !strings.Contains(err.Error(), "json unmarshal") {
		t.Fatalf("err = %v", err)
	}
}

func TestBuildStreamURL(t *testing.T) {
	cases := []struct {
		in, want string
	}{
		{"rtmp://example.com/live/stream", "example.com/live/stream"},
		{"rtmp://example.com:1935/live/stream", "example.com/live/stream"},
		{"rtmp://127.0.0.1/live/stream", "__defaultVhost__/live/stream"},
		{"rtmp://localhost/live/stream", "__defaultVhost__/live/stream"},
		{"rtmp://localhost:1935/live/stream", "__defaultVhost__/live/stream"},
		{"rtmp://[::1]/live/stream", "__defaultVhost__/live/stream"},
		{"rtmp://[2001:db8::1]:1935/live/stream", "__defaultVhost__/live/stream"},
	}
	for _, c := range cases {
		got, err := BuildStreamURL(c.in)
		if err != nil {
			t.Fatalf("%s: err = %v", c.in, err)
		}
		if got != c.want {
			t.Fatalf("%s: got %q want %q", c.in, got, c.want)
		}
	}
}

func TestBuildStreamURL_ParseError(t *testing.T) {
	if _, err := BuildStreamURL("http://%zz"); err == nil {
		t.Fatal("want error")
	}
}

func TestIsPeerClosedError(t *testing.T) {
	cases := []struct {
		name string
		err  error
		want bool
	}{
		{"nil", nil, false},
		{"EOF", io.EOF, true},
		{"wrapped-EOF", errors.Wrap(io.EOF, "ctx"), true},
		{"EPIPE", syscall.EPIPE, true},
		// errors.Cause fully unwraps OpError → SyscallError → Errno, so the
		// OpError branch inside IsPeerClosedError is not reachable for the
		// canonical wrapping shape. We still exercise these constructions to
		// lock in the current behavior.
		{"ECONNRESET-wrapped-in-OpError", &net.OpError{Err: &os.SyscallError{Err: syscall.ECONNRESET}}, false},
		{"OpError-with-other-syscall", &net.OpError{Err: &os.SyscallError{Err: syscall.EINVAL}}, false},
		{"OpError-not-SyscallError", &net.OpError{Err: errors.New("boom")}, false},
		{"unrelated", errors.New("other"), false},
	}
	for _, c := range cases {
		if got := IsPeerClosedError(c.err); got != c.want {
			t.Fatalf("%s: got %v want %v", c.name, got, c.want)
		}
	}
}

func TestIsClosedNetworkError(t *testing.T) {
	cases := []struct {
		name string
		err  error
		want bool
	}{
		{"nil", nil, false},
		{"OpError-matching", &net.OpError{Err: errors.New("use of closed network connection")}, true},
		{"OpError-other", &net.OpError{Err: errors.New("other")}, false},
		{"plain-with-substring", errors.New("wrap: use of closed network connection"), true},
		{"plain-unrelated", errors.New("other thing"), false},
	}
	for _, c := range cases {
		if got := IsClosedNetworkError(c.err); got != c.want {
			t.Fatalf("%s: got %v want %v", c.name, got, c.want)
		}
	}
}

func TestConvertURLToStreamURL_PathForm(t *testing.T) {
	req := httptest.NewRequest(http.MethodGet, "http://example.com:8080/live/stream.flv", nil)
	unified, full := ConvertURLToStreamURL(req)
	if unified != "http://example.com/live/stream" {
		t.Fatalf("unified = %q", unified)
	}
	if full != "http://example.com/live/stream.flv" {
		t.Fatalf("full = %q", full)
	}
}

func TestConvertURLToStreamURL_HostWithoutPort(t *testing.T) {
	req := httptest.NewRequest(http.MethodGet, "http://example.com/a/b.m3u8", nil)
	req.Host = "example.com"
	unified, full := ConvertURLToStreamURL(req)
	if unified != "http://__defaultVhost__/a/b" {
		t.Fatalf("unified = %q", unified)
	}
	if full != "http://__defaultVhost__/a/b.m3u8" {
		t.Fatalf("full = %q", full)
	}
}

func TestConvertURLToStreamURL_BadHostWithColonFallsBack(t *testing.T) {
	req := httptest.NewRequest(http.MethodGet, "http://example.com/a/b.ts", nil)
	req.Host = "a:b:c"
	unified, _ := ConvertURLToStreamURL(req)
	if !strings.Contains(unified, "__defaultVhost__") {
		t.Fatalf("unified = %q", unified)
	}
}

func TestConvertURLToStreamURL_QueryForm(t *testing.T) {
	req := httptest.NewRequest(http.MethodGet, "http://example.com:8080/?app=live&stream=foo", nil)
	unified, full := ConvertURLToStreamURL(req)
	if unified != "http://example.com/live/foo" {
		t.Fatalf("unified = %q", unified)
	}
	if full != "http://example.com/live/foo" {
		t.Fatalf("full = %q", full)
	}
}

func TestConvertURLToStreamURL_TLS(t *testing.T) {
	req := httptest.NewRequest(http.MethodGet, "https://example.com:443/a/b.flv", nil)
	req.TLS = &tls.ConnectionState{}
	unified, _ := ConvertURLToStreamURL(req)
	if !strings.HasPrefix(unified, "https://") {
		t.Fatalf("unified = %q", unified)
	}
}

func TestRtcIsSTUN(t *testing.T) {
	cases := []struct {
		data []byte
		want bool
	}{
		{nil, false},
		{[]byte{}, false},
		{[]byte{0x00, 0x01}, true},
		{[]byte{0x01}, true},
		{[]byte{0x02}, false},
	}
	for i, c := range cases {
		if got := RtcIsSTUN(c.data); got != c.want {
			t.Fatalf("case %d: got %v want %v", i, got, c.want)
		}
	}
}

func TestRtcIsRTPOrRTCP(t *testing.T) {
	short := make([]byte, 11)
	valid := make([]byte, 12)
	valid[0] = 0x80
	badFirstByte := make([]byte, 12)
	badFirstByte[0] = 0xC0

	if RtcIsRTPOrRTCP(short) {
		t.Fatal("short should be false")
	}
	if !RtcIsRTPOrRTCP(valid) {
		t.Fatal("valid should be true")
	}
	if RtcIsRTPOrRTCP(badFirstByte) {
		t.Fatal("0xC0 should be false")
	}
}

func TestSrtIsHandshake(t *testing.T) {
	if SrtIsHandshake([]byte{0x80, 0x00, 0x00}) {
		t.Fatal("short should be false")
	}
	if !SrtIsHandshake([]byte{0x80, 0x00, 0x00, 0x00}) {
		t.Fatal("handshake magic should be true")
	}
	if SrtIsHandshake([]byte{0x00, 0x00, 0x00, 0x01}) {
		t.Fatal("non-handshake should be false")
	}
}

func TestSrtParseSocketID(t *testing.T) {
	if SrtParseSocketID(make([]byte, 15)) != 0 {
		t.Fatal("short should be 0")
	}
	data := make([]byte, 16)
	data[12], data[13], data[14], data[15] = 0x00, 0x00, 0x00, 0x42
	if got := SrtParseSocketID(data); got != 0x42 {
		t.Fatalf("got %#x", got)
	}
}

func TestParseIceUfragPwd(t *testing.T) {
	sdp := "v=0\r\na=ice-ufrag:abc\r\na=ice-pwd:secret\r\n"
	ufrag, pwd, err := ParseIceUfragPwd(sdp)
	if err != nil {
		t.Fatalf("err = %v", err)
	}
	if ufrag != "abc" || pwd != "secret" {
		t.Fatalf("ufrag=%q pwd=%q", ufrag, pwd)
	}
}

func TestParseIceUfragPwd_MissingUfrag(t *testing.T) {
	if _, _, err := ParseIceUfragPwd("a=ice-pwd:secret"); err == nil {
		t.Fatal("want error")
	}
}

func TestParseIceUfragPwd_MissingPwd(t *testing.T) {
	if _, _, err := ParseIceUfragPwd("a=ice-ufrag:abc"); err == nil {
		t.Fatal("want error")
	}
}

// SDP embedded in the legacy /rtc/v1/play/ JSON envelope arrives with "\r\n" as
// the literal 2-byte sequence (backslash + r/n), not real CRLF. The value
// charset must stop at the backslash, otherwise the ufrag would absorb the rest
// of the SDP up to the next real whitespace.
func TestParseIceUfragPwd_JSONEscapedSDP(t *testing.T) {
	sdp := `v=0\r\na=ice-ufrag:1f1n4272\r\na=ice-pwd:5f6y69408x2h55232i080mj894901b8n\r\na=fingerprint:sha-256 2D:1D\r\n`
	ufrag, pwd, err := ParseIceUfragPwd(sdp)
	if err != nil {
		t.Fatalf("err = %v", err)
	}
	if ufrag != "1f1n4272" {
		t.Fatalf("ufrag=%q, want 1f1n4272", ufrag)
	}
	if pwd != "5f6y69408x2h55232i080mj894901b8n" {
		t.Fatalf("pwd=%q, want 5f6y69408x2h55232i080mj894901b8n", pwd)
	}
}

func TestParseSRTStreamID_WithHost(t *testing.T) {
	host, resource, err := ParseSRTStreamID("h=example.com,r=live/stream")
	if err != nil {
		t.Fatalf("err = %v", err)
	}
	if host != "example.com" || resource != "live/stream" {
		t.Fatalf("host=%q resource=%q", host, resource)
	}
}

func TestParseSRTStreamID_WithoutHost(t *testing.T) {
	host, resource, err := ParseSRTStreamID("r=live/stream")
	if err != nil {
		t.Fatalf("err = %v", err)
	}
	if host != "" || resource != "live/stream" {
		t.Fatalf("host=%q resource=%q", host, resource)
	}
}

func TestParseSRTStreamID_MissingResource(t *testing.T) {
	if _, _, err := ParseSRTStreamID("h=example.com"); err == nil {
		t.Fatal("want error")
	}
}

func TestParseListenEndpoint(t *testing.T) {
	cases := []struct {
		name     string
		in       string
		wantErr  bool
		protocol string
		ip       string // "" means nil
		port     uint16
	}{
		{"bare-port", "1935", false, "tcp", "", 1935},
		{"bare-port-bad", "abc", true, "", "", 0},
		{"url-host-port", "tcp://0.0.0.0:1935", false, "tcp", "0.0.0.0", 1935},
		{"url-empty-host", "tcp://:1935", false, "tcp", "", 1935},
		{"url-port-only", "udp://1935", false, "udp", "", 1935},
		{"url-port-only-bad", "udp://abc", true, "", "", 0},
		{"url-split-fail", "tcp://a:b:c:d", true, "", "", 0},
		{"url-bad-port", "tcp://host:bad", true, "", "", 0},
		{"legacy", "tcp:1.2.3.4:1935", false, "tcp", "1.2.3.4", 1935},
		{"legacy-bad-port", "tcp:1.2.3.4:bad", true, "", "", 0},
		{"legacy-wrong-parts", "a:b", true, "", "", 0},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			proto, ip, port, err := ParseListenEndpoint(c.in)
			if (err != nil) != c.wantErr {
				t.Fatalf("err = %v wantErr = %v", err, c.wantErr)
			}
			if c.wantErr {
				return
			}
			if proto != c.protocol {
				t.Fatalf("protocol = %q want %q", proto, c.protocol)
			}
			if port != c.port {
				t.Fatalf("port = %d want %d", port, c.port)
			}
			if c.ip == "" {
				if ip != nil {
					t.Fatalf("ip = %v want nil", ip)
				}
			} else {
				if ip == nil || ip.String() != c.ip {
					t.Fatalf("ip = %v want %s", ip, c.ip)
				}
			}
		})
	}
}
