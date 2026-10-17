// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
	"os/exec"
	"slices"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/metrics"
	"srs-integ/internal/rtc"
	"srs-integ/internal/server"
)

// metricFamilies are the families of SRS's /metrics, in order, with their
// types.
var metricFamilies = []struct{ name, typ string }{
	{"srs_node_uname_info", "gauge"},
	{"srs_build_info", "gauge"},
	{"srs_cpu_percent", "gauge"},
	{"srs_memory", "gauge"},
	{"srs_send_bytes_total", "counter"},
	{"srs_receive_bytes_total", "counter"},
	{"srs_streams", "gauge"},
	{"srs_clients", "gauge"},
	{"srs_clients_total", "counter"},
	{"srs_clients_errs_total", "counter"},
}

// The labels of the info metrics; srs_build_info has label and tag too when
// the exporter sets them.
var (
	unameLabels = []string{"sysname", "nodename", "release", "version", "machine"}
	buildLabels = []string{"server", "service", "pid", "build_date", "major", "version", "code"}
)

// The exporter's label and tag on TestApi_Stats; the tag has the characters
// the exposition format escapes in a label value.
const (
	statsExporterLabel = "cn-beijing"
	statsExporterTag   = `edge "a\b"`
)

// scrapeText gets /metrics from a URL and checks the answer is text: 200,
// its content type and length.
func scrapeText(c *check.C, client *http.Client, url string) []byte {
	c.Helper()
	req, err := http.NewRequest(http.MethodGet, url, nil)
	c.Must(err, "request")
	r := apiDo(c, client, req)
	c.CheckEqual(r.status, http.StatusOK, url+" status")
	c.CheckEqual(r.header.Get("Content-Type"), "text/plain; charset=utf-8", url+" content type")
	c.CheckEqual(r.header.Get("Content-Length"), strconv.Itoa(len(r.body)), url+" content length")
	return r.body
}

// scrape gets the exporter's /metrics and parses it as a scraper does.
func scrape(c *check.C, srs *server.Server) []metrics.Family {
	c.Helper()
	return parseMetrics(c, scrapeText(c, httpClient, "http://"+srs.Addr(server.Exporter)+"/metrics"))
}

// parseMetrics parses exposition text, or fails the case.
func parseMetrics(c *check.C, text []byte) []metrics.Family {
	c.Helper()
	fams, err := metrics.Parse(bytes.NewReader(text))
	c.Must(err, "parse /metrics")
	return fams
}

// oneSample returns the only sample of a family, or fails the case.
func oneSample(c *check.C, fams []metrics.Family, name string) metrics.Sample {
	c.Helper()
	f := metrics.Find(fams, name)
	if f == nil || len(f.Samples) != 1 {
		c.Fatalf("%s: %+v, want one sample", name, f)
	}
	return f.Samples[0]
}

// metricLine returns the line of a metric's sample in exposition text.
func metricLine(c *check.C, text []byte, name string) string {
	c.Helper()
	for line := range strings.SplitSeq(string(text), "\n") {
		if strings.HasPrefix(line, name+"{") || strings.HasPrefix(line, name+" ") {
			return line
		}
	}
	c.Fatalf("no %s in /metrics", name)
	return ""
}

// uname runs uname with one flag, such as -s.
func uname(c *check.C, flag string) string {
	c.Helper()
	out, err := exec.Command("uname", flag).Output()
	c.Must(err, "uname "+flag)
	return strings.TrimSpace(string(out))
}

// httpsAPIClient is an API client that trusts the test certificate.
var httpsAPIClient = sync.OnceValues(func() (*http.Client, error) {
	cfg, err := server.ClientTLS()
	if err != nil {
		return nil, err
	}
	tr := httpClient.Transport.(*http.Transport).Clone()
	tr.TLSClientConfig = cfg
	return &http.Client{Transport: tr}, nil
})

// httpsAPI returns the HTTPS API client, or fails the case.
func httpsAPI(c *check.C) *http.Client {
	c.Helper()
	client, err := httpsAPIClient()
	c.Must(err, "the HTTPS API client")
	return client
}

