// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package redisclient

import (
	"context"
	"time"

	// Use v8 because we use Go 1.16+, while v9 requires Go 1.18+
	"github.com/go-redis/redis/v8"
)

// RedisClient is the subset of *redis.Client methods used by callers in this
// codebase. Declared as an interface so tests can substitute a fake without
// standing up a real Redis server. *redis.Client satisfies this interface
// directly.
type RedisClient interface {
	Ping(ctx context.Context) *redis.StatusCmd
	Get(ctx context.Context, key string) *redis.StringCmd
	Set(ctx context.Context, key string, value interface{}, expiration time.Duration) *redis.StatusCmd
	String() string
}

// New connects to a Redis server at addr (host:port) with the given password
// and database index. Returns a RedisClient satisfied by *redis.Client.
func New(addr, password string, db int) RedisClient {
	return redis.NewClient(&redis.Options{
		Addr:     addr,
		Password: password,
		DB:       db,
	})
}
