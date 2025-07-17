// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package vnet

import (
	"encoding/binary"
	"errors"
	"fmt"
	"math/rand"
	"net"
	"strconv"
	"strings"
	"sync"

	"github.com/pion/transport/v3"
)

const (
	lo0String = "lo0String"
	udp       = "udp"
	udp4      = "udp4"
)

var (
	macAddrCounter                 uint64 = 0xBEEFED910200 //nolint:gochecknoglobals
	errNoInterface                        = errors.New("no interface is available")
	errUnexpectedNetwork                  = errors.New("unexpected network")
	errCantAssignRequestedAddr            = errors.New("can't assign requested address")
	errUnknownNetwork                     = errors.New("unknown network")
	errNoRouterLinked                     = errors.New("no router linked")
	errInvalidPortNumber                  = errors.New("invalid port number")
	errUnexpectedTypeSwitchFailure        = errors.New("unexpected type-switch failure")
	errBindFailedFor                      = errors.New("bind failed for")
	errEndPortLessThanStart               = errors.New("end port is less than the start")
	errPortSpaceExhausted                 = errors.New("port space exhausted")
)

func newMACAddress() net.HardwareAddr {
	b := make([]byte, 8)
	binary.BigEndian.PutUint64(b, macAddrCounter)
	macAddrCounter++
	return b[2:]
}

// Net represents a local network stack equivalent to a set of layers from NIC
// up to the transport (UDP / TCP) layer.
type Net struct {
	interfaces []*transport.Interface // read-only
	staticIPs  []net.IP               // read-only
	router     *Router                // read-only
	udpConns   *udpConnMap            // read-only
	mutex      sync.RWMutex
}

// Compile-time assertion
var _ transport.Net = &Net{}

func (v *Net) _getInterfaces() ([]*transport.Interface, error) {
	if len(v.interfaces) == 0 {
		return nil, errNoInterface
	}

	return v.interfaces, nil
}

// Interfaces returns a list of the system's network interfaces.
func (v *Net) Interfaces() ([]*transport.Interface, error) {
	v.mutex.RLock()
	defer v.mutex.RUnlock()

	return v._getInterfaces()
}

// caller must hold the mutex (read)
func (v *Net) _getInterface(ifName string) (*transport.Interface, error) {
	ifs, err := v._getInterfaces()
	if err != nil {
		return nil, err
	}
	for _, ifc := range ifs {
		if ifc.Name == ifName {
			return ifc, nil
		}
	}

	return nil, fmt.Errorf("%w: %s", transport.ErrInterfaceNotFound, ifName)
}

func (v *Net) getInterface(ifName string) (*transport.Interface, error) {
	v.mutex.RLock()
	defer v.mutex.RUnlock()

	return v._getInterface(ifName)
}

// InterfaceByIndex returns the interface specified by index.
//
// On Solaris, it returns one of the logical network interfaces
// sharing the logical data link; for more precision use
// InterfaceByName.
func (v *Net) InterfaceByIndex(index int) (*transport.Interface, error) {
	for _, ifc := range v.interfaces {
		if ifc.Index == index {
			return ifc, nil
		}
	}

	return nil, fmt.Errorf("%w: index=%d", transport.ErrInterfaceNotFound, index)
}

// InterfaceByName returns the interface specified by name.
func (v *Net) InterfaceByName(ifName string) (*transport.Interface, error) {
	return v.getInterface(ifName)
}

// caller must hold the mutex
func (v *Net) getAllIPAddrs(ipv6 bool) []net.IP {
	ips := []net.IP{}

	for _, ifc := range v.interfaces {
		addrs, err := ifc.Addrs()
		if err != nil {
			continue
		}

		for _, addr := range addrs {
			var ip net.IP
			if ipNet, ok := addr.(*net.IPNet); ok {
				ip = ipNet.IP
			} else if ipAddr, ok := addr.(*net.IPAddr); ok {
				ip = ipAddr.IP
			} else {
				continue
			}

			if !ipv6 {
				if ip.To4() != nil {
					ips = append(ips, ip)
				}
			}
		}
	}

	return ips
}

func (v *Net) setRouter(r *Router) error {
	v.mutex.Lock()
	defer v.mutex.Unlock()

	v.router = r
	return nil
}

