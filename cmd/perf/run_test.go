package main

import (
	"encoding/json"
	"math"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// Each step plays and publishes a stream new to it, so a step's publisher is
// never refused as busy while SRS has yet to read the close of the last
// step's publisher of that stream over UDP, as for SRT and WHIP: perf-run
// and the step's start in ms for a player scenario, perf and the start for
// the publisher and many-streams scenarios, whose workers add each client's
// index.
func TestStepStream(t *testing.T) {
	at := time.UnixMilli(1760000000123)
	if got := stepStream("srt-rtmp", at); got != "perf-run-1760000000123" {
		t.Errorf("player scenario stream = %q", got)
	}
	if got := stepStream("rtmp-pub", at); got != "perf-1760000000123" {
		t.Errorf("publisher scenario stream = %q", got)
	}
	if stepStream("whip-flv", at) == stepStream("whip-flv", at.Add(time.Millisecond)) {
		t.Error("two steps share a stream")
	}
}

func TestRampDecision(t *testing.T) {
	cases := []struct {
		name        string
		clients     int
		samples     []sample
		wantHealthy bool
		wantValid   bool
		wantStop    bool
	}{
		{"healthy", 100, []sample{{SRSCPUPercent: 50, Frames: 100}, {SRSCPUPercent: 60, Frames: 100}}, true, true, false},
		{"cpu stop", 200, []sample{{SRSCPUPercent: 89, Frames: 100}, {SRSCPUPercent: 91, Frames: 100}}, true, true, true},
		{"lag stop", 200, []sample{{SRSCPUPercent: 30, Frames: 100, LagMillis: 2001}}, false, true, true},
		{"worker bound", 200, []sample{{SRSCPUPercent: 30, Frames: 100, Failures: 1, WorkerCPUPercent: []float64{96}}}, false, false, true},
		{"one busy worker is not load-bound", 200, []sample{{SRSCPUPercent: 30, Frames: 100, Failures: 1, WorkerCPUPercent: []float64{113, 40}}}, false, true, true},
		{"one second without frames", 200, []sample{{SRSCPUPercent: 30, Frames: 100}, {SRSCPUPercent: 30}}, true, true, false},
		{"five seconds without frames", 200, []sample{{SRSCPUPercent: 30}, {SRSCPUPercent: 30}, {SRSCPUPercent: 30}, {SRSCPUPercent: 30}, {SRSCPUPercent: 30}}, false, true, true},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			got, stop := evaluateRampStep(scenario{}, tc.clients, 0, tc.samples, 10000)
			if got.Health.Healthy != tc.wantHealthy || got.Validity.Valid != tc.wantValid || stop != tc.wantStop {
				t.Fatalf("health=%+v validity=%+v stop=%v", got.Health, got.Validity, stop)
			}
		})
	}
}

func TestCapacityAndCPUPerClient(t *testing.T) {
	steps := []step{
		{Clients: 100, Samples: []sample{{SRSCPUPercent: 20}}, Health: health{Healthy: true}, Validity: validity{Valid: true}},
		{Clients: 200, Samples: []sample{{SRSCPUPercent: 50}}, Health: health{Healthy: true}, Validity: validity{Valid: true}},
		{Clients: 400, Samples: []sample{{SRSCPUPercent: 95}}, Health: health{Healthy: true}, Validity: validity{Valid: true}},
	}
	capacity, perClient := rampSummary(steps)
	if capacity != 200 || perClient != 0.225 {
		t.Fatalf("capacity=%d cpu/client=%v", capacity, perClient)
	}
}

// A step counts every sample after its settle seconds, so a long step counts
// all of its window, not only its last seconds.
func TestStepCountsEverySampleAfterSettle(t *testing.T) {
	samples := make([]sample, 30)
	for i := range samples {
		samples[i] = sample{SRSCPUPercent: 85, Frames: 1}
		if i < 10 {
			samples[i].SRSCPUPercent = 100
		}
	}
	_, stop := evaluateRampStep(scenario{}, 100, 10, samples, 10000)
	if stop {
		t.Fatal("ramp stopped on settle CPU")
	}
	capacity, perClient := rampSummary([]step{{Clients: 100, SettleSeconds: 10, Samples: samples, Health: health{Healthy: true}, Validity: validity{Valid: true}}})
	if capacity != 100 || perClient != 0.85 {
		t.Fatalf("capacity=%d cpu/client=%v", capacity, perClient)
	}
	if got := steadySamples(samples, 10); len(got) != 20 {
		t.Fatalf("counted=%d, want 20", len(got))
	}
}

func TestWriteRunJSON(t *testing.T) {
	dir := t.TempDir()
	want := result{Scenario: scenarios["rtmp-rtmp"], Steps: []step{{Mode: "ramp", Clients: 100}}}
	path, err := writeRunJSON(dir, "pilot", want)
	if err != nil {
		t.Fatal(err)
	}
	if path != filepath.Join(dir, "pilot", "result.json") {
		t.Fatalf("path=%q", path)
	}
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	var got result
	if err := json.Unmarshal(data, &got); err != nil || got.Scenario.Name != want.Scenario.Name {
		t.Fatalf("json=%+v err=%v", got, err)
	}
}

