// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package server

import (
	"encoding/base64"
	"fmt"
	"net/http"
	"strings"
)

// APIAuth is the authentication of a server's HTTP API. The adapter sends its
// credentials on every request it makes, so it reads an API that needs them.
type APIAuth struct {
	// Type is basic or bearer; empty leaves it unset, which SRS refuses.
	Type string
	// Username and Password are the credentials of basic authentication.
	Username, Password string
	// Token is the token of bearer authentication.
	Token string
	// RTC makes bearer authentication protect the WebRTC signaling under
	// /rtc/ too, rtc_bearer_enabled.
	RTC bool
}

// authEnvPrefix starts every env of the API authentication, which only
// Config.APIAuth sets.
const authEnvPrefix = "SRS_HTTP_API_AUTH_"

// authEnv is the env that turns the API authentication of a on: each value
// that is set, so SRS sees the ones that are not as missing.
func authEnv(a *APIAuth) map[string]string {
	if a == nil {
		return nil
	}
	env := map[string]string{authEnvPrefix + "ENABLED": "on"}
	for k, v := range map[string]string{
		"TYPE": a.Type, "USERNAME": a.Username, "PASSWORD": a.Password, "TOKEN": a.Token,
	} {
		if v != "" {
			env[authEnvPrefix+k] = v
		}
	}
	if a.RTC {
		env[authEnvPrefix+"RTC_BEARER_ENABLED"] = "on"
	}
	return env
}

// Authorization is the Authorization header that a's credentials make, or
// empty when a is nil or of an unknown type.
func (a *APIAuth) Authorization() string {
	if a == nil {
		return ""
	}
	switch a.Type {
	case "basic":
		return "Basic " + base64.StdEncoding.EncodeToString([]byte(a.Username+":"+a.Password))
	case "bearer":
		return "Bearer " + a.Token
	}
	return ""
}

// Authorize adds the server's API credentials to a request, if it has any.
func (s *Server) Authorize(req *http.Request) {
	if v := s.auth.Authorization(); v != "" {
		req.Header.Set("Authorization", v)
	}
}

// ExitError is how a server that exited before it was ready failed to start.
type ExitError struct {
	Code int
}

func (e *ExitError) Error() string {
	return fmt.Sprintf("exited with code %d before it was ready", e.Code)
}

// listenerPorts allocates a port for each listener of cfg. With OneHTTPPort,
// the HTTP server shares the HTTP API's port, and the HTTPS server the HTTPS
// API's.
func listenerPorts(cfg Config, alloc func() (int, error)) (map[Listener]int, error) {
	shares := map[Listener]Listener{}
	if cfg.OneHTTPPort {
		shares = map[Listener]Listener{HTTPServer: HTTPAPI, HTTPSServer: HTTPSAPI}
	}
	ports := map[Listener]int{}
	if cfg.APIPort > 0 {
		ports[HTTPAPI] = cfg.APIPort
	}
	for _, l := range cfg.Listeners {
		if _, ok := shares[l]; ok {
			continue
		}
		if _, ok := ports[l]; ok {
			continue
		}
		port, err := alloc()
		if err != nil {
			return nil, err
		}
		ports[l] = port
	}
	for _, l := range cfg.Listeners {
		if api, ok := shares[l]; ok && ports[api] != 0 {
			ports[l] = ports[api]
		}
	}
	return ports, nil
}

// checkOneHTTPPort checks a OneHTTPPort config: the HTTP server is on, the
// HTTPS server and API are on together, and each server has its API's port.
func checkOneHTTPPort(on map[Listener]bool, ports map[Listener]int) error {
	if !on[HTTPServer] {
		return fmt.Errorf("OneHTTPPort needs the HTTP server")
	}
	if on[HTTPSAPI] != on[HTTPSServer] {
		return fmt.Errorf("OneHTTPPort needs the HTTPS API and the HTTPS server together")
	}
	for server, api := range map[Listener]Listener{HTTPServer: HTTPAPI, HTTPSServer: HTTPSAPI} {
		if on[server] && ports[server] != ports[api] {
			return fmt.Errorf("OneHTTPPort: %v has port %d, not the port %d of %v", server, ports[server], ports[api], api)
		}
	}
	return nil
}

// isAuthEnv reports whether k sets the API authentication.
func isAuthEnv(k string) bool {
	return strings.HasPrefix(k, authEnvPrefix)
}
