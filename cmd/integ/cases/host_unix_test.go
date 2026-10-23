// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

//go:build !windows

package cases

import (
	"os/exec"
	"strconv"
	"strings"

	"srs-integ/internal/check"
)

// unameFlags are the uname flags of the srs_node_uname_info labels.
var unameFlags = map[string]string{"sysname": "-s", "nodename": "-n", "release": "-r", "version": "-v", "machine": "-m"}

// hostUname is one field of the host's uname, such as sysname, as uname
// prints it.
func hostUname(c *check.C, field string) string {
	c.Helper()
	out, err := exec.Command("uname", unameFlags[field]).Output()
	c.Must(err, "uname "+unameFlags[field])
	return strings.TrimSpace(string(out))
}

// processRSS is the resident memory of a process in kB, as ps reports it.
func processRSS(c *check.C, pid int) int {
	c.Helper()
	out, err := exec.Command("ps", "-o", "rss=", "-p", strconv.Itoa(pid)).Output()
	c.Must(err, "ps")
	rss, err := strconv.Atoi(strings.TrimSpace(string(out)))
	c.Must(err, "parse ps rss "+string(out))
	return rss
}
