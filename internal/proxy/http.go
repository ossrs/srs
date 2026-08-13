// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package proxy

import (
	"context"
	"fmt"
	"io"

	"net/http"
	"os"
	"strconv"
	"strings"
	stdSync "sync"
	"time"

	"srsx/internal/env"
	"srsx/internal/errors"
	"srsx/internal/lb"
	"srsx/internal/logger"
	"srsx/internal/utils"
	"srsx/internal/version"
)

// HTTPStreamProxyServer is the proxy server for SRS HTTP stream server, for HTTP-FLV, HTTP-TS,
// HLS, etc. The proxy server will figure out which SRS origin server to proxy to, then proxy
// the request to the origin server.
type HTTPStreamProxyServer interface {
	Run(ctx context.Context) error
	Close() error
}

// httpServer is the minimal contract of an HTTP server that httpStreamProxyServer drives.
// *http.Server satisfies it. Tests may supply a fake that does not bind a real port.
type httpServer interface {
	ListenAndServe() error
	Shutdown(ctx context.Context) error
}

// buildBackendHTTPURL composes the backend HTTP URL for a request path, targeting
// the given backend IP and port. Callers append query strings separately when needed.
func buildBackendHTTPURL(ip string, port int, path string) string {
	return fmt.Sprintf("http://%v:%v%s", ip, port, path)
}

// copyBackendResponseHeaders copies end-to-end response headers while dropping
// fields that apply only to the backend connection. Connection can nominate
// additional hop-by-hop fields, so collect those names before copying.
func copyBackendResponseHeaders(dst, src http.Header) {
	hopByHop := map[string]bool{
		"Connection":          true,
		"Proxy-Connection":    true,
		"Keep-Alive":          true,
		"Proxy-Authenticate":  true,
		"Proxy-Authorization": true,
		"Te":                  true,
		"Trailer":             true,
		"Transfer-Encoding":   true,
		"Upgrade":             true,
	}
	for _, value := range src.Values("Connection") {
		for _, name := range strings.Split(value, ",") {
			if name = strings.TrimSpace(name); name != "" {
				hopByHop[http.CanonicalHeaderKey(name)] = true
			}
		}
	}

	for name, values := range src {
		name = http.CanonicalHeaderKey(name)
		if hopByHop[name] {
			continue
		}

		dst.Del(name)
		for _, value := range values {
			dst.Add(name, value)
		}
	}
}

// repairRewrittenResponseHeaders removes backend representation metadata that
// no longer describes a rewritten response body, then publishes its new size.
func repairRewrittenResponseHeaders(header http.Header, contentLength int) {
	for _, name := range []string{
		"Accept-Ranges",
		"Content-Digest",
		"Content-Encoding",
		"Content-MD5",
		"Content-Range",
		"Digest",
		"ETag",
		"Repr-Digest",
	} {
		header.Del(name)
	}
	header.Set("Content-Length", strconv.Itoa(contentLength))
}

type httpStreamProxyServer struct {
	// The environment interface.
	environment env.ProxyEnvironment
	// The load balancer for origin servers.
	loadBalancer lb.OriginLoadBalancer
	// The underlayer HTTP server.
	server httpServer
	// The gracefully quit timeout, wait server to quit.
	gracefulQuitTimeout time.Duration
	// The wait group for all goroutines.
	wg stdSync.WaitGroup
	// shutdown gracefully shuts down the underlying HTTP server. Defaults to
	// v.server.Shutdown; tests may override via a functional option to verify
	// the shutdown contract without binding a real socket.
	shutdown func(ctx context.Context) error
	// newServer constructs the underlying HTTP server bound to addr and the
	// ServeMux that handlers are registered on. Defaults to a real http.Server
	// and ServeMux; tests may override via a functional option to supply a fake
	// server that does not bind a real port.
	newServer func(addr string) (httpServer, *http.ServeMux)
	// newHLSStream constructs a per-stream HLS playback object for the given
	// stream URL pair. Defaults to newHLSPlayStream pre-wired with this server's
	// load balancer and a fresh SPBHID; tests may override via a functional option.
	newHLSStream func(streamURL, fullURL string) *hlsPlayStream
	// newFlvTsConn constructs a per-request HTTP-FLV/TS connection bound to ctx.
	// Defaults to newHTTPFlvTsConnection pre-wired with this server's load
	// balancer; tests may override via a functional option.
	newFlvTsConn func(ctx context.Context) *httpFlvTsConnection
}

