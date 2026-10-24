// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"fmt"
	"strconv"
	"strings"
	"time"
)

// HeartbeatPath is the path on the mock that the heartbeat POSTs to.
const HeartbeatPath = "/api/v1/heartbeat"

// Heartbeat is the heartbeat a server POSTs to its hooks' mock, at
// HeartbeatPath.
type Heartbeat struct {
	// Interval is the time between heartbeats, whole seconds, as SRS beats
	// it; 0 leaves SRS's default.
	Interval time.Duration
	// DeviceID is the device_id of each heartbeat.
	DeviceID string
	// IP is the ip of each heartbeat, SRS_DEVICE_IP; empty leaves SRS to
	// pick one of the host's addresses.
	IP string
	// Summaries adds the summaries of /api/v1/summaries, and Ports the
	// listen endpoints.
	Summaries, Ports bool
	// Token turns the heartbeat's bearer authentication on, with this token.
	Token string
	// Query is added to the heartbeat URL, such as k=v.
	Query string
	// NoPath leaves the path out of the heartbeat URL, such as
	// http://host:port?k=v, so the heartbeat POSTs to the mock's root, /.
	NoPath bool
}

// isHeartbeatEnv reports whether k is a setting of the heartbeat, which only
// Config.Heartbeat sets when it is on.
func isHeartbeatEnv(k string) bool {
	return strings.HasPrefix(k, "SRS_HEARTBEAT_") || k == "SRS_DEVICE_IP"
}

// heartbeatEnv is the env that turns the heartbeat h on, POSTing to the mock
// at base, or nil when h is nil. SRS beats on whole seconds.
func heartbeatEnv(h *Heartbeat, base string) (map[string]string, error) {
	if h == nil {
		return nil, nil
	}
	url := base + HeartbeatPath
	if h.NoPath {
		url = base
	}
	if h.Query != "" {
		url += "?" + h.Query
	}
	onOff := map[bool]string{true: "on", false: "off"}
	env := map[string]string{
		"SRS_HEARTBEAT_ENABLED":   "on",
		"SRS_HEARTBEAT_URL":       url,
		"SRS_HEARTBEAT_SUMMARIES": onOff[h.Summaries],
		"SRS_HEARTBEAT_PORTS":     onOff[h.Ports],
	}
	if h.Interval != 0 {
		if h.Interval < time.Second || h.Interval%time.Second != 0 {
			return nil, fmt.Errorf("heartbeat interval %v, want whole seconds", h.Interval)
		}
		env["SRS_HEARTBEAT_INTERVAL"] = strconv.Itoa(int(h.Interval / time.Second))
	}
	if h.DeviceID != "" {
		env["SRS_HEARTBEAT_DEVICE_ID"] = h.DeviceID
	}
	if h.IP != "" {
		env["SRS_DEVICE_IP"] = h.IP
	}
	if h.Token != "" {
		env["SRS_HEARTBEAT_AUTH_ENABLED"] = "on"
		env["SRS_HEARTBEAT_AUTH_TYPE"] = "bearer"
		env["SRS_HEARTBEAT_AUTH_TOKEN"] = h.Token
	}
	return env, nil
}
