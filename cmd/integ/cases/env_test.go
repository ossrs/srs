// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"bufio"
	"context"
	"encoding/json"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

// envTZ is the time zone of the env cases' servers, UTC+5:45, so a local time
// is never a UTC one, not even in its hour; envZone is the same zone in Go.
// The name has three letters, the only form the Windows CRT reads: it takes
// INTEG-5:45 as UTC+1.
const envTZ = "INT-5:45"

var envZone = time.FixedZone("INT", 5*3600+45*60)

const (
	// envServerID is the server id of TestEnv_Global.
	envServerID = "vid-integ-env"
	// envMaxConnections is the max_connections of TestEnv_Global: each of
	// its other cases has at most 2 connections at once, and the 2 of the
	// case before it may not be gone yet.
	envMaxConnections = 5
	// envRunTimeout is how long a start of TestEnv_Cli runs, as a server
	// with no listener, before it is killed: long enough to write the log of
	// its start.
	envRunTimeout = 300 * time.Millisecond
)

// envGlobalConfig is the config of TestEnv_Global, every global setting
// changed from the adapter's: RTMP on the SRS_LISTEN alias, a server id, the
// work dir work in the server's dir, a pid file and a log file relative to
// it, the log to the file, UTC times in a time zone of UTC+5:45, at most
// envMaxConnections connections, and every stream recorded by DVR, at a path
// of the date and time, with the hooks for on_dvr. It plays at the lowest
// latency, like Common.
func envGlobalConfig() server.Config {
	return server.WithDVR(server.Config{
		Listeners:   []server.Listener{server.RTMP, server.HTTPAPI},
		Hooks:       true,
		ListenAlias: true,
		WorkDir:     "work",
		TZ:          envTZ,
		BelowPeak:   true,
		Process:     map[string]string{"SRS_PID": "./env.pid", "SRS_LOG_TANK": "file"},
		Env: map[string]string{
			"SRS_SERVER_ID":             envServerID,
			"SRS_LOG_FILE":              "./env.log",
			"SRS_UTC_TIME":              "on",
			"SRS_MAX_CONNECTIONS":       strconv.Itoa(envMaxConnections),
			"SRS_VHOST_MIN_LATENCY":     "on",
			"SRS_VHOST_PLAY_MW_LATENCY": "0",
		},
	}, server.DVR{Path: "./dvr/[stream]/[2006]-[01]-[02]/[15].[04].[05].[999].flv"})
}

// envClient is the HTTP client of the env cases, without keep-alive, so no
// idle connection to the server counts toward its max_connections.
var envClient = &http.Client{Transport: &http.Transport{DisableKeepAlives: true}, Timeout: time.Second}

