// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package flv

import (
	"context"
	"crypto/tls"
	"crypto/x509"
	"errors"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/media"
)

// flvBytes is an FLV header with the flags, then the tags.
func flvBytes(audio, video bool, tags ...*media.FLVTag) []byte {
	flags := byte(0)
	if audio {
		flags |= 0x04
	}
	if video {
		flags |= 0x01
	}
	b := []byte{'F', 'L', 'V', 1, flags, 0, 0, 0, 9, 0, 0, 0, 0}
	for _, tag := range tags {
		n := len(tag.Data)
		ts := tag.Timestamp
		b = append(b, tag.Type, byte(n>>16), byte(n>>8), byte(n), byte(ts>>16), byte(ts>>8), byte(ts), byte(ts>>24), 0, 0, 0)
		b = append(b, tag.Data...)
		size := n + 11
		b = append(b, byte(size>>24), byte(size>>16), byte(size>>8), byte(size))
	}
	return b
}

func dialTest(t *testing.T, url string, opts *HTTPOptions) *HTTPPlayer {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	p, err := DialHTTP(ctx, url, opts)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { p.Close() })
	p.SetDeadline(time.Now().Add(time.Second))
	return p
}

// TestHTTPPlayerReadsStream checks the request the player sends, then the
// answer, the FLV header flags and the tags, and io.EOF at the end.
func TestHTTPPlayerReadsStream(t *testing.T) {
	tag := &media.FLVTag{Type: media.TagAudio, Timestamp: 0x01020304, Data: []byte{0xaf, 1, 2, 3}}
	got := make(chan *http.Request, 1)
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		got <- r
		w.Header().Set("Content-Type", "video/x-flv")
		w.Write(flvBytes(true, false, tag))
	}))
	defer srv.Close()

	opts := &HTTPOptions{Host: "example.com", Header: http.Header{"X-Forwarded-For": {"10.1.2.3"}}}
	p := dialTest(t, srv.URL+"/live/s.flv?token=abc", opts)

	res, err := p.Response()
	if err != nil {
		t.Fatal(err)
	}
	if res.StatusCode != 200 || res.Header.Get("Content-Type") != "video/x-flv" {
		t.Errorf("answer %v %q", res.Status, res.Header.Get("Content-Type"))
	}
	r := <-got
	if r.Method != "GET" || r.URL.Path != "/live/s.flv" || r.URL.RawQuery != "token=abc" {
		t.Errorf("request %v %v", r.Method, r.URL)
	}
	if r.Host != "example.com" || r.Header.Get("X-Forwarded-For") != "10.1.2.3" {
		t.Errorf("request host %q, X-Forwarded-For %q", r.Host, r.Header.Get("X-Forwarded-For"))
	}

	audio, video, err := p.Header()
	if err != nil || !audio || video {
		t.Errorf("header audio %v video %v, err %v; want audio only", audio, video, err)
	}
	read, err := p.ReadTag()
	if err != nil {
		t.Fatal(err)
	}
	if read.Type != tag.Type || read.Timestamp != tag.Timestamp || string(read.Data) != string(tag.Data) {
		t.Errorf("tag %+v, want %+v", read, tag)
	}
	if _, err := p.ReadTag(); err != io.EOF {
		t.Errorf("after the last tag: %v, want io.EOF", err)
	}
	if _, err := p.ReadTag(); err != io.EOF {
		t.Errorf("read again after the end: %v, want io.EOF", err)
	}
}

// TestHTTPPlayerStatus checks that an answer other than 200 is a
// *StatusError from Header and ReadTag, and that Response returns it.
func TestHTTPPlayerStatus(t *testing.T) {
	srv := httptest.NewServer(http.NotFoundHandler())
	defer srv.Close()
	p := dialTest(t, srv.URL+"/live/s.flv", nil)

	res, err := p.Response()
	if err != nil || res.StatusCode != 404 {
		t.Fatalf("response %v, err %v; want 404", res, err)
	}
	var se *StatusError
	if _, _, err := p.Header(); !errors.As(err, &se) || se.Code != 404 {
		t.Errorf("header err %v, want a StatusError 404", err)
	}
	if _, err := p.ReadTag(); !errors.As(err, &se) || se.Code != 404 {
		t.Errorf("read err %v, want a StatusError 404", err)
	}
}

