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
		// HTTP-FLV players of an RTMP publisher.
		{[]string{"run", "-scenario", "rtmp-flv"}, command{kind: "run", scenario: "rtmp-flv", repeat: 1, settle: 2, window: 3, media: defaultMedia}, ""},
		{[]string{"smoke", "-scenario", "rtmp-flv"}, command{kind: "smoke", scenario: "rtmp-flv", media: defaultMedia}, ""},
		// RTSP players of an RTMP publisher.
		{[]string{"run", "-scenario", "rtmp-rtsp"}, command{kind: "run", scenario: "rtmp-rtsp", repeat: 1, settle: 2, window: 3, media: defaultMedia}, ""},
		{[]string{"smoke", "-scenario", "rtmp-rtsp"}, command{kind: "smoke", scenario: "rtmp-rtsp", media: defaultMedia}, ""},
		// The delay probes run only as a probe, which measures delays, not capacity.
		{[]string{"run", "-scenario", "delays"}, command{kind: "run", scenario: "delays", repeat: 1, mode: "probe", settle: 10, window: 60, media: defaultMedia}, ""},
		{[]string{"run", "-scenario", "delays", "-window", "120"}, command{kind: "run", scenario: "delays", repeat: 1, mode: "probe", settle: 10, window: 120, media: defaultMedia}, ""},
		{[]string{"run", "-scenario", "delays", "-mode", "ramp"}, command{}, "probe"},
		{[]string{"run", "-scenario", "delays", "-mode", "hold"}, command{}, "probe"},
		{[]string{"run", "-scenario", "rtmp-rtmp", "-mode", "probe"}, command{}, "probe"},
		{[]string{"run", "-scenario", "delays", "-no-publisher"}, command{}, "no-publisher"},
		{[]string{"smoke", "-scenario", "delays"}, command{kind: "smoke", scenario: "delays", media: defaultMedia}, ""},
		{[]string{"smoke", "-scenario", "unknown"}, command{}, "unknown scenario"},
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
	s, ok = scenarioByName("rtmp-flv")
	if !ok || s.Name != "rtmp-flv" || s.Publisher != "rtmp" || s.Player != "flv" || s.Unit != "clients" {
		t.Fatalf("scenario = %+v, %v", s, ok)
	}
	s, ok = scenarioByName("rtmp-rtsp")
	if !ok || s.Name != "rtmp-rtsp" || s.Publisher != "rtmp" || s.Player != "rtsp" || s.Unit != "clients" {
		t.Fatalf("scenario = %+v, %v", s, ok)
	}
	s, ok = scenarioByName("srt-srt")
	if !ok || s.Name != "srt-srt" || s.Publisher != "srt" || s.Player != "srt" || s.Unit != "clients" {
		t.Fatalf("scenario = %+v, %v", s, ok)
	}
	s, ok = scenarioByName("rtmp-whep")
	if !ok || s.Name != "rtmp-whep" || s.Publisher != "rtmp" || s.Player != "whep" || s.Unit != "clients" {
		t.Fatalf("scenario = %+v, %v", s, ok)
	}
	s, ok = scenarioByName("whip-whep")
	if !ok || s.Name != "whip-whep" || s.Publisher != "whip" || s.Player != "whep" || s.Unit != "clients" {
		t.Fatalf("scenario = %+v, %v", s, ok)
	}
	// The cross-protocol scenarios publish in one protocol and play in another.
	for _, tt := range []struct{ name, publisher, player string }{
		{"srt-rtmp", "srt", "rtmp"}, {"srt-whep", "srt", "whep"}, {"whip-rtmp", "whip", "rtmp"}, {"whip-flv", "whip", "flv"},
	} {
		s, ok = scenarioByName(tt.name)
		if !ok || s.Name != tt.name || s.Publisher != tt.publisher || s.Player != tt.player || s.Unit != "clients" || s.segment != 0 {
			t.Fatalf("scenario = %+v, %v", s, ok)
		}
	}
	if _, ok := scenarioByName("missing"); ok {
		t.Fatal("unknown scenario was accepted")
	}
}

