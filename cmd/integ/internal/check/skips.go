// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package check

import "regexp"

// A PlatformSkip is a case that cannot pass on one OS, and why.
type PlatformSkip struct {
	// GOOS is the OS, such as windows.
	GOOS string
	// Pattern is a regexp of the case's full name, such as ^TestApi_Common/Rusage.
	Pattern string
	Reason  string
}

// platformSkips is the skip list: the cases that do not run on an OS. Each
// one is skipped there with its reason, and listed in the summary.
var platformSkips = []PlatformSkip{
	{GOOS: "windows", Pattern: `^TestTimer_Signal_Quit/(QuitGraceful|TermDuringGracefulQuit)$`,
		Reason: "Windows has no console event for SIGQUIT; a graceful quit there is SIGTERM with force_grace_quit, as in ForceGraceQuit"},
	{GOOS: "windows", Pattern: `^TestTimer_(Signal_LogFile/Usr1|Signal_Console/)`,
		Reason: "Windows has no SIGUSR1; the log is reopened by the RAW API rpc=reopen-log, as in the LogFile RawApi cases"},
	{GOOS: "windows", Pattern: `^TestTimer_Reload_Sighup/`,
		Reason: "Windows has no SIGHUP; reload by the RAW API, as in TestTimer_Reload_RawApi"},
}

// platformSkip returns why a case does not run on goos, "" when it runs.
func platformSkip(skips []PlatformSkip, goos, name string) string {
	for _, s := range skips {
		if s.GOOS == goos && regexp.MustCompile(s.Pattern).MatchString(name) {
			return "not on " + goos + ": " + s.Reason
		}
	}
	return ""
}
