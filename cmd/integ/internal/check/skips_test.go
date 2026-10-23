// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package check

import (
	"regexp"
	"testing"
)

// TestPlatformSkip checks a case on the skip list of an OS does not run
// there, is skipped with its reason and counted as skipped, and runs on every
// other OS.
func TestPlatformSkip(t *testing.T) {
	skips := []PlatformSkip{
		{GOOS: "windows", Pattern: `^TestApi_Common/Proc`, Reason: "no /proc"},
		{GOOS: "darwin", Pattern: `^TestRtmp_Common/`, Reason: "another OS"},
	}

	r := newRunner(&fakeClock{})
	r.goos, r.skips = "windows", skips
	tb := &fakeTB{name: "TestApi_Common/ProcStats"}
	ran := false
	res := r.run(tb, Fast, func(c *C) { ran = true })
	if ran {
		t.Error("a case on the windows skip list ran on windows")
	}
	if res.Outcome != Skipped || res.Reason != "not on windows: no /proc" || !contains(tb.skips, "not on windows: no /proc") {
		t.Errorf("result %+v, skips %q; want skipped, not on windows: no /proc", res, tb.skips)
	}
	if len(tb.errors) != 0 {
		t.Errorf("errors %q, want none", tb.errors)
	}

	for _, name := range []string{"TestApi_Common/Versions", "TestRtmp_Common/Play"} {
		tb := &fakeTB{name: name}
		ran := false
		if res := r.run(tb, Fast, func(c *C) { ran = true }); !ran || res.Outcome != Passed {
			t.Errorf("%s on windows: ran %v, outcome %v; want it run and passed", name, ran, res.Outcome)
		}
	}

	r.goos = "linux"
	ran = false
	if res := r.run(&fakeTB{name: "TestApi_Common/ProcStats"}, Fast, func(c *C) { ran = true }); !ran || res.Outcome != Passed {
		t.Errorf("on linux: ran %v, outcome %v; want it run and passed", ran, res.Outcome)
	}
}

// TestPlatformSkipList checks every entry of the list names an OS, a pattern
// that compiles and a reason.
func TestPlatformSkipList(t *testing.T) {
	for _, s := range platformSkips {
		if s.GOOS == "" || s.Reason == "" {
			t.Errorf("%+v: want an OS and a reason", s)
		}
		if _, err := regexp.Compile(s.Pattern); err != nil || s.Pattern == "" {
			t.Errorf("%+v: pattern: %v", s, err)
		}
	}
}

// TestPlatformSkipSignalCases checks which signal cases run on Windows: the
// quits by SIGTERM, which the adapter sends as Ctrl+Break, and the log reopen
// by the RAW API run; SIGQUIT, SIGUSR1 and SIGHUP have no console event, so
// their cases are skipped.
func TestPlatformSkipSignalCases(t *testing.T) {
	for _, name := range []string{
		"TestTimer_Signal_Quit/TermFastQuit",
		"TestTimer_Signal_Quit/ForceGraceQuit",
		"TestTimer_Signal_LogFile/RawApiReopensRenamedLog",
		"TestTimer_Signal_LogFile/RawApiAppendsToLog",
		"TestTimer_Reload_RawApi/ReloadWithoutFile",
	} {
		if why := platformSkip(platformSkips, "windows", name); why != "" {
			t.Errorf("%s on windows: skipped, %q; want it run", name, why)
		}
	}
	for _, name := range []string{
		"TestTimer_Signal_Quit/QuitGraceful",
		"TestTimer_Signal_Quit/TermDuringGracefulQuit",
		"TestTimer_Signal_LogFile/Usr1ReopensRenamedLog",
		"TestTimer_Signal_LogFile/Usr1AppendsToLog",
		"TestTimer_Signal_Console/Usr1KeepsConsole",
		"TestTimer_Reload_Sighup/HupReloads",
	} {
		if why := platformSkip(platformSkips, "windows", name); why == "" {
			t.Errorf("%s on windows: runs, want it skipped", name)
		}
		if why := platformSkip(platformSkips, "linux", name); why != "" {
			t.Errorf("%s on linux: skipped, %q; want it run", name, why)
		}
	}
}

// TestPlatformRunsRtspAndGbCases checks the cases of RTSP and GB28181 run on
// Windows, whose build has both listeners, as on Linux, and the cases of the
// other protocols around them too.
func TestPlatformRunsRtspAndGbCases(t *testing.T) {
	for _, name := range []string{
		"TestRtsp_VhostOff/Describe",
		"TestRtsp_RemuxOff/RtmpToRtspOff",
		"TestApi_Common/RtspPlayerFields",
		"TestHooks_Down/RtspPlay",
		"TestConvert_RtcToRtmp/WhipToRtsp_NotConverted",
		"TestConvert_Common/GbToRtsp_H264Aac",
		"TestHooks_Down/RtmpPlay",
		"TestHooks_Down/RtmpsPublish",
		"TestApi_Common/Features",
		"TestSecurity_Common/RtmpThenSrtRejected",
		"TestConvert_RtcToRtmp/WhipToRtmp_NothingBeforeReports",
		"TestCaster_UdpTsRepush/Plays",
		"TestApi_Common/Gbps",
		"TestGb_Common/Publish",
		"TestTimer_Gb_MediaTimeout/NoConnectionEnds",
		"TestApi_Common/DeleteGbPublisher",
		"TestHooks_Common/GbPublishRejected",
		"TestHooks_Down/GbPublish",
		"TestSecurity_Common/RtmpThenGbRejected",
		"TestConvert_Common/GbToRtmp_LatencyH264Only",
	} {
		for _, goos := range []string{"windows", "linux"} {
			if why := platformSkip(platformSkips, goos, name); why != "" {
				t.Errorf("%s on %s: skipped, %q; want it run", name, goos, why)
			}
		}
	}
}

// TestPlatformRunsRtcSimulatorCases checks the WebRTC cases that call SRS's
// NACK simulator run on Windows, whose build has the simulator, as on Linux.
func TestPlatformRunsRtcSimulatorCases(t *testing.T) {
	for _, name := range []string{
		"TestRtc_Common/NackApiInvalidDrop",
		"TestRtc_Common/NackApiNoSession",
		"TestRtc_Common/NackPlayerPlain",
		"TestRtc_Common/NackPlayerRtxNotPreferred",
		"TestRtc_Common/NackPublisherPlain",
		"TestRtc_NackTwccOff/NackOffPlayer",
		"TestRtc_NackTwccOff/NackOffPublisher",
		"TestRtc_PreferRtx/PlayerRtx",
		"TestRtc_PreferRtx/PlayerWithoutRtxPlain",
		"TestRtc_PreferRtx/PublisherRtx",
	} {
		for _, goos := range []string{"windows", "linux"} {
			if why := platformSkip(platformSkips, goos, name); why != "" {
				t.Errorf("%s on %s: skipped, %q; want it run", name, goos, why)
			}
		}
	}
}
