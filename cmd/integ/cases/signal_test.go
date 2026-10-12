// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"bytes"
	"errors"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"sync"
	"sync/atomic"
	"syscall"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/rtmp"
	"srs-integ/internal/server"
)

const (
	// quitCycle is how often SRS checks its quit and reload signals: in its
	// main cycle, once a second.
	quitCycle = time.Second
	// graceWait is the grace_start_wait and grace_final_wait of the graceful
	// quit cases: short, so they end soon, but long enough to tell from no
	// wait at all.
	graceWait = 300 * time.Millisecond
	// quitMargin is what a quit may take past the waits SRS is set to, for
	// its cleanup and the case's polls.
	quitMargin = 500 * time.Millisecond
	// signalLog is the log file of TestTimer_Signal_LogFile, in its server's
	// dir.
	signalLog = "signal.log"
)

// quitConfig is the config of a quit case's server of its own: RTMP and the
// API, playing at the lowest latency, so a player gets media at once, and the
// grace waits, with more settings in env.
func quitConfig(start, final time.Duration, env map[string]string) server.Config {
	cfg := server.Config{
		Listeners: []server.Listener{server.RTMP, server.HTTPAPI},
		BelowPeak: true,
		Env: map[string]string{
			"SRS_GRACE_START_WAIT":      strconv.Itoa(int(start.Milliseconds())),
			"SRS_GRACE_FINAL_WAIT":      strconv.Itoa(int(final.Milliseconds())),
			"SRS_VHOST_MIN_LATENCY":     "on",
			"SRS_VHOST_PLAY_MW_LATENCY": "0",
		},
	}
	for k, v := range env {
		cfg.Env[k] = v
	}
	return cfg
}

// sendSignal sends a signal to a server and returns when.
func sendSignal(c *check.C, srs *server.Server, sig syscall.Signal) time.Time {
	c.Helper()
	c.Must(srs.Signal(sig), "send "+sig.String())
	return time.Now()
}

// dropIdleAPI closes the idle API connections of this process, such as the
// adapter's ready check: a graceful quit waits for every connection, those
// too.
func dropIdleAPI() {
	httpClient.CloseIdleConnections()
	http.DefaultClient.CloseIdleConnections()
}

// refused reports whether a TCP connect to addr is refused.
func refused(addr string) bool {
	conn, err := net.DialTimeout("tcp", addr, 200*time.Millisecond)
	if err == nil {
		conn.Close()
		return false
	}
	return errors.Is(err, syscall.ECONNREFUSED)
}

// untilRefused waits until a connect to addr is refused, for at most d, and
// returns when.
func untilRefused(c *check.C, addr string, d time.Duration) time.Time {
	c.Helper()
	if !untilEvery(d, 10*time.Millisecond, func() bool { return refused(addr) }) {
		c.Fatalf("%s still accepts connections %v after the signal", addr, d)
	}
	return time.Now()
}

// exitWithin waits up to d for a server to exit, and returns its exit code
// and when it exited.
func exitWithin(c *check.C, srs *server.Server, d time.Duration) (int, time.Time) {
	c.Helper()
	code, err := srs.Wait(d)
	c.Must(err, "wait for the exit")
	return code, time.Now()
}

// stillRunning reports whether a server is still running after d.
func stillRunning(srs *server.Server, d time.Duration) bool {
	_, err := srs.Wait(d)
	return err != nil
}

// readInBackground reads a player until its connection ends, and returns the
// time, in Unix nanoseconds, it last read a tag.
func readInBackground(player *rtmp.Client) *atomic.Int64 {
	var last atomic.Int64
	player.SetDeadline(time.Time{})
	go func() {
		for {
			if _, err := player.ReadTag(); err != nil {
				return
			}
			last.Store(time.Now().UnixNano())
		}
	}()
	return &last
}

