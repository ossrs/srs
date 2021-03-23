// Package client implements the API for a TURN client
package client

import (
	"errors"
	"fmt"
	"io"
	"math"
	"net"
	"sync"
	"time"

	"github.com/pion/logging"
	"github.com/pion/stun"
	"github.com/pion/turn/v2/internal/proto"
)

const (
	maxReadQueueSize    = 1024
	permRefreshInterval = 120 * time.Second
	maxRetryAttempts    = 3
)

const (
	timerIDRefreshAlloc int = iota
	timerIDRefreshPerms
)

func noDeadline() time.Time {
	return time.Time{}
}

type inboundData struct {
	data []byte
	from net.Addr
}

// UDPConnObserver is an interface to UDPConn observer
type UDPConnObserver interface {
	TURNServerAddr() net.Addr
	Username() stun.Username
	Realm() stun.Realm
	WriteTo(data []byte, to net.Addr) (int, error)
	PerformTransaction(msg *stun.Message, to net.Addr, dontWait bool) (TransactionResult, error)
	OnDeallocated(relayedAddr net.Addr)
}

// UDPConnConfig is a set of configuration params use by NewUDPConn
type UDPConnConfig struct {
	Observer    UDPConnObserver
	RelayedAddr net.Addr
	Integrity   stun.MessageIntegrity
	Nonce       stun.Nonce
	Lifetime    time.Duration
	Log         logging.LeveledLogger
}

// UDPConn is the implementation of the Conn and PacketConn interfaces for UDP network connections.
// comatible with net.PacketConn and net.Conn
type UDPConn struct {
	obs               UDPConnObserver       // read-only
	relayedAddr       net.Addr              // read-only
	permMap           *permissionMap        // thread-safe
	bindingMgr        *bindingManager       // thread-safe
	integrity         stun.MessageIntegrity // read-only
	_nonce            stun.Nonce            // needs mutex x
	_lifetime         time.Duration         // needs mutex x
	readCh            chan *inboundData     // thread-safe
	closeCh           chan struct{}         // thread-safe
	readTimer         *time.Timer           // thread-safe
	refreshAllocTimer *PeriodicTimer        // thread-safe
	refreshPermsTimer *PeriodicTimer        // thread-safe
	mutex             sync.RWMutex          // thread-safe
	log               logging.LeveledLogger // read-only
}

// NewUDPConn creates a new instance of UDPConn
func NewUDPConn(config *UDPConnConfig) *UDPConn {
	c := &UDPConn{
		obs:         config.Observer,
		relayedAddr: config.RelayedAddr,
		permMap:     newPermissionMap(),
		bindingMgr:  newBindingManager(),
		integrity:   config.Integrity,
		_nonce:      config.Nonce,
		_lifetime:   config.Lifetime,
		readCh:      make(chan *inboundData, maxReadQueueSize),
		closeCh:     make(chan struct{}),
		readTimer:   time.NewTimer(time.Duration(math.MaxInt64)),
		log:         config.Log,
	}

	c.log.Debugf("initial lifetime: %d seconds", int(c.lifetime().Seconds()))

	c.refreshAllocTimer = NewPeriodicTimer(
		timerIDRefreshAlloc,
		c.onRefreshTimers,
		c.lifetime()/2,
	)

	c.refreshPermsTimer = NewPeriodicTimer(
		timerIDRefreshPerms,
		c.onRefreshTimers,
		permRefreshInterval,
	)

	if c.refreshAllocTimer.Start() {
		c.log.Debugf("refreshAllocTimer started")
	}
	if c.refreshPermsTimer.Start() {
		c.log.Debugf("refreshPermsTimer started")
	}

	return c
}

// ReadFrom reads a packet from the connection,
// copying the payload into p. It returns the number of
// bytes copied into p and the return address that
// was on the packet.
// It returns the number of bytes read (0 <= n <= len(p))
// and any error encountered. Callers should always process
// the n > 0 bytes returned before considering the error err.
// ReadFrom can be made to time out and return
// an Error with Timeout() == true after a fixed time limit;
// see SetDeadline and SetReadDeadline.
func (c *UDPConn) ReadFrom(p []byte) (n int, addr net.Addr, err error) {
	for {
		select {
		case ibData := <-c.readCh:
			n := copy(p, ibData.data)
			if n < len(ibData.data) {
				return 0, nil, io.ErrShortBuffer
			}
			return n, ibData.from, nil

		case <-c.readTimer.C:
			return 0, nil, &net.OpError{
				Op:   "read",
				Net:  c.LocalAddr().Network(),
				Addr: c.LocalAddr(),
				Err:  newTimeoutError("i/o timeout"),
			}

		case <-c.closeCh:
			return 0, nil, &net.OpError{
				Op:   "read",
				Net:  c.LocalAddr().Network(),
				Addr: c.LocalAddr(),
				Err:  errClosed,
			}
		}
	}
}

