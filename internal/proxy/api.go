// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package proxy

import (
	"context"
	"crypto/subtle"
	"fmt"
	"net/http"
	"os"
	"strings"
	"sync"
	"time"

	"srsx/internal/env"
	"srsx/internal/errors"
	"srsx/internal/lb"
	"srsx/internal/logger"
	"srsx/internal/utils"
	"srsx/internal/version"
)

type HTTPAPIProxyServer interface {
	Run(ctx context.Context) error
	Close() error
}

type httpAPIProxyServer struct {
	environment         env.ProxyEnvironment
	server              httpServer
	rtc                 WebRTCProxyServer
	gracefulQuitTimeout time.Duration
	wg                  sync.WaitGroup
	// Tests replace shutdown and newServer, so no real port is bound.
	shutdown  func(ctx context.Context) error
	newServer func(addr string) (httpServer, *http.ServeMux)
}

func NewHTTPAPIProxyServer(environment env.ProxyEnvironment, gracefulQuitTimeout time.Duration, rtc WebRTCProxyServer, opts ...func(*httpAPIProxyServer)) HTTPAPIProxyServer {
	v := &httpAPIProxyServer{
		environment:         environment,
		gracefulQuitTimeout: gracefulQuitTimeout,
		rtc:                 rtc,
	}

	// Reads v.server at call time, since Run sets it later.
	v.shutdown = func(ctx context.Context) error {
		return v.server.Shutdown(ctx)
	}
	v.newServer = func(addr string) (httpServer, *http.ServeMux) {
		mux := http.NewServeMux()
		return &http.Server{Addr: addr, Handler: mux}, mux
	}

	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *httpAPIProxyServer) Close() error {
	ctx, cancel := context.WithTimeout(context.Background(), v.gracefulQuitTimeout)
	defer cancel()
	v.shutdown(ctx)

	v.wg.Wait()
	return nil
}

func (v *httpAPIProxyServer) Run(ctx context.Context) error {
	addr := v.environment.HttpAPI()
	if !strings.Contains(addr, ":") {
		addr = ":" + addr
	}

	server, mux := v.newServer(addr)
	v.server = server
	logger.Debug(ctx, "HTTP API server listen at %v", addr)

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
		utils.ApiResponse(ctx, w, r, map[string]string{
			"signature": version.Signature(),
			"version":   version.Version(),
		})
	})

	logger.Debug(ctx, "Handle /rtc/v1/whip/ by %v", addr)
	mux.HandleFunc("/rtc/v1/whip/", func(w http.ResponseWriter, r *http.Request) {
		if err := v.rtc.HandleApiForWHIP(ctx, w, r); err != nil {
			utils.ApiError(ctx, w, r, err)
		}
	})
	// srs-bench still uses the legacy SRS WebRTC publish API.
	logger.Debug(ctx, "Handle /rtc/v1/publish/ by %v", addr)
	mux.HandleFunc("/rtc/v1/publish/", func(w http.ResponseWriter, r *http.Request) {
		if err := v.rtc.HandleApiForWHIP(ctx, w, r); err != nil {
			utils.ApiError(ctx, w, r, err)
		}
	})

	logger.Debug(ctx, "Handle /rtc/v1/whep/ by %v", addr)
	mux.HandleFunc("/rtc/v1/whep/", func(w http.ResponseWriter, r *http.Request) {
		if err := v.rtc.HandleApiForWHEP(ctx, w, r); err != nil {
			utils.ApiError(ctx, w, r, err)
		}
	})
	// srs-bench still uses the legacy SRS WebRTC play API.
	logger.Debug(ctx, "Handle /rtc/v1/play/ by %v", addr)
	mux.HandleFunc("/rtc/v1/play/", func(w http.ResponseWriter, r *http.Request) {
		if err := v.rtc.HandleApiForWHEP(ctx, w, r); err != nil {
			utils.ApiError(ctx, w, r, err)
		}
	})

	v.wg.Add(1)
	go func() {
		defer v.wg.Done()

		err := v.server.ListenAndServe()
		if err != nil {
			if err == http.ErrServerClosed {
				logger.Debug(ctx, "HTTP API server done")
			} else if ctx.Err() != nil {
				logger.Debug(ctx, "HTTP API server done with context canceled")
			} else {
				// TODO: If HTTP API server closed unexpectedly, we should notice the main loop to quit.
				logger.Warn(ctx, "HTTP API accept err %+v", err)
			}
		}
	}()

	return nil
}

