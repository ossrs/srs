// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"os"
	"path/filepath"
	"reflect"
	"slices"
	"testing"
	"time"
)

func TestMissingFeatures(t *testing.T) {
	all := `{"code":0,"data":{"features":{"ssl":true,"srt":true,"rtsp":true,"gb28181":true,"rtc":true,"simulator":true,"sanitizer":true}}}`
	if got, err := missingFeatures([]byte(all)); err != nil || len(got) != 0 {
		t.Errorf("missingFeatures(all) = %v, %v", got, err)
	}

	// A false field and a field the server does not report are both missing.
	some := `{"code":0,"data":{"features":{"srt":false,"rtsp":true,"gb28181":true,"rtc":true,"sanitizer":true}}}`
	if got, err := missingFeatures([]byte(some)); err != nil || !reflect.DeepEqual(got, []string{"srt", "simulator"}) {
		t.Errorf("missingFeatures(some) = %v, %v; want [srt simulator]", got, err)
	}

	if _, err := missingFeatures([]byte(`not json`)); err == nil {
		t.Error("missingFeatures(not json) has no error")
	}
	if _, err := missingFeatures([]byte(`{"code":1}`)); err == nil {
		t.Error("missingFeatures(code 1) has no error")
	}
}

func TestCleanStaleRuns(t *testing.T) {
	root := t.TempDir()
	for _, name := range []string{"100", "200", "keep", "300"} {
		if err := os.MkdirAll(filepath.Join(root, name, "srs-1"), 0o755); err != nil {
			t.Fatal(err)
		}
	}
	if err := os.WriteFile(filepath.Join(root, "400"), nil, 0o644); err != nil {
		t.Fatal(err)
	}

	cleanStaleRuns(root, func(pid int) bool { return pid == 200 })

	for name, want := range map[string]bool{"100": false, "200": true, "keep": true, "300": false, "400": true} {
		_, err := os.Stat(filepath.Join(root, name))
		if exists := err == nil; exists != want {
			t.Errorf("%s exists=%v, want %v", name, exists, want)
		}
	}
}

// TestCommonConfig checks the Common server plays with the lowest latency, so
// a player gets each frame at once rather than in 350ms batches, serves
// HTTP-FLV, HLS and DASH with 10s segments, calls every hook, and that its env
// builds.
func TestCommonConfig(t *testing.T) {
	cfg := CommonConfig()
	if !cfg.Hooks {
		t.Error("hooks are off, want every hook on the mock")
	}
	if !cfg.IPv6 {
		t.Error("IPv6 is off, want RTMP on [::] too")
	}
	if !reflect.DeepEqual(cfg.Listeners, CommonListeners) {
		t.Errorf("listeners %v, want %v", cfg.Listeners, CommonListeners)
	}
	want := map[string]string{
		"SRS_VHOST_MIN_LATENCY":             "on",
		"SRS_VHOST_PLAY_MW_LATENCY":         "0",
		"SRS_VHOST_HTTP_REMUX_ENABLED":      "on",
		"SRS_VHOST_HTTP_REMUX_MOUNT":        "[vhost]/[app]/[stream].flv",
		"SRS_VHOST_HLS_ENABLED":             "on",
		"SRS_VHOST_DASH_ENABLED":            "on",
		"SRS_VHOST_DASH_DASH_FRAGMENT":      "10",
		"SRS_VHOST_RTC_ENABLED":             "on",
		"SRS_RTC_SERVER_CANDIDATE":          "127.0.0.1",
		"SRS_RTC_SERVER_KEEP_API_DOMAIN":    "on",
		"SRS_RTC_SERVER_RESOLVE_API_DOMAIN": "off",
		"SRS_VHOST_SRT_ENABLED":             "on",
		"SRS_VHOST_SRT_TO_RTMP":             "on",
		"SRS_SRT_SERVER_RECVLATENCY":        "0",
	}
	if !reflect.DeepEqual(cfg.Env, want) {
		t.Errorf("env %v, want %v", cfg.Env, want)
	}
	ports := map[Listener]int{}
	for i, l := range cfg.Listeners {
		ports[l] = 14000 + i
	}
	if _, err := BuildEnv(cfg, ports, "/tmp/x", TLSFiles{Key: "k", Cert: "c"}); err != nil {
		t.Errorf("build env: %v", err)
	}
}

