// Copyright (c) 2025 Winlin
//
// SPDX-License-Identifier: MIT
package lb

import (
	"context"
	"sync"
	"testing"
	"time"
)

// mockEnvironment is a mock implementation of the Environment interface for testing.
type mockEnvironment struct{}

func (m *mockEnvironment) GoPprof() string                 { return "" }
func (m *mockEnvironment) GraceQuitTimeout() string        { return "20s" }
func (m *mockEnvironment) ForceQuitTimeout() string        { return "30s" }
func (m *mockEnvironment) HttpAPI() string                 { return "1985" }
func (m *mockEnvironment) HttpServer() string              { return "8080" }
func (m *mockEnvironment) RtmpServer() string              { return "1935" }
func (m *mockEnvironment) WebRTCServer() string            { return "8000" }
func (m *mockEnvironment) SRTServer() string               { return "10080" }
func (m *mockEnvironment) SystemAPI() string               { return "12025" }
func (m *mockEnvironment) StaticFiles() string             { return "" }
func (m *mockEnvironment) LoadBalancerType() string        { return "memory" }
func (m *mockEnvironment) RedisHost() string               { return "127.0.0.1" }
func (m *mockEnvironment) RedisPort() string               { return "6379" }
func (m *mockEnvironment) RedisPassword() string           { return "" }
func (m *mockEnvironment) RedisDB() string                 { return "0" }
func (m *mockEnvironment) DefaultBackendEnabled() string   { return "off" }
func (m *mockEnvironment) DefaultBackendIP() string        { return "127.0.0.1" }
func (m *mockEnvironment) DefaultBackendRTMP() string      { return "1935" }
func (m *mockEnvironment) DefaultBackendHttp() string      { return "8080" }
func (m *mockEnvironment) DefaultBackendAPI() string       { return "1985" }
func (m *mockEnvironment) DefaultBackendRTC() string       { return "8000" }
func (m *mockEnvironment) DefaultBackendSRT() string       { return "10080" }

// TestPick_HealthyServer tests that Pick returns the same server when it's healthy.
func TestPick_HealthyServer(t *testing.T) {
	ctx := context.Background()
	lb := NewMemoryLoadBalancer(&mockEnvironment{}).(*MemoryLoadBalancer)

	// Create and register a healthy server
	server1 := &SRSServer{
		IP:        "192.168.1.1",
		ServerID:  "server1",
		ServiceID: "service1",
		PID:       "1234",
		UpdatedAt: time.Now(), // Fresh timestamp
	}
	err := lb.Update(ctx, server1)
	if err != nil {
		t.Fatalf("Failed to update server: %v", err)
	}

	streamURL := "rtmp://test/live/stream1"

	// First pick
	picked1, err := lb.Pick(ctx, streamURL)
	if err != nil {
		t.Fatalf("First pick failed: %v", err)
	}
	if picked1.ID() != server1.ID() {
		t.Errorf("Expected server %v, got %v", server1.ID(), picked1.ID())
	}

	// Second pick should return the same server
	picked2, err := lb.Pick(ctx, streamURL)
	if err != nil {
		t.Fatalf("Second pick failed: %v", err)
	}
	if picked2.ID() != server1.ID() {
		t.Errorf("Expected same server %v, got %v", server1.ID(), picked2.ID())
	}

	// Verify both picks returned the same server
	if picked1.ID() != picked2.ID() {
		t.Errorf("Picks should return same server, got %v and %v", picked1.ID(), picked2.ID())
	}
}

// TestPick_ExpiredServerSwitchesToNew tests that when a server expires, Pick switches to a new healthy server.
func TestPick_ExpiredServerSwitchesToNew(t *testing.T) {
	ctx := context.Background()
	lb := NewMemoryLoadBalancer(&mockEnvironment{}).(*MemoryLoadBalancer)

	// Create an expired server (updated 400 seconds ago, beyond the 300s threshold)
	oldServer := &SRSServer{
		IP:        "192.168.1.1",
		ServerID:  "server-old",
		ServiceID: "service-old",
		PID:       "1111",
		UpdatedAt: time.Now().Add(-400 * time.Second), // Expired
	}
	err := lb.Update(ctx, oldServer)
	if err != nil {
		t.Fatalf("Failed to update old server: %v", err)
	}

	// Create a healthy server
	newServer := &SRSServer{
		IP:        "192.168.1.2",
		ServerID:  "server-new",
		ServiceID: "service-new",
		PID:       "2222",
		UpdatedAt: time.Now(), // Fresh timestamp
	}
	err = lb.Update(ctx, newServer)
	if err != nil {
		t.Fatalf("Failed to update new server: %v", err)
	}

	streamURL := "rtmp://test/live/stream1"

	// First pick - should get the old server initially if it was picked before expiry
	// Let's manually set picked to old server to simulate this scenario
	lb.picked.Store(streamURL, oldServer)

	// Now pick - should detect old server is expired and switch to new server
	picked, err := lb.Pick(ctx, streamURL)
	if err != nil {
		t.Fatalf("Pick failed: %v", err)
	}

	// Should have switched to the new healthy server
	if picked.ID() != newServer.ID() {
		t.Errorf("Expected to switch to new server %v, but got %v", newServer.ID(), picked.ID())
	}

	// Verify the server is healthy
	if time.Since(picked.UpdatedAt) >= ServerAliveDuration {
		t.Errorf("Picked server should be healthy, but UpdatedAt is %v", picked.UpdatedAt)
	}
}

