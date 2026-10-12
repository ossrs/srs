// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"encoding/json"
	"net/http"
	"regexp"
	"strconv"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/server"
)

// The RAW API's codes: the API is off, or a reload is not allowed; and an rpc
// it doesn't serve.
const (
	rawDisabledCode = 1061
	rawUnknownCode  = 1060
)

// The reload states SRS reports: none yet, and a reload that failed to parse
// its config file, which an env-only server never has. 1023 is SRS's code
// for an invalid config.
const (
	reloadStateInit    = 0
	reloadStateParsing = 10
	reloadConfigErr    = 1023
)

// rawUnservedRPCs are the rpc values the RAW API doesn't serve: none, empty,
// the query and update SRS turned off for good, a case it doesn't match and
// an unknown one.
var rawUnservedRPCs = []string{"", "?rpc=", "?rpc=query", "?rpc=update", "?rpc=RAW", "?rpc=none"}

// reloadIDPattern is a reload id, 7 random letters and digits.
var reloadIDPattern = regexp.MustCompile(`^[a-z0-9]{7}$`)

// rawPath is the RAW API's path with a query.
func rawPath(q string) string {
	return "/api/v1/raw" + q
}

// checkRawCode checks that a RAW API request answers 200 with only a code.
func checkRawCode(c *check.C, srs *server.Server, q string, code int) {
	c.Helper()
	r := apiFetch(c, srs, rawPath(q))
	c.CheckEqual(r.status, http.StatusOK, q+" status")
	c.CheckEqual(r.header.Get("Content-Type"), "application/json", q+" content type")
	c.CheckEqual(string(r.body), `{"code":`+strconv.Itoa(code)+`}`, q+" body")
}

// checkRawConfig checks the answer of rpc=raw: the API's own settings and
// the RAW API's, of which query and update are always off.
func checkRawConfig(c *check.C, srs *server.Server, enabled, allowReload bool) {
	c.Helper()
	v := apiObject(c, srs, rawPath("?rpc=raw"))
	checkFields(c, v, []string{"code", "http_api"}, "rpc=raw")
	api := object(c, v, "http_api")
	checkFields(c, api, []string{"enabled", "listen", "crossdomain", "raw_api"}, "http_api")
	c.CheckEqual(api["enabled"], true, "http_api enabled")
	c.CheckEqual(api["listen"], strconv.Itoa(srs.Ports[server.HTTPAPI]), "http_api listen")
	c.CheckEqual(api["crossdomain"], true, "http_api crossdomain")
	raw := object(c, api, "raw_api")
	checkFields(c, raw, []string{"enabled", "allow_reload", "allow_query", "allow_update"}, "raw_api")
	c.CheckEqual(raw["enabled"], enabled, "raw_api enabled")
	c.CheckEqual(raw["allow_reload"], allowReload, "raw_api allow_reload")
	c.CheckEqual(raw["allow_query"], false, "raw_api allow_query")
	c.CheckEqual(raw["allow_update"], false, "raw_api allow_update")
}

// reloadStatus is the answer of rpc=reload-fetch: the last reload's error
// code and message, its state and its id.
type reloadStatus struct {
	Err   int    `json:"err"`
	Msg   string `json:"msg"`
	State int    `json:"state"`
	Rid   string `json:"rid"`
}

// reloadFetch gets the status of the last reload.
func reloadFetch(c *check.C, srs *server.Server) reloadStatus {
	c.Helper()
	v := apiObject(c, srs, rawPath("?rpc=reload-fetch"))
	checkFields(c, v, []string{"code", "data"}, "rpc=reload-fetch")
	data := object(c, v, "data")
	checkFields(c, data, []string{"err", "msg", "state", "rid"}, "reload-fetch data")
	b, err := json.Marshal(data)
	c.Must(err, "encode reload-fetch data")
	var st reloadStatus
	c.Must(json.Unmarshal(b, &st), "decode reload-fetch data")
	c.Check(reloadIDPattern.MatchString(st.Rid), "reload id %q, want 7 letters and digits", st.Rid)
	return st
}

