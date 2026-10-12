// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"encoding/json"
	"io"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strconv"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/server"
)

// systemEndpoints are the API paths that describe the server itself, each
// answered with the server's ids beside its data.
var systemEndpoints = []string{
	"/", "/api/", "/api/v1/",
	"/api/v1/versions", "/api/v1/summaries", "/api/v1/rusages",
	"/api/v1/self_proc_stats", "/api/v1/system_proc_stats", "/api/v1/meminfos",
	"/api/v1/authors", "/api/v1/features",
}

// The fields of each system endpoint's data.
var (
	rusageFields = []string{
		"ok", "sample_time", "ru_utime", "ru_stime", "ru_maxrss", "ru_ixrss", "ru_idrss", "ru_isrss",
		"ru_minflt", "ru_majflt", "ru_nswap", "ru_inblock", "ru_oublock", "ru_msgsnd", "ru_msgrcv",
		"ru_nsignals", "ru_nvcsw", "ru_nivcsw",
	}
	selfProcFields = []string{
		"ok", "sample_time", "percent", "pid", "comm", "state", "ppid", "pgrp", "session", "tty_nr",
		"tpgid", "flags", "minflt", "cminflt", "majflt", "cmajflt", "utime", "stime", "cutime",
		"cstime", "priority", "nice", "num_threads", "itrealvalue", "starttime", "vsize", "rss",
		"rsslim", "startcode", "endcode", "startstack", "kstkesp", "kstkeip", "signal", "blocked",
		"sigignore", "sigcatch", "wchan", "nswap", "cnswap", "exit_signal", "processor",
		"rt_priority", "policy", "delayacct_blkio_ticks", "guest_time", "cguest_time",
	}
	systemProcFields = []string{
		"ok", "sample_time", "percent", "user", "nice", "sys", "idle", "iowait", "irq", "softirq",
		"steal", "guest",
	}
	memInfoFields = []string{
		"ok", "sample_time", "percent_ram", "percent_swap", "MemActive", "RealInUse", "NotInUse",
		"MemTotal", "MemFree", "Buffers", "Cached", "SwapTotal", "SwapFree",
	}
	summarySelfFields = []string{
		"version", "pid", "ppid", "argv", "cwd", "mem_kbyte", "mem_percent", "cpu_percent", "srs_uptime",
	}
	summarySystemFields = []string{
		"cpu_percent", "disk_read_KBps", "disk_write_KBps", "disk_busy_percent", "mem_ram_kbyte",
		"mem_ram_percent", "mem_swap_kbyte", "mem_swap_percent", "cpus", "cpus_online", "uptime",
		"ilde_time", "load_1m", "load_5m", "load_15m", "net_sample_time", "net_recv_bytes",
		"net_send_bytes", "net_recvi_bytes", "net_sendi_bytes", "srs_sample_time", "srs_recv_bytes",
		"srs_send_bytes", "conn_sys", "conn_sys_et", "conn_sys_tw", "conn_sys_udp", "conn_srs",
	}
	// The v1 index's URLs, each an endpoint under /api/v1/.
	v1IndexURLs = []string{
		"versions", "summaries", "rusages", "self_proc_stats", "system_proc_stats", "meminfos",
		"authors", "features", "vhosts", "streams", "clients", "forwards", "raw",
		"clusters",
	}
	// The features /api/v1/features reports, each a boolean.
	featureNames = []string{
		"ssl", "hls", "callback", "api", "httpd", "dvr", "transcode", "ingest", "stat",
		"caster", "complex_send", "tcp_nodelay", "so_sendbuf", "mr", "srt", "rtsp", "gb28181", "rtc",
		"simulator", "sanitizer",
	}
)

// noRedirect is an API client that returns a redirect as it is.
var noRedirect = &http.Client{
	Transport:     httpClient.Transport,
	CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse },
}

// apiReply is an API answer: the status, the headers and the body.
type apiReply struct {
	status int
	header http.Header
	body   []byte
}

// apiDo sends a request to the API and reads its whole answer.
func apiDo(c *check.C, client *http.Client, req *http.Request) apiReply {
	c.Helper()
	res, err := client.Do(req)
	c.Must(err, req.Method+" "+req.URL.String())
	defer res.Body.Close()
	body, err := io.ReadAll(res.Body)
	c.Must(err, "read "+req.URL.String())
	return apiReply{status: res.StatusCode, header: res.Header, body: body}
}

