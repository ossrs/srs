// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"os"
	"os/exec"
	"regexp"
	"runtime"
	"syscall"
	"unsafe"

	"srs-integ/internal/check"
)

// windowsVersion is the major.minor.build of the ver command, such as
// 10.0.19045 of "Microsoft Windows [Version 10.0.19045.6456]".
var windowsVersion = regexp.MustCompile(`\[Version (\d+)\.(\d+)\.(\d+)`)

// hostUname is one field of the host's uname, such as sysname, with the
// values SRS gives on Windows: the system Windows, the DNS host name, the
// major version as the release, major.minor.build as the version, and the
// machine as Linux names it.
func hostUname(c *check.C, field string) string {
	c.Helper()
	switch field {
	case "sysname":
		return "Windows"
	case "nodename":
		name, err := os.Hostname()
		c.Must(err, "host name")
		return name
	case "machine":
		if runtime.GOARCH == "amd64" {
			return "x86_64"
		}
		return runtime.GOARCH
	}
	out, err := exec.Command("cmd", "/c", "ver").Output()
	c.Must(err, "ver")
	m := windowsVersion.FindStringSubmatch(string(out))
	if m == nil {
		c.Fatalf("no version in ver's %q", out)
	}
	if field == "release" {
		return m[1]
	}
	return m[1] + "." + m[2] + "." + m[3]
}

// processMemoryCounters is PROCESS_MEMORY_COUNTERS of psapi.h.
type processMemoryCounters struct {
	cb                         uint32
	pageFaultCount             uint32
	peakWorkingSetSize         uintptr
	workingSetSize             uintptr
	quotaPeakPagedPoolUsage    uintptr
	quotaPagedPoolUsage        uintptr
	quotaPeakNonPagedPoolUsage uintptr
	quotaNonPagedPoolUsage     uintptr
	pagefileUsage              uintptr
	peakPagefileUsage          uintptr
}

var getProcessMemoryInfo = syscall.NewLazyDLL("kernel32.dll").NewProc("K32GetProcessMemoryInfo")

// processRSS is the resident memory of a process in kB: its working set,
// which Task Manager shows as its memory.
func processRSS(c *check.C, pid int) int {
	c.Helper()
	const processQueryLimitedInformation, processVMRead = 0x1000, 0x0010
	h, err := syscall.OpenProcess(processQueryLimitedInformation|processVMRead, false, uint32(pid))
	c.Must(err, "open the process")
	defer syscall.CloseHandle(h)
	var m processMemoryCounters
	m.cb = uint32(unsafe.Sizeof(m))
	if r, _, err := getProcessMemoryInfo.Call(uintptr(h), uintptr(unsafe.Pointer(&m)), uintptr(m.cb)); r == 0 {
		c.Fatalf("process memory of %d: %v", pid, err)
	}
	return int(m.workingSetSize / 1024)
}
