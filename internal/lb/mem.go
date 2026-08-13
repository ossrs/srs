// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package lb

import (
	"context"
	"fmt"
	"math/rand"
	"time"

	"srsx/internal/env"
	"srsx/internal/errors"
	"srsx/internal/logger"
	"srsx/internal/sync"
)

// memoryLoadBalancer stores state in memory.
type memoryLoadBalancer struct {
	// The environment interface.
	environment env.ProxyEnvironment
	// All available SRS servers, key is server ID.
	servers sync.Map[string, *OriginServer]
	// The picked server to service client by specified stream URL, key is stream url.
	picked sync.Map[string, *OriginServer]
	// The HLS streaming, key is stream URL.
	hlsStreamURL sync.Map[string, HLSPlayStream]
	// The HLS streaming, key is SPBHID.
	hlsSPBHID sync.Map[string, HLSPlayStream]
	// The WebRTC streaming, key is stream URL.
	rtcStreamURL sync.Map[string, RTCConnection]
	// The WebRTC streaming, key is ufrag.
	rtcUfrag sync.Map[string, RTCConnection]
	// keepaliveInterval is the period at which the default-backend keep-alive
	// goroutine re-Updates its registration. Struct field for test injection
	// (avoids racing a package global across concurrent tests).
	keepaliveInterval time.Duration
	// originServerTTL is the period during which an origin registration is
	// considered healthy.
	originServerTTL time.Duration
}

// NewMemoryLoadBalancer creates a new memory-based load balancer.
func NewMemoryLoadBalancer(environment env.ProxyEnvironment) OriginLoadBalancer {
	return &memoryLoadBalancer{
		environment:       environment,
		servers:           sync.NewMap[string, *OriginServer](),
		picked:            sync.NewMap[string, *OriginServer](),
		hlsStreamURL:      sync.NewMap[string, HLSPlayStream](),
		hlsSPBHID:         sync.NewMap[string, HLSPlayStream](),
		rtcStreamURL:      sync.NewMap[string, RTCConnection](),
		rtcUfrag:          sync.NewMap[string, RTCConnection](),
		keepaliveInterval: 30 * time.Second,
	}
}

func (v *memoryLoadBalancer) Initialize(ctx context.Context) error {
	originServerTTL, err := parseOriginServerTTL(v.environment.OriginServerTTL())
	if err != nil {
		return err
	}
	v.originServerTTL = originServerTTL

	server, err := NewDefaultOriginServerForDebugging(v.environment)
	if err != nil {
		return errors.Wrapf(err, "initialize default SRS")
	}

	if server == nil {
		return nil
	}

	if err := v.Update(ctx, server); err != nil {
		return errors.Wrapf(err, "update default SRS %+v", server)
	}

	// Keep alive.
	go func() {
		for {
			select {
			case <-ctx.Done():
				return
			case <-time.After(v.keepaliveInterval):
				if err := v.Update(ctx, server); err != nil {
					logger.Warn(ctx, "update default SRS %+v failed, %+v", server, err)
				}
			}
		}
	}()
	logger.Debug(ctx, "MemoryLB: Initialize default SRS media server, %+v", server)
	return nil
}

func (v *memoryLoadBalancer) Update(ctx context.Context, server *OriginServer) error {
	v.servers.Store(server.ID(), server)
	return nil
}

func (v *memoryLoadBalancer) Pick(ctx context.Context, streamURL string) (*OriginServer, error) {
	// Always proxy to the same server for the same stream URL.
	if server, ok := v.picked.Load(streamURL); ok {
		return server, nil
	}

	// Gather all servers that were alive within the last few seconds.
	var servers []*OriginServer
	v.servers.Range(func(key string, server *OriginServer) bool {
		if time.Since(server.UpdatedAt) < v.originServerTTL {
			servers = append(servers, server)
		}
		return true
	})

	// If no servers available, use all possible servers.
	if len(servers) == 0 {
		v.servers.Range(func(key string, server *OriginServer) bool {
			servers = append(servers, server)
			return true
		})
	}

	// No server found, failed.
	if len(servers) == 0 {
		return nil, fmt.Errorf("no server available for %v", streamURL)
	}

	// Pick a server randomly from servers. Use global rand which is thread-safe since Go 1.20.
	// For older Go versions, this is still safe as we're only reading from the servers slice.
	server := servers[rand.Intn(len(servers))]
	v.picked.Store(streamURL, server)
	return server, nil
}

func (v *memoryLoadBalancer) LoadHLSBySPBHID(ctx context.Context, spbhid string) (HLSPlayStream, error) {
	// Load the HLS streaming for the SPBHID, for TS files.
	if actual, ok := v.hlsSPBHID.Load(spbhid); !ok {
		return nil, errors.Errorf("no HLS streaming for SPBHID %v", spbhid)
	} else {
		return actual, nil
	}
}

func (v *memoryLoadBalancer) LoadOrStoreHLS(ctx context.Context, streamURL string, value HLSPlayStream) (HLSPlayStream, error) {
	// Update the HLS streaming for the stream URL, for M3u8.
	actual, _ := v.hlsStreamURL.LoadOrStore(streamURL, value)
	if actual == nil {
		return nil, errors.Errorf("load or store HLS streaming for %v failed", streamURL)
	}

	// Update the HLS streaming for the SPBHID, for TS files.
	v.hlsSPBHID.Store(value.GetSPBHID(), actual)

	return actual, nil
}

func (v *memoryLoadBalancer) StoreWebRTC(ctx context.Context, streamURL string, value RTCConnection) error {
	// Update the WebRTC streaming for the stream URL.
	v.rtcStreamURL.Store(streamURL, value)

	// Update the WebRTC streaming for the ufrag.
	v.rtcUfrag.Store(value.GetUfrag(), value)
	return nil
}

func (v *memoryLoadBalancer) LoadWebRTCByUfrag(ctx context.Context, ufrag string) (RTCConnection, error) {
	if actual, ok := v.rtcUfrag.Load(ufrag); !ok {
		return nil, errors.Errorf("no WebRTC streaming for ufrag %v", ufrag)
	} else {
		return actual, nil
	}
}
