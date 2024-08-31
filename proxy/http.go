// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"fmt"
	"net"
	"net/http"
	"os"
	"path"
	"srs-proxy/errors"
	"strconv"
	"strings"
	"sync"
	"time"

	"srs-proxy/logger"
)

type httpServer struct {
	// The underlayer HTTP server.
	server *http.Server
	// The gracefully quit timeout, wait server to quit.
	gracefulQuitTimeout time.Duration
	// The wait group for all goroutines.
	wg sync.WaitGroup
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
		// For HTTP streaming, we will proxy the request to the streaming server.
		if strings.HasSuffix(r.URL.Path, ".flv") ||
			strings.HasSuffix(r.URL.Path, ".ts") {
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

	if err := v.serve(ctx, w, r); err != nil {
		if perr, ok := err.(*RTMPProxyError); ok {
			if perr.isBackend {
				handleBackendErr(perr.err)
			} else {
				handleClientErr(perr.err)
			}
		} else {
			handleClientErr(err)
		}

		if !v.written {
			apiError(ctx, w, r, err)
		}
	}
}

func (v *HTTPStreaming) serve(ctx context.Context, w http.ResponseWriter, r *http.Request) error {
	// Build the stream URL in vhost/app/stream schema.
	var requestURL, originalURL string
	if true {
		scheme := "http"
		if r.TLS != nil {
			scheme = "https"
		}

		hostname, _, err := net.SplitHostPort(r.Host)
		if err != nil {
			return errors.Wrapf(err, "split host %v", r.Host)
		}

		streamExt := path.Ext(r.URL.Path)
		streamName := strings.TrimSuffix(r.URL.Path, streamExt)
		requestURL = fmt.Sprintf("%v://%v%v", scheme, hostname, streamName)
		originalURL = fmt.Sprintf("%v%v", requestURL, streamExt)
		logger.Df(ctx, "Got HTTP client from %v for %v", r.RemoteAddr, originalURL)
	}

	streamURL, err := buildStreamURL(requestURL)
	if err != nil {
		return errors.Wrapf(err, "build stream url %v", requestURL)
	}

	// Pick a backend SRS server to proxy the RTMP stream.
	backend, err := srsLoadBalancer.Pick(ctx, streamURL)
	if err != nil {
		return errors.Wrapf(err, "pick backend for %v", streamURL)
	}

	if err = v.serveByBackend(ctx, w, r, backend, streamURL); err != nil {
		wrappedErr := errors.Wrapf(err, "serve %v by backend %+v", originalURL, backend)
		if perr, ok := err.(*RTMPProxyError); ok {
			return &RTMPProxyError{perr.isBackend, wrappedErr}
		}
		return wrappedErr
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

	// Connect to backend SRS server via HTTP client.
	backendURL := fmt.Sprintf("http://%v:%v%s", backend.IP, httpPort, r.URL.Path)
	req, err := http.NewRequestWithContext(ctx, "GET", backendURL, nil)
	if err != nil {
		return &RTMPProxyError{true, errors.Wrapf(err, "create request to %v", backendURL)}
	}

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
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

	// Copy all data from backend to client.
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

	return nil
}