// TestTimer_Signal_Quit sends the quit signals, each case to a server of its
// own, which it ends. SRS acts on them in its main cycle, once a second, so
// these are timer cases: SIGTERM quits at once, SIGQUIT quits gracefully,
// after grace_start_wait, its clients and grace_final_wait.
func TestTimer_Signal_Quit(t *testing.T) {
	t.Parallel()

	// SIGTERM quits in the next cycle, without the grace waits, 3s each
	// here, and with a publisher and a player connected: the player's
	// connection ends, and the port is closed.
	check.Case(t, "TermFastQuit", check.Timer, func(c *check.C) {
		srs := server.Group(c, quitConfig(3*time.Second, 3*time.Second, nil))
		url := rtmpURL(srs, "live/"+streamName(c))
		startPublisher(c, url, media.Path(c, media.H264HighAAC), publishLead)
		player, _ := playRTMPClient(c, url, nil)

		sent := sendSignal(c, srs, syscall.SIGTERM)
		code, exited := exitWithin(c, srs, 3*time.Second)
		c.CheckEqual(code, 0, "exit code after SIGTERM")
		c.Check(exited.Sub(sent) <= quitCycle+quitMargin, "exited %v after SIGTERM, want within the cycle, without the 3s grace waits", exited.Sub(sent))
		c.CheckNoError(untilEOF(player, quitMargin), "the player's connection after the exit")
		c.Check(refused(srs.Addr(server.RTMP)), "the RTMP port accepts after the exit")
	})

	// SIGQUIT quits gracefully: the listeners stay open for
	// grace_start_wait, then close, while the connected publisher and player
	// go on; SRS waits for them, then for grace_final_wait, and exits with 0.
	check.Case(t, "QuitGraceful", check.Timer, func(c *check.C) {
		srs := server.Group(c, quitConfig(graceWait, graceWait, nil))
		url := rtmpURL(srs, "live/"+streamName(c))
		pub := startPublisher(c, url, media.Path(c, media.H264HighAAC), publishLead)
		player, _ := playRTMPClient(c, url, nil)
		last := readInBackground(player)
		dropIdleAPI()

		sent := sendSignal(c, srs, syscall.SIGQUIT)
		res, err := envClient.Get(srs.APIURL("/api/v1/versions"))
		if c.CheckNoError(err, "the API right after SIGQUIT") {
			res.Body.Close()
			c.CheckEqual(res.StatusCode, http.StatusOK, "the API's status right after SIGQUIT")
		}
		closed := untilRefused(c, srs.Addr(server.RTMP), quitCycle+graceWait+quitMargin)
		c.Check(closed.Sub(sent) >= graceWait, "the RTMP port closed %v after SIGQUIT, before grace_start_wait", closed.Sub(sent))
		c.Check(refused(srs.Addr(server.HTTPAPI)), "the API port accepts once the RTMP port is closed")
		c.Check(until(quitMargin, func() bool { return last.Load() > closed.UnixNano() }), "the player got no media after the ports closed")
		c.Check(stillRunning(srs, 2*graceWait), "exited with a publisher and a player connected")

		pub.Close()
		player.Close()
		left := time.Now()
		code, exited := exitWithin(c, srs, 2*quitCycle+graceWait)
		c.CheckEqual(code, 0, "exit code after SIGQUIT")
		took := exited.Sub(left)
		c.Check(took >= graceWait && took <= quitCycle+graceWait+quitMargin,
			"exited %v after the clients left, want after grace_final_wait %v, within the cycle", took, graceWait)
	})

	// SIGTERM during a graceful quit quits without waiting for the
	// publisher, which would hold the graceful quit for as long as it
	// publishes.
	check.Case(t, "TermDuringGracefulQuit", check.Timer, func(c *check.C) {
		srs := server.Group(c, quitConfig(graceWait, graceWait, nil))
		startPublisher(c, rtmpURL(srs, "live/"+streamName(c)), media.Path(c, media.H264HighAAC), publishLead)
		dropIdleAPI()

		sendSignal(c, srs, syscall.SIGQUIT)
		untilRefused(c, srs.Addr(server.RTMP), quitCycle+graceWait+quitMargin)
		c.Check(stillRunning(srs, 2*graceWait), "exited with a publisher connected, before SIGTERM")

		sent := sendSignal(c, srs, syscall.SIGTERM)
		code, exited := exitWithin(c, srs, 2*quitCycle+graceWait)
		c.CheckEqual(code, 0, "exit code after SIGTERM")
		c.Check(exited.Sub(sent) <= quitCycle+graceWait+quitMargin,
			"exited %v after SIGTERM, want within the cycle and grace_final_wait", exited.Sub(sent))
	})

	// With force_grace_quit, SIGTERM quits gracefully, as SIGQUIT does: the
	// listeners close after grace_start_wait, and SRS waits for the
	// publisher, then for grace_final_wait.
	check.Case(t, "ForceGraceQuit", check.Timer, func(c *check.C) {
		srs := server.Group(c, quitConfig(graceWait, graceWait, map[string]string{"SRS_FORCE_GRACE_QUIT": "on"}))
		pub := startPublisher(c, rtmpURL(srs, "live/"+streamName(c)), media.Path(c, media.H264HighAAC), publishLead)
		dropIdleAPI()

		sent := sendSignal(c, srs, syscall.SIGTERM)
		closed := untilRefused(c, srs.Addr(server.RTMP), quitCycle+graceWait+quitMargin)
		c.Check(closed.Sub(sent) >= graceWait, "the RTMP port closed %v after SIGTERM, before grace_start_wait", closed.Sub(sent))
		c.Check(stillRunning(srs, 2*graceWait), "exited with a publisher connected: a fast quit")

		pub.Close()
		left := time.Now()
		code, exited := exitWithin(c, srs, 2*quitCycle+graceWait)
		c.CheckEqual(code, 0, "exit code after SIGTERM")
		took := exited.Sub(left)
		c.Check(took >= graceWait && took <= quitCycle+graceWait+quitMargin,
			"exited %v after the publisher left, want after grace_final_wait %v, within the cycle", took, graceWait)
	})
}