// TestRemuxConfig checks a remux variant serves its extension, plays with
// Common's latency, has only the listeners it needs, and that its env builds.
func TestRemuxConfig(t *testing.T) {
	cfg := RemuxConfig("ts")
	if want := []Listener{RTMP, HTTPAPI, HTTPServer}; !reflect.DeepEqual(cfg.Listeners, want) {
		t.Errorf("listeners %v, want %v", cfg.Listeners, want)
	}
	want := map[string]string{
		"SRS_VHOST_MIN_LATENCY":        "on",
		"SRS_VHOST_PLAY_MW_LATENCY":    "0",
		"SRS_VHOST_HTTP_REMUX_ENABLED": "on",
		"SRS_VHOST_HTTP_REMUX_MOUNT":   "[vhost]/[app]/[stream].ts",
	}
	if !reflect.DeepEqual(cfg.Env, want) {
		t.Errorf("env %v, want %v", cfg.Env, want)
	}
	ports := map[Listener]int{RTMP: 14000, HTTPAPI: 14001, HTTPServer: 14002}
	if _, err := BuildEnv(cfg, ports, "/tmp/x", TLSFiles{Key: "k", Cert: "c"}); err != nil {
		t.Errorf("build env: %v", err)
	}
}

// TestPlayConfig checks a play variant has RTMP and the HTTP API, Common's
// latency, SRS's play defaults for the zero Play, and each setting it asks
// for, and that its env builds.
func TestPlayConfig(t *testing.T) {
	latency := map[string]string{
		"SRS_VHOST_MIN_LATENCY":     "on",
		"SRS_VHOST_PLAY_MW_LATENCY": "0",
	}
	with := func(kv ...string) map[string]string {
		m := map[string]string{}
		for k, v := range latency {
			m[k] = v
		}
		for i := 0; i < len(kv); i += 2 {
			m[kv[i]] = kv[i+1]
		}
		return m
	}
	cases := []struct {
		name string
		play Play
		env  map[string]string
	}{
		{"Defaults", Play{}, with()},
		{"GopCacheOff", Play{GopCacheOff: true}, with("SRS_VHOST_PLAY_GOP_CACHE", "off")},
		{"GopCacheMaxFrames", Play{GopCacheMaxFrames: 10}, with("SRS_VHOST_PLAY_GOP_CACHE_MAX_FRAMES", "10")},
		{"QueueLength", Play{QueueLength: 2 * time.Second}, with("SRS_VHOST_PLAY_QUEUE_LENGTH", "2")},
		{"ReduceSequenceHeader", Play{ReduceSequenceHeader: true}, with("SRS_VHOST_PLAY_REDUCE_SEQUENCE_HEADER", "on")},
		{"TimeJitterZero", Play{TimeJitter: "zero"}, with("SRS_VHOST_PLAY_TIME_JITTER", "zero")},
		{"TimeJitterOff", Play{TimeJitter: "off"}, with("SRS_VHOST_PLAY_TIME_JITTER", "off")},
		{"ATC", Play{ATC: true, MixCorrect: true}, with("SRS_VHOST_PLAY_ATC", "on", "SRS_VHOST_PLAY_MIX_CORRECT", "on")},
		{"ATCAuto", Play{ATCAuto: true}, with("SRS_VHOST_PLAY_ATC_AUTO", "on")},
		{"SendMinInterval", Play{SendMinInterval: 20 * time.Millisecond}, with("SRS_VHOST_PLAY_SEND_MIN_INTERVAL", "20")},
		{"MergedWrite", Play{MergedWrite: &Merged{WriteLatency: 200 * time.Millisecond, WriteMsgs: 4, ReadLatency: 100 * time.Millisecond}}, map[string]string{
			"SRS_VHOST_MIN_LATENCY":        "off",
			"SRS_VHOST_PLAY_MW_LATENCY":    "200",
			"SRS_VHOST_PLAY_MW_MSGS":       "4",
			"SRS_VHOST_PUBLISH_MR":         "on",
			"SRS_VHOST_PUBLISH_MR_LATENCY": "100",
		}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			cfg := PlayConfig(tc.play)
			if want := []Listener{RTMP, HTTPAPI}; !reflect.DeepEqual(cfg.Listeners, want) {
				t.Errorf("listeners %v, want %v", cfg.Listeners, want)
			}
			if !reflect.DeepEqual(cfg.Env, tc.env) {
				t.Errorf("env %v, want %v", cfg.Env, tc.env)
			}
			if _, err := BuildEnv(cfg, map[Listener]int{RTMP: 14000, HTTPAPI: 14001}, "/tmp/x", TLSFiles{}); err != nil {
				t.Errorf("build env: %v", err)
			}
		})
	}
}

