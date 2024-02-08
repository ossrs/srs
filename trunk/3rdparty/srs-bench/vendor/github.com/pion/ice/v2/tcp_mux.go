// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"encoding/binary"
	"errors"
	"io"
	"net"
	"strings"
	"sync"

	"github.com/pion/logging"
	"github.com/pion/stun"
)

// ErrGetTransportAddress can't convert net.Addr to underlying type (UDPAddr or TCPAddr).
var ErrGetTransportAddress = errors.New("failed to get local transport address")

// TCPMux is allows grouping multiple TCP net.Conns and using them like UDP
// net.PacketConns. The main implementation of this is TCPMuxDefault, and this
// interface exists to allow mocking in tests.
type TCPMux interface {
	io.Closer
	GetConnByUfrag(ufrag string, isIPv6 bool, local net.IP) (net.PacketConn, error)
	RemoveConnByUfrag(ufrag string)
}

type ipAddr string

// TCPMuxDefault muxes TCP net.Conns into net.PacketConns and groups them by
// Ufrag. It is a default implementation of TCPMux interface.
type TCPMuxDefault struct {
	params *TCPMuxParams
	closed bool

	// connsIPv4 and connsIPv6 are maps of all tcpPacketConns indexed by ufrag and local address
	connsIPv4, connsIPv6 map[string]map[ipAddr]*tcpPacketConn

	mu sync.Mutex
	wg sync.WaitGroup
}

// TCPMuxParams are parameters for TCPMux.
type TCPMuxParams struct {
	Listener       net.Listener
	Logger         logging.LeveledLogger
	ReadBufferSize int

	// Maximum buffer size for write op. 0 means no write buffer, the write op will block until the whole packet is written
	// if the write buffer is full, the subsequent write packet will be dropped until it has enough space.
	// a default 4MB is recommended.
	WriteBufferSize int
}

// NewTCPMuxDefault creates a new instance of TCPMuxDefault.
func NewTCPMuxDefault(params TCPMuxParams) *TCPMuxDefault {
	if params.Logger == nil {
		params.Logger = logging.NewDefaultLoggerFactory().NewLogger("ice")
	}

	m := &TCPMuxDefault{
		params: &params,

		connsIPv4: map[string]map[ipAddr]*tcpPacketConn{},
		connsIPv6: map[string]map[ipAddr]*tcpPacketConn{},
	}

	m.wg.Add(1)
	go func() {
		defer m.wg.Done()
		m.start()
	}()

	return m
}

func (m *TCPMuxDefault) start() {
	m.params.Logger.Infof("Listening TCP on %s", m.params.Listener.Addr())
	for {
		conn, err := m.params.Listener.Accept()
		if err != nil {
			m.params.Logger.Infof("Error accepting connection: %s", err)
			return
		}

		m.params.Logger.Debugf("Accepted connection from: %s to %s", conn.RemoteAddr(), conn.LocalAddr())

		m.wg.Add(1)
		go func() {
			defer m.wg.Done()
			m.handleConn(conn)
		}()
	}
}

// LocalAddr returns the listening address of this TCPMuxDefault.
func (m *TCPMuxDefault) LocalAddr() net.Addr {
	return m.params.Listener.Addr()
}

// GetConnByUfrag retrieves an existing or creates a new net.PacketConn.
func (m *TCPMuxDefault) GetConnByUfrag(ufrag string, isIPv6 bool, local net.IP) (net.PacketConn, error) {
	m.mu.Lock()
	defer m.mu.Unlock()

	if m.closed {
		return nil, io.ErrClosedPipe
	}

	if conn, ok := m.getConn(ufrag, isIPv6, local); ok {
		return conn, nil
	}

	return m.createConn(ufrag, isIPv6, local)
}

