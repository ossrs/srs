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
	"net/url"
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
				v.ctx, v.StreamURL, v.FullURL = logger.WithContext(ctx), streamURL, fullURL
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
	// Whether has written response to client.
	written bool
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

	var backendClosedErr, clientClosedErr bool

	handleBackendErr := func(err error) {
		if isPeerClosedError(err) {
			if !backendClosedErr {
				backendClosedErr = true
				logger.Df(ctx, "HTTP backend peer closed")
			}
		} else {
			logger.Wf(ctx, "HTTP backend err %+v", err)
		}
	}

	handleClientErr := func(err error) {
		if isPeerClosedError(err) {
			if !clientClosedErr {
				clientClosedErr = true
				logger.Df(ctx, "HTTP client peer closed")
			}
		} else {
			logger.Wf(ctx, "HTTP client %v err %+v", r.RemoteAddr, err)
		}
	}

	handleErr := func(err error) {
		if perr, ok := err.(*RTMPProxyError); ok {
			if perr.isBackend {
				handleBackendErr(perr.err)
			} else {
				handleClientErr(perr.err)
			}
		} else {
			handleClientErr(err)
		}
	}

	if err := v.serve(ctx, w, r); err != nil {
		if merr, ok := err.(*RTMPMultipleError); ok {
			// If multiple errors, handle all of them.
			for _, err := range merr.errs {
				handleErr(err)
			}
		} else {
			// If single error, directly handle it.
			handleErr(err)
		}

		if !v.written {
			apiError(ctx, w, r, err)
		}
	} else {
		logger.Df(ctx, "HTTP client done")
	}
}

func (v *HTTPStreaming) serve(ctx context.Context, w http.ResponseWriter, r *http.Request) error {
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

	if err = v.serveByBackend(ctx, w, r, backend, streamURL); err != nil {
		extraMsg := fmt.Sprintf("serve %v by backend %+v", fullURL, backend)
		return wrapProxyError(err, extraMsg)
	}

	return nil
}

func (v *HTTPStreaming) serveByBackend(ctx context.Context, w http.ResponseWriter, r *http.Request, backend *SRSServer, streamURL string) error {
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

	// If any goroutine quit, cancel another one.
	parentCtx := ctx
	ctx, cancel := context.WithCancel(ctx)

	go func() {
		select {
		case <-ctx.Done():
		case <-r.Context().Done():
			// If client request cancelled, cancel the proxy goroutine.
			cancel()
		}
	}()

	// Connect to backend SRS server via HTTP client.
	backendURL := fmt.Sprintf("http://%v:%v%s", backend.IP, httpPort, r.URL.Path)
	req, err := http.NewRequestWithContext(ctx, "GET", backendURL, nil)
	if err != nil {
		return &RTMPProxyError{true, errors.Wrapf(err, "create request to %v", backendURL)}
	}

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		if urlErr, ok := err.(*url.Error); ok {
			if urlErr.Err == io.EOF {
				return &RTMPProxyError{true, errors.Errorf("do request to %v EOF", backendURL)}
			}
			if urlErr.Err == context.Canceled && r.Context().Err() != nil {
				return &RTMPProxyError{false, errors.Wrapf(io.EOF, "client closed")}
			}
		}
		return &RTMPProxyError{true, errors.Wrapf(err, "do request to %v", backendURL)}
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return &RTMPProxyError{true, errors.Errorf("proxy stream to %v failed, status=%v", backendURL, resp.Status)}
	}

	// Copy all headers from backend to client.
	w.WriteHeader(resp.StatusCode)
	for k, v := range resp.Header {
		for _, vv := range v {
			w.Header().Add(k, vv)
		}
	}

	v.written = true
	logger.Df(ctx, "HTTP start streaming")

	// For all proxy goroutines.
	var wg stdSync.WaitGroup
	defer wg.Wait()

	// Detect the client closed.
	wg.Add(1)
	var r0 error
	go func() {
		defer wg.Done()
		defer cancel()

		r0 = func() error {
			select {
			case <-ctx.Done():
				return nil
			case <-r.Context().Done():
				return &RTMPProxyError{false, errors.Wrapf(io.EOF, "client closed")}
			}
		}()
	}()

	// Copy all data from backend to client.
	wg.Add(1)
	var r1 error
	go func() {
		defer wg.Done()
		defer cancel()

		r1 = func() error {
			buf := make([]byte, 4096)
			for {
				n, err := resp.Body.Read(buf)
				if err != nil {
					return &RTMPProxyError{true, errors.Wrapf(err, "read stream from %v", backendURL)}
				}

				if _, err := w.Write(buf[:n]); err != nil {
					return &RTMPProxyError{false, errors.Wrapf(err, "write stream client")}
				}
			}
		}()
	}()

	// Wait until all goroutine quit.
	wg.Wait()

	// Reset the error if caused by another goroutine.
	if errors.Cause(r0) == context.Canceled && parentCtx.Err() == nil {
		r0 = nil
	}
	if errors.Cause(r1) == context.Canceled && parentCtx.Err() == nil {
		r1 = nil
	}

	return NewRTMPMultipleError(r0, r1, parentCtx.Err())
}

