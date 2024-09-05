// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"encoding/binary"
	"encoding/json"
	stdErr "errors"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"net/http"
	"net/url"
	"os"
	"path"
	"reflect"
	"regexp"
	"strings"
	"syscall"
	"time"

	"srs-proxy/errors"
	"srs-proxy/logger"
)

func apiResponse(ctx context.Context, w http.ResponseWriter, r *http.Request, data any) {
	w.Header().Set("Server", fmt.Sprintf("%v/%v", Signature(), Version()))

	b, err := json.Marshal(data)
	if err != nil {
		apiError(ctx, w, r, errors.Wrapf(err, "marshal %v %v", reflect.TypeOf(data), data))
		return
	}

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	w.Write(b)
}

func apiError(ctx context.Context, w http.ResponseWriter, r *http.Request, err error) {
	logger.Wf(ctx, "HTTP API error %+v", err)
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	w.WriteHeader(http.StatusInternalServerError)
	fmt.Fprintln(w, fmt.Sprintf("%v", err))
}

func apiCORS(ctx context.Context, w http.ResponseWriter, r *http.Request) bool {
	// Always support CORS. Note that browser may send origin header for m3u8, but no origin header
	// for ts. So we always response CORS header.
	if true {
		// SRS does not need cookie or credentials, so we disable CORS credentials, and use * for CORS origin,
		// headers, expose headers and methods.
		w.Header().Set("Access-Control-Allow-Origin", "*")
		// See https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Access-Control-Allow-Headers
		w.Header().Set("Access-Control-Allow-Headers", "*")
		// See https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Access-Control-Allow-Methods
		w.Header().Set("Access-Control-Allow-Methods", "*")
	}

	if r.Method == http.MethodOptions {
		w.WriteHeader(http.StatusOK)
		return true
	}

	return false
}

func parseGracefullyQuitTimeout() (time.Duration, error) {
	if t, err := time.ParseDuration(envGraceQuitTimeout()); err != nil {
		return 0, errors.Wrapf(err, "parse duration %v", envGraceQuitTimeout())
	} else {
		return t, nil
	}
}

// ParseBody read the body from r, and unmarshal JSON to v.
func ParseBody(r io.ReadCloser, v interface{}) error {
	b, err := ioutil.ReadAll(r)
	if err != nil {
		return errors.Wrapf(err, "read body")
	}
	defer r.Close()

	if len(b) == 0 {
		return nil
	}

	if err := json.Unmarshal(b, v); err != nil {
		return errors.Wrapf(err, "json unmarshal %v", string(b))
	}

	return nil
}

// buildStreamURL build as vhost/app/stream for stream URL r.
func buildStreamURL(r string) (string, error) {
	u, err := url.Parse(r)
	if err != nil {
		return "", errors.Wrapf(err, "parse url %v", r)
	}

	// If not domain or ip in hostname, it's __defaultVhost__.
	defaultVhost := !strings.Contains(u.Hostname(), ".")

	// If hostname is actually an IP address, it's __defaultVhost__.
	if ip := net.ParseIP(u.Hostname()); ip.To4() != nil {
		defaultVhost = true
	}

	if defaultVhost {
		return fmt.Sprintf("__defaultVhost__%v", u.Path), nil
	}

	// Ignore port, only use hostname as vhost.
	return fmt.Sprintf("%v%v", u.Hostname(), u.Path), nil
}

// isPeerClosedError indicates whether peer object closed the connection.
func isPeerClosedError(err error) bool {
	causeErr := errors.Cause(err)

	if stdErr.Is(causeErr, io.EOF) {
		return true
	}

	if stdErr.Is(causeErr, syscall.EPIPE) {
		return true
	}

	if netErr, ok := causeErr.(*net.OpError); ok {
		if sysErr, ok := netErr.Err.(*os.SyscallError); ok {
			if stdErr.Is(sysErr.Err, syscall.ECONNRESET) {
				return true
			}
		}
	}

	return false
}

