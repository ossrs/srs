// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"fmt"
	"maps"
	"slices"
	"strings"
)

// hookNames are the hooks SRS POSTs to, each pointed at /hooks/<name>.
var hookNames = []string{"on_connect", "on_close", "on_publish", "on_unpublish", "on_play", "on_stop", "on_dvr", "on_hls"}

// HooksEnv returns the env that turns every hook on and points it at the mock
// at base, each at /hooks/<action>. on_hls_notify is a GET with no body, so its
// action and every variable SRS fills in, the server ids, app, stream, segment
// and param, go in the query, where the mock routes it.
func HooksEnv(base string) map[string]string {
	env := map[string]string{"SRS_VHOST_HTTP_HOOKS_ENABLED": "on"}
	for _, name := range hookNames {
		env["SRS_VHOST_HTTP_HOOKS_"+strings.ToUpper(name)] = base + "/hooks/" + name
	}
	env["SRS_VHOST_HTTP_HOOKS_ON_HLS_NOTIFY"] = base + "/hooks/on_hls_notify" +
		"?action=on_hls_notify&server_id=[server_id]&service_id=[service_id]&app=[app]&stream=[stream]&ts_url=[ts_url]&param=[param]"
	return env
}

// withHooks returns cfg with the hooks env of the mock at base added to a copy
// of its env, the forward backend env with ForwardBackend, and the heartbeat
// env with Heartbeat. A config may not set a hook, the backend or that
// heartbeat env itself.
func withHooks(cfg Config, base string) (Config, error) {
	env := maps.Clone(cfg.Env)
	if env == nil {
		env = map[string]string{}
	}
	add := HooksEnv(base)
	if cfg.ForwardBackend {
		add["SRS_VHOST_FORWARD_ENABLED"] = "on"
		add["SRS_VHOST_FORWARD_BACKEND"] = base + "/api/v1/forward"
	}
	beat, err := heartbeatEnv(cfg.Heartbeat, base)
	if err != nil {
		return cfg, err
	}
	maps.Copy(add, beat)
	for k := range env {
		if cfg.Heartbeat != nil && isHeartbeatEnv(k) {
			return cfg, fmt.Errorf("env %s is set by the adapter's Heartbeat", k)
		}
	}
	for k, v := range add {
		if _, ok := env[k]; ok {
			return cfg, fmt.Errorf("env %s is set by the adapter, which points every hook, the forward backend and the heartbeat at the mock", k)
		}
		env[k] = v
	}
	cfg.Env = env
	return cfg, nil
}

// HookDown is a base of Config.HookURLs: a port of the range that nothing
// listens on, like a hook server that is down.
const HookDown = "down"

// downHooks reports whether a hook of the config calls HookDown.
func (cfg Config) downHooks() bool {
	for _, bases := range cfg.HookURLs {
		if slices.Contains(bases, HookDown) {
			return true
		}
	}
	return false
}

// withHookURLs is withHooks, with the hooks that cfg.HookURLs names pointed
// at each of their bases in order: a path on the mock at base, or the server
// at down for HookDown.
func withHookURLs(cfg Config, base, down string) (Config, error) {
	cfg, err := withHooks(cfg, base)
	if err != nil {
		return cfg, err
	}
	for name, bases := range cfg.HookURLs {
		if !slices.Contains(hookNames, name) {
			return cfg, fmt.Errorf("HookURLs: no hook %q", name)
		}
		if len(bases) == 0 {
			return cfg, fmt.Errorf("HookURLs: no URL of %s", name)
		}
		var urls []string
		for _, b := range bases {
			switch {
			case b == HookDown:
				urls = append(urls, down+"/hooks/"+name)
			case strings.HasPrefix(b, "/"):
				urls = append(urls, base+b+"/"+name)
			default:
				return cfg, fmt.Errorf("HookURLs: %s base %q, want a path or HookDown", name, b)
			}
		}
		cfg.Env["SRS_VHOST_HTTP_HOOKS_"+strings.ToUpper(name)] = strings.Join(urls, " ")
	}
	return cfg, nil
}
