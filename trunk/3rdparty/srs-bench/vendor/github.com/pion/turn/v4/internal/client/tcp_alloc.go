// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package client

import (
	"encoding/binary"
	"errors"
	"fmt"
	"math"
	"net"
	"time"

	"github.com/pion/stun/v3"
	"github.com/pion/transport/v3"
	"github.com/pion/turn/v4/internal/proto"
)

var (
	_ transport.TCPListener = (*TCPAllocation)(nil) // Includes type check for net.Listener
	_ transport.Dialer      = (*TCPAllocation)(nil)
)

func noDeadline() time.Time {
	return time.Time{}
}

// TCPAllocation is an active TCP allocation on the TURN server
// as specified by RFC 6062.
// The allocation can be used to Dial/Accept relayed outgoing/incoming TCP connections.
type TCPAllocation struct {
	connAttemptCh chan *connectionAttempt
	acceptTimer   *time.Timer
	allocation
}

// NewTCPAllocation creates a new instance of TCPConn
func NewTCPAllocation(config *AllocationConfig) *TCPAllocation {
	a := &TCPAllocation{
		connAttemptCh: make(chan *connectionAttempt, 10),
		acceptTimer:   time.NewTimer(time.Duration(math.MaxInt64)),
		allocation: allocation{
			client:      config.Client,
			relayedAddr: config.RelayedAddr,
			serverAddr:  config.ServerAddr,
			username:    config.Username,
			realm:       config.Realm,
			permMap:     newPermissionMap(),
			integrity:   config.Integrity,
			_nonce:      config.Nonce,
			_lifetime:   config.Lifetime,
			net:         config.Net,
			log:         config.Log,
		},
	}

	a.log.Debugf("Initial lifetime: %d seconds", int(a.lifetime().Seconds()))

	a.refreshAllocTimer = NewPeriodicTimer(
		timerIDRefreshAlloc,
		a.onRefreshTimers,
		a.lifetime()/2,
	)

	a.refreshPermsTimer = NewPeriodicTimer(
		timerIDRefreshPerms,
		a.onRefreshTimers,
		permRefreshInterval,
	)

	if a.refreshAllocTimer.Start() {
		a.log.Debug("Started refreshAllocTimer")
	}
	if a.refreshPermsTimer.Start() {
		a.log.Debug("Started refreshPermsTimer")
	}

	return a
}

// Connect sends a Connect request to the turn server and returns a chosen connection ID
func (a *TCPAllocation) Connect(peer net.Addr) (proto.ConnectionID, error) {
	setters := []stun.Setter{
		stun.TransactionID,
		stun.NewType(stun.MethodConnect, stun.ClassRequest),
		addr2PeerAddress(peer),
		a.username,
		a.realm,
		a.nonce(),
		a.integrity,
		stun.Fingerprint,
	}

	msg, err := stun.Build(setters...)
	if err != nil {
		return 0, err
	}

	a.log.Debugf("Send connect request (peer=%v)", peer)
	trRes, err := a.client.PerformTransaction(msg, a.serverAddr, false)
	if err != nil {
		return 0, err
	}

	res := trRes.Msg

	if res.Type.Class == stun.ClassErrorResponse {
		var code stun.ErrorCodeAttribute
		if err = code.GetFrom(res); err == nil {
			return 0, fmt.Errorf("%s (error %s)", res.Type, code) //nolint:goerr113
		}

		return 0, fmt.Errorf("%s", res.Type) //nolint:goerr113
	}

	var cid proto.ConnectionID
	if err := cid.GetFrom(res); err != nil {
		return 0, err
	}

	a.log.Debugf("Connect request successful (cid=%v)", cid)
	return cid, nil
}

// Dial connects to the address on the named network.
func (a *TCPAllocation) Dial(network, rAddrStr string) (net.Conn, error) {
	rAddr, err := net.ResolveTCPAddr(network, rAddrStr)
	if err != nil {
		return nil, err
	}

	return a.DialTCP(network, nil, rAddr)
}

// DialWithConn connects to the address on the named network with an already existing connection.
// The provided connection must be an already connected TCP connection to the TURN server.
func (a *TCPAllocation) DialWithConn(conn net.Conn, network, rAddrStr string) (*TCPConn, error) {
	rAddr, err := net.ResolveTCPAddr(network, rAddrStr)
	if err != nil {
		return nil, err
	}

	return a.DialTCPWithConn(conn, network, rAddr)
}

// DialTCP acts like Dial for TCP networks.
func (a *TCPAllocation) DialTCP(network string, lAddr, rAddr *net.TCPAddr) (*TCPConn, error) {
	var rAddrServer *net.TCPAddr
	if addr, ok := a.serverAddr.(*net.TCPAddr); ok {
		rAddrServer = &net.TCPAddr{
			IP:   addr.IP,
			Port: addr.Port,
		}
	} else if addr, ok := a.serverAddr.(*net.UDPAddr); ok {
		rAddrServer = &net.TCPAddr{
			IP:   addr.IP,
			Port: addr.Port,
		}
	} else {
		return nil, errInvalidTURNAddress
	}

	conn, err := a.net.DialTCP(network, lAddr, rAddrServer)
	if err != nil {
		return nil, err
	}

	dataConn, err := a.DialTCPWithConn(conn, network, rAddr)
	if err != nil {
		conn.Close() //nolint:errcheck,gosec
	}

	return dataConn, err
}