// TestPublishConfig checks the env of each publish setting, beside Common's
// lowest play latency, and that it builds.
func TestPublishConfig(t *testing.T) {
	with := func(kv ...string) map[string]string {
		m := map[string]string{
			"SRS_VHOST_MIN_LATENCY":     "on",
			"SRS_VHOST_PLAY_MW_LATENCY": "0",
		}
		for i := 0; i < len(kv); i += 2 {
			m[kv[i]] = kv[i+1]
		}
		return m
	}
	cases := []struct {
		name    string
		publish Publish
		env     map[string]string
	}{
		{"Defaults", Publish{}, with()},
		{"FirstPacketTimeout", Publish{FirstPacketTimeout: 100 * time.Millisecond}, with("SRS_VHOST_PUBLISH_FIRSTPKT_TIMEOUT", "100")},
		{"NormalTimeout", Publish{NormalTimeout: 200 * time.Millisecond}, with("SRS_VHOST_PUBLISH_NORMAL_TIMEOUT", "200")},
		{"KickoffForIdle", Publish{KickoffForIdle: 300 * time.Millisecond}, with("SRS_VHOST_PUBLISH_KICKOFF_FOR_IDLE", "0.3")},
		{"KickoffWholeSeconds", Publish{KickoffForIdle: 2 * time.Second}, with("SRS_VHOST_PUBLISH_KICKOFF_FOR_IDLE", "2")},
		{"All", Publish{FirstPacketTimeout: time.Second, NormalTimeout: 1500 * time.Millisecond, KickoffForIdle: 1500 * time.Millisecond}, with(
			"SRS_VHOST_PUBLISH_FIRSTPKT_TIMEOUT", "1000",
			"SRS_VHOST_PUBLISH_NORMAL_TIMEOUT", "1500",
			"SRS_VHOST_PUBLISH_KICKOFF_FOR_IDLE", "1.5",
		)},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			cfg := PublishConfig(tc.publish)
			if want := []Listener{RTMP, HTTPAPI}; !reflect.DeepEqual(cfg.Listeners, want) {
				t.Errorf("listeners %v, want %v", cfg.Listeners, want)
			}
			if !reflect.DeepEqual(cfg.Env, tc.env) {
				t.Errorf("env %v, want %v", cfg.Env, tc.env)
			}
			if _, err := BuildEnv(cfg, map[Listener]int{RTMP: 14000, HTTPAPI: 14001}, "/tmp/x", TLSFiles{}); err != nil {
				t.Errorf("build env: %v", err)
			}
		})
	}
}

