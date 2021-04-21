// Package udp provides a connection-oriented listener over a UDP PacketConn
package udp

import (
	"context"
	"errors"
	"net"
	"sync"
	"sync/atomic"
	"time"

	"github.com/pion/transport/deadline"
	"github.com/pion/transport/packetio"
)

const receiveMTU = 8192
const defaultListenBacklog = 128 // same as Linux default

var errClosedListener = errors.New("udp: listener closed")
var errListenQueueExceeded = errors.New("udp: listen queue exceeded")

// listener augments a connection-oriented Listener over a UDP PacketConn
type listener struct {
	pConn *net.UDPConn

	accepting    atomic.Value // bool
	acceptCh     chan *Conn
	doneCh       chan struct{}
	doneOnce     sync.Once
	acceptFilter func([]byte) bool

	connLock sync.Mutex
	conns    map[string]*Conn
	connWG   sync.WaitGroup

	readWG   sync.WaitGroup
	errClose atomic.Value // error
}

// Accept waits for and returns the next connection to the listener.
func (l *listener) Accept() (net.Conn, error) {
	select {
	case c := <-l.acceptCh:
		l.connWG.Add(1)
		return c, nil

	case <-l.doneCh:
		return nil, errClosedListener
	}
}

// Close closes the listener.
// Any blocked Accept operations will be unblocked and return errors.
func (l *listener) Close() error {
	var err error
	l.doneOnce.Do(func() {
		l.accepting.Store(false)
		close(l.doneCh)

		l.connLock.Lock()
		// Close unaccepted connections
	L_CLOSE:
		for {
			select {
			case c := <-l.acceptCh:
				close(c.doneCh)
				delete(l.conns, c.rAddr.String())

			default:
				break L_CLOSE
			}
		}
		nConns := len(l.conns)
		l.connLock.Unlock()

		l.connWG.Done()

		if nConns == 0 {
			// Wait if this is the final connection
			l.readWG.Wait()
			if errClose, ok := l.errClose.Load().(error); ok {
				err = errClose
			}
		} else {
			err = nil
		}
	})

	return err
}

// Addr returns the listener's network address.
func (l *listener) Addr() net.Addr {
	return l.pConn.LocalAddr()
}

// ListenConfig stores options for listening to an address.
type ListenConfig struct {
	// Backlog defines the maximum length of the queue of pending
	// connections. It is equivalent of the backlog argument of
	// POSIX listen function.
	// If a connection request arrives when the queue is full,
	// the request will be silently discarded, unlike TCP.
	// Set zero to use default value 128 which is same as Linux default.
	Backlog int

	// AcceptFilter determines whether the new conn should be made for
	// the incoming packet. If not set, any packet creates new conn.
	AcceptFilter func([]byte) bool
}

// Listen creates a new listener based on the ListenConfig.
func (lc *ListenConfig) Listen(network string, laddr *net.UDPAddr) (net.Listener, error) {
	if lc.Backlog == 0 {
		lc.Backlog = defaultListenBacklog
	}

	conn, err := net.ListenUDP(network, laddr)
	if err != nil {
		return nil, err
	}

	l := &listener{
		pConn:        conn,
		acceptCh:     make(chan *Conn, lc.Backlog),
		conns:        make(map[string]*Conn),
		doneCh:       make(chan struct{}),
		acceptFilter: lc.AcceptFilter,
	}
	l.accepting.Store(true)
	l.connWG.Add(1)
	l.readWG.Add(2) // wait readLoop and Close execution routine

	go l.readLoop()
	go func() {
		l.connWG.Wait()
		if err := l.pConn.Close(); err != nil {
			l.errClose.Store(err)
		}
		l.readWG.Done()
	}()

	return l, nil
}

// Listen creates a new listener using default ListenConfig.
func Listen(network string, laddr *net.UDPAddr) (net.Listener, error) {
	return (&ListenConfig{}).Listen(network, laddr)
}

