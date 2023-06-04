// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"net"

	"github.com/pion/logging"
	"github.com/pion/transport/v2"
)

// The conditions of invalidation written below are defined in
// https://tools.ietf.org/html/rfc8445#section-5.1.1.1
func isSupportedIPv6(ip net.IP) bool {
	if len(ip) != net.IPv6len ||
		isZeros(ip[0:12]) || // !(IPv4-compatible IPv6)
		ip[0] == 0xfe && ip[1]&0xc0 == 0xc0 || // !(IPv6 site-local unicast)
		ip.IsLinkLocalUnicast() ||
		ip.IsLinkLocalMulticast() {
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

func localInterfaces(n transport.Net, interfaceFilter func(string) bool, ipFilter func(net.IP) bool, networkTypes []NetworkType, includeLoopback bool) ([]net.IP, error) { //nolint:gocognit
	ips := []net.IP{}
	ifaces, err := n.Interfaces()
	if err != nil {
		return ips, err
	}

	var IPv4Requested, IPv6Requested bool
	for _, typ := range networkTypes {
		if typ.IsIPv4() {
			IPv4Requested = true
		}

		if typ.IsIPv6() {
			IPv6Requested = true
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

		addrs, err := iface.Addrs()
		if err != nil {
			continue
		}

		for _, addr := range addrs {
			var ip net.IP
			switch addr := addr.(type) {
			case *net.IPNet:
				ip = addr.IP
			case *net.IPAddr:
				ip = addr.IP
			}
			if ip == nil || (ip.IsLoopback() && !includeLoopback) {
				continue
			}

			if ipv4 := ip.To4(); ipv4 == nil {
				if !IPv6Requested {
					continue
				} else if !isSupportedIPv6(ip) {
					continue
				}
			} else if !IPv4Requested {
				continue
			}

			if ipFilter != nil && !ipFilter(ip) {
				continue
			}

			ips = append(ips, ip)
		}
	}
	return ips, nil
}

func listenUDPInPortRange(n transport.Net, log logging.LeveledLogger, portMax, portMin int, network string, lAddr *net.UDPAddr) (transport.UDPConn, error) {
	if (lAddr.Port != 0) || ((portMin == 0) && (portMax == 0)) {
		return n.ListenUDP(network, lAddr)
	}
	var i, j int
	i = portMin
	if i == 0 {
		i = 1
	}
	j = portMax
	if j == 0 {
		j = 0xFFFF
	}
	if i > j {
		return nil, ErrPort
	}

	portStart := globalMathRandomGenerator.Intn(j-i+1) + i
	portCurrent := portStart
	for {
		lAddr = &net.UDPAddr{IP: lAddr.IP, Port: portCurrent}
		c, e := n.ListenUDP(network, lAddr)
		if e == nil {
			return c, e //nolint:nilerr
		}
		log.Debugf("Failed to listen %s: %v", lAddr.String(), e)
		portCurrent++
		if portCurrent > j {
			portCurrent = i
		}
		if portCurrent == portStart {
			break
		}
	}
	return nil, ErrPort
}