// httpsAPIURL is the URL of a path on the server's HTTPS API.
func httpsAPIURL(srs *server.Server, path string) string {
	return "https://" + srs.Addr(server.HTTPSAPI) + path
}

// httpsAPIObject gets a path from the HTTPS API, which answers 200 with a JSON
// object, and returns the object.
func httpsAPIObject(c *check.C, srs *server.Server, path string) map[string]any {
	c.Helper()
	r := apiRequest(c, httpsAPI(c), http.MethodGet, httpsAPIURL(srs, path), "", nil)
	c.CheckEqual(r.status, http.StatusOK, "HTTPS "+path+" status")
	c.CheckEqual(r.header.Get("Content-Type"), "application/json", "HTTPS "+path+" content type")
	var v map[string]any
	c.Must(json.Unmarshal(r.body, &v), "decode HTTPS "+path+": "+string(r.body))
	c.CheckEqual(v["code"], 0.0, "HTTPS "+path+" code")
	return v
}

// apiExporter adds the cases of the exporter to TestApi_Common: the format
// of /metrics, the info metrics and the ports that serve it. Common's other
// streams change its counts, so TestApi_Stats checks those.
func apiExporter(t *testing.T) {
	// MetricsFormat: the exporter answers text that a scraper parses, with
	// every family in order, each with its HELP and TYPE and one sample,
	// and the info metrics of value 1.
	check.Case(t, "MetricsFormat", check.Fast, func(c *check.C) {
		fams := scrape(c, server.Common(c))
		var names []string
		for _, f := range fams {
			names = append(names, f.Name)
		}
		var want []string
		for _, f := range metricFamilies {
			want = append(want, f.name)
		}
		c.CheckEqual(names, want, "the families")
		for _, w := range metricFamilies {
			f := metrics.Find(fams, w.name)
			if f == nil {
				continue
			}
			c.CheckEqual(f.Type, w.typ, w.name+" type")
			c.Check(f.Help != "", "%s has no HELP", w.name)
			c.CheckEqual(len(f.Samples), 1, w.name+" samples")
			for _, s := range f.Samples {
				c.Check(s.Value >= 0, "%s is %v, want not negative", w.name, s.Value)
				if strings.HasSuffix(w.name, "_info") {
					c.CheckEqual(s.Value, 1.0, w.name+" value")
				} else {
					c.CheckEqual(len(s.Labels), 0, w.name+" labels")
				}
			}
		}
	})

	// MetricsBuildInfo: srs_build_info names the server as the API does: its
	// ids, its pid, its version, with no label or tag when the exporter has
	// none.
	check.Case(t, "MetricsBuildInfo", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		s := oneSample(c, scrape(c, srs), "srs_build_info")
		c.CheckEqual(s.LabelNames(), buildLabels, "srs_build_info labels")
		v := apiObject(c, srs, "/api/v1/versions")
		data := object(c, v, "data")
		label := func(name string) string { l, _ := s.Label(name); return l }
		c.CheckEqual(label("server"), v["server"], "server")
		c.CheckEqual(label("service"), v["service"], "service")
		c.CheckEqual(label("pid"), strconv.Itoa(srs.PID()), "pid")
		c.CheckEqual(label("version"), data["version"], "version")
		c.CheckEqual(label("major"), fmt.Sprint(data["major"]), "major")
		c.Check(label("code") != "", "srs_build_info has an empty code")
		_, err := time.Parse(time.DateTime, label("build_date"))
		c.CheckNoError(err, "build_date "+label("build_date"))
	})

	// MetricsUnameInfo: srs_node_uname_info has the host's uname.
	check.Case(t, "MetricsUnameInfo", check.Fast, func(c *check.C) {
		s := oneSample(c, scrape(c, server.Common(c)), "srs_node_uname_info")
		c.CheckEqual(s.LabelNames(), unameLabels, "srs_node_uname_info labels")
		for name, flag := range map[string]string{"sysname": "-s", "nodename": "-n", "release": "-r", "version": "-v", "machine": "-m"} {
			got, _ := s.Label(name)
			c.CheckEqual(got, uname(c, flag), name)
		}
	})

	// MetricsOnApiPorts: the API and the HTTPS API serve /metrics too, the
	// same families as the exporter's port.
	check.Case(t, "MetricsOnApiPorts", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		want := scrape(c, srs)
		for _, url := range []string{srs.APIURL("/metrics"), httpsAPIURL(srs, "/metrics")} {
			client := httpClient
			if strings.HasPrefix(url, "https:") {
				client = httpsAPI(c)
			}
			fams := parseMetrics(c, scrapeText(c, client, url))
			c.CheckEqual(len(fams), len(want), url+" families")
			for i := range min(len(fams), len(want)) {
				c.CheckEqual(fams[i].Name, want[i].Name, url+" family")
			}
		}
	})

	// MetricsMemoryLinux: srs_memory is the process's resident memory in kB,
	// near what ps reports; SRS samples it every second.
	check.Case(t, "MetricsMemoryLinux", check.Normal, func(c *check.C) {
		linuxOnly(c)
		srs := server.Common(c)
		var mem float64
		c.Untimed(func() {
			until(2*time.Second, func() bool { mem = oneSample(c, scrape(c, srs), "srs_memory").Value; return mem > 0 })
		})
		out, err := exec.Command("ps", "-o", "rss=", "-p", strconv.Itoa(srs.PID())).Output()
		c.Must(err, "ps")
		rss, err := strconv.ParseFloat(strings.TrimSpace(string(out)), 64)
		c.Must(err, "the RSS of ps")
		c.Check(mem >= rss/4 && mem <= rss*4, "srs_memory %v, want near the RSS of %v kB", mem, rss)
	})
}