// publishOnce publishes a stream without media and closes it, which SRS
// logs.
func publishOnce(c *check.C, srs *server.Server) {
	c.Helper()
	publishOnly(c, rtmpURL(srs, "live/"+streamName(c)), rtmp.FMLE).Close()
}

// TestTimer_Signal_LogFile sends SIGUSR1 to a server of its own that logs to
// a file, to reopen it, as a log rotation does. SRS reopens it at once, so
// the cases are Normal; they run one at a time, since each watches the file.
// The log's lines are read only for where they go and their pid, never for
// their text.
func TestTimer_Signal_LogFile(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.Config{
		Listeners: []server.Listener{server.RTMP, server.HTTPAPI},
		BelowPeak: true,
		Process:   map[string]string{"SRS_LOG_TANK": "file"},
		Env:       map[string]string{"SRS_LOG_FILE": "./" + signalLog},
	})
	logFile := filepath.Join(srs.WorkDir(), signalLog)
	var mu sync.Mutex
	lock := func(c *check.C) {
		c.Untimed(func() { mu.Lock() })
		c.Cleanup(mu.Unlock)
	}

	// A log renamed away gets the lines until SIGUSR1; then SRS creates the
	// log again, and its lines go there, not to the renamed one.
	check.Case(t, "Usr1ReopensRenamedLog", check.Normal, func(c *check.C) {
		lock(c)
		rotated := filepath.Join(srs.WorkDir(), streamName(c)+".log")
		c.Must(os.Rename(logFile, rotated), "rename the log")
		size := fileSize(c, rotated)
		publishOnce(c, srs)
		c.Check(until(quitMargin, func() bool { return fileSize(c, rotated) > size }), "the renamed log didn't grow at a publish before SIGUSR1")
		c.Check(!exists(logFile), "the log was there again before SIGUSR1")

		sendSignal(c, srs, syscall.SIGUSR1)
		if !c.Check(until(quitMargin, func() bool { return exists(logFile) }), "no log %v after SIGUSR1", quitMargin) {
			return
		}
		rotatedSize, size := fileSize(c, rotated), fileSize(c, logFile)
		publishOnce(c, srs)
		c.Check(until(quitMargin, func() bool { return fileSize(c, logFile) > size }), "the new log didn't grow at a publish")
		c.CheckEqual(fileSize(c, rotated), rotatedSize, "the renamed log's size after SIGUSR1")
		for _, l := range logLines(c, logFile, 0) {
			c.CheckEqual(l.pid, srs.PID(), "the pid of a line of the new log: "+l.text)
		}
	})

	// A log still in its place is reopened to append: what it had stays,
	// and the lines after SIGUSR1 follow it.
	check.Case(t, "Usr1AppendsToLog", check.Normal, func(c *check.C) {
		lock(c)
		before, err := os.ReadFile(logFile)
		c.Must(err, "read the log")
		sendSignal(c, srs, syscall.SIGUSR1)
		publishOnce(c, srs)
		c.Check(until(quitMargin, func() bool { return fileSize(c, logFile) > int64(len(before)) }), "the log didn't grow at a publish after SIGUSR1")
		after, err := os.ReadFile(logFile)
		c.Must(err, "read the log")
		c.Check(bytes.HasPrefix(after, before), "the log after SIGUSR1 doesn't start with what it had: truncated or replaced")
	})
}

