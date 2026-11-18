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

type memoryLoadBalancer struct {
	environment  env.ProxyEnvironment
	servers      sync.Map[string, *OriginServer]
	picked       sync.Map[string, *OriginServer]
	hlsStreamURL sync.Map[string, HLSPlayStream]
	hlsSPBHID    sync.Map[string, HLSPlayStream]
	rtcStreamURL sync.Map[string, RTCConnection]
	rtcUfrag     sync.Map[string, RTCConnection]
	// A field, not a package global, so concurrent tests can each set their own.
	keepaliveInterval time.Duration
	originServerTTL   time.Duration
}

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
	if server, ok := v.picked.Load(streamURL); ok {
		return server, nil
	}

	var servers []*OriginServer
	v.servers.Range(func(key string, server *OriginServer) bool {
		if time.Since(server.UpdatedAt) < v.originServerTTL {
			servers = append(servers, server)
		}
		return true
	})

	if len(servers) == 0 {
		v.servers.Range(func(key string, server *OriginServer) bool {
			servers = append(servers, server)
			return true
		})
	}

	if len(servers) == 0 {
		return nil, fmt.Errorf("no server available for %v", streamURL)
	}

	server := servers[rand.Intn(len(servers))]
	v.picked.Store(streamURL, server)
	return server, nil
}

func (v *memoryLoadBalancer) LoadHLSBySPBHID(ctx context.Context, spbhid string) (HLSPlayStream, error) {
	if actual, ok := v.hlsSPBHID.Load(spbhid); !ok {
		return nil, errors.Errorf("no HLS streaming for SPBHID %v", spbhid)
	} else {
		return actual, nil
	}
}

func (v *memoryLoadBalancer) LoadOrStoreHLS(ctx context.Context, streamURL string, value HLSPlayStream) (HLSPlayStream, error) {
	actual, _ := v.hlsStreamURL.LoadOrStore(streamURL, value)
	if actual == nil {
		return nil, errors.Errorf("load or store HLS streaming for %v failed", streamURL)
	}

	v.hlsSPBHID.Store(value.GetSPBHID(), actual)

	return actual, nil
}

func (v *memoryLoadBalancer) StoreWebRTC(ctx context.Context, streamURL string, value RTCConnection) error {
	v.rtcStreamURL.Store(streamURL, value)

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
