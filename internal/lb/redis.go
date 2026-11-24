// Copyright (c) 2026 Winlin
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

	"srsx/internal/env"
	"srsx/internal/errors"
	"srsx/internal/logger"
	"srsx/internal/redisclient"
)

type redisLoadBalancer struct {
	environment env.ProxyEnvironment
	rdb         redisclient.RedisClient
	// newClient and keepaliveInterval are fields, not package globals, so
	// concurrent tests can each set their own.
	newClient         func(addr, password string, db int) redisclient.RedisClient
	keepaliveInterval time.Duration
	originServerTTL   time.Duration
}

func NewRedisLoadBalancer(environment env.ProxyEnvironment) OriginLoadBalancer {
	return &redisLoadBalancer{
		environment:       environment,
		newClient:         redisclient.New,
		keepaliveInterval: 30 * time.Second,
	}
}

func (v *redisLoadBalancer) Initialize(ctx context.Context) error {
	originServerTTL, err := parseOriginServerTTL(v.environment.OriginServerTTL())
	if err != nil {
		return err
	}
	v.originServerTTL = originServerTTL

	redisDatabase, err := strconv.Atoi(v.environment.RedisDB())
	if err != nil {
		return errors.Wrapf(err, "invalid PROXY_REDIS_DB %v", v.environment.RedisDB())
	}

	rdb := v.newClient(
		fmt.Sprintf("%v:%v", v.environment.RedisHost(), v.environment.RedisPort()),
		v.environment.RedisPassword(),
		redisDatabase,
	)
	v.rdb = rdb

	if err := rdb.Ping(ctx).Err(); err != nil {
		return errors.Wrapf(err, "unable to connect to redis %v", rdb.String())
	}
	logger.Debug(ctx, "RedisLB: connected to redis %v ok", rdb.String())

	server, err := NewDefaultOriginServerForDebugging(v.environment)
	if err != nil {
		return errors.Wrapf(err, "initialize default SRS")
	}

	if server != nil {
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
		logger.Debug(ctx, "RedisLB: Initialize default SRS media server, %+v", server)
	}
	return nil
}

func (v *redisLoadBalancer) Update(ctx context.Context, server *OriginServer) error {
	b, err := json.Marshal(server)
	if err != nil {
		return errors.Wrapf(err, "marshal server %+v", server)
	}

	key := v.redisKeyServer(server.ID())
	if err = v.rdb.Set(ctx, key, b, v.originServerTTL).Err(); err != nil {
		return errors.Wrapf(err, "set key=%v server %+v", key, server)
	}

	var serverKeys []string
	if b, err := v.rdb.Get(ctx, v.redisKeyServers()).Bytes(); err == nil {
		if err := json.Unmarshal(b, &serverKeys); err != nil {
			return errors.Wrapf(err, "unmarshal key=%v servers %v", v.redisKeyServers(), string(b))
		}
	}

	for i := len(serverKeys) - 1; i >= 0; i-- {
		if _, err := v.rdb.Get(ctx, serverKeys[i]).Bytes(); err != nil {
			serverKeys = append(serverKeys[:i], serverKeys[i+1:]...)
		}
	}

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

	b, err = json.Marshal(serverKeys)
	if err != nil {
		return errors.Wrapf(err, "marshal servers %+v", serverKeys)
	}
	if err = v.rdb.Set(ctx, v.redisKeyServers(), b, 0).Err(); err != nil {
		return errors.Wrapf(err, "set key=%v servers %+v", v.redisKeyServers(), serverKeys)
	}

	return nil
}

func (v *redisLoadBalancer) Pick(ctx context.Context, streamURL string) (*OriginServer, error) {
	key := v.redisKeyURL(streamURL)

	if serverKey, err := v.rdb.Get(ctx, key).Result(); err == nil {
		if b, err := v.rdb.Get(ctx, serverKey).Bytes(); err == nil && len(b) > 0 {
			var server OriginServer
			if err := json.Unmarshal(b, &server); err != nil {
				return nil, errors.Wrapf(err, "unmarshal key=%v server %v", key, string(b))
			}

			// TODO: If server fail, we should migrate the streams to another server.
			return &server, nil
		}
	}

	var serverKeys []string
	if b, err := v.rdb.Get(ctx, v.redisKeyServers()).Bytes(); err == nil {
		if err := json.Unmarshal(b, &serverKeys); err != nil {
			return nil, errors.Wrapf(err, "unmarshal key=%v servers %v", v.redisKeyServers(), string(b))
		}
	}

	if len(serverKeys) == 0 {
		return nil, fmt.Errorf("no server available for %v", streamURL)
	}

	// Redis expires the key of a dead server, but the server list keeps it until
	// the next Update.
	var serverKey string
	var server OriginServer
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

	if err := v.rdb.Set(ctx, key, []byte(serverKey), 0).Err(); err != nil {
		return nil, errors.Wrapf(err, "set key=%v server %v", key, serverKey)
	}

	return &server, nil
}

