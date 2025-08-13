// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package transport implements various networking related
// functions used throughout the Pion modules.
package transport

import (
	"errors"
	"io"
	"net"
	"time"
)

var (
	// ErrNoAddressAssigned ...
	ErrNoAddressAssigned = errors.New("no address assigned")
	// ErrNotSupported ...
	ErrNotSupported = errors.New("not supported yey")
	// ErrInterfaceNotFound ...
	ErrInterfaceNotFound = errors.New("interface not found")
	// ErrNotUDPAddress ...
	ErrNotUDPAddress = errors.New("not a UDP address")
)

// Net is an interface providing common networking functions which are
// similar to the functions provided by standard net package.
type Net interface {
	// ListenPacket announces on the local network address.
	//
	// The network must be "udp", "udp4", "udp6", "unixgram", or an IP
	// transport. The IP transports are "ip", "ip4", or "ip6" followed by
	// a colon and a literal protocol number or a protocol name, as in
	// "ip:1" or "ip:icmp".
	//
	// For UDP and IP networks, if the host in the address parameter is
	// empty or a literal unspecified IP address, ListenPacket listens on
	// all available IP addresses of the local system except multicast IP
	// addresses.
	// To only use IPv4, use network "udp4" or "ip4:proto".
	// The address can use a host name, but this is not recommended,
	// because it will create a listener for at most one of the host's IP
	// addresses.
	// If the port in the address parameter is empty or "0", as in
	// "127.0.0.1:" or "[::1]:0", a port number is automatically chosen.
	// The LocalAddr method of PacketConn can be used to discover the
	// chosen port.
	//
	// See func Dial for a description of the network and address
	// parameters.
	//
	// ListenPacket uses context.Background internally; to specify the context, use
	// ListenConfig.ListenPacket.
	ListenPacket(network string, address string) (net.PacketConn, error)

	// ListenUDP acts like ListenPacket for UDP networks.
	//
	// The network must be a UDP network name; see func Dial for details.
	//
	// If the IP field of laddr is nil or an unspecified IP address,
	// ListenUDP listens on all available IP addresses of the local system
	// except multicast IP addresses.
	// If the Port field of laddr is 0, a port number is automatically
	// chosen.
	ListenUDP(network string, locAddr *net.UDPAddr) (UDPConn, error)

	// ListenTCP acts like Listen for TCP networks.
	//
	// The network must be a TCP network name; see func Dial for details.
	//
	// If the IP field of laddr is nil or an unspecified IP address,
	// ListenTCP listens on all available unicast and anycast IP addresses
	// of the local system.
	// If the Port field of laddr is 0, a port number is automatically
	// chosen.
	ListenTCP(network string, laddr *net.TCPAddr) (TCPListener, error)

	// Dial connects to the address on the named network.
	//
	// Known networks are "tcp", "tcp4" (IPv4-only), "tcp6" (IPv6-only),
	// "udp", "udp4" (IPv4-only), "udp6" (IPv6-only), "ip", "ip4"
	// (IPv4-only), "ip6" (IPv6-only), "unix", "unixgram" and
	// "unixpacket".
	//
	// For TCP and UDP networks, the address has the form "host:port".
	// The host must be a literal IP address, or a host name that can be
	// resolved to IP addresses.
	// The port must be a literal port number or a service name.
	// If the host is a literal IPv6 address it must be enclosed in square
	// brackets, as in "[2001:db8::1]:80" or "[fe80::1%zone]:80".
	// The zone specifies the scope of the literal IPv6 address as defined
	// in RFC 4007.
	// The functions JoinHostPort and SplitHostPort manipulate a pair of
	// host and port in this form.
	// When using TCP, and the host resolves to multiple IP addresses,
	// Dial will try each IP address in order until one succeeds.
	//
	// Examples:
	//
	//	Dial("tcp", "golang.org:http")
	//	Dial("tcp", "192.0.2.1:http")
	//	Dial("tcp", "198.51.100.1:80")
	//	Dial("udp", "[2001:db8::1]:domain")
	//	Dial("udp", "[fe80::1%lo0]:53")
	//	Dial("tcp", ":80")
	//
	// For IP networks, the network must be "ip", "ip4" or "ip6" followed
	// by a colon and a literal protocol number or a protocol name, and
	// the address has the form "host". The host must be a literal IP
	// address or a literal IPv6 address with zone.
	// It depends on each operating system how the operating system
	// behaves with a non-well known protocol number such as "0" or "255".
	//
	// Examples:
	//
	//	Dial("ip4:1", "192.0.2.1")
	//	Dial("ip6:ipv6-icmp", "2001:db8::1")
	//	Dial("ip6:58", "fe80::1%lo0")
	//
	// For TCP, UDP and IP networks, if the host is empty or a literal
	// unspecified IP address, as in ":80", "0.0.0.0:80" or "[::]:80" for
	// TCP and UDP, "", "0.0.0.0" or "::" for IP, the local system is
	// assumed.
	//
	// For Unix networks, the address must be a file system path.
	Dial(network, address string) (net.Conn, error)

	// DialUDP acts like Dial for UDP networks.
	//
	// The network must be a UDP network name; see func Dial for details.
	//
	// If laddr is nil, a local address is automatically chosen.
	// If the IP field of raddr is nil or an unspecified IP address, the
	// local system is assumed.
	DialUDP(network string, laddr, raddr *net.UDPAddr) (UDPConn, error)

	// DialTCP acts like Dial for TCP networks.
	//
	// The network must be a TCP network name; see func Dial for details.
	//
	// If laddr is nil, a local address is automatically chosen.
	// If the IP field of raddr is nil or an unspecified IP address, the
	// local system is assumed.
	DialTCP(network string, laddr, raddr *net.TCPAddr) (TCPConn, error)

	// ResolveIPAddr returns an address of IP end point.
	//
	// The network must be an IP network name.
	//
	// If the host in the address parameter is not a literal IP address,
	// ResolveIPAddr resolves the address to an address of IP end point.
	// Otherwise, it parses the address as a literal IP address.
	// The address parameter can use a host name, but this is not
	// recommended, because it will return at most one of the host name's
	// IP addresses.
	//
	// See func Dial for a description of the network and address
	// parameters.
	ResolveIPAddr(network, address string) (*net.IPAddr, error)

	// ResolveUDPAddr returns an address of UDP end point.
	//
	// The network must be a UDP network name.
	//
	// If the host in the address parameter is not a literal IP address or
	// the port is not a literal port number, ResolveUDPAddr resolves the
	// address to an address of UDP end point.
	// Otherwise, it parses the address as a pair of literal IP address
	// and port number.
	// The address parameter can use a host name, but this is not
	// recommended, because it will return at most one of the host name's
	// IP addresses.
	//
	// See func Dial for a description of the network and address
	// parameters.
	ResolveUDPAddr(network, address string) (*net.UDPAddr, error)

	// ResolveTCPAddr returns an address of TCP end point.
	//
	// The network must be a TCP network name.
	//
	// If the host in the address parameter is not a literal IP address or
	// the port is not a literal port number, ResolveTCPAddr resolves the
	// address to an address of TCP end point.
	// Otherwise, it parses the address as a pair of literal IP address
	// and port number.
	// The address parameter can use a host name, but this is not
	// recommended, because it will return at most one of the host name's
	// IP addresses.
	//
	// See func Dial for a description of the network and address
	// parameters.
	ResolveTCPAddr(network, address string) (*net.TCPAddr, error)

	// Interfaces returns a list of the system's network interfaces.
	Interfaces() ([]*Interface, error)

	// InterfaceByIndex returns the interface specified by index.
	//
	// On Solaris, it returns one of the logical network interfaces
	// sharing the logical data link; for more precision use
	// InterfaceByName.
	InterfaceByIndex(index int) (*Interface, error)

	// InterfaceByName returns the interface specified by name.
	InterfaceByName(name string) (*Interface, error)

	// The following functions are extensions to Go's standard net package

	CreateDialer(dialer *net.Dialer) Dialer
}