type HLSStreaming struct {
	// The context for HLS streaming.
	ctx context.Context

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
		return nil
	}

	// Pick a backend SRS server to proxy the RTMP stream.
	backend, err := srsLoadBalancer.Pick(ctx, streamURL)
	if err != nil {
		return errors.Wrapf(err, "pick backend for %v", streamURL)
	}

	if err = v.serveByBackend(ctx, w, r, backend, streamURL); err != nil {
		extraMsg := fmt.Sprintf("serve %v by backend %+v", fullURL, backend)
		return wrapProxyError(err, extraMsg)
	}

	return nil
}

func (v *HLSStreaming) serveByBackend(ctx context.Context, w http.ResponseWriter, r *http.Request, backend *SRSServer, streamURL string) error {
	// Parse HTTP port from backend.
	if len(backend.HTTP) == 0 {
		return errors.Errorf("no rtmp server %+v for %v", backend, streamURL)
	}

	var httpPort int
	if iv, err := strconv.ParseInt(backend.HTTP[0], 10, 64); err != nil {
		return errors.Wrapf(err, "parse backend %+v rtmp port %v", backend, backend.HTTP[0])
	} else {
		httpPort = int(iv)
	}

	// Connect to backend SRS server via HTTP client.
	backendURL := fmt.Sprintf("http://%v:%v%s", backend.IP, httpPort, r.URL.Path)
	if r.URL.RawQuery != "" {
		backendURL += "?" + r.URL.RawQuery
	}

	req, err := http.NewRequestWithContext(ctx, "GET", backendURL, nil)
	if err != nil {
		return &RTMPProxyError{true, errors.Wrapf(err, "create request to %v", backendURL)}
	}

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		if urlErr, ok := err.(*url.Error); ok {
			if urlErr.Err == io.EOF {
				return &RTMPProxyError{true, errors.Errorf("do request to %v EOF", backendURL)}
			}
			if urlErr.Err == context.Canceled && r.Context().Err() != nil {
				return &RTMPProxyError{false, errors.Wrapf(io.EOF, "client closed")}
			}
		}
		return &RTMPProxyError{true, errors.Wrapf(err, "do request to %v", backendURL)}
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return &RTMPProxyError{true, errors.Errorf("proxy stream to %v failed, status=%v", backendURL, resp.Status)}
	}

	// Copy all headers from backend to client.
	w.WriteHeader(resp.StatusCode)
	for k, v := range resp.Header {
		for _, vv := range v {
			w.Header().Add(k, vv)
		}
	}

	// Read all content of m3u8, append the stream ID to ts URL. Note that we only append stream ID to ts
	// URL, to identify the stream to specified backend server. The spbhid is the SRS Proxy Backend HLS ID.
	if strings.HasSuffix(r.URL.Path, ".m3u8") {
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
			return errors.Wrapf(err, "write stream client")
		}
	} else {
		// For TS file, directly copy it.
		if _, err := io.Copy(w, resp.Body); err != nil {
			return errors.Wrapf(err, "write stream client")
		}
	}

	return nil
}