// reload asks for a reload by rpc=reload, which SRS answers at once, then
// waits until a reload with another id than old ran, and returns its status.
// SRS reloads in its main cycle, once a second.
func reload(c *check.C, srs *server.Server, old string) reloadStatus {
	c.Helper()
	checkRawCode(c, srs, "?rpc=reload", 0)
	return waitReload(c, srs, old)
}

// waitReload waits until a reload with another id than old ran, and returns
// its status. SRS reloads in its main cycle, once a second.
func waitReload(c *check.C, srs *server.Server, old string) reloadStatus {
	c.Helper()
	var st reloadStatus
	if !until(2*time.Second, func() bool {
		st = reloadFetch(c, srs)
		return st.Rid != old
	}) {
		c.Fatalf("no reload after 2s: the reload id is still %v", old)
	}
	return st
}

// checkReloadFailed checks the status of a reload of an env-only server: it
// failed to parse a config file, since there is none.
func checkReloadFailed(c *check.C, st reloadStatus) {
	c.Helper()
	c.CheckEqual(st.State, reloadStateParsing, "reload state")
	c.CheckEqual(st.Err, reloadConfigErr, "reload err")
	c.Check(st.Msg != "" && st.Msg != "Success", "reload msg %q, want the parse error", st.Msg)
}

// apiRaw adds the cases of the RAW API off, as on Common: rpc=raw still
// answers, every other rpc is refused.
func apiRaw(t *testing.T) {
	// RawOffQuery reads the RAW API's settings, which rpc=raw answers even
	// with the API off.
	check.Case(t, "RawOffQuery", check.Fast, func(c *check.C) {
		checkRawConfig(c, server.Common(c), false, false)
	})

	// RawOffRefused asks for a reload, its status, and each rpc the API
	// doesn't serve: each is refused as the API being off.
	check.Case(t, "RawOffRefused", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		for _, q := range append([]string{"?rpc=reload", "?rpc=reload-fetch"}, rawUnservedRPCs...) {
			checkRawCode(c, srs, q, rawDisabledCode)
		}
	})

	// RawJsonp reads rpc=raw with a JSONP callback: the same answer wrapped
	// in it.
	check.Case(t, "RawJsonp", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		plain := apiFetch(c, srs, rawPath("?rpc=raw"))
		r := apiFetch(c, srs, rawPath("?rpc=raw&callback=cb"))
		c.CheckEqual(r.status, http.StatusOK, "status")
		c.CheckEqual(r.header.Get("Content-Type"), "text/javascript", "content type")
		c.CheckEqual(string(r.body), "cb("+string(plain.body)+")", "body")
	})
}

// TestApi_RawApi checks the RAW API on with reload off, on the shared
// RemuxOff, whose other groups never use the API. They publish segments at
// once, which can hold an API answer past the Fast limit, so the cases are
// Normal.
func TestApi_RawApi(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "RemuxOff", sharedRemuxOff())

	check.Case(t, "RawQuery", check.Normal, func(c *check.C) {
		checkRawConfig(c, srs, true, false)
	})

	// ReloadNotAllowed asks for a reload, which is refused with the code of
	// the API being off.
	check.Case(t, "ReloadNotAllowed", check.Normal, func(c *check.C) {
		checkRawCode(c, srs, "?rpc=reload", rawDisabledCode)
	})

	// ReloadFetchBeforeReload reads the status of a server that never
	// reloaded: no error, no state, and an id that stays the same.
	check.Case(t, "ReloadFetchBeforeReload", check.Normal, func(c *check.C) {
		st := reloadFetch(c, srs)
		c.CheckEqual(st, reloadStatus{Err: 0, Msg: "Success", State: reloadStateInit, Rid: st.Rid}, "reload status")
		c.CheckEqual(reloadFetch(c, srs).Rid, st.Rid, "the reload id of a second fetch")
	})

	// UnknownRpc asks for each rpc the API doesn't serve.
	check.Case(t, "UnknownRpc", check.Normal, func(c *check.C) {
		for _, q := range rawUnservedRPCs {
			checkRawCode(c, srs, q, rawUnknownCode)
		}
	})
}

