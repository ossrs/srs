//go:build !wasm
// +build !wasm

package vnet

import (
	"context"
	"errors"
	"fmt"
	"net"
	"sync"
	"testing"
	"time"

	"github.com/pion/logging"
)

// The vnet client:
//
//	10.0.0.11:5787
//
// which proxy to real server:
//
//	192.168.1.10:8000
//
// We should get a reply if directly deliver to proxy.
func TestUDPProxyDirectDeliverTypical(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())

	var r0, r1, r2 error
	defer func() {
		if r0 != nil || r1 != nil || r2 != nil {
			t.Errorf("fail for ctx=%v, r0=%v, r1=%v, r2=%v", ctx.Err(), r0, r1, r2)
		}
	}()

	var wg sync.WaitGroup
	defer wg.Wait()

	// Timeout, fail
	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		select {
		case <-ctx.Done():
		case <-time.After(time.Duration(*testTimeout) * time.Millisecond):
			r2 = fmt.Errorf("timeout") // nolint:goerr113
		}
	}()

	// For utest, we always proxy vnet packets to the random port we listen to.
	mockServer := NewMockUDPEchoServer()
	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()
		if err := mockServer.doMockUDPServer(ctx); err != nil {
			r0 = err
		}
	}()

	// Create a vent and proxy.
	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		// When real server is ready, start the vnet test.
		select {
		case <-ctx.Done():
			return
		case <-mockServer.realServerReady.Done():
		}

		doVnetProxy := func() error {
			router, err := NewRouter(&RouterConfig{
				CIDR:          "0.0.0.0/0",
				LoggerFactory: logging.NewDefaultLoggerFactory(),
			})
			if err != nil {
				return err
			}

			clientNetwork := NewNet(&NetConfig{
				StaticIP: "10.0.0.11",
			})
			if err = router.AddNet(clientNetwork); err != nil {
				return err
			}

			if err = router.Start(); err != nil {
				return err
			}
			defer router.Stop() // nolint:errcheck

			proxy, err := NewProxy(router)
			if err != nil {
				return err
			}
			defer proxy.Close() // nolint:errcheck

			// For utest, mock the target real server.
			proxy.mockRealServerAddr = mockServer.realServerAddr

			// The real server address to proxy to.
			// Note that for utest, we will proxy to a local address.
			serverAddr, err := net.ResolveUDPAddr("udp4", "192.168.1.10:8000")
			if err != nil {
				return err
			}

			if err = proxy.Proxy(clientNetwork, serverAddr); err != nil {
				return err
			}

			// Now, all packets from client, will be proxy to real server, vice versa.
			client, err := clientNetwork.ListenPacket("udp4", "10.0.0.11:5787")
			if err != nil {
				return err
			}

			// When system quit, interrupt client.
			selfKill, selfKillCancel := context.WithCancel(context.Background())
			go func() {
				<-ctx.Done()
				selfKillCancel()
				_ = client.Close()
			}()

			// Write by vnet client.
			if _, err = client.WriteTo([]byte("Hello"), serverAddr); err != nil {
				return err
			}

			buf := make([]byte, 1500)
			if n, addr, err := client.ReadFrom(buf); err != nil { // nolint:gocritic,govet
				if errors.Is(selfKill.Err(), context.Canceled) {
					return nil
				}
				return err
			} else if n != 5 || addr == nil {
				return fmt.Errorf("n=%v, addr=%v", n, addr) // nolint:goerr113
			} else if string(buf[:n]) != "Hello" { // nolint:goconst
				return fmt.Errorf("data %v", buf[:n]) // nolint:goerr113
			}

			// Directly write, simulate the ARQ packet.
			// We should got the echo packet also.
			if _, err = proxy.Deliver(client.LocalAddr(), serverAddr, []byte("Hello")); err != nil {
				return err
			}

			if n, addr, err := client.ReadFrom(buf); err != nil { // nolint:gocritic,govet
				if errors.Is(selfKill.Err(), context.Canceled) {
					return nil
				}
				return err
			} else if n != 5 || addr == nil {
				return fmt.Errorf("n=%v, addr=%v", n, addr) // nolint:goerr113
			} else if string(buf[:n]) != "Hello" {
				return fmt.Errorf("data %v", buf[:n]) // nolint:goerr113
			}

			return err
		}

		if err := doVnetProxy(); err != nil {
			r1 = err
		}
	}()
}

