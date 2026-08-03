// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package lb

import (
	"context"
	"encoding/json"
	"fmt"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/go-redis/redis/v8"

	"srsx/internal/env/envfakes"
	"srsx/internal/redisclient"
	"srsx/internal/redisclient/redisclientfakes"
)

// ----------------------------------------------------------------------------
// Helpers.
// ----------------------------------------------------------------------------

// statusCmd returns a *redis.StatusCmd that resolves to the given error.
func statusCmd(err error) *redis.StatusCmd {
	c := redis.NewStatusCmd(context.Background())
	if err != nil {
		c.SetErr(err)
	}
	return c
}

// stringOK returns a *redis.StringCmd that resolves to the given bytes.
func stringOK(b []byte) *redis.StringCmd {
	c := redis.NewStringCmd(context.Background())
	c.SetVal(string(b))
	return c
}

// stringErr returns a *redis.StringCmd that resolves to the given error.
func stringErr(err error) *redis.StringCmd {
	c := redis.NewStringCmd(context.Background())
	c.SetErr(err)
	return c
}

// withFakeClient returns a fresh *redisLoadBalancer whose newClient factory is
// wired to return the supplied fake. Each test gets its own instance, so
// concurrent tests cannot race on shared state.
func withFakeClient(env *envfakes.FakeProxyEnvironment, client redisclient.RedisClient) *redisLoadBalancer {
	lb := NewRedisLoadBalancer(env).(*redisLoadBalancer)
	lb.newClient = func(string, string, int) redisclient.RedisClient { return client }
	return lb
}

// newRedisLB constructs a redisLoadBalancer with a fake rdb already wired in.
// Used by tests that exercise methods other than Initialize.
func newRedisLB(rdb redisclient.RedisClient) *redisLoadBalancer {
	env := &envfakes.FakeProxyEnvironment{}
	lb := NewRedisLoadBalancer(env).(*redisLoadBalancer)
	lb.rdb = rdb
	return lb
}

// ----------------------------------------------------------------------------
// Constructor & Initialize.
// ----------------------------------------------------------------------------

func TestNewRedisLoadBalancer(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	if lb := NewRedisLoadBalancer(env); lb == nil {
		t.Fatal("NewRedisLoadBalancer returned nil")
	}
}

func TestRedisLB_Initialize_BadRedisDB(t *testing.T) {
	env := &envfakes.FakeProxyEnvironment{}
	env.RedisDBReturns("not-a-number")
	err := NewRedisLoadBalancer(env).Initialize(context.Background())
	if err == nil || !strings.Contains(err.Error(), "invalid PROXY_REDIS_DB") {
		t.Fatalf("expected Atoi error, got %v", err)
	}
}

func TestRedisLB_Initialize_PingFails(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	fake.PingReturns(statusCmd(fmt.Errorf("connection refused")))
	fake.StringReturns("Redis<fake>")

	env := &envfakes.FakeProxyEnvironment{}
	env.RedisDBReturns("0")
	err := withFakeClient(env, fake).Initialize(context.Background())
	if err == nil || !strings.Contains(err.Error(), "unable to connect to redis") {
		t.Fatalf("expected ping error, got %v", err)
	}
}

func TestRedisLB_Initialize_DefaultBackendDisabled(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	fake.PingReturns(statusCmd(nil))

	env := &envfakes.FakeProxyEnvironment{}
	env.RedisDBReturns("0")
	// DefaultBackendEnabled defaults to "" (not "on") => no server registered.
	if err := withFakeClient(env, fake).Initialize(context.Background()); err != nil {
		t.Fatalf("Initialize: %v", err)
	}
}

func TestRedisLB_Initialize_DefaultBackendError(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	fake.PingReturns(statusCmd(nil))

	env := &envfakes.FakeProxyEnvironment{}
	env.RedisDBReturns("0")
	env.DefaultBackendEnabledReturns("on")
	env.DefaultBackendIPReturns("") // triggers NewDefaultOriginServerForDebugging error
	err := withFakeClient(env, fake).Initialize(context.Background())
	if err == nil || !strings.Contains(err.Error(), "initialize default SRS") {
		t.Fatalf("expected default-SRS error, got %v", err)
	}
}