func (v *Net) onInboundChunk(c Chunk) {
	v.mutex.Lock()
	defer v.mutex.Unlock()

	if c.Network() == udp {
		if conn, ok := v.udpConns.find(c.DestinationAddr()); ok {
			conn.onInboundChunk(c)
		}
	}
}

// caller must hold the mutex
func (v *Net) _dialUDP(network string, locAddr, remAddr *net.UDPAddr) (transport.UDPConn, error) {
	// validate network
	if network != udp && network != udp4 {
		return nil, fmt.Errorf("%w: %s", errUnexpectedNetwork, network)
	}

	if locAddr == nil {
		locAddr = &net.UDPAddr{
			IP: net.IPv4zero,
		}
	} else if locAddr.IP == nil {
		locAddr.IP = net.IPv4zero
	}

	// validate address. do we have that address?
	if !v.hasIPAddr(locAddr.IP) {
		return nil, &net.OpError{
			Op:   "listen",
			Net:  network,
			Addr: locAddr,
			Err:  fmt.Errorf("bind: %w", errCantAssignRequestedAddr),
		}
	}

	if locAddr.Port == 0 {
		// choose randomly from the range between 5000 and 5999
		port, err := v.assignPort(locAddr.IP, 5000, 5999)
		if err != nil {
			return nil, &net.OpError{
				Op:   "listen",
				Net:  network,
				Addr: locAddr,
				Err:  err,
			}
		}
		locAddr.Port = port
	} else if _, ok := v.udpConns.find(locAddr); ok {
		return nil, &net.OpError{
			Op:   "listen",
			Net:  network,
			Addr: locAddr,
			Err:  fmt.Errorf("bind: %w", errAddressAlreadyInUse),
		}
	}

	conn, err := newUDPConn(locAddr, remAddr, v)
	if err != nil {
		return nil, err
	}

	err = v.udpConns.insert(conn)
	if err != nil {
		return nil, err
	}

	return conn, nil
}

// ListenPacket announces on the local network address.
func (v *Net) ListenPacket(network string, address string) (net.PacketConn, error) {
	v.mutex.Lock()
	defer v.mutex.Unlock()

	locAddr, err := v.ResolveUDPAddr(network, address)
	if err != nil {
		return nil, err
	}

	return v._dialUDP(network, locAddr, nil)
}

// ListenUDP acts like ListenPacket for UDP networks.
func (v *Net) ListenUDP(network string, locAddr *net.UDPAddr) (transport.UDPConn, error) {
	v.mutex.Lock()
	defer v.mutex.Unlock()

	return v._dialUDP(network, locAddr, nil)
}

// DialUDP acts like Dial for UDP networks.
func (v *Net) DialUDP(network string, locAddr, remAddr *net.UDPAddr) (transport.UDPConn, error) {
	v.mutex.Lock()
	defer v.mutex.Unlock()

	return v._dialUDP(network, locAddr, remAddr)
}

// Dial connects to the address on the named network.
func (v *Net) Dial(network string, address string) (net.Conn, error) {
	v.mutex.Lock()
	defer v.mutex.Unlock()

	remAddr, err := v.ResolveUDPAddr(network, address)
	if err != nil {
		return nil, err
	}

	// Determine source address
	srcIP := v.determineSourceIP(nil, remAddr.IP)

	locAddr := &net.UDPAddr{IP: srcIP, Port: 0}

	return v._dialUDP(network, locAddr, remAddr)
}

// ResolveIPAddr returns an address of IP end point.
func (v *Net) ResolveIPAddr(_, address string) (*net.IPAddr, error) {
	var err error

	// Check if host is a domain name
	ip := net.ParseIP(address)
	if ip == nil {
		address = strings.ToLower(address)
		if address == "localhost" {
			ip = net.IPv4(127, 0, 0, 1)
		} else {
			// host is a domain name. resolve IP address by the name
			if v.router == nil {
				return nil, errNoRouterLinked
			}

			ip, err = v.router.resolver.lookUp(address)
			if err != nil {
				return nil, err
			}
		}
	}

	return &net.IPAddr{
		IP: ip,
	}, nil
}

