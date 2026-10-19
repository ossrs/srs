package main

import (
	"encoding/json"
	"math"
	"os"
	"path/filepath"
	"testing"
)

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
			got, stop := evaluateRampStep(tc.clients, tc.samples, 10000)
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

func TestRampUsesLastTwentySecondsForCPU(t *testing.T) {
	samples := make([]sample, 30)
	for i := range samples {
		samples[i] = sample{SRSCPUPercent: 85, Frames: 1}
		if i < 10 {
			samples[i].SRSCPUPercent = 100
		}
	}
	_, stop := evaluateRampStep(100, samples, 10000)
	if stop {
		t.Fatal("ramp stopped on warmup CPU")
	}
	capacity, perClient := rampSummary([]step{{Clients: 100, Samples: samples, Health: health{Healthy: true}, Validity: validity{Valid: true}}})
	if capacity != 100 || perClient != 0.85 {
		t.Fatalf("capacity=%d cpu/client=%v", capacity, perClient)
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
	if got := steadySamples(samples); len(got) != 3 || got[0].SRSCPUPercent != 40 {
		t.Fatalf("steady=%+v", got)
	}
	if _, stop := evaluateRampStep(100, samples, defaultClientCap); stop {
		t.Fatal("ramp stopped on settle CPU")
	}
	capacity, perClient := rampSummary([]step{{Clients: 100, Samples: samples, Health: health{Healthy: true}, Validity: validity{Valid: true}}})
	if capacity != 100 || math.Abs(perClient-0.5) > 1e-9 {
		t.Fatalf("capacity=%d cpu/client=%v", capacity, perClient)
	}
}

func TestStepSecondsDefaults(t *testing.T) {
	if got := stepSeconds("ramp", 0); got != 5 {
		t.Fatalf("ramp default=%d, want 5", got)
	}
	if got := stepSeconds("hold", 0); got != 30 {
		t.Fatalf("hold default=%d, want 30", got)
	}
	if got := stepSeconds("ramp", 75); got != 75 {
		t.Fatalf("explicit seconds=%d, want 75", got)
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
	if got := holdCPUPerClient(step{Clients: 200, Samples: samples}); math.Abs(got-0.2) > 1e-9 {
		t.Fatalf("cpu/client=%v, want 0.2", got)
	}
	if got := holdCPUPerClient(step{Clients: 0}); got != 0 {
		t.Fatalf("empty hold cpu/client=%v", got)
	}
}
