package main

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"reflect"
	"regexp"
	"strconv"
	"strings"
	"testing"
)

func TestParseCommand(t *testing.T) {
	tests := []struct {
		args []string
		want command
		err  string
	}{
		{[]string{"run", "-scenario", "rtmp-rtmp", "-repeat", "3"}, command{kind: "run", scenario: "rtmp-rtmp", repeat: 3, settle: 2, window: 3, media: defaultMedia}, ""},
		// One run by default; the noise is measured by a second identical hold.
		{[]string{"run", "-scenario", "rtmp-rtmp"}, command{kind: "run", scenario: "rtmp-rtmp", repeat: 1, settle: 2, window: 3, media: defaultMedia}, ""},
		// Runs read the flat CBR source in the image unless told otherwise.
		{[]string{"run", "-scenario", "rtmp-rtmp", "-media", "/tmp/other.flv"}, command{kind: "run", scenario: "rtmp-rtmp", repeat: 1, settle: 2, window: 3, media: "/tmp/other.flv"}, ""},
		// A measuring hold settles 30s and counts a 60s window, 180s for a verdict.
		{[]string{"run", "-scenario", "rtmp-rtmp", "-mode", "hold"}, command{kind: "run", scenario: "rtmp-rtmp", repeat: 1, mode: "hold", settle: 30, window: 60, media: defaultMedia}, ""},
		{[]string{"run", "-scenario", "rtmp-rtmp", "-mode", "hold", "-window", "180"}, command{kind: "run", scenario: "rtmp-rtmp", repeat: 1, mode: "hold", settle: 30, window: 180, media: defaultMedia}, ""},
		{[]string{"run", "-scenario", "rtmp-rtmp", "-mode", "hold", "-settle", "0", "-window", "20"}, command{kind: "run", scenario: "rtmp-rtmp", repeat: 1, mode: "hold", settle: 0, window: 20, media: defaultMedia}, ""},
		// A profile hold runs perf after its settle; its CPU is not a measurement.
		{[]string{"run", "-scenario", "rtmp-rtmp", "-mode", "profile"}, command{kind: "run", scenario: "rtmp-rtmp", repeat: 1, mode: "profile", settle: 30, window: 75, media: defaultMedia}, ""},
		{[]string{"run", "-scenario", "rtmp-rtmp", "-mode", "hold", "-window", "0"}, command{}, "invalid run options"},
		{[]string{"run", "-scenario", "rtmp-rtmp", "-mode", "hold", "-settle", "-2"}, command{}, "invalid run options"},
		// -settle and -window replace -seconds, so a step's counted seconds are explicit.
		{[]string{"run", "-scenario", "rtmp-rtmp", "-seconds", "75"}, command{}, "seconds"},
		{[]string{"run", "-scenario", "rtmp-rtmp", "-media", ""}, command{}, "media is required"},
		{[]string{"smoke", "-scenario", "rtmp-rtmp"}, command{kind: "smoke", scenario: "rtmp-rtmp", media: defaultMedia}, ""},
		{[]string{"smoke", "-scenario", "rtmp-rtmp", "-media", "/tmp/other.flv"}, command{kind: "smoke", scenario: "rtmp-rtmp", media: "/tmp/other.flv"}, ""},
		// perf-cap-check.sh connects players with no media, so only connection limits show.
		{[]string{"run", "-scenario", "rtmp-rtmp", "-mode", "hold", "-no-publisher"}, command{kind: "run", scenario: "rtmp-rtmp", repeat: 1, mode: "hold", settle: 30, window: 60, noPublisher: true, media: defaultMedia}, ""},
		// The two sides of an A/B run share an id, so compare pairs their seconds.
		{[]string{"run", "-scenario", "rtmp-rtmp", "-mode", "hold", "-ab-run", "ab-1"}, command{kind: "run", scenario: "rtmp-rtmp", repeat: 1, mode: "hold", settle: 30, window: 60, media: defaultMedia, abRun: "ab-1"}, ""},
		{[]string{"run", "-scenario", "unknown"}, command{}, "unknown scenario"},
		{[]string{"run", "-scenario", "rtmp-rtmp", "-repeat", "0"}, command{}, "repeat must be positive"},
		{[]string{"run"}, command{}, "scenario is required"},
		{[]string{"worker", "-id", "2"}, command{kind: "worker", workerID: 2}, ""},
		{[]string{"worker"}, command{}, "worker id is required"},
		{[]string{"compare", "before.json", "after.json"}, command{kind: "compare", paths: []string{"before.json", "after.json"}}, ""},
		{[]string{"compare", "before.json"}, command{}, "two result files"},
		{[]string{"other"}, command{}, "unknown command"},
	}
	for _, tt := range tests {
		got, err := parseCommand(tt.args)
		if tt.err != "" {
			if err == nil || !strings.Contains(err.Error(), tt.err) {
				t.Errorf("parseCommand(%q) error = %v, want %q", tt.args, err, tt.err)
			}
			continue
		}
		if err != nil || !reflect.DeepEqual(got, tt.want) {
			t.Errorf("parseCommand(%q) = %+v, %v; want %+v", tt.args, got, err, tt.want)
		}
	}
}