func NewHTTPStreamProxyServer(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer, gracefulQuitTimeout time.Duration, opts ...func(*httpStreamProxyServer)) HTTPStreamProxyServer {
	v := &httpStreamProxyServer{
		environment:         environment,
		loadBalancer:        loadBalancer,
		gracefulQuitTimeout: gracefulQuitTimeout,
	}

	// Default shutdown: delegate to the underlying http.Server. The closure
	// captures v rather than v.server so the dereference happens at call time,
	// after Run() has assigned v.server.
	v.shutdown = func(ctx context.Context) error {
		return v.server.Shutdown(ctx)
	}
	// Default newServer: a real http.Server and ServeMux pair.
	v.newServer = func(addr string) (httpServer, *http.ServeMux) {
		mux := http.NewServeMux()
		return &http.Server{Addr: addr, Handler: mux}, mux
	}
	// Default newHLSStream: a real hlsPlayStream wired with the server's load
	// balancer and a fresh SPBHID for this stream.
	v.newHLSStream = func(streamURL, fullURL string) *hlsPlayStream {
		return newHLSPlayStream(func(s *hlsPlayStream) {
			s.loadBalancer = v.loadBalancer
			s.SRSProxyBackendHLSID = logger.GenerateContextID()
			s.StreamURL, s.FullURL = streamURL, fullURL
		})
	}
	// Default newFlvTsConn: a real httpFlvTsConnection wired with the server's
	// load balancer and the given ctx.
	v.newFlvTsConn = func(ctx context.Context) *httpFlvTsConnection {
		return newHTTPFlvTsConnection(func(c *httpFlvTsConnection) {
			c.ctx = ctx
			c.loadBalancer = v.loadBalancer
		})
	}

	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *httpStreamProxyServer) Close() error {
	ctx, cancel := context.WithTimeout(context.Background(), v.gracefulQuitTimeout)
	defer cancel()
	v.shutdown(ctx)

	v.wg.Wait()
	return nil
}