// TestEnv_Global checks the global SRS_* settings of a server of its own,
// started only while the process runs fewer SRS than its peak. Its cases run
// one at a time, since max_connections counts every connection.
func TestEnv_Global(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, envGlobalConfig())
	var mu sync.Mutex
	lock := func(c *check.C) {
		c.Untimed(func() { mu.Lock() })
		c.Cleanup(mu.Unlock)
	}

	// RTMP listens on the port of SRS_LISTEN, the alias of SRS_RTMP_LISTEN,
	// which the server doesn't set.
	check.Case(t, "ListenAliasPlays", check.Normal, func(c *check.C) {
		lock(c)
		url := rtmpURL(srs, "live/"+streamName(c))
		startPublisher(c, url, media.Path(c, media.H264HighAAC), 0)
		ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		defer cancel()
		r, err := flv.PlayRTMP(ctx, url, nil, playedUntil)
		c.Must(err, "play on the SRS_LISTEN port")
		checkPlayed(c, r, "h264", "aac")
	})

	// The API names the server by SRS_SERVER_ID.
	check.Case(t, "ServerIdApi", check.Normal, func(c *check.C) {
		lock(c)
		for _, path := range []string{"/api/v1/versions", "/api/v1/summaries"} {
			var ids serverIDs
			envAPI(c, srs, path, &ids)
			c.CheckEqual(ids.Server, envServerID, path+" server")
		}
	})

	// The hooks name the server by SRS_SERVER_ID.
	check.Case(t, "ServerIdHooks", check.Normal, func(c *check.C) {
		lock(c)
		stream := streamName(c)
		publishOnly(c, rtmpURL(srs, "live/"+stream), rtmp.FMLE)
		h := waitHookCountOf(c, srs, mock.Match{Action: "on_publish", Stream: stream}, 1)[0]
		c.CheckEqual(h.Field("server_id"), envServerID, "on_publish server_id")
	})

	// SRS keeps its server id in a file beside the pid file, the pid file's
	// name with .id for .pid.
	check.Case(t, "ServerIdFile", check.Normal, func(c *check.C) {
		lock(c)
		var ids serverIDs
		envAPI(c, srs, "/api/v1/versions", &ids)
		c.CheckEqual(readTrimmed(c, filepath.Join(srs.WorkDir(), "env.id")), envServerID, "the id file")
	})

	// The pid file, relative to the work dir, holds the server's pid, and
	// there is none in the server's own dir.
	check.Case(t, "PidFile", check.Normal, func(c *check.C) {
		lock(c)
		c.CheckEqual(readTrimmed(c, filepath.Join(srs.WorkDir(), "env.pid")), strconv.Itoa(srs.PID()), "the pid file")
		c.Check(!exists(filepath.Join(srs.Dir, "env.pid")), "a pid file in the server's own dir, not its work dir")
	})

	// SRS locks its pid file: a second SRS with the same pid file refuses to
	// start, and leaves the file and the id file beside it as they are.
	check.Case(t, "PidFileLocked", check.Normal, func(c *check.C) {
		lock(c)
		pid := filepath.Join(srs.WorkDir(), "env.pid")
		r := server.Exec(c, []string{"-e"}, map[string]string{"SRS_PID": pid}, cliTimeout)
		checkExit(c, r, false)
		c.CheckEqual(readTrimmed(c, pid), strconv.Itoa(srs.PID()), "the pid file after the second start")
		c.CheckEqual(readTrimmed(c, filepath.Join(srs.WorkDir(), "env.id")), envServerID, "the id file after the second start")
		var ids serverIDs
		envAPI(c, srs, "/api/v1/versions", &ids)
		c.CheckEqual(ids.Server, envServerID, "the server answers after the second start")
	})

	// SRS changes to SRS_WORK_DIR: on_dvr names it as the cwd, and the DVR
	// file, the log and the pid file are in it, none in the server's own dir.
	check.Case(t, "WorkDirFiles", check.Normal, func(c *check.C) {
		lock(c)
		stream := streamName(c)
		publishDvr(c, srs, "live/"+stream, readTags(c, media.H264HighAAC, 0, time.Second))
		h := dvrHooks(c, srs, stream, 1)[0]
		c.Check(sameDir(c, h.Field("cwd"), srs.WorkDir()), "on_dvr cwd %q, want the work dir %v", h.Field("cwd"), srs.WorkDir())
		file := h.Field("file")
		c.Check(strings.HasPrefix(file, "./dvr/"+stream+"/"), "on_dvr file %q, want it relative to the work dir", file)
		c.Check(exists(filepath.Join(srs.WorkDir(), file)), "no DVR file %v in the work dir", file)
		for _, name := range []string{"env.log", "env.pid"} {
			c.Check(exists(filepath.Join(srs.WorkDir(), name)), "no %v in the work dir", name)
		}
		for _, name := range []string{"dvr", "env.log", "env.pid", "env.id", "objs"} {
			c.Check(!exists(filepath.Join(srs.Dir, name)), "%v in the server's own dir, not its work dir", name)
		}
	})

	// With SRS_LOG_TANK=file the log goes to SRS_LOG_FILE, relative to the
	// work dir, not to the console: a client's connect and publish grow the
	// file and leave the console as it was. Each line is the server's.
	check.Case(t, "LogTankFile", check.Normal, func(c *check.C) {
		lock(c)
		logFile := filepath.Join(srs.WorkDir(), "env.log")
		console := filepath.Join(srs.Dir, "console.log")
		fileBefore, consoleBefore := fileSize(c, logFile), fileSize(c, console)
		pub := publishOnly(c, rtmpURL(srs, "live/"+streamName(c)), rtmp.FMLE)
		pub.Close()
		c.Check(until(time.Second, func() bool { return fileSize(c, logFile) > fileBefore }), "the log file didn't grow at a publish")
		c.CheckEqual(fileSize(c, console), consoleBefore, "the console's size after a publish")
		for _, l := range logLines(c, logFile, 0) {
			c.CheckEqual(l.pid, srs.PID(), "the pid of a log line: "+l.text)
		}
	})

	// With SRS_UTC_TIME=on the log's times are UTC, in a time zone of
	// UTC+5:45.
	check.Case(t, "UtcTimeLog", check.Normal, func(c *check.C) {
		lock(c)
		logFile := filepath.Join(srs.WorkDir(), "env.log")
		before := fileSize(c, logFile)
		start := time.Now()
		pub := publishOnly(c, rtmpURL(srs, "live/"+streamName(c)), rtmp.FMLE)
		pub.Close()
		c.Check(until(time.Second, func() bool { return fileSize(c, logFile) > before }), "the log file didn't grow at a publish")
		checkLogTimes(c, logLines(c, logFile, before), time.UTC, start, time.Now())
	})

	// With SRS_UTC_TIME=on the date and time of a DVR path are UTC.
	check.Case(t, "UtcTimeFiles", check.Normal, func(c *check.C) {
		lock(c)
		stream := streamName(c)
		start := time.Now()
		publishDvr(c, srs, "live/"+stream, readTags(c, media.H264HighAAC, 0, time.Second))
		h := dvrHooks(c, srs, stream, 1)[0]
		end := time.Now()
		re := regexp.MustCompile(`^\./dvr/` + regexp.QuoteMeta(stream) + `/(\d{4}-\d{2}-\d{2}/\d{2}\.\d{2}\.\d{2}\.\d{3})\.flv$`)
		m := re.FindStringSubmatch(h.Field("file"))
		if !c.Check(m != nil, "file %q, want ./dvr/%v/<date>/<time>.flv", h.Field("file"), stream) {
			return
		}
		at, err := time.ParseInLocation("2006-01-02/15.04.05.000", m[1], time.UTC)
		c.Must(err, "parse the time of the file")
		c.Check(!at.Before(start.Truncate(time.Millisecond)) && !at.After(end),
			"file time %v as UTC, want between the publish %v and its on_dvr %v", at, start.UTC(), end.UTC())
	})

	// With SRS_MAX_CONNECTIONS SRS closes a connection past the limit at
	// once, an RTMP client or an API request, and serves the ones it holds.
	check.Case(t, "MaxConnectionsRefused", check.Normal, func(c *check.C) {
		lock(c)
		held := holdConnections(c, srs, envMaxConnections)
		_, err := tryDialRTMP(rtmpURL(srs, "live/"+streamName(c)))
		c.Check(err != nil, "an RTMP client past %d connections connected", envMaxConnections)
		_, err = envClient.Get(srs.APIURL("/api/v1/versions"))
		c.Check(err != nil, "an API request past %d connections answered", envMaxConnections)
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		defer cancel()
		_, err = held[0].Unpublish(ctx)
		c.CheckNoError(err, "unpublish on a held connection")
	})

	// A connection that closes frees its place: the next client connects,
	// and the one after it is refused again.
	check.Case(t, "MaxConnectionsFreed", check.Normal, func(c *check.C) {
		lock(c)
		held := holdConnections(c, srs, envMaxConnections)
		held[0].Close()
		url := rtmpURL(srs, "live/"+streamName(c))
		var next *rtmp.Client
		c.Check(until(time.Second, func() bool {
			var err error
			next, err = tryDialRTMP(url)
			return err == nil
		}), "no client connected in 1s after a held one closed")
		if next != nil {
			c.Cleanup(func() { next.Close() })
		}
		_, err := tryDialRTMP(url)
		c.Check(err != nil, "a client past %d connections connected after one was replaced", envMaxConnections)
	})
}

