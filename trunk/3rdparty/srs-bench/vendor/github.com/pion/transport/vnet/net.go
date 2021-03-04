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
)

const (
	lo0String = "lo0String"
	udpString = "udp"
)

var (
	macAddrCounter                 uint64 = 0xBEEFED910200 //nolint:gochecknoglobals
	errNoInterface                        = errors.New("no interface is available")
	errNotFound                           = errors.New("not found")
	errUnexpectedNetwork                  = errors.New("unexpected network")
	errCantAssignRequestedAddr            = errors.New("can't assign requested address")
	errUnknownNetwork                     = errors.New("unknown network")
	errNoRouterLinked                     = errors.New("no router linked")
	errInvalidPortNumber                  = errors.New("invalid port number")
	errUnexpectedTypeSwitchFailure        = errors.New("unexpected type-switch failure")
	errBindFailerFor                      = errors.New("bind failed for")
	errEndPortLessThanStart               = errors.New("end port is less than the start")
	errPortSpaceExhausted                 = errors.New("port space exhausted")
	errVNetDisabled                       = errors.New("vnet is not enabled")
)

func newMACAddress() net.HardwareAddr {
	b := make([]byte, 8)
	binary.BigEndian.PutUint64(b, macAddrCounter)
	macAddrCounter++
	return b[2:]
}

type vNet struct {
	interfaces []*Interface // read-only
	staticIPs  []net.IP     // read-only
	router     *Router      // read-only
	udpConns   *udpConnMap  // read-only
	mutex      sync.RWMutex
}

func (v *vNet) _getInterfaces() ([]*Interface, error) {
	if len(v.interfaces) == 0 {
		return nil, errNoInterface
	}

	return v.interfaces, nil
}

func (v *vNet) getInterfaces() ([]*Interface, error) {
	v.mutex.RLock()
	defer v.mutex.RUnlock()

	return v._getInterfaces()
}

// caller must hold the mutex (read)
func (v *vNet) _getInterface(ifName string) (*Interface, error) {
	ifs, err := v._getInterfaces()
	if err != nil {
		return nil, err
	}
	for _, ifc := range ifs {
		if ifc.Name == ifName {
			return ifc, nil
		}
	}

	return nil, fmt.Errorf("interface %s %w", ifName, errNotFound)
}

func (v *vNet) getInterface(ifName string) (*Interface, error) {
	v.mutex.RLock()
	defer v.mutex.RUnlock()

	return v._getInterface(ifName)
}

