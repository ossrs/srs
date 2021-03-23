package ice

import (
	"fmt"
	"net"
	"sync/atomic"
	"time"

	"github.com/pion/logging"
	"github.com/pion/stun"
	"github.com/pion/transport/vnet"
)

type atomicError struct{ v atomic.Value }

func (a *atomicError) Store(err error) {
	a.v.Store(struct{ error }{err})
}

func (a *atomicError) Load() error {
	err, _ := a.v.Load().(struct{ error })
	return err.error
}

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

func parseAddr(in net.Addr) (net.IP, int, NetworkType, bool) {
	switch addr := in.(type) {
	case *net.UDPAddr:
		return addr.IP, addr.Port, NetworkTypeUDP4, true
	case *net.TCPAddr:
		return addr.IP, addr.Port, NetworkTypeTCP4, true
	}
	return nil, 0, 0, false
}

func createAddr(network NetworkType, ip net.IP, port int) net.Addr {
	switch {
	case network.IsTCP():
		return &net.TCPAddr{IP: ip, Port: port}
	default:
		return &net.UDPAddr{IP: ip, Port: port}
	}
}

func addrEqual(a, b net.Addr) bool {
	aIP, aPort, aType, aOk := parseAddr(a)
	if !aOk {
		return false
	}

	bIP, bPort, bType, bOk := parseAddr(b)
	if !bOk {
		return false
	}

	return aType == bType && aIP.Equal(bIP) && aPort == bPort
}

// getXORMappedAddr initiates a stun requests to serverAddr using conn, reads the response and returns
// the XORMappedAddress returned by the stun server.
//
// Adapted from stun v0.2.
func getXORMappedAddr(conn net.PacketConn, serverAddr net.Addr, deadline time.Duration) (*stun.XORMappedAddress, error) {
	if deadline > 0 {
		if err := conn.SetReadDeadline(time.Now().Add(deadline)); err != nil {
			return nil, err
		}
	}
	defer func() {
		if deadline > 0 {
			_ = conn.SetReadDeadline(time.Time{})
		}
	}()
	resp, err := stunRequest(
		func(p []byte) (int, error) {
			n, _, errr := conn.ReadFrom(p)
			return n, errr
		},
		func(b []byte) (int, error) {
			return conn.WriteTo(b, serverAddr)
		},
	)
	if err != nil {
		return nil, err
	}
	var addr stun.XORMappedAddress
	if err = addr.GetFrom(resp); err != nil {
		return nil, fmt.Errorf("%w: %v", errGetXorMappedAddrResponse, err)
	}
	return &addr, nil
}

func stunRequest(read func([]byte) (int, error), write func([]byte) (int, error)) (*stun.Message, error) {
	req, err := stun.Build(stun.BindingRequest, stun.TransactionID)
	if err != nil {
		return nil, err
	}
	if _, err = write(req.Raw); err != nil {
		return nil, err
	}
	const maxMessageSize = 1280
	bs := make([]byte, maxMessageSize)
	n, err := read(bs)
	if err != nil {
		return nil, err
	}
	res := &stun.Message{Raw: bs[:n]}
	if err := res.Decode(); err != nil {
		return nil, err
	}
	return res, nil
}

func localInterfaces(vnet *vnet.Net, interfaceFilter func(string) bool, networkTypes []NetworkType) ([]net.IP, error) { //nolint:gocognit
	ips := []net.IP{}
	ifaces, err := vnet.Interfaces()
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
			continue // interface down
		}
		if iface.Flags&net.FlagLoopback != 0 {
			continue // loopback interface
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
			if ip == nil || ip.IsLoopback() {
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

			ips = append(ips, ip)
		}
	}
	return ips, nil
}

func listenUDPInPortRange(vnet *vnet.Net, log logging.LeveledLogger, portMax, portMin int, network string, laddr *net.UDPAddr) (vnet.UDPPacketConn, error) {
	if (laddr.Port != 0) || ((portMin == 0) && (portMax == 0)) {
		return vnet.ListenUDP(network, laddr)
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
		laddr = &net.UDPAddr{IP: laddr.IP, Port: portCurrent}
		c, e := vnet.ListenUDP(network, laddr)
		if e == nil {
			return c, e
		}
		log.Debugf("failed to listen %s: %v", laddr.String(), e)
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
