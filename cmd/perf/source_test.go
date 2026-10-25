package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// writeSource writes a small FLV and, unless flatness is empty, its flatness
// record beside it, named the way perf-source.sh names it.
func writeSource(t *testing.T, flatness string) string {
	t.Helper()
	dir := t.TempDir()
	path := filepath.Join(dir, "cbr.flv")
	data := []byte{'F', 'L', 'V', 1, 5, 0, 0, 0, 9, 0, 0, 0, 0,
		9, 0, 0, 3, 0, 0, 40, 0, 0, 0, 0, 1, 2, 3, 0, 0, 0, 14}
	if err := os.WriteFile(path, data, 0o644); err != nil {
		t.Fatal(err)
	}
	if flatness != "" {
		if err := os.WriteFile(filepath.Join(dir, "cbr-flatness.json"), []byte(flatness), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	return path
}

func TestLoadSourceAcceptsFlat(t *testing.T) {
	flatness := `{"file":"/srs/perf/cbr.flv","flat":true,"reason":"","target_kbps":200}`
	path := writeSource(t, flatness)
	tags, record, err := loadSource(path)
	if err != nil {
		t.Fatal(err)
	}
	if len(tags) != 1 {
		t.Fatalf("tags = %d, want 1", len(tags))
	}
	var got map[string]any
	if err := json.Unmarshal(record, &got); err != nil || got["flat"] != true || got["target_kbps"] != float64(200) {
		t.Fatalf("flatness record = %s, %v", record, err)
	}
}

func TestLoadSourceRefusesUnflat(t *testing.T) {
	tests := []struct {
		name     string
		flatness string
		want     string
	}{
		{"missing flatness", "", "flatness"},
		{"not flat", `{"flat":false,"reason":"second 12 at 242.4 kbps"}`, "not flat: second 12 at 242.4 kbps"},
		{"no flat field", `{"target_kbps":200}`, "not flat"},
		{"bad JSON", `{"flat":`, "flatness"},
	}
	for _, tt := range tests {
		path := writeSource(t, tt.flatness)
		if _, _, err := loadSource(path); err == nil || !strings.Contains(err.Error(), tt.want) {
			t.Errorf("%s: err = %v, want %q", tt.name, err, tt.want)
		}
	}
}

func TestLoadSourceRefusesMissingFile(t *testing.T) {
	if _, _, err := loadSource(filepath.Join(t.TempDir(), "missing.flv")); err == nil {
		t.Fatal("a missing source was accepted")
	}
}

func TestSourceFlatnessPath(t *testing.T) {
	if got := sourceFlatnessPath("/srs/perf/cbr.flv"); got != "/srs/perf/cbr-flatness.json" {
		t.Fatalf("flatness path = %s", got)
	}
}

// Every result records the source it was measured on and its flatness, so a
// report shows it and compare can tell a VBR run from a CBR one.
func TestSetupRecordsSource(t *testing.T) {
	flatness := json.RawMessage(`{"flat":true,"target_kbps":200}`)
	s := captureSetup(command{media: "/srs/perf/cbr.flv"})
	s = withSource(s, "/srs/perf/cbr.flv", flatness)
	encoded, err := json.Marshal(result{Setup: s})
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(encoded), `"source":"/srs/perf/cbr.flv"`) ||
		!strings.Contains(string(encoded), `"source_flatness":{"flat":true,"target_kbps":200}`) {
		t.Fatalf("setup JSON = %s", encoded)
	}
}

func TestCompareRejectsDifferentSource(t *testing.T) {
	old := result{Setup: setup{Machine: "M4", Source: "doc/source.flv"}, Scenario: scenario{Name: "rtmp-rtmp"}}
	newer := old
	newer.Setup.Source = "/srs/perf/cbr.flv"
	if sameComparisonSetup(old, newer) {
		t.Fatal("results on different sources were compared")
	}
}
