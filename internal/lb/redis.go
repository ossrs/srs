// Copyright (c) 2025 Winlin
//
// SPDX-License-Identifier: MIT
package lb

import (
	"context"
	"encoding/json"
	"fmt"
	"math/rand"
	"strconv"
	"time"

	// Use v8 because we use Go 1.16+, while v9 requires Go 1.18+
	"github.com/go-redis/redis/v8"

	"srsx/internal/env"
	"srsx/internal/errors"
	"srsx/internal/logger"
)

// RedisLoadBalancer stores state in Redis.
type RedisLoadBalancer struct {
	// The environment interface.
	environment env.Environment
	// The redis client sdk.
	rdb *redis.Client
}

// NewRedisLoadBalancer creates a new Redis-based load balancer.
func NewRedisLoadBalancer(environment env.Environment) SRSLoadBalancer {
	return &RedisLoadBalancer{
		environment: environment,
	}
}

func (v *RedisLoadBalancer) Initialize(ctx context.Context) error {
	redisDatabase, err := strconv.Atoi(v.environment.RedisDB())
	if err != nil {
		return errors.Wrapf(err, "invalid PROXY_REDIS_DB %v", v.environment.RedisDB())
	}

	rdb := redis.NewClient(&redis.Options{
		Addr:     fmt.Sprintf("%v:%v", v.environment.RedisHost(), v.environment.RedisPort()),
		Password: v.environment.RedisPassword(),
		DB:       redisDatabase,
	})
	v.rdb = rdb

	if err := rdb.Ping(ctx).Err(); err != nil {
		return errors.Wrapf(err, "unable to connect to redis %v", rdb.String())
	}
	logger.Df(ctx, "RedisLB: connected to redis %v ok", rdb.String())

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
		logger.Df(ctx, "RedisLB: Initialize default SRS media server, %+v", server)
	}
	return nil
}

func (v *RedisLoadBalancer) Update(ctx context.Context, server *SRSServer) error {
	b, err := json.Marshal(server)
	if err != nil {
		return errors.Wrapf(err, "marshal server %+v", server)
	}

	key := v.redisKeyServer(server.ID())
	if err = v.rdb.Set(ctx, key, b, ServerAliveDuration).Err(); err != nil {
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
	for i := len(serverKeys) - 1; i >= 0; i-- {
		if _, err := v.rdb.Get(ctx, serverKeys[i]).Bytes(); err != nil {
			serverKeys = append(serverKeys[:i], serverKeys[i+1:]...)
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

func (v *RedisLoadBalancer) Pick(ctx context.Context, streamURL string) (*SRSServer, error) {
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
	// random pick one that is always available. Use global rand which is thread-safe since Go 1.20.
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

func (v *RedisLoadBalancer) LoadHLSBySPBHID(ctx context.Context, spbhid string) (HLSPlayStream, error) {
	key := v.redisKeySPBHID(spbhid)

	b, err := v.rdb.Get(ctx, key).Bytes()
	if err != nil {
		return nil, errors.Wrapf(err, "get key=%v HLS", key)
	}

	// Store the raw JSON bytes that will be unmarshaled by the concrete type
	// The caller will need to handle the deserialization
	var actual map[string]interface{}
	if err := json.Unmarshal(b, &actual); err != nil {
		return nil, errors.Wrapf(err, "unmarshal key=%v HLS %v", key, string(b))
	}

	// Return nil for now - Redis LB needs the concrete type to properly deserialize
	// This is a limitation of using Redis with interfaces
	return nil, errors.Errorf("Redis load balancer cannot deserialize interface types")
}

func (v *RedisLoadBalancer) LoadOrStoreHLS(ctx context.Context, streamURL string, value HLSPlayStream) (HLSPlayStream, error) {
	b, err := json.Marshal(value)
	if err != nil {
		return nil, errors.Wrapf(err, "marshal HLS %v", value)
	}

	key := v.redisKeyHLS(streamURL)
	if err = v.rdb.Set(ctx, key, b, HLSAliveDuration).Err(); err != nil {
		return nil, errors.Wrapf(err, "set key=%v HLS %v", key, value)
	}

	// Get SPBHID from value
	key2 := v.redisKeySPBHID(value.GetSPBHID())
	if err := v.rdb.Set(ctx, key2, b, HLSAliveDuration).Err(); err != nil {
		return nil, errors.Wrapf(err, "set key=%v HLS %v", key2, value)
	}

	// Return the same value since we just stored it
	return value, nil
}

func (v *RedisLoadBalancer) StoreWebRTC(ctx context.Context, streamURL string, value RTCConnection) error {
	b, err := json.Marshal(value)
	if err != nil {
		return errors.Wrapf(err, "marshal WebRTC %v", value)
	}

	key := v.redisKeyRTC(streamURL)
	if err = v.rdb.Set(ctx, key, b, RTCAliveDuration).Err(); err != nil {
		return errors.Wrapf(err, "set key=%v WebRTC %v", key, value)
	}

	// Get Ufrag from value
	key2 := v.redisKeyUfrag(value.GetUfrag())
	if err := v.rdb.Set(ctx, key2, b, RTCAliveDuration).Err(); err != nil {
		return errors.Wrapf(err, "set key=%v WebRTC %v", key2, value)
	}

	return nil
}

func (v *RedisLoadBalancer) LoadWebRTCByUfrag(ctx context.Context, ufrag string) (RTCConnection, error) {
	key := v.redisKeyUfrag(ufrag)

	b, err := v.rdb.Get(ctx, key).Bytes()
	if err != nil {
		return nil, errors.Wrapf(err, "get key=%v WebRTC", key)
	}

	// Return nil for now - Redis LB needs the concrete type to properly deserialize
	// This is a limitation of using Redis with interfaces
	var actual map[string]interface{}
	if err := json.Unmarshal(b, &actual); err != nil {
		return nil, errors.Wrapf(err, "unmarshal key=%v WebRTC %v", key, string(b))
	}

	return nil, errors.Errorf("Redis load balancer cannot deserialize interface types")
}

func (v *RedisLoadBalancer) redisKeyUfrag(ufrag string) string {
	return fmt.Sprintf("srs-proxy-ufrag:%v", ufrag)
}

func (v *RedisLoadBalancer) redisKeyRTC(streamURL string) string {
	return fmt.Sprintf("srs-proxy-rtc:%v", streamURL)
}

func (v *RedisLoadBalancer) redisKeySPBHID(spbhid string) string {
	return fmt.Sprintf("srs-proxy-spbhid:%v", spbhid)
}

func (v *RedisLoadBalancer) redisKeyHLS(streamURL string) string {
	return fmt.Sprintf("srs-proxy-hls:%v", streamURL)
}

func (v *RedisLoadBalancer) redisKeyServer(serverID string) string {
	return fmt.Sprintf("srs-proxy-server:%v", serverID)
}

func (v *RedisLoadBalancer) redisKeyServers() string {
	return fmt.Sprintf("srs-proxy-all-servers")
}