// apiHTTPS adds the cases of the HTTPS API to TestApi_Common: the test
// certificate, the same answers as the API, WebRTC signaling, a DELETE, CORS
// and bad input on its port.
func apiHTTPS(t *testing.T) {
	// HttpsApiCertificate: the HTTPS API presents the test certificate.
	check.Case(t, "HttpsApiCertificate", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		res, err := httpsAPI(c).Get(httpsAPIURL(srs, "/api/v1/versions"))
		c.Must(err, "GET versions over HTTPS")
		res.Body.Close()
		st := res.TLS
		if c.Check(st != nil && len(st.VerifiedChains) == 1 && len(st.VerifiedChains[0]) == 1, "verified chains, want one chain of the test certificate") {
			c.CheckEqual(st.VerifiedChains[0][0].Subject.CommonName, "localhost", "certificate subject")
			c.Check(st.Version >= 0x0303, "TLS version %x, want 1.2 or later", st.Version)
		}
	})

	// HttpsApiVersions: the HTTPS API answers as the API, with the same ids.
	check.Case(t, "HttpsApiVersions", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		got := httpsAPIObject(c, srs, "/api/v1/versions")
		want := apiObject(c, srs, "/api/v1/versions")
		c.CheckEqual(got, want, "versions over HTTPS")
	})

	// HttpsApiEndpoints: every system endpoint and list answers over HTTPS.
	check.Case(t, "HttpsApiEndpoints", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		for _, p := range append(slices.Clone(systemEndpoints), "/api/v1/vhosts/", "/api/v1/streams/", "/api/v1/clients/") {
			v := httpsAPIObject(c, srs, p)
			c.CheckEqual(v["server"], apiObject(c, srs, "/api/v1/versions")["server"], "HTTPS "+p+" server id")
		}
	})

	// HttpsApiStreamIds: a stream and its publisher are listed over HTTPS
	// with the ids the hooks have.
	check.Case(t, "HttpsApiStreamIds", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		_, pubID, streamID := publishRTMP(c, srs, stream, media.H264HighAAC)
		st := object(c, httpsAPIObject(c, srs, "/api/v1/streams/"+streamID), "stream")
		c.CheckEqual(st["name"], stream, "stream name")
		cl := object(c, httpsAPIObject(c, srs, "/api/v1/clients/"+pubID), "client")
		c.CheckEqual(cl["stream"], streamID, "the publisher's stream")
	})

	// HttpsApiWhipWhep publishes over WHIP and plays over WHEP through the
	// HTTPS API, as a browser on an HTTPS page does.
	check.Case(t, "HttpsApiWhipWhep", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		opts := &rtc.Options{Client: httpsAPI(c)}
		pub := rtcPublisher(c, httpsAPIURL(srs, "/rtc/v1/whip/?app=live&stream="+stream), opts)
		player := rtcPlayer(c, httpsAPIURL(srs, "/rtc/v1/whep/?app=live&stream="+stream), opts)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})

	// HttpsApiDeleteClient deletes a WHEP player over HTTPS: it leaves.
	check.Case(t, "HttpsApiDeleteClient", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		whipStream(c, srs, stream, 200*time.Millisecond)
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		id := waitHook(c, srs, "on_play", stream).Field("client_id")
		checkKicked(c, apiRequest(c, httpsAPI(c), http.MethodDelete, httpsAPIURL(srs, "/api/v1/clients/"+id), "", nil))
		c.CheckNoError(untilGone(c, srs, "/api/v1/clients/"+id), "the player leaves the API")
		c.CheckNoError(untilPCLeaves(player.PC, kickWithin), "the player's peer connection")
	})

	// HttpsApiCorsPreflight sends a browser's preflight over HTTPS: 200 with
	// the CORS headers, as the API.
	check.Case(t, "HttpsApiCorsPreflight", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		header := originHeader.Clone()
		header.Set("Access-Control-Request-Method", http.MethodDelete)
		r := apiRequest(c, httpsAPI(c), http.MethodOptions, httpsAPIURL(srs, "/api/v1/clients/none"), "", header)
		c.CheckEqual(r.status, http.StatusOK, "status")
		checkCors(c, r, "preflight over HTTPS")
	})

	// PlainHttpOnHttpsApiPort sends a plain HTTP request to the HTTPS API's
	// port: SRS closes the connection, perhaps after a TLS alert, and still
	// serves the HTTPS API.
	check.Case(t, "PlainHttpOnHttpsApiPort", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		conn := rawConnTo(c, srs.Addr(server.HTTPSAPI))
		_, err := fmt.Fprintf(conn, "GET /api/v1/versions HTTP/1.1\r\nHost: localhost\r\n\r\n")
		c.Must(err, "write a plain request")
		checkTLSRefused(c, conn, "after a plain HTTP request")
		httpsAPIObject(c, srs, "/api/v1/versions")
	})

	// HttpsOnApiPort dials the plain API port with TLS: the handshake fails
	// at once, and SRS still serves the API.
	check.Case(t, "HttpsOnApiPort", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		_, err := httpsAPI(c).Get("https://" + srs.Addr(server.HTTPAPI) + "/api/v1/versions")
		c.Check(err != nil, "an HTTPS request to the API port succeeded")
		c.Check(!isTimeout(err), "the API port didn't end the TLS handshake: %v", err)
		apiObject(c, srs, "/api/v1/versions")
	})

	// CloseInHttpsApiHandshake sends part of a TLS ClientHello to the HTTPS
	// API and closes: SRS still serves it.
	check.Case(t, "CloseInHttpsApiHandshake", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		conn := rawConnTo(c, srs.Addr(server.HTTPSAPI))
		_, err := conn.Write([]byte{0x16, 0x03, 0x01, 0x02, 0x00, 0x01, 0x00, 0x01, 0xfc, 0x03})
		c.Must(err, "write part of a ClientHello")
		conn.Close()
		httpsAPIObject(c, srs, "/api/v1/versions")
	})
}

