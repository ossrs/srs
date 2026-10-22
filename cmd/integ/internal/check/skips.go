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
	{GOOS: "windows", Pattern: `^TestTimer_(Signal_Quit/(QuitGraceful|TermDuringGracefulQuit)$|Signal_LogFile/|Signal_Console/|Reload_Sighup/)`,
		Reason: "Windows has no console event for SIGQUIT, SIGUSR1 or SIGHUP; Ctrl+Break is SIGTERM, and Ctrl+C cannot be sent to one process"},
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