// TestHTTPPlayerDeadline checks that a wait past the deadline is a timeout,
// and that the stream reads on after the deadline is moved.
func TestHTTPPlayerDeadline(t *testing.T) {
	tag := &media.FLVTag{Type: media.TagVideo, Timestamp: 40, Data: []byte{0x17, 1, 0, 0, 0}}
	release := make(chan struct{})
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Write(flvBytes(false, true))
		w.(http.Flusher).Flush()
		<-release
		w.Write(flvBytes(false, true, tag)[13:])
	}))
	defer srv.Close()
	defer close(release)
	p := dialTest(t, srv.URL+"/live/s.flv", nil)
	if _, video, err := p.Header(); err != nil || !video {
		t.Fatalf("header video %v, err %v", video, err)
	}

	p.SetDeadline(time.Now().Add(50 * time.Millisecond))
	start := time.Now()
	_, err := p.ReadTag()
	var ne net.Error
	if !errors.As(err, &ne) || !ne.Timeout() {
		t.Fatalf("read err %v, want a timeout", err)
	}
	if d := time.Since(start); d < 40*time.Millisecond || d > 500*time.Millisecond {
		t.Errorf("timed out after %v, want about 50ms", d)
	}

	p.SetDeadline(time.Now().Add(time.Second))
	release <- struct{}{}
	read, err := p.ReadTag()
	if err != nil || read.Timestamp != 40 {
		t.Errorf("read after the timeout: %+v, %v", read, err)
	}
}

// TestHTTPPlayerNoAnswer checks that the answer of a server that hasn't
// answered yet is a timeout, not an error of the dial.
func TestHTTPPlayerNoAnswer(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	reqs := make(chan string, 1)
	go func() {
		conn, err := ln.Accept()
		if err != nil {
			return
		}
		defer conn.Close()
		b := make([]byte, 4096)
		n, _ := conn.Read(b)
		reqs <- string(b[:n])
		time.Sleep(time.Second)
	}()

	p := dialTest(t, "http://"+ln.Addr().String()+"/live/s.flv", &HTTPOptions{Method: "OPTIONS"})
	if req := <-reqs; !strings.HasPrefix(req, "OPTIONS /live/s.flv HTTP/1.1\r\n") {
		t.Errorf("request %q", req)
	}
	p.SetDeadline(time.Now().Add(50 * time.Millisecond))
	_, err = p.Response()
	var ne net.Error
	if !errors.As(err, &ne) || !ne.Timeout() {
		t.Errorf("response err %v, want a timeout", err)
	}
}

// TestHTTPPlayerClosedByServer checks that a connection the server closes
// mid-tag is an error, not io.EOF.
func TestHTTPPlayerClosedByServer(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	go func() {
		conn, err := ln.Accept()
		if err != nil {
			return
		}
		b := make([]byte, 4096)
		conn.Read(b)
		body := flvBytes(true, true)
		conn.Write([]byte("HTTP/1.1 200 OK\r\nContent-Type: video/x-flv\r\nConnection: close\r\n\r\n"))
		conn.Write(append(body, media.TagAudio, 0, 0, 9))
		conn.Close()
	}()

	p := dialTest(t, "http://"+ln.Addr().String()+"/live/s.flv", nil)
	if _, err := p.ReadTag(); err == nil || err == io.EOF {
		t.Errorf("read err %v, want an unexpected EOF", err)
	}
}

// TestHTTPPlayerTLS plays over https:// with a config that trusts the test
// server.
func TestHTTPPlayerTLS(t *testing.T) {
	srv := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Write(flvBytes(true, true))
	}))
	defer srv.Close()
	pool := x509.NewCertPool()
	pool.AddCert(srv.Certificate())

	p := dialTest(t, srv.URL+"/live/s.flv", &HTTPOptions{TLS: &tls.Config{RootCAs: pool}})
	if audio, video, err := p.Header(); err != nil || !audio || !video {
		t.Errorf("header audio %v video %v, err %v", audio, video, err)
	}
	if st := p.TLS(); st == nil || len(st.VerifiedChains) != 1 {
		t.Errorf("TLS state %+v, want one verified chain", st)
	}

	// The system roots don't trust the test server, so the dial fails in the
	// TLS handshake.
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	if p, err := DialHTTP(ctx, srv.URL+"/live/s.flv", nil); err == nil {
		p.Close()
		t.Errorf("the dial to an untrusted server worked")
	}
}

// TestHTTPPlayerRefused checks that a refused dial fails at once.
func TestHTTPPlayerRefused(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	addr := ln.Addr().String()
	ln.Close()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	if _, err := DialHTTP(ctx, "http://"+addr+"/live/s.flv", nil); err == nil {
		t.Errorf("dial of a closed port worked")
	}
	if _, err := DialHTTP(ctx, "rtmp://"+addr+"/live/s", nil); err == nil {
		t.Errorf("dial of an rtmp:// URL worked")
	}
}
