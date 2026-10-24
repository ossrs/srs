// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package httpplay

import (
	"bufio"
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
)

// lines is a decoder whose units are the lines of the body, without their
// end; a last line without one is an unexpected EOF.
func lines(body io.Reader, emit func(string) bool) error {
	r := bufio.NewReader(body)
	for {
		s, err := r.ReadString('\n')
		if err == io.EOF && s != "" {
			return io.ErrUnexpectedEOF
		} else if err != nil {
			return err
		}
		if !emit(strings.TrimSuffix(s, "\n")) {
			return nil
		}
	}
}

func dialTest(t *testing.T, url string, opts *Options) *Player[string] {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	p, err := Dial(ctx, url, opts, lines)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { p.Close() })
	p.SetDeadline(time.Now().Add(time.Second))
	return p
}

func isTimeout(err error) bool {
	var ne net.Error
	return errors.As(err, &ne) && ne.Timeout()
}

// TestReadsStream checks the request the player sends, then the answer and
// the units, and io.EOF at the end, again on a read after it.
func TestReadsStream(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet || r.Host != "example.com" || r.Header.Get("X-Forwarded-For") != "10.1.2.3" {
			http.Error(w, "bad request "+r.Method+" "+r.Host, http.StatusBadRequest)
			return
		}
		w.Header().Set("Content-Type", "text/plain")
		io.WriteString(w, "a\nb\n")
	}))
	defer srv.Close()

	p := dialTest(t, srv.URL+"/live/s.txt", &Options{Host: "example.com", Header: http.Header{"X-Forwarded-For": {"10.1.2.3"}}})
	res, err := p.Response()
	if err != nil || res.StatusCode != 200 || res.Header.Get("Content-Type") != "text/plain" {
		t.Fatalf("response %v, err %v", res, err)
	}
	for _, want := range []string{"a", "b"} {
		if got, err := p.Read(); err != nil || got != want {
			t.Errorf("read %q, %v, want %q", got, err, want)
		}
	}
	if _, err := p.Read(); err != io.EOF {
		t.Errorf("after the last unit: %v, want io.EOF", err)
	}
	if _, err := p.Read(); err != io.EOF {
		t.Errorf("read again after the end: %v, want io.EOF", err)
	}
	if err := p.Wait(make(chan struct{})); err != io.EOF {
		t.Errorf("wait after the end: %v, want io.EOF", err)
	}
}

// TestStatus checks that an answer other than 200 is a *StatusError from
// Read and Wait, and that Response returns the answer.
func TestStatus(t *testing.T) {
	srv := httptest.NewServer(http.NotFoundHandler())
	defer srv.Close()
	p := dialTest(t, srv.URL+"/live/s.txt", nil)
	res, err := p.Response()
	if err != nil || res.StatusCode != 404 {
		t.Fatalf("response %v, err %v", res, err)
	}
	var se *StatusError
	if _, err := p.Read(); !errors.As(err, &se) || se.Code != 404 {
		t.Errorf("read err %v, want a StatusError 404", err)
	}
	if err := p.Wait(make(chan struct{})); !errors.As(err, &se) || se.Code != 404 {
		t.Errorf("wait err %v, want a StatusError 404", err)
	}
}

// TestWait checks that Wait returns nil once its channel is closed, even
// after the stream ended, and times out at the deadline.
func TestWait(t *testing.T) {
	release := make(chan struct{})
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.(http.Flusher).Flush()
		<-release
	}))
	defer srv.Close()
	defer close(release)
	p := dialTest(t, srv.URL+"/live/s.txt", nil)

	done := make(chan struct{})
	p.SetDeadline(time.Now().Add(50 * time.Millisecond))
	if err := p.Wait(done); !isTimeout(err) {
		t.Errorf("wait err %v, want a timeout", err)
	}
	close(done)
	if err := p.Wait(done); err != nil {
		t.Errorf("wait for a closed channel: %v", err)
	}
}

// TestDeadline checks that a read past the deadline is a timeout, and that
// the stream reads on after the deadline is moved.
func TestDeadline(t *testing.T) {
	release := make(chan struct{})
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		io.WriteString(w, "a\n")
		w.(http.Flusher).Flush()
		<-release
		io.WriteString(w, "b\n")
	}))
	defer srv.Close()
	defer close(release)
	p := dialTest(t, srv.URL+"/live/s.txt", nil)
	if got, err := p.Read(); err != nil || got != "a" {
		t.Fatalf("read %q, %v", got, err)
	}

	p.SetDeadline(time.Now().Add(50 * time.Millisecond))
	start := time.Now()
	if _, err := p.Read(); !isTimeout(err) {
		t.Fatalf("read err %v, want a timeout", err)
	}
	if d := time.Since(start); d < 40*time.Millisecond || d > 500*time.Millisecond {
		t.Errorf("timed out after %v, want about 50ms", d)
	}

	p.SetDeadline(time.Now().Add(time.Second))
	release <- struct{}{}
	if got, err := p.Read(); err != nil || got != "b" {
		t.Errorf("read after the timeout: %q, %v", got, err)
	}
}

