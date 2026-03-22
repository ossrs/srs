// Copyright (c) 2025 Winlin
//
// SPDX-License-Identifier: MIT
package protocol

import (
	"context"
	"fmt"
	"io"
	"io/ioutil"
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

// srsHTTPStreamServer is the proxy server for SRS HTTP stream server, for HTTP-FLV, HTTP-TS,
// HLS, etc. The proxy server will figure out which SRS origin server to proxy to, then proxy
// the request to the origin server.
type srsHTTPStreamServer struct {
	// The environment interface.
	environment env.Environment
	// The underlayer HTTP server.
	server *http.Server
	// The gracefully quit timeout, wait server to quit.
	gracefulQuitTimeout time.Duration
	// The wait group for all goroutines.
	wg stdSync.WaitGroup
}

func NewSRSHTTPStreamServer(environment env.Environment, gracefulQuitTimeout time.Duration) *srsHTTPStreamServer {
	v := &srsHTTPStreamServer{
		environment:         environment,
		gracefulQuitTimeout: gracefulQuitTimeout,
	}
	return v
}

func (v *srsHTTPStreamServer) Close() error {
	ctx, cancel := context.WithTimeout(context.Background(), v.gracefulQuitTimeout)
	defer cancel()
	v.server.Shutdown(ctx)

	v.wg.Wait()
	return nil
}

func (v *srsHTTPStreamServer) Run(ctx context.Context) error {
	// Parse address to listen.
	addr := v.environment.HttpServer()
	if !strings.Contains(addr, ":") {
		addr = ":" + addr
	}

	// Create server and handler.
	mux := http.NewServeMux()
	v.server = &http.Server{Addr: addr, Handler: mux}
	logger.Df(ctx, "HTTP Stream server listen at %v", addr)

	// Shutdown the server gracefully when quiting.
	go func() {
		ctxParent := ctx
		<-ctxParent.Done()

		ctx, cancel := context.WithTimeout(context.Background(), v.gracefulQuitTimeout)
		defer cancel()

		v.server.Shutdown(ctx)
	}()

	// The basic version handler, also can be used as health check API.
	logger.Df(ctx, "Handle /api/v1/versions by %v", addr)
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
		logger.Df(ctx, "Handle static files at %v", staticFiles)
	}

	// The default handler, for both static web server and streaming server.
	logger.Df(ctx, "Handle / by %v", addr)
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		// For HLS streaming, we will proxy the request to the streaming server.
		if strings.HasSuffix(r.URL.Path, ".m3u8") {
			unifiedURL, fullURL := utils.ConvertURLToStreamURL(r)
			streamURL, err := utils.BuildStreamURL(unifiedURL)
			if err != nil {
				http.Error(w, fmt.Sprintf("build stream url by %v from %v", unifiedURL, fullURL), http.StatusBadRequest)
				return
			}

			stream, _ := lb.SrsLoadBalancer.LoadOrStoreHLS(ctx, streamURL, NewHLSPlayStream(func(s *HLSPlayStream) {
				s.SRSProxyBackendHLSID = logger.GenerateContextID()
				s.StreamURL, s.FullURL = streamURL, fullURL
			}))

			stream.Initialize(ctx).(*HLSPlayStream).ServeHTTP(w, r)
			return
		}

		// For HTTP streaming, we will proxy the request to the streaming server.
		if strings.HasSuffix(r.URL.Path, ".flv") ||
			strings.HasSuffix(r.URL.Path, ".ts") {
			// If SPBHID is specified, it must be a HLS stream client.
			if srsProxyBackendID := r.URL.Query().Get("spbhid"); srsProxyBackendID != "" {
				if stream, err := lb.SrsLoadBalancer.LoadHLSBySPBHID(ctx, srsProxyBackendID); err != nil {
					http.Error(w, fmt.Sprintf("load stream by spbhid %v", srsProxyBackendID), http.StatusBadRequest)
				} else {
					stream.Initialize(ctx).(*HLSPlayStream).ServeHTTP(w, r)
				}
				return
			}

			// Use HTTP pseudo streaming to proxy the request.
			NewHTTPFlvTsConnection(func(c *HTTPFlvTsConnection) {
				c.ctx = ctx
			}).ServeHTTP(w, r)
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
				logger.Df(ctx, "HTTP Stream server done")
			} else if ctx.Err() != nil {
				logger.Df(ctx, "HTTP Stream server done with context canceled")
			} else {
				// TODO: If HTTP Stream server closed unexpectedly, we should notice the main loop to quit.
				logger.Wf(ctx, "HTTP Stream accept err %+v", err)
			}
		}
	}()

	return nil
}

