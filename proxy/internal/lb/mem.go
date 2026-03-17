// Copyright (c) 2025 Winlin
//
// SPDX-License-Identifier: MIT
package lb

import (
	"context"
	"fmt"
	"math/rand"
	stdsync "sync"
	"time"

	"proxy/internal/env"
	"proxy/internal/errors"
	"proxy/internal/logger"
	"proxy/internal/sync"
)

// MemoryLoadBalancer stores state in memory.
type MemoryLoadBalancer struct {
	// The environment interface.
	environment env.Environment
	// All available SRS servers, key is server ID.
	servers sync.Map[string, *SRSServer]
	// The picked server to service client by specified stream URL, key is stream url.
	picked sync.Map[string, *SRSServer]
	// Mutex to protect the Pick operation when reselecting servers.
	pickMutex stdsync.Mutex
	// The HLS streaming, key is stream URL.
	hlsStreamURL sync.Map[string, HLSPlayStream]
	// The HLS streaming, key is SPBHID.
	hlsSPBHID sync.Map[string, HLSPlayStream]
	// The WebRTC streaming, key is stream URL.
	rtcStreamURL sync.Map[string, RTCConnection]
	// The WebRTC streaming, key is ufrag.
	rtcUfrag sync.Map[string, RTCConnection]
}

// NewMemoryLoadBalancer creates a new memory-based load balancer.
func NewMemoryLoadBalancer(environment env.Environment) SRSLoadBalancer {
	return &MemoryLoadBalancer{
		environment: environment,
	}
}

func (v *MemoryLoadBalancer) Initialize(ctx context.Context) error {
	server, err := NewDefaultSRSForDebugging(v.environment)
	if err != nil {
		return errors.Wrapf(err, "initialize default SRS")
	}

	if server != nil {
		if err := v.Update(ctx, server); err != nil {
			return errors.Wrapf(err, "update default SRS %+v", server)
		}

		// Keep alive.
		go func() {
			for {
				select {
				case <-ctx.Done():
					return
				case <-time.After(30 * time.Second):
					if err := v.Update(ctx, server); err != nil {
						logger.Wf(ctx, "update default SRS %+v failed, %+v", server, err)
					}
				}
			}
		}()
		logger.Df(ctx, "MemoryLB: Initialize default SRS media server, %+v", server)
	}
	return nil
}

func (v *MemoryLoadBalancer) Update(ctx context.Context, server *SRSServer) error {
	v.servers.Store(server.ID(), server)
	return nil
}

func (v *MemoryLoadBalancer) Pick(ctx context.Context, streamURL string) (*SRSServer, error) {
	// First check (without lock): fast path for healthy servers.
	// Try to load the previously picked server for this stream URL.
	if pickedServer, ok := v.picked.Load(streamURL); ok {
		// Check if the server still exists and is healthy by getting its latest state from servers map.
		if actualServer, exists := v.servers.Load(pickedServer.ID()); exists {
			if time.Since(actualServer.UpdatedAt) < ServerAliveDuration {
				// Server is still healthy, return the latest server state.
				// Most requests will return here without acquiring the lock.
				return actualServer, nil
			}
		}
	}

	// Server is unhealthy or doesn't exist, need to pick a new one.
	// Acquire lock to ensure only one goroutine picks a new server at a time.
	v.pickMutex.Lock()
	defer v.pickMutex.Unlock()

	// Second check (with lock): another goroutine might have already updated the server.
	if pickedServer, ok := v.picked.Load(streamURL); ok {
		if actualServer, exists := v.servers.Load(pickedServer.ID()); exists {
			if time.Since(actualServer.UpdatedAt) < ServerAliveDuration {
				// Another goroutine has already picked a healthy server.
				return actualServer, nil
			}
		}
	}

	// Now we're certain we need to pick a new server.
	// Gather all servers that are alive within the last few seconds.
	var servers []*SRSServer
	v.servers.Range(func(key string, server *SRSServer) bool {
		if time.Since(server.UpdatedAt) < ServerAliveDuration {
			servers = append(servers, server)
		}
		return true
	})

	// If no healthy servers available, use all possible servers as fallback.
	if len(servers) == 0 {
		v.servers.Range(func(key string, server *SRSServer) bool {
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
	newServer := servers[rand.Intn(len(servers))]
	v.picked.Store(streamURL, newServer)

	return newServer, nil
}

func (v *MemoryLoadBalancer) LoadHLSBySPBHID(ctx context.Context, spbhid string) (HLSPlayStream, error) {
	// Load the HLS streaming for the SPBHID, for TS files.
	if actual, ok := v.hlsSPBHID.Load(spbhid); !ok {
		return nil, errors.Errorf("no HLS streaming for SPBHID %v", spbhid)
	} else {
		return actual, nil
	}
}

func (v *MemoryLoadBalancer) LoadOrStoreHLS(ctx context.Context, streamURL string, value HLSPlayStream) (HLSPlayStream, error) {
	// Update the HLS streaming for the stream URL, for M3u8.
	actual, _ := v.hlsStreamURL.LoadOrStore(streamURL, value)
	if actual == nil {
		return nil, errors.Errorf("load or store HLS streaming for %v failed", streamURL)
	}

	// Update the HLS streaming for the SPBHID, for TS files.
	v.hlsSPBHID.Store(value.GetSPBHID(), actual)

	return actual, nil
}

func (v *MemoryLoadBalancer) StoreWebRTC(ctx context.Context, streamURL string, value RTCConnection) error {
	// Update the WebRTC streaming for the stream URL.
	v.rtcStreamURL.Store(streamURL, value)

	// Update the WebRTC streaming for the ufrag.
	v.rtcUfrag.Store(value.GetUfrag(), value)
	return nil
}

func (v *MemoryLoadBalancer) LoadWebRTCByUfrag(ctx context.Context, ufrag string) (RTCConnection, error) {
	if actual, ok := v.rtcUfrag.Load(ufrag); !ok {
		return nil, errors.Errorf("no WebRTC streaming for ufrag %v", ufrag)
	} else {
		return actual, nil
	}
}