// WriteTo writes a packet with payload p to addr.
// WriteTo can be made to time out and return
// an Error with Timeout() == true after a fixed time limit;
// see SetDeadline and SetWriteDeadline.
// On packet-oriented connections, write timeouts are rare.
func (c *UDPConn) WriteTo(p []byte, addr net.Addr) (int, error) { //nolint: gocognit
	var err error
	_, ok := addr.(*net.UDPAddr)
	if !ok {
		return 0, errUDPAddrCast
	}

	// check if we have a permission for the destination IP addr
	perm, ok := c.permMap.find(addr)
	if !ok {
		perm = &permission{}
		c.permMap.insert(addr, perm)
	}

	// This func-block would block, per destination IP (, or perm), until
	// the perm state becomes "requested". Purpose of this is to guarantee
	// the order of packets (within the same perm).
	// Note that CreatePermission transaction may not be complete before
	// all the data transmission. This is done assuming that the request
	// will be mostly likely successful and we can tolerate some loss of
	// UDP packet (or reorder), inorder to minimize the latency in most cases.
	createPermission := func() error {
		perm.mutex.Lock()
		defer perm.mutex.Unlock()

		if perm.state() == permStateIdle {
			// punch a hole! (this would block a bit..)
			if err = c.createPermissions(addr); err != nil {
				c.permMap.delete(addr)
				return err
			}
			perm.setState(permStatePermitted)
		}
		return nil
	}

	for i := 0; i < maxRetryAttempts; i++ {
		if err = createPermission(); !errors.Is(err, errTryAgain) {
			break
		}
	}
	if err != nil {
		return 0, err
	}

	// bind channel
	b, ok := c.bindingMgr.findByAddr(addr)
	if !ok {
		b = c.bindingMgr.create(addr)
	}

	bindSt := b.state()

	if bindSt == bindingStateIdle || bindSt == bindingStateRequest || bindSt == bindingStateFailed {
		func() {
			// block only callers with the same binding until
			// the binding transaction has been complete
			b.muBind.Lock()
			defer b.muBind.Unlock()

			// binding state may have been changed while waiting. check again.
			if b.state() == bindingStateIdle {
				b.setState(bindingStateRequest)
				go func() {
					err2 := c.bind(b)
					if err2 != nil {
						c.log.Warnf("bind() failed: %s", err2.Error())
						b.setState(bindingStateFailed)
						// keep going...
					} else {
						b.setState(bindingStateReady)
					}
				}()
			}
		}()

		// send data using SendIndication
		peerAddr := addr2PeerAddress(addr)
		var msg *stun.Message
		msg, err = stun.Build(
			stun.TransactionID,
			stun.NewType(stun.MethodSend, stun.ClassIndication),
			proto.Data(p),
			peerAddr,
			stun.Fingerprint,
		)
		if err != nil {
			return 0, err
		}

		// indication has no transaction (fire-and-forget)

		return c.obs.WriteTo(msg.Raw, c.obs.TURNServerAddr())
	}

	// binding is either ready

	// check if the binding needs a refresh
	func() {
		b.muBind.Lock()
		defer b.muBind.Unlock()

		if b.state() == bindingStateReady && time.Since(b.refreshedAt()) > 5*time.Minute {
			b.setState(bindingStateRefresh)
			go func() {
				err = c.bind(b)
				if err != nil {
					c.log.Warnf("bind() for refresh failed: %s", err.Error())
					b.setState(bindingStateFailed)
					// keep going...
				} else {
					b.setRefreshedAt(time.Now())
					b.setState(bindingStateReady)
				}
			}()
		}
	}()

	// send via ChannelData
	return c.sendChannelData(p, b.number)
}

// Close closes the connection.
// Any blocked ReadFrom or WriteTo operations will be unblocked and return errors.
func (c *UDPConn) Close() error {
	c.refreshAllocTimer.Stop()
	c.refreshPermsTimer.Stop()

	select {
	case <-c.closeCh:
		return errAlreadyClosed
	default:
		close(c.closeCh)
	}

	c.obs.OnDeallocated(c.relayedAddr)
	return c.refreshAllocation(0, true /* dontWait=true */)
}

// LocalAddr returns the local network address.
func (c *UDPConn) LocalAddr() net.Addr {
	return c.relayedAddr
}

