// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

//go:build !windows

package server

import (
	"errors"
	"os"
	"os/exec"
	"syscall"
)

func pidAlive(pid int) bool {
	err := syscall.Kill(pid, 0)
	return err == nil || errors.Is(err, syscall.EPERM)
}

// newProcessGroup leaves the server in this process's group: a signal goes
// to one process anyway.
func newProcessGroup(cmd *exec.Cmd) {}

func signalProcess(p *os.Process, sig syscall.Signal) error {
	return p.Signal(sig)
}