func (v *httpStreamProxyServer) Run(ctx context.Context) error {
	// Parse address to listen.
	addr := v.environment.HttpServer()
	if !strings.Contains(addr, ":") {
		addr = ":" + addr
	}

	// Create server and handler.
	server, mux := v.newServer(addr)
	v.server = server
	logger.Debug(ctx, "HTTP Stream server listen at %v", addr)

	// Shutdown the server gracefully when quiting.
	go func() {
		ctxParent := ctx
		<-ctxParent.Done()

		ctx, cancel := context.WithTimeout(context.Background(), v.gracefulQuitTimeout)
		defer cancel()

		v.shutdown(ctx)
	}()

	// The basic version handler, also can be used as health check API.
	logger.Debug(ctx, "Handle /api/v1/versions by %v", addr)
	mux.HandleFunc("/api/v1/versions", func(w http.ResponseWriter, r *http.Request) {
		type Response struct {
			Code int    `json:"code"`
			PID  string `json:"pid"`
			Data struct {
				Major    int    `json:"major"`
				Minor    int    `json:"minor"`
				Revision int    `json:"revision"`
				Version  string `json:"version"`
			} `json:"data"`
		}

		res := Response{Code: 0, PID: fmt.Sprintf("%v", os.Getpid())}
		res.Data.Major = version.VersionMajor()
		res.Data.Minor = version.VersionMinor()
		res.Data.Revision = version.VersionRevision()
		res.Data.Version = version.Version()

		utils.ApiResponse(ctx, w, r, &res)
	})

	// The static web server, for the web pages.
	var staticServer http.Handler
	if staticFiles := v.environment.StaticFiles(); staticFiles != "" {
		if _, err := os.Stat(staticFiles); err != nil {
			return errors.Wrapf(err, "invalid static files %v", staticFiles)
		}

		staticServer = http.FileServer(http.Dir(staticFiles))
		logger.Debug(ctx, "Handle static files at %v", staticFiles)
	}

	// The default handler, for both static web server and streaming server.
	logger.Debug(ctx, "Handle / by %v", addr)
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		// For HLS streaming, we will proxy the request to the streaming server.
		if strings.HasSuffix(r.URL.Path, ".m3u8") {
			unifiedURL, fullURL := utils.ConvertURLToStreamURL(r)
			streamURL, err := utils.BuildStreamURL(unifiedURL)
			if err != nil {
				http.Error(w, fmt.Sprintf("build stream url by %v from %v", unifiedURL, fullURL), http.StatusBadRequest)
				return
			}

			stream, err := v.loadBalancer.LoadOrStoreHLS(ctx, streamURL, v.newHLSStream(streamURL, fullURL))
			if err != nil {
				http.Error(w, fmt.Sprintf("load or store hls %v", streamURL), http.StatusBadRequest)
				return
			}

			stream.Initialize(ctx).(*hlsPlayStream).ServeHTTP(w, r)
			return
		}

		// For HTTP streaming, we will proxy the request to the streaming server.
		if strings.HasSuffix(r.URL.Path, ".flv") ||
			strings.HasSuffix(r.URL.Path, ".ts") {
			// If SPBHID is specified, it must be a HLS stream client.
			if srsProxyBackendID := r.URL.Query().Get("spbhid"); srsProxyBackendID != "" {
				if stream, err := v.loadBalancer.LoadHLSBySPBHID(ctx, srsProxyBackendID); err != nil {
					http.Error(w, fmt.Sprintf("load stream by spbhid %v", srsProxyBackendID), http.StatusBadRequest)
				} else {
					stream.Initialize(ctx).(*hlsPlayStream).ServeHTTP(w, r)
				}
				return
			}

			// Use HTTP pseudo streaming to proxy the request.
			v.newFlvTsConn(ctx).ServeHTTP(w, r)
			return
		}

		// Serve by static server.
		if staticServer != nil {
			staticServer.ServeHTTP(w, r)
			return
		}

		http.NotFound(w, r)
	})

	// Run HTTP server.
	v.wg.Add(1)
	go func() {
		defer v.wg.Done()

		err := v.server.ListenAndServe()
		if err != nil {
			if err == http.ErrServerClosed {
				logger.Debug(ctx, "HTTP Stream server done")
			} else if ctx.Err() != nil {
				logger.Debug(ctx, "HTTP Stream server done with context canceled")
			} else {
				// TODO: If HTTP Stream server closed unexpectedly, we should notice the main loop to quit.
				logger.Warn(ctx, "HTTP Stream accept err %+v", err)
			}
		}
	}()

	return nil
}

// httpFlvTsConnection is an HTTP pseudo streaming connection, such as an HTTP-FLV or HTTP-TS
// connection. There is no state need to be sync between proxy servers.
//
// When we got an HTTP FLV or TS request, we will parse the stream URL from the HTTP request,
// then proxy to the corresponding backend server. All state is in the HTTP request, so this
// connection is stateless.
type httpFlvTsConnection struct {
	// The context for HTTP streaming.
	ctx context.Context
	// The load balancer for origin servers.
	loadBalancer lb.OriginLoadBalancer
	// buildBackendURL composes the backend HTTP URL for a request path. Defaults
	// to buildBackendHTTPURL; tests may override via a functional option.
	buildBackendURL func(ip string, port int, path string) string
}

func newHTTPFlvTsConnection(opts ...func(*httpFlvTsConnection)) *httpFlvTsConnection {
	v := &httpFlvTsConnection{
		buildBackendURL: buildBackendHTTPURL,
	}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *httpFlvTsConnection) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	defer r.Body.Close()
	ctx := logger.WithContext(v.ctx)

	if err := v.serve(ctx, w, r); err != nil {
		utils.ApiError(ctx, w, r, err)
	} else {
		logger.Debug(ctx, "HTTP client done")
	}
}