// SetDeadline sets the read and write deadlines associated
// with the connection. It is equivalent to calling both
// SetReadDeadline and SetWriteDeadline.
//
// A deadline is an absolute time after which I/O operations
// fail with a timeout (see type Error) instead of
// blocking. The deadline applies to all future and pending
// I/O, not just the immediately following call to ReadFrom or
// WriteTo. After a deadline has been exceeded, the connection
// can be refreshed by setting a deadline in the future.
//
// An idle timeout can be implemented by repeatedly extending
// the deadline after successful ReadFrom or WriteTo calls.
//
// A zero value for t means I/O operations will not time out.
func (c *UDPConn) SetDeadline(t time.Time) error {
	return c.SetReadDeadline(t)
}

// SetReadDeadline sets the deadline for future ReadFrom calls
// and any currently-blocked ReadFrom call.
// A zero value for t means ReadFrom will not time out.
func (c *UDPConn) SetReadDeadline(t time.Time) error {
	var d time.Duration
	if t == noDeadline() {
		d = time.Duration(math.MaxInt64)
	} else {
		d = time.Until(t)
	}
	c.readTimer.Reset(d)
	return nil
}

// SetWriteDeadline sets the deadline for future WriteTo calls
// and any currently-blocked WriteTo call.
// Even if write times out, it may return n > 0, indicating that
// some of the data was successfully written.
// A zero value for t means WriteTo will not time out.
func (c *UDPConn) SetWriteDeadline(t time.Time) error {
	// Write never blocks.
	return nil
}

func addr2PeerAddress(addr net.Addr) proto.PeerAddress {
	var peerAddr proto.PeerAddress
	switch a := addr.(type) {
	case *net.UDPAddr:
		peerAddr.IP = a.IP
		peerAddr.Port = a.Port
	case *net.TCPAddr:
		peerAddr.IP = a.IP
		peerAddr.Port = a.Port
	}

	return peerAddr
}

func (c *UDPConn) createPermissions(addrs ...net.Addr) error {
	setters := []stun.Setter{
		stun.TransactionID,
		stun.NewType(stun.MethodCreatePermission, stun.ClassRequest),
	}

	for _, addr := range addrs {
		setters = append(setters, addr2PeerAddress(addr))
	}

	setters = append(setters,
		c.obs.Username(),
		c.obs.Realm(),
		c.nonce(),
		c.integrity,
		stun.Fingerprint)

	msg, err := stun.Build(setters...)
	if err != nil {
		return err
	}

	trRes, err := c.obs.PerformTransaction(msg, c.obs.TURNServerAddr(), false)
	if err != nil {
		return err
	}

	res := trRes.Msg

	if res.Type.Class == stun.ClassErrorResponse {
		var code stun.ErrorCodeAttribute
		if err = code.GetFrom(res); err == nil {
			if code.Code == stun.CodeStaleNonce {
				c.setNonceFromMsg(res)
				return errTryAgain
			}
			return fmt.Errorf("%s (error %s)", res.Type, code) //nolint:goerr113
		}

		return fmt.Errorf("%s", res.Type) //nolint:goerr113
	}

	return nil
}

// HandleInbound passes inbound data in UDPConn
func (c *UDPConn) HandleInbound(data []byte, from net.Addr) {
	// copy data
	copied := make([]byte, len(data))
	copy(copied, data)

	select {
	case c.readCh <- &inboundData{data: copied, from: from}:
	default:
		c.log.Warnf("receive buffer full")
	}
}

// FindAddrByChannelNumber returns a peer address associated with the
// channel number on this UDPConn
func (c *UDPConn) FindAddrByChannelNumber(chNum uint16) (net.Addr, bool) {
	b, ok := c.bindingMgr.findByNumber(chNum)
	if !ok {
		return nil, false
	}
	return b.addr, true
}

func (c *UDPConn) setNonceFromMsg(msg *stun.Message) {
	// Update nonce
	var nonce stun.Nonce
	if err := nonce.GetFrom(msg); err == nil {
		c.setNonce(nonce)
		c.log.Debug("refresh allocation: 438, got new nonce.")
	} else {
		c.log.Warn("refresh allocation: 438 but no nonce.")
	}
}

