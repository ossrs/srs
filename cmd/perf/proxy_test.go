package main

import (
	"bytes"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"reflect"
	"strconv"
	"strings"
	"testing"
	"time"
)

func writeCommFixture(t *testing.T, root string, pid int, comm string) {
	t.Helper()
	dir := filepath.Join(root, strconv.Itoa(pid))
	if err := os.MkdirAll(dir, 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "comm"), []byte(comm+"\n"), 0644); err != nil {
		t.Fatal(err)
	}
}

// The origins are the SRS processes beside the proxy in its pid namespace,
// in start order, so origin1 (started first by perf-up.sh) comes first.
func TestFindOrigins(t *testing.T) {
	root := t.TempDir()
	writeCommFixture(t, root, 1, "srs-proxy")
	writeCommFixture(t, root, 31, "srs")
	writeCommFixture(t, root, 9, "srs")
	writeCommFixture(t, root, 40, "srs-perf")
	if err := os.MkdirAll(filepath.Join(root, "self"), 0755); err != nil {
		t.Fatal(err)
	}
	got, err := findOrigins(root, 1)
	if err != nil || !reflect.DeepEqual(got, []int{9, 31}) {
		t.Fatalf("origins = %v, err = %v; want [9 31]", got, err)
	}

	empty := t.TempDir()
	writeCommFixture(t, empty, 1, "srs-proxy")
	if _, err := findOrigins(empty, 1); err == nil || !strings.Contains(err.Error(), "no SRS origin") {
		t.Fatalf("err = %v, want no SRS origin", err)
	}
}

// With the proxy as the subject, its cgroup is the measured CPU, and each
// origin's cgroup CPU is kept beside it, second by second.
func TestProcSamplerOrigins(t *testing.T) {
	root := t.TempDir()
	writeProcFixture(t, root, 1, "1 (srs-proxy) S 0 1 1 0 0 0 0 0 0 0 10 5 0 0 0 0 0 0 0 0\n", "Name:\tsrs-proxy\nVmRSS:\t4096 kB\n")
	writeCgroupFixture(t, root, 1, "/docker/proxy", 1000000)
	writeCgroupFixture(t, root, 9, "/docker/origin1", 2000000)
	writeCgroupFixture(t, root, 31, "/docker/origin2", 3000000)
	s := newProcSampler(filepath.Join(root, "proc"), filepath.Join(root, "cgroup"), 1, nil, 100).withOrigins([]int{9, 31})
	start := time.Unix(100, 0)
	first, err := s.sampleAt(start)
	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(first.originCPUPercent, []float64{0, 0}) || !reflect.DeepEqual(first.originCPUUsageUsec, []uint64{2000000, 3000000}) {
		t.Fatalf("first sample = %+v", first)
	}
	writeCgroupFixture(t, root, 1, "/docker/proxy", 1400000)
	writeCgroupFixture(t, root, 9, "/docker/origin1", 2900000)
	writeCgroupFixture(t, root, 31, "/docker/origin2", 3100000)
	second, err := s.sampleAt(start.Add(time.Second))
	if err != nil {
		t.Fatal(err)
	}
	if second.srsCPUPercent != 40 || second.srsRSSBytes != 4096*1024 || !reflect.DeepEqual(second.originCPUPercent, []float64{90, 10}) {
		t.Fatalf("second sample = %+v, want proxy 40%%, origins 90%% and 10%%", second)
	}

	writeCgroupFixture(t, root, 9, "/docker/origin1", 100)
	if _, err := s.sampleAt(start.Add(2 * time.Second)); err == nil || !strings.Contains(err.Error(), "backwards") {
		t.Fatalf("err = %v, want backwards error", err)
	}
}

// The ramp stops when the proxy reaches 90%, as for SRS, or when an origin
// reaches 90% first: then the step is marked origin-bound, still healthy.
func TestOriginBoundStop(t *testing.T) {
	cases := []struct {
		name      string
		proxy     float64
		origins   []float64
		wantStop  bool
		wantBound string
	}{
		{"both below", 60, []float64{50}, false, ""},
		{"origin first", 60, []float64{91}, true, "origin-bound"},
		{"second origin first", 60, []float64{30, 92}, true, "origin-bound"},
		{"any of three origins first", 60, []float64{30, 40, 91}, true, "origin-bound"},
		{"proxy first", 91, []float64{50}, true, ""},
		{"both at once is the proxy's", 91, []float64{95}, true, ""},
		{"srs target has no origins", 60, nil, false, ""},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			samples := []sample{
				{SRSCPUPercent: 100, OriginCPUPercent: []float64{100, 100}, Frames: 10},
				{SRSCPUPercent: tc.proxy, OriginCPUPercent: tc.origins, Frames: 10},
				{SRSCPUPercent: tc.proxy, OriginCPUPercent: tc.origins, Frames: 10},
			}
			got, stop := evaluateRampStep(scenario{}, 100, 1, samples, 10000)
			if stop != tc.wantStop || got.Bound != tc.wantBound || !got.Health.Healthy || !got.Validity.Valid {
				t.Fatalf("stop=%v bound=%q health=%+v validity=%+v", stop, got.Bound, got.Health, got.Validity)
			}
		})
	}
}

