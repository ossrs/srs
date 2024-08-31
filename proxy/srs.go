// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"encoding/json"
	"fmt"
	"math/rand"
	"os"
	"strconv"
	"strings"
	"time"

	// Use v8 because we use Go 1.16+, while v9 requires Go 1.18+
	"github.com/go-redis/redis/v8"

	"srs-proxy/errors"
	"srs-proxy/logger"
	"srs-proxy/sync"
)

// If server heartbeat in this duration, it's alive.
const srsServerAliveDuration = 300 * time.Second

type SRSServer struct {
	// The server IP.
	IP string `json:"ip,omitempty"`
	// The server device ID, configured by user.
	DeviceID string `json:"device_id,omitempty"`
	// The server id of SRS, store in file, may not change, mandatory.
	ServerID string `json:"server_id,omitempty"`
	// The service id of SRS, always change when restarted, mandatory.
	ServiceID string `json:"service_id,omitempty"`
	// The process id of SRS, always change when restarted, mandatory.
	PID string `json:"pid,omitempty"`
	// The RTMP listen endpoints.
	RTMP []string `json:"rtmp,omitempty"`
	// The HTTP Stream listen endpoints.
	HTTP []string `json:"http,omitempty"`
	// The HTTP API listen endpoints.
	API []string `json:"api,omitempty"`
	// The SRT server listen endpoints.
	SRT []string `json:"srt,omitempty"`
	// The RTC server listen endpoints.
	RTC []string `json:"rtc,omitempty"`
	// Last update time.
	UpdatedAt time.Time `json:"update_at,omitempty"`
}

func (v *SRSServer) ID() string {
	return fmt.Sprintf("%v-%v-%v", v.ServerID, v.ServiceID, v.PID)
}

func (v *SRSServer) String() string {
	return fmt.Sprintf("%v", v)
}

func (v *SRSServer) Format(f fmt.State, c rune) {
	switch c {
	case 'v', 's':
		if f.Flag('+') {
			var sb strings.Builder
			sb.WriteString(fmt.Sprintf("pid=%v, server=%v, service=%v", v.PID, v.ServerID, v.ServiceID))
			if v.DeviceID != "" {
				sb.WriteString(fmt.Sprintf(", device=%v", v.DeviceID))
			}
			if len(v.RTMP) > 0 {
				sb.WriteString(fmt.Sprintf(", rtmp=[%v]", strings.Join(v.RTMP, ",")))
			}
			if len(v.HTTP) > 0 {
				sb.WriteString(fmt.Sprintf(", http=[%v]", strings.Join(v.HTTP, ",")))
			}
			if len(v.API) > 0 {
				sb.WriteString(fmt.Sprintf(", api=[%v]", strings.Join(v.API, ",")))
			}
			if len(v.SRT) > 0 {
				sb.WriteString(fmt.Sprintf(", srt=[%v]", strings.Join(v.SRT, ",")))
			}
			if len(v.RTC) > 0 {
				sb.WriteString(fmt.Sprintf(", rtc=[%v]", strings.Join(v.RTC, ",")))
			}
			sb.WriteString(fmt.Sprintf(", update=%v", v.UpdatedAt.Format("2006-01-02 15:04:05.999")))
			fmt.Fprintf(f, "SRS ip=%v, id=%v, %v", v.IP, v.ID(), sb.String())
		} else {
			fmt.Fprintf(f, "SRS ip=%v, id=%v", v.IP, v.ID())
		}
	default:
		fmt.Fprintf(f, "%v, fmt=%%%c", v, c)
	}
}