// TestEnv_Cli checks the log settings by starts of the binary, each in a dir
// of its own with every listener off and its log to ./srs.log there, killed
// after envRunTimeout: the level, SRS_LOG_LEVEL_V2 over SRS_LOG_LEVEL, UTC or
// local times, and the defaults of -e, no pid file and the log to the
// console. A start writes the log of its start only once it has read its
// config, but for the first lines, which go to the console in local time.
func TestEnv_Cli(t *testing.T) {
	t.Parallel()

	// The default level, trace, writes the start's trace lines.
	check.Case(t, "LogLevelDefault", check.Normal, func(c *check.C) {
		lines := envStartLog(c, nil)
		c.Check(levelCount(lines, belowWarn) > 0, "no trace line in the log of a start:\n%s", joinLines(lines))
	})

	// SRS_LOG_LEVEL=warn writes the start's warnings and nothing below.
	check.Case(t, "LogLevelWarn", check.Normal, func(c *check.C) {
		lines := envStartLog(c, map[string]string{"SRS_LOG_LEVEL": "warn"})
		c.Check(levelCount(lines, isWarn) > 0, "no warning in the log of a start:\n%s", joinLines(lines))
		c.CheckEqual(levelCount(lines, belowWarn), 0, "lines below warn")
	})

	// SRS_LOG_LEVEL=error writes no warning.
	check.Case(t, "LogLevelError", check.Normal, func(c *check.C) {
		lines := envStartLog(c, map[string]string{"SRS_LOG_LEVEL": "error"})
		c.CheckEqual(levelCount(lines, func(l string) bool { return belowWarn(l) || isWarn(l) }), 0, "lines below error")
	})

	// SRS_LOG_LEVEL_V2 wins over SRS_LOG_LEVEL.
	check.Case(t, "LogLevelV2Overrides", check.Normal, func(c *check.C) {
		lines := envStartLog(c, map[string]string{"SRS_LOG_LEVEL": "trace", "SRS_LOG_LEVEL_V2": "warn"})
		c.Check(levelCount(lines, isWarn) > 0, "no warning in the log of a start:\n%s", joinLines(lines))
		c.CheckEqual(levelCount(lines, belowWarn), 0, "lines below warn")
	})

	// SRS_UTC_TIME=on writes UTC times in a time zone of UTC+5:45.
	check.Case(t, "UtcTimeOn", check.Normal, func(c *check.C) {
		start := time.Now()
		lines := envStartLog(c, map[string]string{"TZ": envTZ, "SRS_UTC_TIME": "on"})
		checkLogTimes(c, lines, time.UTC, start, time.Now())
	})

	// SRS_UTC_TIME is off by default: the times are of the time zone.
	check.Case(t, "UtcTimeOff", check.Normal, func(c *check.C) {
		start := time.Now()
		lines := envStartLog(c, map[string]string{"TZ": envTZ})
		checkLogTimes(c, lines, envZone, start, time.Now())
	})

	// With -e and no SRS_LOG_TANK the log goes to the console, and no log
	// file is written.
	check.Case(t, "LogTankConsoleDefault", check.Normal, func(c *check.C) {
		r := server.Exec(c, []string{"-e"}, map[string]string{"SRS_LOG_TANK": ""}, envRunTimeout)
		c.Check(!r.Exited, "exited with code %d, want it to run as a server", r.Code)
		var warns int
		for _, line := range strings.Split(r.Stdout, "\n") {
			if m := logHeader.FindStringSubmatch(stripColor(line)); m != nil && isWarn(m[2]) {
				warns++
			}
		}
		c.Check(warns > 0, "no warning of the start on the console: %q", firstLine(r.Stdout))
		c.Check(!exists(filepath.Join(r.Dir, "objs", "srs.log")), "a log file ./objs/srs.log")
	})

	// With -e and no SRS_PID there is no pid file, nor an id file.
	check.Case(t, "NoPidFile", check.Normal, func(c *check.C) {
		r := server.Exec(c, []string{"-e"}, map[string]string{"SRS_PID": ""}, envRunTimeout)
		c.Check(!r.Exited, "exited with code %d, want it to run as a server", r.Code)
		var files []string
		filepath.Walk(r.Dir, func(path string, info os.FileInfo, err error) error {
			if err == nil && (strings.HasSuffix(path, ".pid") || strings.HasSuffix(path, ".id")) {
				files = append(files, path)
			}
			return nil
		})
		c.CheckEqual(len(files), 0, "pid and id files: "+strings.Join(files, ", "))
	})

	// A work dir that is not there stops the start with an error.
	check.Case(t, "WorkDirMissing", check.Normal, func(c *check.C) {
		r := server.Exec(c, []string{"-e"}, map[string]string{"SRS_WORK_DIR": "./missing"}, cliTimeout)
		checkExit(c, r, false)
	})
}