// apiFetch gets an API path.
func apiFetch(c *check.C, srs *server.Server, path string) apiReply {
	c.Helper()
	req, err := http.NewRequest(http.MethodGet, srs.APIURL(path), nil)
	c.Must(err, "request")
	return apiDo(c, httpClient, req)
}

// apiObject gets an API path that answers 200 with a JSON object, and returns
// the object.
func apiObject(c *check.C, srs *server.Server, path string) map[string]any {
	c.Helper()
	r := apiFetch(c, srs, path)
	c.CheckEqual(r.status, http.StatusOK, path+" status")
	c.CheckEqual(r.header.Get("Content-Type"), "application/json", path+" content type")
	c.CheckEqual(r.header.Get("Content-Length"), strconv.Itoa(len(r.body)), path+" content length")
	var v map[string]any
	c.Must(json.Unmarshal(r.body, &v), "decode "+path+": "+string(r.body))
	c.CheckEqual(v["code"], 0.0, path+" code")
	return v
}

// object returns the object at a field of v, or fails the case.
func object(c *check.C, v map[string]any, field string) map[string]any {
	c.Helper()
	o, ok := v[field].(map[string]any)
	if !ok {
		c.Fatalf("%s is %v, want an object", field, v[field])
	}
	return o
}

// checkFields checks that o has every field and no other.
func checkFields(c *check.C, o map[string]any, fields []string, what string) {
	c.Helper()
	want := map[string]bool{}
	for _, f := range fields {
		want[f] = true
		_, ok := o[f]
		c.Check(ok, "%s has no %s", what, f)
	}
	var extra []string
	for k := range o {
		if !want[k] {
			extra = append(extra, k)
		}
	}
	sort.Strings(extra)
	c.Check(len(extra) == 0, "%s has fields %v it shouldn't", what, extra)
}

// num returns a number field of o, or fails the case.
func num(c *check.C, o map[string]any, field string) float64 {
	c.Helper()
	n, ok := o[field].(float64)
	if !ok {
		c.Fatalf("%s is %v, want a number", field, o[field])
	}
	return n
}

// nearNow checks that a time in ms since the epoch is within d of now.
func nearNow(c *check.C, ms float64, d time.Duration, what string) {
	c.Helper()
	at := time.UnixMilli(int64(ms))
	c.Check(at.After(time.Now().Add(-d)) && at.Before(time.Now().Add(d)), "%s %v, want within %v of now", what, at, d)
}

// linuxOnly skips a case that reads /proc values, which SRS has only on Linux.
func linuxOnly(c *check.C) {
	c.Helper()
	if runtime.GOOS != "linux" {
		c.Skipf("SRS reads these values from /proc, which only Linux has (%s)", runtime.GOOS)
	}
}

