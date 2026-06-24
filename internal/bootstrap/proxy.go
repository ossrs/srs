// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package bootstrap

import (
	"context"
	"time"

	"srsx/internal/debug"
	"srsx/internal/env"
	"srsx/internal/errors"
	"srsx/internal/lb"
	"srsx/internal/logger"
	"srsx/internal/proxy"
	"srsx/internal/signal"
	"srsx/internal/version"
)

// NewProxyBootstrap creates a new Bootstrap instance for the proxy server.
func NewProxyBootstrap(opts ...func(*proxyBootstrap)) Bootstrap {
	v := &proxyBootstrap{}

	// Default newEnvironment: read the real process env / .env file.
	v.newEnvironment = func(ctx context.Context) (env.ProxyEnvironment, error) {
		return env.NewProxyEnvironment(ctx)
	}
	// Default newSignalHandler: construct a real OS signal handler.
	v.newSignalHandler = func() signalHandler {
		return signal.NewHandler()
	}
	// Default newRedisLoadBalancer: construct a real Redis-backed load balancer.
	v.newRedisLoadBalancer = func(environment env.ProxyEnvironment) lb.OriginLoadBalancer {
		return lb.NewRedisLoadBalancer(environment)
	}
	// Default newMemoryLoadBalancer: construct a real in-memory load balancer.
	v.newMemoryLoadBalancer = func(environment env.ProxyEnvironment) lb.OriginLoadBalancer {
		return lb.NewMemoryLoadBalancer(environment)
	}
	// Default newRTMPProxyServer: construct a real RTMP proxy server.
	v.newRTMPProxyServer = func(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer) proxy.RTMPProxyServer {
		return proxy.NewRTMPProxyServer(environment, loadBalancer)
	}
	// Default newWebRTCProxyServer: construct a real WebRTC proxy server.
	v.newWebRTCProxyServer = func(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer) proxy.WebRTCProxyServer {
		return proxy.NewWebRTCProxyServer(environment, loadBalancer)
	}
	// Default newHTTPAPIProxyServer: construct a real HTTP API proxy server.
	v.newHTTPAPIProxyServer = func(environment env.ProxyEnvironment, gracefulQuitTimeout time.Duration, rtc proxy.WebRTCProxyServer) proxy.HTTPAPIProxyServer {
		return proxy.NewHTTPAPIProxyServer(environment, gracefulQuitTimeout, rtc)
	}
	// Default newSRSSRTProxyServer: construct a real SRT proxy server.
	v.newSRSSRTProxyServer = func(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer) proxyServer {
		return proxy.NewSRSSRTProxyServer(environment, loadBalancer)
	}
	// Default newSystemAPI: construct a real system API server.
	v.newSystemAPI = func(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer, gracefulQuitTimeout time.Duration) proxyServer {
		return proxy.NewSystemAPI(environment, loadBalancer, gracefulQuitTimeout)
	}
	// Default newHTTPStreamProxyServer: construct a real HTTP stream proxy server.
	v.newHTTPStreamProxyServer = func(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer, gracefulQuitTimeout time.Duration) proxy.HTTPStreamProxyServer {
		return proxy.NewHTTPStreamProxyServer(environment, loadBalancer, gracefulQuitTimeout)
	}

	for _, opt := range opts {
		opt(v)
	}
	return v
}

