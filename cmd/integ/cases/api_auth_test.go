// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"encoding/base64"
	"encoding/json"
	"errors"
	"net/http"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/flv"
	"srs-integ/internal/media"
	"srs-integ/internal/rtc"
	"srs-integ/internal/server"
)

// The credentials of the auth variants.
const (
	authUser     = "admin"
	authPassword = "secret"
	authToken    = "integ-token"
)

// basicOf is the Authorization header of basic credentials.
func basicOf(user, password string) string {
	return "Basic " + base64.StdEncoding.EncodeToString([]byte(user+":"+password))
}

// apiRequest sends a request with an Authorization header, none when it is
// empty, and more headers, and reads the whole answer.
func apiRequest(c *check.C, client *http.Client, method, url, authorization string, header http.Header) apiReply {
	c.Helper()
	req, err := http.NewRequest(method, url, nil)
	c.Must(err, "request")
	for k, vs := range header {
		for _, v := range vs {
			req.Header.Add(k, v)
		}
	}
	if authorization != "" {
		req.Header.Set("Authorization", authorization)
	}
	return apiDo(c, client, req)
}

// checkCode0 checks an answer is 200 with a JSON body of code 0.
func checkCode0(c *check.C, r apiReply, what string) {
	c.Helper()
	c.CheckEqual(r.status, http.StatusOK, what+" status")
	var v struct {
		Code *int `json:"code"`
	}
	if c.CheckNoError(json.Unmarshal(r.body, &v), what+" body "+string(r.body)) {
		c.Check(v.Code != nil && *v.Code == 0, "%s code %v, want 0", what, v.Code)
	}
}

// checkUnauthorized checks an answer is SRS's refusal of credentials: 401 with no
// body, and, when challenge is not empty, a WWW-Authenticate of that scheme.
func checkUnauthorized(c *check.C, r apiReply, challenge, what string) {
	c.Helper()
	c.CheckEqual(r.status, http.StatusUnauthorized, what+" status")
	c.CheckEqual(string(r.body), "", what+" body")
	if challenge != "" {
		c.CheckEqual(r.header.Get("WWW-Authenticate"), challenge, what+" WWW-Authenticate")
	}
}

// checkCors checks the CORS headers of an answer to a request with an Origin.
func checkCors(c *check.C, r apiReply, what string) {
	c.Helper()
	for k, v := range map[string]string{
		"Access-Control-Allow-Origin":      "*",
		"Access-Control-Allow-Methods":     "*",
		"Access-Control-Allow-Headers":     "*",
		"Access-Control-Allow-Credentials": "false",
	} {
		c.CheckEqual(r.header.Get(k), v, what+" "+k)
	}
}

// apiProtectedPaths are API paths that each need the credentials, with the
// method of the request.
var apiProtectedPaths = []struct{ method, path string }{
	{http.MethodGet, "/api/"},
	{http.MethodGet, "/api/v1/"},
	{http.MethodGet, "/api/v1/versions"},
	{http.MethodGet, "/api/v1/summaries"},
	{http.MethodGet, "/api/v1/features"},
	{http.MethodGet, "/api/v1/vhosts/"},
	{http.MethodGet, "/api/v1/streams/"},
	{http.MethodGet, "/api/v1/clients/"},
	{http.MethodGet, "/api/v1/raw?rpc=raw"},
	{http.MethodGet, "/api/v1/raw?rpc=reload"},
	{http.MethodDelete, "/api/v1/clients/none"},
}

