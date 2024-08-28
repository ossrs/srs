// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"net/http"
	"srs-proxy/logger"
	"strings"
	"sync"
	"time"
)

type httpAPI struct {
	// The underlayer HTTP server.
	server *http.Server
	// The gracefully quit timeout, wait server to quit.
	gracefulQuitTimeout time.Duration
	// The wait group for all goroutines.
	wg sync.WaitGroup
}

func NewHttpAPI(opts ...func(*httpAPI)) *httpAPI {
	v := &httpAPI{}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *httpAPI) Close() error {
	ctx, cancel := context.WithTimeout(context.Background(), v.gracefulQuitTimeout)
	defer cancel()
	v.server.Shutdown(ctx)

	v.wg.Wait()
	return nil
}

func (v *httpAPI) Run(ctx context.Context) error {
	// Parse address to listen.
	addr := envHttpAPI()
	if !strings.Contains(addr, ":") {
		addr = ":" + addr
	}

	// Create server and handler.
	mux := http.NewServeMux()
	v.server = &http.Server{Addr: addr, Handler: mux}
	logger.Df(ctx, "HTTP API server listen at %v", addr)

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
		apiResponse(ctx, w, r, map[string]string{
			"signature": Signature(),
			"version":   Version(),
		})
	})

	// Run HTTP API server.
	v.wg.Add(1)
	go func() {
		defer v.wg.Done()

		err := v.server.ListenAndServe()
		if err != nil {
			if ctx.Err() != context.Canceled {
				// TODO: If HTTP API server closed unexpectedly, we should notice the main loop to quit.
				logger.Wf(ctx, "HTTP API accept err %+v", err)
			} else {
				logger.Df(ctx, "HTTP API server done")
			}
		}
	}()

	return nil
}
