// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package mock

import (
	"context"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"strings"
	"sync"
	"testing"
	"time"
)

func start(t *testing.T) *Server {
	t.Helper()
	s, err := Start("127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { s.Close() })
	return s
}

// post sends a hook request like SRS does and returns the status and body.
func post(t *testing.T, s *Server, path string, body any) (int, string) {
	t.Helper()
	b, _ := json.Marshal(body)
	res, err := http.Post(s.URL(path), "application/json", strings.NewReader(string(b)))
	if err != nil {
		t.Fatal(err)
	}
	defer res.Body.Close()
	data, _ := io.ReadAll(res.Body)
	return res.StatusCode, string(data)
}

func hook(action, stream string) map[string]any {
	return map[string]any{"action": action, "client_id": "c1", "ip": "127.0.0.1", "vhost": "__defaultVhost__",
		"app": "live", "stream": stream, "param": "?k=v"}
}

func TestRecordsRequests(t *testing.T) {
	s := start(t)
	post(t, s, "/hooks/on_publish", hook("on_publish", "s1"))

	reqs := s.Requests(Match{})
	if len(reqs) != 1 {
		t.Fatalf("got %d requests, want 1", len(reqs))
	}
	r := reqs[0]
	if r.Method != http.MethodPost || r.Path != "/hooks/on_publish" {
		t.Errorf("got %s %s, want POST /hooks/on_publish", r.Method, r.Path)
	}
	if got := r.Header.Get("Content-Type"); got != "application/json" {
		t.Errorf("content type %q", got)
	}
	if r.Action() != "on_publish" || r.Stream() != "s1" || r.Field("param") != "?k=v" || r.Field("app") != "live" {
		t.Errorf("action %q stream %q param %q app %q", r.Action(), r.Stream(), r.Field("param"), r.Field("app"))
	}
	if r.JSON == nil || r.JSON["client_id"] != "c1" {
		t.Errorf("JSON %v", r.JSON)
	}
	if !strings.Contains(string(r.Body), `"action":"on_publish"`) {
		t.Errorf("body %q", r.Body)
	}
	if r.Time.IsZero() {
		t.Error("no time")
	}
}

func TestNonJSONBody(t *testing.T) {
	s := start(t)
	res, err := http.Post(s.URL("/x"), "text/plain", strings.NewReader("hello"))
	if err != nil {
		t.Fatal(err)
	}
	res.Body.Close()
	r := s.Requests(Match{Path: "/x"})
	if len(r) != 1 || r[0].JSON != nil || string(r[0].Body) != "hello" || r[0].Action() != "" {
		t.Errorf("requests %+v", r)
	}
}

func TestDefaultReplyIsOK(t *testing.T) {
	s := start(t)
	status, body := post(t, s, "/hooks/on_play", hook("on_play", "s1"))
	if status != http.StatusOK || body != `{"code":0}` {
		t.Errorf("got %d %q, want 200 {\"code\":0}", status, body)
	}
}

func TestScriptByStream(t *testing.T) {
	s := start(t)
	s.Script(Match{Action: "on_publish", Stream: "a"}, Reject(403))

	if status, body := post(t, s, "/hooks/on_publish", hook("on_publish", "a")); status != 200 || body != `{"code":403}` {
		t.Errorf("stream a: %d %q, want 200 {\"code\":403}", status, body)
	}
	if _, body := post(t, s, "/hooks/on_publish", hook("on_publish", "b")); body != `{"code":0}` {
		t.Errorf("stream b: %q, want ok", body)
	}
	if _, body := post(t, s, "/hooks/on_play", hook("on_play", "a")); body != `{"code":0}` {
		t.Errorf("on_play of a: %q, want ok", body)
	}
}

func TestScriptNewestWins(t *testing.T) {
	s := start(t)
	s.Script(Match{Stream: "a"}, Reject(1))
	s.Script(Match{Stream: "a"}, Reject(2))
	if _, body := post(t, s, "/h", hook("on_publish", "a")); body != `{"code":2}` {
		t.Errorf("got %q, want the newest script", body)
	}
}

func TestScriptFunc(t *testing.T) {
	s := start(t)
	s.Script(Match{Func: func(r Request) bool { return r.Field("param") == "?deny" }}, Reject(5))
	m := hook("on_connect", "")
	m["param"] = "?deny"
	if _, body := post(t, s, "/h", m); body != `{"code":5}` {
		t.Errorf("got %q, want the Func script", body)
	}
	if _, body := post(t, s, "/h", hook("on_connect", "")); body != `{"code":0}` {
		t.Errorf("got %q, want ok", body)
	}
}

func TestHTTPError(t *testing.T) {
	s := start(t)
	s.Script(Match{Stream: "a"}, HTTPError(500))
	if status, _ := post(t, s, "/h", hook("on_publish", "a")); status != 500 {
		t.Errorf("status %d, want 500", status)
	}
}

func TestJSONReply(t *testing.T) {
	s := start(t)
	s.Script(Match{Action: "on_forward"}, JSON(map[string]any{"code": 0, "data": map[string]any{"urls": []string{"rtmp://x/live/s"}}}))
	_, body := post(t, s, "/api/v1/forward", hook("on_forward", "a"))
	if body != `{"code":0,"data":{"urls":["rtmp://x/live/s"]}}` {
		t.Errorf("body %q", body)
	}
}

func TestDelay(t *testing.T) {
	s := start(t)
	s.Script(Match{Stream: "a"}, Reject(7).After(100*time.Millisecond))
	begin := time.Now()
	_, body := post(t, s, "/h", hook("on_publish", "a"))
	if d := time.Since(begin); d < 100*time.Millisecond {
		t.Errorf("answered in %v, want at least 100ms", d)
	}
	if body != `{"code":7}` {
		t.Errorf("body %q", body)
	}
	// The request is recorded when it arrives, not when it is answered.
	r := s.Requests(Match{Stream: "a"})
	if len(r) != 1 || r[0].Time.Sub(begin) > 50*time.Millisecond {
		t.Errorf("requests %v, want one recorded at once", r)
	}
}

func TestNoAnswer(t *testing.T) {
	s := start(t)
	s.Script(Match{Stream: "a"}, NoAnswer())

	client := &http.Client{Timeout: 100 * time.Millisecond}
	_, err := client.Post(s.URL("/h"), "application/json", strings.NewReader(`{"action":"on_publish","stream":"a"}`))
	if err == nil {
		t.Fatal("got an answer, want none")
	}
	if len(s.Requests(Match{Stream: "a"})) != 1 {
		t.Error("the request is not recorded")
	}

	// Close does not wait for a hanging request.
	go client.Post(s.URL("/h"), "application/json", strings.NewReader(`{"action":"on_publish","stream":"a"}`))
	if _, err := s.Wait(ctxTimeout(t, time.Second), Match{Stream: "a", Func: func(Request) bool { return true }}); err != nil {
		t.Fatal(err)
	}
	begin := time.Now()
	s.Close()
	if d := time.Since(begin); d > 500*time.Millisecond {
		t.Errorf("Close took %v", d)
	}
}

func TestQueryRouting(t *testing.T) {
	s := start(t)
	s.Script(Match{Action: "on_hls_notify", Stream: "a"}, HTTPError(404))
	res, err := http.Get(s.URL("/hooks/on_hls_notify?action=on_hls_notify&app=live&stream=a&ts_url=live/a-1.ts"))
	if err != nil {
		t.Fatal(err)
	}
	res.Body.Close()
	if res.StatusCode != 404 {
		t.Errorf("status %d, want the script's 404", res.StatusCode)
	}
	r := s.Requests(Match{Action: "on_hls_notify"})
	if len(r) != 1 || r[0].Method != http.MethodGet || r[0].Stream() != "a" || r[0].Field("ts_url") != "live/a-1.ts" {
		t.Errorf("requests %+v", r)
	}
}

func TestHeartbeat(t *testing.T) {
	s := start(t)
	post(t, s, "/api/v1/heartbeat", map[string]any{"device_id": "d1", "ip": "127.0.0.1"})
	r := s.Requests(Match{Path: "/api/v1/heartbeat"})
	if len(r) != 1 || r[0].Field("device_id") != "d1" {
		t.Errorf("requests %+v", r)
	}
	if len(s.Requests(Match{Path: "/other"})) != 0 {
		t.Error("the path match matches another path")
	}
}

func TestRequestsInOrder(t *testing.T) {
	s := start(t)
	for _, a := range []string{"on_connect", "on_publish", "on_unpublish", "on_close"} {
		post(t, s, "/h", hook(a, "a"))
	}
	post(t, s, "/h", hook("on_publish", "b"))
	var got []string
	for _, r := range s.Requests(Match{Stream: "a"}) {
		got = append(got, r.Action())
	}
	if strings.Join(got, ",") != "on_connect,on_publish,on_unpublish,on_close" {
		t.Errorf("got %v", got)
	}
}

func ctxTimeout(t *testing.T, d time.Duration) context.Context {
	ctx, cancel := context.WithTimeout(context.Background(), d)
	t.Cleanup(cancel)
	return ctx
}

func TestWaitForExisting(t *testing.T) {
	s := start(t)
	post(t, s, "/h", hook("on_unpublish", "a"))
	r, err := s.Wait(ctxTimeout(t, time.Second), Match{Action: "on_unpublish", Stream: "a"})
	if err != nil || r.Stream() != "a" {
		t.Errorf("got %+v %v", r, err)
	}
}

func TestWaitForLater(t *testing.T) {
	s := start(t)
	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		defer wg.Done()
		time.Sleep(50 * time.Millisecond)
		post(t, s, "/h", hook("on_publish", "a"))
		post(t, s, "/h", hook("on_unpublish", "a"))
	}()
	r, err := s.Wait(ctxTimeout(t, 2*time.Second), Match{Action: "on_unpublish", Stream: "a"})
	wg.Wait()
	if err != nil || r.Action() != "on_unpublish" {
		t.Errorf("got %+v %v", r, err)
	}
}

