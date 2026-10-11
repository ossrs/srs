// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package rtmp

import (
	"fmt"
	"net"
	"net/url"
	"strings"
)

// URL is an RTMP URL split the way a client sends it: the tcUrl and app of
// connect, and the stream name of publish or play.
type URL struct {
	// Scheme is rtmp or rtmps.
	Scheme string
	// Addr is the host and port to dial, with the scheme's default port.
	Addr string
	// TcURL is the URL up to the app, as written.
	TcURL string
	// App is every path segment but the last, such as live or live/a/b.
	App string
	// Stream is the last path segment with the query, such as
	// livestream?token=x.
	Stream string
}

var defaultPorts = map[string]string{"rtmp": "1935", "rtmps": "443"}

// ParseURL splits rtmp://host[:port]/app[/more]/stream[?query].
func ParseURL(raw string) (URL, error) {
	u, err := url.Parse(raw)
	if err != nil {
		return URL{}, err
	}
	port, ok := defaultPorts[u.Scheme]
	if !ok {
		return URL{}, fmt.Errorf("unsupported URL %v, want rtmp:// or rtmps://", raw)
	}
	if u.Hostname() == "" {
		return URL{}, fmt.Errorf("no host in %v", raw)
	}
	if u.Port() != "" {
		port = u.Port()
	}

	path := strings.TrimPrefix(u.Path, "/")
	i := strings.LastIndex(path, "/")
	if i <= 0 || i == len(path)-1 {
		return URL{}, fmt.Errorf("no app and stream in %v, want rtmp://host/app/stream", raw)
	}
	app, stream := path[:i], path[i+1:]
	if u.RawQuery != "" {
		stream += "?" + u.RawQuery
	}
	return URL{
		Scheme: u.Scheme,
		Addr:   net.JoinHostPort(u.Hostname(), port),
		TcURL:  u.Scheme + "://" + u.Host + "/" + app,
		App:    app,
		Stream: stream,
	}, nil
}
