package ice

import (
	"context"
	"fmt"
	"hash/crc32"
	"net"
	"strconv"
	"strings"
	"sync/atomic"
	"time"

	"github.com/pion/logging"
	"github.com/pion/stun"
)

type candidateBase struct {
	id            string
	networkType   NetworkType
	candidateType CandidateType

	component      uint16
	address        string
	port           int
	relatedAddress *CandidateRelatedAddress
	tcpType        TCPType

	resolvedAddr net.Addr

	lastSent     atomic.Value
	lastReceived atomic.Value
	conn         net.PacketConn

	currAgent *Agent
	closeCh   chan struct{}
	closedCh  chan struct{}

	foundationOverride string
	priorityOverride   uint32
}

// Done implements context.Context
func (c *candidateBase) Done() <-chan struct{} {
	return c.closeCh
}

// Err implements context.Context
func (c *candidateBase) Err() error {
	select {
	case <-c.closedCh:
		return ErrRunCanceled
	default:
		return nil
	}
}

// Deadline implements context.Context
func (c *candidateBase) Deadline() (deadline time.Time, ok bool) {
	return time.Time{}, false
}

// Value implements context.Context
func (c *candidateBase) Value(key interface{}) interface{} {
	return nil
}

// ID returns Candidate ID
func (c *candidateBase) ID() string {
	return c.id
}

func (c *candidateBase) Foundation() string {
	if c.foundationOverride != "" {
		return c.foundationOverride
	}

	return fmt.Sprintf("%d", crc32.ChecksumIEEE([]byte(c.Type().String()+c.address+c.networkType.String())))
}

// Address returns Candidate Address
func (c *candidateBase) Address() string {
	return c.address
}

// Port returns Candidate Port
func (c *candidateBase) Port() int {
	return c.port
}

// Type returns candidate type
func (c *candidateBase) Type() CandidateType {
	return c.candidateType
}

// NetworkType returns candidate NetworkType
func (c *candidateBase) NetworkType() NetworkType {
	return c.networkType
}

// Component returns candidate component
func (c *candidateBase) Component() uint16 {
	return c.component
}

func (c *candidateBase) SetComponent(component uint16) {
	c.component = component
}

// LocalPreference returns the local preference for this candidate
func (c *candidateBase) LocalPreference() uint16 {
	if c.NetworkType().IsTCP() {
		// RFC 6544, section 4.2
		//
		// In Section 4.1.2.1 of [RFC5245], a recommended formula for UDP ICE
		// candidate prioritization is defined.  For TCP candidates, the same
		// formula and candidate type preferences SHOULD be used, and the
		// RECOMMENDED type preferences for the new candidate types defined in
		// this document (see Section 5) are 105 for NAT-assisted candidates and
		// 75 for UDP-tunneled candidates.
		//
		// (...)
		//
		// With TCP candidates, the local preference part of the recommended
		// priority formula is updated to also include the directionality
		// (active, passive, or simultaneous-open) of the TCP connection.  The
		// RECOMMENDED local preference is then defined as:
		//
		//     local preference = (2^13) * direction-pref + other-pref
		//
		// The direction-pref MUST be between 0 and 7 (both inclusive), with 7
		// being the most preferred.  The other-pref MUST be between 0 and 8191
		// (both inclusive), with 8191 being the most preferred.  It is
		// RECOMMENDED that the host, UDP-tunneled, and relayed TCP candidates
		// have the direction-pref assigned as follows: 6 for active, 4 for
		// passive, and 2 for S-O.  For the NAT-assisted and server reflexive
		// candidates, the RECOMMENDED values are: 6 for S-O, 4 for active, and
		// 2 for passive.
		//
		// (...)
		//
		// If any two candidates have the same type-preference and direction-
		// pref, they MUST have a unique other-pref.  With this specification,
		// this usually only happens with multi-homed hosts, in which case
		// other-pref is the preference for the particular IP address from which
		// the candidate was obtained.  When there is only a single IP address,
		// this value SHOULD be set to the maximum allowed value (8191).
		var otherPref uint16 = 8191

		directionPref := func() uint16 {
			switch c.Type() {
			case CandidateTypeHost, CandidateTypeRelay:
				switch c.tcpType {
				case TCPTypeActive:
					return 6
				case TCPTypePassive:
					return 4
				case TCPTypeSimultaneousOpen:
					return 2
				case TCPTypeUnspecified:
					return 0
				}
			case CandidateTypePeerReflexive, CandidateTypeServerReflexive:
				switch c.tcpType {
				case TCPTypeSimultaneousOpen:
					return 6
				case TCPTypeActive:
					return 4
				case TCPTypePassive:
					return 2
				case TCPTypeUnspecified:
					return 0
				}
			case CandidateTypeUnspecified:
				return 0
			}
			return 0
		}()

		return (1<<13)*directionPref + otherPref
	}

	return defaultLocalPreference
}

