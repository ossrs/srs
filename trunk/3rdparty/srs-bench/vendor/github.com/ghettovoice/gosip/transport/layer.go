package transport

import (
	"context"
	"errors"
	"fmt"
	"math/rand"
	"net"
	"strings"
	"sync"
	"time"

	"github.com/ghettovoice/gosip/log"
	"github.com/ghettovoice/gosip/sip"
)

func init() {
	rand.Seed(time.Now().UnixNano())
}

// Layer is responsible for the actual transmission of messages - RFC 3261 - 18.
type Layer interface {
	Cancel()
	Done() <-chan struct{}
	Messages() <-chan sip.Message
	Errors() <-chan error
	// Listen starts listening on `addr` for each registered protocol.
	Listen(network string, addr string, options ...ListenOption) error
	// Send sends message on suitable protocol.
	Send(msg sip.Message) error
	String() string
	IsReliable(network string) bool
	IsStreamed(network string) bool
}

var protocolFactory ProtocolFactory = func(
	network string,
	output chan<- sip.Message,
	errs chan<- error,
	cancel <-chan struct{},
	msgMapper sip.MessageMapper,
	logger log.Logger,
) (Protocol, error) {
	switch strings.ToLower(network) {
	case "udp":
		return NewUdpProtocol(output, errs, cancel, msgMapper, logger), nil
	case "tcp":
		return NewTcpProtocol(output, errs, cancel, msgMapper, logger), nil
	case "tls":
		return NewTlsProtocol(output, errs, cancel, msgMapper, logger), nil
	case "ws":
		return NewWsProtocol(output, errs, cancel, msgMapper, logger), nil
	case "wss":
		return NewWssProtocol(output, errs, cancel, msgMapper, logger), nil
	default:
		return nil, UnsupportedProtocolError(fmt.Sprintf("protocol %s is not supported", network))
	}
}

// SetProtocolFactory replaces default protocol factory
func SetProtocolFactory(factory ProtocolFactory) {
	protocolFactory = factory
}

// GetProtocolFactory returns default protocol factory
func GetProtocolFactory() ProtocolFactory {
	return protocolFactory
}

// TransportLayer implementation.
type layer struct {
	protocols   *protocolStore
	listenPorts map[string][]sip.Port
	ip          net.IP
	dnsResolver *net.Resolver
	msgMapper   sip.MessageMapper

	msgs     chan sip.Message
	errs     chan error
	pmsgs    chan sip.Message
	perrs    chan error
	canceled chan struct{}
	done     chan struct{}

	wg         sync.WaitGroup
	cancelOnce sync.Once

	log log.Logger
}

// NewLayer creates transport layer.
// - ip - host IP
// - dnsAddr - DNS server address, default is 127.0.0.1:53
func NewLayer(
	ip net.IP,
	dnsResolver *net.Resolver,
	msgMapper sip.MessageMapper,
	logger log.Logger,
) Layer {
	tpl := &layer{
		protocols:   newProtocolStore(),
		listenPorts: make(map[string][]sip.Port),
		ip:          ip,
		dnsResolver: dnsResolver,
		msgMapper:   msgMapper,

		msgs:     make(chan sip.Message),
		errs:     make(chan error),
		pmsgs:    make(chan sip.Message),
		perrs:    make(chan error),
		canceled: make(chan struct{}),
		done:     make(chan struct{}),
	}

	tpl.log = logger.
		WithPrefix("transport.Layer").
		WithFields(map[string]interface{}{
			"transport_layer_ptr": fmt.Sprintf("%p", tpl),
		})

	go tpl.serveProtocols()

	return tpl
}

func (tpl *layer) String() string {
	if tpl == nil {
		return "<nil>"
	}

	return fmt.Sprintf("transport.Layer<%s>", tpl.Log().Fields())
}

func (tpl *layer) Log() log.Logger {
	return tpl.log
}

func (tpl *layer) Cancel() {
	select {
	case <-tpl.canceled:
		return
	default:
	}

	tpl.cancelOnce.Do(func() {
		close(tpl.canceled)

		tpl.Log().Debug("transport layer canceled")
	})
}

func (tpl *layer) Done() <-chan struct{} {
	return tpl.done
}

func (tpl *layer) Messages() <-chan sip.Message {
	return tpl.msgs
}

func (tpl *layer) Errors() <-chan error {
	return tpl.errs
}