func TestRedisLB_Initialize_UpdateFails(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	fake.PingReturns(statusCmd(nil))
	fake.SetReturns(statusCmd(fmt.Errorf("set failed"))) // every Set fails

	env := &envfakes.FakeProxyEnvironment{}
	env.RedisDBReturns("0")
	env.DefaultBackendEnabledReturns("on")
	env.DefaultBackendIPReturns("1.2.3.4")
	env.DefaultBackendRTMPReturns(":1935")
	err := withFakeClient(env, fake).Initialize(context.Background())
	if err == nil || !strings.Contains(err.Error(), "update default SRS") {
		t.Fatalf("expected update error, got %v", err)
	}
}

func TestRedisLB_Initialize_Success(t *testing.T) {
	var setCalls atomic.Int32
	fake := &redisclientfakes.FakeRedisClient{}
	fake.PingReturns(statusCmd(nil))
	fake.SetStub = func(ctx context.Context, key string, value interface{}, ttl time.Duration) *redis.StatusCmd {
		setCalls.Add(1)
		return statusCmd(nil)
	}
	// Every Get returns redis.Nil-style error so the server list is treated as empty.
	fake.GetReturns(stringErr(fmt.Errorf("redis: nil")))

	env := &envfakes.FakeProxyEnvironment{}
	env.RedisDBReturns("0")
	env.DefaultBackendEnabledReturns("on")
	env.DefaultBackendIPReturns("1.2.3.4")
	env.DefaultBackendRTMPReturns(":1935")

	lb := withFakeClient(env, fake)
	lb.keepaliveInterval = time.Millisecond

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	if err := lb.Initialize(ctx); err != nil {
		t.Fatalf("Initialize: %v", err)
	}

	// Initial Update made 2 Set calls (server + server list). Wait long enough
	// for the keep-alive tick to issue more.
	deadline := time.Now().Add(200 * time.Millisecond)
	for time.Now().Before(deadline) && setCalls.Load() < 4 {
		time.Sleep(5 * time.Millisecond)
	}
	cancel()
	time.Sleep(10 * time.Millisecond)
	if setCalls.Load() < 4 {
		t.Fatalf("keep-alive did not tick: setCalls=%d", setCalls.Load())
	}
}

func TestRedisLB_Update_UsesConfiguredOriginServerTTL(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	fake.PingReturns(statusCmd(nil))
	fake.SetReturns(statusCmd(nil))
	fake.GetReturns(stringErr(redis.Nil))

	env := &envfakes.FakeProxyEnvironment{}
	env.RedisDBReturns("0")
	env.OriginServerTTLReturns("45s")
	lb := withFakeClient(env, fake)
	if err := lb.Initialize(context.Background()); err != nil {
		t.Fatalf("Initialize: %v", err)
	}

	server := &OriginServer{ServerID: "s", ServiceID: "v", PID: "1"}
	if err := lb.Update(context.Background(), server); err != nil {
		t.Fatalf("Update: %v", err)
	}
	if got := fake.SetCallCount(); got != 2 {
		t.Fatalf("Set call count = %d, want 2", got)
	}

	_, serverKey, _, serverTTL := fake.SetArgsForCall(0)
	if want := lb.redisKeyServer(server.ID()); serverKey != want {
		t.Fatalf("server key = %q, want %q", serverKey, want)
	}
	if serverTTL != 45*time.Second {
		t.Fatalf("server TTL = %v, want %v", serverTTL, 45*time.Second)
	}

	_, serversKey, _, serversTTL := fake.SetArgsForCall(1)
	if want := lb.redisKeyServers(); serversKey != want {
		t.Fatalf("server-index key = %q, want %q", serversKey, want)
	}
	if serversTTL != 0 {
		t.Fatalf("server-index TTL = %v, want no expiration", serversTTL)
	}
}

