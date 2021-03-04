package vnet

import (
	"fmt"
	"net"
	"strconv"
	"strings"
	"sync/atomic"
	"time"
)

type tcpFlag uint8

const (
	tcpFIN tcpFlag = 0x01
	tcpSYN tcpFlag = 0x02
	tcpRST tcpFlag = 0x04
	tcpPSH tcpFlag = 0x08
	tcpACK tcpFlag = 0x10
)

func (f tcpFlag) String() string {
	var sa []string
	if f&tcpFIN != 0 {
		sa = append(sa, "FIN")
	}
	if f&tcpSYN != 0 {
		sa = append(sa, "SYN")
	}
	if f&tcpRST != 0 {
		sa = append(sa, "RST")
	}
	if f&tcpPSH != 0 {
		sa = append(sa, "PSH")
	}
	if f&tcpACK != 0 {
		sa = append(sa, "ACK")
	}

	return strings.Join(sa, "-")
}

// Generate a base36-encoded unique tag
// See: https://play.golang.org/p/0ZaAID1q-HN
var assignChunkTag = func() func() string { //nolint:gochecknoglobals
	var tagCtr uint64

	return func() string {
		n := atomic.AddUint64(&tagCtr, 1)
		return strconv.FormatUint(n, 36)
	}
}()

// Chunk represents a packet passed around in the vnet
type Chunk interface {
	setTimestamp() time.Time                 // used by router
	getTimestamp() time.Time                 // used by router
	getSourceIP() net.IP                     // used by router
	getDestinationIP() net.IP                // used by router
	setSourceAddr(address string) error      // used by nat
	setDestinationAddr(address string) error // used by nat

	SourceAddr() net.Addr
	DestinationAddr() net.Addr
	UserData() []byte
	Tag() string
	Clone() Chunk
	Network() string // returns "udp" or "tcp"
	String() string
}

type chunkIP struct {
	timestamp     time.Time
	sourceIP      net.IP
	destinationIP net.IP
	tag           string
}

func (c *chunkIP) setTimestamp() time.Time {
	c.timestamp = time.Now()
	return c.timestamp
}

func (c *chunkIP) getTimestamp() time.Time {
	return c.timestamp
}

func (c *chunkIP) getDestinationIP() net.IP {
	return c.destinationIP
}

func (c *chunkIP) getSourceIP() net.IP {
	return c.sourceIP
}

func (c *chunkIP) Tag() string {
	return c.tag
}

type chunkUDP struct {
	chunkIP
	sourcePort      int
	destinationPort int
	userData        []byte
}

func newChunkUDP(srcAddr, dstAddr *net.UDPAddr) *chunkUDP {
	return &chunkUDP{
		chunkIP: chunkIP{
			sourceIP:      srcAddr.IP,
			destinationIP: dstAddr.IP,
			tag:           assignChunkTag(),
		},
		sourcePort:      srcAddr.Port,
		destinationPort: dstAddr.Port,
	}
}

func (c *chunkUDP) SourceAddr() net.Addr {
	return &net.UDPAddr{
		IP:   c.sourceIP,
		Port: c.sourcePort,
	}
}

func (c *chunkUDP) DestinationAddr() net.Addr {
	return &net.UDPAddr{
		IP:   c.destinationIP,
		Port: c.destinationPort,
	}
}

func (c *chunkUDP) UserData() []byte {
	return c.userData
}

func (c *chunkUDP) Clone() Chunk {
	var userData []byte
	if c.userData != nil {
		userData = make([]byte, len(c.userData))
		copy(userData, c.userData)
	}

	return &chunkUDP{
		chunkIP: chunkIP{
			timestamp:     c.timestamp,
			sourceIP:      c.sourceIP,
			destinationIP: c.destinationIP,
			tag:           c.tag,
		},
		sourcePort:      c.sourcePort,
		destinationPort: c.destinationPort,
		userData:        userData,
	}
}

func (c *chunkUDP) Network() string {
	return udpString
}

func (c *chunkUDP) String() string {
	src := c.SourceAddr()
	dst := c.DestinationAddr()
	return fmt.Sprintf("%s chunk %s %s => %s",
		src.Network(),
		c.tag,
		src.String(),
		dst.String(),
	)
}

func (c *chunkUDP) setSourceAddr(address string) error {
	addr, err := net.ResolveUDPAddr(udpString, address)
	if err != nil {
		return err
	}
	c.sourceIP = addr.IP
	c.sourcePort = addr.Port
	return nil
}

func (c *chunkUDP) setDestinationAddr(address string) error {
	addr, err := net.ResolveUDPAddr(udpString, address)
	if err != nil {
		return err
	}
	c.destinationIP = addr.IP
	c.destinationPort = addr.Port
	return nil
}

type chunkTCP struct {
	chunkIP
	sourcePort      int
	destinationPort int
	flags           tcpFlag // control bits
	userData        []byte  // only with PSH flag
	// seq             uint32  // always starts with 0
	// ack             uint32  // always starts with 0
}

func newChunkTCP(srcAddr, dstAddr *net.TCPAddr, flags tcpFlag) *chunkTCP {
	return &chunkTCP{
		chunkIP: chunkIP{
			sourceIP:      srcAddr.IP,
			destinationIP: dstAddr.IP,
			tag:           assignChunkTag(),
		},
		sourcePort:      srcAddr.Port,
		destinationPort: dstAddr.Port,
		flags:           flags,
	}
}

func (c *chunkTCP) SourceAddr() net.Addr {
	return &net.TCPAddr{
		IP:   c.sourceIP,
		Port: c.sourcePort,
	}
}

func (c *chunkTCP) DestinationAddr() net.Addr {
	return &net.TCPAddr{
		IP:   c.destinationIP,
		Port: c.destinationPort,
	}
}

func (c *chunkTCP) UserData() []byte {
	return c.userData
}

func (c *chunkTCP) Clone() Chunk {
	userData := make([]byte, len(c.userData))
	copy(userData, c.userData)

	return &chunkTCP{
		chunkIP: chunkIP{
			timestamp:     c.timestamp,
			sourceIP:      c.sourceIP,
			destinationIP: c.destinationIP,
		},
		sourcePort:      c.sourcePort,
		destinationPort: c.destinationPort,
		userData:        userData,
	}
}

func (c *chunkTCP) Network() string {
	return "tcp"
}

func (c *chunkTCP) String() string {
	src := c.SourceAddr()
	dst := c.DestinationAddr()
	return fmt.Sprintf("%s %s chunk %s %s => %s",
		src.Network(),
		c.flags.String(),
		c.tag,
		src.String(),
		dst.String(),
	)
}

func (c *chunkTCP) setSourceAddr(address string) error {
	addr, err := net.ResolveTCPAddr("tcp", address)
	if err != nil {
		return err
	}
	c.sourceIP = addr.IP
	c.sourcePort = addr.Port
	return nil
}

func (c *chunkTCP) setDestinationAddr(address string) error {
	addr, err := net.ResolveTCPAddr("tcp", address)
	if err != nil {
		return err
	}
	c.destinationIP = addr.IP
	c.destinationPort = addr.Port
	return nil
}
