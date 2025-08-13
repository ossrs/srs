// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"fmt"
	"net"
	"net/netip"
)

func addrWithOptionalZone(addr netip.Addr, zone string) netip.Addr {
	if zone == "" {
		return addr
	}
	if addr.Is6() && (addr.IsLinkLocalUnicast() || addr.IsLinkLocalMulticast()) {
		return addr.WithZone(zone)
	}

	return addr
}

// parseAddrFromIface should only be used when it's known the address belongs to that interface.
// e.g. it's LocalAddress on a listener.
func parseAddrFromIface(in net.Addr, ifcName string) (netip.Addr, int, NetworkType, error) {
	addr, port, nt, err := parseAddr(in)
	if err != nil {
		return netip.Addr{}, 0, 0, err
	}
	if _, ok := in.(*net.IPNet); ok {
		// net.IPNet does not have a Zone but we provide it from the interface
		addr = addrWithOptionalZone(addr, ifcName)
	}

	return addr, port, nt, nil
}

func parseAddr(in net.Addr) (netip.Addr, int, NetworkType, error) { //nolint:cyclop
	switch addr := in.(type) {
	case *net.IPNet:
		ipAddr, err := ipAddrToNetIP(addr.IP, "")
		if err != nil {
			return netip.Addr{}, 0, 0, err
		}

		return ipAddr, 0, 0, nil
	case *net.IPAddr:
		ipAddr, err := ipAddrToNetIP(addr.IP, addr.Zone)
		if err != nil {
			return netip.Addr{}, 0, 0, err
		}

		return ipAddr, 0, 0, nil
	case *net.UDPAddr:
		ipAddr, err := ipAddrToNetIP(addr.IP, addr.Zone)
		if err != nil {
			return netip.Addr{}, 0, 0, err
		}
		var nt NetworkType
		if ipAddr.Is4() {
			nt = NetworkTypeUDP4
		} else {
			nt = NetworkTypeUDP6
		}

		return ipAddr, addr.Port, nt, nil
	case *net.TCPAddr:
		ipAddr, err := ipAddrToNetIP(addr.IP, addr.Zone)
		if err != nil {
			return netip.Addr{}, 0, 0, err
		}
		var nt NetworkType
		if ipAddr.Is4() {
			nt = NetworkTypeTCP4
		} else {
			nt = NetworkTypeTCP6
		}

		return ipAddr, addr.Port, nt, nil
	default:
		return netip.Addr{}, 0, 0, addrParseError{in}
	}
}

type addrParseError struct {
	addr net.Addr
}

func (e addrParseError) Error() string {
	return fmt.Sprintf("do not know how to parse address type %T", e.addr)
}

type ipConvertError struct {
	ip []byte
}

func (e ipConvertError) Error() string {
	return fmt.Sprintf("failed to convert IP '%s' to netip.Addr", e.ip)
}

func ipAddrToNetIP(ip []byte, zone string) (netip.Addr, error) {
	netIPAddr, ok := netip.AddrFromSlice(ip)
	if !ok {
		return netip.Addr{}, ipConvertError{ip}
	}
	// we'd rather have an IPv4-mapped IPv6 become IPv4 so that it is usable.
	netIPAddr = netIPAddr.Unmap()
	netIPAddr = addrWithOptionalZone(netIPAddr, zone)

	return netIPAddr, nil
}

func createAddr(network NetworkType, ip netip.Addr, port int) net.Addr {
	switch {
	case network.IsTCP():
		return &net.TCPAddr{IP: ip.AsSlice(), Port: port, Zone: ip.Zone()}
	default:
		return &net.UDPAddr{IP: ip.AsSlice(), Port: port, Zone: ip.Zone()}
	}
}

func addrEqual(a, b net.Addr) bool {
	aIP, aPort, aType, aErr := parseAddr(a)
	if aErr != nil {
		return false
	}

	bIP, bPort, bType, bErr := parseAddr(b)
	if bErr != nil {
		return false
	}

	return aType == bType && aIP.Compare(bIP) == 0 && aPort == bPort
}

// AddrPort is  an IP and a port number.
type AddrPort [18]byte

func toAddrPort(addr net.Addr) AddrPort {
	var ap AddrPort
	switch addr := addr.(type) {
	case *net.UDPAddr:
		copy(ap[:16], addr.IP.To16())
		ap[16] = uint8(addr.Port >> 8) //nolint:gosec // G115  false positive
		ap[17] = uint8(addr.Port)      //nolint:gosec // G115  false positive
	case *net.TCPAddr:
		copy(ap[:16], addr.IP.To16())
		ap[16] = uint8(addr.Port >> 8) //nolint:gosec // G115 false positive
		ap[17] = uint8(addr.Port)      //nolint:gosec // G115 false positive
	}

	return ap
}