// envAPI decodes the answer of an API path of a server into v, on a
// connection of its own.
func envAPI(c *check.C, srs *server.Server, path string, v any) {
	c.Helper()
	res, err := envClient.Get(srs.APIURL(path))
	c.Must(err, "GET "+path)
	defer res.Body.Close()
	c.Must(json.NewDecoder(res.Body).Decode(v), "decode "+path)
}

// tryDialRTMP connects an RTMP client, or returns why it couldn't in 1s.
func tryDialRTMP(url string) (*rtmp.Client, error) {
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	return rtmp.Dial(ctx, url, nil)
}

// holdConnections connects n RTMP clients while the server has no other
// connection. A client that SRS refuses, while the connections of the case
// before are going, is tried again for up to 1s. The clients publish to an
// app of their own, so SRS sends the on_connect of each, and when the case
// ends they close and the case waits for the on_close of each, which SRS
// sends just before it lets their places go, so the next case finds them
// free.
func holdConnections(c *check.C, srs *server.Server, n int) []*rtmp.Client {
	c.Helper()
	// No idle API connection of this process may count toward the limit.
	httpClient.CloseIdleConnections()
	http.DefaultClient.CloseIdleConnections()
	app := "hold-" + streamName(c)
	c.Cleanup(func() {
		closed := mock.Match{Action: "on_close", Func: func(r mock.Request) bool { return r.Field("app") == app }}
		c.Check(until(time.Second, func() bool { return len(srs.Hooks.Requests(closed)) >= n }),
			"%d of %d on_close of the held connections in 1s", len(srs.Hooks.Requests(closed)), n)
	})
	var held []*rtmp.Client
	for i := 0; i < n; i++ {
		url := rtmpURL(srs, app+"/"+streamName(c))
		var cl *rtmp.Client
		var err error
		ok := until(time.Second, func() bool {
			cl, err = tryDialRTMP(url)
			return err == nil
		})
		if !ok {
			c.Fatalf("connection %d of %d refused for 1s: %v", i+1, n, err)
		}
		c.Cleanup(func() { cl.Close() })
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		_, err = cl.Publish(ctx, rtmp.FMLE)
		cancel()
		c.Must(err, "publish on a held connection")
		held = append(held, cl)
	}
	return held
}

