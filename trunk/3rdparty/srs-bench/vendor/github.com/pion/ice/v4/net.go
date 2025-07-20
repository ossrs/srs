// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"net"
	"net/netip"

	"github.com/pion/logging"
	"github.com/pion/transport/v3"
)

// The conditions of invalidation written below are defined in
// https://tools.ietf.org/html/rfc8445#section-5.1.1.1
// It is partial because the link-local check is done later in various gather local
// candidate methods which conditionally accept IPv6 based on usage of mDNS or not.
func isSupportedIPv6Partial(ip net.IP) bool {
	if len(ip) != net.IPv6len ||
		// Deprecated IPv4-compatible IPv6 addresses [RFC4291] and IPv6 site-
		//   local unicast addresses [RFC3879] MUST NOT be included in the
		//   address candidates.
		isZeros(ip[0:12]) || // !(IPv4-compatible IPv6)
		ip[0] == 0xfe && ip[1]&0xc0 == 0xc0 { // !(IPv6 site-local unicast)
		return false
	}

	return true
}

func isZeros(ip net.IP) bool {
	for i := 0; i < len(ip); i++ {
		if ip[i] != 0 {
			return false
		}
	}

	return true
}

//nolint:gocognit,cyclop
func localInterfaces(
	n transport.Net,
	interfaceFilter func(string) (keep bool),
	ipFilter func(net.IP) (keep bool),
	networkTypes []NetworkType,
	includeLoopback bool,
) ([]*transport.Interface, []netip.Addr, error) {
	ipAddrs := []netip.Addr{}
	ifaces, err := n.Interfaces()
	if err != nil {
		return nil, ipAddrs, err
	}

	filteredIfaces := make([]*transport.Interface, 0, len(ifaces))

	var ipV4Requested, ipv6Requested bool
	if len(networkTypes) == 0 {
		ipV4Requested = true
		ipv6Requested = true
	} else {
		for _, typ := range networkTypes {
			if typ.IsIPv4() {
				ipV4Requested = true
			}

			if typ.IsIPv6() {
				ipv6Requested = true
			}
		}
	}

	for _, iface := range ifaces {
		if iface.Flags&net.FlagUp == 0 {
			continue // Interface down
		}
		if (iface.Flags&net.FlagLoopback != 0) && !includeLoopback {
			continue // Loopback interface
		}

		if interfaceFilter != nil && !interfaceFilter(iface.Name) {
			continue
		}

		ifaceAddrs, err := iface.Addrs()
		if err != nil {
			continue
		}

		atLeastOneAddr := false
		for _, addr := range ifaceAddrs {
			ipAddr, _, _, err := parseAddrFromIface(addr, iface.Name)
			if err != nil || (ipAddr.IsLoopback() && !includeLoopback) {
				continue
			}
			if ipAddr.Is6() {
				if !ipv6Requested {
					continue
				} else if !isSupportedIPv6Partial(ipAddr.AsSlice()) {
					continue
				}
			} else if !ipV4Requested {
				continue
			}

			if ipFilter != nil && !ipFilter(ipAddr.AsSlice()) {
				continue
			}

			atLeastOneAddr = true
			ipAddrs = append(ipAddrs, ipAddr)
		}

		if atLeastOneAddr {
			ifaceCopy := iface
			filteredIfaces = append(filteredIfaces, ifaceCopy)
		}
	}

	return filteredIfaces, ipAddrs, nil
}

//nolint:cyclop
func listenUDPInPortRange(
	netTransport transport.Net,
	log logging.LeveledLogger,
	portMax, portMin int,
	network string,
	lAddr *net.UDPAddr,
) (transport.UDPConn, error) {
	if (lAddr.Port != 0) || ((portMin == 0) && (portMax == 0)) {
		return netTransport.ListenUDP(network, lAddr)
	}

	if portMin == 0 {
		portMin = 1024 // Start at 1024 which is non-privileged
	}

	if portMax == 0 {
		portMax = 0xFFFF
	}

	if portMin > portMax {
		return nil, ErrPort
	}

	portStart := globalMathRandomGenerator.Intn(portMax-portMin+1) + portMin
	portCurrent := portStart
	for {
		addr := &net.UDPAddr{
			IP:   lAddr.IP,
			Zone: lAddr.Zone,
			Port: portCurrent,
		}

		c, e := netTransport.ListenUDP(network, addr)
		if e == nil {
			return c, e //nolint:nilerr
		}
		log.Debugf("Failed to listen %s: %v", lAddr.String(), e)
		portCurrent++
		if portCurrent > portMax {
			portCurrent = portMin
		}
		if portCurrent == portStart {
			break
		}
	}

	return nil, ErrPort
}
