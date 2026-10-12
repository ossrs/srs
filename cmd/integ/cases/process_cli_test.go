// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"encoding/json"
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
	// testBug and startBug are the known bugs of the test and of the start.
	testBug, startBug string
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
		testBug: "a listen port above 65535 passes the config test",
	},
	{name: "ListenAliasNotPort", env: envOf("SRS_LISTEN", "abc")},
	{name: "HttpApiListen", env: envOf("SRS_HTTP_API_ENABLED", "on", "SRS_HTTP_API_LISTEN", "abc")},
	{name: "HttpServerListen", env: envOf("SRS_HTTP_SERVER_ENABLED", "on", "SRS_HTTP_SERVER_LISTEN", "abc")},
	{name: "RtcListen", env: envOf("SRS_RTC_SERVER_ENABLED", "on", "SRS_RTC_SERVER_LISTEN", "abc")},
	{name: "SrtListen", env: envOf("SRS_SRT_SERVER_ENABLED", "on", "SRS_SRT_SERVER_LISTEN", "abc")},
	{name: "RtspListen", env: envOf("SRS_RTSP_SERVER_ENABLED", "on", "SRS_RTSP_SERVER_LISTEN", "abc")},
	{
		name:     "ExporterListen",
		env:      envOf("SRS_EXPORTER_ENABLED", "on", "SRS_EXPORTER_LISTEN", "abc"),
		testBug:  "an exporter listen that is not a port passes the config test",
		startBug: "an exporter listen that is not a port starts SRS without the exporter",
	},
	{
		name:     "RtmpsListen",
		env:      envOf("SRS_RTMPS_ENABLED", "on", "SRS_RTMPS_LISTEN", "abc"),
		testBug:  "an RTMPS listen that is not a port passes the config test",
		startBug: "an RTMPS listen that is not a port starts SRS without RTMPS",
	},
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
	{
		name:    "ForwardWithKickoffForIdle",
		env:     envOf("SRS_VHOST_FORWARD_ENABLED", "on", "SRS_VHOST_PUBLISH_KICKOFF_FOR_IDLE", "1000"),
		testBug: "with -e the config test checks no vhost setting",
	},
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

	// The settings SRS refuses, by the config test and at a start.
	for _, tt := range badEnv {
		check.Case(t, "CheckRejects"+tt.name, check.Normal, func(c *check.C) {
			if tt.testBug != "" {
				check.KnownBug(c, tt.testBug)
			}
			checkExit(c, cli(c, tt.env(c), "-e", "-t"), false)
		})
		check.Case(t, "StartRejects"+tt.name, check.Normal, func(c *check.C) {
			if tt.startBug != "" {
				check.KnownBug(c, tt.startBug)
			}
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
