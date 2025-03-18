// Copyright (c) 2025 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"fmt"
	"net/http"
	"os"
	"strings"
	"sync"
	"time"

	"srs-proxy/errors"
	"srs-proxy/logger"
)

// srsHTTPAPIServer is the proxy for SRS HTTP API, to proxy the WebRTC HTTP API like WHIP and WHEP,
// to proxy other HTTP API of SRS like the streams and clients, etc.
type srsHTTPAPIServer struct {
	// The underlayer HTTP server.
	server *http.Server
	// The WebRTC server.
	rtc *srsWebRTCServer
	// The gracefully quit timeout, wait server to quit.
	gracefulQuitTimeout time.Duration
	// The wait group for all goroutines.
	wg sync.WaitGroup
}

func NewSRSHTTPAPIServer(opts ...func(*srsHTTPAPIServer)) *srsHTTPAPIServer {
	v := &srsHTTPAPIServer{}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *srsHTTPAPIServer) Close() error {
	ctx, cancel := context.WithTimeout(context.Background(), v.gracefulQuitTimeout)
	defer cancel()
	v.server.Shutdown(ctx)

	v.wg.Wait()
	return nil
}

func (v *srsHTTPAPIServer) Run(ctx context.Context) error {
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

	// The WebRTC WHIP API handler.
	logger.Df(ctx, "Handle /rtc/v1/whip/ by %v", addr)
	mux.HandleFunc("/rtc/v1/whip/", func(w http.ResponseWriter, r *http.Request) {
		if err := v.rtc.HandleApiForWHIP(ctx, w, r); err != nil {
			apiError(ctx, w, r, err)
		}
	})

	// The WebRTC WHEP API handler.
	logger.Df(ctx, "Handle /rtc/v1/whep/ by %v", addr)
	mux.HandleFunc("/rtc/v1/whep/", func(w http.ResponseWriter, r *http.Request) {
		if err := v.rtc.HandleApiForWHEP(ctx, w, r); err != nil {
			apiError(ctx, w, r, err)
		}
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

// systemAPI is the system HTTP API of the proxy server, for SRS media server to register the service
// to proxy server. It also provides some other system APIs like the status of proxy server, like exporter
// for Prometheus metrics.
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
				srs.UpdatedAt = time.Now()
			})
			if err := srsLoadBalancer.Update(ctx, server); err != nil {
				return errors.Wrapf(err, "update SRS server %+v", server)
			}

			logger.Df(ctx, "Register SRS media server, %+v", server)
			return nil
		}(); err != nil {
			apiError(ctx, w, r, err)
		}

		type Response struct {
			Code int    `json:"code"`
			PID  string `json:"pid"`
		}

		apiResponse(ctx, w, r, &Response{
			Code: 0, PID: fmt.Sprintf("%v", os.Getpid()),
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