func NewSRSServer(opts ...func(*SRSServer)) *SRSServer {
	v := &SRSServer{}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

// NewDefaultSRSForDebugging initialize the default SRS media server, for debugging only.
func NewDefaultSRSForDebugging() (*SRSServer, error) {
	if envDefaultBackendEnabled() != "on" {
		return nil, nil
	}

	if envDefaultBackendIP() == "" {
		return nil, fmt.Errorf("empty default backend ip")
	}
	if envDefaultBackendRTMP() == "" {
		return nil, fmt.Errorf("empty default backend rtmp")
	}

	server := NewSRSServer(func(srs *SRSServer) {
		srs.IP = envDefaultBackendIP()
		srs.RTMP = []string{envDefaultBackendRTMP()}
		srs.ServerID = fmt.Sprintf("default-%v", logger.GenerateContextID())
		srs.ServiceID = logger.GenerateContextID()
		srs.PID = fmt.Sprintf("%v", os.Getpid())
		srs.UpdatedAt = time.Now()
	})

	if envDefaultBackendHttp() != "" {
		server.HTTP = []string{envDefaultBackendHttp()}
	}
	return server, nil
}

// SRSLoadBalancer is the interface to load balance the SRS servers.
type SRSLoadBalancer interface {
	// Initialize the load balancer.
	Initialize(ctx context.Context) error
	// Update the backer server.
	Update(ctx context.Context, server *SRSServer) error
	// Pick a backend server for the specified stream URL.
	Pick(ctx context.Context, streamURL string) (*SRSServer, error)
}

// srsLoadBalancer is the global SRS load balancer.
var srsLoadBalancer SRSLoadBalancer

// srsMemoryLoadBalancer stores state in memory.
type srsMemoryLoadBalancer struct {
	// All available SRS servers, key is server ID.
	servers sync.Map[string, *SRSServer]
	// The picked server to servce client by specified stream URL, key is stream url.
	picked sync.Map[string, *SRSServer]
}

func NewMemoryLoadBalancer() SRSLoadBalancer {
	return &srsMemoryLoadBalancer{}
}

func (v *srsMemoryLoadBalancer) Initialize(ctx context.Context) error {
	if server, err := NewDefaultSRSForDebugging(); err != nil {
		return errors.Wrapf(err, "initialize default SRS")
	} else if server != nil {
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

func (v *srsMemoryLoadBalancer) Update(ctx context.Context, server *SRSServer) error {
	v.servers.Store(server.ID(), server)
	return nil
}

func (v *srsMemoryLoadBalancer) Pick(ctx context.Context, streamURL string) (*SRSServer, error) {
	// Always proxy to the same server for the same stream URL.
	if server, ok := v.picked.Load(streamURL); ok {
		return server, nil
	}

	// Gather all servers, alive in 60s ago.
	var servers []*SRSServer
	v.servers.Range(func(key string, server *SRSServer) bool {
		if time.Since(server.UpdatedAt) < srsServerAliveDuration {
			servers = append(servers, server)
		}
		return true
	})

	// If no servers available, use all possible servers.
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

	// Pick a server randomly from servers.
	server := servers[rand.Intn(len(servers))]
	v.picked.Store(streamURL, server)
	return server, nil
}

type srsRedisLoadBalancer struct {
	// The redis client sdk.
	rdb *redis.Client
}

func NewRedisLoadBalancer() SRSLoadBalancer {
	return &srsRedisLoadBalancer{}
}

func (v *srsRedisLoadBalancer) Initialize(ctx context.Context) error {
	redisDatabase, err := strconv.Atoi(envRedisDB())
	if err != nil {
		return errors.Wrapf(err, "invalid PROXY_REDIS_DB %v", envRedisDB())
	}

	rdb := redis.NewClient(&redis.Options{
		Addr:     fmt.Sprintf("%v:%v", envRedisHost(), envRedisPort()),
		Password: envRedisPassword(),
		DB:       redisDatabase,
	})
	v.rdb = rdb

	if err := rdb.Ping(ctx).Err(); err != nil {
		return errors.Wrapf(err, "unable to connect to redis %v", rdb.String())
	}
	logger.Df(ctx, "RedisLB: connected to redis %v ok", rdb.String())

	if server, err := NewDefaultSRSForDebugging(); err != nil {
		return errors.Wrapf(err, "initialize default SRS")
	} else if server != nil {
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
		logger.Df(ctx, "RedisLB: Initialize default SRS media server, %+v", server)
	}
	return nil
}

func (v *srsRedisLoadBalancer) Update(ctx context.Context, server *SRSServer) error {
	b, err := json.Marshal(server)
	if err != nil {
		return errors.Wrapf(err, "marshal server %+v", server)
	}

	key := fmt.Sprintf("srs-proxy-server:%v", server.ID())
	if err = v.rdb.Set(ctx, key, b, srsServerAliveDuration).Err(); err != nil {
		return errors.Wrapf(err, "set key=%v server %+v", key, server)
	}

	// Query all servers from redis, in json string.
	var serverKeys []string
	if b, err := v.rdb.Get(ctx, v.redisKeyServers()).Bytes(); err == nil {
		if err := json.Unmarshal(b, &serverKeys); err != nil {
			return errors.Wrapf(err, "unmarshal key=%v servers %v", v.redisKeyServers(), string(b))
		}
	}

	// Check each server expiration, if not exists in redis, remove from servers.
	for i, serverKey := range serverKeys {
		if _, err := v.rdb.Get(ctx, serverKey).Bytes(); err != nil {
			serverKeys = append(serverKeys[:i], serverKeys[i+1:]...)
			continue
		}
	}

	// Add server to servers if not exists.
	var found bool
	for _, serverKey := range serverKeys {
		if serverKey == key {
			found = true
			break
		}
	}
	if !found {
		serverKeys = append(serverKeys, key)
	}

	// Update all servers to redis.
	b, err = json.Marshal(serverKeys)
	if err != nil {
		return errors.Wrapf(err, "marshal servers %+v", serverKeys)
	}
	if err = v.rdb.Set(ctx, v.redisKeyServers(), b, 0).Err(); err != nil {
		return errors.Wrapf(err, "set key=%v servers %+v", v.redisKeyServers(), serverKeys)
	}

	return nil
}

func (v *srsRedisLoadBalancer) Pick(ctx context.Context, streamURL string) (*SRSServer, error) {
	key := fmt.Sprintf("srs-proxy-url:%v", streamURL)

	// Always proxy to the same server for the same stream URL.
	if serverKey, err := v.rdb.Get(ctx, key).Result(); err == nil {
		// If server not exists, ignore and pick another server for the stream URL.
		if b, err := v.rdb.Get(ctx, serverKey).Bytes(); err == nil && len(b) > 0 {
			var server SRSServer
			if err := json.Unmarshal(b, &server); err != nil {
				return nil, errors.Wrapf(err, "unmarshal key=%v server %v", key, string(b))
			}

			// TODO: If server fail, we should migrate the streams to another server.
			return &server, nil
		}
	}

	// Query all servers from redis, in json string.
	var serverKeys []string
	if b, err := v.rdb.Get(ctx, v.redisKeyServers()).Bytes(); err == nil {
		if err := json.Unmarshal(b, &serverKeys); err != nil {
			return nil, errors.Wrapf(err, "unmarshal key=%v servers %v", v.redisKeyServers(), string(b))
		}
	}

	// No server found, failed.
	if len(serverKeys) == 0 {
		return nil, fmt.Errorf("no server available for %v", streamURL)
	}

	// All server should be alive, if not, should have been removed by redis. So we only
	// random pick one that is always available.
	var serverKey string
	var server SRSServer
	for i := 0; i < 3; i++ {
		tryServerKey := serverKeys[rand.Intn(len(serverKeys))]
		b, err := v.rdb.Get(ctx, tryServerKey).Bytes()
		if err == nil && len(b) > 0 {
			if err := json.Unmarshal(b, &server); err != nil {
				return nil, errors.Wrapf(err, "unmarshal key=%v server %v", serverKey, string(b))
			}

			serverKey = tryServerKey
			break
		}
	}
	if serverKey == "" {
		return nil, errors.Errorf("no server available in %v for %v", serverKeys, streamURL)
	}

	// Update the picked server for the stream URL.
	if err := v.rdb.Set(ctx, key, []byte(serverKey), 0).Err(); err != nil {
		return nil, errors.Wrapf(err, "set key=%v server %v", key, serverKey)
	}

	return &server, nil
}

func (v *srsRedisLoadBalancer) redisKeyServers() string {
	return fmt.Sprintf("srs-proxy-servers-all")
}
