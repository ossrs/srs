// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package redisclient

import (
	"context"
	"time"

	"github.com/go-redis/redis/v8"
)

// An interface, so a test can use a fake instead of a real Redis server.
type RedisClient interface {
	Ping(ctx context.Context) *redis.StatusCmd
	Get(ctx context.Context, key string) *redis.StringCmd
	Set(ctx context.Context, key string, value interface{}, expiration time.Duration) *redis.StatusCmd
	String() string
}

func New(addr, password string, db int) RedisClient {
	return redis.NewClient(&redis.Options{
		Addr:     addr,
		Password: password,
		DB:       db,
	})
}
