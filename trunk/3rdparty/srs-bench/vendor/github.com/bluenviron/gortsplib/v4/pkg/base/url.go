package base

import (
	"fmt"
	"net/url"
	"regexp"
	"strings"
)

// URL is a RTSP URL.
// This is basically an HTTP URL with some additional functions to handle
// control attributes.
type URL url.URL

var escapeRegexp = regexp.MustCompile(`^(.+?)://(.*?)@(.*?)/(.*?)$`)

// ParseURL parses a RTSP URL.
func ParseURL(s string) (*URL, error) {
	// https://github.com/golang/go/issues/30611
	m := escapeRegexp.FindStringSubmatch(s)
	if m != nil {
		m[3] = strings.ReplaceAll(m[3], "%25", "%")
		m[3] = strings.ReplaceAll(m[3], "%", "%25")
		s = m[1] + "://" + m[2] + "@" + m[3] + "/" + m[4]
	}

	u, err := url.Parse(s)
	if err != nil {
		return nil, err
	}

	if u.Scheme != "rtsp" && u.Scheme != "rtsps" {
		return nil, fmt.Errorf("unsupported scheme '%s'", u.Scheme)
	}

	if u.Opaque != "" {
		return nil, fmt.Errorf("URLs with opaque data are not supported")
	}

	if u.Fragment != "" {
		return nil, fmt.Errorf("URLs with fragments are not supported")
	}

	return (*URL)(u), nil
}

// String implements fmt.Stringer.
func (u *URL) String() string {
	return (*url.URL)(u).String()
}

// Clone clones a URL.
func (u *URL) Clone() *URL {
	return (*URL)(&url.URL{
		Scheme:     u.Scheme,
		User:       u.User,
		Host:       u.Host,
		Path:       u.Path,
		RawPath:    u.RawPath,
		ForceQuery: u.ForceQuery,
		RawQuery:   u.RawQuery,
	})
}

// CloneWithoutCredentials clones a URL without its credentials.
func (u *URL) CloneWithoutCredentials() *URL {
	return (*URL)(&url.URL{
		Scheme:     u.Scheme,
		Host:       u.Host,
		Path:       u.Path,
		RawPath:    u.RawPath,
		ForceQuery: u.ForceQuery,
		RawQuery:   u.RawQuery,
	})
}

// RTSPPathAndQuery returns the path and query of a RTSP URL.
//
// Deprecated: not useful anymore.
func (u *URL) RTSPPathAndQuery() (string, bool) {
	var pathAndQuery string
	if u.RawPath != "" {
		pathAndQuery = u.RawPath
	} else {
		pathAndQuery = u.Path
	}
	if u.RawQuery != "" {
		pathAndQuery += "?" + u.RawQuery
	}

	return pathAndQuery, true
}

// Hostname returns u.Host, stripping any valid port number if present.
//
// If the result is enclosed in square brackets, as literal IPv6 addresses are,
// the square brackets are removed from the result.
func (u *URL) Hostname() string {
	return (*url.URL)(u).Hostname()
}

// Port returns the port part of u.Host, without the leading colon.
//
// If u.Host doesn't contain a valid numeric port, Port returns an empty string.
func (u *URL) Port() string {
	return (*url.URL)(u).Port()
}
