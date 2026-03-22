// Copyright (c) 2025 Winlin
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
	"srsx/internal/protocol"
	"srsx/internal/signal"
	"srsx/internal/version"
)

// Bootstrap defines the interface for application bootstrap operations.
type Bootstrap interface {
	// Start initializes the context with logger and signal handlers, then runs the bootstrap.
	// Returns any error encountered during startup.
	Start(ctx context.Context) error

	// Run initializes and starts all proxy servers and the load balancer.
	// It blocks until the context is cancelled.
	Run(ctx context.Context) error
}

// bootstrapImpl implements the Bootstrap interface.
type bootstrapImpl struct{}

// NewBootstrap creates a new Bootstrap instance.
func NewBootstrap() Bootstrap {
	return &bootstrapImpl{}
}

// Start initializes the context with logger and signal handlers, then runs the bootstrap.
// Returns any error encountered during startup.
func (b *bootstrapImpl) Start(ctx context.Context) error {
	ctx = logger.WithContext(ctx)
	logger.Df(ctx, "%v/%v started", version.Signature(), version.Version())

	// Install signals.
	ctx, cancel := context.WithCancel(ctx)
	signal.InstallSignals(ctx, cancel)

	// Run the main loop, ignore the user cancel error.
	err := b.Run(ctx)
	if err != nil && ctx.Err() != context.Canceled {
		logger.Ef(ctx, "main: %+v", err)
		return err
	}

	logger.Df(ctx, "%v done", version.Signature())
	return nil
}

// Run initializes and starts all proxy servers and the load balancer.
// It blocks until the context is cancelled.
func (b *bootstrapImpl) Run(ctx context.Context) error {
	// Setup the environment variables.
	environment, err := env.NewEnvironment(ctx)
	if err != nil {
		return errors.Wrapf(err, "create environment")
	}

	// When cancelled, the program is forced to exit due to a timeout. Normally, this doesn't occur
	// because the main thread exits after the context is cancelled. However, sometimes the main thread
	// may be blocked for some reason, so a forced exit is necessary to ensure the program terminates.
	if err := signal.InstallForceQuit(ctx, environment); err != nil {
		return errors.Wrapf(err, "install force quit")
	}

	// Start the Go pprof if enabled.
	debug.HandleGoPprof(ctx, environment)

	// Initialize the load balancer.
	if err := b.initializeLoadBalancer(ctx, environment); err != nil {
		return err
	}

	// Parse the gracefully quit timeout.
	gracefulQuitTimeout, err := time.ParseDuration(environment.GraceQuitTimeout())
	if err != nil {
		return errors.Wrapf(err, "parse gracefully quit timeout")
	}

	// Start all servers and block until context is cancelled.
	return b.startServers(ctx, environment, gracefulQuitTimeout)
}

// initializeLoadBalancer sets up the load balancer based on configuration.
func (b *bootstrapImpl) initializeLoadBalancer(ctx context.Context, environment env.Environment) error {
	switch environment.LoadBalancerType() {
	case "redis":
		lb.SrsLoadBalancer = lb.NewRedisLoadBalancer(environment)
	default:
		lb.SrsLoadBalancer = lb.NewMemoryLoadBalancer(environment)
	}

	if err := lb.SrsLoadBalancer.Initialize(ctx); err != nil {
		return errors.Wrapf(err, "initialize srs load balancer")
	}

	return nil
}

// startServers initializes and starts all protocol servers.
func (b *bootstrapImpl) startServers(ctx context.Context, environment env.Environment, gracefulQuitTimeout time.Duration) error {
	// Start the RTMP server.
	srsRTMPServer := protocol.NewSRSRTMPServer(environment)
	if err := srsRTMPServer.Run(ctx); err != nil {
		return errors.Wrapf(err, "rtmp server")
	}
	defer srsRTMPServer.Close()

	// Start the WebRTC server.
	srsWebRTCServer := protocol.NewSRSWebRTCServer(environment)
	if err := srsWebRTCServer.Run(ctx); err != nil {
		return errors.Wrapf(err, "rtc server")
	}
	defer srsWebRTCServer.Close()

	// Start the HTTP API server.
	srsHTTPAPIServer := protocol.NewSRSHTTPAPIServer(environment, gracefulQuitTimeout, srsWebRTCServer)
	if err := srsHTTPAPIServer.Run(ctx); err != nil {
		return errors.Wrapf(err, "http api server")
	}
	defer srsHTTPAPIServer.Close()

	// Start the SRT server.
	srsSRTServer := protocol.NewSRSSRTServer(environment)
	if err := srsSRTServer.Run(ctx); err != nil {
		return errors.Wrapf(err, "srt server")
	}
	defer srsSRTServer.Close()

	// Start the System API server.
	systemAPI := protocol.NewSystemAPI(environment, gracefulQuitTimeout)
	if err := systemAPI.Run(ctx); err != nil {
		return errors.Wrapf(err, "system api server")
	}
	defer systemAPI.Close()

	// Start the HTTP web server.
	srsHTTPStreamServer := protocol.NewSRSHTTPStreamServer(environment, gracefulQuitTimeout)
	if err := srsHTTPStreamServer.Run(ctx); err != nil {
		return errors.Wrapf(err, "http server")
	}
	defer srsHTTPStreamServer.Close()

	// Wait for the main loop to quit.
	<-ctx.Done()

	return nil
}
