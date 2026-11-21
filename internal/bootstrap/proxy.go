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

func NewProxyBootstrap(opts ...func(*proxyBootstrap)) Bootstrap {
	v := &proxyBootstrap{}

	v.newEnvironment = func(ctx context.Context) (env.ProxyEnvironment, error) {
		return env.NewProxyEnvironment(ctx)
	}
	v.newSignalHandler = func() signalHandler {
		return signal.NewHandler()
	}
	v.newRedisLoadBalancer = func(environment env.ProxyEnvironment) lb.OriginLoadBalancer {
		return lb.NewRedisLoadBalancer(environment)
	}
	v.newMemoryLoadBalancer = func(environment env.ProxyEnvironment) lb.OriginLoadBalancer {
		return lb.NewMemoryLoadBalancer(environment)
	}
	v.newRTMPProxyServer = func(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer) proxy.RTMPProxyServer {
		return proxy.NewRTMPProxyServer(environment, loadBalancer)
	}
	v.newWebRTCProxyServer = func(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer) proxy.WebRTCProxyServer {
		return proxy.NewWebRTCProxyServer(environment, loadBalancer)
	}
	v.newHTTPAPIProxyServer = func(environment env.ProxyEnvironment, gracefulQuitTimeout time.Duration, rtc proxy.WebRTCProxyServer) proxy.HTTPAPIProxyServer {
		return proxy.NewHTTPAPIProxyServer(environment, gracefulQuitTimeout, rtc)
	}
	v.newSRSSRTProxyServer = func(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer) proxyServer {
		return proxy.NewSRSSRTProxyServer(environment, loadBalancer)
	}
	v.newSystemAPI = func(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer, gracefulQuitTimeout time.Duration) proxyServer {
		return proxy.NewSystemAPI(environment, loadBalancer, gracefulQuitTimeout)
	}
	v.newHTTPStreamProxyServer = func(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer, gracefulQuitTimeout time.Duration) proxy.HTTPStreamProxyServer {
		return proxy.NewHTTPStreamProxyServer(environment, loadBalancer, gracefulQuitTimeout)
	}

	for _, opt := range opts {
		opt(v)
	}
	return v
}

// Tests replace these constructors with an option, to inject fakes that do not
// read the real process env or .env file, install OS signal handlers, connect
// to Redis, or bind real ports.
type proxyBootstrap struct {
	newEnvironment           func(ctx context.Context) (env.ProxyEnvironment, error)
	newSignalHandler         func() signalHandler
	newRedisLoadBalancer     func(environment env.ProxyEnvironment) lb.OriginLoadBalancer
	newMemoryLoadBalancer    func(environment env.ProxyEnvironment) lb.OriginLoadBalancer
	newRTMPProxyServer       func(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer) proxy.RTMPProxyServer
	newWebRTCProxyServer     func(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer) proxy.WebRTCProxyServer
	newHTTPAPIProxyServer    func(environment env.ProxyEnvironment, gracefulQuitTimeout time.Duration, rtc proxy.WebRTCProxyServer) proxy.HTTPAPIProxyServer
	newSRSSRTProxyServer     func(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer) proxyServer
	newSystemAPI             func(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer, gracefulQuitTimeout time.Duration) proxyServer
	newHTTPStreamProxyServer func(environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer, gracefulQuitTimeout time.Duration) proxy.HTTPStreamProxyServer
}

type signalHandler interface {
	InstallSignals(ctx context.Context, cancel context.CancelFunc)
	InstallForceQuit(ctx context.Context, environment env.ProxyEnvironment) error
}

// proxy.NewSRSSRTProxyServer and proxy.NewSystemAPI return unexported types,
// which this package cannot name.
type proxyServer interface {
	Run(ctx context.Context) error
	Close() error
}

func (b *proxyBootstrap) Start(ctx context.Context) error {
	ctx = logger.WithContext(ctx)
	logger.Debug(ctx, "%v-Proxy/%v started", version.Signature(), version.Version())

	ctx, cancel := context.WithCancel(ctx)
	b.newSignalHandler().InstallSignals(ctx, cancel)

	err := b.run(ctx)
	if err != nil && ctx.Err() != context.Canceled {
		logger.Error(ctx, "main: %+v", err)
		return err
	}

	logger.Debug(ctx, "%v done", version.Signature())
	return nil
}

func (b *proxyBootstrap) run(ctx context.Context) error {
	environment, err := b.newEnvironment(ctx)
	if err != nil {
		return errors.Wrapf(err, "create environment")
	}

	// Normally the main goroutine exits after the context is cancelled, but it
	// may be blocked, so the force quit ends the program after a timeout.
	if err := b.newSignalHandler().InstallForceQuit(ctx, environment); err != nil {
		return errors.Wrapf(err, "install force quit")
	}

	debug.HandleGoPprof(ctx, environment)

	loadBalancer, err := b.initializeLoadBalancer(ctx, environment)
	if err != nil {
		return err
	}

	gracefulQuitTimeout, err := time.ParseDuration(environment.GraceQuitTimeout())
	if err != nil {
		return errors.Wrapf(err, "parse gracefully quit timeout")
	}

	return b.startServers(ctx, environment, loadBalancer, gracefulQuitTimeout)
}

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

func (b *proxyBootstrap) startServers(ctx context.Context, environment env.ProxyEnvironment, loadBalancer lb.OriginLoadBalancer, gracefulQuitTimeout time.Duration) error {
	rtmpProxyServer := b.newRTMPProxyServer(environment, loadBalancer)
	if err := rtmpProxyServer.Run(ctx); err != nil {
		return errors.Wrapf(err, "rtmp server")
	}
	defer rtmpProxyServer.Close()

	webRTCProxyServer := b.newWebRTCProxyServer(environment, loadBalancer)
	if err := webRTCProxyServer.Run(ctx); err != nil {
		return errors.Wrapf(err, "rtc server")
	}
	defer webRTCProxyServer.Close()

	httpAPIProxyServer := b.newHTTPAPIProxyServer(environment, gracefulQuitTimeout, webRTCProxyServer)
	if err := httpAPIProxyServer.Run(ctx); err != nil {
		return errors.Wrapf(err, "http api server")
	}
	defer httpAPIProxyServer.Close()

	srsSRTProxyServer := b.newSRSSRTProxyServer(environment, loadBalancer)
	if err := srsSRTProxyServer.Run(ctx); err != nil {
		return errors.Wrapf(err, "srt server")
	}
	defer srsSRTProxyServer.Close()

	systemAPI := b.newSystemAPI(environment, loadBalancer, gracefulQuitTimeout)
	if err := systemAPI.Run(ctx); err != nil {
		return errors.Wrapf(err, "system api server")
	}
	defer systemAPI.Close()

	httpStreamProxyServer := b.newHTTPStreamProxyServer(environment, loadBalancer, gracefulQuitTimeout)
	if err := httpStreamProxyServer.Run(ctx); err != nil {
		return errors.Wrapf(err, "http server")
	}
	defer httpStreamProxyServer.Close()

	<-ctx.Done()

	return nil
}