type systemAPI struct {
	environment         env.ProxyEnvironment
	loadBalancer        lb.OriginLoadBalancer
	server              httpServer
	gracefulQuitTimeout time.Duration
	wg                  sync.WaitGroup
	// Tests replace shutdown and newServer, so no real port is bound.
	shutdown  func(ctx context.Context) error
	newServer func(addr string) (httpServer, *http.ServeMux)
}

func (v *systemAPI) withHTTPAPIAuth(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if v.environment.HttpAPIAuthEnabled() != "on" {
			next(w, r)
			return
		}

		const prefix = "Bearer "
		authorization := r.Header.Get("Authorization")
		if !strings.HasPrefix(authorization, prefix) {
			w.Header().Set("WWW-Authenticate", "Bearer")
			http.Error(w, http.StatusText(http.StatusUnauthorized), http.StatusUnauthorized)
			return
		}

		provided := strings.TrimPrefix(authorization, prefix)
		expected := v.environment.HttpAPIAuthToken()
		if provided == "" || subtle.ConstantTimeCompare([]byte(provided), []byte(expected)) != 1 {
			w.Header().Set("WWW-Authenticate", "Bearer")
			http.Error(w, http.StatusText(http.StatusUnauthorized), http.StatusUnauthorized)
			return
		}

		next(w, r)
	}
}

func NewSystemAPI(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer, gracefulQuitTimeout time.Duration, opts ...func(*systemAPI)) *systemAPI {
	v := &systemAPI{
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

	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *systemAPI) Close() error {
	ctx, cancel := context.WithTimeout(context.Background(), v.gracefulQuitTimeout)
	defer cancel()
	v.shutdown(ctx)

	v.wg.Wait()
	return nil
}

func (v *systemAPI) Run(ctx context.Context) error {
	addr := v.environment.SystemAPI()
	if !strings.Contains(addr, ":") {
		addr = ":" + addr
	}

	server, mux := v.newServer(addr)
	v.server = server
	logger.Debug(ctx, "System API server listen at %v", addr)

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
		utils.ApiResponse(ctx, w, r, map[string]string{
			"signature": version.Signature(),
			"version":   version.Version(),
		})
	})

	logger.Debug(ctx, "Handle /api/v1/srs/register by %v", addr)
	mux.HandleFunc("/api/v1/srs/register", v.withHTTPAPIAuth(func(w http.ResponseWriter, r *http.Request) {
		if err := func() error {
			var deviceID, ip, serverID, serviceID, pid string
			var rtmp, stream, api, srt, rtc []string
			if err := utils.ParseBody(r.Body, &struct {
				IP        *string   `json:"ip"`
				ServerID  *string   `json:"server"`
				ServiceID *string   `json:"service"`
				PID       *string   `json:"pid"`
				RTMP      *[]string `json:"rtmp"`
				HTTP      *[]string `json:"http"`
				API       *[]string `json:"api"`
				SRT       *[]string `json:"srt"`
				RTC       *[]string `json:"rtc"`
				DeviceID  *string   `json:"device_id"`
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

			server := lb.NewOriginServer(func(srs *lb.OriginServer) {
				srs.IP, srs.DeviceID = ip, deviceID
				srs.ServerID, srs.ServiceID, srs.PID = serverID, serviceID, pid
				srs.RTMP, srs.HTTP, srs.API = rtmp, stream, api
				srs.SRT, srs.RTC = srt, rtc
				srs.UpdatedAt = time.Now()
			})
			if err := v.loadBalancer.Update(ctx, server); err != nil {
				return errors.Wrapf(err, "update SRS server %+v", server)
			}

			logger.Debug(ctx, "Register SRS media server, %+v", server)
			return nil
		}(); err != nil {
			utils.ApiError(ctx, w, r, err)
		}

		type Response struct {
			Code int    `json:"code"`
			PID  string `json:"pid"`
		}

		utils.ApiResponse(ctx, w, r, &Response{
			Code: 0, PID: fmt.Sprintf("%v", os.Getpid()),
		})
	}))

	v.wg.Add(1)
	go func() {
		defer v.wg.Done()

		err := v.server.ListenAndServe()
		if err != nil {
			if err == http.ErrServerClosed {
				logger.Debug(ctx, "System API server done")
			} else if ctx.Err() != nil {
				logger.Debug(ctx, "System API server done with context canceled")
			} else {
				// TODO: If System API server closed unexpectedly, we should notice the main loop to quit.
				logger.Warn(ctx, "System API accept err %+v", err)
			}
		}
	}()

	return nil
}
