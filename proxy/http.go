// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"fmt"
	"io"
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
	if err := v.serve(v.ctx, w, r); err != nil {
		apiError(v.ctx, w, r, err)
	}
}

func (v *HTTPStreaming) serve(ctx context.Context, w http.ResponseWriter, r *http.Request) error {
	// Build the stream URL in vhost/app/stream schema.
	scheme := "http"
	if r.TLS != nil {
		scheme = "https"
	}
	streamName := strings.TrimSuffix(r.URL.Path, path.Ext(r.URL.Path))
	streamURL, err := buildStreamURL(fmt.Sprintf("%v://%v%v", scheme, r.URL.Hostname(), streamName))
	if err != nil {
		return errors.Wrapf(err, "build stream url scheme=%v, hostname=%v, stream=%v",
			scheme, r.URL.Hostname(), streamName)
	}

	// Pick a backend SRS server to proxy the RTMP stream.
	backend, err := srsLoadBalancer.Pick(ctx, streamURL)
	if err != nil {
		return errors.Wrapf(err, "pick backend for %v", streamURL)
	}

	if err = v.serveByBackend(ctx, w, r, backend, streamURL); err != nil {
		return errors.Wrapf(err, "serve %v by backend %+v for stream %v",
			r.URL.String(), backend, streamURL)
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
		return errors.Wrapf(err, "create request to %v", backendURL)
	}

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return errors.Wrapf(err, "proxy stream to %v", backendURL)
	}

	if resp.StatusCode != http.StatusOK {
		return errors.Errorf("proxy stream to %v failed, status=%v", backendURL, resp.Status)
	}

	// Copy all data from backend to client.
	if _, err := io.Copy(w, resp.Body); err != nil {
		return errors.Wrapf(err, "copy stream from %v", backendURL)
	}

	return nil
}
