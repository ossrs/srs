package transport

import (
	"fmt"
	"net"
	"strings"

	"github.com/ghettovoice/gosip/log"
	"github.com/ghettovoice/gosip/sip"
)

type tcpListener struct {
	net.Listener
	network string
}

func (l *tcpListener) Network() string {
	return strings.ToUpper(l.network)
}

// TCP protocol implementation
type tcpProtocol struct {
	protocol
	listeners   ListenerPool
	connections ConnectionPool
	conns       chan Connection
	listen      func(addr *net.TCPAddr, options ...ListenOption) (net.Listener, error)
	dial        func(addr *net.TCPAddr) (net.Conn, error)
	resolveAddr func(addr string) (*net.TCPAddr, error)
}

func NewTcpProtocol(
	output chan<- sip.Message,
	errs chan<- error,
	cancel <-chan struct{},
	msgMapper sip.MessageMapper,
	logger log.Logger,
) Protocol {
	p := new(tcpProtocol)
	p.network = "tcp"
	p.reliable = true
	p.streamed = true
	p.conns = make(chan Connection)
	p.log = logger.
		WithPrefix("transport.Protocol").
		WithFields(log.Fields{
			"protocol_ptr": fmt.Sprintf("%p", p),
		})
	// TODO: add separate errs chan to listen errors from pool for reconnection?
	p.listeners = NewListenerPool(p.conns, errs, cancel, p.Log())
	p.connections = NewConnectionPool(output, errs, cancel, msgMapper, p.Log())
	p.listen = p.defaultListen
	p.dial = p.defaultDial
	p.resolveAddr = p.defaultResolveAddr
	// pipe listener and connection pools
	go p.pipePools()

	return p
}

func (p *tcpProtocol) defaultListen(addr *net.TCPAddr, options ...ListenOption) (net.Listener, error) {
	return net.ListenTCP(p.network, addr)
}

func (p *tcpProtocol) defaultDial(addr *net.TCPAddr) (net.Conn, error) {
	return net.DialTCP(p.network, nil, addr)
}

func (p *tcpProtocol) defaultResolveAddr(addr string) (*net.TCPAddr, error) {
	return net.ResolveTCPAddr(p.network, addr)
}

func (p *tcpProtocol) Done() <-chan struct{} {
	return p.connections.Done()
}

// piping new connections to connection pool for serving
func (p *tcpProtocol) pipePools() {
	defer close(p.conns)

	p.Log().Debug("start pipe pools")
	defer p.Log().Debug("stop pipe pools")

	for {
		select {
		case <-p.listeners.Done():
			return
		case conn := <-p.conns:
			logger := log.AddFieldsFrom(p.Log(), conn)

			if err := p.connections.Put(conn, sockTTL); err != nil {
				// TODO should it be passed up to UA?
				logger.Errorf("put %s connection to the pool failed: %s", conn.Key(), err)

				conn.Close()

				continue
			}
		}
	}
}

func (p *tcpProtocol) Listen(target *Target, options ...ListenOption) error {
	target = FillTargetHostAndPort(p.Network(), target)
	laddr, err := p.resolveAddr(target.Addr())
	if err != nil {
		return &ProtocolError{
			err,
			fmt.Sprintf("resolve target address %s %s", p.Network(), target.Addr()),
			fmt.Sprintf("%p", p),
		}
	}

	listener, err := p.listen(laddr, options...)
	if err != nil {
		return &ProtocolError{
			err,
			fmt.Sprintf("listen on %s %s address", p.Network(), target.Addr()),
			fmt.Sprintf("%p", p),
		}
	}

	p.Log().Debugf("begin listening on %s %s", p.Network(), target.Addr())

	// index listeners by local address
	// should live infinitely
	key := ListenerKey(fmt.Sprintf("%s:0.0.0.0:%d", p.network, target.Port))
	err = p.listeners.Put(key, &tcpListener{
		Listener: listener,
		network:  p.network,
	})
	if err != nil {
		err = &ProtocolError{
			Err:      err,
			Op:       fmt.Sprintf("put %s listener to the pool", key),
			ProtoPtr: fmt.Sprintf("%p", p),
		}
	}
	return err // should be nil here
}

func (p *tcpProtocol) Send(target *Target, msg sip.Message) error {
	target = FillTargetHostAndPort(p.Network(), target)

	// validate remote address
	if target.Host == "" {
		return &ProtocolError{
			fmt.Errorf("empty remote target host"),
			fmt.Sprintf("send SIP message to %s %s", p.Network(), target.Addr()),
			fmt.Sprintf("%p", p),
		}
	}

	// resolve remote address
	raddr, err := p.resolveAddr(target.Addr())
	if err != nil {
		return &ProtocolError{
			err,
			fmt.Sprintf("resolve target address %s %s", p.Network(), target.Addr()),
			fmt.Sprintf("%p", p),
		}
	}

	// find or create connection
	conn, err := p.getOrCreateConnection(raddr)
	if err != nil {
		return &ProtocolError{
			Err:      err,
			Op:       fmt.Sprintf("get or create %s connection", p.Network()),
			ProtoPtr: fmt.Sprintf("%p", p),
		}
	}

	logger := log.AddFieldsFrom(p.Log(), conn, msg)
	logger.Tracef("writing SIP message to %s %s", p.Network(), raddr)

	// send message
	_, err = conn.Write([]byte(msg.String()))
	if err != nil {
		err = &ProtocolError{
			Err:      err,
			Op:       fmt.Sprintf("write SIP message to the %s connection", conn.Key()),
			ProtoPtr: fmt.Sprintf("%p", p),
		}
	}

	return err
}

func (p *tcpProtocol) getOrCreateConnection(raddr *net.TCPAddr) (Connection, error) {
	key := ConnectionKey(p.network + ":" + raddr.String())
	conn, err := p.connections.Get(key)
	if err != nil {
		p.Log().Debugf("connection for remote address %s %s not found, create a new one", p.Network(), raddr)

		tcpConn, err := p.dial(raddr)
		if err != nil {
			return nil, fmt.Errorf("dial to %s %s: %w", p.Network(), raddr, err)
		}

		conn = NewConnection(tcpConn, key, p.network, p.Log())

		if err := p.connections.Put(conn, sockTTL); err != nil {
			return conn, fmt.Errorf("put %s connection to the pool: %w", conn.Key(), err)
		}
	}

	return conn, nil
}