// An origin-bound step is not capacity, as a step at the proxy's 90% is not;
// the result carries the mark and each origin's window.
func TestOriginBoundCapacity(t *testing.T) {
	ok := func(clients int, proxy, origin float64) step {
		s, _ := evaluateRampStep(scenario{}, clients, 0, []sample{{SRSCPUPercent: proxy, OriginCPUPercent: []float64{origin}, Frames: 1}}, 10000)
		return s
	}
	r := result{Steps: []step{ok(100, 10, 20), ok(200, 20, 45), ok(400, 40, 92)}}
	r = summarizeResult(r, "ramp")
	if r.Capacity != 200 || r.Bound != "origin-bound" {
		t.Fatalf("capacity=%d bound=%q, want 200 origin-bound", r.Capacity, r.Bound)
	}
	w := r.Steps[2].Window
	if w == nil || len(w.OriginCPUPercent) != 1 || w.OriginCPUPercent[0].Mean != 92 {
		t.Fatalf("window = %+v, want origin mean 92", w)
	}
	srs := summarizeResult(result{Steps: []step{ok(100, 10, 0)}}, "ramp")
	srs.Steps[0].Samples[0].OriginCPUPercent = nil
	if w := windowOf(srs.Steps[0].Samples, 0); w.OriginCPUPercent != nil {
		t.Fatalf("srs window has origins: %+v", w.OriginCPUPercent)
	}

	hold := summarizeResult(result{Steps: []step{ok(300, 50, 93)}}, "hold")
	if hold.Bound != "origin-bound" {
		t.Fatalf("hold bound = %q, want origin-bound", hold.Bound)
	}
}

func TestParseTarget(t *testing.T) {
	got, err := parseCommand([]string{"run", "-scenario", "rtmp-rtmp", "-target", "proxy"})
	want := command{kind: "run", scenario: "rtmp-rtmp", repeat: 1, settle: 2, window: 3, media: defaultMedia, target: "proxy"}
	if err != nil || !reflect.DeepEqual(got, want) {
		t.Fatalf("got %+v, %v", got, err)
	}
	// srs is the default, stored empty like the default mode.
	got, err = parseCommand([]string{"run", "-scenario", "rtmp-rtmp", "-target", "srs"})
	if err != nil || got.target != "" {
		t.Fatalf("got %+v, %v", got, err)
	}
	for _, args := range [][]string{
		{"run", "-scenario", "rtmp-rtmp", "-target", "edge"},
		{"run", "-scenario", "delays", "-target", "proxy"},
	} {
		if _, err := parseCommand(args); err == nil || !strings.Contains(err.Error(), "target") {
			t.Errorf("parseCommand(%q) err = %v, want a target error", args, err)
		}
	}
}

// The smoke takes the same target, so make perf-smoke TARGET=proxy smokes
// through the proxy and records it; the delay probes have no proxy target.
func TestParseSmokeTarget(t *testing.T) {
	got, err := parseCommand([]string{"smoke", "-scenario", "many-streams", "-target", "proxy"})
	want := command{kind: "smoke", scenario: "many-streams", media: defaultMedia, target: "proxy"}
	if err != nil || !reflect.DeepEqual(got, want) {
		t.Fatalf("got %+v, %v", got, err)
	}
	got, err = parseCommand([]string{"smoke", "-scenario", "rtmp-rtmp", "-target", "srs"})
	if err != nil || got.target != "" {
		t.Fatalf("got %+v, %v", got, err)
	}
	for _, args := range [][]string{
		{"smoke", "-scenario", "rtmp-rtmp", "-target", "edge"},
		{"smoke", "-scenario", "delays", "-target", "proxy"},
	} {
		if _, err := parseCommand(args); err == nil || !strings.Contains(err.Error(), "target") {
			t.Errorf("parseCommand(%q) err = %v, want a target error", args, err)
		}
	}
}

