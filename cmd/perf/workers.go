package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"os/exec"
	"strconv"
	"sync"
	"sync/atomic"
	"time"
)

type workerRequest struct {
	Kind     string `json:"kind"`
	Scenario string `json:"scenario,omitempty"`
	Clients  int    `json:"clients,omitempty"`
	// Media is the source the publisher loops; players measure lag by it.
	Media string `json:"media,omitempty"`
	// Streams are the streams players split over, or one, the prefix of the
	// publishers' streams; First is the index in the step of the worker's first
	// client.
	Streams []string `json:"streams,omitempty"`
	First   int      `json:"first,omitempty"`
	Second  int      `json:"second,omitempty"`
	// LatestTimestamps is each stream's publisher's newest timestamp.
	LatestTimestamps []uint32 `json:"latest_timestamps,omitempty"`
	// Final marks the step's last sample: a player with no frame yet fails.
	Final bool `json:"final,omitempty"`
}

type workerResponse struct {
	Frames    int64 `json:"frames"`
	Segments  int64 `json:"segments,omitempty"`
	Bytes     int64 `json:"bytes"`
	LagMillis int64 `json:"lag_millis"`
	Failures  int64 `json:"failures"`
}

type workerClient struct {
	cmd   *exec.Cmd
	input io.WriteCloser
	enc   *json.Encoder
	dec   *json.Decoder
}

type workerGroup struct {
	workers []*workerClient
}

func splitClients(clients, cores int) []int {
	if clients < 0 || cores <= 0 {
		return nil
	}
	shares := make([]int, cores)
	for i := range shares {
		shares[i] = clients / cores
		if i < clients%cores {
			shares[i]++
		}
	}
	return shares
}

func startWorkers(ctx context.Context, factory func(int) *exec.Cmd, scenario, media string, streams []string, clients, cores int) (*workerGroup, error) {
	shares := splitClients(clients, cores)
	if shares == nil {
		return nil, fmt.Errorf("clients must be nonnegative and cores positive")
	}
	offsets := clientOffsets(shares)
	if _, ok := scenarioByName(scenario); !ok {
		return nil, fmt.Errorf("unknown scenario %q", scenario)
	}
	if factory == nil {
		factory = func(id int) *exec.Cmd {
			return exec.CommandContext(ctx, os.Args[0], "worker", "-id", strconv.Itoa(id))
		}
	}
	group := &workerGroup{}
	for id, share := range shares {
		if err := ctx.Err(); err != nil {
			group.close()
			return nil, err
		}
		cmd := factory(id)
		input, err := cmd.StdinPipe()
		if err != nil {
			group.close()
			return nil, err
		}
		output, err := cmd.StdoutPipe()
		if err != nil {
			input.Close()
			group.close()
			return nil, err
		}
		cmd.Stderr = os.Stderr
		if err := cmd.Start(); err != nil {
			input.Close()
			group.close()
			return nil, fmt.Errorf("start worker %d: %w", id, err)
		}
		worker := &workerClient{cmd: cmd, input: input, enc: json.NewEncoder(input), dec: json.NewDecoder(output)}
		group.workers = append(group.workers, worker)
		if err := worker.enc.Encode(workerRequest{Kind: "start", Scenario: scenario, Media: media, Streams: streams, First: offsets[id], Clients: share}); err != nil {
			group.close()
			return nil, fmt.Errorf("configure worker %d: %w", id, err)
		}
		var ack workerResponse
		if err := worker.dec.Decode(&ack); err != nil {
			group.close()
			return nil, fmt.Errorf("ack worker %d: %w", id, err)
		}
	}
	return group, nil
}

func (g *workerGroup) pids() []int {
	pids := make([]int, len(g.workers))
	for i, worker := range g.workers {
		pids[i] = worker.cmd.Process.Pid
	}
	return pids
}

func (g *workerGroup) collect(second int) (workerResponse, error) {
	return g.collectWithTimestamp(second, nil, false)
}