// Dialer is identical to net.Dialer excepts that its methods
// (Dial, DialContext) are overridden to use the Net interface.
// Use vnet.CreateDialer() to create an instance of this Dialer.
type Dialer interface {
	Dial(network, address string) (net.Conn, error)
}

// UDPConn is packet-oriented connection for UDP.
type UDPConn interface {
	// Close closes the connection.
	// Any blocked Read or Write operations will be unblocked and return errors.
	Close() error

	// LocalAddr returns the local network address, if known.
	LocalAddr() net.Addr

	// RemoteAddr returns the remote network address, if known.
	RemoteAddr() net.Addr

	// SetDeadline sets the read and write deadlines associated
	// with the connection. It is equivalent to calling both
	// SetReadDeadline and SetWriteDeadline.
	//
	// A deadline is an absolute time after which I/O operations
	// fail instead of blocking. The deadline applies to all future
	// and pending I/O, not just the immediately following call to
	// Read or Write. After a deadline has been exceeded, the
	// connection can be refreshed by setting a deadline in the future.
	//
	// If the deadline is exceeded a call to Read or Write or to other
	// I/O methods will return an error that wraps os.ErrDeadlineExceeded.
	// This can be tested using errors.Is(err, os.ErrDeadlineExceeded).
	// The error's Timeout method will return true, but note that there
	// are other possible errors for which the Timeout method will
	// return true even if the deadline has not been exceeded.
	//
	// An idle timeout can be implemented by repeatedly extending
	// the deadline after successful Read or Write calls.
	//
	// A zero value for t means I/O operations will not time out.
	SetDeadline(t time.Time) error

	// SetReadDeadline sets the deadline for future Read calls
	// and any currently-blocked Read call.
	// A zero value for t means Read will not time out.
	SetReadDeadline(t time.Time) error

	// SetWriteDeadline sets the deadline for future Write calls
	// and any currently-blocked Write call.
	// Even if write times out, it may return n > 0, indicating that
	// some of the data was successfully written.
	// A zero value for t means Write will not time out.
	SetWriteDeadline(t time.Time) error

	// SetReadBuffer sets the size of the operating system's
	// receive buffer associated with the connection.
	SetReadBuffer(bytes int) error

	// SetWriteBuffer sets the size of the operating system's
	// transmit buffer associated with the connection.
	SetWriteBuffer(bytes int) error

	// Read reads data from the connection.
	// Read can be made to time out and return an error after a fixed
	// time limit; see SetDeadline and SetReadDeadline.
	Read(b []byte) (n int, err error)

	// ReadFrom reads a packet from the connection,
	// copying the payload into p. It returns the number of
	// bytes copied into p and the return address that
	// was on the packet.
	// It returns the number of bytes read (0 <= n <= len(p))
	// and any error encountered. Callers should always process
	// the n > 0 bytes returned before considering the error err.
	// ReadFrom can be made to time out and return an error after a
	// fixed time limit; see SetDeadline and SetReadDeadline.
	ReadFrom(p []byte) (n int, addr net.Addr, err error)

	// ReadFromUDP acts like ReadFrom but returns a UDPAddr.
	ReadFromUDP(b []byte) (n int, addr *net.UDPAddr, err error)

	// ReadMsgUDP reads a message from c, copying the payload into b and
	// the associated out-of-band data into oob. It returns the number of
	// bytes copied into b, the number of bytes copied into oob, the flags
	// that were set on the message and the source address of the message.
	//
	// The packages golang.org/x/net/ipv4 and golang.org/x/net/ipv6 can be
	// used to manipulate IP-level socket options in oob.
	ReadMsgUDP(b, oob []byte) (n, oobn, flags int, addr *net.UDPAddr, err error)

	// Write writes data to the connection.
	// Write can be made to time out and return an error after a fixed
	// time limit; see SetDeadline and SetWriteDeadline.
	Write(b []byte) (n int, err error)

	// WriteTo writes a packet with payload p to addr.
	// WriteTo can be made to time out and return an Error after a
	// fixed time limit; see SetDeadline and SetWriteDeadline.
	// On packet-oriented connections, write timeouts are rare.
	WriteTo(p []byte, addr net.Addr) (n int, err error)

	// WriteToUDP acts like WriteTo but takes a UDPAddr.
	WriteToUDP(b []byte, addr *net.UDPAddr) (int, error)

	// WriteMsgUDP writes a message to addr via c if c isn't connected, or
	// to c's remote address if c is connected (in which case addr must be
	// nil). The payload is copied from b and the associated out-of-band
	// data is copied from oob. It returns the number of payload and
	// out-of-band bytes written.
	//
	// The packages golang.org/x/net/ipv4 and golang.org/x/net/ipv6 can be
	// used to manipulate IP-level socket options in oob.
	WriteMsgUDP(b, oob []byte, addr *net.UDPAddr) (n, oobn int, err error)
}

