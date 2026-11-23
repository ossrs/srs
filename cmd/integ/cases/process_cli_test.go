// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"encoding/json"
	"runtime"
	"strconv"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/server"
)

// cliTimeout is how long a run of the binary may take to exit: printing its
// version or help, testing its config, or refusing to start. A run that still
// goes at the timeout started as a server, and is killed.
const cliTimeout = 400 * time.Millisecond

// cli runs the SRS binary with args and env, with every listener off but
// those env sets.
func cli(c *check.C, env map[string]string, args ...string) *server.CLIRun {
	c.Helper()
	return server.Exec(c, args, env, cliTimeout)
}

// checkExit checks that a run exited by itself, with code 0 when ok, else
// with an error.
func checkExit(c *check.C, r *server.CLIRun, ok bool) {
	c.Helper()
	if !c.Check(r.Exited, "still running after %v, killed: it started as a server", cliTimeout) {
		return
	}
	if ok {
		c.CheckEqual(r.Code, 0, "exit code")
	} else {
		c.Check(r.Code != 0, "exit code 0, want an error")
	}
}

// checkHelp checks that a run printed the help and exited by itself.
func checkHelp(c *check.C, r *server.CLIRun) {
	c.Helper()
	c.Check(r.Exited, "still running after %v, killed: it started as a server", cliTimeout)
	c.Check(strings.Contains(r.Stdout, "Usage:"), "stdout %q, want the help with its Usage", firstLine(r.Stdout))
	for _, opt := range []string{"-h", "-v", "-g", "-e", "-t", "-c"} {
		c.Check(strings.Contains(r.Stdout, opt), "the help lacks the option %s", opt)
	}
}

// commonVersion returns the version of the Common server and the Server
// header of its API.
func commonVersion(c *check.C) (version, signature string) {
	c.Helper()
	r := apiFetch(c, server.Common(c), "/api/v1/versions")
	var v struct {
		Data struct{ Version string }
	}
	c.Must(json.Unmarshal(r.body, &v), "decode versions: "+string(r.body))
	c.Check(v.Data.Version != "", "no version in %s", r.body)
	return v.Data.Version, r.header.Get("Server")
}

