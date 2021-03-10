// The MIT License (MIT)
//
// Copyright (c) 2021 srs-bench(ossrs)
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
	"net"
	"sync"
	"time"

	"github.com/pion/transport/vnet"
)

// A UDP proxy between real server(net.UDPConn) and vnet.UDPConn.
//
// High level design:
//                           ..............................................
//                           :         Virtual Network (vnet)             :
//                           :                                            :
//   +-------+ *         1 +----+         +--------+                      :
//   | :App  |------------>|:Net|--o<-----|:Router |          .............................
//   +-------+             +----+         |        |          :        UDPProxy           :
//                           :            |        |       +----+     +---------+     +---------+     +--------+
//                           :            |        |--->o--|:Net|-->o-| vnet.   |-->o-|  net.   |--->-| :Real  |
//                           :            |        |       +----+     | UDPConn |     | UDPConn |     | Server |
//                           :            |        |          :       +---------+     +---------+     +--------+
//                           :            |        |          ............................:
//                           :            +--------+                       :
//                           ...............................................
//
// The whole big picture:
//                           ......................................
//                           :         Virtual Network (vnet)     :
//                           :                                    :
//   +-------+ *         1 +----+         +--------+              :
//   | :App  |------------>|:Net|--o<-----|:Router |          .............................
//   +-------+             +----+         |        |          :        UDPProxy           :
//   +-----------+ *     1 +----+         |        |       +----+     +---------+     +---------+     +--------+
//   |:STUNServer|-------->|:Net|--o<-----|        |--->o--|:Net|-->o-| vnet.   |-->o-|  net.   |--->-| :Real  |
//   +-----------+         +----+         |        |       +----+     | UDPConn |     | UDPConn |     | Server |
//   +-----------+ *     1 +----+         |        |          :       +---------+     +---------+     +--------+
//   |:TURNServer|-------->|:Net|--o<-----|        |          ............................:
//   +-----------+         +----+ [1]     |        |              :
//                           :          1 |        | 1  <<has>>   :
//                           :      +---<>|        |<>----+ [2]   :
//                           :      |     +--------+      |       :
//                         To form  |      *|             v 0..1  :
//                   a subnet tree  |       o [3]      +-----+    :
//                           :      |       ^          |:NAT |    :
//                           :      |       |          +-----+    :
//                           :      +-------+                     :
//                           ......................................
type UDPProxy struct {
	// The router bind to.
	router *vnet.Router

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
func NewProxy(router *vnet.Router) (*UDPProxy, error) {
	v := &UDPProxy{router: router, timeout: 2 * time.Minute}
	return v, nil
}

// Close the proxy, stop all workers.
func (v *UDPProxy) Close() error {
	// nolint:godox // TODO: FIXME: Do cleanup.
	return nil
}

// Proxy starts a worker for server, ignore if already started.
func (v *UDPProxy) Proxy(client *vnet.Net, server *net.UDPAddr) error {
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
	v.workers.Store(server.String(), worker)

	return worker.Proxy(client, server)
}

// A proxy worker for a specified proxy server.
type aUDPProxyWorker struct {
	router             *vnet.Router
	mockRealServerAddr *net.UDPAddr

	// Each vnet source, bind to a real socket to server.
	// key is vnet client addr, which is net.Addr
	// value is *net.UDPConn
	endpoints sync.Map
}

func (v *aUDPProxyWorker) Proxy(client *vnet.Net, serverAddr *net.UDPAddr) error { // nolint:gocognit
	// Create vnet for real server by serverAddr.
	nw := vnet.NewNet(&vnet.NetConfig{
		StaticIP: serverAddr.IP.String(),
	})
	if err := v.router.AddNet(nw); err != nil {
		return err
	}

	// We must create a "same" vnet.UDPConn as the net.UDPConn,
	// which has the same ip:port, to copy packets between them.
	vnetSocket, err := nw.ListenUDP("udp4", serverAddr)
	if err != nil {
		return err
	}

	// Start a proxy goroutine.
	var findEndpointBy func(addr net.Addr) (*net.UDPConn, error)
	// nolint:godox // TODO: FIXME: Do cleanup.
	go func() {
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

	// Got new vnet client, start a new endpoint.
	findEndpointBy = func(addr net.Addr) (*net.UDPConn, error) {
		// Exists binding.
		if value, ok := v.endpoints.Load(addr.String()); ok {
			// Exists endpoint, reuse it.
			return value.(*net.UDPConn), nil
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

		// Bind address.
		v.endpoints.Store(addr.String(), realSocket)

		// Got packet from real serverAddr, we should proxy it to vnet.
		// nolint:godox // TODO: FIXME: Do cleanup.
		go func(vnetClientAddr net.Addr) {
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

	return nil
}