// TestNoAnswer checks that the answer of a server that hasn't answered yet is
// a timeout, not an error of the dial, and the method the player sent.
func TestNoAnswer(t *testing.T) {
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

	p := dialTest(t, "http://"+ln.Addr().String()+"/live/s.txt", &Options{Method: "OPTIONS"})
	if req := <-reqs; !strings.HasPrefix(req, "OPTIONS /live/s.txt HTTP/1.1\r\n") {
		t.Errorf("request %q", req)
	}
	p.SetDeadline(time.Now().Add(50 * time.Millisecond))
	if _, err := p.Response(); !isTimeout(err) {
		t.Errorf("response err %v, want a timeout", err)
	}
}

// TestClosedByServer checks that a stream the server cuts mid-unit is the
// decoder's error, not io.EOF.
func TestClosedByServer(t *testing.T) {
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
		conn.Write([]byte("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\na\nhalf"))
		conn.Close()
	}()

	p := dialTest(t, "http://"+ln.Addr().String()+"/live/s.txt", nil)
	if got, err := p.Read(); err != nil || got != "a" {
		t.Fatalf("read %q, %v", got, err)
	}
	if _, err := p.Read(); err != io.ErrUnexpectedEOF {
		t.Errorf("read err %v, want an unexpected EOF", err)
	}
}

// TestClose checks that a read after Close ends, and that the decoder stops.
func TestClose(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		for {
			if _, err := io.WriteString(w, "x\n"); err != nil {
				return
			}
			w.(http.Flusher).Flush()
			time.Sleep(time.Millisecond)
		}
	}))
	defer srv.Close()
	p := dialTest(t, srv.URL+"/live/s.txt", nil)
	if _, err := p.Read(); err != nil {
		t.Fatal(err)
	}
	p.Close()
	for i := 0; ; i++ {
		_, err := p.Read()
		if err != nil {
			if isTimeout(err) {
				t.Errorf("read after close timed out")
			}
			break
		}
		if i > 10000 {
			t.Fatalf("reads go on after close")
		}
	}
}

// TestTLS plays over https:// with a config that trusts the test server, and
// fails the dial with one that doesn't.
func TestTLS(t *testing.T) {
	srv := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		io.WriteString(w, "a\n")
	}))
	defer srv.Close()
	pool := x509.NewCertPool()
	pool.AddCert(srv.Certificate())

	p := dialTest(t, srv.URL+"/live/s.txt", &Options{TLS: &tls.Config{RootCAs: pool}})
	if got, err := p.Read(); err != nil || got != "a" {
		t.Errorf("read %q, %v", got, err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	if p, err := Dial(ctx, srv.URL+"/live/s.txt", nil, lines); err == nil {
		p.Close()
		t.Errorf("the dial to an untrusted server worked")
	}
}

// TestRefused checks that a refused dial and a URL that isn't HTTP fail at
// once.
func TestRefused(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	addr := ln.Addr().String()
	ln.Close()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	if _, err := Dial(ctx, "http://"+addr+"/live/s.txt", nil, lines); err == nil {
		t.Errorf("dial of a closed port worked")
	}
	if _, err := Dial(ctx, "rtmp://"+addr+"/live/s", nil, lines); err == nil {
		t.Errorf("dial of an rtmp:// URL worked")
	}
}

// TestTLSState checks the connection state of an https:// player, with the
// verified chain of the test server, and none for http://.
func TestTLSState(t *testing.T) {
	handler := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		io.WriteString(w, "a\n")
	})
	srv := httptest.NewTLSServer(handler)
	defer srv.Close()
	pool := x509.NewCertPool()
	pool.AddCert(srv.Certificate())

	p := dialTest(t, srv.URL+"/live/s.txt", &Options{TLS: &tls.Config{RootCAs: pool}})
	st := p.TLS()
	if st == nil || !st.HandshakeComplete {
		t.Fatalf("TLS state %+v, want a complete handshake", st)
	}
	if len(st.VerifiedChains) != 1 || !st.VerifiedChains[0][0].Equal(srv.Certificate()) {
		t.Errorf("verified chains %v, want the test server's certificate", st.VerifiedChains)
	}

	plain := httptest.NewServer(handler)
	defer plain.Close()
	if st := dialTest(t, plain.URL+"/live/s.txt", nil).TLS(); st != nil {
		t.Errorf("TLS state %+v over http://, want nil", st)
	}
}