func (c *UDPConn) refreshAllocation(lifetime time.Duration, dontWait bool) error {
	msg, err := stun.Build(
		stun.TransactionID,
		stun.NewType(stun.MethodRefresh, stun.ClassRequest),
		proto.Lifetime{Duration: lifetime},
		c.obs.Username(),
		c.obs.Realm(),
		c.nonce(),
		c.integrity,
		stun.Fingerprint,
	)
	if err != nil {
		return fmt.Errorf("%w: %s", errFailedToBuildRefreshRequest, err.Error())
	}

	c.log.Debugf("send refresh request (dontWait=%v)", dontWait)
	trRes, err := c.obs.PerformTransaction(msg, c.obs.TURNServerAddr(), dontWait)
	if err != nil {
		return fmt.Errorf("%w: %s", errFailedToRefreshAllocation, err.Error())
	}

	if dontWait {
		c.log.Debug("refresh request sent")
		return nil
	}

	c.log.Debug("refresh request sent, and waiting response")

	res := trRes.Msg
	if res.Type.Class == stun.ClassErrorResponse {
		var code stun.ErrorCodeAttribute
		if err = code.GetFrom(res); err == nil {
			if code.Code == stun.CodeStaleNonce {
				c.setNonceFromMsg(res)
				return errTryAgain
			}
			return err
		}
		return fmt.Errorf("%s", res.Type) //nolint:goerr113
	}

	// Getting lifetime from response
	var updatedLifetime proto.Lifetime
	if err := updatedLifetime.GetFrom(res); err != nil {
		return fmt.Errorf("%w: %s", errFailedToGetLifetime, err.Error())
	}

	c.setLifetime(updatedLifetime.Duration)
	c.log.Debugf("updated lifetime: %d seconds", int(c.lifetime().Seconds()))
	return nil
}

func (c *UDPConn) refreshPermissions() error {
	addrs := c.permMap.addrs()
	if len(addrs) == 0 {
		c.log.Debug("no permission to refresh")
		return nil
	}
	if err := c.createPermissions(addrs...); err != nil {
		if errors.Is(err, errTryAgain) {
			return errTryAgain
		}
		c.log.Errorf("fail to refresh permissions: %s", err.Error())
		return err
	}
	c.log.Debug("refresh permissions successful")
	return nil
}

func (c *UDPConn) bind(b *binding) error {
	setters := []stun.Setter{
		stun.TransactionID,
		stun.NewType(stun.MethodChannelBind, stun.ClassRequest),
		addr2PeerAddress(b.addr),
		proto.ChannelNumber(b.number),
		c.obs.Username(),
		c.obs.Realm(),
		c.nonce(),
		c.integrity,
		stun.Fingerprint,
	}

	msg, err := stun.Build(setters...)
	if err != nil {
		return err
	}

	trRes, err := c.obs.PerformTransaction(msg, c.obs.TURNServerAddr(), false)
	if err != nil {
		c.bindingMgr.deleteByAddr(b.addr)
		return err
	}

	res := trRes.Msg

	if res.Type != stun.NewType(stun.MethodChannelBind, stun.ClassSuccessResponse) {
		return fmt.Errorf("unexpected response type %s", res.Type) //nolint:goerr113
	}

	c.log.Debugf("channel binding successful: %s %d", b.addr.String(), b.number)

	// Success.
	return nil
}

func (c *UDPConn) sendChannelData(data []byte, chNum uint16) (int, error) {
	chData := &proto.ChannelData{
		Data:   data,
		Number: proto.ChannelNumber(chNum),
	}
	chData.Encode()
	return c.obs.WriteTo(chData.Raw, c.obs.TURNServerAddr())
}

func (c *UDPConn) onRefreshTimers(id int) {
	c.log.Debugf("refresh timer %d expired", id)
	switch id {
	case timerIDRefreshAlloc:
		var err error
		lifetime := c.lifetime()
		// limit the max retries on errTryAgain to 3
		// when stale nonce returns, sencond retry should succeed
		for i := 0; i < maxRetryAttempts; i++ {
			err = c.refreshAllocation(lifetime, false)
			if !errors.Is(err, errTryAgain) {
				break
			}
		}
		if err != nil {
			c.log.Warnf("refresh allocation failed")
		}
	case timerIDRefreshPerms:
		var err error
		for i := 0; i < maxRetryAttempts; i++ {
			err = c.refreshPermissions()
			if !errors.Is(err, errTryAgain) {
				break
			}
		}
		if err != nil {
			c.log.Warnf("refresh permissions failed")
		}
	}
}

func (c *UDPConn) nonce() stun.Nonce {
	c.mutex.RLock()
	defer c.mutex.RUnlock()

	return c._nonce
}

func (c *UDPConn) setNonce(nonce stun.Nonce) {
	c.mutex.Lock()
	defer c.mutex.Unlock()

	c.log.Debugf("set new nonce with %d bytes", len(nonce))
	c._nonce = nonce
}

func (c *UDPConn) lifetime() time.Duration {
	c.mutex.RLock()
	defer c.mutex.RUnlock()

	return c._lifetime
}

func (c *UDPConn) setLifetime(lifetime time.Duration) {
	c.mutex.Lock()
	defer c.mutex.Unlock()

	c._lifetime = lifetime
}
