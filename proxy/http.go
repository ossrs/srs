// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"fmt"
	"net/http"
	"os"
	"srs-proxy/logger"
	"strings"
	"time"

	"srs-proxy/errors"
)

type httpServer struct {
	server *http.Server
}

func NewHttpServer() *httpServer {
	return &httpServer{}
}

func (v *httpServer) Close() error {
	return v.server.Close()
}

func (v *httpServer) ListenAndServe(ctx context.Context) error {
	// Parse the gracefully quit timeout.
	var gracefulQuitTimeout time.Duration
	if t, err := time.ParseDuration(envGraceQuitTimeout()); err != nil {
		return errors.Wrapf(err, "parse duration %v", envGraceQuitTimeout())
	} else {
		gracefulQuitTimeout = t
	}

	// Parse address to listen.
	addr := envHttpServer()
	if !strings.Contains(addr, ":") {
		addr = ":" + addr
	}

	// Create server and handler.
	mux := http.NewServeMux()
	v.server = &http.Server{Addr: addr, Handler: mux}

	// Shutdown the server gracefully when quiting.
	go func() {
		ctxParent := ctx
		<-ctxParent.Done()

		ctx, cancel := context.WithTimeout(context.Background(), gracefulQuitTimeout)
		defer cancel()

		v.server.Shutdown(ctx)
	}()

	// The basic version handler, also can be used as health check API.
	logger.Df(ctx, "Handle /api/v1/versions by %v", addr)
	mux.HandleFunc("/api/v1/versions", func(w http.ResponseWriter, r *http.Request) {
		res := struct {
			Code int    `json:"code"`
			PID  string `json:"pid"`
			Data struct {
				Major    int    `json:"major"`
				Minor    int    `json:"minor"`
				Revision int    `json:"revision"`
				Version  string `json:"version"`
			} `json:"data"`
		}{}

		res.Code = 0
		res.PID = fmt.Sprintf("%v", os.Getpid())
		res.Data.Major = VersionMajor()
		res.Data.Minor = VersionMinor()
		res.Data.Revision = VersionRevision()
		res.Data.Version = Version()

		apiResponse(ctx, w, r, &res)
	})

	// Run HTTP server.
	return v.server.ListenAndServe()
}