func TestWaitTimeout(t *testing.T) {
	s := start(t)
	post(t, s, "/h", hook("on_publish", "a"))
	_, err := s.Wait(ctxTimeout(t, 50*time.Millisecond), Match{Action: "on_unpublish", Stream: "a"})
	if err == nil {
		t.Fatal("no error")
	}
	if !errors.Is(err, context.DeadlineExceeded) || !strings.Contains(err.Error(), "on_unpublish") || !strings.Contains(err.Error(), "1 requests") {
		t.Errorf("error %q, want the match, the request count and the deadline", err)
	}
}

func TestWaitAfterClose(t *testing.T) {
	s := start(t)
	s.Close()
	if _, err := s.Wait(ctxTimeout(t, time.Second), Match{Action: "on_publish"}); err == nil || !strings.Contains(err.Error(), "closed") {
		t.Errorf("error %v, want closed", err)
	}
}

func TestMatchString(t *testing.T) {
	if got := (Match{Action: "on_publish", Stream: "a"}).String(); got != "action=on_publish stream=a" {
		t.Errorf("got %q", got)
	}
	if got := (Match{}).String(); got != "any request" {
		t.Errorf("got %q", got)
	}
}

// TestRepliesCloseTheConnection checks the mock closes each connection after
// its reply, so the closed socket waits out TIME_WAIT on the mock's port, not
// on an ephemeral port of the server that calls it. SRS opens a connection
// per hook, and a run of many cases would use up the ephemeral ports.
func TestRepliesCloseTheConnection(t *testing.T) {
	s := start(t)
	res, err := http.Post(s.URL("/hooks/on_play"), "application/json", strings.NewReader(`{"action":"on_play"}`))
	if err != nil {
		t.Fatal(err)
	}
	io.Copy(io.Discard, res.Body)
	res.Body.Close()
	if !res.Close {
		t.Error("the reply keeps the connection, want the mock to close it")
	}
}
