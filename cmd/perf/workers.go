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
	Kind            string `json:"kind"`
	Scenario        string `json:"scenario,omitempty"`
	Clients         int    `json:"clients,omitempty"`
	Second          int    `json:"second,omitempty"`
	LatestTimestamp uint32 `json:"latest_timestamp,omitempty"`
	// Final marks the step's last sample: a player with no frame yet fails.
	Final bool `json:"final,omitempty"`
}

type workerResponse struct {
	Frames    int64 `json:"frames"`
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

func startWorkers(ctx context.Context, factory func(int) *exec.Cmd, scenario string, clients, cores int) (*workerGroup, error) {
	shares := splitClients(clients, cores)
	if shares == nil {
		return nil, fmt.Errorf("clients must be nonnegative and cores positive")
	}
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
		if err := worker.enc.Encode(workerRequest{Kind: "start", Scenario: scenario, Clients: share}); err != nil {
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
	return g.collectWithTimestamp(second, 0, false)
}

func (g *workerGroup) collectWithTimestamp(second int, timestamp uint32, final bool) (workerResponse, error) {
	if second <= 0 {
		return workerResponse{}, fmt.Errorf("sample second must be positive")
	}
	for i, worker := range g.workers {
		if err := worker.enc.Encode(workerRequest{Kind: "sample", Second: second, LatestTimestamp: timestamp, Final: final}); err != nil {
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
	if _, ok := scenarioByName(start.Scenario); !ok {
		return fmt.Errorf("unknown scenario %q", start.Scenario)
	}
	runtime := startRTMPPlayers(start.Clients)
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
			if err := enc.Encode(runtime.sample(request.LatestTimestamp, request.Final)); err != nil {
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
	stop     chan struct{}
	started  time.Time
	wg       sync.WaitGroup
	frames   atomic.Int64
	bytes    atomic.Int64
	failures atomic.Int64
	last     []atomic.Int64
	latest   []atomic.Uint32
}

func startRTMPPlayers(clients int) *workerPlayers {
	w := &workerPlayers{stop: make(chan struct{}), started: time.Now(), last: make([]atomic.Int64, clients), latest: make([]atomic.Uint32, clients)}
	// The first dial error of a worker names a load-side limit, such as no
	// free ephemeral port or too many open files.
	var logged atomic.Bool
	for id := 0; id < clients; id++ {
		w.wg.Add(1)
		go func(id int) {
			defer w.wg.Done()
			c, err := dialPlayer(func() (*rtmpClient, error) { return dialRTMP("127.0.0.1:1935", "live") }, w.stop)
			if err != nil {
				w.failures.Add(1)
				if logged.CompareAndSwap(false, true) {
					fmt.Fprintf(os.Stderr, "player dial: %v\n", err)
				}
				return
			}
			report := make(chan rtmpMessage, 128)
			readDone := make(chan error, 1)
			go func() { readDone <- playRTMP(c, "perf-run", w.stop, report); close(report) }()
			for m := range report {
				w.frames.Add(1)
				w.bytes.Add(int64(len(m.payload)))
				w.latest[id].Store(m.timestamp)
				w.last[id].Store(time.Now().UnixNano())
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
func dialPlayer(dial func() (*rtmpClient, error), stop <-chan struct{}) (*rtmpClient, error) {
	for {
		c, err := dial()
		var netErr net.Error
		if err == nil || !errors.As(err, &netErr) || !netErr.Timeout() {
			return c, err
		}
		select {
		case <-stop:
			return nil, err
		default:
		}
	}
}

func (w *workerPlayers) sample(publisherTimestamp uint32, final bool) workerResponse {
	r := workerResponse{Frames: w.frames.Swap(0), Bytes: w.bytes.Swap(0), Failures: w.failures.Swap(0)}
	now := time.Now()
	for id := range w.last {
		last := w.last[id].Load()
		if last == 0 {
			if final || now.Sub(w.started) > 5*time.Second {
				r.Failures++
			}
			continue
		}
		if now.Sub(time.Unix(0, last)) > 5*time.Second {
			r.Failures++
		}
		lag := int64(publisherTimestamp) - int64(w.latest[id].Load())
		if lag > r.LagMillis {
			r.LagMillis = lag
		}
	}
	return r
}

func (w *workerPlayers) close() { close(w.stop); w.wg.Wait() }
