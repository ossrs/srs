// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

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

	"srs-proxy/errors"
	"srs-proxy/logger"
)

type httpServer struct {
	// The underlayer HTTP server.
	server *http.Server
	// The gracefully quit timeout, wait server to quit.
	gracefulQuitTimeout time.Duration
	// The wait group for all goroutines.
	wg stdSync.WaitGroup
}

func NewHttpServer(opts ...func(*httpServer)) *httpServer {
	v := &httpServer{}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *httpServer) Close() error {
	ctx, cancel := context.WithTimeout(context.Background(), v.gracefulQuitTimeout)
	defer cancel()
	v.server.Shutdown(ctx)

	v.wg.Wait()
	return nil
}

func (v *httpServer) Run(ctx context.Context) error {
	// Parse address to listen.
	addr := envHttpServer()
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
		res.Data.Major = VersionMajor()
		res.Data.Minor = VersionMinor()
		res.Data.Revision = VersionRevision()
		res.Data.Version = Version()

		apiResponse(ctx, w, r, &res)
	})

	// The default handler, for both static web server and streaming server.
	logger.Df(ctx, "Handle / by %v", addr)
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		// For HLS streaming, we will proxy the request to the streaming server.
		if strings.HasSuffix(r.URL.Path, ".m3u8") {
			unifiedURL, fullURL := convertURLToStreamURL(r)
			streamURL, err := buildStreamURL(unifiedURL)
			if err != nil {
				http.Error(w, fmt.Sprintf("build stream url by %v from %v", unifiedURL, fullURL), http.StatusBadRequest)
				return
			}

			stream, _ := srsLoadBalancer.LoadOrStoreHLS(ctx, streamURL, NewHLSStreaming(func(v *HLSStreaming) {
				v.SRSProxyBackendHLSID = logger.GenerateContextID()
				v.StreamURL, v.FullURL = streamURL, fullURL
				v.BuildContext(ctx)
			}))

			stream.ServeHTTP(w, r)
			return
		}

		// For HTTP streaming, we will proxy the request to the streaming server.
		if strings.HasSuffix(r.URL.Path, ".flv") ||
			strings.HasSuffix(r.URL.Path, ".ts") {
			// If SPBHID is specified, it must be a HLS stream client.
			if srsProxyBackendID := r.URL.Query().Get("spbhid"); srsProxyBackendID != "" {
				if stream, err := srsLoadBalancer.LoadHLSBySPBHID(ctx, srsProxyBackendID); err != nil {
					http.Error(w, fmt.Sprintf("load stream by spbhid %v", srsProxyBackendID), http.StatusBadRequest)
				} else {
					stream.ServeHTTP(w, r)
				}
				return
			}

			// Use HTTP pseudo streaming to proxy the request.
			NewHTTPStreaming(func(streaming *HTTPStreaming) {
				streaming.ctx = ctx
			}).ServeHTTP(w, r)
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
			if ctx.Err() != context.Canceled {
				// TODO: If HTTP Stream server closed unexpectedly, we should notice the main loop to quit.
				logger.Wf(ctx, "HTTP Stream accept err %+v", err)
			} else {
				logger.Df(ctx, "HTTP Stream server done")
			}
		}
	}()

	return nil
}

type HTTPStreaming struct {
	// The context for HTTP streaming.
	ctx context.Context
}

func NewHTTPStreaming(opts ...func(streaming *HTTPStreaming)) *HTTPStreaming {
	v := &HTTPStreaming{}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *HTTPStreaming) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	defer r.Body.Close()
	ctx := logger.WithContext(v.ctx)

	if err := v.serve(ctx, w, r); err != nil {
		apiError(ctx, w, r, err)
	} else {
		logger.Df(ctx, "HTTP client done")
	}
}

func (v *HTTPStreaming) serve(ctx context.Context, w http.ResponseWriter, r *http.Request) error {
	// Always allow CORS for all requests.
	if ok := apiCORS(ctx, w, r); ok {
		return nil
	}

	// Build the stream URL in vhost/app/stream schema.
	unifiedURL, fullURL := convertURLToStreamURL(r)
	logger.Df(ctx, "Got HTTP client from %v for %v", r.RemoteAddr, fullURL)

	streamURL, err := buildStreamURL(unifiedURL)
	if err != nil {
		return errors.Wrapf(err, "build stream url %v", unifiedURL)
	}

	// Pick a backend SRS server to proxy the RTMP stream.
	backend, err := srsLoadBalancer.Pick(ctx, streamURL)
	if err != nil {
		return errors.Wrapf(err, "pick backend for %v", streamURL)
	}

	if err = v.serveByBackend(ctx, w, r, backend); err != nil {
		return errors.Wrapf(err, "serve %v with %v by backend %+v", fullURL, streamURL, backend)
	}

	return nil
}

func (v *HTTPStreaming) serveByBackend(ctx context.Context, w http.ResponseWriter, r *http.Request, backend *SRSServer) error {
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

type HLSStreaming struct {
	// The context for HLS streaming.
	ctx context.Context
	// The context ID for recovering the context.
	ContextID string `json:"cid"`

	// The spbhid, used to identify the backend server.
	SRSProxyBackendHLSID string `json:"spbhid"`
	// The stream URL in vhost/app/stream schema.
	StreamURL string `json:"stream_url"`
	// The full request URL for HLS streaming
	FullURL string `json:"full_url"`
}

func NewHLSStreaming(opts ...func(streaming *HLSStreaming)) *HLSStreaming {
	v := &HLSStreaming{}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *HLSStreaming) BuildContext(ctx context.Context) {
	if v.ContextID == "" {
		v.ContextID = logger.GenerateContextID()
	}
	v.ctx = logger.WithContextID(ctx, v.ContextID)
}

func (v *HLSStreaming) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	defer r.Body.Close()

	if err := v.serve(v.ctx, w, r); err != nil {
		apiError(v.ctx, w, r, err)
	} else {
		logger.Df(v.ctx, "HLS client %v for %v with %v done",
			v.SRSProxyBackendHLSID, v.StreamURL, r.URL.Path)
	}
}

func (v *HLSStreaming) serve(ctx context.Context, w http.ResponseWriter, r *http.Request) error {
	ctx, streamURL, fullURL := v.ctx, v.StreamURL, v.FullURL

	// Always allow CORS for all requests.
	if ok := apiCORS(ctx, w, r); ok {
		return nil
	}

	// Pick a backend SRS server to proxy the RTMP stream.
	backend, err := srsLoadBalancer.Pick(ctx, streamURL)
	if err != nil {
		return errors.Wrapf(err, "pick backend for %v", streamURL)
	}

	if err = v.serveByBackend(ctx, w, r, backend); err != nil {
		return errors.Wrapf(err, "serve %v with %v by backend %+v", fullURL, streamURL, backend)
	}

	return nil
}

func (v *HLSStreaming) serveByBackend(ctx context.Context, w http.ResponseWriter, r *http.Request, backend *SRSServer) error {
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