func TestParseCalibrationGap(t *testing.T) {
	if got := parseCalibrationGap("idle_seconds=0.758\nbusy_seconds=1.319\ngap_percent=74.01\n"); math.Abs(got-0.7401) > 1e-9 {
		t.Fatalf("gap=%v", got)
	}
}

// A 5s ramp step lets new clients settle for 2s and counts only the last 3
// samples, so the connect burst does not stop the ramp or skew CPU per client.
func TestRampStepCountsLastThreeOfFive(t *testing.T) {
	samples := []sample{{SRSCPUPercent: 100, Frames: 1}, {SRSCPUPercent: 100, Frames: 1}, {SRSCPUPercent: 40, Frames: 1}, {SRSCPUPercent: 50, Frames: 1}, {SRSCPUPercent: 60, Frames: 1}}
	if got := steadySamples(samples, 2); len(got) != 3 || got[0].SRSCPUPercent != 40 {
		t.Fatalf("steady=%+v", got)
	}
	if _, stop := evaluateRampStep(scenario{}, 100, 2, samples, defaultClientCap); stop {
		t.Fatal("ramp stopped on settle CPU")
	}
	capacity, perClient := rampSummary([]step{{Clients: 100, SettleSeconds: 2, Samples: samples, Health: health{Healthy: true}, Validity: validity{Valid: true}}})
	if capacity != 100 || math.Abs(perClient-0.5) > 1e-9 {
		t.Fatalf("capacity=%d cpu/client=%v", capacity, perClient)
	}
}

// A ramp step settles 2s and counts 3; a measuring hold settles 30s and counts
// a 60s window; a profile hold settles 30s and runs 75s for the 60s record and
// the 10s syscall count.
func TestStepTimingDefaults(t *testing.T) {
	cases := []struct {
		mode           string
		settle, window int
	}{
		{"ramp", 2, 3},
		{"hold", 30, 60},
		{"profile", 30, 75},
		// The delay probes settle 10s, for the forward and the edge to start,
		// then measure 60s.
		{"probe", 10, 60},
	}
	for _, tc := range cases {
		settle, window := stepTiming(tc.mode)
		if settle != tc.settle || window != tc.window {
			t.Errorf("%s: settle=%d window=%d, want %d and %d", tc.mode, settle, window, tc.settle, tc.window)
		}
	}
}

func TestDefaultClientCap(t *testing.T) {
	if defaultClientCap != 40000 {
		t.Fatalf("cap=%d, want 40000 for TCP protocols", defaultClientCap)
	}
}

// Two holds at the same client count are compared by CPU per client, which
// shows the run-to-run noise.
func TestHoldCPUPerClient(t *testing.T) {
	samples := make([]sample, 30)
	for i := range samples {
		samples[i] = sample{SRSCPUPercent: 40, Frames: 1}
		if i < 10 {
			samples[i].SRSCPUPercent = 100
		}
	}
	if got := holdCPUPerClient(step{Clients: 200, SettleSeconds: 10, Samples: samples}); math.Abs(got-0.2) > 1e-9 {
		t.Fatalf("cpu/client=%v, want 0.2", got)
	}
	if got := holdCPUPerClient(step{Clients: 0}); got != 0 {
		t.Fatalf("empty hold cpu/client=%v", got)
	}
}

// Each step gives the mean and sample sd of every metric over its counted
// samples: here the last 3 of a 5s step, after the 2s settle.
func TestStepWindowStats(t *testing.T) {
	samples := []sample{
		{SRSCPUPercent: 99, SRSRSSBytes: 9, WorkerCPUPercent: []float64{99}, Frames: 99, Bytes: 99, LagMillis: 99, Failures: 9},
		{SRSCPUPercent: 99, SRSRSSBytes: 9, WorkerCPUPercent: []float64{99}, Frames: 99, Bytes: 99, LagMillis: 99, Failures: 9},
		{SRSCPUPercent: 40, SRSRSSBytes: 100, WorkerCPUPercent: []float64{10, 30}, Frames: 10, Bytes: 1000, LagMillis: 100},
		{SRSCPUPercent: 50, SRSRSSBytes: 200, WorkerCPUPercent: []float64{20, 40}, Frames: 20, Bytes: 2000, LagMillis: 200},
		{SRSCPUPercent: 60, SRSRSSBytes: 300, WorkerCPUPercent: []float64{30, 50}, Frames: 30, Bytes: 3000, LagMillis: 300},
	}
	w := windowOf(samples, 2)
	near := func(name string, got, want float64) {
		t.Helper()
		if math.Abs(got-want) > 1e-9 {
			t.Fatalf("%s = %v, want %v", name, got, want)
		}
	}
	if w.Samples != 3 {
		t.Fatalf("samples = %d, want 3", w.Samples)
	}
	near("srs cpu mean", w.SRSCPUPercent.Mean, 50)
	near("srs cpu sd", w.SRSCPUPercent.SD, 10)
	near("rss mean", w.SRSRSSBytes.Mean, 200)
	near("rss sd", w.SRSRSSBytes.SD, 100)
	near("worker cpu mean", w.WorkerCPUPercent.Mean, 30)
	near("worker cpu sd", w.WorkerCPUPercent.SD, 10)
	near("frames mean", w.Frames.Mean, 20)
	near("bytes sd", w.Bytes.SD, 1000)
	near("lag mean", w.LagMillis.Mean, 200)
	near("failures mean", w.Failures.Mean, 0)
}