// apiPorts adds the cases of the API's dedicated port and its CORS headers to
// TestApi_Common: the HTTP server answers no API but versions, the API port
// serves no stream, and crossdomain is on by default.
func apiPorts(t *testing.T) {
	// ServerPortVersionsOnly asks the HTTP server's port, not the API's, for
	// API paths: it answers versions, as a health check, and nothing else.
	check.Case(t, "ServerPortVersionsOnly", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		base := "http://" + srs.Addr(server.HTTPServer)
		checkCode0(c, apiRequest(c, httpClient, http.MethodGet, base+"/api/v1/versions", "", nil), "versions")
		for _, p := range []string{"/api/v1/summaries", "/api/v1/streams/", "/api/v1/clients/"} {
			r := apiRequest(c, httpClient, http.MethodGet, base+p, "", nil)
			c.CheckEqual(r.status, http.StatusNotFound, p+" status")
		}
	})

	// ApiPortServesNoStreams asks the API's port for a published stream's
	// HTTP-FLV URL: it answers the API's root, not the stream.
	check.Case(t, "ApiPortServesNoStreams", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		name := streamName(c)
		publishRTMP(c, srs, name, media.H264HighAAC)
		checkHTTPStatus(c, srs, "live/"+name+".flv", nil, http.StatusOK)

		r := apiRequest(c, httpClient, http.MethodGet, srs.APIURL("/live/"+name+".flv"), "", nil)
		c.CheckEqual(r.header.Get("Content-Type"), "application/json", "content type")
		checkCode0(c, r, "the stream's path on the API port")
	})

	// WhipNotOnServerPort posts a WHIP offer to the HTTP server's port: 404,
	// the WebRTC signaling is on the API's port.
	check.Case(t, "WhipNotOnServerPort", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		url := "http://" + srs.Addr(server.HTTPServer) + "/rtc/v1/whip/?app=live&stream=" + streamName(c)
		rtcRefused(c, url, rtcOffer(c, true, []string{"H264"}, []string{"opus"}), http.StatusNotFound)
	})

	// ApiCorsHeaders asks the API with and without an Origin: crossdomain is
	// on by default, so a page of another site may read it, and a request
	// that is not cross-origin gets no CORS headers.
	check.Case(t, "ApiCorsHeaders", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		r := apiRequest(c, httpClient, http.MethodGet, srs.APIURL("/api/v1/summaries"), "", originHeader)
		checkCode0(c, r, "summaries")
		checkCors(c, r, "with an Origin:")

		r = apiRequest(c, httpClient, http.MethodGet, srs.APIURL("/api/v1/summaries"), "", nil)
		c.CheckEqual(r.header.Get("Access-Control-Allow-Origin"), "", "without an Origin: Access-Control-Allow-Origin")
	})

	// ApiCorsPreflight sends a browser's preflight: 200 with no body and the
	// CORS headers.
	check.Case(t, "ApiCorsPreflight", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		header := originHeader.Clone()
		header.Set("Access-Control-Request-Method", http.MethodDelete)
		r := apiRequest(c, httpClient, http.MethodOptions, srs.APIURL("/api/v1/clients/none"), "", header)
		c.CheckEqual(r.status, http.StatusOK, "status")
		c.CheckEqual(r.header.Get("Content-Length"), "0", "content length")
		checkCors(c, r, "preflight")
	})
}

// basicOnePortConfig is a server with basic authentication, whose HTTP API
// shares its port with the HTTP server, and the HTTPS API with the HTTPS
// server: RTMP, HTTP-FLV and WebRTC.
func basicOnePortConfig() server.Config {
	cfg := server.WithRTC(server.RemuxConfig("flv"), server.RTC{})
	cfg.Listeners = append(cfg.Listeners, server.HTTPSAPI, server.HTTPSServer, server.RTCUDP)
	cfg.OneHTTPPort = true
	cfg.APIAuth = &server.APIAuth{Type: "basic", Username: authUser, Password: authPassword}
	return cfg
}

