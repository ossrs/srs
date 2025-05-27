// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package turn

import (
	b64 "encoding/base64"
	"fmt"
	"math"
	"net"
	"sync"
	"time"

	"github.com/pion/logging"
	"github.com/pion/stun/v3"
	"github.com/pion/transport/v3"
	"github.com/pion/transport/v3/stdnet"
	"github.com/pion/turn/v4/internal/client"
	"github.com/pion/turn/v4/internal/proto"
)

const (
	defaultRTO        = 200 * time.Millisecond
	maxRtxCount       = 7              // Total 7 requests (Rc)
	maxDataBufferSize = math.MaxUint16 // Message size limit for Chromium
)

//              interval [msec]
// 0: 0 ms      +500
// 1: 500 ms	+1000
// 2: 1500 ms   +2000
// 3: 3500 ms   +4000
// 4: 7500 ms   +8000
// 5: 15500 ms  +16000
// 6: 31500 ms  +32000
// -: 63500 ms  failed

// ClientConfig is a bag of config parameters for Client.
type ClientConfig struct {
	STUNServerAddr string // STUN server address (e.g. "stun.abc.com:3478")
	TURNServerAddr string // TURN server address (e.g. "turn.abc.com:3478")
	Username       string
	Password       string
	Realm          string
	Software       string
	RTO            time.Duration
	Conn           net.PacketConn // Listening socket (net.PacketConn)
	Net            transport.Net
	LoggerFactory  logging.LoggerFactory
}

// Client is a STUN server client
type Client struct {
	conn           net.PacketConn // Read-only
	net            transport.Net  // Read-only
	stunServerAddr net.Addr       // Read-only
	turnServerAddr net.Addr       // Read-only

	username      stun.Username          // Read-only
	password      string                 // Read-only
	realm         stun.Realm             // Read-only
	integrity     stun.MessageIntegrity  // Read-only
	software      stun.Software          // Read-only
	trMap         *client.TransactionMap // Thread-safe
	rto           time.Duration          // Read-only
	relayedConn   *client.UDPConn        // Protected by mutex ***
	tcpAllocation *client.TCPAllocation  // Protected by mutex ***
	allocTryLock  client.TryLock         // Thread-safe
	listenTryLock client.TryLock         // Thread-safe
	mutex         sync.RWMutex           // Thread-safe
	mutexTrMap    sync.Mutex             // Thread-safe
	log           logging.LeveledLogger  // Read-only
}

// NewClient returns a new Client instance. listeningAddress is the address and port to listen on, default "0.0.0.0:0"
func NewClient(config *ClientConfig) (*Client, error) {
	loggerFactory := config.LoggerFactory
	if loggerFactory == nil {
		loggerFactory = logging.NewDefaultLoggerFactory()
	}

	log := loggerFactory.NewLogger("turnc")

	if config.Conn == nil {
		return nil, errNilConn
	}

	rto := defaultRTO
	if config.RTO > 0 {
		rto = config.RTO
	}

	if config.Net == nil {
		n, err := stdnet.NewNet()
		if err != nil {
			return nil, err
		}
		config.Net = n
	}

	var stunServ, turnServ net.Addr
	var err error

	if len(config.STUNServerAddr) > 0 {
		stunServ, err = config.Net.ResolveUDPAddr("udp4", config.STUNServerAddr)
		if err != nil {
			return nil, err
		}

		log.Debugf("Resolved STUN server %s to %s", config.STUNServerAddr, stunServ)
	}

	if len(config.TURNServerAddr) > 0 {
		turnServ, err = config.Net.ResolveUDPAddr("udp4", config.TURNServerAddr)
		if err != nil {
			return nil, err
		}

		log.Debugf("Resolved TURN server %s to %s", config.TURNServerAddr, turnServ)
	}

	c := &Client{
		conn:           config.Conn,
		stunServerAddr: stunServ,
		turnServerAddr: turnServ,
		username:       stun.NewUsername(config.Username),
		password:       config.Password,
		realm:          stun.NewRealm(config.Realm),
		software:       stun.NewSoftware(config.Software),
		trMap:          client.NewTransactionMap(),
		net:            config.Net,
		rto:            rto,
		log:            log,
	}

	return c, nil
}