func (g *workerGroup) collectWithTimestamp(second int, timestamps []uint32, final bool) (workerResponse, error) {
	if second <= 0 {
		return workerResponse{}, fmt.Errorf("sample second must be positive")
	}
	for i, worker := range g.workers {
		if err := worker.enc.Encode(workerRequest{Kind: "sample", Second: second, LatestTimestamps: timestamps, Final: final}); err != nil {
			return workerResponse{}, fmt.Errorf("request worker %d: %w", i, err)
		}
	}
	var total workerResponse
	for i, worker := range g.workers {
		var response workerResponse
		if err := worker.dec.Decode(&response); err != nil {
			return workerResponse{}, fmt.Errorf("read worker %d: %w", i, err)
		}
		total.Frames += response.Frames
		total.Segments += response.Segments
		total.Bytes += response.Bytes
		total.Failures += response.Failures
		if response.LagMillis > total.LagMillis {
			total.LagMillis = response.LagMillis
		}
	}
	return total, nil
}

func (g *workerGroup) collectEverySecond(ctx context.Context, output chan<- workerResponse) error {
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()
	second := 0
	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-ticker.C:
			second++
			response, err := g.collect(second)
			if err != nil {
				return err
			}
			select {
			case output <- response:
			case <-ctx.Done():
				return ctx.Err()
			}
		}
	}
}

func (g *workerGroup) close() error {
	var first error
	for _, worker := range g.workers {
		if err := worker.enc.Encode(workerRequest{Kind: "stop"}); err != nil && first == nil {
			first = err
		}
		worker.input.Close()
	}
	for _, worker := range g.workers {
		if err := worker.cmd.Wait(); err != nil && first == nil {
			first = err
		}
	}
	g.workers = nil
	return first
}

// classifyWorkerLoad marks a failed step load-bound when, in one second, the
// workers average above 95% CPU: the load cpuset is pegged. A worker is a Go
// process that runs on every core of the cpuset, so one worker above 100% alone
// is not a pegged load.
func classifyWorkerLoad(failed bool, cpus []float64) validity {
	if !failed || len(cpus) == 0 {
		return validity{Valid: true}
	}
	var total float64
	for _, cpu := range cpus {
		total += cpu
	}
	if total/float64(len(cpus)) > 95 {
		return validity{Valid: false, Reason: "load-bound"}
	}
	return validity{Valid: true}
}

func serveWorker(input io.Reader, output io.Writer) error {
	dec := json.NewDecoder(input)
	enc := json.NewEncoder(output)
	var start workerRequest
	if err := dec.Decode(&start); err != nil {
		return err
	}
	if start.Kind != "start" || start.Clients < 0 {
		return fmt.Errorf("invalid worker start request")
	}
	s, ok := scenarioByName(start.Scenario)
	if !ok {
		return fmt.Errorf("unknown scenario %q", start.Scenario)
	}
	var tags []rtmpMessage
	var timeline *sourceTimeline
	if start.Media != "" {
		var err error
		if tags, err = loadFLV(start.Media); err != nil {
			return err
		}
		if timeline, err = lagTimeline(start.Media, tags); err != nil {
			return err
		}
	}
	streams := start.Streams
	if len(streams) == 0 {
		streams = []string{"perf-run"}
	}
	runtime, err := startClients(s, start.Clients, streams, start.First, start.Media, tags, timeline)
	if err != nil {
		return err
	}
	defer runtime.close()
	if err := enc.Encode(workerResponse{}); err != nil {
		return err
	}
	for {
		var request workerRequest
		if err := dec.Decode(&request); err != nil {
			return err
		}
		switch request.Kind {
		case "sample":
			if request.Second <= 0 {
				return fmt.Errorf("sample second must be positive")
			}
			if err := enc.Encode(runtime.sample(request.LatestTimestamps, request.Final)); err != nil {
				return err
			}
		case "stop":
			return nil
		default:
			return fmt.Errorf("unknown worker request %q", request.Kind)
		}
	}
}