// TestApi_BasicOnePort checks basic authentication, which protects only the
// API, on a server that serves the API and the streams on one port.
func TestApi_BasicOnePort(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, basicOnePortConfig())
	api := func(p string) string { return srs.APIURL(p) }
	good := basicOf(authUser, authPassword)

	// SharedPorts checks the API and the server listen on one port, and the
	// HTTPS API and server on another.
	check.Case(t, "SharedPorts", check.Fast, func(c *check.C) {
		p := srs.Ports
		c.Check(p[server.HTTPAPI] == p[server.HTTPServer] && p[server.HTTPSAPI] == p[server.HTTPSServer] && p[server.HTTPAPI] != p[server.HTTPSAPI],
			"ports %v, want the API and server on one port, and the HTTPS ones on another", p)
	})

	// NoCredentials asks without credentials: 401 with a Basic challenge and
	// no body.
	check.Case(t, "NoCredentials", check.Fast, func(c *check.C) {
		checkUnauthorized(c, apiRequest(c, httpClient, http.MethodGet, api("/api/v1/summaries"), "", nil), "Basic", "summaries")
	})

	// GoodCredentials asks with the right username and password.
	check.Case(t, "GoodCredentials", check.Fast, func(c *check.C) {
		checkCode0(c, apiRequest(c, httpClient, http.MethodGet, api("/api/v1/summaries"), good, nil), "summaries")
	})

	// WrongPassword and WrongUsername: 401 with a Basic challenge.
	check.Case(t, "WrongPassword", check.Fast, func(c *check.C) {
		r := apiRequest(c, httpClient, http.MethodGet, api("/api/v1/summaries"), basicOf(authUser, "wrong"), nil)
		checkUnauthorized(c, r, "Basic", "summaries")
	})

	check.Case(t, "WrongUsername", check.Fast, func(c *check.C) {
		r := apiRequest(c, httpClient, http.MethodGet, api("/api/v1/summaries"), basicOf("root", authPassword), nil)
		checkUnauthorized(c, r, "Basic", "summaries")
	})

	// MalformedCredentials sends credentials that are not a username and a
	// password: 401 each.
	malformed := map[string]string{
		"no colon":       "Basic " + base64.StdEncoding.EncodeToString([]byte(authUser)),
		"not base64":     "Basic !!!",
		"empty":          "Basic ",
		"another scheme": "Bearer " + authPassword,
	}
	check.Case(t, "MalformedCredentials", check.Fast, func(c *check.C) {
		for what, v := range malformed {
			checkUnauthorized(c, apiRequest(c, httpClient, http.MethodGet, api("/api/v1/summaries"), v, nil), "", what)
		}
	})

	// EveryRefusalChallenges checks those malformed credentials get the Basic
	// challenge too: a 401 must have a WWW-Authenticate (RFC 9110).
	check.Case(t, "EveryRefusalChallenges", check.Fast, func(c *check.C) {
		check.KnownBug(c, "a 401 for credentials that are not basic or don't parse has no WWW-Authenticate")
		for what, v := range malformed {
			r := apiRequest(c, httpClient, http.MethodGet, api("/api/v1/summaries"), v, nil)
			c.CheckEqual(r.header.Get("WWW-Authenticate"), "Basic", what+" WWW-Authenticate")
		}
	})

	// LowercaseScheme sends the right credentials as "basic": the scheme is
	// case-insensitive (RFC 9110).
	check.Case(t, "LowercaseScheme", check.Fast, func(c *check.C) {
		check.KnownBug(c, "the scheme of the credentials is matched case-sensitively")
		r := apiRequest(c, httpClient, http.MethodGet, api("/api/v1/summaries"), "basic"+strings.TrimPrefix(good, "Basic"), nil)
		checkCode0(c, r, "summaries")
	})

	// EveryApiPathProtected asks each API path, of each method, without and
	// with the credentials.
	check.Case(t, "EveryApiPathProtected", check.Fast, func(c *check.C) {
		for _, p := range apiProtectedPaths {
			what := p.method + " " + p.path
			checkUnauthorized(c, apiRequest(c, httpClient, p.method, api(p.path), "", nil), "Basic", what)
			r := apiRequest(c, httpClient, p.method, api(p.path), good, nil)
			c.CheckEqual(r.status, http.StatusOK, what+" with the credentials: status")
		}
	})

	// CorsPreflightOpen sends a browser's preflight, which never carries
	// credentials: 200 with the CORS headers.
	check.Case(t, "CorsPreflightOpen", check.Fast, func(c *check.C) {
		r := apiRequest(c, httpClient, http.MethodOptions, api("/api/v1/summaries"), "", originHeader)
		c.CheckEqual(r.status, http.StatusOK, "status")
		checkCors(c, r, "preflight")
	})

	// FlvOnSharedPort plays a stream over HTTP-FLV on the API's port, with no
	// credentials: they protect only the API.
	check.Case(t, "FlvOnSharedPort", check.Normal, func(c *check.C) {
		stream := "live/" + streamName(c)
		startPublisher(c, rtmpURL(srs, stream), media.Path(c, media.H264HighAAC), publishLead)
		p := httpFlvPlayer(c, srs, stream+".flv", nil)
		checkPlayed(c, readHTTPFlv(c, p, playedUntil), "h264", "aac")
	})

	// StreamUnderApiApp plays a stream of the app live/api over HTTP-FLV with
	// no credentials: its path is not under /api/. With the credentials it
	// plays, so the stream is there.
	check.Case(t, "StreamUnderApiApp", check.Normal, func(c *check.C) {
		check.KnownBug(c, "a stream path with /api/ in it needs the API's credentials")
		stream := "live/api/" + streamName(c)
		startPublisher(c, rtmpURL(srs, stream), media.Path(c, media.H264HighAAC), publishLead)
		checkHTTPStatus(c, srs, stream+".flv", &flv.HTTPOptions{Header: http.Header{"Authorization": {good}}}, http.StatusOK)
		checkHTTPStatus(c, srs, stream+".flv", nil, http.StatusOK)
	})

	// HttpsApiOnSharedPort asks the HTTPS server's port for the API, without
	// and with the credentials.
	check.Case(t, "HttpsApiOnSharedPort", check.Fast, func(c *check.C) {
		cfg, err := server.ClientTLS()
		c.Must(err, "the client TLS config")
		tr := httpClient.Transport.(*http.Transport).Clone()
		tr.TLSClientConfig = cfg
		client := &http.Client{Transport: tr}
		c.Cleanup(tr.CloseIdleConnections)

		url := "https://" + srs.Addr(server.HTTPSServer) + "/api/v1/summaries"
		checkUnauthorized(c, apiRequest(c, client, http.MethodGet, url, "", nil), "Basic", "summaries")
		checkCode0(c, apiRequest(c, client, http.MethodGet, url, good, nil), "summaries")
	})

	// WhipOnSharedPort publishes over WHIP on the shared port with no
	// credentials: basic authentication doesn't protect WebRTC signaling.
	check.Case(t, "WhipOnSharedPort", check.Normal, func(c *check.C) {
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})
}