// RelatedAddress returns *CandidateRelatedAddress
func (c *candidateBase) RelatedAddress() *CandidateRelatedAddress {
	return c.relatedAddress
}

func (c *candidateBase) TCPType() TCPType {
	return c.tcpType
}

// start runs the candidate using the provided connection
func (c *candidateBase) start(a *Agent, conn net.PacketConn, initializedCh <-chan struct{}) {
	if c.conn != nil {
		c.agent().log.Warn("Can't start already started candidateBase")
		return
	}
	c.currAgent = a
	c.conn = conn
	c.closeCh = make(chan struct{})
	c.closedCh = make(chan struct{})

	go c.recvLoop(initializedCh)
}

func (c *candidateBase) recvLoop(initializedCh <-chan struct{}) {
	defer func() {
		close(c.closedCh)
	}()

	select {
	case <-initializedCh:
	case <-c.closeCh:
		return
	}

	log := c.agent().log
	buffer := make([]byte, receiveMTU)
	for {
		n, srcAddr, err := c.conn.ReadFrom(buffer)
		if err != nil {
			return
		}

		handleInboundCandidateMsg(c, c, buffer[:n], srcAddr, log)
	}
}

func handleInboundCandidateMsg(ctx context.Context, c Candidate, buffer []byte, srcAddr net.Addr, log logging.LeveledLogger) {
	if stun.IsMessage(buffer) {
		m := &stun.Message{
			Raw: make([]byte, len(buffer)),
		}
		// Explicitly copy raw buffer so Message can own the memory.
		copy(m.Raw, buffer)
		if err := m.Decode(); err != nil {
			log.Warnf("Failed to handle decode ICE from %s to %s: %v", c.addr(), srcAddr, err)
			return
		}
		err := c.agent().run(ctx, func(ctx context.Context, agent *Agent) {
			agent.handleInbound(m, c, srcAddr)
		})
		if err != nil {
			log.Warnf("Failed to handle message: %v", err)
		}

		return
	}

	if !c.agent().validateNonSTUNTraffic(c, srcAddr) {
		log.Warnf("Discarded message from %s, not a valid remote candidate", c.addr())
		return
	}

	// NOTE This will return packetio.ErrFull if the buffer ever manages to fill up.
	if _, err := c.agent().buffer.Write(buffer); err != nil {
		log.Warnf("failed to write packet")
	}
}

// close stops the recvLoop
func (c *candidateBase) close() error {
	// If conn has never been started will be nil
	if c.Done() == nil {
		return nil
	}

	// Assert that conn has not already been closed
	select {
	case <-c.Done():
		return nil
	default:
	}

	var firstErr error

	// Unblock recvLoop
	close(c.closeCh)
	if err := c.conn.SetDeadline(time.Now()); err != nil {
		firstErr = err
	}

	// Close the conn
	if err := c.conn.Close(); err != nil && firstErr == nil {
		firstErr = err
	}

	if firstErr != nil {
		return firstErr
	}

	// Wait until the recvLoop is closed
	<-c.closedCh

	return nil
}

func (c *candidateBase) writeTo(raw []byte, dst Candidate) (int, error) {
	n, err := c.conn.WriteTo(raw, dst.addr())
	if err != nil {
		c.agent().log.Warnf("%s: %v", errSendPacket, err)
		return n, nil
	}
	c.seen(true)
	return n, nil
}

// Priority computes the priority for this ICE Candidate
func (c *candidateBase) Priority() uint32 {
	if c.priorityOverride != 0 {
		return c.priorityOverride
	}

	// The local preference MUST be an integer from 0 (lowest preference) to
	// 65535 (highest preference) inclusive.  When there is only a single IP
	// address, this value SHOULD be set to 65535.  If there are multiple
	// candidates for a particular component for a particular data stream
	// that have the same type, the local preference MUST be unique for each
	// one.
	return (1<<24)*uint32(c.Type().Preference()) +
		(1<<8)*uint32(c.LocalPreference()) +
		uint32(256-c.Component())
}

// Equal is used to compare two candidateBases
func (c *candidateBase) Equal(other Candidate) bool {
	return c.NetworkType() == other.NetworkType() &&
		c.Type() == other.Type() &&
		c.Address() == other.Address() &&
		c.Port() == other.Port() &&
		c.TCPType() == other.TCPType() &&
		c.RelatedAddress().Equal(other.RelatedAddress())
}

// String makes the candidateBase printable
func (c *candidateBase) String() string {
	return fmt.Sprintf("%s %s %s:%d%s", c.NetworkType(), c.Type(), c.Address(), c.Port(), c.relatedAddress)
}