func (tpl *layer) IsReliable(network string) bool {
	if protocol, ok := tpl.protocols.get(protocolKey(network)); ok && protocol.Reliable() {
		return true
	}
	return false
}

func (tpl *layer) IsStreamed(network string) bool {
	if protocol, ok := tpl.protocols.get(protocolKey(network)); ok && protocol.Streamed() {
		return true
	}
	return false
}

func (tpl *layer) Listen(network string, addr string, options ...ListenOption) error {
	select {
	case <-tpl.canceled:
		return fmt.Errorf("transport layer is canceled")
	default:
	}

	protocol, err := tpl.getProtocol(network)
	if err != nil {
		return err
	}
	target, err := NewTargetFromAddr(addr)
	if err != nil {
		return err
	}
	target = FillTargetHostAndPort(protocol.Network(), target)

	err = protocol.Listen(target, options...)
	if err == nil {
		if _, ok := tpl.listenPorts[protocol.Network()]; !ok {
			if tpl.listenPorts[protocol.Network()] == nil {
				tpl.listenPorts[protocol.Network()] = make([]sip.Port, 0)
			}
			tpl.listenPorts[protocol.Network()] = append(tpl.listenPorts[protocol.Network()], *target.Port)
		}
	}

	return err
}

func (tpl *layer) Send(msg sip.Message) error {
	select {
	case <-tpl.canceled:
		return fmt.Errorf("transport layer is canceled")
	default:
	}

	viaHop, ok := msg.ViaHop()
	if !ok {
		return &sip.MalformedMessageError{
			Err: fmt.Errorf("missing required 'Via' header"),
			Msg: msg.String(),
		}
	}

	switch msg := msg.(type) {
	// RFC 3261 - 18.1.1.
	case sip.Request:
		network := msg.Transport()
		// rewrite sent-by transport
		viaHop.Transport = strings.ToUpper(network)
		viaHop.Host = tpl.ip.String()

		protocol, err := tpl.getProtocol(network)
		if err != nil {
			return err
		}

		// rewrite sent-by port
		if viaHop.Port == nil {
			if ports, ok := tpl.listenPorts[network]; ok {
				port := ports[rand.Intn(len(ports))]
				viaHop.Port = &port
			} else {
				defPort := sip.DefaultPort(network)
				viaHop.Port = &defPort
			}
		}

		target, err := NewTargetFromAddr(msg.Destination())
		if err != nil {
			return fmt.Errorf("build address target for %s: %w", msg.Destination(), err)
		}

		// dns srv lookup
		if net.ParseIP(target.Host) == nil {
			ctx := context.Background()
			proto := strings.ToLower(network)
			if _, addrs, err := tpl.dnsResolver.LookupSRV(ctx, "sip", proto, target.Host); err == nil && len(addrs) > 0 {
				addr := addrs[0]
				addrStr := fmt.Sprintf("%s:%d", addr.Target[:len(addr.Target)-1], addr.Port)
				switch network {
				case "UDP":
					if addr, err := net.ResolveUDPAddr("udp", addrStr); err == nil {
						port := sip.Port(addr.Port)
						if addr.IP.To4() == nil {
							target.Host = fmt.Sprintf("[%v]", addr.IP.String())
						} else {
							target.Host = addr.IP.String()
						}
						target.Port = &port
					}
				case "TLS":
					fallthrough
				case "WS":
					fallthrough
				case "WSS":
					fallthrough
				case "TCP":
					if addr, err := net.ResolveTCPAddr("tcp", addrStr); err == nil {
						port := sip.Port(addr.Port)
						if addr.IP.To4() == nil {
							target.Host = fmt.Sprintf("[%v]", addr.IP.String())
						} else {
							target.Host = addr.IP.String()
						}
						target.Port = &port
					}
				}
			}
		}

		logger := log.AddFieldsFrom(tpl.Log(), protocol, msg)
		logger.Debugf("sending SIP request:\n%s", msg)

		if err = protocol.Send(target, msg); err != nil {
			return fmt.Errorf("send SIP message through %s protocol to %s: %w", protocol.Network(), target.Addr(), err)
		}

		return nil
		// RFC 3261 - 18.2.2.
	case sip.Response:
		// resolve protocol from Via
		protocol, err := tpl.getProtocol(msg.Transport())
		if err != nil {
			return err
		}

		target, err := NewTargetFromAddr(msg.Destination())
		if err != nil {
			return fmt.Errorf("build address target for %s: %w", msg.Destination(), err)
		}

		logger := log.AddFieldsFrom(tpl.Log(), protocol, msg)
		logger.Debugf("sending SIP response:\n%s", msg)

		if err = protocol.Send(target, msg); err != nil {
			return fmt.Errorf("send SIP message through %s protocol to %s: %w", protocol.Network(), target.Addr(), err)
		}

		return nil
	default:
		return &sip.UnsupportedMessageError{
			Err: fmt.Errorf("unsupported message %s", msg.Short()),
			Msg: msg.String(),
		}
	}
}