// reloadMu makes the reloads of SegmentsKeep, by the RAW API or by SIGHUP,
// run one at a time, since SRS keeps the status of the last reload only.
var reloadMu sync.Mutex

// lockReload holds reloadMu, out of the case time, until the case ends.
func lockReload(c *check.C) {
	c.Untimed(func() {
		reloadMu.Lock()
		c.Cleanup(reloadMu.Unlock)
	})
}

// TestTimer_Reload_RawApi reloads by the RAW API, on the shared timer server
// SegmentsKeep with reload allowed. The server is env-only, so each reload
// fails to parse a config file and changes nothing for the other groups;
// SRS keeps the status of the last reload only, so the cases run one at a
// time, with those of TestTimer_Reload_Sighup.
func TestTimer_Reload_RawApi(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "SegmentsKeep", sharedSegmentsKeep())
	lock := lockReload

	check.Case(t, "RawQuery", check.Normal, func(c *check.C) {
		checkRawConfig(c, srs, true, true)
	})

	// ReloadWithoutFile reloads: SRS answers code 0 at once, and reloads in
	// its next cycle, within about 1s, where it fails to parse the config
	// file an env-only server doesn't have.
	check.Case(t, "ReloadWithoutFile", check.Timer, func(c *check.C) {
		lock(c)
		before := reloadFetch(c, srs)
		start := time.Now()
		st := reload(c, srs, before.Rid)
		took := time.Since(start)
		checkReloadFailed(c, st)
		c.Check(took <= 1500*time.Millisecond, "the reload ran after %v, want within the 1s cycle", took)
		c.CheckEqual(reloadFetch(c, srs), st, "the status of a second fetch")
	})

	// EachReloadNewId reloads twice, one after the other: each reload has an
	// id of its own and the same failure.
	check.Case(t, "EachReloadNewId", check.Timer, func(c *check.C) {
		lock(c)
		first := reload(c, srs, reloadFetch(c, srs).Rid)
		second := reload(c, srs, first.Rid)
		checkReloadFailed(c, first)
		checkReloadFailed(c, second)
		c.Check(first.Rid != second.Rid, "both reloads have the id %v", first.Rid)
	})

	// StreamPlaysAcrossReload reloads while a stream plays: the publisher
	// stays, the player gets media after the reload, and the RAW API's
	// settings are the same.
	check.Case(t, "StreamPlaysAcrossReload", check.Timer, func(c *check.C) {
		lock(c)
		stream := streamName(c)
		_, pubID, streamID := publishRTMP(c, srs, stream, media.H264HighAAC)
		player, _ := playRTMPOf(c, srs, stream, nil)
		var last atomic.Int64
		player.SetDeadline(time.Time{})
		go func() {
			for {
				if _, err := player.ReadTag(); err != nil {
					return
				}
				last.Store(time.Now().UnixNano())
			}
		}()
		raw := apiFetch(c, srs, rawPath("?rpc=raw"))

		checkReloadFailed(c, reload(c, srs, reloadFetch(c, srs).Rid))
		done := time.Now().UnixNano()
		c.Check(until(500*time.Millisecond, func() bool { return last.Load() > done }), "the player got no media in 500ms after the reload")
		pub := object(c, apiStreamOf(c, srs, streamID), "publish")
		c.CheckEqual(pub["active"], true, "stream publish active")
		c.CheckEqual(pub["cid"], pubID, "stream publish cid, the publisher")
		c.CheckEqual(string(apiFetch(c, srs, rawPath("?rpc=raw")).body), string(raw.body), "rpc=raw after the reload")
	})
}