// Error if deliver to invalid address.
func TestUDPProxyDirectDeliverBadcase(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())

	var r0, r1, r2 error
	defer func() {
		if r0 != nil || r1 != nil || r2 != nil {
			t.Errorf("fail for ctx=%v, r0=%v, r1=%v, r2=%v", ctx.Err(), r0, r1, r2)
		}
	}()

	var wg sync.WaitGroup
	defer wg.Wait()

	// Timeout, fail
	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		select {
		case <-ctx.Done():
		case <-time.After(time.Duration(*testTimeout) * time.Millisecond):
			r2 = fmt.Errorf("timeout") // nolint:goerr113
		}
	}()

	// For utest, we always proxy vnet packets to the random port we listen to.
	mockServer := NewMockUDPEchoServer()
	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()
		if err := mockServer.doMockUDPServer(ctx); err != nil {
			r0 = err
		}
	}()

	// Create a vent and proxy.
	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		// When real server is ready, start the vnet test.
		select {
		case <-ctx.Done():
			return
		case <-mockServer.realServerReady.Done():
		}

		doVnetProxy := func() error {
			router, err := NewRouter(&RouterConfig{
				CIDR:          "0.0.0.0/0",
				LoggerFactory: logging.NewDefaultLoggerFactory(),
			})
			if err != nil {
				return err
			}

			clientNetwork := NewNet(&NetConfig{
				StaticIP: "10.0.0.11",
			})
			if err = router.AddNet(clientNetwork); err != nil {
				return err
			}

			if err = router.Start(); err != nil {
				return err
			}
			defer router.Stop() // nolint:errcheck

			proxy, err := NewProxy(router)
			if err != nil {
				return err
			}
			defer proxy.Close() // nolint:errcheck

			// For utest, mock the target real server.
			proxy.mockRealServerAddr = mockServer.realServerAddr

			// The real server address to proxy to.
			// Note that for utest, we will proxy to a local address.
			serverAddr, err := net.ResolveUDPAddr("udp4", "192.168.1.10:8000")
			if err != nil {
				return err
			}

			if err = proxy.Proxy(clientNetwork, serverAddr); err != nil {
				return err
			}

			// Now, all packets from client, will be proxy to real server, vice versa.
			client, err := clientNetwork.ListenPacket("udp4", "10.0.0.11:5787")
			if err != nil {
				return err
			}

			// When system quit, interrupt client.
			selfKill, selfKillCancel := context.WithCancel(context.Background())
			go func() {
				<-ctx.Done()
				selfKillCancel()
				_ = client.Close()
			}()

			// Write by vnet client.
			if _, err = client.WriteTo([]byte("Hello"), serverAddr); err != nil {
				return err
			}

			buf := make([]byte, 1500)
			if n, addr, err := client.ReadFrom(buf); err != nil { // nolint:gocritic,govet
				if errors.Is(selfKill.Err(), context.Canceled) {
					return nil
				}
				return err
			} else if n != 5 || addr == nil {
				return fmt.Errorf("n=%v, addr=%v", n, addr) // nolint:goerr113
			} else if string(buf[:n]) != "Hello" { // nolint:goconst
				return fmt.Errorf("data %v", buf[:n]) // nolint:goerr113
			}

			// BadCase: Invalid address, error and ignore.
			tcpAddr, err := net.ResolveTCPAddr("tcp4", "192.168.1.10:8000")
			if err != nil {
				return err
			}

			if _, err = proxy.Deliver(tcpAddr, serverAddr, []byte("Hello")); err == nil {
				return fmt.Errorf("should err") // nolint:goerr113
			}

			// BadCase: Invalid target address, ignore.
			udpAddr, err := net.ResolveUDPAddr("udp4", "10.0.0.12:5788")
			if err != nil {
				return err
			}

			if nn, err := proxy.Deliver(udpAddr, serverAddr, []byte("Hello")); err != nil { // nolint:govet
				return err
			} else if nn != 0 {
				return fmt.Errorf("invalid %v", nn) // nolint:goerr113
			}

			// BadCase: Write on closed socket, error and ignore.
			proxy.workers.Range(func(key, value interface{}) bool {
				value.(*aUDPProxyWorker).endpoints.Range(func(key, value interface{}) bool {
					_ = value.(*net.UDPConn).Close()
					return true
				})
				return true
			})

			if _, err = proxy.Deliver(client.LocalAddr(), serverAddr, []byte("Hello")); err == nil {
				return fmt.Errorf("should error") // nolint:goerr113
			}

			return nil
		}

		if err := doVnetProxy(); err != nil {
			r1 = err
		}
	}()
}