// logLine is a line of SRS's log: its time, as written, its level and the pid
// of the process.
type logLine struct {
	time, level, text string
	pid               int
}

// logHeader is the header of a line of SRS's log, such as
// [2026-10-11 14:22:40.098][INFO][22128][o6025efv].
var logHeader = regexp.MustCompile(`^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]\[([A-Za-z]+)\]\[(\d+)\]`)

// ansiColor is a color code of the console's log.
var ansiColor = regexp.MustCompile("\x1b\\[[0-9;]*m")

func stripColor(s string) string {
	return ansiColor.ReplaceAllString(s, "")
}

// logLines reads the lines of a log file from offset. A line without a header
// goes on the line before, as the stack of an error does; the first line
// must have one.
func logLines(c *check.C, file string, offset int64) []logLine {
	c.Helper()
	f, err := os.Open(file)
	if os.IsNotExist(err) {
		return nil
	}
	c.Must(err, "open the log")
	defer f.Close()
	_, err = f.Seek(offset, 0)
	c.Must(err, "seek the log")
	var lines []logLine
	s := bufio.NewScanner(f)
	s.Buffer(make([]byte, 64*1024), 1024*1024)
	for s.Scan() {
		text := s.Text()
		if text == "" {
			continue
		}
		m := logHeader.FindStringSubmatch(text)
		if m == nil && len(lines) > 0 {
			lines[len(lines)-1].text += "\n" + text
			continue
		}
		if !c.Check(m != nil, "a log line without a header: %q", text) {
			continue
		}
		pid, _ := strconv.Atoi(m[3])
		lines = append(lines, logLine{time: m[1], level: m[2], text: text, pid: pid})
	}
	return lines
}

