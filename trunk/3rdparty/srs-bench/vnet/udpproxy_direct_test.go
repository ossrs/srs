// The MIT License (MIT)
//
// Copyright (c) 2021 Winlin
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
package vnet

import (
	"context"
	"fmt"
	"github.com/pion/logging"
	"github.com/pion/transport/vnet"
	"net"
	"sync"
	"testing"
	"time"
)

// vnet client:
//		10.0.0.11:5787
// proxy to real server:
//		192.168.1.10:8000
func TestUDPProxyDirectDeliver(t *testing.T) {
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
			r2 = fmt.Errorf("timeout")
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
			router, err := vnet.NewRouter(&vnet.RouterConfig{
				CIDR:          "0.0.0.0/0",
				LoggerFactory: logging.NewDefaultLoggerFactory(),
			})
			if err != nil {
				return err
			}

			clientNetwork := vnet.NewNet(&vnet.NetConfig{
				StaticIP: "10.0.0.11",
			})
			if err = router.AddNet(clientNetwork); err != nil {
				return err
			}

			if err := router.Start(); err != nil {
				return err
			}
			defer router.Stop()

			proxy, err := NewProxy(router)
			if err != nil {
				return err
			}
			defer proxy.Close()

			// For utest, mock the target real server.
			proxy.mockRealServerAddr = mockServer.realServerAddr

			// The real server address to proxy to.
			// Note that for utest, we will proxy to a local address.
			serverAddr, err := net.ResolveUDPAddr("udp4", "192.168.1.10:8000")
			if err != nil {
				return err
			}

			if err := proxy.Proxy(clientNetwork, serverAddr); err != nil {
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
				client.Close()
			}()

			// Write by vnet client.
			if _, err := client.WriteTo([]byte("Hello"), serverAddr); err != nil {
				return err
			}

			buf := make([]byte, 1500)
			if n, addr, err := client.ReadFrom(buf); err != nil {
				if selfKill.Err() == context.Canceled {
					return nil
				}
				return err
			} else if n != 5 || addr == nil {
				return fmt.Errorf("n=%v, addr=%v", n, addr)
			} else if string(buf[:n]) != "Hello" {
				return fmt.Errorf("data %v", buf[:n])
			}

			// Directly write, simulate the ARQ packet.
			// We should got the echo packet also.
			if _, err := proxy.Deliver(client.LocalAddr(), serverAddr, []byte("Hello")); err != nil {
				return err
			}

			if n, addr, err := client.ReadFrom(buf); err != nil {
				if selfKill.Err() == context.Canceled {
					return nil
				}
				return err
			} else if n != 5 || addr == nil {
				return fmt.Errorf("n=%v, addr=%v", n, addr)
			} else if string(buf[:n]) != "Hello" {
				return fmt.Errorf("data %v", buf[:n])
			}

			return err
		}

		if err := doVnetProxy(); err != nil {
			r1 = err
		}
	}()
}