// ResolveUDPAddr returns an address of UDP end point.
func (v *Net) ResolveUDPAddr(network, address string) (*net.UDPAddr, error) {
	if network != udp && network != udp4 {
		return nil, fmt.Errorf("%w %s", errUnknownNetwork, network)
	}

	host, sPort, err := net.SplitHostPort(address)
	if err != nil {
		return nil, err
	}

	ipAddress, err := v.ResolveIPAddr("ip", host)
	if err != nil {
		return nil, err
	}

	port, err := strconv.Atoi(sPort)
	if err != nil {
		return nil, errInvalidPortNumber
	}

	udpAddr := &net.UDPAddr{
		IP:   ipAddress.IP,
		Zone: ipAddress.Zone,
		Port: port,
	}

	return udpAddr, nil
}

// ResolveTCPAddr returns an address of TCP end point.
func (v *Net) ResolveTCPAddr(network, address string) (*net.TCPAddr, error) {
	if network != udp && network != "udp4" {
		return nil, fmt.Errorf("%w %s", errUnknownNetwork, network)
	}

	host, sPort, err := net.SplitHostPort(address)
	if err != nil {
		return nil, err
	}

	ipAddr, err := v.ResolveIPAddr("ip", host)
	if err != nil {
		return nil, err
	}

	port, err := strconv.Atoi(sPort)
	if err != nil {
		return nil, errInvalidPortNumber
	}

	udpAddr := &net.TCPAddr{
		IP:   ipAddr.IP,
		Zone: ipAddr.Zone,
		Port: port,
	}

	return udpAddr, nil
}

func (v *Net) write(c Chunk) error {
	if c.Network() == udp {
		if udp, ok := c.(*chunkUDP); ok {
			if c.getDestinationIP().IsLoopback() {
				if conn, ok := v.udpConns.find(udp.DestinationAddr()); ok {
					conn.onInboundChunk(udp)
				}
				return nil
			}
		} else {
			return errUnexpectedTypeSwitchFailure
		}
	}

	if v.router == nil {
		return errNoRouterLinked
	}

	v.router.push(c)
	return nil
}

func (v *Net) onClosed(addr net.Addr) {
	if addr.Network() == udp {
		//nolint:errcheck
		v.udpConns.delete(addr) // #nosec
	}
}

// This method determines the srcIP based on the dstIP when locIP
// is any IP address ("0.0.0.0" or "::"). If locIP is a non-any addr,
// this method simply returns locIP.
// caller must hold the mutex
func (v *Net) determineSourceIP(locIP, dstIP net.IP) net.IP {
	if locIP != nil && !locIP.IsUnspecified() {
		return locIP
	}

	var srcIP net.IP

	if dstIP.IsLoopback() {
		srcIP = net.ParseIP("127.0.0.1")
	} else {
		ifc, err2 := v._getInterface("eth0")
		if err2 != nil {
			return nil
		}

		addrs, err2 := ifc.Addrs()
		if err2 != nil {
			return nil
		}

		if len(addrs) == 0 {
			return nil
		}

		var findIPv4 bool
		if locIP != nil {
			findIPv4 = (locIP.To4() != nil)
		} else {
			findIPv4 = (dstIP.To4() != nil)
		}

		for _, addr := range addrs {
			ip := addr.(*net.IPNet).IP //nolint:forcetypeassert
			if findIPv4 {
				if ip.To4() != nil {
					srcIP = ip
					break
				}
			} else {
				if ip.To4() == nil {
					srcIP = ip
					break
				}
			}
		}
	}

	return srcIP
}

// caller must hold the mutex
func (v *Net) hasIPAddr(ip net.IP) bool { //nolint:gocognit
	for _, ifc := range v.interfaces {
		if addrs, err := ifc.Addrs(); err == nil {
			for _, addr := range addrs {
				var locIP net.IP
				if ipNet, ok := addr.(*net.IPNet); ok {
					locIP = ipNet.IP
				} else if ipAddr, ok := addr.(*net.IPAddr); ok {
					locIP = ipAddr.IP
				} else {
					continue
				}

				switch ip.String() {
				case "0.0.0.0":
					if locIP.To4() != nil {
						return true
					}
				case "::":
					if locIP.To4() == nil {
						return true
					}
				default:
					if locIP.Equal(ip) {
						return true
					}
				}
			}
		}
	}

	return false
}