func TestScenarioRegistry(t *testing.T) {
	s, ok := scenarioByName("rtmp-rtmp")
	if !ok || s.Name != "rtmp-rtmp" || s.Publisher != "rtmp" || s.Player != "rtmp" || s.Unit != "clients" {
		t.Fatalf("scenario = %+v, %v", s, ok)
	}
	if _, ok := scenarioByName("missing"); ok {
		t.Fatal("unknown scenario was accepted")
	}
}

func TestResultJSONRoundTrip(t *testing.T) {
	want := result{
		Setup:    setup{Machine: "M4", Cores: 10, MemoryBytes: 8589934592, DockerVersion: "29.8.2", Kernel: "7.0.14-linuxkit", CoreCalibrationGap: 0.12},
		Scenario: scenario{Name: "rtmp-rtmp", Publisher: "rtmp", Player: "rtmp", Unit: "clients", Parameters: map[string]string{"codec": "h264+aac"}},
		Steps:    []step{{Mode: "ramp", Clients: 100, Samples: []sample{{Second: 1, SRSCPUPercent: 42.1, SRSRSSBytes: 1234, WorkerCPUPercent: []float64{30, 31}, Frames: 1000, Bytes: 3000, LagMillis: 20, Failures: 0}}, Health: health{Healthy: true}, Validity: validity{Valid: true}}},
		Health:   health{Healthy: true}, Validity: validity{Valid: true},
	}
	encoded, err := json.Marshal(want)
	if err != nil {
		t.Fatal(err)
	}
	for _, key := range []string{"\"setup\"", "\"scenario\"", "\"steps\"", "\"samples\"", "\"health\"", "\"validity\""} {
		if !bytes.Contains(encoded, []byte(key)) {
			t.Errorf("missing JSON key %s: %s", key, encoded)
		}
	}
	var got result
	if err := json.Unmarshal(encoded, &got); err != nil || !reflect.DeepEqual(got, want) {
		t.Fatalf("round trip = %+v, %v; want %+v", got, err, want)
	}
}

// A scenario's SRS env must admit the ramp's cap plus its publisher, or the
// ramp stops at the connection limit instead of a load SRS cannot serve.
func TestScenarioEnvAdmitsCap(t *testing.T) {
	for name := range scenarios {
		data, err := os.ReadFile(filepath.Join("env", name+".env"))
		if err != nil {
			t.Fatalf("%s: %v", name, err)
		}
		m := regexp.MustCompile(`(?m)^SRS_MAX_CONNECTIONS=(\d+)$`).FindSubmatch(data)
		if m == nil {
			t.Fatalf("%s: no SRS_MAX_CONNECTIONS, the shipped 1000 is below the cap %d", name, defaultClientCap)
		}
		n, _ := strconv.Atoi(string(m[1]))
		if n <= defaultClientCap {
			t.Fatalf("%s: SRS_MAX_CONNECTIONS %d does not admit the cap %d and the publisher", name, n, defaultClientCap)
		}
	}
}
