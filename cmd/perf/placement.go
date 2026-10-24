package main

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strconv"
	"strings"
	"time"
)

// originAPIBase is origin1's HTTP API port in the proxy's network namespace;
// origin i's is 10 above origin i-1's, as in cmd/perf/env/proxy/origin<i>.env.
const originAPIBase = 19851

// originAPI is an origin's HTTP API and its pid, which its API must answer
// with, so a port that drifted from its env file is caught.
type originAPI struct {
	url string
	pid int
}

// originAPIs gives the APIs of the origins found by findOrigins, origin1 first.
func originAPIs(pids []int) []originAPI {
	apis := make([]originAPI, len(pids))
	for i, pid := range pids {
		apis[i] = originAPI{url: fmt.Sprintf("http://127.0.0.1:%d", originAPIBase+10*i), pid: pid}
	}
	return apis
}

// originShare is what one origin carries in a proxy step: a player scenario's
// one stream and its players, or a count of the step's publisher streams.
type originShare struct {
	Origin  int    `json:"origin"`
	Stream  string `json:"stream,omitempty"`
	Streams int    `json:"streams"`
	Players int    `json:"players,omitempty"`
}

// originStreams lists the streams published on an origin. SRS's API gives 10
// streams unless asked for more, and keeps a stream after its publisher left.
func originStreams(o originAPI) ([]string, error) {
	client := http.Client{Timeout: 5 * time.Second}
	resp, err := client.Get(o.url + "/api/v1/streams/?count=1000000")
	if err != nil {
		return nil, fmt.Errorf("origin streams: %w", err)
	}
	defer resp.Body.Close()
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("origin streams: %w", err)
	}
	var v struct {
		PID     string `json:"pid"`
		Streams []struct {
			Name    string `json:"name"`
			Publish struct {
				Active bool `json:"active"`
			} `json:"publish"`
		} `json:"streams"`
	}
	if err := json.Unmarshal(body, &v); err != nil {
		return nil, fmt.Errorf("origin streams: %s answers %q: %w", o.url, strings.TrimSpace(string(body)), err)
	}
	if pid, err := strconv.Atoi(v.PID); err != nil || pid != o.pid {
		return nil, fmt.Errorf("origin streams: %s answers pid %q, not the origin's pid %d", o.url, v.PID, o.pid)
	}
	var names []string
	for _, s := range v.Streams {
		if s.Publish.Active {
			names = append(names, s.Name)
		}
	}
	return names, nil
}

// locateStream finds the origin a stream was published on, waiting for it to
// show up there for up to timeout.
func locateStream(origins []originAPI, name string, timeout time.Duration) (int, error) {
	deadline := time.Now().Add(timeout)
	for {
		for i, o := range origins {
			names, err := originStreams(o)
			if err != nil {
				return 0, err
			}
			for _, n := range names {
				if n == name {
					return i, nil
				}
			}
		}
		if time.Now().After(deadline) {
			return 0, fmt.Errorf("stream %s is on no origin after %v", name, timeout)
		}
		time.Sleep(100 * time.Millisecond)
	}
}

// placedStream is a publisher of a step's stream, with its clock for the
// players' lag.
type placedStream struct {
	name  string
	clock *mediaClock
	stop  func() error
}

// placeStreams publishes one stream on each of n origins. The proxy's memory
// load balancer picks a random origin for each new stream, so a stream that
// lands on an origin that has one already is stopped and published again
// under a new name, at most ten tries per origin. The streams are returned in
// origin order.
func placeStreams(n int, prefix string, publish func(name string) (placedStream, error), locate func(name string) (int, error)) ([]placedStream, error) {
	placed := make([]placedStream, n)
	stopAll := func() {
		for _, s := range placed {
			if s.stop != nil {
				s.stop()
			}
		}
	}
	filled := 0
	for try := 0; filled < n; try++ {
		if try >= 10*n {
			stopAll()
			return nil, fmt.Errorf("no stream on each of %d origins after %d tries", n, try)
		}
		s, err := publish(fmt.Sprintf("%s-%d", prefix, try))
		if err != nil {
			stopAll()
			return nil, err
		}
		origin, err := locate(s.name)
		if err != nil {
			s.stop()
			stopAll()
			return nil, err
		}
		if origin < 0 || origin >= n || placed[origin].stop != nil {
			s.stop()
			continue
		}
		placed[origin] = s
		filled++
	}
	return placed, nil
}