// TURNServerAddr return the TURN server address
func (c *Client) TURNServerAddr() net.Addr {
	return c.turnServerAddr
}

// STUNServerAddr return the STUN server address
func (c *Client) STUNServerAddr() net.Addr {
	return c.stunServerAddr
}

// Username returns username
func (c *Client) Username() stun.Username {
	return c.username
}

// Realm return realm
func (c *Client) Realm() stun.Realm {
	return c.realm
}

// WriteTo sends data to the specified destination using the base socket.
func (c *Client) WriteTo(data []byte, to net.Addr) (int, error) {
	return c.conn.WriteTo(data, to)
}

// Listen will have this client start listening on the conn provided via the config.
// This is optional. If not used, you will need to call HandleInbound method
// to supply incoming data, instead.
func (c *Client) Listen() error {
	if err := c.listenTryLock.Lock(); err != nil {
		return fmt.Errorf("%w: %s", errAlreadyListening, err.Error())
	}

	go func() {
		buf := make([]byte, maxDataBufferSize)
		for {
			n, from, err := c.conn.ReadFrom(buf)
			if err != nil {
				c.log.Debugf("Failed to read: %s. Exiting loop", err)
				break
			}

			_, err = c.HandleInbound(buf[:n], from)
			if err != nil {
				c.log.Debugf("Failed to handle inbound message: %s. Exiting loop", err)
				break
			}
		}

		c.listenTryLock.Unlock()
	}()

	return nil
}

// Close closes this client
func (c *Client) Close() {
	c.mutexTrMap.Lock()
	defer c.mutexTrMap.Unlock()

	c.trMap.CloseAndDeleteAll()
}

// TransactionID & Base64: https://play.golang.org/p/EEgmJDI971P

// SendBindingRequestTo sends a new STUN request to the given transport address
func (c *Client) SendBindingRequestTo(to net.Addr) (net.Addr, error) {
	attrs := []stun.Setter{stun.TransactionID, stun.BindingRequest}
	if len(c.software) > 0 {
		attrs = append(attrs, c.software)
	}

	msg, err := stun.Build(attrs...)
	if err != nil {
		return nil, err
	}
	trRes, err := c.PerformTransaction(msg, to, false)
	if err != nil {
		return nil, err
	}

	var reflAddr stun.XORMappedAddress
	if err := reflAddr.GetFrom(trRes.Msg); err != nil {
		return nil, err
	}

	return &net.UDPAddr{
		IP:   reflAddr.IP,
		Port: reflAddr.Port,
	}, nil
}

// SendBindingRequest sends a new STUN request to the STUN server
func (c *Client) SendBindingRequest() (net.Addr, error) {
	if c.stunServerAddr == nil {
		return nil, errSTUNServerAddressNotSet
	}
	return c.SendBindingRequestTo(c.stunServerAddr)
}

func (c *Client) sendAllocateRequest(protocol proto.Protocol) (proto.RelayedAddress, proto.Lifetime, stun.Nonce, error) {
	var relayed proto.RelayedAddress
	var lifetime proto.Lifetime
	var nonce stun.Nonce

	msg, err := stun.Build(
		stun.TransactionID,
		stun.NewType(stun.MethodAllocate, stun.ClassRequest),
		proto.RequestedTransport{Protocol: protocol},
		stun.Fingerprint,
	)
	if err != nil {
		return relayed, lifetime, nonce, err
	}

	trRes, err := c.PerformTransaction(msg, c.turnServerAddr, false)
	if err != nil {
		return relayed, lifetime, nonce, err
	}

	res := trRes.Msg

	// Anonymous allocate failed, trying to authenticate.
	if err = nonce.GetFrom(res); err != nil {
		return relayed, lifetime, nonce, err
	}
	if err = c.realm.GetFrom(res); err != nil {
		return relayed, lifetime, nonce, err
	}
	c.realm = append([]byte(nil), c.realm...)
	c.integrity = stun.NewLongTermIntegrity(
		c.username.String(), c.realm.String(), c.password,
	)
	// Trying to authorize.
	msg, err = stun.Build(
		stun.TransactionID,
		stun.NewType(stun.MethodAllocate, stun.ClassRequest),
		proto.RequestedTransport{Protocol: protocol},
		&c.username,
		&c.realm,
		&nonce,
		&c.integrity,
		stun.Fingerprint,
	)
	if err != nil {
		return relayed, lifetime, nonce, err
	}

	trRes, err = c.PerformTransaction(msg, c.turnServerAddr, false)
	if err != nil {
		return relayed, lifetime, nonce, err
	}
	res = trRes.Msg

	if res.Type.Class == stun.ClassErrorResponse {
		var code stun.ErrorCodeAttribute
		if err = code.GetFrom(res); err == nil {
			return relayed, lifetime, nonce, fmt.Errorf("%s (error %s)", res.Type, code) //nolint:goerr113
		}
		return relayed, lifetime, nonce, fmt.Errorf("%s", res.Type) //nolint:goerr113
	}

	// Getting relayed addresses from response.
	if err := relayed.GetFrom(res); err != nil {
		return relayed, lifetime, nonce, err
	}

	// Getting lifetime from response
	if err := lifetime.GetFrom(res); err != nil {
		return relayed, lifetime, nonce, err
	}
	return relayed, lifetime, nonce, nil
}