func (m *TCPMuxDefault) createConn(ufrag string, isIPv6 bool, local net.IP) (*tcpPacketConn, error) {
	addr, ok := m.LocalAddr().(*net.TCPAddr)
	if !ok {
		return nil, ErrGetTransportAddress
	}
	localAddr := *addr
	localAddr.IP = local

	conn := newTCPPacketConn(tcpPacketParams{
		ReadBuffer:  m.params.ReadBufferSize,
		WriteBuffer: m.params.WriteBufferSize,
		LocalAddr:   &localAddr,
		Logger:      m.params.Logger,
	})

	var conns map[ipAddr]*tcpPacketConn
	if isIPv6 {
		if conns, ok = m.connsIPv6[ufrag]; !ok {
			conns = make(map[ipAddr]*tcpPacketConn)
			m.connsIPv6[ufrag] = conns
		}
	} else {
		if conns, ok = m.connsIPv4[ufrag]; !ok {
			conns = make(map[ipAddr]*tcpPacketConn)
			m.connsIPv4[ufrag] = conns
		}
	}
	conns[ipAddr(local.String())] = conn

	m.wg.Add(1)
	go func() {
		defer m.wg.Done()
		<-conn.CloseChannel()
		m.removeConnByUfragAndLocalHost(ufrag, local)
	}()

	return conn, nil
}

func (m *TCPMuxDefault) closeAndLogError(closer io.Closer) {
	err := closer.Close()
	if err != nil {
		m.params.Logger.Warnf("Error closing connection: %s", err)
	}
}

func (m *TCPMuxDefault) handleConn(conn net.Conn) {
	buf := make([]byte, receiveMTU)

	n, err := readStreamingPacket(conn, buf)
	if err != nil {
		m.params.Logger.Warnf("Error reading first packet from %s: %s", conn.RemoteAddr().String(), err)
		return
	}

	buf = buf[:n]

	msg := &stun.Message{
		Raw: make([]byte, len(buf)),
	}
	// Explicitly copy raw buffer so Message can own the memory.
	copy(msg.Raw, buf)
	if err = msg.Decode(); err != nil {
		m.closeAndLogError(conn)
		m.params.Logger.Warnf("Failed to handle decode ICE from %s to %s: %v", conn.RemoteAddr(), conn.LocalAddr(), err)
		return
	}

	if m == nil || msg.Type.Method != stun.MethodBinding { // Not a STUN
		m.closeAndLogError(conn)
		m.params.Logger.Warnf("Not a STUN message from %s to %s", conn.RemoteAddr(), conn.LocalAddr())
		return
	}

	for _, attr := range msg.Attributes {
		m.params.Logger.Debugf("msg attr: %s", attr.String())
	}

	attr, err := msg.Get(stun.AttrUsername)
	if err != nil {
		m.closeAndLogError(conn)
		m.params.Logger.Warnf("No Username attribute in STUN message from %s to %s", conn.RemoteAddr(), conn.LocalAddr())
		return
	}

	ufrag := strings.Split(string(attr), ":")[0]
	m.params.Logger.Debugf("Ufrag: %s", ufrag)

	m.mu.Lock()
	defer m.mu.Unlock()

	host, _, err := net.SplitHostPort(conn.RemoteAddr().String())
	if err != nil {
		m.closeAndLogError(conn)
		m.params.Logger.Warnf("Failed to get host in STUN message from %s to %s", conn.RemoteAddr(), conn.LocalAddr())
		return
	}

	isIPv6 := net.ParseIP(host).To4() == nil

	localAddr, ok := conn.LocalAddr().(*net.TCPAddr)
	if !ok {
		m.closeAndLogError(conn)
		m.params.Logger.Warnf("Failed to get local tcp address in STUN message from %s to %s", conn.RemoteAddr(), conn.LocalAddr())
		return
	}
	packetConn, ok := m.getConn(ufrag, isIPv6, localAddr.IP)
	if !ok {
		packetConn, err = m.createConn(ufrag, isIPv6, localAddr.IP)
		if err != nil {
			m.closeAndLogError(conn)
			m.params.Logger.Warnf("Failed to create packetConn for STUN message from %s to %s", conn.RemoteAddr(), conn.LocalAddr())
			return
		}
	}

	if err := packetConn.AddConn(conn, buf); err != nil {
		m.closeAndLogError(conn)
		m.params.Logger.Warnf("Error adding conn to tcpPacketConn from %s to %s: %s", conn.RemoteAddr(), conn.LocalAddr(), err)
		return
	}
}

// Close closes the listener and waits for all goroutines to exit.
func (m *TCPMuxDefault) Close() error {
	m.mu.Lock()
	m.closed = true

	for _, conns := range m.connsIPv4 {
		for _, conn := range conns {
			m.closeAndLogError(conn)
		}
	}
	for _, conns := range m.connsIPv6 {
		for _, conn := range conns {
			m.closeAndLogError(conn)
		}
	}

	m.connsIPv4 = map[string]map[ipAddr]*tcpPacketConn{}
	m.connsIPv6 = map[string]map[ipAddr]*tcpPacketConn{}

	err := m.params.Listener.Close()

	m.mu.Unlock()

	m.wg.Wait()

	return err
}