// convertURLToStreamURL convert the URL in HTTP request to special URLs. The unifiedURL is the URL
// in unified, foramt as scheme://vhost/app/stream without extensions. While the fullURL is the unifiedURL
// with extension.
func convertURLToStreamURL(r *http.Request) (unifiedURL, fullURL string) {
	scheme := "http"
	if r.TLS != nil {
		scheme = "https"
	}

	hostname := "__defaultVhost__"
	if strings.Contains(r.Host, ":") {
		if v, _, err := net.SplitHostPort(r.Host); err == nil {
			hostname = v
		}
	}

	var appStream, streamExt string

	// Parse app/stream from query string.
	q := r.URL.Query()
	if app := q.Get("app"); app != "" {
		appStream = "/" + app
	}
	if stream := q.Get("stream"); stream != "" {
		appStream = fmt.Sprintf("%v/%v", appStream, stream)
	}

	// Parse app/stream from path.
	if appStream == "" {
		streamExt = path.Ext(r.URL.Path)
		appStream = strings.TrimSuffix(r.URL.Path, streamExt)
	}

	unifiedURL = fmt.Sprintf("%v://%v%v", scheme, hostname, appStream)
	fullURL = fmt.Sprintf("%v%v", unifiedURL, streamExt)
	return
}

// rtcIsSTUN returns true if data of UDP payload is a STUN packet.
func rtcIsSTUN(data []byte) bool {
	return len(data) > 0 && (data[0] == 0 || data[0] == 1)
}

// rtcIsRTPOrRTCP returns true if data of UDP payload is a RTP or RTCP packet.
func rtcIsRTPOrRTCP(data []byte) bool {
	return len(data) >= 12 && (data[0]&0xC0) == 0x80
}

// srtIsHandshake returns true if data of UDP payload is a SRT handshake packet.
func srtIsHandshake(data []byte) bool {
	return len(data) >= 4 && binary.BigEndian.Uint32(data) == 0x80000000
}

// srtParseSocketID parse the socket id from the SRT packet.
func srtParseSocketID(data []byte) uint32 {
	if len(data) >= 16 {
		return binary.BigEndian.Uint32(data[12:])
	}
	return 0
}

// parseIceUfragPwd parse the ice-ufrag and ice-pwd from the SDP.
func parseIceUfragPwd(sdp string) (ufrag, pwd string, err error) {
	if true {
		ufragRe := regexp.MustCompile(`a=ice-ufrag:([^\s]+)`)
		ufragMatch := ufragRe.FindStringSubmatch(sdp)
		if len(ufragMatch) <= 1 {
			return "", "", errors.Errorf("no ice-ufrag in sdp %v", sdp)
		}
		ufrag = ufragMatch[1]
	}

	if true {
		pwdRe := regexp.MustCompile(`a=ice-pwd:([^\s]+)`)
		pwdMatch := pwdRe.FindStringSubmatch(sdp)
		if len(pwdMatch) <= 1 {
			return "", "", errors.Errorf("no ice-pwd in sdp %v", sdp)
		}
		pwd = pwdMatch[1]
	}

	return ufrag, pwd, nil
}

// parseSRTStreamID parse the SRT stream id to host(optional) and resource(required).
// See https://ossrs.io/lts/en-us/docs/v7/doc/srt#srt-url
func parseSRTStreamID(sid string) (host, resource string, err error) {
	if true {
		hostRe := regexp.MustCompile(`h=([^,]+)`)
		hostMatch := hostRe.FindStringSubmatch(sid)
		if len(hostMatch) > 1 {
			host = hostMatch[1]
		}
	}

	if true {
		resourceRe := regexp.MustCompile(`r=([^,]+)`)
		resourceMatch := resourceRe.FindStringSubmatch(sid)
		if len(resourceMatch) <= 1 {
			return "", "", errors.Errorf("no resource in sid %v", sid)
		}
		resource = resourceMatch[1]
	}

	return host, resource, nil
}
