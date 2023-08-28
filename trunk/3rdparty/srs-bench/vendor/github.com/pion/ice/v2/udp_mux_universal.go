// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"fmt"
	"net"
	"time"

	"github.com/pion/logging"
	"github.com/pion/stun"
	"github.com/pion/transport/v2"
)

// UniversalUDPMux allows multiple connections to go over a single UDP port for
// host, server reflexive and relayed candidates.
// Actual connection muxing is happening in the UDPMux.
type UniversalUDPMux interface {
	UDPMux
	GetXORMappedAddr(stunAddr net.Addr, deadline time.Duration) (*stun.XORMappedAddress, error)
	GetRelayedAddr(turnAddr net.Addr, deadline time.Duration) (*net.Addr, error)
	GetConnForURL(ufrag string, url string, addr net.Addr) (net.PacketConn, error)
}

// UniversalUDPMuxDefault handles STUN and TURN servers packets by wrapping the original UDPConn overriding ReadFrom.
// It the passes packets to the UDPMux that does the actual connection muxing.
type UniversalUDPMuxDefault struct {
	*UDPMuxDefault
	params UniversalUDPMuxParams

	// Since we have a shared socket, for srflx candidates it makes sense to have a shared mapped address across all the agents
	// stun.XORMappedAddress indexed by the STUN server addr
	xorMappedMap map[string]*xorMapped
}

// UniversalUDPMuxParams are parameters for UniversalUDPMux server reflexive.
type UniversalUDPMuxParams struct {
	Logger                logging.LeveledLogger
	UDPConn               net.PacketConn
	XORMappedAddrCacheTTL time.Duration
	Net                   transport.Net
}

// NewUniversalUDPMuxDefault creates an implementation of UniversalUDPMux embedding UDPMux
func NewUniversalUDPMuxDefault(params UniversalUDPMuxParams) *UniversalUDPMuxDefault {
	if params.Logger == nil {
		params.Logger = logging.NewDefaultLoggerFactory().NewLogger("ice")
	}
	if params.XORMappedAddrCacheTTL == 0 {
		params.XORMappedAddrCacheTTL = time.Second * 25
	}

	m := &UniversalUDPMuxDefault{
		params:       params,
		xorMappedMap: make(map[string]*xorMapped),
	}

	// Wrap UDP connection, process server reflexive messages
	// before they are passed to the UDPMux connection handler (connWorker)
	m.params.UDPConn = &udpConn{
		PacketConn: params.UDPConn,
		mux:        m,
		logger:     params.Logger,
	}

	// Embed UDPMux
	udpMuxParams := UDPMuxParams{
		Logger:  params.Logger,
		UDPConn: m.params.UDPConn,
		Net:     m.params.Net,
	}
	m.UDPMuxDefault = NewUDPMuxDefault(udpMuxParams)

	return m
}

// udpConn is a wrapper around UDPMux conn that overrides ReadFrom and handles STUN/TURN packets
type udpConn struct {
	net.PacketConn
	mux    *UniversalUDPMuxDefault
	logger logging.LeveledLogger
}

// GetRelayedAddr creates relayed connection to the given TURN service and returns the relayed addr.
// Not implemented yet.
func (m *UniversalUDPMuxDefault) GetRelayedAddr(net.Addr, time.Duration) (*net.Addr, error) {
	return nil, errNotImplemented
}

// GetConnForURL add uniques to the muxed connection by concatenating ufrag and URL (e.g. STUN URL) to be able to support multiple STUN/TURN servers
// and return a unique connection per server.
func (m *UniversalUDPMuxDefault) GetConnForURL(ufrag string, url string, addr net.Addr) (net.PacketConn, error) {
	return m.UDPMuxDefault.GetConn(fmt.Sprintf("%s%s", ufrag, url), addr)
}

// ReadFrom is called by UDPMux connWorker and handles packets coming from the STUN server discovering a mapped address.
// It passes processed packets further to the UDPMux (maybe this is not really necessary).
func (c *udpConn) ReadFrom(p []byte) (n int, addr net.Addr, err error) {
	n, addr, err = c.PacketConn.ReadFrom(p)
	if err != nil {
		return
	}

	if stun.IsMessage(p[:n]) {
		msg := &stun.Message{
			Raw: append([]byte{}, p[:n]...),
		}

		if err = msg.Decode(); err != nil {
			c.logger.Warnf("Failed to handle decode ICE from %s: %v", addr.String(), err)
			err = nil
			return
		}

		udpAddr, ok := addr.(*net.UDPAddr)
		if !ok {
			// Message about this err will be logged in the UDPMux
			return
		}

		if c.mux.isXORMappedResponse(msg, udpAddr.String()) {
			err = c.mux.handleXORMappedResponse(udpAddr, msg)
			if err != nil {
				c.logger.Debugf("%w: %v", errGetXorMappedAddrResponse, err)
				err = nil
			}
			return
		}
	}
	return n, addr, err
}