// HTTPFlvTsConnection is an HTTP pseudo streaming connection, such as an HTTP-FLV or HTTP-TS
// connection. There is no state need to be sync between proxy servers.
//
// When we got an HTTP FLV or TS request, we will parse the stream URL from the HTTP request,
// then proxy to the corresponding backend server. All state is in the HTTP request, so this
// connection is stateless.
type HTTPFlvTsConnection struct {
	// The context for HTTP streaming.
	ctx context.Context
}

func NewHTTPFlvTsConnection(opts ...func(*HTTPFlvTsConnection)) *HTTPFlvTsConnection {
	v := &HTTPFlvTsConnection{}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *HTTPFlvTsConnection) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	defer r.Body.Close()
	ctx := logger.WithContext(v.ctx)

	if err := v.serve(ctx, w, r); err != nil {
		utils.ApiError(ctx, w, r, err)
	} else {
		logger.Df(ctx, "HTTP client done")
	}
}

func (v *HTTPFlvTsConnection) serve(ctx context.Context, w http.ResponseWriter, r *http.Request) error {
	// Always allow CORS for all requests.
	if ok := utils.ApiCORS(ctx, w, r); ok {
		return nil
	}

	// Build the stream URL in vhost/app/stream schema.
	unifiedURL, fullURL := utils.ConvertURLToStreamURL(r)
	logger.Df(ctx, "Got HTTP client from %v for %v", r.RemoteAddr, fullURL)

	streamURL, err := utils.BuildStreamURL(unifiedURL)
	if err != nil {
		return errors.Wrapf(err, "build stream url %v", unifiedURL)
	}

	// Pick a backend SRS server to proxy the RTMP stream.
	backend, err := lb.SrsLoadBalancer.Pick(ctx, streamURL)
	if err != nil {
		return errors.Wrapf(err, "pick backend for %v", streamURL)
	}

	if err = v.serveByBackend(ctx, w, r, backend); err != nil {
		return errors.Wrapf(err, "serve %v with %v by backend %+v", fullURL, streamURL, backend)
	}

	return nil
}

func (v *HTTPFlvTsConnection) serveByBackend(ctx context.Context, w http.ResponseWriter, r *http.Request, backend *lb.SRSServer) error {
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
	backendURL := fmt.Sprintf("http://%v:%v%s", backend.IP, httpPort, r.URL.Path)
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

	// Copy all headers from backend to client.
	w.WriteHeader(resp.StatusCode)
	for k, v := range resp.Header {
		for _, vv := range v {
			w.Header().Add(k, vv)
		}
	}

	logger.Df(ctx, "HTTP start streaming")

	// Proxy the stream from backend to client.
	if _, err := io.Copy(w, resp.Body); err != nil {
		return errors.Wrapf(err, "copy stream to client, backend=%v", backendURL)
	}

	return nil
}

// HLSPlayStream is an HLS stream proxy, which represents the stream level object. This means multiple HLS
// clients will share this object, and they do not use the same ctx among proxy servers.
//
// Unlike the HTTP FLV or TS connection, HLS client may request the m3u8 or ts via different HTTP connections.
// Especially for requesting ts, we need to identify the stream URl or backend server for it. So we create
// the spbhid which can be seen as the hash of stream URL or backend server. The spbhid enable us to convert
// to the stream URL and then query the backend server to serve it.
type HLSPlayStream struct {
	// The context for HLS streaming.
	ctx context.Context

	// The spbhid, used to identify the backend server.
	SRSProxyBackendHLSID string `json:"spbhid"`
	// The stream URL in vhost/app/stream schema.
	StreamURL string `json:"stream_url"`
	// The full request URL for HLS streaming
	FullURL string `json:"full_url"`
}