// envStartLog starts the binary with env, its log to ./srs.log of its dir,
// kills it after envRunTimeout, and returns its log.
func envStartLog(c *check.C, env map[string]string) []logLine {
	c.Helper()
	all := map[string]string{"SRS_LOG_TANK": "file", "SRS_LOG_FILE": "./srs.log"}
	for k, v := range env {
		all[k] = v
	}
	r := server.Exec(c, []string{"-e"}, all, envRunTimeout)
	c.Check(!r.Exited, "exited with code %d, want it to run as a server", r.Code)
	return logLines(c, filepath.Join(r.Dir, "srs.log"), 0)
}

// belowWarn reports whether a level is below warn: SRS's trace, info or
// verbose, or with its log4j names info, debug or trace.
func belowWarn(level string) bool {
	switch strings.ToLower(level) {
	case "trace", "info", "verb", "verbose", "debug":
		return true
	}
	return false
}

func isWarn(level string) bool {
	return strings.EqualFold(level, "warn")
}

// levelCount counts the lines whose level matches.
func levelCount(lines []logLine, match func(level string) bool) int {
	n := 0
	for _, l := range lines {
		if match(l.level) {
			n++
		}
	}
	return n
}

func joinLines(lines []logLine) string {
	var b strings.Builder
	for _, l := range lines {
		b.WriteString(l.text + "\n")
	}
	return b.String()
}

// checkLogTimes checks there are log lines, and that each line's time, read
// in loc, is between start and end, to the second.
func checkLogTimes(c *check.C, lines []logLine, loc *time.Location, start, end time.Time) {
	c.Helper()
	if !c.Check(len(lines) > 0, "no log line") {
		return
	}
	for _, l := range lines {
		at, err := time.ParseInLocation("2006-01-02 15:04:05.000", l.time, loc)
		if !c.Check(err == nil, "parse the time of %q: %v", l.text, err) {
			continue
		}
		c.Check(!at.Before(start.Add(-time.Second)) && !at.After(end.Add(time.Second)),
			"log time %v read in %v, want between %v and %v: %q", l.time, loc, start.In(loc).Format(time.TimeOnly), end.In(loc).Format(time.TimeOnly), l.text)
	}
}

// readTrimmed reads a small file, without its spaces at the ends.
func readTrimmed(c *check.C, file string) string {
	c.Helper()
	b, err := os.ReadFile(file)
	c.Must(err, "read "+file)
	return strings.TrimSpace(string(b))
}

// fileSize is the size of a file, 0 when it is not there.
func fileSize(c *check.C, file string) int64 {
	c.Helper()
	st, err := os.Stat(file)
	if os.IsNotExist(err) {
		return 0
	}
	c.Must(err, "stat "+file)
	return st.Size()
}