// TestPick_ConcurrentAccess tests thread safety when multiple goroutines call Pick simultaneously.
func TestPick_ConcurrentAccess(t *testing.T) {
	ctx := context.Background()
	lb := NewMemoryLoadBalancer(&mockEnvironment{}).(*MemoryLoadBalancer)

	// Create multiple healthy servers
	for i := 1; i <= 3; i++ {
		server := &SRSServer{
			IP:        "192.168.1." + string(rune('0'+i)),
			ServerID:  "server" + string(rune('0'+i)),
			ServiceID: "service" + string(rune('0'+i)),
			PID:       string(rune('0' + i)),
			UpdatedAt: time.Now(),
		}
		err := lb.Update(ctx, server)
		if err != nil {
			t.Fatalf("Failed to update server%d: %v", i, err)
		}
	}

	streamURL := "rtmp://test/live/concurrent-stream"
	numGoroutines := 100
	var wg sync.WaitGroup
	results := make(chan string, numGoroutines)

	// Launch multiple goroutines to call Pick concurrently
	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			picked, err := lb.Pick(ctx, streamURL)
			if err != nil {
				t.Errorf("Pick failed in goroutine: %v", err)
				return
			}
			results <- picked.ID()
		}()
	}

	wg.Wait()
	close(results)

	// Collect all results
	serverIDs := make(map[string]int)
	for id := range results {
		serverIDs[id]++
	}

	// All goroutines should have picked the same server
	if len(serverIDs) != 1 {
		t.Errorf("Expected all goroutines to pick the same server, but got %d different servers: %v",
			len(serverIDs), serverIDs)
	}
}

// TestPick_ConcurrentExpiration tests thread safety when server expires during concurrent access.
func TestPick_ConcurrentExpiration(t *testing.T) {
	ctx := context.Background()
	lb := NewMemoryLoadBalancer(&mockEnvironment{}).(*MemoryLoadBalancer)

	// Create an expired server
	oldServer := &SRSServer{
		IP:        "192.168.1.1",
		ServerID:  "server-old",
		ServiceID: "service-old",
		PID:       "1111",
		UpdatedAt: time.Now().Add(-400 * time.Second), // Expired
	}
	err := lb.Update(ctx, oldServer)
	if err != nil {
		t.Fatalf("Failed to update old server: %v", err)
	}

	// Create healthy servers
	for i := 1; i <= 3; i++ {
		server := &SRSServer{
			IP:        "192.168.1." + string(rune('1'+i)),
			ServerID:  "server-new" + string(rune('0'+i)),
			ServiceID: "service-new" + string(rune('0'+i)),
			PID:       string(rune('0' + i)),
			UpdatedAt: time.Now(),
		}
		err := lb.Update(ctx, server)
		if err != nil {
			t.Fatalf("Failed to update server%d: %v", i, err)
		}
	}

	streamURL := "rtmp://test/live/expiration-stream"
	// Manually set picked to old expired server
	lb.picked.Store(streamURL, oldServer)

	numGoroutines := 100
	var wg sync.WaitGroup
	results := make(chan string, numGoroutines)

	// Launch multiple goroutines that will all detect expiration simultaneously
	for i := 0; i < numGoroutines; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			picked, err := lb.Pick(ctx, streamURL)
			if err != nil {
				t.Errorf("Pick failed in goroutine: %v", err)
				return
			}
			// Should not get the expired server
			if picked.ID() == oldServer.ID() {
				t.Errorf("Got expired server %v", picked.ID())
				return
			}
			results <- picked.ID()
		}()
	}

	wg.Wait()
	close(results)

	// Collect all results
	serverIDs := make(map[string]int)
	for id := range results {
		serverIDs[id]++
	}

	// All goroutines should have picked the same new healthy server
	// (thanks to double-checked locking)
	if len(serverIDs) != 1 {
		t.Errorf("Expected all goroutines to pick the same new server, but got %d different servers: %v",
			len(serverIDs), serverIDs)
	}

	// Verify none picked the old expired server
	if _, hasOld := serverIDs[oldServer.ID()]; hasOld {
		t.Errorf("Some goroutines picked the expired server, this should not happen")
	}
}

// TestPick_NoServersAvailable tests error handling when no servers are available.
func TestPick_NoServersAvailable(t *testing.T) {
	ctx := context.Background()
	lb := NewMemoryLoadBalancer(&mockEnvironment{}).(*MemoryLoadBalancer)

	streamURL := "rtmp://test/live/no-server-stream"

	// Try to pick when no servers are registered
	_, err := lb.Pick(ctx, streamURL)
	if err == nil {
		t.Error("Expected error when no servers available, got nil")
	}
}
