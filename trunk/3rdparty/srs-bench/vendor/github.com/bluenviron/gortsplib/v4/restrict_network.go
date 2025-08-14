package gortsplib

import (
	"net"
)

// do not listen on IPv6 when address is 0.0.0.0.
func restrictNetwork(network string, address string) (string, string) {
	host, _, err := net.SplitHostPort(address)
	if err == nil {
		if host == "0.0.0.0" {
			return network + "4", address
		}
	}

	return network, address
}