// Allocate sends a TURN allocation request to the given transport address
func (c *Client) Allocate() (net.PacketConn, error) {
	if err := c.allocTryLock.Lock(); err != nil {
		return nil, fmt.Errorf("%w: %s", errOneAllocateOnly, err.Error())
	}
	defer c.allocTryLock.Unlock()

	relayedConn := c.relayedUDPConn()
	if relayedConn != nil {
		return nil, fmt.Errorf("%w: %s", errAlreadyAllocated, relayedConn.LocalAddr().String())
	}

	relayed, lifetime, nonce, err := c.sendAllocateRequest(proto.ProtoUDP)
	if err != nil {
		return nil, err
	}

	relayedAddr := &net.UDPAddr{
		IP:   relayed.IP,
		Port: relayed.Port,
	}

	relayedConn = client.NewUDPConn(&client.AllocationConfig{
		Client:      c,
		RelayedAddr: relayedAddr,
		ServerAddr:  c.turnServerAddr,
		Realm:       c.realm,
		Username:    c.username,
		Integrity:   c.integrity,
		Nonce:       nonce,
		Lifetime:    lifetime.Duration,
		Net:         c.net,
		Log:         c.log,
	})
	c.setRelayedUDPConn(relayedConn)

	return relayedConn, nil
}

// AllocateTCP creates a new TCP allocation at the TURN server.
func (c *Client) AllocateTCP() (*client.TCPAllocation, error) {
	if err := c.allocTryLock.Lock(); err != nil {
		return nil, fmt.Errorf("%w: %s", errOneAllocateOnly, err.Error())
	}
	defer c.allocTryLock.Unlock()

	allocation := c.getTCPAllocation()
	if allocation != nil {
		return nil, fmt.Errorf("%w: %s", errAlreadyAllocated, allocation.Addr())
	}

	relayed, lifetime, nonce, err := c.sendAllocateRequest(proto.ProtoTCP)
	if err != nil {
		return nil, err
	}

	relayedAddr := &net.TCPAddr{
		IP:   relayed.IP,
		Port: relayed.Port,
	}

	allocation = client.NewTCPAllocation(&client.AllocationConfig{
		Client:      c,
		RelayedAddr: relayedAddr,
		ServerAddr:  c.turnServerAddr,
		Realm:       c.realm,
		Username:    c.username,
		Integrity:   c.integrity,
		Nonce:       nonce,
		Lifetime:    lifetime.Duration,
		Net:         c.net,
		Log:         c.log,
	})

	c.setTCPAllocation(allocation)

	return allocation, nil
}

// CreatePermission Issues a CreatePermission request for the supplied addresses
// as described in https://datatracker.ietf.org/doc/html/rfc5766#section-9
func (c *Client) CreatePermission(addrs ...net.Addr) error {
	if conn := c.relayedUDPConn(); conn != nil {
		if err := conn.CreatePermissions(addrs...); err != nil {
			return err
		}
	}

	if allocation := c.getTCPAllocation(); allocation != nil {
		if err := allocation.CreatePermissions(addrs...); err != nil {
			return err
		}
	}
	return nil
}