// DialTCPWithConn acts like DialWithConn for TCP networks.
func (a *TCPAllocation) DialTCPWithConn(conn net.Conn, _ string, rAddr *net.TCPAddr) (*TCPConn, error) {
	var err error

	// Check if we have a permission for the destination IP addr
	perm, ok := a.permMap.find(rAddr)
	if !ok {
		perm = &permission{}
		a.permMap.insert(rAddr, perm)
	}

	for i := 0; i < maxRetryAttempts; i++ {
		if err = a.createPermission(perm, rAddr); !errors.Is(err, errTryAgain) {
			break
		}
	}
	if err != nil {
		return nil, err
	}

	// Send connect request if haven't done so.
	cid, err := a.Connect(rAddr)
	if err != nil {
		return nil, err
	}

	tcpConn, ok := conn.(transport.TCPConn)
	if !ok {
		return nil, errTCPAddrCast
	}

	dataConn := &TCPConn{
		TCPConn:       tcpConn,
		ConnectionID:  cid,
		remoteAddress: rAddr,
		allocation:    a,
	}

	if err := a.BindConnection(dataConn, cid); err != nil {
		return nil, fmt.Errorf("failed to bind connection: %w", err)
	}

	return dataConn, nil
}

// BindConnection associates the provided connection
func (a *TCPAllocation) BindConnection(dataConn *TCPConn, cid proto.ConnectionID) error {
	msg, err := stun.Build(
		stun.TransactionID,
		stun.NewType(stun.MethodConnectionBind, stun.ClassRequest),
		cid,
		a.username,
		a.realm,
		a.nonce(),
		a.integrity,
		stun.Fingerprint,
	)
	if err != nil {
		return err
	}

	a.log.Debugf("Send connectionBind request (cid=%v)", cid)

	_, err = dataConn.Write(msg.Raw)
	if err != nil {
		return err
	}

	// Read exactly one STUN message, any data after belongs to the user
	b := make([]byte, stunHeaderSize)
	n, err := dataConn.Read(b)
	if n != stunHeaderSize {
		return errIncompleteTURNFrame
	} else if err != nil {
		return err
	}

	if !stun.IsMessage(b) {
		return errInvalidTURNFrame
	}

	datagramSize := binary.BigEndian.Uint16(b[2:4]) + stunHeaderSize
	raw := make([]byte, datagramSize)
	copy(raw, b)
	_, err = dataConn.Read(raw[stunHeaderSize:])
	if err != nil {
		return err
	}
	res := &stun.Message{Raw: raw}
	if err = res.Decode(); err != nil {
		return fmt.Errorf("failed to decode STUN message: %w", err)
	}

	switch res.Type.Class {
	case stun.ClassErrorResponse:
		var code stun.ErrorCodeAttribute
		if err = code.GetFrom(res); err == nil {
			return fmt.Errorf("%s (error %s)", res.Type, code) //nolint:goerr113
		}
		return fmt.Errorf("%s", res.Type) //nolint:goerr113
	case stun.ClassSuccessResponse:
		a.log.Debug("Successful connectionBind request")
		return nil
	default:
		return fmt.Errorf("%w: %s", errUnexpectedSTUNRequestMessage, res.String())
	}
}

// Accept waits for and returns the next connection to the listener.
func (a *TCPAllocation) Accept() (net.Conn, error) {
	return a.AcceptTCP()
}

// AcceptTCP accepts the next incoming call and returns the new connection.
func (a *TCPAllocation) AcceptTCP() (transport.TCPConn, error) {
	addr, err := net.ResolveTCPAddr("tcp4", a.serverAddr.String())
	if err != nil {
		return nil, err
	}

	tcpConn, err := a.net.DialTCP("tcp", nil, addr)
	if err != nil {
		return nil, err
	}

	dataConn, err := a.AcceptTCPWithConn(tcpConn)
	if err != nil {
		tcpConn.Close() //nolint:errcheck,gosec
	}

	return dataConn, err
}

// AcceptTCPWithConn accepts the next incoming call and returns the new connection.
func (a *TCPAllocation) AcceptTCPWithConn(conn net.Conn) (*TCPConn, error) {
	select {
	case attempt := <-a.connAttemptCh:

		tcpConn, ok := conn.(transport.TCPConn)
		if !ok {
			return nil, errTCPAddrCast
		}

		dataConn := &TCPConn{
			TCPConn:       tcpConn,
			ConnectionID:  attempt.cid,
			remoteAddress: attempt.from,
			allocation:    a,
		}

		if err := a.BindConnection(dataConn, attempt.cid); err != nil {
			return nil, fmt.Errorf("failed to bind connection: %w", err)
		}

		return dataConn, nil
	case <-a.acceptTimer.C:
		return nil, &net.OpError{
			Op:   "accept",
			Net:  a.Addr().Network(),
			Addr: a.Addr(),
			Err:  newTimeoutError("i/o timeout"),
		}
	}
}

// SetDeadline sets the deadline associated with the listener. A zero time value disables the deadline.
func (a *TCPAllocation) SetDeadline(t time.Time) error {
	var d time.Duration
	if t == noDeadline() {
		d = time.Duration(math.MaxInt64)
	} else {
		d = time.Until(t)
	}
	a.acceptTimer.Reset(d)
	return nil
}

// Close releases the allocation
// Any blocked Accept operations will be unblocked and return errors.
// Any opened connection via Dial/Accept will be closed.
func (a *TCPAllocation) Close() error {
	a.refreshAllocTimer.Stop()
	a.refreshPermsTimer.Stop()

	a.client.OnDeallocated(a.relayedAddr)
	return a.refreshAllocation(0, true /* dontWait=true */)
}

// Addr returns the relayed address of the allocation
func (a *TCPAllocation) Addr() net.Addr {
	return a.relayedAddr
}

// HandleConnectionAttempt is called by the TURN client
// when it receives a ConnectionAttempt indication.
func (a *TCPAllocation) HandleConnectionAttempt(from *net.TCPAddr, cid proto.ConnectionID) {
	a.connAttemptCh <- &connectionAttempt{
		from: from,
		cid:  cid,
	}
}
