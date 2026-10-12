// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"fmt"
	"maps"
	"strings"
)

// hookNames are the hooks SRS POSTs to, each pointed at /hooks/<name>.
var hookNames = []string{"on_connect", "on_close", "on_publish", "on_unpublish", "on_play", "on_stop", "on_dvr", "on_hls"}

// HooksEnv returns the env that turns every hook on and points it at the mock
// at base, each at /hooks/<action>. on_hls_notify is a GET with no body, so its
// action, app, stream and segment go in the query, where the mock routes it.
func HooksEnv(base string) map[string]string {
	env := map[string]string{"SRS_VHOST_HTTP_HOOKS_ENABLED": "on"}
	for _, name := range hookNames {
		env["SRS_VHOST_HTTP_HOOKS_"+strings.ToUpper(name)] = base + "/hooks/" + name
	}
	env["SRS_VHOST_HTTP_HOOKS_ON_HLS_NOTIFY"] = base + "/hooks/on_hls_notify" +
		"?action=on_hls_notify&app=[app]&stream=[stream]&ts_url=[ts_url]"
	return env
}

// withHooks returns cfg with the hooks env of the mock at base added to a copy
// of its env. A config may not set a hook itself.
func withHooks(cfg Config, base string) (Config, error) {
	env := maps.Clone(cfg.Env)
	if env == nil {
		env = map[string]string{}
	}
	for k, v := range HooksEnv(base) {
		if _, ok := env[k]; ok {
			return cfg, fmt.Errorf("env %s is set by the adapter, which points every hook at the mock", k)
		}
		env[k] = v
	}
	cfg.Env = env
	return cfg, nil
}
