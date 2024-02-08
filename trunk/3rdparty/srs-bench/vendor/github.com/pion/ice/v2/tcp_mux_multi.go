// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import "net"

// AllConnsGetter allows multiple fixed TCP ports to be used,
// each of which is multiplexed like TCPMux. AllConnsGetter also acts as
// a TCPMux, in which case it will return a single connection for one
// of the ports.
type AllConnsGetter interface {
	GetAllConns(ufrag string, isIPv6 bool, localIP net.IP) ([]net.PacketConn, error)
}

// MultiTCPMuxDefault implements both TCPMux and AllConnsGetter,
// allowing users to pass multiple TCPMux instances to the ICE agent
// configuration.
type MultiTCPMuxDefault struct {
	muxes []TCPMux
}

// NewMultiTCPMuxDefault creates an instance of MultiTCPMuxDefault that
// uses the provided TCPMux instances.
func NewMultiTCPMuxDefault(muxes ...TCPMux) *MultiTCPMuxDefault {
	return &MultiTCPMuxDefault{
		muxes: muxes,
	}
}

// GetConnByUfrag returns a PacketConn given the connection's ufrag, network and local address
// creates the connection if an existing one can't be found. This, unlike
// GetAllConns, will only return a single PacketConn from the first mux that was
// passed in to NewMultiTCPMuxDefault.
func (m *MultiTCPMuxDefault) GetConnByUfrag(ufrag string, isIPv6 bool, local net.IP) (net.PacketConn, error) {
	// NOTE: We always use the first element here in order to maintain the
	// behavior of using an existing connection if one exists.
	if len(m.muxes) == 0 {
		return nil, errNoTCPMuxAvailable
	}
	return m.muxes[0].GetConnByUfrag(ufrag, isIPv6, local)
}

// RemoveConnByUfrag stops and removes the muxed packet connection
// from all underlying TCPMux instances.
func (m *MultiTCPMuxDefault) RemoveConnByUfrag(ufrag string) {
	for _, mux := range m.muxes {
		mux.RemoveConnByUfrag(ufrag)
	}
}

// GetAllConns returns a PacketConn for each underlying TCPMux
func (m *MultiTCPMuxDefault) GetAllConns(ufrag string, isIPv6 bool, local net.IP) ([]net.PacketConn, error) {
	if len(m.muxes) == 0 {
		// Make sure that we either return at least one connection or an error.
		return nil, errNoTCPMuxAvailable
	}
	var conns []net.PacketConn
	for _, mux := range m.muxes {
		conn, err := mux.GetConnByUfrag(ufrag, isIPv6, local)
		if err != nil {
			// For now, this implementation is all or none.
			return nil, err
		}
		if conn != nil {
			conns = append(conns, conn)
		}
	}
	return conns, nil
}

// Close the multi mux, no further connections could be created
func (m *MultiTCPMuxDefault) Close() error {
	var err error
	for _, mux := range m.muxes {
		if e := mux.Close(); e != nil {
			err = e
		}
	}
	return err
}
