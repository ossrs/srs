// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"fmt"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
)

// Listener is one port a server can listen on.
type Listener int

const (
	RTMP        Listener = iota // RTMP over TCP.
	HTTPAPI                     // The HTTP API over TCP, always on: the adapter reads it.
	HTTPServer                  // The HTTP server for streams and static files, over TCP.
	RTCUDP                      // WebRTC media over UDP.
	RTCTCP                      // WebRTC media over TCP, needs RTCUDP.
	SRT                         // SRT over UDP.
	RTSP                        // RTSP over TCP.
	Exporter                    // The Prometheus exporter over TCP.
	GB                          // GB28181 media over TCP, a stream caster.
	UDPTS                       // MPEG-TS over UDP, a stream caster; one caster per server.
	HTTPSAPI                    // The HTTPS API over TCP, needs HTTPAPI.
	HTTPSServer                 // The HTTPS server over TCP, needs HTTPServer.
	RTMPS                       // RTMPS over TCP.
)

// CommonListeners are the listeners of the Common server: every listener
// except UDPTS, because a server has only one stream caster.
var CommonListeners = []Listener{
	RTMP, HTTPAPI, HTTPServer, RTCUDP, RTCTCP, SRT, RTSP, Exporter, GB, HTTPSAPI, HTTPSServer, RTMPS,
}

// listenerInfo is how SRS turns a listener on: the env var that enables it, the
// one that sets its port, and the listener it needs.
type listenerInfo struct {
	name   string
	udp    bool
	enable string
	listen string
	needs  Listener
	tls    string // The env prefix of its key and cert, for a TLS listener.
}

const noListener Listener = -1

var listeners = map[Listener]listenerInfo{
	RTMP:        {"rtmp", false, "", "SRS_RTMP_LISTEN", noListener, ""},
	HTTPAPI:     {"http-api", false, "SRS_HTTP_API_ENABLED", "SRS_HTTP_API_LISTEN", noListener, ""},
	HTTPServer:  {"http-server", false, "SRS_HTTP_SERVER_ENABLED", "SRS_HTTP_SERVER_LISTEN", noListener, ""},
	RTCUDP:      {"rtc-udp", true, "SRS_RTC_SERVER_ENABLED", "SRS_RTC_SERVER_LISTEN", noListener, ""},
	RTCTCP:      {"rtc-tcp", false, "SRS_RTC_SERVER_TCP_ENABLED", "SRS_RTC_SERVER_TCP_LISTEN", RTCUDP, ""},
	SRT:         {"srt", true, "SRS_SRT_SERVER_ENABLED", "SRS_SRT_SERVER_LISTEN", noListener, ""},
	RTSP:        {"rtsp", false, "SRS_RTSP_SERVER_ENABLED", "SRS_RTSP_SERVER_LISTEN", noListener, ""},
	Exporter:    {"exporter", false, "SRS_EXPORTER_ENABLED", "SRS_EXPORTER_LISTEN", noListener, ""},
	GB:          {"gb28181", false, "", "SRS_STREAM_CASTER_LISTEN", RTMP, ""},
	UDPTS:       {"udp-ts", true, "", "SRS_STREAM_CASTER_LISTEN", RTMP, ""},
	HTTPSAPI:    {"https-api", false, "SRS_HTTP_API_HTTPS_ENABLED", "SRS_HTTP_API_HTTPS_LISTEN", HTTPAPI, "SRS_HTTP_API_HTTPS_"},
	HTTPSServer: {"https-server", false, "SRS_HTTP_SERVER_HTTPS_ENABLED", "SRS_HTTP_SERVER_HTTPS_LISTEN", HTTPServer, "SRS_HTTP_SERVER_HTTPS_"},
	RTMPS:       {"rtmps", false, "SRS_RTMPS_ENABLED", "SRS_RTMPS_LISTEN", noListener, "SRS_RTMPS_"},
}

// UDP reports whether the listener is a UDP port.
func (l Listener) UDP() bool {
	return listeners[l].udp
}

// String returns the listener's name, such as "rtmp".
func (l Listener) String() string {
	if info, ok := listeners[l]; ok {
		return info.name
	}
	return "listener-" + strconv.Itoa(int(l))
}

// Config is what a server turns on.
type Config struct {
	// Listeners to turn on, each with its own port. Every other listener is off.
	Listeners []Listener
	// Env holds more SRS_* settings for a variant, such as a feature that must be
	// off. It cannot change what the adapter sets: the ports, the listeners or the
	// process settings.
	Env map[string]string
	// Hooks starts a mock on a port of its own and points every hook at it.
	Hooks bool
	// HookURLs, with Hooks, sets the URLs of the hooks it names, such as
	// on_publish: each calls every base in order, as <base>/<name>, where a
	// base is a path on the mock, such as /hooks or /more, or HookDown. A hook
	// it doesn't name calls the mock at /hooks.
	HookURLs map[string][]string
	// ForwardBackend, with Hooks, turns forward on with the mock as its
	// backend, at /api/v1/forward: every publish asks it for the URLs to
	// forward to, as an on_forward, and fails unless it answers some or none.
	ForwardBackend bool
	// IPv6 makes RTMP listen on [::] too, on the same port, when the host has
	// IPv6.
	IPv6 bool
	// APIAuth turns the HTTP API's authentication on, nil leaves it off.
	APIAuth *APIAuth
	// OneHTTPPort makes the HTTP API and the HTTP server share one port, and
	// the HTTPS API and the HTTPS server another: SRS then serves the API on
	// the server's port.
	OneHTTPPort bool
}