func NewHLSPlayStream(opts ...func(*HLSPlayStream)) *HLSPlayStream {
	v := &HLSPlayStream{}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *HLSPlayStream) Initialize(ctx context.Context) lb.HLSPlayStream {
	if v.ctx == nil {
		v.ctx = logger.WithContext(ctx)
	}
	return v
}

func (v *HLSPlayStream) GetSPBHID() string {
	return v.SRSProxyBackendHLSID
}

func (v *HLSPlayStream) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	defer r.Body.Close()

	if err := v.serve(v.ctx, w, r); err != nil {
		utils.ApiError(v.ctx, w, r, err)
	} else {
		logger.Df(v.ctx, "HLS client %v for %v with %v done",
			v.SRSProxyBackendHLSID, v.StreamURL, r.URL.Path)
	}
}

func (v *HLSPlayStream) serve(ctx context.Context, w http.ResponseWriter, r *http.Request) error {
	ctx, streamURL, fullURL := v.ctx, v.StreamURL, v.FullURL

	// Always allow CORS for all requests.
	if ok := utils.ApiCORS(ctx, w, r); ok {
		return nil
	}

	// Pick a backend SRS server to proxy the RTMP stream.
	backend, err := lb.SrsLoadBalancer.Pick(ctx, streamURL)
	if err != nil {
		return errors.Wrapf(err, "pick backend for %v", streamURL)
	}

	if err = v.serveByBackend(ctx, w, r, backend); err != nil {
		return errors.Wrapf(err, "serve %v with %v by backend %+v", fullURL, streamURL, backend)
	}

	return nil
}

func (v *HLSPlayStream) serveByBackend(ctx context.Context, w http.ResponseWriter, r *http.Request, backend *lb.SRSServer) error {
	// Parse HTTP port from backend.
	if len(backend.HTTP) == 0 {
		return errors.Errorf("no rtmp server")
	}

	var httpPort int
	if iv, err := strconv.ParseInt(backend.HTTP[0], 10, 64); err != nil {
		return errors.Wrapf(err, "parse http port %v", backend.HTTP[0])
	} else {
		httpPort = int(iv)
	}

	// Connect to backend SRS server via HTTP client.
	backendURL := fmt.Sprintf("http://%v:%v%s", backend.IP, httpPort, r.URL.Path)
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

	// Copy all headers from backend to client.
	w.WriteHeader(resp.StatusCode)
	for k, v := range resp.Header {
		for _, vv := range v {
			w.Header().Add(k, vv)
		}
	}

	// For TS file, directly copy it.
	if !strings.HasSuffix(r.URL.Path, ".m3u8") {
		if _, err := io.Copy(w, resp.Body); err != nil {
			return errors.Wrapf(err, "copy stream to client, backend=%v", backendURL)
		}

		return nil
	}

	// Read all content of m3u8, append the stream ID to ts URL. Note that we only append stream ID to ts
	// URL, to identify the stream to specified backend server. The spbhid is the SRS Proxy Backend HLS ID.
	b, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return errors.Wrapf(err, "read stream from %v", backendURL)
	}

	m3u8 := string(b)
	if strings.Contains(m3u8, ".ts?") {
		m3u8 = strings.ReplaceAll(m3u8, ".ts?", fmt.Sprintf(".ts?spbhid=%v&&", v.SRSProxyBackendHLSID))
	} else {
		m3u8 = strings.ReplaceAll(m3u8, ".ts", fmt.Sprintf(".ts?spbhid=%v", v.SRSProxyBackendHLSID))
	}

	if _, err := io.Copy(w, strings.NewReader(m3u8)); err != nil {
		return errors.Wrapf(err, "proxy m3u8 client to %v", backendURL)
	}

	return nil
}