// ----------------------------------------------------------------------------
// Update.
// ----------------------------------------------------------------------------

func TestRedisLB_Update_SetServerFails(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	fake.SetReturns(statusCmd(fmt.Errorf("boom")))
	lb := newRedisLB(fake)
	err := lb.Update(context.Background(), &OriginServer{ServerID: "s", ServiceID: "v", PID: "1"})
	if err == nil || !strings.Contains(err.Error(), "set key=") {
		t.Fatalf("expected set-server error, got %v", err)
	}
}

func TestRedisLB_Update_FreshList(t *testing.T) {
	// No existing server list => Get for server-list key returns error.
	fake := &redisclientfakes.FakeRedisClient{}
	fake.SetReturns(statusCmd(nil))
	fake.GetReturns(stringErr(fmt.Errorf("nil")))

	lb := newRedisLB(fake)
	server := &OriginServer{ServerID: "s", ServiceID: "v", PID: "1"}
	if err := lb.Update(context.Background(), server); err != nil {
		t.Fatalf("Update: %v", err)
	}

	// Two Set calls: server + servers-list.
	if got := fake.SetCallCount(); got != 2 {
		t.Fatalf("Set call count=%d, want 2", got)
	}
	// The second Set value should be a JSON array containing the server key.
	_, _, value, _ := fake.SetArgsForCall(1)
	var keys []string
	if err := json.Unmarshal(value.([]byte), &keys); err != nil {
		t.Fatalf("server-list value not JSON: %v", err)
	}
	want := lb.redisKeyServer(server.ID())
	if len(keys) != 1 || keys[0] != want {
		t.Fatalf("server-list keys=%v, want [%q]", keys, want)
	}
}

func TestRedisLB_Update_PrunesDeadAndAppends(t *testing.T) {
	server := &OriginServer{ServerID: "s", ServiceID: "v", PID: "1"}

	fake := &redisclientfakes.FakeRedisClient{}
	fake.SetReturns(statusCmd(nil))

	// First Get: server-list, returns ["dead", "alive"].
	// Subsequent Gets: probe each key — "dead" missing, "alive" present.
	fake.GetStub = func(ctx context.Context, key string) *redis.StringCmd {
		if strings.HasSuffix(key, "all-servers") {
			b, _ := json.Marshal([]string{"dead", "alive"})
			return stringOK(b)
		}
		if key == "alive" {
			return stringOK([]byte("ok"))
		}
		return stringErr(fmt.Errorf("nil"))
	}

	lb := newRedisLB(fake)
	if err := lb.Update(context.Background(), server); err != nil {
		t.Fatalf("Update: %v", err)
	}

	// Inspect the server-list Set call: should contain "alive" (kept) and the
	// new server key (appended); "dead" should be pruned.
	_, _, value, _ := fake.SetArgsForCall(1)
	var keys []string
	if err := json.Unmarshal(value.([]byte), &keys); err != nil {
		t.Fatalf("not JSON: %v", err)
	}
	wantNew := lb.redisKeyServer(server.ID())
	if len(keys) != 2 || keys[0] != "alive" || keys[1] != wantNew {
		t.Fatalf("server-list keys=%v, want [alive, %q]", keys, wantNew)
	}
}

func TestRedisLB_Update_AlreadyInList(t *testing.T) {
	server := &OriginServer{ServerID: "s", ServiceID: "v", PID: "1"}

	fake := &redisclientfakes.FakeRedisClient{}
	fake.SetReturns(statusCmd(nil))
	lb := newRedisLB(fake)
	wantKey := lb.redisKeyServer(server.ID())

	fake.GetStub = func(ctx context.Context, key string) *redis.StringCmd {
		if strings.HasSuffix(key, "all-servers") {
			b, _ := json.Marshal([]string{wantKey})
			return stringOK(b)
		}
		return stringOK([]byte("ok"))
	}

	if err := lb.Update(context.Background(), server); err != nil {
		t.Fatalf("Update: %v", err)
	}
	_, _, value, _ := fake.SetArgsForCall(1)
	var keys []string
	_ = json.Unmarshal(value.([]byte), &keys)
	if len(keys) != 1 || keys[0] != wantKey {
		t.Fatalf("expected no duplication, got %v", keys)
	}
}