// isXORMappedResponse indicates whether the message is a XORMappedAddress and is coming from the known STUN server.
func (m *UniversalUDPMuxDefault) isXORMappedResponse(msg *stun.Message, stunAddr string) bool {
	m.mu.Lock()
	defer m.mu.Unlock()
	// Check first if it is a STUN server address because remote peer can also send similar messages but as a BindingSuccess
	_, ok := m.xorMappedMap[stunAddr]
	_, err := msg.Get(stun.AttrXORMappedAddress)
	return err == nil && ok
}

// handleXORMappedResponse parses response from the STUN server, extracts XORMappedAddress attribute
// and set the mapped address for the server
func (m *UniversalUDPMuxDefault) handleXORMappedResponse(stunAddr *net.UDPAddr, msg *stun.Message) error {
	m.mu.Lock()
	defer m.mu.Unlock()

	mappedAddr, ok := m.xorMappedMap[stunAddr.String()]
	if !ok {
		return errNoXorAddrMapping
	}

	var addr stun.XORMappedAddress
	if err := addr.GetFrom(msg); err != nil {
		return err
	}

	m.xorMappedMap[stunAddr.String()] = mappedAddr
	mappedAddr.SetAddr(&addr)

	return nil
}

// GetXORMappedAddr returns *stun.XORMappedAddress if already present for a given STUN server.
// Makes a STUN binding request to discover mapped address otherwise.
// Blocks until the stun.XORMappedAddress has been discovered or deadline.
// Method is safe for concurrent use.
func (m *UniversalUDPMuxDefault) GetXORMappedAddr(serverAddr net.Addr, deadline time.Duration) (*stun.XORMappedAddress, error) {
	m.mu.Lock()
	mappedAddr, ok := m.xorMappedMap[serverAddr.String()]
	// If we already have a mapping for this STUN server (address already received)
	// and if it is not too old we return it without making a new request to STUN server
	if ok {
		if mappedAddr.expired() {
			mappedAddr.closeWaiters()
			delete(m.xorMappedMap, serverAddr.String())
			ok = false
		} else if mappedAddr.pending() {
			ok = false
		}
	}
	m.mu.Unlock()
	if ok {
		return mappedAddr.addr, nil
	}

	// Otherwise, make a STUN request to discover the address
	// or wait for already sent request to complete
	waitAddrReceived, err := m.sendSTUN(serverAddr)
	if err != nil {
		return nil, fmt.Errorf("%w: %s", errSendSTUNPacket, err) //nolint:errorlint
	}

	// Block until response was handled by the connWorker routine and XORMappedAddress was updated
	select {
	case <-waitAddrReceived:
		// When channel closed, addr was obtained
		m.mu.Lock()
		mappedAddr := *m.xorMappedMap[serverAddr.String()]
		m.mu.Unlock()
		if mappedAddr.addr == nil {
			return nil, errNoXorAddrMapping
		}
		return mappedAddr.addr, nil
	case <-time.After(deadline):
		return nil, errXORMappedAddrTimeout
	}
}

// sendSTUN sends a STUN request via UDP conn.
//
// The returned channel is closed when the STUN response has been received.
// Method is safe for concurrent use.
func (m *UniversalUDPMuxDefault) sendSTUN(serverAddr net.Addr) (chan struct{}, error) {
	m.mu.Lock()
	defer m.mu.Unlock()

	// If record present in the map, we already sent a STUN request,
	// just wait when waitAddrReceived will be closed
	addrMap, ok := m.xorMappedMap[serverAddr.String()]
	if !ok {
		addrMap = &xorMapped{
			expiresAt:        time.Now().Add(m.params.XORMappedAddrCacheTTL),
			waitAddrReceived: make(chan struct{}),
		}
		m.xorMappedMap[serverAddr.String()] = addrMap
	}

	req, err := stun.Build(stun.BindingRequest, stun.TransactionID)
	if err != nil {
		return nil, err
	}

	if _, err = m.params.UDPConn.WriteTo(req.Raw, serverAddr); err != nil {
		return nil, err
	}

	return addrMap.waitAddrReceived, nil
}

type xorMapped struct {
	addr             *stun.XORMappedAddress
	waitAddrReceived chan struct{}
	expiresAt        time.Time
}

func (a *xorMapped) closeWaiters() {
	select {
	case <-a.waitAddrReceived:
		// Notify was close, ok, that means we received duplicate response just exit
		break
	default:
		// Notify tha twe have a new addr
		close(a.waitAddrReceived)
	}
}

func (a *xorMapped) pending() bool {
	return a.addr == nil
}

func (a *xorMapped) expired() bool {
	return a.expiresAt.Before(time.Now())
}

func (a *xorMapped) SetAddr(addr *stun.XORMappedAddress) {
	a.addr = addr
	a.closeWaiters()
}