// apiSystem adds the cases of the system endpoints: the indexes, the server's
// own stats, JSONP, the method override, unknown paths and the test helpers.
func apiSystem(t *testing.T) {
	check.Case(t, "Root", check.Fast, func(c *check.C) {
		v := apiObject(c, server.Common(c), "/")
		urls := object(c, v, "urls")
		c.Check(urls["api"] != nil, "urls %v, want api", urls)
		v1 := object(c, object(c, urls, "rtc"), "v1")
		for _, k := range []string{"play", "publish", "nack"} {
			c.Check(v1[k] != nil, "urls.rtc.v1 %v, want %s", v1, k)
		}
	})

	check.Case(t, "ApiIndex", check.Fast, func(c *check.C) {
		urls := object(c, apiObject(c, server.Common(c), "/api/"), "urls")
		c.Check(urls["v1"] != nil, "urls %v, want v1", urls)
	})

	check.Case(t, "V1Index", check.Fast, func(c *check.C) {
		v := apiObject(c, server.Common(c), "/api/v1/")
		urls := object(c, v, "urls")
		for _, k := range v1IndexURLs {
			c.Check(urls[k] != nil, "urls has no %s: %v", k, urls)
		}
		tests := object(c, v, "tests")
		for _, k := range []string{"requests", "errors", "redirects"} {
			c.Check(tests[k] != nil, "tests has no %s: %v", k, tests)
		}
	})

	check.Case(t, "V1IndexUrlsServed", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		urls := object(c, apiObject(c, srs, "/api/v1/"), "urls")
		var notServed []string
		for k := range urls {
			r := apiFetch(c, srs, "/api/v1/"+k)
			var v map[string]any
			json.Unmarshal(r.body, &v)
			if u, ok := v["urls"].(map[string]any); ok && u["versions"] != nil {
				notServed = append(notServed, k)
			}
		}
		sort.Strings(notServed)
		c.Check(len(notServed) == 0, "%v answer the v1 index, not an endpoint of their own", notServed)
	})

	check.Case(t, "VersionsConsistent", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		r := apiFetch(c, srs, "/api/v1/versions")
		var v struct {
			Data struct {
				Major, Minor, Revision int
				Version                string
			}
		}
		c.Must(json.Unmarshal(r.body, &v), "decode versions")
		d := v.Data
		c.CheckEqual(d.Version, strconv.Itoa(d.Major)+"."+strconv.Itoa(d.Minor)+"."+strconv.Itoa(d.Revision), "version")
		c.Check(strings.HasPrefix(r.header.Get("Server"), "SRS/"+d.Version), "Server header %q, want SRS/%s", r.header.Get("Server"), d.Version)
	})

	check.Case(t, "IdsMatch", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		var server0, service0 string
		for i, p := range systemEndpoints {
			v := apiObject(c, srs, p)
			s, _ := v["server"].(string)
			svc, _ := v["service"].(string)
			c.Check(s != "" && svc != "", "%s server %q service %q, want both", p, s, svc)
			c.CheckEqual(v["pid"], strconv.Itoa(srs.PID()), p+" pid")
			if i == 0 {
				server0, service0 = s, svc
				continue
			}
			c.CheckEqual(s, server0, p+" server")
			c.CheckEqual(svc, service0, p+" service")
		}
	})

	check.Case(t, "Summaries", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		data := object(c, apiObject(c, srs, "/api/v1/summaries"), "data")
		checkFields(c, data, []string{"ok", "now_ms", "self", "system"}, "summaries")
		c.CheckEqual(data["ok"], true, "ok")
		nearNow(c, num(c, data, "now_ms"), 2*time.Second, "now_ms")

		self := object(c, data, "self")
		checkFields(c, self, summarySelfFields, "summaries self")
		var ver struct{ Data struct{ Version string } }
		c.Must(json.Unmarshal(apiFetch(c, srs, "/api/v1/versions").body, &ver), "decode versions")
		c.CheckEqual(self["version"], ver.Data.Version, "self version")
		c.CheckEqual(num(c, self, "pid"), float64(srs.PID()), "self pid")
		argv, _ := self["argv"].(string)
		c.Check(strings.HasSuffix(argv, " -e"), "argv %q, want the binary with -e", argv)
		cwd, _ := self["cwd"].(string)
		c.CheckEqual(realPath(cwd), realPath(srs.Dir), "cwd")
		up := num(c, self, "srs_uptime")
		c.Check(up >= 0 && up < 3600, "srs_uptime %v, want 0 to an hour", up)

		sys := object(c, data, "system")
		checkFields(c, sys, summarySystemFields, "summaries system")
		cpus, online := num(c, sys, "cpus"), num(c, sys, "cpus_online")
		c.Check(cpus >= 1 && online >= 1 && online <= cpus, "cpus %v online %v, want 1 or more, online of them", cpus, online)
	})

	check.Case(t, "SummariesMemKbyte", check.Fast, func(c *check.C) {
		if runtime.GOOS == "darwin" {
			check.KnownBug(c, "on macOS summaries mem_kbyte is ru_maxrss as is, which macOS gives in bytes, not kB")
		}
		srs := server.Common(c)
		self := object(c, object(c, apiObject(c, srs, "/api/v1/summaries"), "data"), "self")
		kb := num(c, self, "mem_kbyte")
		out, err := exec.Command("ps", "-o", "rss=", "-p", strconv.Itoa(srs.PID())).Output()
		c.Must(err, "ps")
		rss, err := strconv.Atoi(strings.TrimSpace(string(out)))
		c.Must(err, "parse ps rss "+string(out))
		// mem_kbyte is the peak, so it may be above the RSS now, but not by much.
		// SRS samples it on a timer, and a new server's RSS may grow several
		// times before the next sample, as with a sanitizer build on Linux.
		c.Check(kb >= float64(rss)/16 && kb <= float64(rss)*16, "mem_kbyte %v, want near the RSS ps reports, %d kB", kb, rss)
	})

	check.Case(t, "Rusages", check.Fast, func(c *check.C) {
		data := object(c, apiObject(c, server.Common(c), "/api/v1/rusages"), "data")
		checkFields(c, data, rusageFields, "rusages")
		c.CheckEqual(data["ok"], true, "ok")
		nearNow(c, num(c, data, "sample_time"), time.Minute, "sample_time")
		c.Check(num(c, data, "ru_maxrss") > 0, "ru_maxrss %v, want more than 0", data["ru_maxrss"])
		c.Check(num(c, data, "ru_minflt") > 0, "ru_minflt %v, want more than 0", data["ru_minflt"])
	})

	check.Case(t, "SelfProcStats", check.Fast, func(c *check.C) {
		data := object(c, apiObject(c, server.Common(c), "/api/v1/self_proc_stats"), "data")
		checkFields(c, data, selfProcFields, "self_proc_stats")
	})

	check.Case(t, "SelfProcStatsLinux", check.Fast, func(c *check.C) {
		linuxOnly(c)
		srs := server.Common(c)
		data := object(c, apiObject(c, srs, "/api/v1/self_proc_stats"), "data")
		c.CheckEqual(data["ok"], true, "ok")
		c.CheckEqual(num(c, data, "pid"), float64(srs.PID()), "pid")
		c.CheckEqual(num(c, data, "ppid"), float64(os.Getpid()), "ppid, this test process")
		if data["comm"] == "(srs)" {
			check.KnownBug(c, "self_proc_stats keeps the parentheses of the comm field of /proc/<pid>/stat, (srs), not srs")
		}
		c.CheckEqual(data["comm"], "srs", "comm")
		state, _ := data["state"].(string)
		c.Check(strings.ContainsAny(state, "RS") && len(state) == 1, "state %q, want R or S", state)
		c.Check(num(c, data, "num_threads") >= 1, "num_threads %v", data["num_threads"])
		c.Check(num(c, data, "rss") > 0 && num(c, data, "vsize") > 0, "rss %v vsize %v, want both", data["rss"], data["vsize"])
	})

	check.Case(t, "SystemProcStats", check.Fast, func(c *check.C) {
		data := object(c, apiObject(c, server.Common(c), "/api/v1/system_proc_stats"), "data")
		checkFields(c, data, systemProcFields, "system_proc_stats")
	})

	check.Case(t, "SystemProcStatsLinux", check.Fast, func(c *check.C) {
		linuxOnly(c)
		data := object(c, apiObject(c, server.Common(c), "/api/v1/system_proc_stats"), "data")
		c.CheckEqual(data["ok"], true, "ok")
		total := num(c, data, "user") + num(c, data, "sys") + num(c, data, "idle")
		c.Check(total > 0, "user, sys and idle add up to %v, want more than 0", total)
	})

	check.Case(t, "MemInfos", check.Fast, func(c *check.C) {
		data := object(c, apiObject(c, server.Common(c), "/api/v1/meminfos"), "data")
		checkFields(c, data, memInfoFields, "meminfos")
	})

	check.Case(t, "MemInfosLinux", check.Fast, func(c *check.C) {
		linuxOnly(c)
		data := object(c, apiObject(c, server.Common(c), "/api/v1/meminfos"), "data")
		c.CheckEqual(data["ok"], true, "ok")
		total, free := num(c, data, "MemTotal"), num(c, data, "MemFree")
		c.Check(total > 0 && free > 0 && free <= total, "MemTotal %v MemFree %v, want free of the total", total, free)
	})

	check.Case(t, "Authors", check.Fast, func(c *check.C) {
		data := object(c, apiObject(c, server.Common(c), "/api/v1/authors"), "data")
		checkFields(c, data, []string{"license", "contributors"}, "authors")
		c.CheckEqual(data["license"], "MIT", "license")
		s, _ := data["contributors"].(string)
		c.Check(strings.HasPrefix(s, "https://"), "contributors %q, want a link", s)
	})

	check.Case(t, "Features", check.Fast, func(c *check.C) {
		data := object(c, apiObject(c, server.Common(c), "/api/v1/features"), "data")
		checkFields(c, data, []string{"options", "options2", "build", "build2", "features"}, "features")
		for _, k := range []string{"options", "options2", "build", "build2"} {
			s, _ := data[k].(string)
			c.Check(s != "", "%s %v, want a string", k, data[k])
		}
		features := object(c, data, "features")
		checkFields(c, features, featureNames, "features")
		options2, _ := data["options2"].(string)
		// HDS is removed: no feature and no build option.
		for _, k := range []string{"options", "options2"} {
			s, _ := data[k].(string)
			c.Check(!strings.Contains(s, "hds"), "%s %q has hds, which is removed", k, s)
		}
		for _, k := range featureNames {
			b, ok := features[k].(bool)
			c.Check(ok, "feature %s is %v, want a boolean", k, features[k])
			// A build option and its feature agree.
			for _, opt := range []string{"srt", "rtsp", "gb28181", "rtc", "simulator", "sanitizer"} {
				if k == opt {
					c.CheckEqual(b, strings.Contains(options2, "--"+k+"=on"), "feature "+k+" against options2")
				}
			}
		}
	})

	check.Case(t, "Jsonp", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		for _, p := range []string{"/api/v1/versions", "/api/v1/authors"} {
			plain := apiFetch(c, srs, p)
			r := apiFetch(c, srs, p+"?callback=cb")
			c.CheckEqual(r.status, http.StatusOK, p+" status")
			c.CheckEqual(r.header.Get("Content-Type"), "text/javascript", p+" content type")
			c.CheckEqual(r.header.Get("Content-Length"), strconv.Itoa(len(r.body)), p+" content length")
			c.CheckEqual(string(r.body), "cb("+string(plain.body)+")", p+" body")
		}
	})

	check.Case(t, "JsonpCallbackChars", check.Fast, func(c *check.C) {
		r := apiFetch(c, server.Common(c), "/api/v1/authors?callback=a.b-c_D9")
		c.CheckEqual(r.status, http.StatusOK, "status")
		c.Check(strings.HasPrefix(string(r.body), "a.b-c_D9({") && strings.HasSuffix(string(r.body), "})"), "body %q, want the callback with letters, digits, dot, dash and underscore", r.body)
	})

	check.Case(t, "JsonpEmptyCallback", check.Fast, func(c *check.C) {
		r := apiFetch(c, server.Common(c), "/api/v1/authors?callback=")
		c.CheckEqual(r.header.Get("Content-Type"), "application/json", "content type")
		c.Check(json.Valid(r.body), "body %q, want plain JSON", r.body)
	})

	check.Case(t, "JsonpInvalidCallback", check.Fast, func(c *check.C) {
		check.KnownBug(c, "a JSONP callback with a character SRS refuses gets no answer: SRS closes the connection without a response")
		srs := server.Common(c)
		res, err := httpClient.Get(srs.APIURL("/api/v1/authors?callback=alert(1)"))
		if err == nil {
			body, _ := io.ReadAll(res.Body)
			res.Body.Close()
			c.Check(!strings.Contains(string(body), "alert(1)"), "body %q echoes the callback", body)
			c.CheckEqual(res.StatusCode, http.StatusBadRequest, "status")
		}
		// SRS is still up.
		apiObject(c, srs, "/api/v1/authors")
		c.CheckNoError(err, "an answer to the refused callback")
	})

	check.Case(t, "JsonpErrorCode", check.Fast, func(c *check.C) {
		r := apiFetch(c, server.Common(c), "/api/v1/tests/errors?callback=cb")
		c.CheckEqual(r.header.Get("Content-Type"), "text/javascript", "content type")
		c.CheckEqual(string(r.body), `cb({"code":100})`, "body")
	})

	check.Case(t, "MethodOverride", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		for _, m := range []string{"DELETE", "PUT", "POST"} {
			r := apiFetch(c, srs, "/api/v1/tests/requests?callback=cb&method="+m)
			body := strings.TrimSuffix(strings.TrimPrefix(string(r.body), "cb("), ")")
			var v struct{ Data struct{ METHOD string } }
			c.Must(json.Unmarshal([]byte(body), &v), "decode "+string(r.body))
			c.CheckEqual(v.Data.METHOD, m, "method of a JSONP GET with method="+m)
		}
	})

	check.Case(t, "MethodOverrideNeedsCallback", check.Fast, func(c *check.C) {
		r := apiFetch(c, server.Common(c), "/api/v1/tests/requests?method=DELETE")
		var v struct{ Data struct{ METHOD string } }
		c.Must(json.Unmarshal(r.body, &v), "decode")
		c.CheckEqual(v.Data.METHOD, "GET", "method without a callback")
	})

	check.Case(t, "TestsRequests", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		for _, m := range []string{http.MethodGet, http.MethodPost, http.MethodDelete} {
			req, err := http.NewRequest(m, srs.APIURL("/api/v1/tests/requests?k=v"), strings.NewReader("{}"))
			c.Must(err, "request")
			r := apiDo(c, httpClient, req)
			var v map[string]any
			c.Must(json.Unmarshal(r.body, &v), "decode "+string(r.body))
			c.CheckEqual(v["code"], 0.0, m+" code")
			data := object(c, v, "data")
			c.CheckEqual(data["METHOD"], m, "METHOD")
			c.CheckEqual(data["path"], "/api/v1/tests/requests", m+" path")
			u, _ := data["uri"].(string)
			c.Check(strings.HasPrefix(u, "http://") && strings.HasSuffix(u, "/api/v1/tests/requests"), "uri %q", u)
		}
	})

	check.Case(t, "TestsRequestsHeaders", check.Fast, func(c *check.C) {
		check.KnownBug(c, "tests/requests answers the server's signature and version as headers, not the request's headers")
		req, err := http.NewRequest(http.MethodGet, server.Common(c).APIURL("/api/v1/tests/requests"), nil)
		c.Must(err, "request")
		req.Header.Set("X-Integ-Case", "hello")
		r := apiDo(c, httpClient, req)
		var v struct {
			Data struct{ Headers map[string]any }
		}
		c.Must(json.Unmarshal(r.body, &v), "decode")
		c.CheckEqual(v.Data.Headers["X-Integ-Case"], "hello", "the request header in data.headers")
	})

	check.Case(t, "TestsErrors", check.Fast, func(c *check.C) {
		r := apiFetch(c, server.Common(c), "/api/v1/tests/errors")
		c.CheckEqual(r.status, http.StatusOK, "status")
		c.CheckEqual(r.header.Get("Content-Type"), "application/json", "content type")
		c.CheckEqual(string(r.body), `{"code":100}`, "body")
	})

	check.Case(t, "TestsErrorsVhost", check.Fast, func(c *check.C) {
		req, err := http.NewRequest(http.MethodGet, server.Common(c).APIURL("/api/v1/tests/errors"), nil)
		c.Must(err, "request")
		req.Host = "error.srs.com"
		r := apiDo(c, httpClient, req)
		c.CheckEqual(r.status, http.StatusOK, "status")
		c.CheckEqual(string(r.body), `{"code":100}`, "body")
	})

	check.Case(t, "TestsRedirects", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		req, err := http.NewRequest(http.MethodGet, srs.APIURL("/api/v1/tests/redirects"), nil)
		c.Must(err, "request")
		r := apiDo(c, noRedirect, req)
		c.CheckEqual(r.status, http.StatusMovedPermanently, "status")
		c.CheckEqual(r.header.Get("Location"), "/api/v1/tests/errors", "Location")
		followed := apiFetch(c, srs, "/api/v1/tests/redirects")
		c.CheckEqual(string(followed.body), `{"code":100}`, "the body after the redirect")
	})

	check.Case(t, "UnknownPaths", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		// A path is served by the longest registered prefix: an unknown path
		// under /api/v1/ gets the v1 index, under /api/ the API index, and
		// any other the root.
		for p, index := range map[string]string{
			"/api/v1/unknown":       "versions",
			"/api/v1/versions/sub":  "versions",
			"/api/v1/tests/unknown": "versions",
			"/api/v2/":              "v1",
			"/unknown":              "api",
		} {
			urls := object(c, apiObject(c, srs, p), "urls")
			c.Check(urls[index] != nil, "%s urls %v, want the index with %s", p, urls, index)
		}
	})

	check.Case(t, "ListRedirects", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		for _, k := range []string{"vhosts", "streams", "clients", "forwards"} {
			req, err := http.NewRequest(http.MethodGet, srs.APIURL("/api/v1/"+k), nil)
			c.Must(err, "request")
			r := apiDo(c, noRedirect, req)
			c.CheckEqual(r.status, http.StatusFound, k+" status")
			c.CheckEqual(r.header.Get("Location"), "/api/v1/"+k+"/", k+" Location")
		}
	})
}

// realPath is a path with its symlinks resolved, such as /tmp to
// /private/tmp on macOS, or the path as it is if it doesn't resolve.
func realPath(p string) string {
	if r, err := filepath.EvalSymlinks(p); err == nil {
		return r
	}
	return p
}