func TestRedisLB_Update_BadServerListJSON(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	fake.SetReturns(statusCmd(nil))
	fake.GetStub = func(ctx context.Context, key string) *redis.StringCmd {
		if strings.HasSuffix(key, "all-servers") {
			return stringOK([]byte("not-json"))
		}
		return stringErr(fmt.Errorf("nil"))
	}
	lb := newRedisLB(fake)
	err := lb.Update(context.Background(), &OriginServer{ServerID: "s", ServiceID: "v", PID: "1"})
	if err == nil || !strings.Contains(err.Error(), "unmarshal") {
		t.Fatalf("expected unmarshal error, got %v", err)
	}
}

func TestRedisLB_Update_SetServerListFails(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	// First Set ok (server), second Set fails (server list).
	fake.SetReturnsOnCall(0, statusCmd(nil))
	fake.SetReturnsOnCall(1, statusCmd(fmt.Errorf("set list failed")))
	fake.GetReturns(stringErr(fmt.Errorf("nil")))

	lb := newRedisLB(fake)
	err := lb.Update(context.Background(), &OriginServer{ServerID: "s", ServiceID: "v", PID: "1"})
	if err == nil || !strings.Contains(err.Error(), "set list failed") {
		t.Fatalf("expected server-list set error, got %v", err)
	}
}

// ----------------------------------------------------------------------------
// Pick.
// ----------------------------------------------------------------------------

func TestRedisLB_Pick_StickyHit(t *testing.T) {
	server := &OriginServer{ServerID: "a", ServiceID: "b", PID: "1"}
	serverJSON, _ := json.Marshal(server)

	fake := &redisclientfakes.FakeRedisClient{}
	fake.SetReturns(statusCmd(nil))
	lb := newRedisLB(fake)
	streamKey := "srs-proxy-url:url1"
	serverKey := lb.redisKeyServer(server.ID())

	fake.GetStub = func(ctx context.Context, key string) *redis.StringCmd {
		switch key {
		case streamKey:
			return stringOK([]byte(serverKey))
		case serverKey:
			return stringOK(serverJSON)
		}
		return stringErr(fmt.Errorf("nil"))
	}

	got, err := lb.Pick(context.Background(), "url1")
	if err != nil {
		t.Fatalf("Pick: %v", err)
	}
	if got.ID() != server.ID() {
		t.Fatalf("Pick returned %v, want %v", got, server)
	}
}

func TestRedisLB_Pick_StickyBadJSON(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	lb := newRedisLB(fake)
	streamKey := "srs-proxy-url:url1"

	fake.GetStub = func(ctx context.Context, key string) *redis.StringCmd {
		switch key {
		case streamKey:
			return stringOK([]byte("srv-key"))
		case "srv-key":
			return stringOK([]byte("not-json"))
		}
		return stringErr(fmt.Errorf("nil"))
	}

	_, err := lb.Pick(context.Background(), "url1")
	if err == nil || !strings.Contains(err.Error(), "unmarshal") {
		t.Fatalf("expected unmarshal error, got %v", err)
	}
}

func TestRedisLB_Pick_NoServersAvailable(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	// Sticky miss + server list missing.
	fake.GetReturns(stringErr(fmt.Errorf("nil")))
	lb := newRedisLB(fake)
	_, err := lb.Pick(context.Background(), "url1")
	if err == nil || !strings.Contains(err.Error(), "no server available") {
		t.Fatalf("expected no-server error, got %v", err)
	}
}

