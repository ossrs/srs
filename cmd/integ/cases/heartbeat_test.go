// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"errors"
	"net"
	"net/http"
	"strconv"
	"sync/atomic"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/mock"
	"srs-integ/internal/server"
)

const (
	// heartbeatInterval is the heartbeat interval of the heartbeat servers,
	// the smallest, since SRS beats on whole seconds.
	heartbeatInterval = time.Second
	// heartbeatSlack is how far from the interval the time between two
	// heartbeats may be: SRS's timer fires on its own ticks, and the mock
	// stamps a heartbeat when it arrives.
	heartbeatSlack = 300 * time.Millisecond
	// heartbeatDeviceID, heartbeatIP and heartbeatToken are the device id,
	// ip and bearer token of the full heartbeat.
	heartbeatDeviceID = "integ-device"
	heartbeatIP       = "10.11.12.13"
	heartbeatToken    = "integ-beat-token"
	// heartbeatQuery is the query of the full heartbeat's URL.
	heartbeatQuery = "k1=v1&k2=v2"
)

// sharedHeartbeatFull is the heartbeat of the timer server SegmentsKeep:
// every 1s, with every setting on. Its other groups never read the mock's
// heartbeats.
var sharedHeartbeatFull = server.Heartbeat{
	Interval:  heartbeatInterval,
	DeviceID:  heartbeatDeviceID,
	IP:        heartbeatIP,
	Summaries: true,
	Ports:     true,
	Token:     heartbeatToken,
	Query:     heartbeatQuery,
}

// sharedHeartbeatDefaults is the heartbeat of the timer server
// PublishTimeout: every 1s, every other setting left at SRS's default.
var sharedHeartbeatDefaults = server.Heartbeat{Interval: heartbeatInterval}

// heartbeatOf matches the heartbeats that arrive after a time.
func heartbeatOf(after time.Time) mock.Match {
	return mock.Match{Path: server.HeartbeatPath, Func: func(r mock.Request) bool { return r.Time.After(after) }}
}

// nextHeartbeats waits for the next n heartbeats of a server and returns them.
func nextHeartbeats(c *check.C, srs *server.Server, n int) []mock.Request {
	c.Helper()
	after := time.Now()
	ctx, cancel := context.WithTimeout(context.Background(), time.Duration(n)*heartbeatInterval+2*time.Second)
	defer cancel()
	var out []mock.Request
	for len(out) < n {
		r, err := srs.Hooks.Wait(ctx, heartbeatOf(after))
		c.Must(err, "heartbeat "+strconv.Itoa(len(out)+1))
		out = append(out, r)
		after = r.Time
	}
	return out
}

// checkBeats checks the time between each two heartbeats is the interval.
func checkBeats(c *check.C, beats []mock.Request) {
	c.Helper()
	for i := 1; i < len(beats); i++ {
		d := beats[i].Time.Sub(beats[i-1].Time)
		c.Check(d >= heartbeatInterval-heartbeatSlack && d <= heartbeatInterval+heartbeatSlack,
			"heartbeat %d came %v after the one before, want %v", i+1, d, heartbeatInterval)
	}
}

// checkServerIDs checks the ids of a heartbeat are the server's, as its API
// gives them, and its pid the process's.
func checkServerIDs(c *check.C, srs *server.Server, beat mock.Request) {
	c.Helper()
	v := apiObject(c, srs, "/api/v1/versions")
	for _, f := range []string{"server", "service", "pid"} {
		c.CheckEqual(beat.JSON[f], v[f], "heartbeat "+f)
	}
	c.CheckEqual(beat.JSON["pid"], strconv.Itoa(srs.PID()), "heartbeat pid, the process's")
}

// hostIPs are the addresses of this host's interfaces.
func hostIPs(c *check.C) map[string]bool {
	c.Helper()
	addrs, err := net.InterfaceAddrs()
	c.Must(err, "interface addresses")
	ips := map[string]bool{}
	for _, a := range addrs {
		if n, ok := a.(*net.IPNet); ok {
			ips[n.IP.String()] = true
		}
	}
	return ips
}