// TestRemuxVariant checks that the zero Remux is RemuxConfig, and that each
// remux setting it asks for is set, and that its env builds.
func TestRemuxVariant(t *testing.T) {
	if got, want := RemuxVariant("flv", Remux{}), RemuxConfig("flv"); !reflect.DeepEqual(got, want) {
		t.Errorf("zero Remux %v, want %v", got, want)
	}

	cases := []struct {
		name string
		r    Remux
		want map[string]string
	}{
		{"GuessOff", Remux{GuessOff: true}, map[string]string{"SRS_VHOST_HTTP_REMUX_GUESS_HAS_AV": "off"}},
		{"NoAudio", Remux{NoAudio: true}, map[string]string{"SRS_VHOST_HTTP_REMUX_HAS_AUDIO": "off"}},
		{"NoVideo", Remux{NoVideo: true}, map[string]string{"SRS_VHOST_HTTP_REMUX_HAS_VIDEO": "off"}},
		{"KeepUnmatched", Remux{KeepUnmatched: true}, map[string]string{"SRS_VHOST_HTTP_REMUX_DROP_IF_NOT_MATCH": "off"}},
		{"FastCache", Remux{FastCache: 1500 * time.Millisecond}, map[string]string{"SRS_VHOST_HTTP_REMUX_FAST_CACHE": "1.5"}},
		{"CrossdomainOff", Remux{CrossdomainOff: true}, map[string]string{"SRS_HTTP_SERVER_CROSSDOMAIN": "off"}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			cfg := RemuxVariant("flv", tc.r)
			want := RemuxConfig("flv").Env
			for k, v := range tc.want {
				want[k] = v
			}
			if !reflect.DeepEqual(cfg.Env, want) {
				t.Errorf("env %v, want %v", cfg.Env, want)
			}
		})
	}

	t.Run("Off", func(t *testing.T) {
		cfg := RemuxVariant("flv", Remux{Off: true})
		if want := []Listener{RTMP, HTTPAPI, HTTPServer}; !reflect.DeepEqual(cfg.Listeners, want) {
			t.Errorf("listeners %v, want %v", cfg.Listeners, want)
		}
		want := map[string]string{
			"SRS_VHOST_MIN_LATENCY":        "on",
			"SRS_VHOST_PLAY_MW_LATENCY":    "0",
			"SRS_VHOST_HTTP_REMUX_ENABLED": "off",
		}
		if !reflect.DeepEqual(cfg.Env, want) {
			t.Errorf("env %v, want %v", cfg.Env, want)
		}
	})

	all := Remux{GuessOff: true, NoAudio: true, KeepUnmatched: true, FastCache: time.Second, CrossdomainOff: true}
	ports := map[Listener]int{RTMP: 14000, HTTPAPI: 14001, HTTPServer: 14002}
	if _, err := BuildEnv(RemuxVariant("flv", all), ports, "/tmp/x", TLSFiles{Key: "k", Cert: "c"}); err != nil {
		t.Errorf("build env: %v", err)
	}
}

// TestHLSConfig checks that the zero HLS turns HLS on with SRS's defaults,
// that each HLS setting it asks for is set, and that its env builds.
func TestHLSConfig(t *testing.T) {
	base := map[string]string{
		"SRS_VHOST_MIN_LATENCY":     "on",
		"SRS_VHOST_PLAY_MW_LATENCY": "0",
		"SRS_VHOST_HLS_ENABLED":     "on",
	}
	cases := []struct {
		name string
		h    HLS
		want map[string]string
	}{
		{"Defaults", HLS{}, nil},
		{"Fragment", HLS{Fragment: 2 * time.Second}, map[string]string{"SRS_VHOST_HLS_HLS_FRAGMENT": "2"}},
		{"Window", HLS{Window: 7500 * time.Millisecond}, map[string]string{"SRS_VHOST_HLS_HLS_WINDOW": "7.5"}},
		{"TDRatio", HLS{TDRatio: 1.5}, map[string]string{"SRS_VHOST_HLS_HLS_TD_RATIO": "1.5"}},
		{"AOFRatio", HLS{AOFRatio: 2.5}, map[string]string{"SRS_VHOST_HLS_HLS_AOF_RATIO": "2.5"}},
		{"Path", HLS{Path: "./hls"}, map[string]string{"SRS_VHOST_HLS_HLS_PATH": "./hls", "SRS_HTTP_SERVER_DIR": "./hls"}},
		{"M3U8File", HLS{M3U8File: "[app]/[stream]/index.m3u8"}, map[string]string{"SRS_VHOST_HLS_HLS_M3U8_FILE": "[app]/[stream]/index.m3u8"}},
		{"TSFile", HLS{TSFile: "[app]/[stream]/seg-[seq].ts"}, map[string]string{"SRS_VHOST_HLS_HLS_TS_FILE": "[app]/[stream]/seg-[seq].ts"}},
		{"EntryPrefix", HLS{EntryPrefix: "http://cdn.example.com"}, map[string]string{"SRS_VHOST_HLS_HLS_ENTRY_PREFIX": "http://cdn.example.com"}},
		{"TSFloor", HLS{TSFloor: true}, map[string]string{"SRS_VHOST_HLS_HLS_TS_FLOOR": "on"}},
		{"FMP4", HLS{FMP4: true}, map[string]string{"SRS_VHOST_HLS_HLS_USE_FMP4": "on"}},
		{"FMP4File", HLS{FMP4File: "[app]/[stream]-seg-[seq].m4s"}, map[string]string{"SRS_VHOST_HLS_HLS_FMP4_FILE": "[app]/[stream]-seg-[seq].m4s"}},
		{"InitFile", HLS{InitFile: "[app]/[stream]-init.mp4"}, map[string]string{"SRS_VHOST_HLS_HLS_INIT_FILE": "[app]/[stream]-init.mp4"}},
		{"Keys", HLS{Keys: true}, map[string]string{"SRS_VHOST_HLS_HLS_KEYS": "on"}},
		{"FragmentsPerKey", HLS{Keys: true, FragmentsPerKey: 2}, map[string]string{"SRS_VHOST_HLS_HLS_KEYS": "on", "SRS_VHOST_HLS_HLS_FRAGMENTS_PER_KEY": "2"}},
		{"KeyURL", HLS{KeyURL: "http://cdn.example.com/?token=a&sig=b"}, map[string]string{"SRS_VHOST_HLS_HLS_KEY_URL": "http://cdn.example.com/?token=a&sig=b"}},
		{"NoCtx", HLS{NoCtx: true}, map[string]string{"SRS_VHOST_HLS_HLS_CTX": "off"}},
		{"NoTSCtx", HLS{NoTSCtx: true}, map[string]string{"SRS_VHOST_HLS_HLS_TS_CTX": "off"}},
		{"MasterRelative", HLS{MasterRelative: true}, map[string]string{"SRS_VHOST_HLS_HLS_MASTER_M3U8_PATH_RELATIVE": "on"}},
		{"Dispose", HLS{Dispose: time.Second}, map[string]string{"SRS_VHOST_HLS_HLS_DISPOSE": "1"}},
		{"NoCleanup", HLS{NoCleanup: true}, map[string]string{"SRS_VHOST_HLS_HLS_CLEANUP": "off"}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			cfg := HLSConfig(tc.h)
			if want := []Listener{RTMP, HTTPAPI, HTTPServer}; !reflect.DeepEqual(cfg.Listeners, want) {
				t.Errorf("listeners %v, want %v", cfg.Listeners, want)
			}
			want := map[string]string{}
			for k, v := range base {
				want[k] = v
			}
			for k, v := range tc.want {
				want[k] = v
			}
			if !reflect.DeepEqual(cfg.Env, want) {
				t.Errorf("env %v, want %v", cfg.Env, want)
			}
			if _, err := BuildEnv(cfg, map[Listener]int{RTMP: 14000, HTTPAPI: 14001, HTTPServer: 14002}, "/tmp/x", TLSFiles{}); err != nil {
				t.Errorf("build env: %v", err)
			}
		})
	}
}

