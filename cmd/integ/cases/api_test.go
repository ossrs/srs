// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"encoding/json"
	"net"
	"net/http"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/server"
)

// TestApi_Smoke starts a server with only the HTTP API on.
func TestApi_Smoke(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, server.Config{Listeners: []server.Listener{server.HTTPAPI}})

	check.Case(t, "Versions", check.Fast, func(c *check.C) {
		checkVersions(c, srs)
	})

	check.Case(t, "BuildFeatures", check.Fast, func(c *check.C) {
		features, err := srs.Features()
		c.Must(err, "features")
		for _, name := range []string{"srt", "rtsp", "gb28181", "rtc", "simulator", "sanitizer"} {
			c.Check(features[name], "feature %s is %v, want true", name, features[name])
		}
	})

	check.Case(t, "OnlyItsListener", check.Fast, func(c *check.C) {
		c.Check(len(srs.Ports) == 1 && srs.Ports[server.HTTPAPI] != 0, "ports %v, want only the HTTP API", srs.Ports)
	})

	// MetricsDisabled: with the exporter off, the API's /metrics answers
	// code 1090 (ExporterDisabled), not the metrics.
	check.Case(t, "MetricsDisabled", check.Fast, func(c *check.C) {
		r := apiFetch(c, srs, "/metrics")
		c.CheckEqual(r.status, http.StatusOK, "status")
		c.CheckEqual(string(r.body), `{"code":1090}`, "body")
	})
}

// TestApi_Common checks the Common server, with every listener on.
func TestApi_Common(t *testing.T) {
	t.Parallel()

	check.Case(t, "Versions", check.Fast, func(c *check.C) {
		checkVersions(c, server.Common(c))
	})

	check.Case(t, "ListenersAccept", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		r, err := server.ProcessRange()
		c.Must(err, "process range")

		seen := map[int]bool{}
		for _, l := range server.CommonListeners {
			port := srs.Ports[l]
			c.Check(port >= r.Lo && port <= r.Hi && !seen[port], "%v has port %d, want a new port in %v", l, port, r)
			seen[port] = true

			if l.UDP() {
				continue
			}
			conn, err := net.DialTimeout("tcp", srs.Addr(l), time.Second)
			if c.CheckNoError(err, l.String()+" at "+srs.Addr(l)+" accepts") {
				conn.Close()
			}
		}
	})

	apiSystem(t)
	apiStreams(t)
	apiPorts(t)
	apiRaw(t)
	apiExporter(t)
	apiHTTPS(t)
}

func checkVersions(c *check.C, srs *server.Server) {
	c.Helper()
	res, err := httpClient.Get(srs.APIURL("/api/v1/versions"))
	c.Must(err, "versions")
	defer res.Body.Close()
	c.CheckEqual(res.StatusCode, http.StatusOK, "status")

	var v struct {
		Code int `json:"code"`
		Data struct {
			Major    *int   `json:"major"`
			Minor    *int   `json:"minor"`
			Revision *int   `json:"revision"`
			Version  string `json:"version"`
		} `json:"data"`
	}
	c.Must(json.NewDecoder(res.Body).Decode(&v), "decode versions")
	c.Check(v.Code == 0 && v.Data.Major != nil && v.Data.Minor != nil && v.Data.Revision != nil && v.Data.Version != "",
		"versions %+v, want code 0 with major, minor, revision and version", v)
}
