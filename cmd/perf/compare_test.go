package main

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func comparisonFixture() result {
	return result{
		Setup:    setup{Machine: "M4", Cores: 10, MemoryBytes: 8589934592, DockerVersion: "29.8.2", Kernel: "7.0-linuxkit"},
		Scenario: scenario{Name: "rtmp-rtmp", Publisher: "rtmp", Player: "rtmp", Unit: "clients", Parameters: map[string]string{"mode": "ramp", "media": "doc/source.flv", "seconds": "30"}},
		Health:   health{Healthy: true}, Validity: validity{Valid: true}, Capacity: 1000, CPUPerClient: 0.08,
	}
}

func writeComparisonFixture(t *testing.T, name string, value result) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), name)
	data, err := json.Marshal(value)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, data, 0600); err != nil {
		t.Fatal(err)
	}
	return path
}

func TestCompareRunFiles(t *testing.T) {
	old := comparisonFixture()
	cases := []struct {
		name string
		edit func(*result)
		want []string
	}{
		{"same", func(*result) {}, []string{"capacity: 1000 -> 1000", "cpu per client: 0.08 -> 0.08", "no changes >= 10%"}},
		{"capacity regression boundary", func(r *result) { r.Capacity = 900 }, []string{"capacity: 1000 -> 900", "capacity regression >= 10%"}},
		{"capacity gain", func(r *result) { r.Capacity = 1200 }, []string{"capacity gain >= 10%"}},
		{"cpu regression boundary", func(r *result) { r.CPUPerClient = 0.088 }, []string{"cpu per client regression >= 10%"}},
		{"cpu gain", func(r *result) { r.CPUPerClient = 0.07 }, []string{"cpu per client gain >= 10%"}},
		{"below threshold", func(r *result) { r.Capacity = 901; r.CPUPerClient = 0.087 }, []string{"no changes >= 10%"}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			newer := comparisonFixture()
			tc.edit(&newer)
			var out bytes.Buffer
			err := compareRunFiles(&out, writeComparisonFixture(t, "old.json", old), writeComparisonFixture(t, "new.json", newer))
			if err != nil {
				t.Fatal(err)
			}
			for _, want := range tc.want {
				if !strings.Contains(out.String(), want) {
					t.Errorf("output %q lacks %q", out.String(), want)
				}
			}
		})
	}
}

func TestCompareRejectsDifferentSetup(t *testing.T) {
	old := comparisonFixture()
	cases := []struct {
		name string
		edit func(*result)
	}{
		{"machine", func(r *result) { r.Setup.Machine = "other" }},
		{"cores", func(r *result) { r.Setup.Cores++ }},
		{"memory", func(r *result) { r.Setup.MemoryBytes++ }},
		{"docker", func(r *result) { r.Setup.DockerVersion = "other" }},
		{"kernel", func(r *result) { r.Setup.Kernel = "other" }},
		{"scenario", func(r *result) { r.Scenario.Name = "other" }},
		{"publisher", func(r *result) { r.Scenario.Publisher = "other" }},
		{"player", func(r *result) { r.Scenario.Player = "other" }},
		{"unit", func(r *result) { r.Scenario.Unit = "other" }},
		{"parameters", func(r *result) { r.Scenario.Parameters["seconds"] = "60" }},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			newer := comparisonFixture()
			tc.edit(&newer)
			var out bytes.Buffer
			err := compareRunFiles(&out, writeComparisonFixture(t, "old.json", old), writeComparisonFixture(t, "new.json", newer))
			if err != nil || !strings.Contains(out.String(), "different setup, no comparison") || !strings.Contains(out.String(), "new baseline") {
				t.Fatalf("output=%q err=%v", out.String(), err)
			}
		})
	}
}

func TestCompareRejectsBadData(t *testing.T) {
	old := comparisonFixture()
	path := writeComparisonFixture(t, "old.json", old)
	bad := filepath.Join(t.TempDir(), "bad.json")
	if err := os.WriteFile(bad, []byte("{"), 0600); err != nil {
		t.Fatal(err)
	}
	if err := compareRunFiles(&bytes.Buffer{}, path, bad); err == nil {
		t.Fatal("invalid JSON accepted")
	}
	if err := compareRunFiles(&bytes.Buffer{}, path, filepath.Join(t.TempDir(), "missing.json")); err == nil {
		t.Fatal("missing report accepted")
	}
}