func (v *httpFlvTsConnection) serve(ctx context.Context, w http.ResponseWriter, r *http.Request) error {
	// Always allow CORS for all requests.
	if ok := utils.ApiCORS(ctx, w, r); ok {
		return nil
	}

	// Build the stream URL in vhost/app/stream schema.
	unifiedURL, fullURL := utils.ConvertURLToStreamURL(r)
	logger.Debug(ctx, "Got HTTP client from %v for %v", r.RemoteAddr, fullURL)

	streamURL, err := utils.BuildStreamURL(unifiedURL)
	if err != nil {
		return errors.Wrapf(err, "build stream url %v", unifiedURL)
	}

	// Pick a backend SRS server to proxy the RTMP stream.
	backend, err := v.loadBalancer.Pick(ctx, streamURL)
	if err != nil {
		return errors.Wrapf(err, "pick backend for %v", streamURL)
	}

	if err = v.serveByBackend(ctx, w, r, backend); err != nil {
		return errors.Wrapf(err, "serve %v with %v by backend %+v", fullURL, streamURL, backend)
	}

	return nil
}

func (v *httpFlvTsConnection) serveByBackend(ctx context.Context, w http.ResponseWriter, r *http.Request, backend *lb.OriginServer) error {
	// Parse HTTP port from backend.
	if len(backend.HTTP) == 0 {
		return errors.Errorf("no http stream server")
	}

	var httpPort int
	if iv, err := strconv.ParseInt(backend.HTTP[0], 10, 64); err != nil {
		return errors.Wrapf(err, "parse http port %v", backend.HTTP[0])
	} else {
		httpPort = int(iv)
	}

	// Connect to backend SRS server via HTTP client.
	backendURL := v.buildBackendURL(backend.IP, httpPort, r.URL.Path)
	req, err := http.NewRequestWithContext(ctx, r.Method, backendURL, nil)
	if err != nil {
		return errors.Wrapf(err, "create request to %v", backendURL)
	}

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return errors.Wrapf(err, "do request to %v", backendURL)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return errors.Errorf("proxy stream to %v failed, status=%v", backendURL, resp.Status)
	}

	// Copy end-to-end headers before committing the response. Headers changed
	// after WriteHeader are not sent by net/http.
	copyBackendResponseHeaders(w.Header(), resp.Header)
	w.WriteHeader(resp.StatusCode)

	logger.Debug(ctx, "HTTP start streaming")

	// Proxy the stream from backend to client.
	if _, err := io.Copy(w, resp.Body); err != nil {
		return errors.Wrapf(err, "copy stream to client, backend=%v", backendURL)
	}

	return nil
}

// hlsPlayStream is an HLS stream proxy, which represents the stream level object. This means multiple HLS
// clients will share this object, and they do not use the same ctx among proxy servers.
//
// Unlike the HTTP FLV or TS connection, HLS client may request the m3u8 or ts via different HTTP connections.
// Especially for requesting ts, we need to identify the stream URl or backend server for it. So we create
// the spbhid which can be seen as the hash of stream URL or backend server. The spbhid enable us to convert
// to the stream URL and then query the backend server to serve it.
type hlsPlayStream struct {
	// The context for HLS streaming.
	ctx context.Context
	// The load balancer for origin servers.
	loadBalancer lb.OriginLoadBalancer

	// The spbhid, used to identify the backend server.
	SRSProxyBackendHLSID string `json:"spbhid"`
	// The stream URL in vhost/app/stream schema.
	StreamURL string `json:"stream_url"`
	// The full request URL for HLS streaming
	FullURL string `json:"full_url"`
	// buildBackendURL composes the backend HTTP URL for a request path. Defaults
	// to buildBackendHTTPURL; tests may override via a functional option.
	buildBackendURL func(ip string, port int, path string) string `json:"-"`
}

func newHLSPlayStream(opts ...func(*hlsPlayStream)) *hlsPlayStream {
	v := &hlsPlayStream{
		buildBackendURL: buildBackendHTTPURL,
	}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *hlsPlayStream) Initialize(ctx context.Context) lb.HLSPlayStream {
	if v.ctx == nil {
		v.ctx = logger.WithContext(ctx)
	}
	return v
}