// bearerConfig is a server with bearer authentication of the API, and of
// WebRTC signaling with rtc.
func bearerConfig(rtcToo bool) server.Config {
	cfg := server.WithRTC(server.Config{Listeners: []server.Listener{server.HTTPAPI, server.HTTPServer, server.RTCUDP}}, server.RTC{})
	cfg.APIAuth = &server.APIAuth{Type: "bearer", Token: authToken, RTC: rtcToo}
	return cfg
}

// bearerHeader is a request header with the bearer token.
var bearerHeader = http.Header{"Authorization": {"Bearer " + authToken}}

// TestApi_Bearer checks bearer authentication with rtc_bearer_enabled: the
// API and the WebRTC signaling need the token.
func TestApi_Bearer(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, bearerConfig(true))
	api := func(p string) string { return srs.APIURL(p) }
	good := "Bearer " + authToken

	check.Case(t, "NoToken", check.Fast, func(c *check.C) {
		checkUnauthorized(c, apiRequest(c, httpClient, http.MethodGet, api("/api/v1/summaries"), "", nil), "Bearer", "summaries")
	})

	check.Case(t, "GoodToken", check.Fast, func(c *check.C) {
		checkCode0(c, apiRequest(c, httpClient, http.MethodGet, api("/api/v1/summaries"), good, nil), "summaries")
	})

	// WrongToken and OtherCredentials: 401 with a Bearer challenge.
	check.Case(t, "WrongToken", check.Fast, func(c *check.C) {
		r := apiRequest(c, httpClient, http.MethodGet, api("/api/v1/summaries"), good+"x", nil)
		checkUnauthorized(c, r, "Bearer", "summaries")
	})

	check.Case(t, "OtherCredentials", check.Fast, func(c *check.C) {
		for what, v := range map[string]string{
			"basic":          basicOf(authUser, authToken),
			"empty token":    "Bearer ",
			"another scheme": "Token " + authToken,
			"token alone":    authToken,
		} {
			checkUnauthorized(c, apiRequest(c, httpClient, http.MethodGet, api("/api/v1/summaries"), v, nil), "Bearer", what)
		}
	})

	check.Case(t, "LowercaseScheme", check.Fast, func(c *check.C) {
		check.KnownBug(c, "the scheme of the credentials is matched case-sensitively")
		checkCode0(c, apiRequest(c, httpClient, http.MethodGet, api("/api/v1/summaries"), "bearer "+authToken, nil), "summaries")
	})

	check.Case(t, "EveryApiPathProtected", check.Fast, func(c *check.C) {
		for _, p := range apiProtectedPaths {
			what := p.method + " " + p.path
			checkUnauthorized(c, apiRequest(c, httpClient, p.method, api(p.path), "", nil), "Bearer", what)
			r := apiRequest(c, httpClient, p.method, api(p.path), good, nil)
			c.CheckEqual(r.status, http.StatusOK, what+" with the token: status")
		}
	})

	// RtcNeedsToken posts to each WebRTC signaling path without the token:
	// WHIP, WHEP, the legacy publish and play, and the NACK simulator.
	check.Case(t, "RtcNeedsToken", check.Fast, func(c *check.C) {
		stream := streamName(c)
		rtcRefused(c, whipURL(srs, "live", stream), rtcOffer(c, true, []string{"H264"}, []string{"opus"}), http.StatusUnauthorized)
		rtcRefused(c, whepURL(srs, "live", stream), rtcOffer(c, false, []string{"H264"}, []string{"opus"}), http.StatusUnauthorized)
		for _, p := range []string{"/rtc/v1/publish/", "/rtc/v1/play/", "/rtc/v1/nack/"} {
			checkUnauthorized(c, apiRequest(c, httpClient, http.MethodPost, api(p), "", nil), "Bearer", p)
		}
	})

	// WhipWhepWithToken publishes and plays with the token.
	check.Case(t, "WhipWhepWithToken", check.Normal, func(c *check.C) {
		stream := streamName(c)
		opts := &rtc.Options{Header: bearerHeader}
		pub := rtcPublisher(c, whipURL(srs, "live", stream), opts)
		player := rtcPlayer(c, whepURL(srs, "live", stream), opts)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})

	// DeleteNeedsToken ends a WHIP session by its Location: 401 without the
	// token, 200 with it.
	check.Case(t, "DeleteNeedsToken", check.Normal, func(c *check.C) {
		pub := rtcPublisher(c, whipURL(srs, "live", streamName(c)), &rtc.Options{Header: bearerHeader})
		if !c.Check(pub.Location != "", "the WHIP session has no Location") {
			return
		}
		checkUnauthorized(c, apiRequest(c, httpClient, http.MethodDelete, pub.Location, "", nil), "Bearer", "DELETE without the token")
		r := apiRequest(c, httpClient, http.MethodDelete, pub.Location, good, nil)
		c.CheckEqual(r.status, http.StatusOK, "DELETE with the token: status")
		c.CheckNoError(untilPCLeaves(pub.PC, time.Second), "the publisher after the DELETE")
	})

	// CorsPreflightOpen sends a browser's preflight of WHIP, which never
	// carries the token: 200.
	check.Case(t, "CorsPreflightOpen", check.Fast, func(c *check.C) {
		r := apiRequest(c, httpClient, http.MethodOptions, whipURL(srs, "live", streamName(c)), "", originHeader)
		c.CheckEqual(r.status, http.StatusOK, "status")
		checkCors(c, r, "preflight")
	})
}