func TestStepWindowStatsShort(t *testing.T) {
	if w := windowOf(nil, 2); w.Samples != 0 || w.SRSCPUPercent.Mean != 0 || w.SRSCPUPercent.SD != 0 {
		t.Fatalf("empty window = %+v", w)
	}
	// One counted sample has a mean but no spread.
	w := windowOf([]sample{{SRSCPUPercent: 10}, {SRSCPUPercent: 10}, {SRSCPUPercent: 42}}, 2)
	if w.Samples != 1 || w.SRSCPUPercent.Mean != 42 || w.SRSCPUPercent.SD != 0 {
		t.Fatalf("one-sample window = %+v", w)
	}
}

// A hold's result carries its step's window; every step of a ramp has its own.
func TestSummarizeResultWindows(t *testing.T) {
	samples := []sample{{SRSCPUPercent: 90, Frames: 1}, {SRSCPUPercent: 90, Frames: 1}, {SRSCPUPercent: 20, Frames: 1}, {SRSCPUPercent: 40, Frames: 1}}
	hold := summarizeResult(result{Steps: []step{{Mode: "hold", Clients: 100, SettleSeconds: 2, Samples: samples, Health: health{Healthy: true}, Validity: validity{Valid: true}}}}, "hold")
	if hold.Window == nil || hold.Window.Samples != 2 || hold.Window.SRSCPUPercent.Mean != 30 || hold.Steps[0].Window == nil {
		t.Fatalf("hold window = %+v, step window = %+v", hold.Window, hold.Steps[0].Window)
	}
	if math.Abs(hold.CPUPerClient-0.3) > 1e-9 {
		t.Fatalf("hold cpu/client = %v", hold.CPUPerClient)
	}
	ramp := summarizeResult(result{Steps: []step{
		{Mode: "ramp", Clients: 100, SettleSeconds: 2, Samples: samples, Health: health{Healthy: true}, Validity: validity{Valid: true}},
		{Mode: "ramp", Clients: 200, SettleSeconds: 2, Samples: samples, Health: health{Healthy: true}, Validity: validity{Valid: true}},
	}}, "ramp")
	if ramp.Window != nil || ramp.Steps[0].Window == nil || ramp.Steps[1].Window == nil || ramp.Capacity != 200 {
		t.Fatalf("ramp = %+v", ramp)
	}
	data, err := json.Marshal(hold)
	if err != nil || !strings.Contains(string(data), `"window":{"samples":2,"srs_cpu_percent":{"mean":30,"sd":`) {
		t.Fatalf("json = %s, err = %v", data, err)
	}
}

// A measuring hold counts its whole window after a 30s settle: 60 samples, not
// the last 20.
func TestHoldCountsWholeWindow(t *testing.T) {
	samples := make([]sample, 90)
	for i := range samples {
		samples[i] = sample{SRSCPUPercent: 40, Frames: 1}
		if i < 30 {
			samples[i].SRSCPUPercent = 100
		}
	}
	hold := summarizeResult(result{Steps: []step{{Mode: "hold", Clients: 400, SettleSeconds: 30, Samples: samples, Health: health{Healthy: true}, Validity: validity{Valid: true}}}}, "hold")
	if hold.Window == nil || hold.Window.Samples != 60 || hold.Window.SRSCPUPercent.Mean != 40 {
		t.Fatalf("hold window = %+v", hold.Window)
	}
	if math.Abs(hold.CPUPerClient-0.1) > 1e-9 {
		t.Fatalf("hold cpu/client = %v", hold.CPUPerClient)
	}
}

// A profile hold runs perf in the SRS container, which counts in its cgroup, so
// its CPU is never a measurement: no CPU per client and no result window.
func TestProfileHoldIsNotAMeasurement(t *testing.T) {
	samples := []sample{{SRSCPUPercent: 90, Frames: 1}, {SRSCPUPercent: 50, Frames: 1}}
	profile := summarizeResult(result{Steps: []step{{Mode: "profile", Clients: 100, SettleSeconds: 1, Samples: samples, Health: health{Healthy: true}, Validity: validity{Valid: true}}}}, "profile")
	if profile.CPUPerClient != 0 || profile.Window != nil {
		t.Fatalf("profile = %+v", profile)
	}
	if profile.Steps[0].Window == nil || profile.Steps[0].Window.Samples != 1 {
		t.Fatalf("profile step window = %+v", profile.Steps[0].Window)
	}
}