// caller must hold the mutex
func (v *Net) allocateLocalAddr(ip net.IP, port int) error {
	// gather local IP addresses to bind
	var ips []net.IP
	if ip.IsUnspecified() {
		ips = v.getAllIPAddrs(ip.To4() == nil)
	} else if v.hasIPAddr(ip) {
		ips = []net.IP{ip}
	}

	if len(ips) == 0 {
		return fmt.Errorf("%w %s", errBindFailedFor, ip.String())
	}

	// check if all these transport addresses are not in use
	for _, ip2 := range ips {
		addr := &net.UDPAddr{
			IP:   ip2,
			Port: port,
		}
		if _, ok := v.udpConns.find(addr); ok {
			return &net.OpError{
				Op:   "bind",
				Net:  udp,
				Addr: addr,
				Err:  fmt.Errorf("bind: %w", errAddressAlreadyInUse),
			}
		}
	}

	return nil
}

// caller must hold the mutex
func (v *Net) assignPort(ip net.IP, start, end int) (int, error) {
	// choose randomly from the range between start and end (inclusive)
	if end < start {
		return -1, errEndPortLessThanStart
	}

	space := end + 1 - start
	offset := rand.Intn(space) //nolint:gosec
	for i := 0; i < space; i++ {
		port := ((offset + i) % space) + start

		err := v.allocateLocalAddr(ip, port)
		if err == nil {
			return port, nil
		}
	}

	return -1, errPortSpaceExhausted
}

func (v *Net) getStaticIPs() []net.IP {
	return v.staticIPs
}

// NetConfig is a bag of configuration parameters passed to NewNet().
type NetConfig struct {
	// StaticIPs is an array of static IP addresses to be assigned for this Net.
	// If no static IP address is given, the router will automatically assign
	// an IP address.
	StaticIPs []string

	// StaticIP is deprecated. Use StaticIPs.
	StaticIP string
}

// NewNet creates an instance of a virtual network.
//
// By design, it always have lo0 and eth0 interfaces.
// The lo0 has the address 127.0.0.1 assigned by default.
// IP address for eth0 will be assigned when this Net is added to a router.
func NewNet(config *NetConfig) (*Net, error) {
	lo0 := transport.NewInterface(net.Interface{
		Index:        1,
		MTU:          16384,
		Name:         lo0String,
		HardwareAddr: nil,
		Flags:        net.FlagUp | net.FlagLoopback | net.FlagMulticast,
	})
	lo0.AddAddress(&net.IPNet{
		IP:   net.ParseIP("127.0.0.1"),
		Mask: net.CIDRMask(8, 32),
	})

	eth0 := transport.NewInterface(net.Interface{
		Index:        2,
		MTU:          1500,
		Name:         "eth0",
		HardwareAddr: newMACAddress(),
		Flags:        net.FlagUp | net.FlagMulticast,
	})

	var staticIPs []net.IP
	for _, ipStr := range config.StaticIPs {
		if ip := net.ParseIP(ipStr); ip != nil {
			staticIPs = append(staticIPs, ip)
		}
	}
	if len(config.StaticIP) > 0 {
		if ip := net.ParseIP(config.StaticIP); ip != nil {
			staticIPs = append(staticIPs, ip)
		}
	}

	return &Net{
		interfaces: []*transport.Interface{lo0, eth0},
		staticIPs:  staticIPs,
		udpConns:   newUDPConnMap(),
	}, nil
}

// DialTCP acts like Dial for TCP networks.
func (v *Net) DialTCP(string, *net.TCPAddr, *net.TCPAddr) (transport.TCPConn, error) {
	return nil, transport.ErrNotSupported
}

// ListenTCP acts like Listen for TCP networks.
func (v *Net) ListenTCP(string, *net.TCPAddr) (transport.TCPListener, error) {
	return nil, transport.ErrNotSupported
}

// CreateDialer creates an instance of vnet.Dialer
func (v *Net) CreateDialer(d *net.Dialer) transport.Dialer {
	return &dialer{
		dialer: d,
		net:    v,
	}
}

type dialer struct {
	dialer *net.Dialer
	net    *Net
}

func (d *dialer) Dial(network, address string) (net.Conn, error) {
	return d.net.Dial(network, address)
}
