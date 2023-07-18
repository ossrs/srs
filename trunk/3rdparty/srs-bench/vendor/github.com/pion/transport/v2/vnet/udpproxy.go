// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package vnet

import (
	"context"
	"net"
	"sync"
	"time"
)

// UDPProxy is a proxy between real server(net.UDPConn) and vnet.UDPConn.
//
// High level design:
//
//	                        ..............................................
//	                        :         Virtual Network (vnet)             :
//	                        :                                            :
//	+-------+ *         1 +----+         +--------+                      :
//	| :App  |------------>|:Net|--o<-----|:Router |          .............................
//	+-------+             +----+         |        |          :        UDPProxy           :
//	                        :            |        |       +----+     +---------+     +---------+     +--------+
//	                        :            |        |--->o--|:Net|-->o-| vnet.   |-->o-|  net.   |--->-| :Real  |
//	                        :            |        |       +----+     | UDPConn |     | UDPConn |     | Server |
//	                        :            |        |          :       +---------+     +---------+     +--------+
//	                        :            |        |          ............................:
//	                        :            +--------+                       :
//	                        ...............................................
type UDPProxy struct {
	// The router bind to.
	router *Router

	// Each vnet source, bind to a real socket to server.
	// key is real server addr, which is net.Addr
	// value is *aUDPProxyWorker
	workers sync.Map

	// For each endpoint, we never know when to start and stop proxy,
	// so we stop the endpoint when timeout.
	timeout time.Duration

	// For utest, to mock the target real server.
	// Optional, use the address of received client packet.
	mockRealServerAddr *net.UDPAddr
}

// NewProxy create a proxy, the router for this proxy belongs/bind to. If need to proxy for
// please create a new proxy for each router. For all addresses we proxy, we will create a
// vnet.Net in this router and proxy all packets.
func NewProxy(router *Router) (*UDPProxy, error) {
	v := &UDPProxy{router: router, timeout: 2 * time.Minute}
	return v, nil
}

// Close the proxy, stop all workers.
func (v *UDPProxy) Close() error {
	v.workers.Range(func(key, value interface{}) bool {
		_ = value.(*aUDPProxyWorker).Close() //nolint:forcetypeassert
		return true
	})
	return nil
}

// Proxy starts a worker for server, ignore if already started.
func (v *UDPProxy) Proxy(client *Net, server *net.UDPAddr) error {
	// Note that even if the worker exists, it's also ok to create a same worker,
	// because the router will use the last one, and the real server will see a address
	// change event after we switch to the next worker.
	if _, ok := v.workers.Load(server.String()); ok {
		// nolint:godox // TODO: Need to restart the stopped worker?
		return nil
	}

	// Not exists, create a new one.
	worker := &aUDPProxyWorker{
		router: v.router, mockRealServerAddr: v.mockRealServerAddr,
	}

	// Create context for cleanup.
	var ctx context.Context
	ctx, worker.ctxDisposeCancel = context.WithCancel(context.Background())

	v.workers.Store(server.String(), worker)

	return worker.Proxy(ctx, client, server)
}

// A proxy worker for a specified proxy server.
type aUDPProxyWorker struct {
	router             *Router
	mockRealServerAddr *net.UDPAddr

	// Each vnet source, bind to a real socket to server.
	// key is vnet client addr, which is net.Addr
	// value is *net.UDPConn
	endpoints sync.Map

	// For cleanup.
	ctxDisposeCancel context.CancelFunc
	wg               sync.WaitGroup
}

func (v *aUDPProxyWorker) Close() error {
	// Notify all goroutines to dispose.
	v.ctxDisposeCancel()

	// Wait for all goroutines quit.
	v.wg.Wait()

	return nil
}

func (v *aUDPProxyWorker) Proxy(ctx context.Context, _ *Net, serverAddr *net.UDPAddr) error { // nolint:gocognit
	// Create vnet for real server by serverAddr.
	nw, err := NewNet(&NetConfig{
		StaticIP: serverAddr.IP.String(),
	})
	if err != nil {
		return err
	}

	if err = v.router.AddNet(nw); err != nil {
		return err
	}

	// We must create a "same" vnet.UDPConn as the net.UDPConn,
	// which has the same ip:port, to copy packets between them.
	vnetSocket, err := nw.ListenUDP("udp4", serverAddr)
	if err != nil {
		return err
	}

	// User stop proxy, we should close the socket.
	go func() {
		<-ctx.Done()
		_ = vnetSocket.Close()
	}()

	// Got new vnet client, start a new endpoint.
	findEndpointBy := func(addr net.Addr) (*net.UDPConn, error) {
		// Exists binding.
		if value, ok := v.endpoints.Load(addr.String()); ok {
			// Exists endpoint, reuse it.
			return value.(*net.UDPConn), nil //nolint:forcetypeassert
		}

		// The real server we proxy to, for utest to mock it.
		realAddr := serverAddr
		if v.mockRealServerAddr != nil {
			realAddr = v.mockRealServerAddr
		}

		// Got new vnet client, create new endpoint.
		realSocket, err := net.DialUDP("udp4", nil, realAddr)
		if err != nil {
			return nil, err
		}

		// User stop proxy, we should close the socket.
		go func() {
			<-ctx.Done()
			_ = realSocket.Close()
		}()

		// Bind address.
		v.endpoints.Store(addr.String(), realSocket)

		// Got packet from real serverAddr, we should proxy it to vnet.
		v.wg.Add(1)
		go func(vnetClientAddr net.Addr) {
			defer v.wg.Done()

			buf := make([]byte, 1500)
			for {
				n, _, err := realSocket.ReadFrom(buf)
				if err != nil {
					return
				}

				if n <= 0 {
					continue // Drop packet
				}

				if _, err := vnetSocket.WriteTo(buf[:n], vnetClientAddr); err != nil {
					return
				}
			}
		}(addr)

		return realSocket, nil
	}

	// Start a proxy goroutine.
	v.wg.Add(1)
	go func() {
		defer v.wg.Done()

		buf := make([]byte, 1500)

		for {
			n, addr, err := vnetSocket.ReadFrom(buf)
			if err != nil {
				return
			}

			if n <= 0 || addr == nil {
				continue // Drop packet
			}

			realSocket, err := findEndpointBy(addr)
			if err != nil {
				continue // Drop packet.
			}

			if _, err := realSocket.Write(buf[:n]); err != nil {
				return
			}
		}
	}()

	return nil
}
