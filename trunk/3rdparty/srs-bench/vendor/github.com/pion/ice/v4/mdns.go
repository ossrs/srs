// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"net"

	"github.com/google/uuid"
	"github.com/pion/logging"
	"github.com/pion/mdns/v2"
	"github.com/pion/transport/v3"
	"golang.org/x/net/ipv4"
	"golang.org/x/net/ipv6"
)

// MulticastDNSMode represents the different Multicast modes ICE can run in.
type MulticastDNSMode byte

// MulticastDNSMode enum.
const (
	// MulticastDNSModeDisabled means remote mDNS candidates will be discarded, and local host candidates will use IPs.
	MulticastDNSModeDisabled MulticastDNSMode = iota + 1

	// MulticastDNSModeQueryOnly means remote mDNS candidates will be accepted, and local host candidates will use IPs.
	MulticastDNSModeQueryOnly

	// MulticastDNSModeQueryAndGather means remote mDNS candidates will be accepted,
	// and local host candidates will use mDNS.
	MulticastDNSModeQueryAndGather
)

func generateMulticastDNSName() (string, error) {
	// https://tools.ietf.org/id/draft-ietf-rtcweb-mdns-ice-candidates-02.html#gathering
	// The unique name MUST consist of a version 4 UUID as defined in [RFC4122], followed by “.local”.
	u, err := uuid.NewRandom()

	return u.String() + ".local", err
}

//nolint:cyclop
func createMulticastDNS(
	netTransport transport.Net,
	networkTypes []NetworkType,
	interfaces []*transport.Interface,
	includeLoopback bool,
	mDNSMode MulticastDNSMode,
	mDNSName string,
	log logging.LeveledLogger,
	loggerFactory logging.LoggerFactory,
) (*mdns.Conn, MulticastDNSMode, error) {
	if mDNSMode == MulticastDNSModeDisabled {
		return nil, mDNSMode, nil
	}

	var useV4, useV6 bool
	if len(networkTypes) == 0 {
		useV4 = true
		useV6 = true
	} else {
		for _, nt := range networkTypes {
			if nt.IsIPv4() {
				useV4 = true

				continue
			}
			if nt.IsIPv6() {
				useV6 = true
			}
		}
	}

	addr4, mdnsErr := netTransport.ResolveUDPAddr("udp4", mdns.DefaultAddressIPv4)
	if mdnsErr != nil {
		return nil, mDNSMode, mdnsErr
	}
	addr6, mdnsErr := netTransport.ResolveUDPAddr("udp6", mdns.DefaultAddressIPv6)
	if mdnsErr != nil {
		return nil, mDNSMode, mdnsErr
	}

	var pktConnV4 *ipv4.PacketConn
	var mdns4Err error
	if useV4 {
		var l transport.UDPConn
		l, mdns4Err = netTransport.ListenUDP("udp4", addr4)
		if mdns4Err != nil {
			// If ICE fails to start MulticastDNS server just warn the user and continue
			log.Errorf("Failed to enable mDNS over IPv4: (%s)", mdns4Err)

			return nil, MulticastDNSModeDisabled, nil
		}
		pktConnV4 = ipv4.NewPacketConn(l)
	}

	var pktConnV6 *ipv6.PacketConn
	var mdns6Err error
	if useV6 {
		var l transport.UDPConn
		l, mdns6Err = netTransport.ListenUDP("udp6", addr6)
		if mdns6Err != nil {
			log.Errorf("Failed to enable mDNS over IPv6: (%s)", mdns6Err)

			return nil, MulticastDNSModeDisabled, nil
		}
		pktConnV6 = ipv6.NewPacketConn(l)
	}

	if mdns4Err != nil && mdns6Err != nil {
		// If ICE fails to start MulticastDNS server just warn the user and continue
		log.Errorf("Failed to enable mDNS, continuing in mDNS disabled mode")
		//nolint:nilerr
		return nil, MulticastDNSModeDisabled, nil
	}
	var ifcs []net.Interface
	if interfaces != nil {
		ifcs = make([]net.Interface, 0, len(ifcs))
		for _, ifc := range interfaces {
			ifcs = append(ifcs, ifc.Interface)
		}
	}

	switch mDNSMode {
	case MulticastDNSModeQueryOnly:
		conn, err := mdns.Server(pktConnV4, pktConnV6, &mdns.Config{
			Interfaces:      ifcs,
			IncludeLoopback: includeLoopback,
			LoggerFactory:   loggerFactory,
		})

		return conn, mDNSMode, err
	case MulticastDNSModeQueryAndGather:
		conn, err := mdns.Server(pktConnV4, pktConnV6, &mdns.Config{
			Interfaces:      ifcs,
			IncludeLoopback: includeLoopback,
			LocalNames:      []string{mDNSName},
			LoggerFactory:   loggerFactory,
		})

		return conn, mDNSMode, err
	default:
		return nil, mDNSMode, nil
	}
}
