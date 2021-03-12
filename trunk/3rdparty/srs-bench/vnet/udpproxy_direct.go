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
	"net"
)

func (v *UDPProxy) Deliver(sourceAddr, destAddr net.Addr, b []byte) (nn int, err error) {
	v.workers.Range(func(key, value interface{}) bool {
		if nn, err := value.(*aUDPProxyWorker).Deliver(sourceAddr, destAddr, b); err != nil {
			return false // Fail, abort.
		} else if nn == len(b) {
			return false // Done.
		}

		return true // Deliver by next worker.
	})
	return
}

func (v *aUDPProxyWorker) Deliver(sourceAddr, destAddr net.Addr, b []byte) (nn int, err error) {
	addr, ok := sourceAddr.(*net.UDPAddr)
	if !ok {
		return 0, nil
	}

	// TODO: Support deliver packet from real server to vnet.
	// If packet is from vent, proxy to real server.
	var realSocket *net.UDPConn
	if value, ok := v.endpoints.Load(addr.String()); !ok {
		return 0, nil
	} else {
		realSocket = value.(*net.UDPConn)
	}

	// Send to real server.
	if _, err := realSocket.Write(b); err != nil {
		return 0, err
	}

	return len(b), nil
}
