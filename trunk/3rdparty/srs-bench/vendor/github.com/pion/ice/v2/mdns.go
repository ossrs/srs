// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"github.com/google/uuid"
	"github.com/pion/logging"
	"github.com/pion/mdns"
	"github.com/pion/transport/v2"
	"golang.org/x/net/ipv4"
)

// MulticastDNSMode represents the different Multicast modes ICE can run in
type MulticastDNSMode byte

// MulticastDNSMode enum
const (
	// MulticastDNSModeDisabled means remote mDNS candidates will be discarded, and local host candidates will use IPs
	MulticastDNSModeDisabled MulticastDNSMode = iota + 1

	// MulticastDNSModeQueryOnly means remote mDNS candidates will be accepted, and local host candidates will use IPs
	MulticastDNSModeQueryOnly

	// MulticastDNSModeQueryAndGather means remote mDNS candidates will be accepted, and local host candidates will use mDNS
	MulticastDNSModeQueryAndGather
)

func generateMulticastDNSName() (string, error) {
	// https://tools.ietf.org/id/draft-ietf-rtcweb-mdns-ice-candidates-02.html#gathering
	// The unique name MUST consist of a version 4 UUID as defined in [RFC4122], followed by “.local”.
	u, err := uuid.NewRandom()
	return u.String() + ".local", err
}

func createMulticastDNS(n transport.Net, mDNSMode MulticastDNSMode, mDNSName string, log logging.LeveledLogger) (*mdns.Conn, MulticastDNSMode, error) {
	if mDNSMode == MulticastDNSModeDisabled {
		return nil, mDNSMode, nil
	}

	addr, mdnsErr := n.ResolveUDPAddr("udp4", mdns.DefaultAddress)
	if mdnsErr != nil {
		return nil, mDNSMode, mdnsErr
	}

	l, mdnsErr := n.ListenUDP("udp4", addr)
	if mdnsErr != nil {
		// If ICE fails to start MulticastDNS server just warn the user and continue
		log.Errorf("Failed to enable mDNS, continuing in mDNS disabled mode: (%s)", mdnsErr)
		return nil, MulticastDNSModeDisabled, nil
	}

	switch mDNSMode {
	case MulticastDNSModeQueryOnly:
		conn, err := mdns.Server(ipv4.NewPacketConn(l), &mdns.Config{})
		return conn, mDNSMode, err
	case MulticastDNSModeQueryAndGather:
		conn, err := mdns.Server(ipv4.NewPacketConn(l), &mdns.Config{
			LocalNames: []string{mDNSName},
		})
		return conn, mDNSMode, err
	default:
		return nil, mDNSMode, nil
	}
}