func TestWithHLS(t *testing.T) {
	// A play variant gets HLS on with the settings, and the HTTP server to
	// serve the files; its own env stays.
	base := PlayConfig(Play{QueueLength: time.Second})
	cfg := WithHLS(base, HLS{Fragment: time.Second, Dispose: time.Second})
	if want := []Listener{RTMP, HTTPAPI, HTTPServer}; !reflect.DeepEqual(cfg.Listeners, want) {
		t.Errorf("listeners %v, want %v", cfg.Listeners, want)
	}
	want := map[string]string{
		"SRS_VHOST_HLS_ENABLED":      "on",
		"SRS_VHOST_HLS_HLS_FRAGMENT": "1",
		"SRS_VHOST_HLS_HLS_DISPOSE":  "1",
	}
	for k, v := range base.Env {
		want[k] = v
	}
	if !reflect.DeepEqual(cfg.Env, want) {
		t.Errorf("env %v, want %v", cfg.Env, want)
	}
	if _, ok := base.Env["SRS_VHOST_HLS_ENABLED"]; ok || len(base.Listeners) != 2 {
		t.Errorf("the base config changed: %+v", base)
	}

	// A config that has the HTTP server keeps its listeners as they are.
	remux := RemuxConfig("flv")
	if cfg := WithHLS(remux, HLS{}); !reflect.DeepEqual(cfg.Listeners, remux.Listeners) {
		t.Errorf("listeners %v, want %v", cfg.Listeners, remux.Listeners)
	}
}

