// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package proxy

import (
	"context"
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

// HTTPAPIProxyServer is the proxy for SRS HTTP API, to proxy the WebRTC HTTP API like WHIP and WHEP,
// to proxy other HTTP API of SRS like the streams and clients, etc.
type HTTPAPIProxyServer interface {
	Run(ctx context.Context) error
	Close() error
}

type httpAPIProxyServer struct {
	// The environment interface.
	environment env.ProxyEnvironment
	// The underlayer HTTP server.
	server httpServer
	// The WebRTC server.
	rtc WebRTCProxyServer
	// The gracefully quit timeout, wait server to quit.
	gracefulQuitTimeout time.Duration
	// The wait group for all goroutines.
	wg sync.WaitGroup
	// shutdown gracefully shuts down the underlying HTTP server. Defaults to
	// v.server.Shutdown; tests may override via a functional option to verify
	// the shutdown contract without binding a real socket.
	shutdown func(ctx context.Context) error
	// newServer constructs the underlying HTTP server bound to addr and the
	// ServeMux that handlers are registered on. Defaults to a real http.Server
	// and ServeMux; tests may override via a functional option to supply a fake
	// server that does not bind a real port.
	newServer func(addr string) (httpServer, *http.ServeMux)
}

func NewHTTPAPIProxyServer(environment env.ProxyEnvironment, gracefulQuitTimeout time.Duration, rtc WebRTCProxyServer, opts ...func(*httpAPIProxyServer)) HTTPAPIProxyServer {
	v := &httpAPIProxyServer{
		environment:         environment,
		gracefulQuitTimeout: gracefulQuitTimeout,
		rtc:                 rtc,
	}

	// Default shutdown: delegate to the underlying http.Server. The closure
	// captures v rather than v.server so the dereference happens at call time,
	// after Run() has assigned v.server.
	v.shutdown = func(ctx context.Context) error {
		return v.server.Shutdown(ctx)
	}
	// Default newServer: a real http.Server and ServeMux pair.
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
	// Parse address to listen.
	addr := v.environment.HttpAPI()
	if !strings.Contains(addr, ":") {
		addr = ":" + addr
	}

	// Create server and handler.
	server, mux := v.newServer(addr)
	v.server = server
	logger.Debug(ctx, "HTTP API server listen at %v", addr)

	// Shutdown the server gracefully when quiting.
	go func() {
		ctxParent := ctx
		<-ctxParent.Done()

		ctx, cancel := context.WithTimeout(context.Background(), v.gracefulQuitTimeout)
		defer cancel()

		v.shutdown(ctx)
	}()

	// The basic version handler, also can be used as health check API.
	logger.Debug(ctx, "Handle /api/v1/versions by %v", addr)
	mux.HandleFunc("/api/v1/versions", func(w http.ResponseWriter, r *http.Request) {
		utils.ApiResponse(ctx, w, r, map[string]string{
			"signature": version.Signature(),
			"version":   version.Version(),
		})
	})

	// The WebRTC WHIP API handler.
	logger.Debug(ctx, "Handle /rtc/v1/whip/ by %v", addr)
	mux.HandleFunc("/rtc/v1/whip/", func(w http.ResponseWriter, r *http.Request) {
		if err := v.rtc.HandleApiForWHIP(ctx, w, r); err != nil {
			utils.ApiError(ctx, w, r, err)
		}
	})
	// Keep compatibility with the legacy SRS WebRTC publish API used by srs-bench.
	logger.Debug(ctx, "Handle /rtc/v1/publish/ by %v", addr)
	mux.HandleFunc("/rtc/v1/publish/", func(w http.ResponseWriter, r *http.Request) {
		if err := v.rtc.HandleApiForWHIP(ctx, w, r); err != nil {
			utils.ApiError(ctx, w, r, err)
		}
	})

	// The WebRTC WHEP API handler.
	logger.Debug(ctx, "Handle /rtc/v1/whep/ by %v", addr)
	mux.HandleFunc("/rtc/v1/whep/", func(w http.ResponseWriter, r *http.Request) {
		if err := v.rtc.HandleApiForWHEP(ctx, w, r); err != nil {
			utils.ApiError(ctx, w, r, err)
		}
	})
	// Keep compatibility with the legacy SRS WebRTC play API used by srs-bench.
	logger.Debug(ctx, "Handle /rtc/v1/play/ by %v", addr)
	mux.HandleFunc("/rtc/v1/play/", func(w http.ResponseWriter, r *http.Request) {
		if err := v.rtc.HandleApiForWHEP(ctx, w, r); err != nil {
			utils.ApiError(ctx, w, r, err)
		}
	})

	// Run HTTP API server.
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

// systemAPI is the system HTTP API of the proxy server, for SRS media server to register the service
// to proxy server. It also provides some other system APIs like the status of proxy server, like exporter
// for Prometheus metrics.
type systemAPI struct {
	// The environment interface.
	environment env.ProxyEnvironment
	// The load balancer for origin servers.
	loadBalancer lb.OriginLoadBalancer
	// The underlayer HTTP server.
	server httpServer
	// The gracefully quit timeout, wait server to quit.
	gracefulQuitTimeout time.Duration
	// The wait group for all goroutines.
	wg sync.WaitGroup
	// shutdown gracefully shuts down the underlying HTTP server. Defaults to
	// v.server.Shutdown; tests may override via a functional option to verify
	// the shutdown contract without binding a real socket.
	shutdown func(ctx context.Context) error
	// newServer constructs the underlying HTTP server bound to addr and the
	// ServeMux that handlers are registered on. Defaults to a real http.Server
	// and ServeMux; tests may override via a functional option to supply a fake
	// server that does not bind a real port.
	newServer func(addr string) (httpServer, *http.ServeMux)
}

func NewSystemAPI(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer, gracefulQuitTimeout time.Duration, opts ...func(*systemAPI)) *systemAPI {
	v := &systemAPI{
		environment:         environment,
		loadBalancer:        loadBalancer,
		gracefulQuitTimeout: gracefulQuitTimeout,
	}

	// Default shutdown: delegate to the underlying http.Server. The closure
	// captures v rather than v.server so the dereference happens at call time,
	// after Run() has assigned v.server.
	v.shutdown = func(ctx context.Context) error {
		return v.server.Shutdown(ctx)
	}
	// Default newServer: a real http.Server and ServeMux pair.
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
	// Parse address to listen.
	addr := v.environment.SystemAPI()
	if !strings.Contains(addr, ":") {
		addr = ":" + addr
	}

	// Create server and handler.
	server, mux := v.newServer(addr)
	v.server = server
	logger.Debug(ctx, "System API server listen at %v", addr)

	// Shutdown the server gracefully when quiting.
	go func() {
		ctxParent := ctx
		<-ctxParent.Done()

		ctx, cancel := context.WithTimeout(context.Background(), v.gracefulQuitTimeout)
		defer cancel()

		v.shutdown(ctx)
	}()

	// The basic version handler, also can be used as health check API.
	logger.Debug(ctx, "Handle /api/v1/versions by %v", addr)
	mux.HandleFunc("/api/v1/versions", func(w http.ResponseWriter, r *http.Request) {
		utils.ApiResponse(ctx, w, r, map[string]string{
			"signature": version.Signature(),
			"version":   version.Version(),
		})
	})

	// The register service for SRS media servers.
	logger.Debug(ctx, "Handle /api/v1/srs/register by %v", addr)
	mux.HandleFunc("/api/v1/srs/register", func(w http.ResponseWriter, r *http.Request) {
		if err := func() error {
			var deviceID, ip, serverID, serviceID, pid string
			var rtmp, stream, api, srt, rtc []string
			if err := utils.ParseBody(r.Body, &struct {
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
	})

	// Run System API server.
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