type workerPlayers struct {
	stop    chan struct{}
	started time.Time
	// gap is how long a player may get nothing before it fails, its
	// scenario's gap.
	gap      time.Duration
	wg       sync.WaitGroup
	frames   atomic.Int64
	segments atomic.Int64
	bytes    atomic.Int64
	failures atomic.Int64
	last     []atomic.Int64
	latest   []atomic.Uint32
	timeline *sourceTimeline
	// anchor is 1 + the time in the source loop of a player's first video frame
	// with a unique payload, 0 until it has one; anchorTS is that frame's
	// timestamp as the player got it.
	anchor   []atomic.Int64
	anchorTS []atomic.Uint32
	// offset turns a player's timestamps into publisher time; only sample uses
	// it, once the player has an anchor.
	offset   []int64
	resolved []bool
	// clocks is each player's own publisher, in a many-streams step; nil
	// when the players play the publishers whose times sample gets.
	clocks []*mediaClock
	// streams is the index of each player's stream in sample's timestamps;
	// nil when every player plays the first.
	streams []int
}

func newWorkerPlayers(clients int, timeline *sourceTimeline) *workerPlayers {
	return &workerPlayers{
		stop: make(chan struct{}), started: time.Now(), gap: 5 * time.Second, timeline: timeline,
		last: make([]atomic.Int64, clients), latest: make([]atomic.Uint32, clients),
		anchor: make([]atomic.Int64, clients), anchorTS: make([]atomic.Uint32, clients),
		offset: make([]int64, clients), resolved: make([]bool, clients),
	}
}

// segmentMessage is the type of a player's report that a whole segment came,
// after its frames; it is not media.
const segmentMessage byte = 0xff

// received counts a player's message, keeps its newest timestamp, and anchors
// the player to the source at its first video frame with a unique payload.
func (w *workerPlayers) received(id int, m rtmpMessage) {
	if m.typeID == segmentMessage {
		w.segments.Add(1)
		return
	}
	w.frames.Add(1)
	w.bytes.Add(int64(len(m.payload)))
	w.latest[id].Store(m.timestamp)
	if w.anchor[id].Load() == 0 {
		if at, ok := w.timeline.inLoop(m); ok {
			w.anchorTS[id].Store(m.timestamp)
			w.anchor[id].Store(int64(at) + 1)
		}
	}
	w.last[id].Store(time.Now().UnixNano())
}

// lag is how far a player's newest message is behind the publisher's newest
// one, both in publisher time; false until the player has an anchor.
func (w *workerPlayers) lag(id int, publisherTimestamp uint32) (int64, bool) {
	if !w.resolved[id] {
		anchor := w.anchor[id].Load()
		if anchor == 0 || publisherTimestamp == 0 {
			return 0, false
		}
		at := w.timeline.publisherTime(uint32(anchor-1), publisherTimestamp)
		w.offset[id] = at - int64(w.anchorTS[id].Load())
		w.resolved[id] = true
	}
	return int64(publisherTimestamp) - (int64(w.latest[id].Load()) + w.offset[id]), true
}

// player is one connected player of a scenario's protocol; play reports its
// audio and video messages until stop.
type player interface {
	play(stream string, stop <-chan struct{}, report chan<- rtmpMessage) error
}

func (c *rtmpClient) play(stream string, stop <-chan struct{}, report chan<- rtmpMessage) error {
	return playRTMP(c, stream, stop, report)
}

