// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"errors"
	"fmt"
	"io"
	"net"
	"sync"
	"sync/atomic"
	"time"

	"github.com/pion/logging"
	"github.com/pion/transport/v3/packetio"
)

type bufferedConn struct {
	net.Conn
	buf    *packetio.Buffer
	logger logging.LeveledLogger
	closed int32
}

func newBufferedConn(conn net.Conn, bufSize int, logger logging.LeveledLogger) net.Conn {
	buf := packetio.NewBuffer()
	if bufSize > 0 {
		buf.SetLimitSize(bufSize)
	}

	bc := &bufferedConn{
		Conn:   conn,
		buf:    buf,
		logger: logger,
	}

	go bc.writeProcess()

	return bc
}

func (bc *bufferedConn) Write(b []byte) (int, error) {
	n, err := bc.buf.Write(b)
	if err != nil {
		return n, err
	}

	return n, nil
}

func (bc *bufferedConn) writeProcess() {
	pktBuf := make([]byte, receiveMTU)
	for atomic.LoadInt32(&bc.closed) == 0 {
		n, err := bc.buf.Read(pktBuf)
		if errors.Is(err, io.EOF) {
			return
		}

		if err != nil {
			bc.logger.Warnf("Failed to read from buffer: %s", err)

			continue
		}

		if _, err := bc.Conn.Write(pktBuf[:n]); err != nil {
			bc.logger.Warnf("Failed to write: %s", err)

			continue
		}
	}
}

func (bc *bufferedConn) Close() error {
	atomic.StoreInt32(&bc.closed, 1)
	_ = bc.buf.Close()

	return bc.Conn.Close()
}

type tcpPacketConn struct {
	params *tcpPacketParams

	// conns is a map of net.Conns indexed by remote net.Addr.String()
	conns map[string]net.Conn

	recvChan chan streamingPacket

	mu         sync.Mutex
	wg         sync.WaitGroup
	closedChan chan struct{}
	closeOnce  sync.Once
	aliveTimer *time.Timer
}

type streamingPacket struct {
	Data  []byte
	RAddr net.Addr
	Err   error
}

type tcpPacketParams struct {
	ReadBuffer    int
	LocalAddr     net.Addr
	Logger        logging.LeveledLogger
	WriteBuffer   int
	AliveDuration time.Duration
}

func newTCPPacketConn(params tcpPacketParams) *tcpPacketConn {
	packet := &tcpPacketConn{
		params: &params,

		conns: map[string]net.Conn{},

		recvChan:   make(chan streamingPacket, params.ReadBuffer),
		closedChan: make(chan struct{}),
	}

	if params.AliveDuration > 0 {
		packet.aliveTimer = time.AfterFunc(params.AliveDuration, func() {
			packet.params.Logger.Warn("close tcp packet conn by alive timeout")
			_ = packet.Close()
		})
	}

	return packet
}

func (t *tcpPacketConn) ClearAliveTimer() {
	t.mu.Lock()
	if t.aliveTimer != nil {
		t.aliveTimer.Stop()
	}
	t.mu.Unlock()
}

func (t *tcpPacketConn) AddConn(conn net.Conn, firstPacketData []byte) error {
	t.params.Logger.Infof(
		"Added connection: %s remote %s to local %s",
		conn.RemoteAddr().Network(),
		conn.RemoteAddr(),
		conn.LocalAddr(),
	)

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

	if t.params.WriteBuffer > 0 {
		conn = newBufferedConn(conn, t.params.WriteBuffer, t.params.Logger)
	}
	t.conns[conn.RemoteAddr().String()] = conn

	t.wg.Add(1)
	go func() {
		defer t.wg.Done()
		if firstPacketData != nil {
			select {
			case <-t.closedChan:
				// NOTE: recvChan can fill up and never drain in edge
				// cases while closing a connection, which can cause the
				// packetConn to never finish closing. Bail out early
				// here to prevent that.
				return
			case t.recvChan <- streamingPacket{firstPacketData, conn.RemoteAddr(), nil}:
			}
		}
		t.startReading(conn)
	}()

	return nil
}

func (t *tcpPacketConn) startReading(conn net.Conn) {
	buf := make([]byte, receiveMTU)

	for {
		n, err := readStreamingPacket(conn, buf)
		if err != nil {
			t.params.Logger.Warnf("Failed to read streaming packet: %s", err)
			last := t.removeConn(conn)
			// Only propagate connection closure errors if no other open connection exists.
			if last || !(errors.Is(err, io.EOF) || errors.Is(err, net.ErrClosed)) {
				t.handleRecv(streamingPacket{nil, conn.RemoteAddr(), err})
			}

			return
		}

		data := make([]byte, n)
		copy(data, buf[:n])

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
func (t *tcpPacketConn) ReadFrom(b []byte) (n int, rAddr net.Addr, err error) {
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
func (t *tcpPacketConn) WriteTo(buf []byte, rAddr net.Addr) (n int, err error) {
	t.mu.Lock()
	conn, ok := t.conns[rAddr.String()]
	t.mu.Unlock()

	if !ok {
		return 0, io.ErrClosedPipe
	}

	n, err = writeStreamingPacket(conn, buf)
	if err != nil {
		t.params.Logger.Tracef("%w %s", errWrite, rAddr)

		return n, err
	}

	return n, err
}

func (t *tcpPacketConn) closeAndLogError(closer io.Closer) {
	err := closer.Close()
	if err != nil {
		t.params.Logger.Warnf("%v: %s", errClosingConnection, err)
	}
}

func (t *tcpPacketConn) removeConn(conn net.Conn) bool {
	t.mu.Lock()
	defer t.mu.Unlock()

	t.closeAndLogError(conn)

	delete(t.conns, conn.RemoteAddr().String())

	return len(t.conns) == 0
}

func (t *tcpPacketConn) Close() error {
	t.mu.Lock()

	var shouldCloseRecvChan bool
	t.closeOnce.Do(func() {
		close(t.closedChan)
		shouldCloseRecvChan = true
		if t.aliveTimer != nil {
			t.aliveTimer.Stop()
		}
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

func (t *tcpPacketConn) SetDeadline(time.Time) error {
	return nil
}

func (t *tcpPacketConn) SetReadDeadline(time.Time) error {
	return nil
}

func (t *tcpPacketConn) SetWriteDeadline(time.Time) error {
	return nil
}

func (t *tcpPacketConn) CloseChannel() <-chan struct{} {
	return t.closedChan
}

func (t *tcpPacketConn) String() string {
	return fmt.Sprintf("tcpPacketConn{LocalAddr: %s}", t.params.LocalAddr)
}