func (v *hlsPlayStream) GetSPBHID() string {
	return v.SRSProxyBackendHLSID
}

func (v *hlsPlayStream) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	defer r.Body.Close()

	if err := v.serve(v.ctx, w, r); err != nil {
		utils.ApiError(v.ctx, w, r, err)
	} else {
		logger.Debug(v.ctx, "HLS client %v for %v with %v done",
			v.SRSProxyBackendHLSID, v.StreamURL, r.URL.Path)
	}
}

func (v *hlsPlayStream) serve(ctx context.Context, w http.ResponseWriter, r *http.Request) error {
	ctx, streamURL, fullURL := v.ctx, v.StreamURL, v.FullURL

	// Always allow CORS for all requests.
	if ok := utils.ApiCORS(ctx, w, r); ok {
		return nil
	}

	// Pick a backend SRS server to proxy the RTMP stream.
	backend, err := v.loadBalancer.Pick(ctx, streamURL)
	if err != nil {
		return errors.Wrapf(err, "pick backend for %v", streamURL)
	}

	if err = v.serveByBackend(ctx, w, r, backend); err != nil {
		return errors.Wrapf(err, "serve %v with %v by backend %+v", fullURL, streamURL, backend)
	}

	return nil
}

func (v *hlsPlayStream) serveByBackend(ctx context.Context, w http.ResponseWriter, r *http.Request, backend *lb.OriginServer) error {
	// Parse HTTP port from backend.
	if len(backend.HTTP) == 0 {
		return errors.Errorf("no http server")
	}

	var httpPort int
	if iv, err := strconv.ParseInt(backend.HTTP[0], 10, 64); err != nil {
		return errors.Wrapf(err, "parse http port %v", backend.HTTP[0])
	} else {
		httpPort = int(iv)
	}

	// Connect to backend SRS server via HTTP client.
	backendURL := v.buildBackendURL(backend.IP, httpPort, r.URL.Path)
	if r.URL.RawQuery != "" {
		backendURL += "?" + r.URL.RawQuery
	}

	req, err := http.NewRequestWithContext(ctx, r.Method, backendURL, nil)
	if err != nil {
		return errors.Wrapf(err, "create request to %v", backendURL)
	}

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return errors.Errorf("do request to %v EOF", backendURL)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return errors.Errorf("proxy stream to %v failed, status=%v", backendURL, resp.Status)
	}

	// For TS file, directly copy it.
	if !strings.HasSuffix(r.URL.Path, ".m3u8") {
		copyBackendResponseHeaders(w.Header(), resp.Header)
		w.WriteHeader(resp.StatusCode)

		if _, err := io.Copy(w, resp.Body); err != nil {
			return errors.Wrapf(err, "copy stream to client, backend=%v", backendURL)
		}

		return nil
	}

	// Read all content of m3u8, append the stream ID to ts URL. Note that we only append stream ID to ts
	// URL, to identify the stream to specified backend server. The spbhid is the SRS Proxy Backend HLS ID.
	b, err := io.ReadAll(resp.Body)
	if err != nil {
		return errors.Wrapf(err, "read stream from %v", backendURL)
	}

	backendM3U8 := string(b)
	m3u8 := backendM3U8
	if strings.Contains(m3u8, ".ts?") {
		m3u8 = strings.ReplaceAll(m3u8, ".ts?", fmt.Sprintf(".ts?spbhid=%v&", v.SRSProxyBackendHLSID))
	} else {
		m3u8 = strings.ReplaceAll(m3u8, ".ts", fmt.Sprintf(".ts?spbhid=%v", v.SRSProxyBackendHLSID))
	}

	copyBackendResponseHeaders(w.Header(), resp.Header)
	if m3u8 != backendM3U8 {
		repairRewrittenResponseHeaders(w.Header(), len(m3u8))
	}
	w.WriteHeader(resp.StatusCode)

	if _, err := io.Copy(w, strings.NewReader(m3u8)); err != nil {
		return errors.Wrapf(err, "proxy m3u8 client to %v", backendURL)
	}

	return nil
}