// TestTimer_Heartbeat_Full checks the heartbeat with every setting on, on
// the shared timer server SegmentsKeep: every 1s, a POST with the device id,
// the ip, the server's ids, its summaries and its ports, and a bearer token.
func TestTimer_Heartbeat_Full(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "SegmentsKeep", sharedSegmentsKeep())

	// EverySecond waits for 3 heartbeats: each comes 1s after the one before.
	check.Case(t, "EverySecond", check.Timer, func(c *check.C) {
		checkBeats(c, nextHeartbeats(c, srs, 3))
	})

	// Request checks how a heartbeat is sent: a POST of JSON to the URL's
	// path, with its length.
	check.Case(t, "Request", check.Timer, func(c *check.C) {
		beat := nextHeartbeats(c, srs, 1)[0]
		c.CheckEqual(beat.Method, http.MethodPost, "method")
		c.CheckEqual(beat.Header.Get("Content-Type"), "application/json", "content type")
		c.CheckEqual(beat.Header.Get("Content-Length"), strconv.Itoa(len(beat.Body)), "content length")
		c.Check(beat.JSON != nil, "the body is not a JSON object: %s", beat.Body)
	})

	// Fields checks every field: the device id and the ip set, the server's
	// ids, and the summaries and the ports, nothing else.
	check.Case(t, "Fields", check.Timer, func(c *check.C) {
		beat := nextHeartbeats(c, srs, 1)[0]
		checkFields(c, beat.JSON, []string{"device_id", "ip", "server", "service", "pid", "summaries", "rtmp", "http", "api", "srt", "rtc"}, "heartbeat")
		c.CheckEqual(beat.JSON["device_id"], heartbeatDeviceID, "device_id")
		c.CheckEqual(beat.JSON["ip"], heartbeatIP, "ip, SRS_DEVICE_IP")
		checkServerIDs(c, srs, beat)
	})

	// Summaries checks the summaries are those of /api/v1/summaries: its
	// data, with the same fields, for this process, at the time of the
	// heartbeat.
	check.Case(t, "Summaries", check.Timer, func(c *check.C) {
		beat := nextHeartbeats(c, srs, 1)[0]
		summaries := object(c, beat.JSON, "summaries")
		checkFields(c, summaries, []string{"data"}, "heartbeat summaries")
		data := object(c, summaries, "data")
		api := object(c, apiObject(c, srs, "/api/v1/summaries"), "data")
		checkFields(c, data, keysOf(api), "heartbeat summaries data")
		checkFields(c, object(c, data, "self"), keysOf(object(c, api, "self")), "heartbeat summaries self")
		checkFields(c, object(c, data, "system"), keysOf(object(c, api, "system")), "heartbeat summaries system")
		c.CheckEqual(data["ok"], procRead(), "summaries ok, true only where SRS reads /proc")
		c.CheckEqual(num(c, object(c, data, "self"), "pid"), float64(srs.PID()), "summaries self pid")
		at := time.UnixMilli(int64(num(c, data, "now_ms")))
		c.Check(at.Sub(beat.Time).Abs() <= 500*time.Millisecond, "summaries now_ms %v, want the time of the heartbeat %v", at, beat.Time)
	})

	// Ports checks the listen endpoints of each listener the server has on:
	// RTMP, the HTTP server, the API, SRT and WebRTC over UDP; RTSP and
	// WebRTC over TCP are off, so not listed.
	check.Case(t, "Ports", check.Timer, func(c *check.C) {
		beat := nextHeartbeats(c, srs, 1)[0]
		port := func(l server.Listener) []any { return []any{strconv.Itoa(srs.Ports[l])} }
		c.CheckEqual(beat.JSON["rtmp"], port(server.RTMP), "rtmp")
		c.CheckEqual(beat.JSON["http"], port(server.HTTPServer), "http")
		c.CheckEqual(beat.JSON["api"], port(server.HTTPAPI), "api")
		c.CheckEqual(beat.JSON["srt"], port(server.SRT), "srt")
		c.CheckEqual(beat.JSON["rtc"], []any{"udp://" + strconv.Itoa(srs.Ports[server.RTCUDP])}, "rtc")
		_, rtsp := beat.JSON["rtsp"]
		c.Check(!rtsp, "rtsp is listed, but it is off: %v", beat.JSON["rtsp"])
	})

	// BearerToken checks each heartbeat has the bearer token.
	check.Case(t, "BearerToken", check.Timer, func(c *check.C) {
		for i, beat := range nextHeartbeats(c, srs, 2) {
			c.CheckEqual(beat.Header.Values("Authorization"), []string{"Bearer " + heartbeatToken}, "Authorization of heartbeat "+strconv.Itoa(i+1))
		}
	})

	// UrlQuery checks the heartbeat goes to its URL with the URL's query, as
	// the hooks do: a receiver may take a token or an id from it.
	check.Case(t, "UrlQuery", check.Timer, func(c *check.C) {
		beat := nextHeartbeats(c, srs, 1)[0]
		c.CheckEqual(beat.Query.Encode(), heartbeatQuery, "the query of the heartbeat")
	})
}