func TestRedisLB_Pick_BadServerListJSON(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	fake.GetStub = func(ctx context.Context, key string) *redis.StringCmd {
		if strings.HasSuffix(key, "all-servers") {
			return stringOK([]byte("not-json"))
		}
		return stringErr(fmt.Errorf("nil"))
	}
	lb := newRedisLB(fake)
	_, err := lb.Pick(context.Background(), "url1")
	if err == nil || !strings.Contains(err.Error(), "unmarshal") {
		t.Fatalf("expected unmarshal error, got %v", err)
	}
}

func TestRedisLB_Pick_AllProbesFail(t *testing.T) {
	// Server list contains one key, but probing it returns nil bytes (the
	// `len(b) > 0` guard rejects it). After 3 attempts, Pick errors out.
	fake := &redisclientfakes.FakeRedisClient{}
	fake.GetStub = func(ctx context.Context, key string) *redis.StringCmd {
		if strings.HasSuffix(key, "all-servers") {
			b, _ := json.Marshal([]string{"srv-key"})
			return stringOK(b)
		}
		// "srv-key" probe returns empty bytes — falls through the available check.
		if key == "srv-key" {
			return stringOK(nil)
		}
		return stringErr(fmt.Errorf("nil"))
	}
	lb := newRedisLB(fake)
	_, err := lb.Pick(context.Background(), "url1")
	if err == nil || !strings.Contains(err.Error(), "no server available in") {
		t.Fatalf("expected exhausted-probes error, got %v", err)
	}
}

func TestRedisLB_Pick_ScanSuccess(t *testing.T) {
	server := &OriginServer{ServerID: "a", ServiceID: "b", PID: "1"}
	serverJSON, _ := json.Marshal(server)

	fake := &redisclientfakes.FakeRedisClient{}
	fake.SetReturns(statusCmd(nil))
	lb := newRedisLB(fake)
	serverKey := lb.redisKeyServer(server.ID())

	fake.GetStub = func(ctx context.Context, key string) *redis.StringCmd {
		if strings.HasSuffix(key, "all-servers") {
			b, _ := json.Marshal([]string{serverKey})
			return stringOK(b)
		}
		if key == serverKey {
			return stringOK(serverJSON)
		}
		// Sticky lookup for the URL key misses.
		return stringErr(fmt.Errorf("nil"))
	}

	got, err := lb.Pick(context.Background(), "url1")
	if err != nil {
		t.Fatalf("Pick: %v", err)
	}
	if got.ID() != server.ID() {
		t.Fatalf("Pick returned %v", got)
	}
	// Pick should also store the picked-mapping.
	if fake.SetCallCount() != 1 {
		t.Fatalf("expected 1 Set call to store picked mapping, got %d", fake.SetCallCount())
	}
}

func TestRedisLB_Pick_ScanBadJSON(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	fake.GetStub = func(ctx context.Context, key string) *redis.StringCmd {
		if strings.HasSuffix(key, "all-servers") {
			b, _ := json.Marshal([]string{"srv-key"})
			return stringOK(b)
		}
		if key == "srv-key" {
			return stringOK([]byte("not-json"))
		}
		return stringErr(fmt.Errorf("nil"))
	}
	lb := newRedisLB(fake)
	_, err := lb.Pick(context.Background(), "url1")
	if err == nil || !strings.Contains(err.Error(), "unmarshal") {
		t.Fatalf("expected unmarshal error, got %v", err)
	}
}

func TestRedisLB_Pick_StoreMappingFails(t *testing.T) {
	server := &OriginServer{ServerID: "a", ServiceID: "b", PID: "1"}
	serverJSON, _ := json.Marshal(server)

	fake := &redisclientfakes.FakeRedisClient{}
	fake.SetReturns(statusCmd(fmt.Errorf("set failed")))
	lb := newRedisLB(fake)
	serverKey := lb.redisKeyServer(server.ID())

	fake.GetStub = func(ctx context.Context, key string) *redis.StringCmd {
		if strings.HasSuffix(key, "all-servers") {
			b, _ := json.Marshal([]string{serverKey})
			return stringOK(b)
		}
		if key == serverKey {
			return stringOK(serverJSON)
		}
		return stringErr(fmt.Errorf("nil"))
	}

	_, err := lb.Pick(context.Background(), "url1")
	if err == nil || !strings.Contains(err.Error(), "set failed") {
		t.Fatalf("expected set-mapping error, got %v", err)
	}
}

