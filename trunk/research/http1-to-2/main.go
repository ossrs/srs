package main

import (
	"context"
	"crypto/tls"
	"flag"
	"fmt"
	"golang.org/x/net/http2"
	"net"
	"net/http"
	"net/http/httputil"
	"net/url"
	"os"
	"strings"
)

func main() {
	var listen string
	flag.StringVar(&listen, "listen", ":4317", "The listen endpoint, overwrite by env SRS_HTTP1TO2_LISTEN, such as :4317")

	var backend string
	flag.StringVar(&backend, "backend", "ap-guangzhou.apm.tencentcs.com:4317", "The proxy backend, overwrite by env SRS_HTTP1TO2_BACKEND, such as ap-guangzhou.apm.tencentcs.com:4317")

	flag.Parse()

	if os.Getenv("SRS_HTTP1TO2_LISTEN") != "" {
		listen = os.Getenv("SRS_HTTP1TO2_LISTEN")
	}
	if os.Getenv("SRS_HTTP1TO2_BACKEND") != "" {
		backend = os.Getenv("SRS_HTTP1TO2_BACKEND")
	}

	wrapURL := func(backend string) string {
		if !strings.HasPrefix(backend, "http://") && !strings.HasPrefix(backend, "https://") {
			return "http://" + backend
		}
		return backend
	}

	u, err := url.Parse(wrapURL(backend))
	if err != nil {
		panic(err)
	}
	fmt.Println(fmt.Sprintf("Proxy HTTP1 to HTTP2, listen=%v, backend=%v, url=%v", listen, backend, u))

	// Proxy HTTP/1 requests to HTTP/2 server.
	p := httputil.NewSingleHostReverseProxy(u)
	p.Transport = &http2.Transport{
		AllowHTTP: true,
		DialTLSContext: func(ctx context.Context, network, addr string, cfg *tls.Config) (net.Conn, error) {
			return (&net.Dialer{}).DialContext(ctx, network, addr)
		},
	}

	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		fmt.Println(fmt.Sprintf("Proxy to %v, req=%v", u, r.URL))
		p.ServeHTTP(w, r)
	})

	if err := http.ListenAndServe(listen, nil); err != nil {
		panic(err)
	}
}