// proxyBootstrap implements the Bootstrap interface for the proxy server.
type proxyBootstrap struct {
	// newEnvironment constructs the proxy environment. Defaults to
	// env.NewProxyEnvironment; tests may override via a functional option to
	// supply a fake environment without reading the real process env or .env file.
	newEnvironment func(ctx context.Context) (env.ProxyEnvironment, error)
	// newSignalHandler constructs the OS signal handler used to install
	// signal listeners and the force-quit timer. Defaults to signal.NewHandler;
	// tests may override via a functional option to supply a fake handler that
	// does not install real OS signal handlers or a real force-quit timer.
	newSignalHandler func() signalHandler
	// newRedisLoadBalancer constructs the Redis-backed load balancer used when
	// environment.LoadBalancerType() == "redis". Defaults to lb.NewRedisLoadBalancer;
	// tests may override via a functional option to supply a fake load balancer
	// that does not connect to a real Redis instance.
	newRedisLoadBalancer func(environment env.ProxyEnvironment) lb.OriginLoadBalancer
	// newMemoryLoadBalancer constructs the in-memory load balancer used when
	// environment.LoadBalancerType() is anything other than "redis". Defaults to
	// lb.NewMemoryLoadBalancer; tests may override via a functional option to
	// supply a fake load balancer for assertions on the default branch.
	newMemoryLoadBalancer func(environment env.ProxyEnvironment) lb.OriginLoadBalancer
	// newRTMPProxyServer constructs the RTMP proxy server. Defaults to
	// proxy.NewRTMPProxyServer; tests may override via a functional option to
	// supply a fake (e.g. proxyfakes.FakeRTMPProxyServer) that does not bind a
	// real TCP port.
	newRTMPProxyServer func(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer) proxy.RTMPProxyServer
	// newWebRTCProxyServer constructs the WebRTC proxy server. Defaults to
	// proxy.NewWebRTCProxyServer; tests may override via a functional option to
	// supply a fake (e.g. proxyfakes.FakeWebRTCProxyServer) that does not bind
	// a real UDP port.
	newWebRTCProxyServer func(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer) proxy.WebRTCProxyServer
	// newHTTPAPIProxyServer constructs the HTTP API proxy server. Defaults to
	// proxy.NewHTTPAPIProxyServer; tests may override via a functional option
	// to supply a fake (e.g. proxyfakes.FakeHTTPAPIProxyServer) that does not
	// bind a real HTTP port.
	newHTTPAPIProxyServer func(environment env.ProxyEnvironment, gracefulQuitTimeout time.Duration, rtc proxy.WebRTCProxyServer) proxy.HTTPAPIProxyServer
	// newSRSSRTProxyServer constructs the SRT proxy server. Defaults to
	// proxy.NewSRSSRTProxyServer; tests may override via a functional option
	// to supply a fake that does not bind a real UDP port. Returned as the
	// local proxyServer interface because proxy.NewSRSSRTProxyServer currently
	// returns an unexported concrete type.
	newSRSSRTProxyServer func(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer) proxyServer
	// newSystemAPI constructs the system API server. Defaults to proxy.NewSystemAPI;
	// tests may override via a functional option to supply a fake that does not
	// bind a real HTTP port. Returned as the local proxyServer interface because
	// proxy.NewSystemAPI currently returns an unexported concrete type.
	newSystemAPI func(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer, gracefulQuitTimeout time.Duration) proxyServer
	// newHTTPStreamProxyServer constructs the HTTP stream proxy server. Defaults
	// to proxy.NewHTTPStreamProxyServer; tests may override via a functional
	// option to supply a fake (e.g. proxyfakes.FakeHTTPStreamProxyServer) that
	// does not bind a real HTTP port.
	newHTTPStreamProxyServer func(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer, gracefulQuitTimeout time.Duration) proxy.HTTPStreamProxyServer
}

// signalHandler is the minimal contract of a signal handler that proxyBootstrap
// drives. *signal.Handler satisfies it. Tests may supply a fake that does not
// install real OS signal handlers or a real force-quit timer.
type signalHandler interface {
	InstallSignals(ctx context.Context, cancel context.CancelFunc)
	InstallForceQuit(ctx context.Context, environment env.ProxyEnvironment) error
}

// proxyServer is the minimal Run/Close contract used by proxyBootstrap for the
// SRT proxy and system API. proxy.NewSRSSRTProxyServer and proxy.NewSystemAPI
// currently return unexported concrete types which bootstrap cannot name; their
// values satisfy this interface structurally so tests can still inject fakes.
type proxyServer interface {
	Run(ctx context.Context) error
	Close() error
}

// Start initializes the context with logger and signal handlers, then runs the bootstrap.
// Returns any error encountered during startup.
func (b *proxyBootstrap) Start(ctx context.Context) error {
	ctx = logger.WithContext(ctx)
	logger.Debug(ctx, "%v-Proxy/%v started", version.Signature(), version.Version())

	// Install signals.
	ctx, cancel := context.WithCancel(ctx)
	b.newSignalHandler().InstallSignals(ctx, cancel)

	// Run the main loop, ignore the user cancel error.
	err := b.run(ctx)
	if err != nil && ctx.Err() != context.Canceled {
		logger.Error(ctx, "main: %+v", err)
		return err
	}

	logger.Debug(ctx, "%v done", version.Signature())
	return nil
}