// startStepStreams publishes a player step's streams: one, named stream, on
// SRS, or one on each origin behind the proxy, named <stream>-<try>.
func startStepStreams(kind, stream, media string, tags []rtmpMessage, origins []originAPI) ([]placedStream, error) {
	publish := func(name string) (placedStream, error) {
		clock := &mediaClock{}
		stop := make(chan struct{})
		done, err := startPublisher(kind, name, media, tags, clock, stop)
		if err != nil {
			close(stop)
			return placedStream{}, err
		}
		return placedStream{name: name, clock: clock, stop: func() error { close(stop); return <-done }}, nil
	}
	if len(origins) == 0 {
		s, err := publish(stream)
		if err != nil {
			return nil, err
		}
		return []placedStream{s}, nil
	}
	locate := func(name string) (int, error) { return locateStream(origins, name, 5*time.Second) }
	return placeStreams(len(origins), stream, publish, locate)
}

// streamNames and streamTimestamps are the placed streams' names, and their
// publishers' newest timestamps, by the players' stream index.
func streamNames(streams []placedStream) []string {
	names := make([]string, len(streams))
	for i, s := range streams {
		names[i] = s.name
	}
	return names
}

func streamTimestamps(streams []placedStream) []uint32 {
	timestamps := make([]uint32, len(streams))
	for i, s := range streams {
		timestamps[i] = s.clock.get()
	}
	return timestamps
}

// stopStreams stops the publishers and gives the first one's error.
func stopStreams(streams []placedStream) error {
	var first error
	for _, s := range streams {
		if err := s.stop(); err != nil && first == nil {
			first = err
		}
	}
	return first
}

// playerStreamIndex is the stream of each of a worker's n players, the
// worker's first being client first of the step: client i plays stream i
// modulo the streams, so the streams' players differ by at most one.
func playerStreamIndex(first, n, streams int) []int {
	index := make([]int, n)
	for i := range index {
		index[i] = (first + i) % streams
	}
	return index
}

// playerShares is each origin's stream, in origin order, and its players.
func playerShares(streams []string, clients int) []originShare {
	shares := make([]originShare, len(streams))
	for i, name := range streams {
		shares[i] = originShare{Origin: i + 1, Stream: name, Streams: 1, Players: clients / len(streams)}
		if i < clients%len(streams) {
			shares[i].Players++
		}
	}
	return shares
}

// targetShares is what each origin behind the proxy carries in a step: a
// player step's placed streams and their players, or a publisher or
// many-streams step's streams named by its prefix; nil on SRS, or with no
// publisher.
func targetShares(s scenario, origins []originAPI, published []placedStream, prefix string, clients int) ([]originShare, error) {
	if len(origins) == 0 {
		return nil, nil
	}
	if s.Unit != "clients" {
		return streamShares(origins, prefix)
	}
	if len(published) == 0 {
		return nil, nil
	}
	return playerShares(streamNames(published), clients), nil
}

// streamShares counts each origin's published streams of a publisher or
// many-streams step, whose client i publishes <prefix>-<i>.
func streamShares(origins []originAPI, prefix string) ([]originShare, error) {
	shares := make([]originShare, len(origins))
	for i, o := range origins {
		names, err := originStreams(o)
		if err != nil {
			return nil, err
		}
		shares[i].Origin = i + 1
		for _, name := range names {
			if index, ok := strings.CutPrefix(name, prefix+"-"); ok {
				if _, err := strconv.Atoi(index); err == nil {
					shares[i].Streams++
				}
			}
		}
	}
	return shares, nil
}
