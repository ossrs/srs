// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Package mock is the HTTP server that SRS calls back: the hooks, the
// heartbeat and the forward backend. It records every request and answers by
// script, routed by the action and stream of each request, so cases that share
// one mock script replies only for their own streams.
package mock

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"net/url"
	"strconv"
	"strings"
	"sync"
	"time"
)

// Request is one request the mock received.
type Request struct {
	// Time is when the request arrived, before it was answered.
	Time   time.Time
	Method string
	Path   string
	Query  url.Values
	Header http.Header
	Body   []byte
	// JSON is the body as a JSON object, nil when it is not one.
	JSON map[string]any
}

// Action is the hook action, such as on_publish: the "action" of the JSON
// body, or of the query for a GET such as on_hls_notify.
func (r Request) Action() string {
	return r.Field("action")
}

// Stream is the stream name of the JSON body, or of the query.
func (r Request) Stream() string {
	return r.Field("stream")
}

// Field returns a field of the JSON body as a string, or the query value of
// that name when the body has no such field. Numbers and booleans are
// formatted; a missing field is "".
func (r Request) Field(name string) string {
	if v, ok := r.JSON[name]; ok {
		switch v := v.(type) {
		case string:
			return v
		case nil:
			return ""
		case float64:
			return strconv.FormatFloat(v, 'f', -1, 64)
		case bool:
			return strconv.FormatBool(v)
		default:
			b, _ := json.Marshal(v)
			return string(b)
		}
	}
	return r.Query.Get(name)
}

// Match selects requests. An empty field matches anything, so the zero Match
// matches every request.
type Match struct {
	Action string
	Stream string
	Path   string
	// Func, when set, must also accept the request.
	Func func(Request) bool
}

// Matches reports whether the request matches.
func (m Match) Matches(r Request) bool {
	if m.Action != "" && r.Action() != m.Action {
		return false
	}
	if m.Stream != "" && r.Stream() != m.Stream {
		return false
	}
	if m.Path != "" && r.Path != m.Path {
		return false
	}
	return m.Func == nil || m.Func(r)
}

func (m Match) String() string {
	var parts []string
	if m.Action != "" {
		parts = append(parts, "action="+m.Action)
	}
	if m.Stream != "" {
		parts = append(parts, "stream="+m.Stream)
	}
	if m.Path != "" {
		parts = append(parts, "path="+m.Path)
	}
	if m.Func != nil {
		parts = append(parts, "func")
	}
	if len(parts) == 0 {
		return "any request"
	}
	return strings.Join(parts, " ")
}

// Reply is how the mock answers a request.
type Reply struct {
	// Status is the HTTP status, 200 when 0.
	Status int
	// Body is the response body.
	Body string
	// Delay is how long to wait before answering.
	Delay time.Duration
	// Hang never answers: the request waits until the client gives up or the
	// mock closes.
	Hang bool
}

// OK accepts, with HTTP 200 and code 0, the default reply.
func OK() Reply {
	return Reply{Body: `{"code":0}`}
}

// Reject answers HTTP 200 with a non-zero code, which a hook takes as a
// rejection.
func Reject(code int) Reply {
	return Reply{Body: `{"code":` + strconv.Itoa(code) + `}`}
}

// HTTPError answers with an HTTP error status.
func HTTPError(status int) Reply {
	return Reply{Status: status, Body: http.StatusText(status)}
}

// NoAnswer never answers.
func NoAnswer() Reply {
	return Reply{Hang: true}
}

// JSON answers HTTP 200 with v as JSON, such as the URLs of a forward backend.
func JSON(v any) Reply {
	b, err := json.Marshal(v)
	if err != nil {
		panic(fmt.Sprintf("mock: reply %#v: %v", v, err))
	}
	return Reply{Body: string(b)}
}

// After returns the reply, answered after d.
func (r Reply) After(d time.Duration) Reply {
	r.Delay = d
	return r
}

type script struct {
	match Match
	reply Reply
}

// Server is a running mock, an httptest server on the address it was started
// with.
type Server struct {
	ts     *httptest.Server
	closed chan struct{}
	once   sync.Once

	mu       sync.Mutex
	requests []Request
	scripts  []script
	// changed is closed and replaced when a request arrives or the mock closes.
	changed chan struct{}
}