// PerformTransaction performs STUN transaction
func (c *Client) PerformTransaction(msg *stun.Message, to net.Addr, ignoreResult bool) (client.TransactionResult,
	error,
) {
	trKey := b64.StdEncoding.EncodeToString(msg.TransactionID[:])

	raw := make([]byte, len(msg.Raw))
	copy(raw, msg.Raw)

	tr := client.NewTransaction(&client.TransactionConfig{
		Key:          trKey,
		Raw:          raw,
		To:           to,
		Interval:     c.rto,
		IgnoreResult: ignoreResult,
	})

	c.trMap.Insert(trKey, tr)

	c.log.Tracef("Start %s transaction %s to %s", msg.Type, trKey, tr.To)
	_, err := c.conn.WriteTo(tr.Raw, to)
	if err != nil {
		return client.TransactionResult{}, err
	}

	tr.StartRtxTimer(c.onRtxTimeout)

	// If ignoreResult is true, get the transaction going and return immediately
	if ignoreResult {
		return client.TransactionResult{}, nil
	}

	res := tr.WaitForResult()
	if res.Err != nil {
		return res, res.Err
	}
	return res, nil
}

// OnDeallocated is called when de-allocation of relay address has been complete.
// (Called by UDPConn)
func (c *Client) OnDeallocated(net.Addr) {
	c.setRelayedUDPConn(nil)
	c.setTCPAllocation(nil)
}

// HandleInbound handles data received.
// This method handles incoming packet de-multiplex it by the source address
// and the types of the message.
// This return a boolean (handled or not) and if there was an error.
// Caller should check if the packet was handled by this client or not.
// If not handled, it is assumed that the packet is application data.
// If an error is returned, the caller should discard the packet regardless.
func (c *Client) HandleInbound(data []byte, from net.Addr) (bool, error) {
	// +-------------------+-------------------------------+
	// |   Return Values   |                               |
	// +-------------------+       Meaning / Action        |
	// | handled |  error  |                               |
	// |=========+=========+===============================+
	// |  false  |   nil   | Handle the packet as app data |
	// |---------+---------+-------------------------------+
	// |  true   |   nil   |        Nothing to do          |
	// |---------+---------+-------------------------------+
	// |  false  |  error  |     (shouldn't happen)        |
	// |---------+---------+-------------------------------+
	// |  true   |  error  | Error occurred while handling |
	// +---------+---------+-------------------------------+
	// Possible causes of the error:
	//  - Malformed packet (parse error)
	//  - STUN message was a request
	//  - Non-STUN message from the STUN server

	switch {
	case stun.IsMessage(data):
		return true, c.handleSTUNMessage(data, from)
	case proto.IsChannelData(data):
		return true, c.handleChannelData(data)
	case c.stunServerAddr != nil && from.String() == c.stunServerAddr.String():
		// Received from STUN server but it is not a STUN message
		return true, errNonSTUNMessage
	default:
		// Assume, this is an application data
		c.log.Tracef("Ignoring non-STUN/TURN packet")
	}

	return false, nil
}

