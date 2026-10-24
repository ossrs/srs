package main

import (
	"errors"
	"fmt"
	"net/http"
	"net/http/httptest"
	"reflect"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

// Origin i's HTTP API is 10 ports above origin i-1's, as in its env file, and
// is checked against the origin's pid in the shared pid namespace.
func TestOriginAPIs(t *testing.T) {
	got := originAPIs([]int{7, 9, 12})
	want := []originAPI{{url: "http://127.0.0.1:19851", pid: 7}, {url: "http://127.0.0.1:19861", pid: 9}, {url: "http://127.0.0.1:19871", pid: 12}}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("origin APIs = %+v, want %+v", got, want)
	}
}

func fakeOrigin(t *testing.T, pid string, streams func() string) *httptest.Server {
	t.Helper()
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/api/v1/streams/" || r.URL.Query().Get("count") == "" {
			http.NotFound(w, r)
			return
		}
		fmt.Fprintf(w, `{"code":0,"server":"vid-1","pid":%q,"streams":[%s]}`, pid, streams())
	}))
	t.Cleanup(server.Close)
	return server
}

func stream(name string, active bool) string {
	return fmt.Sprintf(`{"name":%q,"app":"live","publish":{"active":%v}}`, name, active)
}

// An origin's streams are its active ones, read from its HTTP API; an API
// that answers with another pid is not that origin.
func TestOriginStreams(t *testing.T) {
	server := fakeOrigin(t, "42", func() string { return stream("a", true) + "," + stream("b", false) })
	got, err := originStreams(originAPI{url: server.URL, pid: 42})
	if err != nil || !reflect.DeepEqual(got, []string{"a"}) {
		t.Fatalf("streams = %v, err = %v; want [a]", got, err)
	}
	if _, err := originStreams(originAPI{url: server.URL, pid: 43}); err == nil || !strings.Contains(err.Error(), "pid") {
		t.Fatalf("err = %v, want a pid mismatch", err)
	}
}

// A stream is on the origin whose API lists it as published, once it shows
// up; one that never does is an error.
func TestLocateStream(t *testing.T) {
	var polls atomic.Int32
	first := fakeOrigin(t, "1", func() string { return stream("other", true) })
	second := fakeOrigin(t, "2", func() string {
		if polls.Add(1) < 3 {
			return ""
		}
		return stream("perf-run-1", true)
	})
	origins := []originAPI{{url: first.URL, pid: 1}, {url: second.URL, pid: 2}}
	if got, err := locateStream(origins, "perf-run-1", 5*time.Second); err != nil || got != 1 {
		t.Fatalf("origin = %d, err = %v; want 1", got, err)
	}
	if _, err := locateStream(origins, "perf-run-2", 300*time.Millisecond); err == nil {
		t.Fatal("a stream on no origin was located")
	}
}

func fakePlacement(at map[string]int, err error) (func(string) (placedStream, error), func(string) (int, error), *[]string) {
	var stopped []string
	publish := func(name string) (placedStream, error) {
		return placedStream{name: name, clock: &mediaClock{}, stop: func() error { stopped = append(stopped, name); return nil }}, nil
	}
	locate := func(name string) (int, error) {
		if err != nil {
			return 0, err
		}
		origin, ok := at[name]
		if !ok {
			return 0, errors.New("not placed")
		}
		return origin, nil
	}
	return publish, locate, &stopped
}

// The proxy places each new stream on a random origin, so a stream that lands
// on an origin that has one already is stopped and published again under a
// new name, until every origin has exactly one; the streams come in origin
// order.
func TestPlaceStreams(t *testing.T) {
	publish, locate, stopped := fakePlacement(map[string]int{"p-0": 0, "p-1": 0, "p-2": 2, "p-3": 1}, nil)
	got, err := placeStreams(3, "p", publish, locate)
	if err != nil {
		t.Fatal(err)
	}
	var names []string
	for _, s := range got {
		names = append(names, s.name)
	}
	if !reflect.DeepEqual(names, []string{"p-0", "p-3", "p-2"}) || !reflect.DeepEqual(*stopped, []string{"p-1"}) {
		t.Fatalf("streams = %v, stopped = %v; want [p-0 p-3 p-2], [p-1]", names, *stopped)
	}
}

// Placing gives up after ten tries per origin, and stops every stream it
// published.
func TestPlaceStreamsGivesUp(t *testing.T) {
	at := map[string]int{}
	for i := 0; i < 40; i++ {
		at[fmt.Sprintf("p-%d", i)] = 0
	}
	publish, locate, stopped := fakePlacement(at, nil)
	if _, err := placeStreams(2, "p", publish, locate); err == nil {
		t.Fatal("two origins were filled by streams that all land on one")
	}
	if len(*stopped) != 20 {
		t.Fatalf("stopped %d streams, want all 20 tries", len(*stopped))
	}
	publish, locate, stopped = fakePlacement(nil, errors.New("no origin API"))
	if _, err := placeStreams(2, "p", publish, locate); err == nil || len(*stopped) != 1 {
		t.Fatalf("err = %v, stopped = %v; want the error and the one stream stopped", err, *stopped)
	}
}

// Players split evenly over the streams by their index in the step, so the
// origins' counts differ by at most one, whatever the workers' shares.
func TestPlayerStreamsSplitEvenly(t *testing.T) {
	if got := playerStreamIndex(1, 4, 3); !reflect.DeepEqual(got, []int{1, 2, 0, 1}) {
		t.Fatalf("index = %v, want [1 2 0 1]", got)
	}
	counts := make([]int, 3)
	shares := splitClients(100, 8)
	for w, first := range clientOffsets(shares) {
		for _, i := range playerStreamIndex(first, shares[w], 3) {
			counts[i]++
		}
	}
	if !reflect.DeepEqual(counts, []int{34, 33, 33}) {
		t.Fatalf("players per stream = %v, want [34 33 33]", counts)
	}
	got := playerShares([]string{"s-0", "s-3", "s-2"}, 100)
	want := []originShare{{Origin: 1, Stream: "s-0", Streams: 1, Players: 34}, {Origin: 2, Stream: "s-3", Streams: 1, Players: 33}, {Origin: 3, Stream: "s-2", Streams: 1, Players: 33}}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("shares = %+v, want %+v", got, want)
	}
}

// The publisher and many-streams scenarios have a stream per client, placed
// at random; each origin's share is its count of the step's active streams.
func TestCountStreams(t *testing.T) {
	first := fakeOrigin(t, "1", func() string {
		return stream("perf-5-0", true) + "," + stream("perf-5-2", true) + "," + stream("perf-4-1", true) + "," + stream("perf-5-3", false)
	})
	second := fakeOrigin(t, "2", func() string { return stream("perf-5-1", true) + "," + stream("perf-50-1", true) })
	got, err := streamShares([]originAPI{{url: first.URL, pid: 1}, {url: second.URL, pid: 2}}, "perf-5")
	want := []originShare{{Origin: 1, Streams: 2}, {Origin: 2, Streams: 1}}
	if err != nil || !reflect.DeepEqual(got, want) {
		t.Fatalf("shares = %+v, err = %v; want %+v", got, err, want)
	}
}