func (tpl *layer) getProtocol(network string) (Protocol, error) {
	network = strings.ToLower(network)
	return tpl.protocols.getOrPutNew(protocolKey(network), func() (Protocol, error) {
		return protocolFactory(
			network,
			tpl.pmsgs,
			tpl.perrs,
			tpl.canceled,
			tpl.msgMapper,
			tpl.Log(),
		)
	})
}

func (tpl *layer) serveProtocols() {
	defer func() {
		tpl.dispose()
		close(tpl.done)
	}()

	tpl.Log().Debug("begin serve protocols")
	defer tpl.Log().Debug("stop serve protocols")

	for {
		select {
		case <-tpl.canceled:
			return
		case msg := <-tpl.pmsgs:
			tpl.handleMessage(msg)
		case err := <-tpl.perrs:
			tpl.handlerError(err)
		}
	}
}

func (tpl *layer) dispose() {
	tpl.Log().Debug("disposing...")
	// wait for protocols
	for _, protocol := range tpl.protocols.all() {
		tpl.protocols.drop(protocolKey(protocol.Network()))
		<-protocol.Done()
	}

	tpl.listenPorts = make(map[string][]sip.Port)

	close(tpl.pmsgs)
	close(tpl.perrs)
	close(tpl.msgs)
	close(tpl.errs)
}

// handles incoming message from protocol
// should be called inside goroutine for non-blocking forwarding
func (tpl *layer) handleMessage(msg sip.Message) {
	logger := tpl.Log().WithFields(msg.Fields())

	logger.Debugf("received SIP message:\n%s", msg)
	logger.Trace("passing up SIP message...")

	// pass up message
	select {
	case <-tpl.canceled:
	case tpl.msgs <- msg:
		logger.Trace("SIP message passed up")
	}
}

func (tpl *layer) handlerError(err error) {
	// TODO: implement re-connection strategy for listeners
	var terr Error
	if errors.As(err, &terr) {
		// currently log
		tpl.Log().Warnf("SIP transport error: %s", err)
	}

	logger := tpl.Log().WithFields(log.Fields{
		"sip_error": err.Error(),
	})

	logger.Trace("passing up error...")

	select {
	case <-tpl.canceled:
	case tpl.errs <- err:
		logger.Trace("error passed up")
	}
}

type protocolKey string

// Thread-safe protocols pool.
type protocolStore struct {
	protocols map[protocolKey]Protocol
	mu        sync.RWMutex
}

func newProtocolStore() *protocolStore {
	return &protocolStore{
		protocols: make(map[protocolKey]Protocol),
	}
}

func (store *protocolStore) put(key protocolKey, protocol Protocol) {
	store.mu.Lock()
	store.protocols[key] = protocol
	store.mu.Unlock()
}

func (store *protocolStore) get(key protocolKey) (Protocol, bool) {
	store.mu.RLock()
	defer store.mu.RUnlock()
	protocol, ok := store.protocols[key]
	return protocol, ok
}

func (store *protocolStore) getOrPutNew(key protocolKey, factory func() (Protocol, error)) (Protocol, error) {
	store.mu.Lock()
	defer store.mu.Unlock()
	protocol, ok := store.protocols[key]
	if ok {
		return protocol, nil
	}
	var err error
	protocol, err = factory()
	if err != nil {
		return nil, err
	}
	store.protocols[key] = protocol
	return protocol, nil
}

func (store *protocolStore) drop(key protocolKey) bool {
	if _, ok := store.get(key); !ok {
		return false
	}
	store.mu.Lock()
	defer store.mu.Unlock()
	delete(store.protocols, key)
	return true
}

func (store *protocolStore) all() []Protocol {
	all := make([]Protocol, 0)
	store.mu.RLock()
	defer store.mu.RUnlock()
	for _, protocol := range store.protocols {
		all = append(all, protocol)
	}

	return all
}