// apiStatsMetrics adds the cases of the exporter's counts and labels to
// TestApi_Stats, whose cases take lock and start from an idle server.
func apiStatsMetrics(t *testing.T, srs *server.Server, lock func(*check.C)) {
	// MetricsFollowClients publishes a stream with an RTMP and an HTTP-FLV
	// player, then each closes: srs_streams and srs_clients follow each
	// step, as the API's totals do.
	check.Case(t, "MetricsFollowClients", check.Normal, func(c *check.C) {
		lock(c)
		checkGauges := func(what string, streams, clients int) {
			c.Helper()
			var s, cl float64
			ok := until(statsWait(), func() bool {
				s, cl = metric(c, srs, "srs_streams"), metric(c, srs, "srs_clients")
				return s == float64(streams) && cl == float64(clients)
			})
			c.Check(ok, "%s: srs_streams %v srs_clients %v, want %d and %d", what, s, cl, streams, clients)
			_, ns := apiList(c, srs, "streams", "?count=1")
			_, nc := apiList(c, srs, "clients", "?count=1")
			c.Check(ns == streams && nc == clients, "%s: the API totals %d streams %d clients, want %d and %d", what, ns, nc, streams, clients)
		}
		checkGauges("idle", 0, 0)
		stream := streamName(c)
		pub, _, _ := publishRTMP(c, srs, stream, media.H264HighAAC)
		checkGauges("published", 1, 1)
		rtmpPlayer, _ := playRTMPOf(c, srs, stream, nil)
		checkGauges("an RTMP player", 1, 2)
		flvPlayer := httpFlvPlayer(c, srs, "live/"+stream+".flv", nil)
		readHTTPFlv(c, flvPlayer, playedUntil)
		checkGauges("an HTTP-FLV player", 1, 3)
		rtmpPlayer.Close()
		flvPlayer.Close()
		checkGauges("the players closed", 1, 1)
		pub.Close()
		checkGauges("the publisher closed", 0, 0)
	})

	// MetricsBytesAfterClose publishes with a player, then both close: the
	// byte totals grew by what SRS read from them and at least what they
	// read from SRS.
	check.Case(t, "MetricsBytesAfterClose", check.Normal, func(c *check.C) {
		lock(c)
		recv0, send0 := metric(c, srs, "srs_receive_bytes_total"), metric(c, srs, "srs_send_bytes_total")
		stream := streamName(c)
		pub, pubID, _ := publishRTMP(c, srs, stream, media.H264HighAAC)
		player, playID := playRTMPOf(c, srs, stream, nil)
		// The player first, and gone before the publisher closes: with no
		// media, SRS sees a player's close only at its next wait, 350ms later.
		player.Close()
		c.Must(untilGone(c, srs, "/api/v1/clients/"+playID), "the player leaves")
		pub.Close()
		c.Must(untilGone(c, srs, "/api/v1/clients/"+pubID), "the publisher leaves")

		recv := metric(c, srs, "srs_receive_bytes_total") - recv0
		send := metric(c, srs, "srs_send_bytes_total") - send0
		in := float64(pub.BytesSent() + player.BytesSent())
		out := float64(pub.BytesReceived() + player.BytesReceived())
		c.CheckEqual(recv, in, "srs_receive_bytes_total, the bytes the clients sent")
		c.Check(send >= out, "srs_send_bytes_total grew %v, want at least the %v the clients read", send, out)
	})

	// MetricsLabelAndTag: srs_build_info has the exporter's label and tag
	// after its ids.
	check.Case(t, "MetricsLabelAndTag", check.Normal, func(c *check.C) {
		lock(c)
		text := scrapeText(c, httpClient, "http://"+srs.Addr(server.Exporter)+"/metrics")
		line := metricLine(c, text, "srs_build_info")
		c.Check(strings.Contains(line, `,label="`+statsExporterLabel+`",tag="`), "srs_build_info %s, want the label %s then the tag", line, statsExporterLabel)
	})

	// MetricsTagEscaped: a tag with a quote and a backslash is escaped in
	// srs_build_info, so a scraper reads the whole scrape and the tag as it
	// is.
	check.Case(t, "MetricsTagEscaped", check.Normal, func(c *check.C) {
		lock(c)
		text := scrapeText(c, httpClient, "http://"+srs.Addr(server.Exporter)+"/metrics")
		fams, err := metrics.Parse(bytes.NewReader(text))
		if !c.CheckNoError(err, "parse /metrics") {
			return
		}
		tag, _ := oneSample(c, fams, "srs_build_info").Label("tag")
		c.CheckEqual(tag, statsExporterTag, "the tag")
	})
}