// badEnv are settings SRS must refuse, each with the env that makes it.
// A run that tests them with -t exits with an error, and so does a start.
var badEnv = []struct {
	name string
	env  func(c *check.C) map[string]string
}{
	{name: "MaxConnectionsZero", env: envOf("SRS_MAX_CONNECTIONS", "0")},
	{name: "MaxConnectionsNegative", env: envOf("SRS_MAX_CONNECTIONS", "-5")},
	{name: "MaxConnectionsNotNumber", env: envOf("SRS_MAX_CONNECTIONS", "abc")},
	{name: "MaxConnectionsOverFileLimit", env: envOf("SRS_MAX_CONNECTIONS", "100000000")},
	{name: "RtmpListenNotPort", env: envOf("SRS_RTMP_LISTEN", "abc")},
	{name: "RtmpListenPortZero", env: envOf("SRS_RTMP_LISTEN", "0")},
	{name: "RtmpListenBadHost", env: func(c *check.C) map[string]string {
		return map[string]string{"SRS_RTMP_LISTEN": "1.2.3:" + strconv.Itoa(server.ReservePort(c))}
	}},
	{
		name: "RtmpListenPortOutOfRange",
		// A port above 65535, which no socket has: the port of the range plus
		// 65536, so a server that cut it to 16 bits would listen on a port of
		// the suite's range.
		env: func(c *check.C) map[string]string {
			return map[string]string{"SRS_RTMP_LISTEN": strconv.Itoa(65536 + server.ReservePort(c))}
		},
	},
	// The same port after a host, and as the second listen of the API, after a good one.
	{name: "RtmpListenHostPortOutOfRange", env: func(c *check.C) map[string]string {
		return map[string]string{"SRS_RTMP_LISTEN": "127.0.0.1:" + strconv.Itoa(65536+server.ReservePort(c))}
	}},
	{name: "HttpApiListenPortOutOfRange", env: func(c *check.C) map[string]string {
		return map[string]string{
			"SRS_HTTP_API_ENABLED": "on",
			"SRS_HTTP_API_LISTEN":  strconv.Itoa(server.ReservePort(c)) + " " + strconv.Itoa(65536+server.ReservePort(c)),
		}
	}},
	{name: "ListenAliasNotPort", env: envOf("SRS_LISTEN", "abc")},
	{name: "HttpApiListen", env: envOf("SRS_HTTP_API_ENABLED", "on", "SRS_HTTP_API_LISTEN", "abc")},
	{name: "HttpServerListen", env: envOf("SRS_HTTP_SERVER_ENABLED", "on", "SRS_HTTP_SERVER_LISTEN", "abc")},
	{name: "RtcListen", env: envOf("SRS_RTC_SERVER_ENABLED", "on", "SRS_RTC_SERVER_LISTEN", "abc")},
	{name: "SrtListen", env: envOf("SRS_SRT_SERVER_ENABLED", "on", "SRS_SRT_SERVER_LISTEN", "abc")},
	{name: "RtspListen", env: envOf("SRS_RTSP_SERVER_ENABLED", "on", "SRS_RTSP_SERVER_LISTEN", "abc")},
	{name: "ExporterListen", env: envOf("SRS_EXPORTER_ENABLED", "on", "SRS_EXPORTER_LISTEN", "abc")},
	{name: "RtmpsListen", env: envOf("SRS_RTMPS_ENABLED", "on", "SRS_RTMPS_LISTEN", "abc")},
	// A second RTMPS listen that is not a port, after a good one.
	{name: "RtmpsListenSecond", env: func(c *check.C) map[string]string {
		return map[string]string{
			"SRS_RTMPS_ENABLED": "on", "SRS_RTMPS_LISTEN": strconv.Itoa(server.ReservePort(c)) + " abc",
		}
	}},
	{name: "HeartbeatIntervalZero", env: envOf("SRS_HEARTBEAT_INTERVAL", "0")},
	// HDS is removed, so each of its settings, on or off, is refused.
	{name: "HdsEnabled", env: envOf("SRS_VHOST_HDS_ENABLED", "on")},
	{name: "HdsDisabled", env: envOf("SRS_VHOST_HDS_ENABLED", "off")},
	{name: "HdsPath", env: envOf("SRS_VHOST_HDS_HDS_PATH", "./objs/nginx/html")},
	{name: "HdsFragment", env: envOf("SRS_VHOST_HDS_HDS_FRAGMENT", "10")},
	{name: "HdsWindow", env: envOf("SRS_VHOST_HDS_HDS_WINDOW", "60")},
	// The FLV POST caster is removed, so a caster of type flv, on with a port
	// to listen on or off, is refused.
	{name: "FlvCaster", env: func(c *check.C) map[string]string {
		return map[string]string{
			"SRS_STREAM_CASTER_ENABLED": "on", "SRS_STREAM_CASTER_CASTER": "flv",
			"SRS_STREAM_CASTER_OUTPUT": "rtmp://127.0.0.1/[app]/[stream]",
			"SRS_STREAM_CASTER_LISTEN": strconv.Itoa(server.ReservePort(c)),
		}
	}},
	{name: "FlvCasterDisabled", env: envOf("SRS_STREAM_CASTER_ENABLED", "off", "SRS_STREAM_CASTER_CASTER", "flv")},
	// asprocess, quitting when the parent process quits, is removed, so it is
	// refused on or off.
	{name: "Asprocess", env: envOf("SRS_ASPROCESS", "on")},
	{name: "AsprocessOff", env: envOf("SRS_ASPROCESS", "off")},
	// The latest version query is removed, so it is refused on or off, and so
	// is the wait before its first query.
	{name: "QueryLatestVersion", env: envOf("SRS_QUERY_LATEST_VERSION", "on")},
	{name: "QueryLatestVersionOff", env: envOf("SRS_QUERY_LATEST_VERSION", "off")},
	{name: "FirstWaitForQlv", env: envOf("SRS_FIRST_WAIT_FOR_QLV", "300")},
	// atc_auto, turning ATC on when the metadata has bravo_atc, is removed,
	// so it is refused on or off.
	{name: "AtcAuto", env: envOf("SRS_VHOST_PLAY_ATC_AUTO", "on")},
	{name: "AtcAutoOff", env: envOf("SRS_VHOST_PLAY_ATC_AUTO", "off")},
	// Edge token traverse, checking each client by a connect to the origin,
	// is removed, so it is refused on or off.
	{name: "TokenTraverse", env: envOf("SRS_VHOST_CLUSTER_TOKEN_TRAVERSE", "on")},
	{name: "TokenTraverseOff", env: envOf("SRS_VHOST_CLUSTER_TOKEN_TRAVERSE", "off")},
	// A vhost setting by env: forward conflicts with kickoff_for_idle.
	{name: "ForwardWithKickoffForIdle", env: envOf("SRS_VHOST_FORWARD_ENABLED", "on", "SRS_VHOST_PUBLISH_KICKOFF_FOR_IDLE", "1000")},
	// A time setting must be a finite number of seconds that is not negative:
	// not nan, not inf, not below zero, and a number.
	{name: "HlsDisposeNan", env: envOf("SRS_VHOST_HLS_HLS_DISPOSE", "nan")},
	{name: "HlsDisposeInf", env: envOf("SRS_VHOST_HLS_HLS_DISPOSE", "inf")},
	{name: "HlsDisposeNegative", env: envOf("SRS_VHOST_HLS_HLS_DISPOSE", "-1")},
	{name: "HlsDisposeNotNumber", env: envOf("SRS_VHOST_HLS_HLS_DISPOSE", "abc")},
	// An HLS ratio must be a finite number above zero.
	{name: "HlsTdRatioNan", env: envOf("SRS_VHOST_HLS_HLS_TD_RATIO", "nan")},
	{name: "HlsTdRatioZero", env: envOf("SRS_VHOST_HLS_HLS_TD_RATIO", "0")},
	{name: "HlsAofRatioInf", env: envOf("SRS_VHOST_HLS_HLS_AOF_RATIO", "inf")},
	{name: "HlsAofRatioNegative", env: envOf("SRS_VHOST_HLS_HLS_AOF_RATIO", "-1")},
	// The tcmalloc release rate must be a finite number: not nan, not inf,
	// and a number.
	{name: "TcmallocReleaseRateNan", env: envOf("SRS_TCMALLOC_RELEASE_RATE", "nan")},
	{name: "TcmallocReleaseRateInf", env: envOf("SRS_TCMALLOC_RELEASE_RATE", "inf")},
	{name: "TcmallocReleaseRateNotNumber", env: envOf("SRS_TCMALLOC_RELEASE_RATE", "abc")},
}