// A run or smoke on the proxy records the target, the proxy's version and
// its origins, and gives the origins' pids for their CPU; on SRS it records
// only the target.
func TestTargetSetup(t *testing.T) {
	proxy := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte(`{"signature":"SRSX","version":"8.0.59"}`))
	}))
	defer proxy.Close()
	root := t.TempDir()
	writeCommFixture(t, root, 1, "srs-proxy")
	writeCommFixture(t, root, 20, "srs")
	writeCommFixture(t, root, 12, "srs")

	s, origins, err := targetSetup(setup{Machine: "m"}, "proxy", proxy.URL, root, 1)
	if err != nil || !reflect.DeepEqual(origins, []int{12, 20}) {
		t.Fatalf("origins = %v, err = %v", origins, err)
	}
	if s.Machine != "m" || s.Target != "proxy" || s.ProxyVersion != "8.0.59" || s.Origins != 2 {
		t.Fatalf("setup = %+v", s)
	}

	s, origins, err = targetSetup(setup{Machine: "m"}, "", "http://127.0.0.1:1", root, 1)
	if err != nil || origins != nil || s.Target != "srs" || s.ProxyVersion != "" || s.Origins != 0 {
		t.Fatalf("srs setup = %+v, origins = %v, err = %v", s, origins, err)
	}

	alone := t.TempDir()
	writeCommFixture(t, alone, 1, "srs-proxy")
	if _, _, err := targetSetup(setup{}, "proxy", proxy.URL, alone, 1); err == nil || !strings.Contains(err.Error(), "no SRS origin") {
		t.Fatalf("err = %v, want no SRS origin", err)
	}
}

// The proxy's version comes from its HTTP API, and the signature proves the
// load is on the proxy, not an SRS answering on the same port.
func TestFetchProxyVersion(t *testing.T) {
	proxy := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/api/v1/versions" {
			http.NotFound(w, r)
			return
		}
		w.Write([]byte(`{"signature":"SRSX","version":"8.0.59"}`))
	}))
	defer proxy.Close()
	if got, err := fetchProxyVersion(proxy.URL); err != nil || got != "8.0.59" {
		t.Fatalf("version = %q, err = %v", got, err)
	}

	srs := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte(`{"code":0,"server":"vid-1","data":{"major":8,"version":"8.0.62"}}`))
	}))
	defer srs.Close()
	if _, err := fetchProxyVersion(srs.URL); err == nil || !strings.Contains(err.Error(), "not the proxy") {
		t.Fatalf("err = %v, want not the proxy", err)
	}
}

// compare never sets a proxy result against an SRS one, nor results with a
// different origin count; a result from before the target field is SRS's.
func TestCompareRefusesProxyAgainstSRS(t *testing.T) {
	old := comparisonFixture()
	proxy := comparisonFixture()
	proxy.Setup.Target, proxy.Setup.ProxyVersion, proxy.Setup.Origins = "proxy", "8.0.59", 1
	var out bytes.Buffer
	if err := compareRunFiles(&out, writeComparisonFixture(t, "old.json", old), writeComparisonFixture(t, "new.json", proxy)); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(out.String(), "different target (srs against proxy), no comparison") {
		t.Fatalf("output = %q", out.String())
	}

	two := proxy
	two.Setup.Origins = 2
	out.Reset()
	if err := compareRunFiles(&out, writeComparisonFixture(t, "old.json", proxy), writeComparisonFixture(t, "new.json", two)); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(out.String(), "different setup, no comparison") {
		t.Fatalf("output = %q", out.String())
	}

	// A new proxy version is what an A/B or a fix compares, so it matches.
	newer := proxy
	newer.Setup.ProxyVersion = "8.0.60"
	out.Reset()
	if err := compareRunFiles(&out, writeComparisonFixture(t, "old.json", proxy), writeComparisonFixture(t, "new.json", newer)); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(out.String(), "no changes >= 10%") {
		t.Fatalf("output = %q", out.String())
	}

	srs := comparisonFixture()
	srs.Setup.Target = "srs"
	out.Reset()
	if err := compareRunFiles(&out, writeComparisonFixture(t, "old.json", old), writeComparisonFixture(t, "new.json", srs)); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(out.String(), "no changes >= 10%") {
		t.Fatalf("output = %q", out.String())
	}
}