// TestApi_BearerApiOnly checks bearer authentication without
// rtc_bearer_enabled, the default, so WebRTC signaling is open, and the
// API's crossdomain off, the HTTP server's on.
func TestApi_BearerApiOnly(t *testing.T) {
	t.Parallel()
	cfg := bearerConfig(false)
	cfg.Env["SRS_HTTP_API_CROSSDOMAIN"] = "off"
	srs := server.Group(t, cfg)

	check.Case(t, "ApiNeedsToken", check.Fast, func(c *check.C) {
		checkUnauthorized(c, apiRequest(c, httpClient, http.MethodGet, srs.APIURL("/api/v1/summaries"), "", nil), "Bearer", "summaries")
	})

	// WhipWithoutToken publishes and plays with no token.
	check.Case(t, "WhipWithoutToken", check.Normal, func(c *check.C) {
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})

	// ApiCrossdomainOff sends a preflight and a request with an Origin to the
	// API, whose crossdomain is off: 405, and no CORS headers.
	check.Case(t, "ApiCrossdomainOff", check.Fast, func(c *check.C) {
		check.KnownBug(c, "the API's CORS follows the HTTP server's crossdomain, not its own")
		r := apiRequest(c, httpClient, http.MethodOptions, srs.APIURL("/api/v1/summaries"), "", originHeader)
		c.CheckEqual(r.status, http.StatusMethodNotAllowed, "preflight status")
		c.CheckEqual(r.header.Get("Access-Control-Allow-Origin"), "", "preflight Access-Control-Allow-Origin")
		r = apiRequest(c, httpClient, http.MethodGet, srs.APIURL("/api/v1/versions"), "Bearer "+authToken, originHeader)
		c.CheckEqual(r.header.Get("Access-Control-Allow-Origin"), "", "Access-Control-Allow-Origin")
	})

	// ServerCrossdomainOn sends a preflight to the HTTP server, whose
	// crossdomain is on: 200 with the CORS headers.
	check.Case(t, "ServerCrossdomainOn", check.Fast, func(c *check.C) {
		url := "http://" + srs.Addr(server.HTTPServer) + "/live/" + streamName(c) + ".flv"
		r := apiRequest(c, httpClient, http.MethodOptions, url, "", originHeader)
		c.CheckEqual(r.status, http.StatusOK, "status")
		checkCors(c, r, "preflight")
	})
}

// TestApi_AuthConfig starts servers whose API authentication SRS refuses:
// each exits with an error before it is ready.
func TestApi_AuthConfig(t *testing.T) {
	t.Parallel()
	for _, tt := range []struct {
		name string
		auth server.APIAuth
	}{
		{"NoType", server.APIAuth{Username: authUser, Password: authPassword}},
		{"UnknownType", server.APIAuth{Type: "digest", Username: authUser, Password: authPassword}},
		{"BearerWithoutToken", server.APIAuth{Type: "bearer"}},
		{"RtcBearerWithBasic", server.APIAuth{Type: "basic", Username: authUser, Password: authPassword, RTC: true}},
	} {
		check.Case(t, tt.name, check.Normal, func(c *check.C) {
			auth := tt.auth
			srs, err := server.Start(c.Name(), server.Config{Listeners: []server.Listener{server.HTTPAPI}, APIAuth: &auth})
			if srs != nil {
				srs.Stop()
			}
			var exit *server.ExitError
			if c.Check(errors.As(err, &exit), "start: %v, want SRS to exit before it is ready", err) {
				c.Check(exit.Code != 0, "exit code %d, want an error", exit.Code)
			}
		})
	}
}