// RemoveConnByUfrag closes and removes a net.PacketConn by Ufrag.
func (m *TCPMuxDefault) RemoveConnByUfrag(ufrag string) {
	removedConns := make([]*tcpPacketConn, 0, 4)

	// Keep lock section small to avoid deadlock with conn lock
	m.mu.Lock()
	if conns, ok := m.connsIPv4[ufrag]; ok {
		delete(m.connsIPv4, ufrag)
		for _, conn := range conns {
			removedConns = append(removedConns, conn)
		}
	}
	if conns, ok := m.connsIPv6[ufrag]; ok {
		delete(m.connsIPv6, ufrag)
		for _, conn := range conns {
			removedConns = append(removedConns, conn)
		}
	}

	m.mu.Unlock()

	// Close the connections outside the critical section to avoid
	// deadlocking TCP mux if (*tcpPacketConn).Close() blocks.
	for _, conn := range removedConns {
		m.closeAndLogError(conn)
	}
}

func (m *TCPMuxDefault) removeConnByUfragAndLocalHost(ufrag string, local net.IP) {
	removedConns := make([]*tcpPacketConn, 0, 4)

	localIP := ipAddr(local.String())
	// Keep lock section small to avoid deadlock with conn lock
	m.mu.Lock()
	if conns, ok := m.connsIPv4[ufrag]; ok {
		if conn, ok := conns[localIP]; ok {
			delete(conns, localIP)
			if len(conns) == 0 {
				delete(m.connsIPv4, ufrag)
			}
			removedConns = append(removedConns, conn)
		}
	}
	if conns, ok := m.connsIPv6[ufrag]; ok {
		if conn, ok := conns[localIP]; ok {
			delete(conns, localIP)
			if len(conns) == 0 {
				delete(m.connsIPv6, ufrag)
			}
			removedConns = append(removedConns, conn)
		}
	}
	m.mu.Unlock()

	// Close the connections outside the critical section to avoid
	// deadlocking TCP mux if (*tcpPacketConn).Close() blocks.
	for _, conn := range removedConns {
		m.closeAndLogError(conn)
	}
}

func (m *TCPMuxDefault) getConn(ufrag string, isIPv6 bool, local net.IP) (val *tcpPacketConn, ok bool) {
	var conns map[ipAddr]*tcpPacketConn
	if isIPv6 {
		conns, ok = m.connsIPv6[ufrag]
	} else {
		conns, ok = m.connsIPv4[ufrag]
	}
	if conns != nil {
		val, ok = conns[ipAddr(local.String())]
	}

	return
}

const streamingPacketHeaderLen = 2

// readStreamingPacket reads 1 packet from stream
// read packet  bytes https://tools.ietf.org/html/rfc4571#section-2
// 2-byte length header prepends each packet:
//
//	 0                   1                   2                   3
//	 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//	-----------------------------------------------------------------
//	|             LENGTH            |  RTP or RTCP packet ...       |
//	-----------------------------------------------------------------
func readStreamingPacket(conn net.Conn, buf []byte) (int, error) {
	header := make([]byte, streamingPacketHeaderLen)
	var bytesRead, n int
	var err error

	for bytesRead < streamingPacketHeaderLen {
		if n, err = conn.Read(header[bytesRead:streamingPacketHeaderLen]); err != nil {
			return 0, err
		}
		bytesRead += n
	}

	length := int(binary.BigEndian.Uint16(header))

	if length > cap(buf) {
		return length, io.ErrShortBuffer
	}

	bytesRead = 0
	for bytesRead < length {
		if n, err = conn.Read(buf[bytesRead:length]); err != nil {
			return 0, err
		}
		bytesRead += n
	}

	return bytesRead, nil
}

func writeStreamingPacket(conn net.Conn, buf []byte) (int, error) {
	bufCopy := make([]byte, streamingPacketHeaderLen+len(buf))
	binary.BigEndian.PutUint16(bufCopy, uint16(len(buf)))
	copy(bufCopy[2:], buf)

	n, err := conn.Write(bufCopy)
	if err != nil {
		return 0, err
	}

	return n - streamingPacketHeaderLen, nil
}
