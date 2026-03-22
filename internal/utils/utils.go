// Copyright (c) 2025 Winlin
//
// SPDX-License-Identifier: MIT
package utils

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
	"strconv"
	"strings"
	"syscall"

	"srsx/internal/errors"
	"srsx/internal/logger"
	"srsx/internal/version"
)

func ApiResponse(ctx context.Context, w http.ResponseWriter, r *http.Request, data any) {
	w.Header().Set("Server", fmt.Sprintf("%v/%v", version.Signature(), version.Version()))

	b, err := json.Marshal(data)
	if err != nil {
		ApiError(ctx, w, r, errors.Wrapf(err, "marshal %v %v", reflect.TypeOf(data), data))
		return
	}

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	w.Write(b)
}

func ApiError(ctx context.Context, w http.ResponseWriter, r *http.Request, err error) {
	logger.Wf(ctx, "HTTP API error %+v", err)
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	w.WriteHeader(http.StatusInternalServerError)
	fmt.Fprintf(w, "%v\n", err)
}

func ApiCORS(ctx context.Context, w http.ResponseWriter, r *http.Request) bool {
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

// BuildStreamURL build as vhost/app/stream for stream URL r.
func BuildStreamURL(r string) (string, error) {
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

// IsPeerClosedError indicates whether peer object closed the connection.
func IsPeerClosedError(err error) bool {
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

// IsClosedNetworkError indicates whether the error is due to a closed network connection.
func IsClosedNetworkError(err error) bool {
	if err == nil {
		return false
	}

	// Unwrap to get the underlying error
	causeErr := errors.Cause(err)

	// Check for "use of closed network connection" error
	if netErr, ok := causeErr.(*net.OpError); ok {
		return netErr.Err.Error() == "use of closed network connection"
	}

	// Also check if the error message contains the text
	return strings.Contains(causeErr.Error(), "use of closed network connection")
}

// ConvertURLToStreamURL convert the URL in HTTP request to special URLs. The unifiedURL is the URL
// in unified, foramt as scheme://vhost/app/stream without extensions. While the fullURL is the unifiedURL
// with extension.
func ConvertURLToStreamURL(r *http.Request) (unifiedURL, fullURL string) {
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

// RtcIsSTUN returns true if data of UDP payload is a STUN packet.
func RtcIsSTUN(data []byte) bool {
	return len(data) > 0 && (data[0] == 0 || data[0] == 1)
}

// RtcIsRTPOrRTCP returns true if data of UDP payload is a RTP or RTCP packet.
func RtcIsRTPOrRTCP(data []byte) bool {
	return len(data) >= 12 && (data[0]&0xC0) == 0x80
}

// SrtIsHandshake returns true if data of UDP payload is a SRT handshake packet.
func SrtIsHandshake(data []byte) bool {
	return len(data) >= 4 && binary.BigEndian.Uint32(data) == 0x80000000
}

// SrtParseSocketID parse the socket id from the SRT packet.
func SrtParseSocketID(data []byte) uint32 {
	if len(data) >= 16 {
		return binary.BigEndian.Uint32(data[12:])
	}
	return 0
}

// ParseIceUfragPwd parse the ice-ufrag and ice-pwd from the SDP.
func ParseIceUfragPwd(sdp string) (ufrag, pwd string, err error) {
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

// ParseSRTStreamID parse the SRT stream id to host(optional) and resource(required).
// See https://ossrs.io/lts/en-us/docs/v7/doc/srt#srt-url
func ParseSRTStreamID(sid string) (host, resource string, err error) {
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

// ParseListenEndpoint parse the listen endpoint as:
//
//	port The tcp listen port, like 1935.
//	protocol://ip:port The listen endpoint, like tcp://:1935 or tcp://0.0.0.0:1935
func ParseListenEndpoint(ep string) (protocol string, ip net.IP, port uint16, err error) {
	// If no colon in ep, it's port in string.
	if !strings.Contains(ep, ":") {
		if p, err := strconv.Atoi(ep); err != nil {
			return "", nil, 0, errors.Wrapf(err, "parse port %v", ep)
		} else {
			return "tcp", nil, uint16(p), nil
		}
	}

	// Handle URL-style format: protocol://host:port or protocol://port
	if strings.Contains(ep, "://") {
		parts := strings.SplitN(ep, "://", 2)
		if len(parts) != 2 {
			return "", nil, 0, errors.Errorf("invalid endpoint %v", ep)
		}

		protocol = parts[0]
		hostPort := parts[1]

		// Check if there's a port specified
		if strings.Contains(hostPort, ":") {
			// Format: protocol://host:port
			host, portStr, err := net.SplitHostPort(hostPort)
			if err != nil {
				return "", nil, 0, errors.Wrapf(err, "parse host:port %v", hostPort)
			}

			p, err := strconv.Atoi(portStr)
			if err != nil {
				return "", nil, 0, errors.Wrapf(err, "parse port %v", portStr)
			}

			if host != "" {
				ip = net.ParseIP(host)
			}
			return protocol, ip, uint16(p), nil
		} else {
			// Format: protocol://port
			p, err := strconv.Atoi(hostPort)
			if err != nil {
				return "", nil, 0, errors.Wrapf(err, "parse port %v", hostPort)
			}
			return protocol, nil, uint16(p), nil
		}
	}

	// Legacy format: protocol:ip:port
	parts := strings.Split(ep, ":")
	if len(parts) != 3 {
		return "", nil, 0, errors.Errorf("invalid endpoint %v", ep)
	}

	if p, err := strconv.Atoi(parts[2]); err != nil {
		return "", nil, 0, errors.Wrapf(err, "parse port %v", parts[2])
	} else {
		return parts[0], net.ParseIP(parts[1]), uint16(p), nil
	}
}