func (c *Client) handleSTUNMessage(data []byte, from net.Addr) error {
	raw := make([]byte, len(data))
	copy(raw, data)

	msg := &stun.Message{Raw: raw}
	if err := msg.Decode(); err != nil {
		return fmt.Errorf("%w: %s", errFailedToDecodeSTUN, err.Error())
	}

	if msg.Type.Class == stun.ClassRequest {
		return fmt.Errorf("%w : %s", errUnexpectedSTUNRequestMessage, msg.String())
	}

	if msg.Type.Class == stun.ClassIndication {
		switch msg.Type.Method {
		case stun.MethodData:
			var peerAddr proto.PeerAddress
			if err := peerAddr.GetFrom(msg); err != nil {
				return err
			}
			from = &net.UDPAddr{
				IP:   peerAddr.IP,
				Port: peerAddr.Port,
			}

			var data proto.Data
			if err := data.GetFrom(msg); err != nil {
				return err
			}

			c.log.Tracef("Data indication received from %s", from)

			relayedConn := c.relayedUDPConn()
			if relayedConn == nil {
				c.log.Debug("No relayed conn allocated")
				return nil // Silently discard
			}
			relayedConn.HandleInbound(data, from)
		case stun.MethodConnectionAttempt:
			var peerAddr proto.PeerAddress
			if err := peerAddr.GetFrom(msg); err != nil {
				return err
			}

			addr := &net.TCPAddr{
				IP:   peerAddr.IP,
				Port: peerAddr.Port,
			}

			var cid proto.ConnectionID
			if err := cid.GetFrom(msg); err != nil {
				return err
			}

			c.log.Debugf("Connection attempt from %s", addr)

			allocation := c.getTCPAllocation()
			if allocation == nil {
				c.log.Debug("No TCP allocation exists")
				return nil // Silently discard
			}

			allocation.HandleConnectionAttempt(addr, cid)
		default:
			c.log.Debug("Received unsupported STUN method")
		}
		return nil
	}

	// This is a STUN response message (transactional)
	// The type is either:
	// - stun.ClassSuccessResponse
	// - stun.ClassErrorResponse

	trKey := b64.StdEncoding.EncodeToString(msg.TransactionID[:])

	c.mutexTrMap.Lock()
	tr, ok := c.trMap.Find(trKey)
	if !ok {
		c.mutexTrMap.Unlock()
		// Silently discard
		c.log.Debugf("No transaction for %s", msg)
		return nil
	}

	// End the transaction
	tr.StopRtxTimer()
	c.trMap.Delete(trKey)
	c.mutexTrMap.Unlock()

	if !tr.WriteResult(client.TransactionResult{
		Msg:     msg,
		From:    from,
		Retries: tr.Retries(),
	}) {
		c.log.Debugf("No listener for %s", msg)
	}

	return nil
}

func (c *Client) handleChannelData(data []byte) error {
	chData := &proto.ChannelData{
		Raw: make([]byte, len(data)),
	}
	copy(chData.Raw, data)
	if err := chData.Decode(); err != nil {
		return err
	}

	relayedConn := c.relayedUDPConn()
	if relayedConn == nil {
		c.log.Debug("No relayed conn allocated")
		return nil // Silently discard
	}

	addr, ok := relayedConn.FindAddrByChannelNumber(uint16(chData.Number))
	if !ok {
		return fmt.Errorf("%w: %d", errChannelBindNotFound, int(chData.Number))
	}

	c.log.Tracef("Channel data received from %s (ch=%d)", addr.String(), int(chData.Number))

	relayedConn.HandleInbound(chData.Data, addr)
	return nil
}

func (c *Client) onRtxTimeout(trKey string, nRtx int) {
	c.mutexTrMap.Lock()
	defer c.mutexTrMap.Unlock()

	tr, ok := c.trMap.Find(trKey)
	if !ok {
		return // Already gone
	}

	if nRtx == maxRtxCount {
		// All retransmissions failed
		c.trMap.Delete(trKey)
		if !tr.WriteResult(client.TransactionResult{
			Err: fmt.Errorf("%w %s", errAllRetransmissionsFailed, trKey),
		}) {
			c.log.Debug("No listener for transaction")
		}
		return
	}

	c.log.Tracef("Retransmitting transaction %s to %s (nRtx=%d)",
		trKey, tr.To.String(), nRtx)
	_, err := c.conn.WriteTo(tr.Raw, tr.To)
	if err != nil {
		c.trMap.Delete(trKey)
		if !tr.WriteResult(client.TransactionResult{
			Err: fmt.Errorf("%w %s", errFailedToRetransmitTransaction, trKey),
		}) {
			c.log.Debug("No listener for transaction")
		}
		return
	}
	tr.StartRtxTimer(c.onRtxTimeout)
}

func (c *Client) setRelayedUDPConn(conn *client.UDPConn) {
	c.mutex.Lock()
	defer c.mutex.Unlock()

	c.relayedConn = conn
}

func (c *Client) relayedUDPConn() *client.UDPConn {
	c.mutex.RLock()
	defer c.mutex.RUnlock()

	return c.relayedConn
}

func (c *Client) setTCPAllocation(alloc *client.TCPAllocation) {
	c.mutex.Lock()
	defer c.mutex.Unlock()

	c.tcpAllocation = alloc
}

func (c *Client) getTCPAllocation() *client.TCPAllocation {
	c.mutex.RLock()
	defer c.mutex.RUnlock()

	return c.tcpAllocation
}