// ----------------------------------------------------------------------------
// LoadHLSBySPBHID and LoadWebRTCByUfrag — symmetric behavior.
// ----------------------------------------------------------------------------

func TestRedisLB_LoadHLSBySPBHID_GetFails(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	fake.GetReturns(stringErr(fmt.Errorf("nil")))
	lb := newRedisLB(fake)
	_, err := lb.LoadHLSBySPBHID(context.Background(), "abc")
	if err == nil || !strings.Contains(err.Error(), "get key=") {
		t.Fatalf("expected get error, got %v", err)
	}
}

func TestRedisLB_LoadHLSBySPBHID_BadJSON(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	fake.GetReturns(stringOK([]byte("not-json")))
	lb := newRedisLB(fake)
	_, err := lb.LoadHLSBySPBHID(context.Background(), "abc")
	if err == nil || !strings.Contains(err.Error(), "unmarshal") {
		t.Fatalf("expected unmarshal error, got %v", err)
	}
}

func TestRedisLB_LoadHLSBySPBHID_InterfaceLimitation(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	fake.GetReturns(stringOK([]byte(`{"foo":"bar"}`)))
	lb := newRedisLB(fake)
	_, err := lb.LoadHLSBySPBHID(context.Background(), "abc")
	if err == nil || !strings.Contains(err.Error(), "cannot deserialize") {
		t.Fatalf("expected interface limitation error, got %v", err)
	}
}

func TestRedisLB_LoadWebRTCByUfrag_GetFails(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	fake.GetReturns(stringErr(fmt.Errorf("nil")))
	lb := newRedisLB(fake)
	_, err := lb.LoadWebRTCByUfrag(context.Background(), "u")
	if err == nil || !strings.Contains(err.Error(), "get key=") {
		t.Fatalf("expected get error, got %v", err)
	}
}

func TestRedisLB_LoadWebRTCByUfrag_BadJSON(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	fake.GetReturns(stringOK([]byte("not-json")))
	lb := newRedisLB(fake)
	_, err := lb.LoadWebRTCByUfrag(context.Background(), "u")
	if err == nil || !strings.Contains(err.Error(), "unmarshal") {
		t.Fatalf("expected unmarshal error, got %v", err)
	}
}

func TestRedisLB_LoadWebRTCByUfrag_InterfaceLimitation(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	fake.GetReturns(stringOK([]byte(`{"foo":"bar"}`)))
	lb := newRedisLB(fake)
	_, err := lb.LoadWebRTCByUfrag(context.Background(), "u")
	if err == nil || !strings.Contains(err.Error(), "cannot deserialize") {
		t.Fatalf("expected interface limitation error, got %v", err)
	}
}

// ----------------------------------------------------------------------------
// LoadOrStoreHLS and StoreWebRTC.
// ----------------------------------------------------------------------------

func TestRedisLB_LoadOrStoreHLS_Success(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	fake.SetReturns(statusCmd(nil))
	lb := newRedisLB(fake)

	hls := &stubHLS{spbhid: "abc"}
	got, err := lb.LoadOrStoreHLS(context.Background(), "url1", hls)
	if err != nil {
		t.Fatalf("LoadOrStoreHLS: %v", err)
	}
	if got != hls {
		t.Fatalf("got %v, want input back", got)
	}
	if fake.SetCallCount() != 2 {
		t.Fatalf("expected 2 Set calls (URL + SPBHID), got %d", fake.SetCallCount())
	}
}