// Start listens on addr, such as 127.0.0.1:14005, and serves.
func Start(addr string) (*Server, error) {
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		return nil, fmt.Errorf("mock: %w", err)
	}
	s := &Server{closed: make(chan struct{}), changed: make(chan struct{})}
	s.ts = httptest.NewUnstartedServer(http.HandlerFunc(s.serve))
	s.ts.Listener.Close()
	s.ts.Listener = ln
	s.ts.Config.ReadHeaderTimeout = 10 * time.Second
	s.ts.Start()
	return s, nil
}

// Addr is the host:port the mock listens on.
func (s *Server) Addr() string {
	return s.ts.Listener.Addr().String()
}

// URL returns the mock's URL of a path, such as /hooks/on_publish.
func (s *Server) URL(path string) string {
	return s.ts.URL + path
}

// Close stops the mock. Requests that hang or wait for a delayed reply are
// answered by closing their connections, never by a reply.
func (s *Server) Close() error {
	s.once.Do(func() {
		close(s.closed)
		// The httptest server's Close waits for every request, so the
		// connections close first, or a request that is never answered, or
		// whose body never ends, blocks it.
		s.ts.CloseClientConnections()
		s.ts.Close()
		s.mu.Lock()
		close(s.changed)
		s.changed = make(chan struct{})
		s.mu.Unlock()
	})
	return nil
}

// Script answers the requests that match with reply. The newest script that
// matches a request wins; a request no script matches gets OK.
func (s *Server) Script(m Match, reply Reply) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.scripts = append(s.scripts, script{m, reply})
}

// Requests returns the requests that match, in the order they arrived.
func (s *Server) Requests(m Match) []Request {
	s.mu.Lock()
	defer s.mu.Unlock()
	var out []Request
	for _, r := range s.requests {
		if m.Matches(r) {
			out = append(out, r)
		}
	}
	return out
}

// Wait returns the first request that matches, one that already arrived or
// the next one to come, or an error when ctx ends or the mock closes.
func (s *Server) Wait(ctx context.Context, m Match) (Request, error) {
	for next := 0; ; {
		s.mu.Lock()
		for ; next < len(s.requests); next++ {
			if r := s.requests[next]; m.Matches(r) {
				s.mu.Unlock()
				return r, nil
			}
		}
		changed, n := s.changed, len(s.requests)
		s.mu.Unlock()

		select {
		case <-s.closed:
			return Request{}, fmt.Errorf("mock closed, no %v in %d requests", m, n)
		case <-ctx.Done():
			return Request{}, fmt.Errorf("no %v in %d requests: %w", m, n, ctx.Err())
		case <-changed:
		}
	}
}

func (s *Server) serve(w http.ResponseWriter, req *http.Request) {
	body, _ := io.ReadAll(req.Body)
	r := Request{
		Time:   time.Now(),
		Method: req.Method,
		Path:   req.URL.Path,
		Query:  req.URL.Query(),
		Header: req.Header.Clone(),
		Body:   body,
	}
	var obj map[string]any
	if json.Unmarshal(body, &obj) == nil {
		r.JSON = obj
	}

	s.mu.Lock()
	s.requests = append(s.requests, r)
	close(s.changed)
	s.changed = make(chan struct{})
	reply := OK()
	for i := len(s.scripts) - 1; i >= 0; i-- {
		if s.scripts[i].match.Matches(r) {
			reply = s.scripts[i].reply
			break
		}
	}
	s.mu.Unlock()

	// A request the mock never answers ends with its connection closed: a
	// handler that returns without a reply would send an empty 200.
	if reply.Hang {
		select {
		case <-req.Context().Done():
		case <-s.closed:
		}
		panic(http.ErrAbortHandler)
	}
	if reply.Delay > 0 {
		select {
		case <-time.After(reply.Delay):
		case <-req.Context().Done():
			panic(http.ErrAbortHandler)
		case <-s.closed:
			panic(http.ErrAbortHandler)
		}
	}

	status := reply.Status
	if status == 0 {
		status = http.StatusOK
	}
	// The mock closes first, so TIME_WAIT is on its port, not on an ephemeral
	// port of the server, which opens a connection per hook.
	w.Header().Set("Connection", "close")
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Content-Length", strconv.Itoa(len(reply.Body)))
	w.WriteHeader(status)
	io.WriteString(w, reply.Body)
}