// envOf returns the env of name and value pairs.
func envOf(kv ...string) func(c *check.C) map[string]string {
	return func(c *check.C) map[string]string {
		env := map[string]string{}
		for i := 0; i+1 < len(kv); i += 2 {
			env[kv[i]] = kv[i+1]
		}
		return env
	}
}

// TestProcess_Cli runs the SRS binary as a command: the version, the
// signature and the help, the config test with -t of env-only settings, and
// the settings SRS refuses, by -t and at a start. Each run has a dir of its
// own and every listener off but those its env sets, and starts only while
// the process runs fewer SRS than its peak, so the runs never raise it.
func TestProcess_Cli(t *testing.T) {
	t.Parallel()

	// The version and the signature.
	for _, flag := range []string{"-v", "-V", "--version"} {
		check.Case(t, "Version"+flagName(flag), check.Normal, func(c *check.C) {
			version, _ := commonVersion(c)
			r := cli(c, nil, flag)
			checkExit(c, r, true)
			c.CheckEqual(r.Stdout, version+"\n", "stdout, the version of /api/v1/versions")
		})
	}
	for _, flag := range []string{"-g", "-G"} {
		check.Case(t, "Signature"+flagName(flag), check.Normal, func(c *check.C) {
			_, signature := commonVersion(c)
			r := cli(c, nil, flag)
			checkExit(c, r, true)
			c.Check(strings.HasPrefix(signature, "SRS/"), "Server header %q, want SRS/<version>", signature)
			c.CheckEqual(r.Stdout, signature+"\n", "stdout, the Server header of the API")
		})
	}
	check.Case(t, "VersionIgnoresEnv", check.Normal, func(c *check.C) {
		r := cli(c, map[string]string{"SRS_MAX_CONNECTIONS": "0"}, "-e", "-v")
		checkExit(c, r, true)
		c.Check(strings.Count(r.Stdout, ".") == 2, "stdout %q, want the version", r.Stdout)
	})

	// The help.
	for _, flag := range []string{"-h", "-?", "--help"} {
		check.Case(t, "Help"+flagName(flag), check.Normal, func(c *check.C) {
			r := cli(c, nil, flag)
			checkExit(c, r, true)
			checkHelp(c, r)
		})
	}
	check.Case(t, "HelpWithoutArgs", check.Normal, func(c *check.C) {
		if runtime.GOOS == "windows" {
			// Windows loads a config without arguments; a missing override must fail without help.
			r := cli(c, map[string]string{"SRS_CONFIG_FILE": "/srs-integ-missing.conf"})
			checkExit(c, r, false)
			c.Check(!strings.Contains(r.Stdout, "Usage:"), "the help printed, want a config error")
			return
		}
		r := cli(c, nil)
		checkExit(c, r, true)
		checkHelp(c, r)
	})
	check.Case(t, "HelpForArgNotOption", check.Normal, func(c *check.C) {
		checkHelp(c, cli(c, nil, "start"))
	})
	check.Case(t, "UnknownOption", check.Normal, func(c *check.C) {
		r := cli(c, nil, "-x")
		checkExit(c, r, false)
		c.Check(!strings.Contains(r.Stdout, "Usage:"), "the help printed, want an error")
	})
	check.Case(t, "ConfigFlagWithoutFile", check.Normal, func(c *check.C) {
		checkExit(c, cli(c, nil, "-c"), false)
	})

	// The config test, -t, of env-only settings. Its output is SRS's log, so
	// only its exit code is checked.
	check.Case(t, "CheckValidEnv", check.Normal, func(c *check.C) {
		port := strconv.Itoa(server.ReservePort(c))
		api := strconv.Itoa(server.ReservePort(c))
		env := map[string]string{
			"SRS_RTMP_LISTEN":       port,
			"SRS_HTTP_API_ENABLED":  "on",
			"SRS_HTTP_API_LISTEN":   api,
			"SRS_VHOST_HLS_ENABLED": "on",
			"SRS_MAX_CONNECTIONS":   "100",
		}
		checkExit(c, cli(c, env, "-e", "-t"), true)
		checkExit(c, cli(c, env, "-t", "-e"), true)
	})
	check.Case(t, "CheckCombinedFlags", check.Normal, func(c *check.C) {
		checkExit(c, cli(c, nil, "-et"), true)
		checkExit(c, cli(c, map[string]string{"SRS_MAX_CONNECTIONS": "0"}, "-et"), false)
	})
	check.Case(t, "CheckNoConfigFile", check.Normal, func(c *check.C) {
		// Without -e, SRS reads conf/srs.conf, which the run's dir doesn't have.
		checkExit(c, cli(c, nil, "-t"), false)
	})
	check.Case(t, "CheckConfigFileEnvMissing", check.Normal, func(c *check.C) {
		checkExit(c, cli(c, map[string]string{"SRS_CONFIG_FILE": "/srs-integ-missing.conf"}, "-t"), false)
	})
	check.Case(t, "CheckEnvOnlyByEnv", check.Normal, func(c *check.C) {
		// SRS_ENV_ONLY set to any value, even off, is -e.
		for _, v := range []string{"on", "off"} {
			checkExit(c, cli(c, map[string]string{"SRS_ENV_ONLY": v}, "-t"), true)
		}
	})
	check.Case(t, "CheckEnvIgnoresConfigFile", check.Normal, func(c *check.C) {
		checkExit(c, cli(c, map[string]string{"SRS_CONFIG_FILE": "/srs-integ-missing.conf"}, "-e", "-t"), true)
		checkExit(c, cli(c, nil, "-e", "-t", "-c", "/srs-integ-missing.conf"), true)
	})

	// A heartbeat interval with a fraction of a second passes the config test
	// and starts: SRS rounds it down to whole seconds, at least 1s.
	heartbeatEnv := func(c *check.C, interval string) map[string]string {
		return map[string]string{
			"SRS_HEARTBEAT_ENABLED":  "on",
			"SRS_HEARTBEAT_INTERVAL": interval,
			"SRS_HEARTBEAT_URL":      "http://" + server.DownAddr(c) + "/api/v1/servers",
		}
	}
	check.Case(t, "CheckHeartbeatIntervalFraction", check.Normal, func(c *check.C) {
		for _, v := range []string{"0.5", "1.5"} {
			checkExit(c, cli(c, heartbeatEnv(c, v), "-e", "-t"), true)
		}
	})
	// Each start runs until it is killed, so each value is a case.
	for _, tt := range []struct{ name, interval string }{{"HalfSecond", "0.5"}, {"Fraction", "1.5"}} {
		check.Case(t, "StartHeartbeatInterval"+tt.name, check.Normal, func(c *check.C) {
			r := cli(c, heartbeatEnv(c, tt.interval), "-e")
			c.Check(!r.Exited, "exited with code %d, want it running as a server: %v", r.Code, r.Stderr)
		})
	}

	// The settings SRS refuses, by the config test and at a start.
	for _, tt := range badEnv {
		check.Case(t, "CheckRejects"+tt.name, check.Normal, func(c *check.C) {
			checkExit(c, cli(c, tt.env(c), "-e", "-t"), false)
		})
		check.Case(t, "StartRejects"+tt.name, check.Normal, func(c *check.C) {
			checkExit(c, cli(c, tt.env(c), "-e"), false)
		})
	}
}

// flagName names a case by its flag: -v is V, -V Upper, --version Long.
func flagName(flag string) string {
	switch {
	case strings.HasPrefix(flag, "--"):
		return "Long"
	case flag == "-?":
		return "Question"
	case strings.ToUpper(flag) == flag:
		return "Upper"
	}
	return ""
}
