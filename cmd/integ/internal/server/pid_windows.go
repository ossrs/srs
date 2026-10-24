// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"errors"
	"fmt"
	"os"
	"os/exec"
	"syscall"
)

const (
	processQueryLimitedInformation = 0x1000
	stillActive                    = 259
)

// pidAlive reads the exit code of the process, since Windows has no signal 0.
func pidAlive(pid int) bool {
	h, err := syscall.OpenProcess(processQueryLimitedInformation, false, uint32(pid))
	if err != nil {
		return errors.Is(err, syscall.ERROR_ACCESS_DENIED)
	}
	defer syscall.CloseHandle(h)
	var code uint32
	if err := syscall.GetExitCodeProcess(h, &code); err != nil {
		return true
	}
	return code == stillActive
}

// newProcessGroup starts the server in a process group of its own, the
// smallest target of a console event, so Ctrl+Break reaches it alone.
func newProcessGroup(cmd *exec.Cmd) {
	cmd.SysProcAttr = &syscall.SysProcAttr{CreationFlags: syscall.CREATE_NEW_PROCESS_GROUP}
}

var generateConsoleCtrlEvent = syscall.NewLazyDLL("kernel32.dll").NewProc("GenerateConsoleCtrlEvent")

// signalProcess sends SIGTERM as Ctrl+Break, which SRS takes as SIGTERM.
// Windows has no signals, and Ctrl+C cannot be sent to one process group, so
// the other signals cannot be sent.
func signalProcess(p *os.Process, sig syscall.Signal) error {
	if sig != syscall.SIGTERM {
		return fmt.Errorf("signal %d: Windows has no console event for it, only SIGTERM as Ctrl+Break", int(sig))
	}
	if r, _, err := generateConsoleCtrlEvent.Call(syscall.CTRL_BREAK_EVENT, uintptr(p.Pid)); r == 0 {
		return fmt.Errorf("send Ctrl+Break to %d: %w", p.Pid, err)
	}
	return nil
}