func TestWithDASH(t *testing.T) {
	cases := []struct {
		name string
		d    DASH
		want map[string]string
	}{
		{"Defaults", DASH{}, nil},
		{"Fragment", DASH{Fragment: 2500 * time.Millisecond}, map[string]string{"SRS_VHOST_DASH_DASH_FRAGMENT": "2.5"}},
		{"WindowSize", DASH{WindowSize: 3}, map[string]string{"SRS_VHOST_DASH_DASH_WINDOW_SIZE": "3"}},
		{"Path", DASH{Path: "./objs/nginx/html/dash"}, map[string]string{"SRS_VHOST_DASH_DASH_PATH": "./objs/nginx/html/dash"}},
		{"MPDFile", DASH{MPDFile: "[app]/[stream]/index.mpd"}, map[string]string{"SRS_VHOST_DASH_DASH_MPD_FILE": "[app]/[stream]/index.mpd"}},
		{"Dispose", DASH{Dispose: 2 * time.Second}, map[string]string{"SRS_VHOST_DASH_DASH_DISPOSE": "2"}},
		{"NoCleanup", DASH{NoCleanup: true}, map[string]string{"SRS_VHOST_DASH_DASH_CLEANUP": "off"}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			base := RemuxConfig("flv")
			cfg := WithDASH(base, tc.d)
			if !reflect.DeepEqual(cfg.Listeners, base.Listeners) {
				t.Errorf("listeners %v, want %v", cfg.Listeners, base.Listeners)
			}
			want := map[string]string{"SRS_VHOST_DASH_ENABLED": "on"}
			for k, v := range base.Env {
				want[k] = v
			}
			for k, v := range tc.want {
				want[k] = v
			}
			if !reflect.DeepEqual(cfg.Env, want) {
				t.Errorf("env %v, want %v", cfg.Env, want)
			}
			if _, ok := base.Env["SRS_VHOST_DASH_ENABLED"]; ok {
				t.Errorf("the base config changed: %v", base.Env)
			}
		})
	}

	// The HTTP server keeps its dir, which serves the DASH files under it.
	t.Run("KeepsHTTPDir", func(t *testing.T) {
		cfg := WithDASH(HLSConfig(HLS{Path: "./hls"}), DASH{Path: "./hls/dash"})
		if cfg.Env["SRS_HTTP_SERVER_DIR"] != "./hls" || cfg.Env["SRS_VHOST_DASH_DASH_PATH"] != "./hls/dash" {
			t.Errorf("env %v", cfg.Env)
		}
	})
}

// TestWithRTC checks a variant with WebRTC on: over UDP only, with
// 127.0.0.1 as its candidate like Common, and each WebRTC setting.
func TestWithRTC(t *testing.T) {
	cases := []struct {
		name string
		r    RTC
		want map[string]string
	}{
		{"Defaults", RTC{}, nil},
		{"DTLSRole", RTC{DTLSRole: "active"}, map[string]string{"SRS_VHOST_RTC_DTLS_ROLE": "active"}},
		{"StunStrictCheck", RTC{StunStrictCheck: true}, map[string]string{"SRS_VHOST_RTC_STUN_STRICT_CHECK": "on"}},
		{"NoAPICandidates", RTC{NoAPICandidates: true}, map[string]string{"SRS_RTC_SERVER_API_AS_CANDIDATES": "off"}},
		{"NackPreferRTX", RTC{NackPreferRTX: true}, map[string]string{"SRS_VHOST_RTC_NACK_PREFER_RTX": "on"}},
		{"NoNack", RTC{NoNack: true}, map[string]string{"SRS_VHOST_RTC_NACK": "off"}},
		{"NoTWCC", RTC{NoTWCC: true}, map[string]string{"SRS_VHOST_RTC_TWCC": "off"}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			base := RemuxConfig("flv")
			cfg := WithRTC(base, tc.r)
			if want := append(slices.Clone(base.Listeners), RTCUDP); !reflect.DeepEqual(cfg.Listeners, want) {
				t.Errorf("listeners %v, want %v", cfg.Listeners, want)
			}
			want := map[string]string{"SRS_VHOST_RTC_ENABLED": "on", "SRS_RTC_SERVER_CANDIDATE": "127.0.0.1"}
			for k, v := range base.Env {
				want[k] = v
			}
			for k, v := range tc.want {
				want[k] = v
			}
			if !reflect.DeepEqual(cfg.Env, want) {
				t.Errorf("env %v, want %v", cfg.Env, want)
			}
			if _, ok := base.Env["SRS_VHOST_RTC_ENABLED"]; ok || len(base.Listeners) != 3 {
				t.Errorf("the base config changed: %v %v", base.Listeners, base.Env)
			}
			ports := map[Listener]int{}
			for i, l := range cfg.Listeners {
				ports[l] = 14000 + i
			}
			env, err := BuildEnv(cfg, ports, "/tmp/x", TLSFiles{})
			if err != nil {
				t.Fatalf("build env: %v", err)
			}
			if slices.Contains(env, "SRS_RTC_SERVER_PROTOCOL=all") {
				t.Errorf("env %v turns on WebRTC over TCP", env)
			}
		})
	}
	// A config with WebRTC on keeps its listeners.
	if cfg := WithRTC(CommonConfig(), RTC{}); !reflect.DeepEqual(cfg.Listeners, CommonListeners) {
		t.Errorf("listeners %v, want Common's", cfg.Listeners)
	}
}