func comparisonResult(mode string, clients string, capacity int, cpu float64) result {
	value := comparisonFixture()
	value.Scenario.Parameters = map[string]string{"mode": mode, "clients": clients, "media": "doc/source.flv"}
	value.Capacity = capacity
	value.CPUPerClient = cpu
	return value
}

func writeCombinedFixture(t *testing.T, name string, results ...result) string {
	t.Helper()
	data, err := json.Marshal(map[string]any{"scenario": "rtmp-rtmp", "run": name, "repeat": 1, "results": results})
	if err != nil {
		t.Fatal(err)
	}
	path := filepath.Join(t.TempDir(), name)
	if err := os.WriteFile(path, data, 0600); err != nil {
		t.Fatal(err)
	}
	return path
}

func TestCompareCombinedRunsPairsByModeAndClients(t *testing.T) {
	old := writeCombinedFixture(t, "old.json",
		comparisonResult("ramp", "100", 1000, 0.2),
		comparisonResult("hold", "500", 0, 0.08),
		comparisonResult("hold", "500", 0, 0.08),
		comparisonResult("hold", "900", 0, 0.08))
	newer := writeCombinedFixture(t, "new.json",
		comparisonResult("hold", "900", 0, 0.09),
		comparisonResult("ramp", "100", 1200, 0.2),
		comparisonResult("hold", "500", 0, 0.07),
		comparisonResult("hold", "500", 0, 0.081))
	var out bytes.Buffer
	if err := compareRunFiles(&out, old, newer); err != nil {
		t.Fatal(err)
	}
	want := "ramp 100 clients:\ncapacity: 1000 -> 1200\ncpu per client: 0.2 -> 0.2\ncapacity gain >= 10%\n" +
		"hold 500 clients:\ncapacity: 0 -> 0\ncpu per client: 0.08 -> 0.07\ncpu per client gain >= 10%\n" +
		"hold 500 clients:\ncapacity: 0 -> 0\ncpu per client: 0.08 -> 0.081\nno changes >= 10%\n" +
		"hold 900 clients:\ncapacity: 0 -> 0\ncpu per client: 0.08 -> 0.09\ncpu per client regression >= 10%\n"
	if out.String() != want {
		t.Fatalf("output\n%s\nwant\n%s", out.String(), want)
	}
}

func TestCompareCombinedRunWithOneResult(t *testing.T) {
	old := writeCombinedFixture(t, "old.json",
		comparisonResult("ramp", "100", 1000, 0.2),
		comparisonResult("hold", "900", 0, 0.08))
	single := writeComparisonFixture(t, "new.json", comparisonResult("hold", "900", 0, 0.08))
	var out bytes.Buffer
	if err := compareRunFiles(&out, old, single); err != nil {
		t.Fatal(err)
	}
	want := "ramp 100 clients: only in old\nhold 900 clients:\ncapacity: 0 -> 0\ncpu per client: 0.08 -> 0.08\nno changes >= 10%\n"
	if out.String() != want {
		t.Fatalf("output\n%s\nwant\n%s", out.String(), want)
	}
}

func TestCompareCombinedRunsReportsUnmatchedAndSetup(t *testing.T) {
	other := comparisonResult("hold", "900", 0, 0.08)
	other.Setup.Kernel = "other"
	old := writeCombinedFixture(t, "old.json", comparisonResult("hold", "900", 0, 0.08))
	newer := writeCombinedFixture(t, "new.json", other, comparisonResult("hold", "700", 0, 0.08))
	var out bytes.Buffer
	if err := compareRunFiles(&out, old, newer); err != nil {
		t.Fatal(err)
	}
	want := "hold 900 clients:\ndifferent setup, no comparison; new report is a new baseline\nhold 700 clients: only in new\n"
	if out.String() != want {
		t.Fatalf("output\n%s\nwant\n%s", out.String(), want)
	}
}

func TestCompareRejectsEmptyCombinedRun(t *testing.T) {
	empty := writeCombinedFixture(t, "empty.json")
	path := writeComparisonFixture(t, "old.json", comparisonFixture())
	if err := compareRunFiles(&bytes.Buffer{}, path, empty); err == nil {
		t.Fatal("combined run without results accepted")
	}
}
