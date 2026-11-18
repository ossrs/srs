// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package utils

import (
	"context"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
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
	logger.Warn(ctx, "HTTP API error %+v", err)
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	w.WriteHeader(http.StatusInternalServerError)
	fmt.Fprintf(w, "%v\n", err)
}

func ApiCORS(ctx context.Context, w http.ResponseWriter, r *http.Request) bool {
	// A browser may send an Origin header for m3u8 but not for ts, so the CORS headers are always set.
	if true {
		// SRS needs no cookies or credentials, so * is used for the origin, headers and methods.
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

func ParseBody(r io.ReadCloser, v interface{}) error {
	defer r.Close()

	b, err := io.ReadAll(r)
	if err != nil {
		return errors.Wrapf(err, "read body")
	}

	if len(b) == 0 {
		return nil
	}

	if err := json.Unmarshal(b, v); err != nil {
		return errors.Wrapf(err, "json unmarshal %v", string(b))
	}

	return nil
}

func BuildStreamURL(r string) (string, error) {
	u, err := url.Parse(r)
	if err != nil {
		return "", errors.Wrapf(err, "parse url %v", r)
	}

	defaultVhost := !strings.Contains(u.Hostname(), ".")

	if ip := net.ParseIP(u.Hostname()); ip.To4() != nil {
		defaultVhost = true
	}

	if defaultVhost {
		return fmt.Sprintf("__defaultVhost__%v", u.Path), nil
	}

	return fmt.Sprintf("%v%v", u.Hostname(), u.Path), nil
}

func IsPeerClosedError(err error) bool {
	causeErr := errors.Cause(err)

	if errors.Is(causeErr, io.EOF) {
		return true
	}

	if errors.Is(causeErr, syscall.EPIPE) {
		return true
	}

	if netErr, ok := causeErr.(*net.OpError); ok {
		if sysErr, ok := netErr.Err.(*os.SyscallError); ok {
			if errors.Is(sysErr.Err, syscall.ECONNRESET) {
				return true
			}
		}
	}

	return false
}

func IsClosedNetworkError(err error) bool {
	if err == nil {
		return false
	}

	causeErr := errors.Cause(err)

	if netErr, ok := causeErr.(*net.OpError); ok {
		return netErr.Err.Error() == "use of closed network connection"
	}

	return strings.Contains(causeErr.Error(), "use of closed network connection")
}

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

	q := r.URL.Query()
	if app := q.Get("app"); app != "" {
		appStream = "/" + app
	}
	if stream := q.Get("stream"); stream != "" {
		appStream = fmt.Sprintf("%v/%v", appStream, stream)
	}

	if appStream == "" {
		streamExt = path.Ext(r.URL.Path)
		appStream = strings.TrimSuffix(r.URL.Path, streamExt)
	}

	unifiedURL = fmt.Sprintf("%v://%v%v", scheme, hostname, appStream)
	fullURL = fmt.Sprintf("%v%v", unifiedURL, streamExt)
	return
}

func RtcIsSTUN(data []byte) bool {
	return len(data) > 0 && (data[0] == 0 || data[0] == 1)
}

func RtcIsRTPOrRTCP(data []byte) bool {
	return len(data) >= 12 && (data[0]&0xC0) == 0x80
}

func SrtIsHandshake(data []byte) bool {
	return len(data) >= 4 && binary.BigEndian.Uint32(data) == 0x80000000
}

func SrtParseSocketID(data []byte) uint32 {
	if len(data) >= 16 {
		return binary.BigEndian.Uint32(data[12:])
	}
	return 0
}

// The value also stops at a backslash, since a JSON-escaped SDP body has its line
// breaks as the 2-byte text "\r" and "\n", not as control characters.
func ParseIceUfragPwd(sdp string) (ufrag, pwd string, err error) {
	if true {
		ufragRe := regexp.MustCompile(`a=ice-ufrag:([^\s\\]+)`)
		ufragMatch := ufragRe.FindStringSubmatch(sdp)
		if len(ufragMatch) <= 1 {
			return "", "", errors.Errorf("no ice-ufrag in sdp %v", sdp)
		}
		ufrag = ufragMatch[1]
	}

	if true {
		pwdRe := regexp.MustCompile(`a=ice-pwd:([^\s\\]+)`)
		pwdMatch := pwdRe.FindStringSubmatch(sdp)
		if len(pwdMatch) <= 1 {
			return "", "", errors.Errorf("no ice-pwd in sdp %v", sdp)
		}
		pwd = pwdMatch[1]
	}

	return ufrag, pwd, nil
}

// The stream ID format is at https://ossrs.io/lts/en-us/docs/v7/doc/srt#srt-url
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

func ParseListenEndpoint(ep string) (protocol string, ip net.IP, port uint16, err error) {
	if !strings.Contains(ep, ":") {
		if p, err := strconv.Atoi(ep); err != nil {
			return "", nil, 0, errors.Wrapf(err, "parse port %v", ep)
		} else {
			return "tcp", nil, uint16(p), nil
		}
	}

	if strings.Contains(ep, "://") {
		parts := strings.SplitN(ep, "://", 2)
		if len(parts) != 2 {
			return "", nil, 0, errors.Errorf("invalid endpoint %v", ep)
		}

		protocol = parts[0]
		hostPort := parts[1]

		if strings.Contains(hostPort, ":") {
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