// caller must hold the mutex
func (v *vNet) getAllIPAddrs(ipv6 bool) []net.IP {
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

func (v *vNet) setRouter(r *Router) error {
	v.mutex.Lock()
	defer v.mutex.Unlock()

	v.router = r
	return nil
}

func (v *vNet) onInboundChunk(c Chunk) {
	v.mutex.Lock()
	defer v.mutex.Unlock()

	if c.Network() == udpString {
		if conn, ok := v.udpConns.find(c.DestinationAddr()); ok {
			conn.onInboundChunk(c)
		}
	}
}

// caller must hold the mutex
func (v *vNet) _dialUDP(network string, locAddr, remAddr *net.UDPAddr) (UDPPacketConn, error) {
	// validate network
	if network != udpString && network != "udp4" {
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

func (v *vNet) listenPacket(network string, address string) (UDPPacketConn, error) {
	v.mutex.Lock()
	defer v.mutex.Unlock()

	locAddr, err := v.resolveUDPAddr(network, address)
	if err != nil {
		return nil, err
	}

	return v._dialUDP(network, locAddr, nil)
}

func (v *vNet) listenUDP(network string, locAddr *net.UDPAddr) (UDPPacketConn, error) {
	v.mutex.Lock()
	defer v.mutex.Unlock()

	return v._dialUDP(network, locAddr, nil)
}

func (v *vNet) dialUDP(network string, locAddr, remAddr *net.UDPAddr) (UDPPacketConn, error) {
	v.mutex.Lock()
	defer v.mutex.Unlock()

	return v._dialUDP(network, locAddr, remAddr)
}

func (v *vNet) dial(network string, address string) (UDPPacketConn, error) {
	v.mutex.Lock()
	defer v.mutex.Unlock()

	remAddr, err := v.resolveUDPAddr(network, address)
	if err != nil {
		return nil, err
	}

	// Determine source address
	srcIP := v.determineSourceIP(nil, remAddr.IP)

	locAddr := &net.UDPAddr{IP: srcIP, Port: 0}

	return v._dialUDP(network, locAddr, remAddr)
}

func (v *vNet) resolveUDPAddr(network, address string) (*net.UDPAddr, error) {
	if network != udpString && network != "udp4" {
		return nil, fmt.Errorf("%w %s", errUnknownNetwork, network)
	}

	host, sPort, err := net.SplitHostPort(address)
	if err != nil {
		return nil, err
	}

	// Check if host is a domain name
	ip := net.ParseIP(host)
	if ip == nil {
		host = strings.ToLower(host)
		if host == "localhost" {
			ip = net.IPv4(127, 0, 0, 1)
		} else {
			// host is a domain name. resolve IP address by the name
			if v.router == nil {
				return nil, errNoRouterLinked
			}

			ip, err = v.router.resolver.lookUp(host)
			if err != nil {
				return nil, err
			}
		}
	}

	port, err := strconv.Atoi(sPort)
	if err != nil {
		return nil, errInvalidPortNumber
	}

	udpAddr := &net.UDPAddr{
		IP:   ip,
		Port: port,
	}

	return udpAddr, nil
}

func (v *vNet) write(c Chunk) error {
	if c.Network() == udpString {
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

func (v *vNet) onClosed(addr net.Addr) {
	if addr.Network() == udpString {
		//nolint:errcheck
		v.udpConns.delete(addr) // #nosec
	}
}

// This method determines the srcIP based on the dstIP when locIP
// is any IP address ("0.0.0.0" or "::"). If locIP is a non-any addr,
// this method simply returns locIP.
// caller must hold the mutex
func (v *vNet) determineSourceIP(locIP, dstIP net.IP) net.IP {
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
			ip := addr.(*net.IPNet).IP
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
func (v *vNet) hasIPAddr(ip net.IP) bool { //nolint:gocognit
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
func (v *vNet) allocateLocalAddr(ip net.IP, port int) error {
	// gather local IP addresses to bind
	var ips []net.IP
	if ip.IsUnspecified() {
		ips = v.getAllIPAddrs(ip.To4() == nil)
	} else if v.hasIPAddr(ip) {
		ips = []net.IP{ip}
	}

	if len(ips) == 0 {
		return fmt.Errorf("%w %s", errBindFailerFor, ip.String())
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
				Net:  udpString,
				Addr: addr,
				Err:  fmt.Errorf("bind: %w", errAddressAlreadyInUse),
			}
		}
	}

	return nil
}

// caller must hold the mutex
func (v *vNet) assignPort(ip net.IP, start, end int) (int, error) {
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

// NetConfig is a bag of configuration parameters passed to NewNet().
type NetConfig struct {
	// StaticIPs is an array of static IP addresses to be assigned for this Net.
	// If no static IP address is given, the router will automatically assign
	// an IP address.
	StaticIPs []string

	// StaticIP is deprecated. Use StaticIPs.
	StaticIP string
}

// Net represents a local network stack euivalent to a set of layers from NIC
// up to the transport (UDP / TCP) layer.
type Net struct {
	v   *vNet
	ifs []*Interface
}

// NewNet creates an instance of Net.
// If config is nil, the virtual network is disabled. (uses corresponding
// net.Xxxx() operations.
// By design, it always have lo0 and eth0 interfaces.
// The lo0 has the address 127.0.0.1 assigned by default.
// IP address for eth0 will be assigned when this Net is added to a router.
func NewNet(config *NetConfig) *Net {
	if config == nil {
		ifs := []*Interface{}
		if orgIfs, err := net.Interfaces(); err == nil {
			for _, orgIfc := range orgIfs {
				ifc := NewInterface(orgIfc)
				if addrs, err := orgIfc.Addrs(); err == nil {
					for _, addr := range addrs {
						ifc.AddAddr(addr)
					}
				}

				ifs = append(ifs, ifc)
			}
		}

		return &Net{ifs: ifs}
	}

	lo0 := NewInterface(net.Interface{
		Index:        1,
		MTU:          16384,
		Name:         lo0String,
		HardwareAddr: nil,
		Flags:        net.FlagUp | net.FlagLoopback | net.FlagMulticast,
	})
	lo0.AddAddr(&net.IPNet{
		IP:   net.ParseIP("127.0.0.1"),
		Mask: net.CIDRMask(8, 32),
	})

	eth0 := NewInterface(net.Interface{
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

	v := &vNet{
		interfaces: []*Interface{lo0, eth0},
		staticIPs:  staticIPs,
		udpConns:   newUDPConnMap(),
	}

	return &Net{
		v: v,
	}
}

// Interfaces returns a list of the system's network interfaces.
func (n *Net) Interfaces() ([]*Interface, error) {
	if n.v == nil {
		return n.ifs, nil
	}

	return n.v.getInterfaces()
}

// InterfaceByName returns the interface specified by name.
func (n *Net) InterfaceByName(name string) (*Interface, error) {
	if n.v == nil {
		for _, ifc := range n.ifs {
			if ifc.Name == name {
				return ifc, nil
			}
		}

		return nil, fmt.Errorf("interface %s %w", name, errNotFound)
	}

	return n.v.getInterface(name)
}

// ListenPacket announces on the local network address.
func (n *Net) ListenPacket(network string, address string) (net.PacketConn, error) {
	if n.v == nil {
		return net.ListenPacket(network, address)
	}

	return n.v.listenPacket(network, address)
}

// ListenUDP acts like ListenPacket for UDP networks.
func (n *Net) ListenUDP(network string, locAddr *net.UDPAddr) (UDPPacketConn, error) {
	if n.v == nil {
		return net.ListenUDP(network, locAddr)
	}

	return n.v.listenUDP(network, locAddr)
}

// Dial connects to the address on the named network.
func (n *Net) Dial(network, address string) (net.Conn, error) {
	if n.v == nil {
		return net.Dial(network, address)
	}

	return n.v.dial(network, address)
}

// CreateDialer creates an instance of vnet.Dialer
func (n *Net) CreateDialer(dialer *net.Dialer) Dialer {
	if n.v == nil {
		return &vDialer{
			dialer: dialer,
		}
	}

	return &vDialer{
		dialer: dialer,
		v:      n.v,
	}
}

// DialUDP acts like Dial for UDP networks.
func (n *Net) DialUDP(network string, laddr, raddr *net.UDPAddr) (UDPPacketConn, error) {
	if n.v == nil {
		return net.DialUDP(network, laddr, raddr)
	}

	return n.v.dialUDP(network, laddr, raddr)
}

// ResolveUDPAddr returns an address of UDP end point.
func (n *Net) ResolveUDPAddr(network, address string) (*net.UDPAddr, error) {
	if n.v == nil {
		return net.ResolveUDPAddr(network, address)
	}

	return n.v.resolveUDPAddr(network, address)
}

func (n *Net) getInterface(ifName string) (*Interface, error) {
	if n.v == nil {
		return nil, errVNetDisabled
	}

	return n.v.getInterface(ifName)
}

func (n *Net) setRouter(r *Router) error {
	if n.v == nil {
		return errVNetDisabled
	}

	return n.v.setRouter(r)
}

func (n *Net) onInboundChunk(c Chunk) {
	if n.v == nil {
		return
	}

	n.v.onInboundChunk(c)
}

func (n *Net) getStaticIPs() []net.IP {
	if n.v == nil {
		return nil
	}

	return n.v.staticIPs
}

// IsVirtual tests if the virtual network is enabled.
func (n *Net) IsVirtual() bool {
	return n.v != nil
}

// Dialer is identical to net.Dialer excepts that its methods
// (Dial, DialContext) are overridden to use virtual network.
// Use vnet.CreateDialer() to create an instance of this Dialer.
type Dialer interface {
	Dial(network, address string) (net.Conn, error)
}

type vDialer struct {
	dialer *net.Dialer
	v      *vNet
}

func (d *vDialer) Dial(network, address string) (net.Conn, error) {
	if d.v == nil {
		return d.dialer.Dial(network, address)
	}

	return d.v.dial(network, address)
}
