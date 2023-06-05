// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"fmt"
	"net"

	"github.com/pion/logging"
	"github.com/pion/transport/v2"
	"github.com/pion/transport/v2/stdnet"
)

// MultiUDPMuxDefault implements both UDPMux and AllConnsGetter,
// allowing users to pass multiple UDPMux instances to the ICE agent
// configuration.
type MultiUDPMuxDefault struct {
	muxes          []UDPMux
	localAddrToMux map[string]UDPMux
}

// NewMultiUDPMuxDefault creates an instance of MultiUDPMuxDefault that
// uses the provided UDPMux instances.
func NewMultiUDPMuxDefault(muxes ...UDPMux) *MultiUDPMuxDefault {
	addrToMux := make(map[string]UDPMux)
	for _, mux := range muxes {
		for _, addr := range mux.GetListenAddresses() {
			addrToMux[addr.String()] = mux
		}
	}
	return &MultiUDPMuxDefault{
		muxes:          muxes,
		localAddrToMux: addrToMux,
	}
}

// GetConn returns a PacketConn given the connection's ufrag and network
// creates the connection if an existing one can't be found.
func (m *MultiUDPMuxDefault) GetConn(ufrag string, addr net.Addr) (net.PacketConn, error) {
	mux, ok := m.localAddrToMux[addr.String()]
	if !ok {
		return nil, errNoUDPMuxAvailable
	}
	return mux.GetConn(ufrag, addr)
}

// RemoveConnByUfrag stops and removes the muxed packet connection
// from all underlying UDPMux instances.
func (m *MultiUDPMuxDefault) RemoveConnByUfrag(ufrag string) {
	for _, mux := range m.muxes {
		mux.RemoveConnByUfrag(ufrag)
	}
}

// Close the multi mux, no further connections could be created
func (m *MultiUDPMuxDefault) Close() error {
	var err error
	for _, mux := range m.muxes {
		if e := mux.Close(); e != nil {
			err = e
		}
	}
	return err
}

// GetListenAddresses returns the list of addresses that this mux is listening on
func (m *MultiUDPMuxDefault) GetListenAddresses() []net.Addr {
	addrs := make([]net.Addr, 0, len(m.localAddrToMux))
	for _, mux := range m.muxes {
		addrs = append(addrs, mux.GetListenAddresses()...)
	}
	return addrs
}

// NewMultiUDPMuxFromPort creates an instance of MultiUDPMuxDefault that
// listen all interfaces on the provided port.
func NewMultiUDPMuxFromPort(port int, opts ...UDPMuxFromPortOption) (*MultiUDPMuxDefault, error) {
	params := multiUDPMuxFromPortParam{
		networks: []NetworkType{NetworkTypeUDP4, NetworkTypeUDP6},
	}
	for _, opt := range opts {
		opt.apply(&params)
	}

	if params.net == nil {
		var err error
		if params.net, err = stdnet.NewNet(); err != nil {
			return nil, fmt.Errorf("failed to get create network: %w", err)
		}
	}

	ips, err := localInterfaces(params.net, params.ifFilter, params.ipFilter, params.networks, params.includeLoopback)
	if err != nil {
		return nil, err
	}

	conns := make([]net.PacketConn, 0, len(ips))
	for _, ip := range ips {
		conn, listenErr := params.net.ListenUDP("udp", &net.UDPAddr{IP: ip, Port: port})
		if listenErr != nil {
			err = listenErr
			break
		}
		if params.readBufferSize > 0 {
			_ = conn.SetReadBuffer(params.readBufferSize)
		}
		if params.writeBufferSize > 0 {
			_ = conn.SetWriteBuffer(params.writeBufferSize)
		}
		conns = append(conns, conn)
	}

	if err != nil {
		for _, conn := range conns {
			_ = conn.Close()
		}
		return nil, err
	}

	muxes := make([]UDPMux, 0, len(conns))
	for _, conn := range conns {
		mux := NewUDPMuxDefault(UDPMuxParams{
			Logger:  params.logger,
			UDPConn: conn,
			Net:     params.net,
		})
		muxes = append(muxes, mux)
	}

	return NewMultiUDPMuxDefault(muxes...), nil
}

// UDPMuxFromPortOption provide options for NewMultiUDPMuxFromPort
type UDPMuxFromPortOption interface {
	apply(*multiUDPMuxFromPortParam)
}

type multiUDPMuxFromPortParam struct {
	ifFilter        func(string) bool
	ipFilter        func(ip net.IP) bool
	networks        []NetworkType
	readBufferSize  int
	writeBufferSize int
	logger          logging.LeveledLogger
	includeLoopback bool
	net             transport.Net
}

type udpMuxFromPortOption struct {
	f func(*multiUDPMuxFromPortParam)
}

func (o *udpMuxFromPortOption) apply(p *multiUDPMuxFromPortParam) {
	o.f(p)
}

// UDPMuxFromPortWithInterfaceFilter set the filter to filter out interfaces that should not be used
func UDPMuxFromPortWithInterfaceFilter(f func(string) bool) UDPMuxFromPortOption {
	return &udpMuxFromPortOption{
		f: func(p *multiUDPMuxFromPortParam) {
			p.ifFilter = f
		},
	}
}

// UDPMuxFromPortWithIPFilter set the filter to filter out IP addresses that should not be used
func UDPMuxFromPortWithIPFilter(f func(ip net.IP) bool) UDPMuxFromPortOption {
	return &udpMuxFromPortOption{
		f: func(p *multiUDPMuxFromPortParam) {
			p.ipFilter = f
		},
	}
}

// UDPMuxFromPortWithNetworks set the networks that should be used. default is both IPv4 and IPv6
func UDPMuxFromPortWithNetworks(networks ...NetworkType) UDPMuxFromPortOption {
	return &udpMuxFromPortOption{
		f: func(p *multiUDPMuxFromPortParam) {
			p.networks = networks
		},
	}
}

// UDPMuxFromPortWithReadBufferSize set the UDP connection read buffer size
func UDPMuxFromPortWithReadBufferSize(size int) UDPMuxFromPortOption {
	return &udpMuxFromPortOption{
		f: func(p *multiUDPMuxFromPortParam) {
			p.readBufferSize = size
		},
	}
}

// UDPMuxFromPortWithWriteBufferSize set the UDP connection write buffer size
func UDPMuxFromPortWithWriteBufferSize(size int) UDPMuxFromPortOption {
	return &udpMuxFromPortOption{
		f: func(p *multiUDPMuxFromPortParam) {
			p.writeBufferSize = size
		},
	}
}

// UDPMuxFromPortWithLogger set the logger for the created UDPMux
func UDPMuxFromPortWithLogger(logger logging.LeveledLogger) UDPMuxFromPortOption {
	return &udpMuxFromPortOption{
		f: func(p *multiUDPMuxFromPortParam) {
			p.logger = logger
		},
	}
}

// UDPMuxFromPortWithLoopback set loopback interface should be included
func UDPMuxFromPortWithLoopback() UDPMuxFromPortOption {
	return &udpMuxFromPortOption{
		f: func(p *multiUDPMuxFromPortParam) {
			p.includeLoopback = true
		},
	}
}

// UDPMuxFromPortWithNet sets the network transport to use.
func UDPMuxFromPortWithNet(n transport.Net) UDPMuxFromPortOption {
	return &udpMuxFromPortOption{
		f: func(p *multiUDPMuxFromPortParam) {
			p.net = n
		},
	}
}