// TLSFiles are the key and certificate of the TLS listeners.
type TLSFiles struct {
	Key, Cert string
}

// BuildEnv returns the SRS_* env vars, sorted, that start a server in dir
// with cfg, listening on ports.
func BuildEnv(cfg Config, ports map[Listener]int, dir string, tls TLSFiles) ([]string, error) {
	on := map[Listener]bool{}
	for _, l := range cfg.Listeners {
		if _, ok := listeners[l]; !ok {
			return nil, fmt.Errorf("unknown listener %v", l)
		}
		on[l] = true
	}
	if !on[HTTPAPI] {
		return nil, fmt.Errorf("the HTTP API must be on, the adapter reads it")
	}
	if cfg.ForwardBackend && !cfg.Hooks {
		return nil, fmt.Errorf("ForwardBackend needs Hooks, whose mock is the backend")
	}
	if on[GB] && on[UDPTS] {
		return nil, fmt.Errorf("a server has only one stream caster, not both %v and %v", GB, UDPTS)
	}
	if cfg.OneHTTPPort {
		if err := checkOneHTTPPort(on, ports); err != nil {
			return nil, err
		}
	}

	env := map[string]string{
		// The process: in the foreground, logging to the console the adapter
		// captures, with its pid file in its own dir.
		"SRS_DAEMON":                  "off",
		"SRS_LOG_TANK":                "console",
		"SRS_PID":                     filepath.Join(dir, "srs.pid"),
		"SRS_QUERY_LATEST_VERSION":    "off",
		"SRS_CIRCUIT_BREAKER_ENABLED": "off",
		"SRS_STREAM_CASTER_ENABLED":   "off",
	}

	// Every listener is set explicitly, on with its port or off, so no default
	// port is ever left in use.
	for l := RTMP; l <= RTMPS; l++ {
		info := listeners[l]
		if !on[l] {
			if info.enable != "" {
				env[info.enable] = "off"
			}
			continue
		}

		port, ok := ports[l]
		if !ok || port <= 0 {
			return nil, fmt.Errorf("listener %v is on but has no port", l)
		}
		if info.needs != noListener && !on[info.needs] {
			return nil, fmt.Errorf("listener %v needs %v", l, info.needs)
		}
		if info.enable != "" {
			env[info.enable] = "on"
		}
		env[info.listen] = strconv.Itoa(port)
		if l == RTMP && cfg.IPv6 {
			env[info.listen] += fmt.Sprintf(" [::]:%d", port)
		}

		if info.tls != "" {
			if tls.Key == "" || tls.Cert == "" {
				return nil, fmt.Errorf("listener %v needs the TLS key and certificate", l)
			}
			env[info.tls+"KEY"] = tls.Key
			env[info.tls+"CERT"] = tls.Cert
		}
	}

	// SRS opens the WebRTC TCP listener only when its protocol is not udp; all
	// keeps the UDP one too.
	if on[RTCTCP] {
		env["SRS_RTC_SERVER_PROTOCOL"] = "all"
	}

	for _, l := range []Listener{GB, UDPTS} {
		if !on[l] {
			continue
		}
		env["SRS_STREAM_CASTER_ENABLED"] = "on"
		env["SRS_STREAM_CASTER_CASTER"] = map[Listener]string{GB: "gb28181", UDPTS: "mpegts_over_udp"}[l]
		env["SRS_STREAM_CASTER_OUTPUT"] = fmt.Sprintf("rtmp://127.0.0.1:%d/live/[stream]", ports[RTMP])
	}

	// The API's authentication, whose credentials the adapter sends.
	for k, v := range authEnv(cfg.APIAuth) {
		env[k] = v
	}

	// The variant's own settings come last, and may not change the adapter's.
	for k, v := range cfg.Env {
		if !strings.HasPrefix(k, "SRS_") {
			return nil, fmt.Errorf("env %s is not an SRS_ setting", k)
		}
		if _, ok := env[k]; ok || isListenerEnv(k) || isAuthEnv(k) {
			return nil, fmt.Errorf("env %s is set by the adapter", k)
		}
		env[k] = v
	}

	out := make([]string, 0, len(env))
	for k, v := range env {
		out = append(out, k+"="+v)
	}
	sort.Strings(out)
	return out, nil
}

// isListenerEnv reports whether k turns a listener on or sets its port.
func isListenerEnv(k string) bool {
	if k == "SRS_LISTEN" || k == "SRS_STREAM_CASTER_CASTER" || k == "SRS_STREAM_CASTER_OUTPUT" {
		return true
	}
	for _, info := range listeners {
		if k == info.enable || k == info.listen {
			return true
		}
	}
	return false
}
