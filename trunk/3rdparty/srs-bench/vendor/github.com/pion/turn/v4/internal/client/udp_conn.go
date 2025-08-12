// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package client implements the API for a TURN client
package client

import (
	"errors"
	"fmt"
	"io"
	"math"
	"net"
	"time"

	"github.com/pion/stun/v3"
	"github.com/pion/turn/v4/internal/proto"
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

type inboundData struct {
	data []byte
	from net.Addr
}

// UDPConn is the implementation of the Conn and PacketConn interfaces for UDP network connections.
// compatible with net.PacketConn and net.Conn.
type UDPConn struct {
	bindingMgr *bindingManager   // Thread-safe
	readCh     chan *inboundData // Thread-safe
	closeCh    chan struct{}     // Thread-safe
	allocation
}

// NewUDPConn creates a new instance of UDPConn.
func NewUDPConn(config *AllocationConfig) *UDPConn {
	conn := &UDPConn{
		bindingMgr: newBindingManager(),
		readCh:     make(chan *inboundData, maxReadQueueSize),
		closeCh:    make(chan struct{}),
		allocation: allocation{
			client:      config.Client,
			relayedAddr: config.RelayedAddr,
			serverAddr:  config.ServerAddr,
			readTimer:   time.NewTimer(time.Duration(math.MaxInt64)),
			permMap:     newPermissionMap(),
			username:    config.Username,
			realm:       config.Realm,
			integrity:   config.Integrity,
			_nonce:      config.Nonce,
			_lifetime:   config.Lifetime,
			net:         config.Net,
			log:         config.Log,
		},
	}

	conn.log.Debugf("Initial lifetime: %d seconds", int(conn.lifetime().Seconds()))

	conn.refreshAllocTimer = NewPeriodicTimer(
		timerIDRefreshAlloc,
		conn.onRefreshTimers,
		conn.lifetime()/2,
	)

	conn.refreshPermsTimer = NewPeriodicTimer(
		timerIDRefreshPerms,
		conn.onRefreshTimers,
		permRefreshInterval,
	)

	if conn.refreshAllocTimer.Start() {
		conn.log.Debugf("Started refresh allocation timer")
	}
	if conn.refreshPermsTimer.Start() {
		conn.log.Debugf("Started refresh permission timer")
	}

	return conn
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

func (a *allocation) createPermission(perm *permission, addr net.Addr) error {
	perm.mutex.Lock()
	defer perm.mutex.Unlock()

	if perm.state() == permStateIdle {
		// Punch a hole! (this would block a bit..)
		if err := a.CreatePermissions(addr); err != nil {
			a.permMap.delete(addr)

			return err
		}
		perm.setState(permStatePermitted)
	}

	return nil
}

// WriteTo writes a packet with payload to addr.
// WriteTo can be made to time out and return
// an Error with Timeout() == true after a fixed time limit;
// see SetDeadline and SetWriteDeadline.
// On packet-oriented connections, write timeouts are rare.
func (c *UDPConn) WriteTo(payload []byte, addr net.Addr) (int, error) { //nolint:gocognit,cyclop
	var err error
	_, ok := addr.(*net.UDPAddr)
	if !ok {
		return 0, errUDPAddrCast
	}

	// Check if we have a permission for the destination IP addr
	perm, ok := c.permMap.find(addr)
	if !ok {
		perm = &permission{}
		c.permMap.insert(addr, perm)
	}

	for i := 0; i < maxRetryAttempts; i++ {
		// c.createPermission() would block, per destination IP (, or perm),
		// until the perm state becomes "requested". Purpose of this is to
		// guarantee the order of packets (within the same perm).
		// Note that CreatePermission transaction may not be complete before
		// all the data transmission. This is done assuming that the request
		// will be most likely successful and we can tolerate some loss of
		// UDP packet (or reorder), inorder to minimize the latency in most cases.
		if err = c.createPermission(perm, addr); !errors.Is(err, errTryAgain) {
			break
		}
	}
	if err != nil {
		return 0, err
	}

	// Bind channel
	bound, ok := c.bindingMgr.findByAddr(addr)
	if !ok {
		bound = c.bindingMgr.create(addr)
	}

	bindSt := bound.state()

	//nolint:nestif
	if bindSt == bindingStateIdle || bindSt == bindingStateRequest || bindSt == bindingStateFailed {
		func() {
			// Block only callers with the same binding until
			// the binding transaction has been complete
			bound.muBind.Lock()
			defer bound.muBind.Unlock()

			// Binding state may have been changed while waiting. check again.
			if bound.state() == bindingStateIdle {
				bound.setState(bindingStateRequest)
				go func() {
					err2 := c.bind(bound)
					if err2 != nil {
						c.log.Warnf("Failed to bind bind(): %s", err2)
						bound.setState(bindingStateFailed)
						// Keep going...
					} else {
						bound.setState(bindingStateReady)
					}
				}()
			}
		}()

		// Send data using SendIndication
		peerAddr := addr2PeerAddress(addr)
		var msg *stun.Message
		msg, err = stun.Build(
			stun.TransactionID,
			stun.NewType(stun.MethodSend, stun.ClassIndication),
			proto.Data(payload),
			peerAddr,
			stun.Fingerprint,
		)
		if err != nil {
			return 0, err
		}

		// Indication has no transaction (fire-and-forget)

		return c.client.WriteTo(msg.Raw, c.serverAddr)
	}

	// Binding is either ready

	// Check if the binding needs a refresh
	func() {
		bound.muBind.Lock()
		defer bound.muBind.Unlock()

		if bound.state() == bindingStateReady && time.Since(bound.refreshedAt()) > 5*time.Minute {
			bound.setState(bindingStateRefresh)
			go func() {
				if bindErr := c.bind(bound); bindErr != nil {
					c.log.Warnf("Failed to bind() for refresh: %s", bindErr)
					bound.setState(bindingStateFailed)
					// Keep going...
				} else {
					bound.setRefreshedAt(time.Now())
					bound.setState(bindingStateReady)
				}
			}()
		}
	}()

	// Send via ChannelData
	_, err = c.sendChannelData(payload, bound.number)
	if err != nil {
		return 0, err
	}

	return len(payload), nil
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

	c.client.OnDeallocated(c.relayedAddr)

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
func (c *UDPConn) SetWriteDeadline(time.Time) error {
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

// CreatePermissions Issues a CreatePermission request for the supplied addresses
// as described in https://datatracker.ietf.org/doc/html/rfc5766#section-9
func (a *allocation) CreatePermissions(addrs ...net.Addr) error {
	setters := []stun.Setter{
		stun.TransactionID,
		stun.NewType(stun.MethodCreatePermission, stun.ClassRequest),
	}

	for _, addr := range addrs {
		setters = append(setters, addr2PeerAddress(addr))
	}

	setters = append(setters,
		a.username,
		a.realm,
		a.nonce(),
		a.integrity,
		stun.Fingerprint)

	msg, err := stun.Build(setters...)
	if err != nil {
		return err
	}

	trRes, err := a.client.PerformTransaction(msg, a.serverAddr, false)
	if err != nil {
		return err
	}

	res := trRes.Msg

	if res.Type.Class == stun.ClassErrorResponse {
		var code stun.ErrorCodeAttribute
		if err = code.GetFrom(res); err == nil {
			if code.Code == stun.CodeStaleNonce {
				a.setNonceFromMsg(res)

				return errTryAgain
			}

			return fmt.Errorf("%s (error %s)", res.Type, code) //nolint:goerr113
		}

		return fmt.Errorf("%s", res.Type) //nolint:goerr113
	}

	return nil
}

// HandleInbound passes inbound data in UDPConn.
func (c *UDPConn) HandleInbound(data []byte, from net.Addr) {
	// Copy data
	copied := make([]byte, len(data))
	copy(copied, data)

	select {
	case c.readCh <- &inboundData{data: copied, from: from}:
	default:
		c.log.Warnf("Receive buffer full")
	}
}

// FindAddrByChannelNumber returns a peer address associated with the
// channel number on this UDPConn.
func (c *UDPConn) FindAddrByChannelNumber(chNum uint16) (net.Addr, bool) {
	b, ok := c.bindingMgr.findByNumber(chNum)
	if !ok {
		return nil, false
	}

	return b.addr, true
}

func (c *UDPConn) bind(bound *binding) error {
	setters := []stun.Setter{
		stun.TransactionID,
		stun.NewType(stun.MethodChannelBind, stun.ClassRequest),
		addr2PeerAddress(bound.addr),
		proto.ChannelNumber(bound.number),
		c.username,
		c.realm,
		c.nonce(),
		c.integrity,
		stun.Fingerprint,
	}

	msg, err := stun.Build(setters...)
	if err != nil {
		return err
	}

	trRes, err := c.client.PerformTransaction(msg, c.serverAddr, false)
	if err != nil {
		c.bindingMgr.deleteByAddr(bound.addr)

		return err
	}

	res := trRes.Msg

	if res.Type != stun.NewType(stun.MethodChannelBind, stun.ClassSuccessResponse) {
		return fmt.Errorf("unexpected response type %s", res.Type) //nolint:goerr113
	}

	c.log.Debugf("Channel binding successful: %s %d", bound.addr, bound.number)

	// Success.
	return nil
}

func (c *UDPConn) sendChannelData(data []byte, chNum uint16) (int, error) {
	chData := &proto.ChannelData{
		Data:   data,
		Number: proto.ChannelNumber(chNum),
	}
	chData.Encode()
	_, err := c.client.WriteTo(chData.Raw, c.serverAddr)
	if err != nil {
		return 0, err
	}

	return len(data), nil
}
