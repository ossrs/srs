package ice

import (
	"encoding/binary"
	"io"
	"net"
	"strings"
	"sync"

	"github.com/pion/logging"
	"github.com/pion/stun"
)

// TCPMux is allows grouping multiple TCP net.Conns and using them like UDP
// net.PacketConns. The main implementation of this is TCPMuxDefault, and this
// interface exists to:
// 1. prevent SEGV panics when TCPMuxDefault is not initialized by using the
//    invalidTCPMux implementation, and
// 2. allow mocking in tests.
type TCPMux interface {
	io.Closer
	GetConnByUfrag(ufrag string) (net.PacketConn, error)
	RemoveConnByUfrag(ufrag string)
}

// invalidTCPMux is an implementation of TCPMux that always returns ErroTCPMuxNotInitialized.
type invalidTCPMux struct {
}

func newInvalidTCPMux() *invalidTCPMux {
	return &invalidTCPMux{}
}

// Close implements TCPMux interface.
func (m *invalidTCPMux) Close() error {
	return ErrTCPMuxNotInitialized
}

// GetConnByUfrag implements TCPMux interface.
func (m *invalidTCPMux) GetConnByUfrag(ufrag string) (net.PacketConn, error) {
	return nil, ErrTCPMuxNotInitialized
}

// RemoveConnByUfrag implements TCPMux interface.
func (m *invalidTCPMux) RemoveConnByUfrag(ufrag string) {}

// TCPMuxDefault muxes TCP net.Conns into net.PacketConns and groups them by
// Ufrag. It is a default implementation of TCPMux interface.
type TCPMuxDefault struct {
	params *TCPMuxParams
	closed bool

	// conns is a map of all tcpPacketConns indexed by ufrag
	conns map[string]*tcpPacketConn

	mu sync.Mutex
	wg sync.WaitGroup
}

// TCPMuxParams are parameters for TCPMux.
type TCPMuxParams struct {
	Listener       net.Listener
	Logger         logging.LeveledLogger
	ReadBufferSize int
}

// NewTCPMuxDefault creates a new instance of TCPMuxDefault.
func NewTCPMuxDefault(params TCPMuxParams) *TCPMuxDefault {
	if params.Logger == nil {
		params.Logger = logging.NewDefaultLoggerFactory().NewLogger("ice")
	}

	m := &TCPMuxDefault{
		params: &params,

		conns: map[string]*tcpPacketConn{},
	}

	m.wg.Add(1)
	go func() {
		defer m.wg.Done()
		m.start()
	}()

	return m
}

func (m *TCPMuxDefault) start() {
	m.params.Logger.Infof("Listening TCP on %s\n", m.params.Listener.Addr())
	for {
		conn, err := m.params.Listener.Accept()
		if err != nil {
			m.params.Logger.Infof("Error accepting connection: %s\n", err)
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
func (m *TCPMuxDefault) GetConnByUfrag(ufrag string) (net.PacketConn, error) {
	m.mu.Lock()
	defer m.mu.Unlock()

	if m.closed {
		return nil, io.ErrClosedPipe
	}

	conn, ok := m.conns[ufrag]

	if ok {
		return conn, nil
		// return nil, fmt.Errorf("duplicate ufrag %v", ufrag)
	}

	conn = m.createConn(ufrag, m.LocalAddr())

	return conn, nil
}

func (m *TCPMuxDefault) createConn(ufrag string, localAddr net.Addr) *tcpPacketConn {
	conn := newTCPPacketConn(tcpPacketParams{
		ReadBuffer: m.params.ReadBufferSize,
		LocalAddr:  localAddr,
		Logger:     m.params.Logger,
	})
	m.conns[ufrag] = conn

	m.wg.Add(1)
	go func() {
		defer m.wg.Done()
		<-conn.CloseChannel()
		m.RemoveConnByUfrag(ufrag)
	}()

	return conn
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
		m.params.Logger.Warnf("Error reading first packet: %s", err)
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
		m.params.Logger.Warnf("Failed to handle decode ICE from %s to %s: %v\n", conn.RemoteAddr(), conn.LocalAddr(), err)
		return
	}

	if m == nil || msg.Type.Method != stun.MethodBinding { // not a stun
		m.closeAndLogError(conn)
		m.params.Logger.Warnf("Not a STUN message from %s to %s\n", conn.RemoteAddr(), conn.LocalAddr())
		return
	}

	for _, attr := range msg.Attributes {
		m.params.Logger.Debugf("msg attr: %s\n", attr.String())
	}

	attr, err := msg.Get(stun.AttrUsername)
	if err != nil {
		m.closeAndLogError(conn)
		m.params.Logger.Warnf("No Username attribute in STUN message from %s to %s\n", conn.RemoteAddr(), conn.LocalAddr())
		return
	}

	ufrag := strings.Split(string(attr), ":")[0]
	m.params.Logger.Debugf("Ufrag: %s\n", ufrag)

	m.mu.Lock()
	defer m.mu.Unlock()

	packetConn, ok := m.conns[ufrag]
	if !ok {
		packetConn = m.createConn(ufrag, conn.LocalAddr())
	}

	if err := packetConn.AddConn(conn, buf); err != nil {
		m.closeAndLogError(conn)
		m.params.Logger.Warnf("Error adding conn to tcpPacketConn from %s to %s: %s\n", conn.RemoteAddr(), conn.LocalAddr(), err)
		return
	}
}

// Close closes the listener and waits for all goroutines to exit.
func (m *TCPMuxDefault) Close() error {
	m.mu.Lock()
	m.closed = true

	for _, conn := range m.conns {
		m.closeAndLogError(conn)
	}
	m.conns = map[string]*tcpPacketConn{}

	err := m.params.Listener.Close()

	m.mu.Unlock()

	m.wg.Wait()

	return err
}

// RemoveConnByUfrag closes and removes a net.PacketConn by Ufrag.
func (m *TCPMuxDefault) RemoveConnByUfrag(ufrag string) {
	m.mu.Lock()
	defer m.mu.Unlock()

	if conn, ok := m.conns[ufrag]; ok {
		m.closeAndLogError(conn)
		delete(m.conns, ufrag)
	}
}

const streamingPacketHeaderLen = 2

// readStreamingPacket reads 1 packet from stream
// read packet  bytes https://tools.ietf.org/html/rfc4571#section-2
// 2-byte length header prepends each packet:
//     0                   1                   2                   3
//     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//    -----------------------------------------------------------------
//    |             LENGTH            |  RTP or RTCP packet ...       |
//    -----------------------------------------------------------------
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
	bufferCopy := make([]byte, streamingPacketHeaderLen+len(buf))
	binary.BigEndian.PutUint16(bufferCopy, uint16(len(buf)))
	copy(bufferCopy[2:], buf)

	n, err := conn.Write(bufferCopy)
	if err != nil {
		return 0, err
	}

	return n - streamingPacketHeaderLen, nil
}