// LastReceived returns a time.Time indicating the last time
// this candidate was received
func (c *candidateBase) LastReceived() time.Time {
	lastReceived := c.lastReceived.Load()
	if lastReceived == nil {
		return time.Time{}
	}
	return lastReceived.(time.Time)
}

func (c *candidateBase) setLastReceived(t time.Time) {
	c.lastReceived.Store(t)
}

// LastSent returns a time.Time indicating the last time
// this candidate was sent
func (c *candidateBase) LastSent() time.Time {
	lastSent := c.lastSent.Load()
	if lastSent == nil {
		return time.Time{}
	}
	return lastSent.(time.Time)
}

func (c *candidateBase) setLastSent(t time.Time) {
	c.lastSent.Store(t)
}

func (c *candidateBase) seen(outbound bool) {
	if outbound {
		c.setLastSent(time.Now())
	} else {
		c.setLastReceived(time.Now())
	}
}

func (c *candidateBase) addr() net.Addr {
	return c.resolvedAddr
}

func (c *candidateBase) agent() *Agent {
	return c.currAgent
}

func (c *candidateBase) context() context.Context {
	return c
}

// Marshal returns the string representation of the ICECandidate
func (c *candidateBase) Marshal() string {
	val := fmt.Sprintf("%s %d %s %d %s %d typ %s",
		c.Foundation(),
		c.Component(),
		c.NetworkType().NetworkShort(),
		c.Priority(),
		c.Address(),
		c.Port(),
		c.Type())

	if c.tcpType != TCPTypeUnspecified {
		val += fmt.Sprintf(" tcptype %s", c.tcpType.String())
	}

	if c.RelatedAddress() != nil {
		val = fmt.Sprintf("%s raddr %s rport %d",
			val,
			c.RelatedAddress().Address,
			c.RelatedAddress().Port)
	}

	return val
}

// UnmarshalCandidate creates a Candidate from its string representation
func UnmarshalCandidate(raw string) (Candidate, error) {
	split := strings.Fields(raw)
	if len(split) < 8 {
		return nil, fmt.Errorf("%w (%d)", errAttributeTooShortICECandidate, len(split))
	}

	// Foundation
	foundation := split[0]

	// Component
	rawComponent, err := strconv.ParseUint(split[1], 10, 16)
	if err != nil {
		return nil, fmt.Errorf("%w: %v", errParseComponent, err)
	}
	component := uint16(rawComponent)

	// Protocol
	protocol := split[2]

	// Priority
	priorityRaw, err := strconv.ParseUint(split[3], 10, 32)
	if err != nil {
		return nil, fmt.Errorf("%w: %v", errParsePriority, err)
	}
	priority := uint32(priorityRaw)

	// Address
	address := split[4]

	// Port
	rawPort, err := strconv.ParseUint(split[5], 10, 16)
	if err != nil {
		return nil, fmt.Errorf("%w: %v", errParsePort, err)
	}
	port := int(rawPort)
	typ := split[7]

	relatedAddress := ""
	relatedPort := 0
	tcpType := TCPTypeUnspecified

	if len(split) > 8 {
		split = split[8:]

		if split[0] == "raddr" {
			if len(split) < 4 {
				return nil, fmt.Errorf("%w: incorrect length", errParseRelatedAddr)
			}

			// RelatedAddress
			relatedAddress = split[1]

			// RelatedPort
			rawRelatedPort, parseErr := strconv.ParseUint(split[3], 10, 16)
			if parseErr != nil {
				return nil, fmt.Errorf("%w: %v", errParsePort, parseErr)
			}
			relatedPort = int(rawRelatedPort)
		} else if split[0] == "tcptype" {
			if len(split) < 2 {
				return nil, fmt.Errorf("%w: incorrect length", errParseTypType)
			}

			tcpType = NewTCPType(split[1])
		}
	}

	switch typ {
	case "host":
		return NewCandidateHost(&CandidateHostConfig{"", protocol, address, port, component, priority, foundation, tcpType})
	case "srflx":
		return NewCandidateServerReflexive(&CandidateServerReflexiveConfig{"", protocol, address, port, component, priority, foundation, relatedAddress, relatedPort})
	case "prflx":
		return NewCandidatePeerReflexive(&CandidatePeerReflexiveConfig{"", protocol, address, port, component, priority, foundation, relatedAddress, relatedPort})
	case "relay":
		return NewCandidateRelay(&CandidateRelayConfig{"", protocol, address, port, component, priority, foundation, relatedAddress, relatedPort, nil})
	default:
	}

	return nil, fmt.Errorf("%w (%s)", errUnknownCandidateTyp, typ)
}