// playerDialer connects one player of a scenario's player protocol to SRS in
// the shared network namespace.
func playerDialer(kind string) (func() (player, error), error) {
	switch kind {
	case "rtmp":
		return func() (player, error) {
			c, err := dialRTMP("127.0.0.1:1935", "live")
			if err != nil {
				return nil, err
			}
			return c, nil
		}, nil
	case "flv":
		return func() (player, error) {
			c, err := dialFLV("127.0.0.1:8080", "live")
			if err != nil {
				return nil, err
			}
			return c, nil
		}, nil
	case "ts":
		return func() (player, error) {
			c, err := dialHTTPTS("127.0.0.1:"+httpTSPort, "live")
			if err != nil {
				return nil, err
			}
			return c, nil
		}, nil
	case "rtsp":
		return func() (player, error) {
			c, err := dialRTSP("127.0.0.1:"+rtspPort, "live")
			if err != nil {
				return nil, err
			}
			return c, nil
		}, nil
	case "srt":
		return func() (player, error) {
			c, err := dialSRT("127.0.0.1:"+srtPort, "live")
			if err != nil {
				return nil, err
			}
			return c, nil
		}, nil
	case "hls":
		return func() (player, error) {
			c, err := dialHLS("127.0.0.1:"+hlsPort, "live")
			if err != nil {
				return nil, err
			}
			return c, nil
		}, nil
	case "dash":
		return func() (player, error) {
			c, err := dialDASH("127.0.0.1:"+dashPort, "live")
			if err != nil {
				return nil, err
			}
			return c, nil
		}, nil
	case "whep":
		return func() (player, error) {
			c, err := dialWHEP("127.0.0.1:"+rtcAPIPort, "live")
			if err != nil {
				return nil, err
			}
			return c, nil
		}, nil
	}
	return nil, fmt.Errorf("no player for protocol %q", kind)
}

// startPlayersOn starts the players, player id on stream streamOf(id) once
// wait, if any, lets it; wait returns false at stop.
func startPlayersOn(dial func() (player, error), clients int, timeline *sourceTimeline, gap time.Duration, streamOf func(int) string, wait func(id int, stop <-chan struct{}) bool) *workerPlayers {
	w := newWorkerPlayers(clients, timeline)
	w.gap = gap
	// The first dial error of a worker names a load-side limit, such as no
	// free ephemeral port or too many open files.
	var logged atomic.Bool
	for id := 0; id < clients; id++ {
		w.wg.Add(1)
		go func(id int) {
			defer w.wg.Done()
			if wait != nil && !wait(id, w.stop) {
				return
			}
			c, err := dialPlayer(dial, w.stop)
			if err != nil {
				w.failures.Add(1)
				if logged.CompareAndSwap(false, true) {
					fmt.Fprintf(os.Stderr, "player dial: %v\n", err)
				}
				return
			}
			report := make(chan rtmpMessage, 128)
			readDone := make(chan error, 1)
			go func() { readDone <- c.play(streamOf(id), w.stop, report); close(report) }()
			for m := range report {
				w.received(id, m)
			}
			if err := <-readDone; err != nil {
				w.failures.Add(1)
			}
		}(id)
	}
	return w
}

// dialPlayer dials again while a dial times out, as when a step adds thousands
// of players at once and SRS's listen backlog overflows. A player that never
// connects fails the frame rule; any other dial error ends it.
func dialPlayer[T any](dial func() (T, error), stop <-chan struct{}) (T, error) {
	for {
		c, err := dial()
		var netErr net.Error
		if err == nil || !errors.As(err, &netErr) || !netErr.Timeout() {
			return c, err
		}
		select {
		case <-stop:
			var none T
			return none, err
		default:
		}
	}
}

func (w *workerPlayers) sample(timestamps []uint32, final bool) workerResponse {
	r := workerResponse{Frames: w.frames.Swap(0), Segments: w.segments.Swap(0), Bytes: w.bytes.Swap(0), Failures: w.failures.Swap(0)}
	now := time.Now()
	for id := range w.last {
		last := w.last[id].Load()
		if last == 0 {
			if final || now.Sub(w.started) > w.gap {
				r.Failures++
			}
			continue
		}
		if now.Sub(time.Unix(0, last)) > w.gap {
			r.Failures++
		}
		var newest uint32
		if w.clocks != nil {
			newest = w.clocks[id].get()
		} else if stream := w.streamOf(id); stream < len(timestamps) {
			newest = timestamps[stream]
		}
		if lag, ok := w.lag(id, newest); ok && lag > r.LagMillis {
			r.LagMillis = lag
		}
	}
	return r
}

func (w *workerPlayers) streamOf(id int) int {
	if w.streams == nil {
		return 0
	}
	return w.streams[id]
}

func (w *workerPlayers) close() { close(w.stop); w.wg.Wait() }
