// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"net/http"
	"strings"
	"sync"
	"time"

	"srs-proxy/errors"
	"srs-proxy/logger"
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

type systemAPI struct {
	// The underlayer HTTP server.
	server *http.Server
	// The gracefully quit timeout, wait server to quit.
	gracefulQuitTimeout time.Duration
	// The wait group for all goroutines.
	wg sync.WaitGroup
}

func NewSystemAPI(opts ...func(*systemAPI)) *systemAPI {
	v := &systemAPI{}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *systemAPI) Close() error {
	ctx, cancel := context.WithTimeout(context.Background(), v.gracefulQuitTimeout)
	defer cancel()
	v.server.Shutdown(ctx)

	v.wg.Wait()
	return nil
}

func (v *systemAPI) Run(ctx context.Context) error {
	// Parse address to listen.
	addr := envSystemAPI()
	if !strings.Contains(addr, ":") {
		addr = ":" + addr
	}

	// Create server and handler.
	mux := http.NewServeMux()
	v.server = &http.Server{Addr: addr, Handler: mux}
	logger.Df(ctx, "System API server listen at %v", addr)

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

	// The register service for SRS media servers.
	logger.Df(ctx, "Handle /api/v1/srs/register by %v", addr)
	mux.HandleFunc("/api/v1/srs/register", func(w http.ResponseWriter, r *http.Request) {
		if err := func() error {
			var deviceID, ip, serverID, serviceID, pid string
			var rtmp, stream, api, srt, rtc []string
			if err := ParseBody(r.Body, &struct {
				// The IP of SRS, mandatory.
				IP *string `json:"ip"`
				// The server id of SRS, store in file, may not change, mandatory.
				ServerID *string `json:"server"`
				// The service id of SRS, always change when restarted, mandatory.
				ServiceID *string `json:"service"`
				// The process id of SRS, always change when restarted, mandatory.
				PID *string `json:"pid"`
				// The RTMP listen endpoints, mandatory.
				RTMP *[]string `json:"rtmp"`
				// The HTTP Stream listen endpoints, optional.
				HTTP *[]string `json:"http"`
				// The API listen endpoints, optional.
				API *[]string `json:"api"`
				// The SRT listen endpoints, optional.
				SRT *[]string `json:"srt"`
				// The RTC listen endpoints, optional.
				RTC *[]string `json:"rtc"`
				// The device id of SRS, optional.
				DeviceID *string `json:"device_id"`
			}{
				IP: &ip, DeviceID: &deviceID,
				ServerID: &serverID, ServiceID: &serviceID, PID: &pid,
				RTMP: &rtmp, HTTP: &stream, API: &api, SRT: &srt, RTC: &rtc,
			}); err != nil {
				return errors.Wrapf(err, "parse body")
			}

			if ip == "" {
				return errors.Errorf("empty ip")
			}
			if serverID == "" {
				return errors.Errorf("empty server")
			}
			if serviceID == "" {
				return errors.Errorf("empty service")
			}
			if pid == "" {
				return errors.Errorf("empty pid")
			}
			if len(rtmp) == 0 {
				return errors.Errorf("empty rtmp")
			}

			server := NewSRSServer(func(srs *SRSServer) {
				srs.IP, srs.DeviceID = ip, deviceID
				srs.ServerID, srs.ServiceID, srs.PID = serverID, serviceID, pid
				srs.RTMP, srs.HTTP, srs.API = rtmp, stream, api
				srs.SRT, srs.RTC = srt, rtc
			})
			srsLoadBalancer.Update(server)

			logger.Df(ctx, "Register SRS media server, %v", server)
			return nil
		}(); err != nil {
			apiError(ctx, w, r, err)
		}

		apiResponse(ctx, w, r, map[string]string{
			"signature": Signature(),
			"version":   Version(),
		})
	})

	// Run System API server.
	v.wg.Add(1)
	go func() {
		defer v.wg.Done()

		err := v.server.ListenAndServe()
		if err != nil {
			if ctx.Err() != context.Canceled {
				// TODO: If System API server closed unexpectedly, we should notice the main loop to quit.
				logger.Wf(ctx, "System API accept err %+v", err)
			} else {
				logger.Df(ctx, "System API server done")
			}
		}
	}()

	return nil
}