var readBufferPool = &sync.Pool{
	New: func() interface{} {
		buf := make([]byte, receiveMTU)
		return &buf
	},
}

// readLoop has to tasks:
// 1. Dispatching incoming packets to the correct Conn.
//    It can therefore not be ended until all Conns are closed.
// 2. Creating a new Conn when receiving from a new remote.
func (l *listener) readLoop() {
	defer l.readWG.Done()

	for {
		buf := *(readBufferPool.Get().(*[]byte))
		n, raddr, err := l.pConn.ReadFrom(buf)
		if err != nil {
			return
		}
		conn, ok, err := l.getConn(raddr, buf[:n])
		if err != nil {
			continue
		}
		if ok {
			_, _ = conn.buffer.Write(buf[:n])
		}
	}
}

func (l *listener) getConn(raddr net.Addr, buf []byte) (*Conn, bool, error) {
	l.connLock.Lock()
	defer l.connLock.Unlock()
	conn, ok := l.conns[raddr.String()]
	if !ok {
		if !l.accepting.Load().(bool) {
			return nil, false, errClosedListener
		}
		if l.acceptFilter != nil {
			if !l.acceptFilter(buf) {
				return nil, false, nil
			}
		}
		conn = l.newConn(raddr)
		select {
		case l.acceptCh <- conn:
			l.conns[raddr.String()] = conn
		default:
			return nil, false, errListenQueueExceeded
		}
	}
	return conn, true, nil
}

// Conn augments a connection-oriented connection over a UDP PacketConn
type Conn struct {
	listener *listener

	rAddr net.Addr

	buffer *packetio.Buffer

	doneCh   chan struct{}
	doneOnce sync.Once

	writeDeadline *deadline.Deadline
}

func (l *listener) newConn(rAddr net.Addr) *Conn {
	return &Conn{
		listener:      l,
		rAddr:         rAddr,
		buffer:        packetio.NewBuffer(),
		doneCh:        make(chan struct{}),
		writeDeadline: deadline.New(),
	}
}

// Read
func (c *Conn) Read(p []byte) (int, error) {
	return c.buffer.Read(p)
}

// Write writes len(p) bytes from p to the DTLS connection
func (c *Conn) Write(p []byte) (n int, err error) {
	select {
	case <-c.writeDeadline.Done():
		return 0, context.DeadlineExceeded
	default:
	}
	return c.listener.pConn.WriteTo(p, c.rAddr)
}

// Close closes the conn and releases any Read calls
func (c *Conn) Close() error {
	var err error
	c.doneOnce.Do(func() {
		c.listener.connWG.Done()
		close(c.doneCh)
		c.listener.connLock.Lock()
		delete(c.listener.conns, c.rAddr.String())
		nConns := len(c.listener.conns)
		c.listener.connLock.Unlock()

		if nConns == 0 && !c.listener.accepting.Load().(bool) {
			// Wait if this is the final connection
			c.listener.readWG.Wait()
			if errClose, ok := c.listener.errClose.Load().(error); ok {
				err = errClose
			}
		} else {
			err = nil
		}
	})

	return err
}

// LocalAddr implements net.Conn.LocalAddr
func (c *Conn) LocalAddr() net.Addr {
	return c.listener.pConn.LocalAddr()
}

// RemoteAddr implements net.Conn.RemoteAddr
func (c *Conn) RemoteAddr() net.Addr {
	return c.rAddr
}

// SetDeadline implements net.Conn.SetDeadline
func (c *Conn) SetDeadline(t time.Time) error {
	c.writeDeadline.Set(t)
	return c.SetReadDeadline(t)
}

// SetReadDeadline implements net.Conn.SetDeadline
func (c *Conn) SetReadDeadline(t time.Time) error {
	return c.buffer.SetReadDeadline(t)
}

// SetWriteDeadline implements net.Conn.SetDeadline
func (c *Conn) SetWriteDeadline(t time.Time) error {
	c.writeDeadline.Set(t)
	// Write deadline of underlying connection should not be changed
	// since the connection can be shared.
	return nil
}