// The ramp starts at 100 clients and stops at the TCP cap for the TCP
// protocols, and at 10 and 2,000 for SRT and RTC. The players are the load, so
// a cross-protocol scenario takes its players' bounds.
func TestScenarioRampBounds(t *testing.T) {
	for _, tt := range []struct {
		name       string
		start, cap int
	}{{"rtmp-rtmp", 100, defaultClientCap}, {"rtmp-flv", 100, defaultClientCap}, {"rtmp-ts", 100, defaultClientCap}, {"rtmp-rtsp", 100, defaultClientCap}, {"srt-srt", 10, 2000}, {"rtmp-whep", 10, 2000}, {"whip-whep", 10, 2000},
		{"srt-rtmp", 100, defaultClientCap}, {"srt-whep", 10, 2000}, {"whip-rtmp", 100, defaultClientCap}, {"whip-flv", 100, defaultClientCap}} {
		s, _ := scenarioByName(tt.name)
		if s.rampStart() != tt.start || s.rampCap() != tt.cap {
			t.Errorf("%s: ramp %d to %d, want %d to %d", tt.name, s.rampStart(), s.rampCap(), tt.start, tt.cap)
		}
	}
}

// Each scenario's publisher protocol has a publisher.
func TestScenarioPublishers(t *testing.T) {
	for name, s := range scenarios {
		if _, ok := publishers[s.Publisher]; !ok {
			t.Errorf("%s: no publisher for %q", name, s.Publisher)
		}
	}
}