func TestRedisLB_LoadOrStoreHLS_FirstSetFails(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	fake.SetReturns(statusCmd(fmt.Errorf("boom")))
	lb := newRedisLB(fake)
	_, err := lb.LoadOrStoreHLS(context.Background(), "url1", &stubHLS{spbhid: "abc"})
	if err == nil || !strings.Contains(err.Error(), "boom") {
		t.Fatalf("expected error, got %v", err)
	}
}

func TestRedisLB_LoadOrStoreHLS_SecondSetFails(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	fake.SetReturnsOnCall(0, statusCmd(nil))
	fake.SetReturnsOnCall(1, statusCmd(fmt.Errorf("second boom")))
	lb := newRedisLB(fake)
	_, err := lb.LoadOrStoreHLS(context.Background(), "url1", &stubHLS{spbhid: "abc"})
	if err == nil || !strings.Contains(err.Error(), "second boom") {
		t.Fatalf("expected error, got %v", err)
	}
}

func TestRedisLB_StoreWebRTC_Success(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	fake.SetReturns(statusCmd(nil))
	lb := newRedisLB(fake)
	if err := lb.StoreWebRTC(context.Background(), "url1", &stubRTC{ufrag: "u"}); err != nil {
		t.Fatalf("StoreWebRTC: %v", err)
	}
	if fake.SetCallCount() != 2 {
		t.Fatalf("expected 2 Set calls (URL + Ufrag), got %d", fake.SetCallCount())
	}
}

func TestRedisLB_StoreWebRTC_FirstSetFails(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	fake.SetReturns(statusCmd(fmt.Errorf("boom")))
	lb := newRedisLB(fake)
	err := lb.StoreWebRTC(context.Background(), "url1", &stubRTC{ufrag: "u"})
	if err == nil || !strings.Contains(err.Error(), "boom") {
		t.Fatalf("expected error, got %v", err)
	}
}

func TestRedisLB_StoreWebRTC_SecondSetFails(t *testing.T) {
	fake := &redisclientfakes.FakeRedisClient{}
	fake.SetReturnsOnCall(0, statusCmd(nil))
	fake.SetReturnsOnCall(1, statusCmd(fmt.Errorf("second boom")))
	lb := newRedisLB(fake)
	err := lb.StoreWebRTC(context.Background(), "url1", &stubRTC{ufrag: "u"})
	if err == nil || !strings.Contains(err.Error(), "second boom") {
		t.Fatalf("expected error, got %v", err)
	}
}

// ----------------------------------------------------------------------------
// Key helpers.
// ----------------------------------------------------------------------------

func TestRedisLB_KeyHelpers(t *testing.T) {
	for _, tc := range []struct {
		name, prefix, wantPrefix string
	}{
		{"default", "", ""},
		{"configured", "xxx", "xxx:"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			env := &envfakes.FakeProxyEnvironment{}
			env.RedisKeyPrefixReturns(tc.prefix)
			lb := NewRedisLoadBalancer(env).(*redisLoadBalancer)

			for _, tt := range []struct {
				got, want string
			}{
				{lb.redisKeyUfrag("u"), tc.wantPrefix + "srs-proxy-ufrag:u"},
				{lb.redisKeyRTC("url"), tc.wantPrefix + "srs-proxy-rtc:url"},
				{lb.redisKeySPBHID("s"), tc.wantPrefix + "srs-proxy-spbhid:s"},
				{lb.redisKeyHLS("url"), tc.wantPrefix + "srs-proxy-hls:url"},
				{lb.redisKeyURL("url"), tc.wantPrefix + "srs-proxy-url:url"},
				{lb.redisKeyServer("id"), tc.wantPrefix + "srs-proxy-server:id"},
				{lb.redisKeyServers(), tc.wantPrefix + "srs-proxy-all-servers"},
			} {
				if tt.got != tt.want {
					t.Errorf("got %q, want %q", tt.got, tt.want)
				}
			}
		})
	}
}