// TestTimer_Signal_Console sends SIGUSR1 to the shared timer server
// SegmentsKeep, which logs to the console: there is no file to reopen, and
// the server goes on as before.
func TestTimer_Signal_Console(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "SegmentsKeep", sharedSegmentsKeep())

	// After SIGUSR1 a stream publishes and plays, the console gets its log,
	// and no log file is made, not even SRS's default ./objs/srs.log.
	check.Case(t, "Usr1KeepsConsole", check.Normal, func(c *check.C) {
		console := filepath.Join(srs.Dir, "console.log")
		sendSignal(c, srs, syscall.SIGUSR1)
		size := fileSize(c, console)
		url := rtmpURL(srs, "live/"+streamName(c))
		startPublisher(c, url, media.Path(c, media.H264HighAAC), publishLead)
		playChecked(c, url)
		c.Check(fileSize(c, console) > size, "the console log didn't grow at a publish after SIGUSR1")
		c.Check(!exists(filepath.Join(srs.WorkDir(), "objs", "srs.log")), "a log file after SIGUSR1")
	})
}

// TestTimer_Reload_Sighup reloads by SIGHUP, on the shared timer server
// SegmentsKeep, as TestTimer_Reload_RawApi does by the RAW API, one reload at
// a time with those. The server is env-only, so each reload fails to parse a
// config file and changes nothing; the RAW API reports it. Each reload waits
// for SRS's next cycle, about 1s after the one before, so one case does it.
func TestTimer_Reload_Sighup(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "SegmentsKeep", sharedSegmentsKeep())

	// SIGHUP reloads in the next cycle, while a stream plays: a reload with a
	// new id, which failed to parse a config file, and the stream plays on,
	// with the same publisher and media after the reload.
	check.Case(t, "HupReloads", check.Timer, func(c *check.C) {
		lockReload(c)
		stream := streamName(c)
		_, pubID, streamID := publishRTMP(c, srs, stream, media.H264HighAAC)
		player, _ := playRTMPOf(c, srs, stream, nil)
		last := readInBackground(player)

		before := reloadFetch(c, srs)
		sent := sendSignal(c, srs, syscall.SIGHUP)
		st := waitReload(c, srs, before.Rid)
		took := time.Since(sent)
		checkReloadFailed(c, st)
		c.Check(took <= quitCycle+quitMargin, "the reload ran %v after SIGHUP, want within the cycle", took)

		done := time.Now().UnixNano()
		c.Check(until(quitMargin, func() bool { return last.Load() > done }), "the player got no media in %v after the reload", quitMargin)
		pub := object(c, apiStreamOf(c, srs, streamID), "publish")
		c.CheckEqual(pub["active"], true, "stream publish active")
		c.CheckEqual(pub["cid"], pubID, "stream publish cid, the publisher")
	})
}