// The SRT scenario serves SRT to SRT on SRS's SRT server at 10080, the port
// its publisher and players dial, with the shipped defaults otherwise.
func TestSRTScenarioEnvServesSRT(t *testing.T) {
	data, err := os.ReadFile(filepath.Join("env", "srt-srt.env"))
	if err != nil {
		t.Fatal(err)
	}
	for _, line := range []string{"SRS_SRT_SERVER_ENABLED=on", "SRS_SRT_SERVER_LISTEN=" + srtPort, "SRS_VHOST_SRT_ENABLED=on"} {
		if !regexp.MustCompile(`(?m)^` + regexp.QuoteMeta(line) + `$`).Match(data) {
			t.Errorf("srt-srt.env has no %s", line)
		}
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
	for name, s := range scenarios {
		data, err := os.ReadFile(filepath.Join("env", name+".env"))
		if err != nil {
			t.Fatalf("%s: %v", name, err)
		}
		// The delay probes have a few players and no ramp.
		if s.Unit == "probes" {
			continue
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

// The HTTP-FLV scenario serves the stream from SRS's HTTP server on 8080, the
// port its players dial, with the shipped mount.
func TestFLVScenarioEnvServesHTTPFLV(t *testing.T) {
	data, err := os.ReadFile(filepath.Join("env", "rtmp-flv.env"))
	if err != nil {
		t.Fatal(err)
	}
	for _, line := range []string{"SRS_RTMP_LISTEN=1935", "SRS_HTTP_SERVER_ENABLED=on", "SRS_HTTP_SERVER_LISTEN=8080", "SRS_VHOST_HTTP_REMUX_ENABLED=on", "SRS_VHOST_HTTP_REMUX_MOUNT=[vhost]/[app]/[stream].flv"} {
		if !regexp.MustCompile(`(?m)^` + regexp.QuoteMeta(line) + `$`).Match(data) {
			t.Errorf("rtmp-flv.env has no %s", line)
		}
	}
}

// The RTSP scenario serves the RTMP stream over RTSP on 8554, the port its
// players dial, with rtmp_to_rtsp.
func TestRTSPScenarioEnvServesRTSP(t *testing.T) {
	data, err := os.ReadFile(filepath.Join("env", "rtmp-rtsp.env"))
	if err != nil {
		t.Fatal(err)
	}
	for _, line := range []string{"SRS_RTMP_LISTEN=1935", "SRS_RTSP_SERVER_ENABLED=on", "SRS_RTSP_SERVER_LISTEN=" + rtspPort, "SRS_VHOST_RTSP_ENABLED=on", "SRS_VHOST_RTSP_RTMP_TO_RTSP=on"} {
		if !regexp.MustCompile(`(?m)^` + regexp.QuoteMeta(line) + `$`).Match(data) {
			t.Errorf("rtmp-rtsp.env has no %s", line)
		}
	}
}

// The WebRTC scenarios serve WHIP and WHEP on the HTTP API at 1985, the port
// the clients post to, and media on the RTC server at UDP 8000 with 127.0.0.1
// as its candidate, since the load shares SRS's network namespace. Only
// rtmp-whep converts RTMP to RTC; whip-whep keeps rtmp_to_rtc and rtc_to_rtmp
// at their defaults, off.
func TestRTCScenarioEnvsServeWebRTC(t *testing.T) {
	common := []string{"SRS_HTTP_API_ENABLED=on", "SRS_HTTP_API_LISTEN=" + rtcAPIPort, "SRS_RTC_SERVER_ENABLED=on", "SRS_RTC_SERVER_LISTEN=" + rtcPort, "SRS_RTC_SERVER_CANDIDATE=127.0.0.1", "SRS_VHOST_RTC_ENABLED=on"}
	for _, tt := range []struct {
		name   string
		has    []string
		hasNot []string
	}{
		{"rtmp-whep", append([]string{"SRS_RTMP_LISTEN=1935", "SRS_VHOST_RTC_RTMP_TO_RTC=on"}, common...), []string{"SRS_VHOST_RTC_RTC_TO_RTMP"}},
		{"whip-whep", common, []string{"SRS_VHOST_RTC_RTMP_TO_RTC", "SRS_VHOST_RTC_RTC_TO_RTMP"}},
	} {
		checkEnvLines(t, tt.name, tt.has, tt.hasNot)
	}
}

// The cross-protocol scenarios turn on only their own bridge: srt-rtmp
// srt_to_rtmp; srt-whep rtmp_to_rtc, which also bridges SRT to RTC (with
// AAC to Opus), and no srt_to_rtmp; whip-rtmp and whip-flv rtc_to_rtmp (with
// Opus to AAC), whip-flv with HTTP-FLV on the HTTP server at 8080. whip-flv
// also turns off guess_has_av: the bridge sends audio about a second before
// the first IDR, so a waiting player's FLV header would have no video, and
// SRS would drop every video tag.
func TestBridgeScenarioEnvs(t *testing.T) {
	srt := []string{"SRS_RTMP_LISTEN=1935", "SRS_SRT_SERVER_ENABLED=on", "SRS_SRT_SERVER_LISTEN=" + srtPort, "SRS_VHOST_SRT_ENABLED=on"}
	rtc := []string{"SRS_HTTP_API_ENABLED=on", "SRS_HTTP_API_LISTEN=" + rtcAPIPort, "SRS_RTC_SERVER_ENABLED=on", "SRS_RTC_SERVER_LISTEN=" + rtcPort, "SRS_RTC_SERVER_CANDIDATE=127.0.0.1", "SRS_VHOST_RTC_ENABLED=on"}
	flv := []string{"SRS_HTTP_SERVER_ENABLED=on", "SRS_HTTP_SERVER_LISTEN=8080", "SRS_VHOST_HTTP_REMUX_ENABLED=on", "SRS_VHOST_HTTP_REMUX_MOUNT=[vhost]/[app]/[stream].flv"}
	join := func(lists ...[]string) []string {
		var all []string
		for _, l := range lists {
			all = append(all, l...)
		}
		return all
	}
	for _, tt := range []struct {
		name   string
		has    []string
		hasNot []string
	}{
		{"srt-rtmp", append([]string{"SRS_VHOST_SRT_SRT_TO_RTMP=on"}, srt...), []string{"SRS_RTC_SERVER_ENABLED", "SRS_VHOST_RTC_RTMP_TO_RTC"}},
		{"srt-whep", join(srt, rtc, []string{"SRS_VHOST_RTC_RTMP_TO_RTC=on"}), []string{"SRS_VHOST_SRT_SRT_TO_RTMP", "SRS_VHOST_SRT_TO_RTMP", "SRS_VHOST_RTC_RTC_TO_RTMP"}},
		{"whip-rtmp", join([]string{"SRS_RTMP_LISTEN=1935", "SRS_VHOST_RTC_RTC_TO_RTMP=on"}, rtc), []string{"SRS_VHOST_RTC_RTMP_TO_RTC", "SRS_HTTP_SERVER_ENABLED"}},
		{"whip-flv", join([]string{"SRS_RTMP_LISTEN=1935", "SRS_VHOST_RTC_RTC_TO_RTMP=on", "SRS_VHOST_HTTP_REMUX_GUESS_HAS_AV=off"}, rtc, flv), []string{"SRS_VHOST_RTC_RTMP_TO_RTC"}},
	} {
		checkEnvLines(t, tt.name, tt.has, tt.hasNot)
	}
}

// checkEnvLines fails when the scenario's env lacks a line of has, or sets a
// variable of hasNot.
func checkEnvLines(t *testing.T, scenario string, has, hasNot []string) {
	t.Helper()
	data, err := os.ReadFile(filepath.Join("env", scenario+".env"))
	if err != nil {
		t.Fatal(err)
	}
	for _, line := range has {
		if !regexp.MustCompile(`(?m)^` + regexp.QuoteMeta(line) + `$`).Match(data) {
			t.Errorf("%s.env has no %s", scenario, line)
		}
	}
	for _, name := range hasNot {
		if regexp.MustCompile(`(?m)^` + regexp.QuoteMeta(name) + `=`).Match(data) {
			t.Errorf("%s.env sets %s", scenario, name)
		}
	}
}
