package ice

import (
	"fmt"
	"io"
	"net"
	"sync"
	"time"

	"github.com/pion/logging"
)

type tcpPacketConn struct {
	params *tcpPacketParams

	// conns is a map of net.Conns indexed by remote net.Addr.String()
	conns map[string]net.Conn

	recvChan chan streamingPacket

	mu         sync.Mutex
	wg         sync.WaitGroup
	closedChan chan struct{}
	closeOnce  sync.Once
}

type streamingPacket struct {
	Data  []byte
	RAddr net.Addr
	Err   error
}

type tcpPacketParams struct {
	ReadBuffer int
	LocalAddr  net.Addr
	Logger     logging.LeveledLogger
}

func newTCPPacketConn(params tcpPacketParams) *tcpPacketConn {
	p := &tcpPacketConn{
		params: &params,

		conns: map[string]net.Conn{},

		recvChan:   make(chan streamingPacket, params.ReadBuffer),
		closedChan: make(chan struct{}),
	}

	return p
}

func (t *tcpPacketConn) AddConn(conn net.Conn, firstPacketData []byte) error {
	t.params.Logger.Infof("AddConn: %s %s", conn.RemoteAddr().Network(), conn.RemoteAddr())

	t.mu.Lock()
	defer t.mu.Unlock()

	select {
	case <-t.closedChan:
		return io.ErrClosedPipe
	default:
	}

	if _, ok := t.conns[conn.RemoteAddr().String()]; ok {
		return fmt.Errorf("%w: %s", errConnectionAddrAlreadyExist, conn.RemoteAddr().String())
	}

	t.conns[conn.RemoteAddr().String()] = conn

	t.wg.Add(1)
	go func() {
		if firstPacketData != nil {
			t.recvChan <- streamingPacket{firstPacketData, conn.RemoteAddr(), nil}
		}
		defer t.wg.Done()
		t.startReading(conn)
	}()

	return nil
}

func (t *tcpPacketConn) startReading(conn net.Conn) {
	buf := make([]byte, receiveMTU)

	for {
		n, err := readStreamingPacket(conn, buf)
		// t.params.Logger.Infof("readStreamingPacket read %d bytes", n)
		if err != nil {
			t.params.Logger.Infof("%w: %s\n", errReadingStreamingPacket, err)
			t.handleRecv(streamingPacket{nil, conn.RemoteAddr(), err})
			t.removeConn(conn)
			return
		}

		data := make([]byte, n)
		copy(data, buf[:n])

		// t.params.Logger.Infof("Writing read streaming packet to recvChan: %d bytes", len(data))
		t.handleRecv(streamingPacket{data, conn.RemoteAddr(), nil})
	}
}

func (t *tcpPacketConn) handleRecv(pkt streamingPacket) {
	t.mu.Lock()

	recvChan := t.recvChan
	if t.isClosed() {
		recvChan = nil
	}

	t.mu.Unlock()

	select {
	case recvChan <- pkt:
	case <-t.closedChan:
	}
}

func (t *tcpPacketConn) isClosed() bool {
	select {
	case <-t.closedChan:
		return true
	default:
		return false
	}
}

// WriteTo is for passive and s-o candidates.
func (t *tcpPacketConn) ReadFrom(b []byte) (n int, raddr net.Addr, err error) {
	pkt, ok := <-t.recvChan

	if !ok {
		return 0, nil, io.ErrClosedPipe
	}

	if pkt.Err != nil {
		return 0, pkt.RAddr, pkt.Err
	}

	if cap(b) < len(pkt.Data) {
		return 0, pkt.RAddr, io.ErrShortBuffer
	}

	n = len(pkt.Data)
	copy(b, pkt.Data[:n])
	return n, pkt.RAddr, err
}

// WriteTo is for active and s-o candidates.
func (t *tcpPacketConn) WriteTo(buf []byte, raddr net.Addr) (n int, err error) {
	t.mu.Lock()
	defer t.mu.Unlock()

	conn, ok := t.conns[raddr.String()]
	if !ok {
		return 0, io.ErrClosedPipe
		// conn, err := net.DialTCP(tcp, nil, raddr.(*net.TCPAddr))

		// if err != nil {
		// 	t.params.Logger.Tracef("DialTCP error: %s", err)
		// 	return 0, err
		// }

		// go t.startReading(conn)
		// t.conns[raddr.String()] = conn
	}

	n, err = writeStreamingPacket(conn, buf)
	if err != nil {
		t.params.Logger.Tracef("%w %s\n", errWriting, raddr)
		return n, err
	}

	return n, err
}

func (t *tcpPacketConn) closeAndLogError(closer io.Closer) {
	err := closer.Close()
	if err != nil {
		t.params.Logger.Warnf("%w: %s", errClosingConnection, err)
	}
}

func (t *tcpPacketConn) removeConn(conn net.Conn) {
	t.mu.Lock()
	defer t.mu.Unlock()

	t.closeAndLogError(conn)

	delete(t.conns, conn.RemoteAddr().String())
}

func (t *tcpPacketConn) Close() error {
	t.mu.Lock()

	var shouldCloseRecvChan bool
	t.closeOnce.Do(func() {
		close(t.closedChan)
		shouldCloseRecvChan = true
	})

	for _, conn := range t.conns {
		t.closeAndLogError(conn)
		delete(t.conns, conn.RemoteAddr().String())
	}

	t.mu.Unlock()

	t.wg.Wait()

	if shouldCloseRecvChan {
		close(t.recvChan)
	}

	return nil
}

func (t *tcpPacketConn) LocalAddr() net.Addr {
	return t.params.LocalAddr
}

func (t *tcpPacketConn) SetDeadline(tm time.Time) error {
	return nil
}

func (t *tcpPacketConn) SetReadDeadline(tm time.Time) error {
	return nil
}

func (t *tcpPacketConn) SetWriteDeadline(tm time.Time) error {
	return nil
}

func (t *tcpPacketConn) CloseChannel() <-chan struct{} {
	return t.closedChan
}

func (t *tcpPacketConn) String() string {
	return fmt.Sprintf("tcpPacketConn{LocalAddr: %s}", t.params.LocalAddr)
}
