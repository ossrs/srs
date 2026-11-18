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

type HTTPStreamProxyServer interface {
	Run(ctx context.Context) error
	Close() error
}

type httpServer interface {
	ListenAndServe() error
	Shutdown(ctx context.Context) error
}

func buildBackendHTTPURL(ip string, port int, path string) string {
	return fmt.Sprintf("http://%v:%v%s", ip, port, path)
}

// The Connection header can name more hop-by-hop fields.
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

// These headers describe the backend's body, not the rewritten one.
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
	environment         env.ProxyEnvironment
	loadBalancer        lb.OriginLoadBalancer
	server              httpServer
	gracefulQuitTimeout time.Duration
	wg                  stdSync.WaitGroup
	// Tests replace these funcs; a fake newServer binds no real port.
	shutdown     func(ctx context.Context) error
	newServer    func(addr string) (httpServer, *http.ServeMux)
	newHLSStream func(streamURL, fullURL string) *hlsPlayStream
	newFlvTsConn func(ctx context.Context) *httpFlvTsConnection
}

func NewHTTPStreamProxyServer(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer, gracefulQuitTimeout time.Duration, opts ...func(*httpStreamProxyServer)) HTTPStreamProxyServer {
	v := &httpStreamProxyServer{
		environment:         environment,
		loadBalancer:        loadBalancer,
		gracefulQuitTimeout: gracefulQuitTimeout,
	}

	// Reads v.server at call time, since Run sets it later.
	v.shutdown = func(ctx context.Context) error {
		return v.server.Shutdown(ctx)
	}
	v.newServer = func(addr string) (httpServer, *http.ServeMux) {
		mux := http.NewServeMux()
		return &http.Server{Addr: addr, Handler: mux}, mux
	}
	v.newHLSStream = func(streamURL, fullURL string) *hlsPlayStream {
		return newHLSPlayStream(func(s *hlsPlayStream) {
			s.loadBalancer = v.loadBalancer
			s.SRSProxyBackendHLSID = logger.GenerateContextID()
			s.StreamURL, s.FullURL = streamURL, fullURL
		})
	}
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
	addr := v.environment.HttpServer()
	if !strings.Contains(addr, ":") {
		addr = ":" + addr
	}

	server, mux := v.newServer(addr)
	v.server = server
	logger.Debug(ctx, "HTTP Stream server listen at %v", addr)

	go func() {
		ctxParent := ctx
		<-ctxParent.Done()

		ctx, cancel := context.WithTimeout(context.Background(), v.gracefulQuitTimeout)
		defer cancel()

		v.shutdown(ctx)
	}()

	// Also used as a health check.
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

	var staticServer http.Handler
	if staticFiles := v.environment.StaticFiles(); staticFiles != "" {
		if _, err := os.Stat(staticFiles); err != nil {
			return errors.Wrapf(err, "invalid static files %v", staticFiles)
		}

		staticServer = http.FileServer(http.Dir(staticFiles))
		logger.Debug(ctx, "Handle static files at %v", staticFiles)
	}

	logger.Debug(ctx, "Handle / by %v", addr)
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
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

		if strings.HasSuffix(r.URL.Path, ".flv") ||
			strings.HasSuffix(r.URL.Path, ".ts") {
			// Only the playlists the proxy rewrites give ts URLs a spbhid.
			if srsProxyBackendID := r.URL.Query().Get("spbhid"); srsProxyBackendID != "" {
				if stream, err := v.loadBalancer.LoadHLSBySPBHID(ctx, srsProxyBackendID); err != nil {
					http.Error(w, fmt.Sprintf("load stream by spbhid %v", srsProxyBackendID), http.StatusBadRequest)
				} else {
					stream.Initialize(ctx).(*hlsPlayStream).ServeHTTP(w, r)
				}
				return
			}

			v.newFlvTsConn(ctx).ServeHTTP(w, r)
			return
		}

		if staticServer != nil {
			staticServer.ServeHTTP(w, r)
			return
		}

		http.NotFound(w, r)
	})

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

// The request holds all the state, so unlike HLS, nothing is shared between proxy servers.
type httpFlvTsConnection struct {
	ctx          context.Context
	loadBalancer lb.OriginLoadBalancer
	// Tests replace it.
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
	if ok := utils.ApiCORS(ctx, w, r); ok {
		return nil
	}

	unifiedURL, fullURL := utils.ConvertURLToStreamURL(r)
	logger.Debug(ctx, "Got HTTP client from %v for %v", r.RemoteAddr, fullURL)

	streamURL, err := utils.BuildStreamURL(unifiedURL)
	if err != nil {
		return errors.Wrapf(err, "build stream url %v", unifiedURL)
	}

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
	if len(backend.HTTP) == 0 {
		return errors.Errorf("no http stream server")
	}

	var httpPort int
	if iv, err := strconv.ParseInt(backend.HTTP[0], 10, 64); err != nil {
		return errors.Wrapf(err, "parse http port %v", backend.HTTP[0])
	} else {
		httpPort = int(iv)
	}

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

	// An origin error, such as a 404 for a stream that is not published, is
	// relayed with its status, like a stream.
	if resp.StatusCode != http.StatusOK {
		logger.Warn(ctx, "proxy stream to %v answered status=%v", backendURL, resp.Status)
	}

	// net/http does not send headers changed after WriteHeader.
	copyBackendResponseHeaders(w.Header(), resp.Header)
	w.WriteHeader(resp.StatusCode)

	logger.Debug(ctx, "HTTP start streaming")

	if _, err := io.Copy(w, resp.Body); err != nil {
		return errors.Wrapf(err, "copy stream to client, backend=%v", backendURL)
	}

	return nil
}

// One per stream, shared by all its HLS clients. A player fetches the m3u8 and the ts files over
// separate HTTP connections, so the ts URLs carry the spbhid to find the stream again.
type hlsPlayStream struct {
	ctx          context.Context
	loadBalancer lb.OriginLoadBalancer

	SRSProxyBackendHLSID string `json:"spbhid"`
	StreamURL            string `json:"stream_url"`
	FullURL              string `json:"full_url"`
	// Tests replace it.
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

	if ok := utils.ApiCORS(ctx, w, r); ok {
		return nil
	}

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
	if len(backend.HTTP) == 0 {
		return errors.Errorf("no http server")
	}

	var httpPort int
	if iv, err := strconv.ParseInt(backend.HTTP[0], 10, 64); err != nil {
		return errors.Wrapf(err, "parse http port %v", backend.HTTP[0])
	} else {
		httpPort = int(iv)
	}

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

	// An origin error, such as a 404 for a playlist SRS has not written yet, is
	// relayed with its status, so a player can tell a stream that is not ready
	// from a broken proxy.
	if resp.StatusCode != http.StatusOK {
		logger.Warn(ctx, "proxy stream to %v answered status=%v", backendURL, resp.Status)
	}

	if resp.StatusCode != http.StatusOK || !strings.HasSuffix(r.URL.Path, ".m3u8") {
		copyBackendResponseHeaders(w.Header(), resp.Header)
		w.WriteHeader(resp.StatusCode)

		if _, err := io.Copy(w, resp.Body); err != nil {
			return errors.Wrapf(err, "copy stream to client, backend=%v", backendURL)
		}

		return nil
	}

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