// Run initializes and starts all proxy servers and the load balancer.
// It blocks until the context is cancelled.
func (b *proxyBootstrap) run(ctx context.Context) error {
	// Setup the environment variables.
	environment, err := b.newEnvironment(ctx)
	if err != nil {
		return errors.Wrapf(err, "create environment")
	}

	// When cancelled, the program is forced to exit due to a timeout. Normally, this doesn't occur
	// because the main thread exits after the context is cancelled. However, sometimes the main thread
	// may be blocked for some reason, so a forced exit is necessary to ensure the program terminates.
	if err := b.newSignalHandler().InstallForceQuit(ctx, environment); err != nil {
		return errors.Wrapf(err, "install force quit")
	}

	// Start the Go pprof if enabled.
	debug.HandleGoPprof(ctx, environment)

	// Create and initialize the load balancer.
	loadBalancer, err := b.initializeLoadBalancer(ctx, environment)
	if err != nil {
		return err
	}

	// Parse the gracefully quit timeout.
	gracefulQuitTimeout, err := time.ParseDuration(environment.GraceQuitTimeout())
	if err != nil {
		return errors.Wrapf(err, "parse gracefully quit timeout")
	}

	// Start all servers and block until context is cancelled.
	return b.startServers(ctx, environment, loadBalancer, gracefulQuitTimeout)
}

// initializeLoadBalancer sets up the load balancer based on configuration.
func (b *proxyBootstrap) initializeLoadBalancer(ctx context.Context, environment env.ProxyEnvironment) (lb.OriginLoadBalancer, error) {
	var loadBalancer lb.OriginLoadBalancer
	switch environment.LoadBalancerType() {
	case "redis":
		loadBalancer = b.newRedisLoadBalancer(environment)
	default:
		loadBalancer = b.newMemoryLoadBalancer(environment)
	}

	if err := loadBalancer.Initialize(ctx); err != nil {
		return nil, errors.Wrapf(err, "initialize srs load balancer")
	}

	return loadBalancer, nil
}

// startServers initializes and starts all protocol servers.
func (b *proxyBootstrap) startServers(ctx context.Context, environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer, gracefulQuitTimeout time.Duration) error {
	// Start the RTMP server.
	rtmpProxyServer := b.newRTMPProxyServer(environment, loadBalancer)
	if err := rtmpProxyServer.Run(ctx); err != nil {
		return errors.Wrapf(err, "rtmp server")
	}
	defer rtmpProxyServer.Close()

	// Start the WebRTC server.
	webRTCProxyServer := b.newWebRTCProxyServer(environment, loadBalancer)
	if err := webRTCProxyServer.Run(ctx); err != nil {
		return errors.Wrapf(err, "rtc server")
	}
	defer webRTCProxyServer.Close()

	// Start the HTTP API server.
	httpAPIProxyServer := b.newHTTPAPIProxyServer(environment, gracefulQuitTimeout, webRTCProxyServer)
	if err := httpAPIProxyServer.Run(ctx); err != nil {
		return errors.Wrapf(err, "http api server")
	}
	defer httpAPIProxyServer.Close()

	// Start the SRT server.
	srsSRTProxyServer := b.newSRSSRTProxyServer(environment, loadBalancer)
	if err := srsSRTProxyServer.Run(ctx); err != nil {
		return errors.Wrapf(err, "srt server")
	}
	defer srsSRTProxyServer.Close()

	// Start the System API server.
	systemAPI := b.newSystemAPI(environment, loadBalancer, gracefulQuitTimeout)
	if err := systemAPI.Run(ctx); err != nil {
		return errors.Wrapf(err, "system api server")
	}
	defer systemAPI.Close()

	// Start the HTTP web server.
	httpStreamProxyServer := b.newHTTPStreamProxyServer(environment, loadBalancer, gracefulQuitTimeout)
	if err := httpStreamProxyServer.Run(ctx); err != nil {
		return errors.Wrapf(err, "http server")
	}
	defer httpStreamProxyServer.Close()

	// Wait for the main loop to quit.
	<-ctx.Done()

	return nil
}
