// Package multicast contains multicast connections.
package multicast

import (
	"fmt"
	"net"
)

// Conn is a Multicast connection.
type Conn interface {
	net.PacketConn
	SetReadBuffer(int) error
}

// InterfaceForSource returns a multicast-capable interface that can communicate with given IP.
func InterfaceForSource(ip net.IP) (*net.Interface, error) {
	if ip.Equal(net.ParseIP("127.0.0.1")) {
		return nil, fmt.Errorf("IP 127.0.0.1 can't be used as source of a multicast stream. Use the LAN IP of your PC")
	}

	intfs, err := net.Interfaces()
	if err != nil {
		return nil, err
	}

	for _, intf := range intfs {
		if (intf.Flags & net.FlagMulticast) == 0 {
			continue
		}

		addrs, err := intf.Addrs()
		if err == nil {
			for _, addr := range addrs {
				_, ipnet, err := net.ParseCIDR(addr.String())
				if err == nil && ipnet.Contains(ip) {
					return &intf, nil
				}
			}
		}
	}

	return nil, fmt.Errorf("found no interface that is multicast-capable and can communicate with IP %v", ip)
}