func (v *redisLoadBalancer) LoadHLSBySPBHID(ctx context.Context, spbhid string) (HLSPlayStream, error) {
	key := v.redisKeySPBHID(spbhid)

	b, err := v.rdb.Get(ctx, key).Bytes()
	if err != nil {
		return nil, errors.Wrapf(err, "get key=%v HLS", key)
	}

	var actual map[string]interface{}
	if err := json.Unmarshal(b, &actual); err != nil {
		return nil, errors.Wrapf(err, "unmarshal key=%v HLS %v", key, string(b))
	}

	// JSON cannot rebuild the concrete type behind the interface.
	return nil, errors.Errorf("Redis load balancer cannot deserialize interface types")
}

func (v *redisLoadBalancer) LoadOrStoreHLS(ctx context.Context, streamURL string, value HLSPlayStream) (HLSPlayStream, error) {
	b, err := json.Marshal(value)
	if err != nil {
		return nil, errors.Wrapf(err, "marshal HLS %v", value)
	}

	key := v.redisKeyHLS(streamURL)
	if err = v.rdb.Set(ctx, key, b, HLSAliveDuration).Err(); err != nil {
		return nil, errors.Wrapf(err, "set key=%v HLS %v", key, value)
	}

	key2 := v.redisKeySPBHID(value.GetSPBHID())
	if err := v.rdb.Set(ctx, key2, b, HLSAliveDuration).Err(); err != nil {
		return nil, errors.Wrapf(err, "set key=%v HLS %v", key2, value)
	}

	return value, nil
}

func (v *redisLoadBalancer) StoreWebRTC(ctx context.Context, streamURL string, value RTCConnection) error {
	b, err := json.Marshal(value)
	if err != nil {
		return errors.Wrapf(err, "marshal WebRTC %v", value)
	}

	key := v.redisKeyRTC(streamURL)
	if err = v.rdb.Set(ctx, key, b, RTCAliveDuration).Err(); err != nil {
		return errors.Wrapf(err, "set key=%v WebRTC %v", key, value)
	}

	key2 := v.redisKeyUfrag(value.GetUfrag())
	if err := v.rdb.Set(ctx, key2, b, RTCAliveDuration).Err(); err != nil {
		return errors.Wrapf(err, "set key=%v WebRTC %v", key2, value)
	}

	return nil
}

func (v *redisLoadBalancer) LoadWebRTCByUfrag(ctx context.Context, ufrag string) (RTCConnection, error) {
	key := v.redisKeyUfrag(ufrag)

	b, err := v.rdb.Get(ctx, key).Bytes()
	if err != nil {
		return nil, errors.Wrapf(err, "get key=%v WebRTC", key)
	}

	var actual map[string]interface{}
	if err := json.Unmarshal(b, &actual); err != nil {
		return nil, errors.Wrapf(err, "unmarshal key=%v WebRTC %v", key, string(b))
	}

	// JSON cannot rebuild the concrete type behind the interface.
	return nil, errors.Errorf("Redis load balancer cannot deserialize interface types")
}

// Redis expires both keys of the session after RTCAliveDuration.
func (v *redisLoadBalancer) DeleteWebRTC(ctx context.Context, streamURL string, value RTCConnection) error {
	return nil
}

func (v *redisLoadBalancer) redisKeyUfrag(ufrag string) string {
	return v.redisKey(fmt.Sprintf("srs-proxy-ufrag:%v", ufrag))
}

func (v *redisLoadBalancer) redisKeyRTC(streamURL string) string {
	return v.redisKey(fmt.Sprintf("srs-proxy-rtc:%v", streamURL))
}

func (v *redisLoadBalancer) redisKeySPBHID(spbhid string) string {
	return v.redisKey(fmt.Sprintf("srs-proxy-spbhid:%v", spbhid))
}

func (v *redisLoadBalancer) redisKeyHLS(streamURL string) string {
	return v.redisKey(fmt.Sprintf("srs-proxy-hls:%v", streamURL))
}

func (v *redisLoadBalancer) redisKeyURL(streamURL string) string {
	return v.redisKey(fmt.Sprintf("srs-proxy-url:%v", streamURL))
}

func (v *redisLoadBalancer) redisKeyServer(serverID string) string {
	return v.redisKey(fmt.Sprintf("srs-proxy-server:%v", serverID))
}

func (v *redisLoadBalancer) redisKeyServers() string {
	return v.redisKey("srs-proxy-all-servers")
}

func (v *redisLoadBalancer) redisKey(key string) string {
	if prefix := v.environment.RedisKeyPrefix(); prefix != "" {
		return fmt.Sprintf("%v:%v", prefix, key)
	}
	return key
}
