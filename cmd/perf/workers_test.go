package main

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"os"
	"os/exec"
	"reflect"
	"testing"
	"time"
)

func TestServeWorker(t *testing.T) {
	input := bytes.NewBufferString("{\"kind\":\"start\",\"scenario\":\"rtmp-rtmp\",\"clients\":4}\n{\"kind\":\"sample\",\"second\":1}\n{\"kind\":\"stop\"}\n")
	var output bytes.Buffer
	if err := serveWorker(input, &output); err != nil {
		t.Fatal(err)
	}
	dec := json.NewDecoder(&output)
	for i := 0; i < 2; i++ {
		var response workerResponse
		if err := dec.Decode(&response); err != nil {
			t.Fatalf("response %d: %v", i, err)
		}
	}
}

func TestSplitClients(t *testing.T) {
	if got := splitClients(10, 3); !reflect.DeepEqual(got, []int{4, 3, 3}) {
		t.Fatalf("split 10 clients = %v", got)
	}
	if got := splitClients(2, 4); !reflect.DeepEqual(got, []int{1, 1, 0, 0}) {
		t.Fatalf("split 2 clients = %v", got)
	}
}

func TestWorkersCollectAndStop(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	factory := func(id int) *exec.Cmd {
		cmd := exec.CommandContext(ctx, os.Args[0], "-test.run=TestWorkerHelperProcess")
		cmd.Env = append(os.Environ(), "SRS_PERF_FAKE_WORKER=1")
		return cmd
	}
	group, err := startWorkers(ctx, factory, "rtmp-rtmp", "", []string{"perf-run"}, 10, 3)
	if err != nil {
		t.Fatal(err)
	}
	if got := group.pids(); len(got) != 3 || got[0] <= 0 || got[1] <= 0 || got[2] <= 0 {
		t.Fatalf("worker pids = %v", got)
	}
	for second := 1; second <= 2; second++ {
		got, err := group.collect(second)
		if err != nil {
			t.Fatal(err)
		}
		if got.Frames != 10*int64(second) || got.Bytes != 100*int64(second) || got.LagMillis != 33 || got.Failures != 0 {
			t.Fatalf("second %d counters = %+v", second, got)
		}
	}
	if err := group.close(); err != nil {
		t.Fatal(err)
	}
}

func TestWorkersCollectEverySecond(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 1500*time.Millisecond)
	defer cancel()
	factory := func(id int) *exec.Cmd {
		cmd := exec.CommandContext(ctx, os.Args[0], "-test.run=TestWorkerHelperProcess")
		cmd.Env = append(os.Environ(), "SRS_PERF_FAKE_WORKER=1")
		return cmd
	}
	group, err := startWorkers(ctx, factory, "rtmp-rtmp", "", []string{"perf-run"}, 4, 2)
	if err != nil {
		t.Fatal(err)
	}
	defer group.close()
	output := make(chan workerResponse, 2)
	done := make(chan error, 1)
	go func() { done <- group.collectEverySecond(ctx, output) }()
	select {
	case got := <-output:
		if got.Frames != 4 || got.Bytes != 40 {
			t.Fatalf("first second = %+v", got)
		}
	case err := <-done:
		t.Fatalf("collector stopped before first sample: %v", err)
	}
	if err := <-done; err != context.DeadlineExceeded {
		t.Fatalf("collector ended: %v", err)
	}
}

func TestWorkerLoadClassification(t *testing.T) {
	for _, tt := range []struct {
		failed bool
		cpus   []float64
		want   validity
	}{
		{false, []float64{100}, validity{Valid: true}},
		{true, []float64{80}, validity{Valid: true}},
		{true, []float64{95}, validity{Valid: true}},
		{true, []float64{95.1}, validity{Valid: false, Reason: "load-bound"}},
		// A worker is a Go process that may run on every core of the load
		// cpuset, so one worker above 100% is not a pegged load; the load is
		// pegged when the workers average above 95% of their cores.
		{true, []float64{588, 10, 10, 10, 10, 10, 10, 10}, validity{Valid: true}},
		{true, []float64{96, 96, 96, 96}, validity{Valid: false, Reason: "load-bound"}},
		{true, []float64{95, 95}, validity{Valid: true}},
	} {
		if got := classifyWorkerLoad(tt.failed, tt.cpus); got != tt.want {
			t.Errorf("failed=%v cpus=%v: got %+v, want %+v", tt.failed, tt.cpus, got, tt.want)
		}
	}
}

func TestWorkerHelperProcess(t *testing.T) {
	if os.Getenv("SRS_PERF_FAKE_WORKER") != "1" {
		return
	}
	dec := json.NewDecoder(os.Stdin)
	enc := json.NewEncoder(os.Stdout)
	clients := 0
	for {
		var request workerRequest
		if err := dec.Decode(&request); err != nil {
			if err == io.EOF {
				os.Exit(0)
			}
			os.Exit(2)
		}
		switch request.Kind {
		case "start":
			clients = request.Clients
			if err := enc.Encode(workerResponse{}); err != nil {
				os.Exit(2)
			}
		case "sample":
			if err := enc.Encode(workerResponse{Frames: int64(clients * request.Second), Bytes: int64(clients * request.Second * 10), LagMillis: 33}); err != nil {
				os.Exit(2)
			}
		case "stop":
			os.Exit(0)
		default:
			os.Exit(2)
		}
	}
}

// A player that gets no frame in the whole step fails at the step's last
// sample, even when the step is shorter than the 5s gap rule.
func TestPlayerWithoutFramesFailsAtStepEnd(t *testing.T) {
	w := newWorkerPlayers(2, nil)
	w.last[1].Store(time.Now().UnixNano())
	if got := w.sample(nil, false); got.Failures != 0 {
		t.Fatalf("mid-step failures=%d, want 0", got.Failures)
	}
	if got := w.sample(nil, true); got.Failures != 1 {
		t.Fatalf("final failures=%d, want 1", got.Failures)
	}
}

type timeoutError struct{}

func (timeoutError) Error() string   { return "i/o timeout" }
func (timeoutError) Timeout() bool   { return true }
func (timeoutError) Temporary() bool { return true }

// A player whose dial times out while SRS drains a connect burst dials again
// until the worker stops; any other error ends the player.
func TestDialPlayerRetriesTimeouts(t *testing.T) {
	calls := 0
	c, err := dialPlayer(func() (*rtmpClient, error) {
		calls++
		if calls < 3 {
			return nil, timeoutError{}
		}
		return &rtmpClient{}, nil
	}, make(chan struct{}))
	if err != nil || c == nil || calls != 3 {
		t.Fatalf("client=%v err=%v calls=%d", c, err, calls)
	}
	calls = 0
	if _, err := dialPlayer(func() (*rtmpClient, error) { calls++; return nil, io.EOF }, make(chan struct{})); err != io.EOF || calls != 1 {
		t.Fatalf("err=%v calls=%d", err, calls)
	}
	stop := make(chan struct{})
	close(stop)
	if _, err := dialPlayer(func() (*rtmpClient, error) { return nil, timeoutError{} }, stop); err == nil {
		t.Fatal("stopped dial returned no error")
	}
}