// keysOf is the keys of an object.
func keysOf(o map[string]any) []string {
	var keys []string
	for k := range o {
		keys = append(keys, k)
	}
	return keys
}

// TestTimer_Heartbeat_Defaults checks the heartbeat with only its interval
// set, on the shared timer server PublishTimeout: no device id, no
// summaries, no ports and no credentials, and it goes on after its receiver
// fails.
func TestTimer_Heartbeat_Defaults(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "PublishTimeout", sharedPublishTimeout())

	// DefaultFields checks a heartbeat has only the device id, empty, the ip,
	// one of this host's, and the server's ids, and no Authorization.
	check.Case(t, "DefaultFields", check.Timer, func(c *check.C) {
		beat := nextHeartbeats(c, srs, 1)[0]
		checkFields(c, beat.JSON, []string{"device_id", "ip", "server", "service", "pid"}, "heartbeat")
		c.CheckEqual(beat.JSON["device_id"], "", "device_id")
		ip, _ := beat.JSON["ip"].(string)
		c.Check(hostIPs(c)[ip], "ip %q is not an address of this host", ip)
		checkServerIDs(c, srs, beat)
		c.CheckEqual(beat.Header.Values("Authorization"), []string(nil), "Authorization")
	})

	// ErrorKeepsBeating answers the next three heartbeats with HTTP 500:
	// SRS sends each next one on time all the same.
	check.Case(t, "ErrorKeepsBeating", check.Timer, func(c *check.C) {
		var n atomic.Int32
		srs.Hooks.Script(mock.Match{Path: server.HeartbeatPath, Func: func(mock.Request) bool { return n.Add(1) <= 3 }},
			mock.HTTPError(http.StatusInternalServerError))
		beats := nextHeartbeats(c, srs, 4)
		c.Check(n.Load() >= 4, "the mock answered %d heartbeats", n.Load())
		checkBeats(c, beats)
	})
}

// TestHeartbeat_Config checks the heartbeat settings SRS refuses to start
// with: bearer authentication without a type, of another type or without a
// token, and an interval of 0. These refusals are the intended behavior, so
// the cases passed from the start.
func TestHeartbeat_Config(t *testing.T) {
	t.Parallel()
	for _, tt := range []struct {
		name string
		env  map[string]string
	}{
		{"AuthWithoutType", map[string]string{"SRS_HEARTBEAT_AUTH_ENABLED": "on", "SRS_HEARTBEAT_AUTH_TOKEN": heartbeatToken}},
		{"AuthUnknownType", map[string]string{"SRS_HEARTBEAT_AUTH_ENABLED": "on", "SRS_HEARTBEAT_AUTH_TYPE": "basic", "SRS_HEARTBEAT_AUTH_TOKEN": heartbeatToken}},
		{"AuthWithoutToken", map[string]string{"SRS_HEARTBEAT_AUTH_ENABLED": "on", "SRS_HEARTBEAT_AUTH_TYPE": "bearer"}},
		{"IntervalZero", map[string]string{"SRS_HEARTBEAT_INTERVAL": "0"}},
	} {
		check.Case(t, tt.name, check.Normal, func(c *check.C) {
			env := map[string]string{"SRS_HEARTBEAT_ENABLED": "on"}
			for k, v := range tt.env {
				env[k] = v
			}
			srs, err := server.Start(c.Name(), server.Config{Listeners: []server.Listener{server.HTTPAPI}, Env: env})
			if srs != nil {
				srs.Stop()
			}
			var exit *server.ExitError
			if c.Check(errors.As(err, &exit), "start: %v, want SRS to exit before it is ready", err) {
				c.Check(exit.Code != 0, "exit code %d, want an error", exit.Code)
			}
		})
	}
}