// TCPConn is an interface for TCP network connections.
type TCPConn interface {
	net.Conn

	// CloseRead shuts down the reading side of the TCP connection.
	// Most callers should just use Close.
	CloseRead() error

	// CloseWrite shuts down the writing side of the TCP connection.
	// Most callers should just use Close.
	CloseWrite() error

	// ReadFrom implements the io.ReaderFrom ReadFrom method.
	ReadFrom(r io.Reader) (int64, error)

	// SetLinger sets the behavior of Close on a connection which still
	// has data waiting to be sent or to be acknowledged.
	//
	// If sec < 0 (the default), the operating system finishes sending the
	// data in the background.
	//
	// If sec == 0, the operating system discards any unsent or
	// unacknowledged data.
	//
	// If sec > 0, the data is sent in the background as with sec < 0. On
	// some operating systems after sec seconds have elapsed any remaining
	// unsent data may be discarded.
	SetLinger(sec int) error

	// SetKeepAlive sets whether the operating system should send
	// keep-alive messages on the connection.
	SetKeepAlive(keepalive bool) error

	// SetKeepAlivePeriod sets period between keep-alives.
	SetKeepAlivePeriod(d time.Duration) error

	// SetNoDelay controls whether the operating system should delay
	// packet transmission in hopes of sending fewer packets (Nagle's
	// algorithm).  The default is true (no delay), meaning that data is
	// sent as soon as possible after a Write.
	SetNoDelay(noDelay bool) error

	// SetWriteBuffer sets the size of the operating system's
	// transmit buffer associated with the connection.
	SetWriteBuffer(bytes int) error

	// SetReadBuffer sets the size of the operating system's
	// receive buffer associated with the connection.
	SetReadBuffer(bytes int) error
}

// TCPListener is a TCP network listener. Clients should typically
// use variables of type Listener instead of assuming TCP.
type TCPListener interface {
	net.Listener

	// AcceptTCP accepts the next incoming call and returns the new
	// connection.
	AcceptTCP() (TCPConn, error)

	// SetDeadline sets the deadline associated with the listener.
	// A zero time value disables the deadline.
	SetDeadline(t time.Time) error
}

// Interface wraps a standard net.Interfaces and its assigned addresses
type Interface struct {
	net.Interface
	addrs []net.Addr
}

// NewInterface creates a new interface based of a standard net.Interface
func NewInterface(ifc net.Interface) *Interface {
	return &Interface{
		Interface: ifc,
		addrs:     nil,
	}
}

// AddAddress adds a new address to the interface
func (ifc *Interface) AddAddress(addr net.Addr) {
	ifc.addrs = append(ifc.addrs, addr)
}

// Addrs returns a slice of configured addresses on the interface
func (ifc *Interface) Addrs() ([]net.Addr, error) {
	if len(ifc.addrs) == 0 {
		return nil, ErrNoAddressAssigned
	}
	return ifc.addrs, nil
}