// TestWithSRT checks an SRT variant: the listener, SRT and its conversion to
// RTMP on, Common's latency, and its own settings, on a copy of the base.
func TestWithSRT(t *testing.T) {
	cases := []struct {
		name string
		s    SRTOptions
		want map[string]string
	}{
		{"Defaults", SRTOptions{}, nil},
		{"DefaultApp", SRTOptions{DefaultApp: "srtapp"}, map[string]string{"SRS_SRT_SERVER_DEFAULT_APP": "srtapp"}},
		{"DefaultMode", SRTOptions{DefaultMode: "publish"}, map[string]string{"SRS_SRT_SERVER_DEFAULT_MODE": "publish"}},
		{"DefaultStreamID", SRTOptions{DefaultStreamID: "#!::r=srtapp/default,m=request"},
			map[string]string{"SRS_SRT_SERVER_DEFAULT_STREAMID": "#!::r=srtapp/default,m=request"}},
		{"Passphrase", SRTOptions{Passphrase: "srs-integ-passphrase", PBKeyLen: 16},
			map[string]string{"SRS_SRT_SERVER_PASSPHRASE": "srs-integ-passphrase", "SRS_SRT_SERVER_PBKEYLEN": "16"}},
		{"Latencies", SRTOptions{RecvLatency: 250 * time.Millisecond, PeerLatency: 150 * time.Millisecond},
			map[string]string{"SRS_SRT_SERVER_RECVLATENCY": "250", "SRS_SRT_SERVER_PEERLATENCY": "150"}},
		// An empty value is a variable left out, so SRS's default applies.
		{"LatencyOnly", SRTOptions{Latency: 300 * time.Millisecond, KeepRecvLatency: true},
			map[string]string{"SRS_SRT_SERVER_LATENCY": "300", "SRS_SRT_SERVER_RECVLATENCY": ""}},
		{"PeerIdleTimeout", SRTOptions{PeerIdleTimeout: 1500 * time.Millisecond},
			map[string]string{"SRS_SRT_SERVER_PEER_IDLE_TIMEOUT": "1500"}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			base := RemuxConfig("flv")
			cfg := WithSRT(base, tc.s)
			if want := append(slices.Clone(base.Listeners), SRT); !reflect.DeepEqual(cfg.Listeners, want) {
				t.Errorf("listeners %v, want %v", cfg.Listeners, want)
			}
			want := map[string]string{
				"SRS_VHOST_SRT_ENABLED":      "on",
				"SRS_VHOST_SRT_TO_RTMP":      "on",
				"SRS_SRT_SERVER_RECVLATENCY": "0",
			}
			for k, v := range base.Env {
				want[k] = v
			}
			for k, v := range tc.want {
				if v == "" {
					delete(want, k)
				} else {
					want[k] = v
				}
			}
			if !reflect.DeepEqual(cfg.Env, want) {
				t.Errorf("env %v, want %v", cfg.Env, want)
			}
			if _, ok := base.Env["SRS_VHOST_SRT_ENABLED"]; ok || len(base.Listeners) != 3 {
				t.Errorf("the base config changed: %v %v", base.Listeners, base.Env)
			}
			ports := map[Listener]int{}
			for i, l := range cfg.Listeners {
				ports[l] = 14000 + i
			}
			if _, err := BuildEnv(cfg, ports, "/tmp/x", TLSFiles{}); err != nil {
				t.Fatalf("build env: %v", err)
			}
		})
	}
	// A config with SRT on keeps its listeners.
	if cfg := WithSRT(CommonConfig(), SRTOptions{}); !reflect.DeepEqual(cfg.Listeners, CommonListeners) {
		t.Errorf("listeners %v, want Common's", cfg.Listeners)
	}
}
