// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"context"
	"errors"
	"fmt"
	"hash/crc32"
	"io"
	"net"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/pion/stun/v3"
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

	remoteCandidateCaches map[AddrPort]Candidate
	isLocationTracked     bool
	extensions            []CandidateExtension
}

// Done implements context.Context.
func (c *candidateBase) Done() <-chan struct{} {
	return c.closeCh
}

// Err implements context.Context.
func (c *candidateBase) Err() error {
	select {
	case <-c.closedCh:
		return ErrRunCanceled
	default:
		return nil
	}
}

// Deadline implements context.Context.
func (c *candidateBase) Deadline() (deadline time.Time, ok bool) {
	return time.Time{}, false
}

// Value implements context.Context.
func (c *candidateBase) Value(interface{}) interface{} {
	return nil
}

// ID returns Candidate ID.
func (c *candidateBase) ID() string {
	return c.id
}

func (c *candidateBase) Foundation() string {
	if c.foundationOverride != "" {
		return c.foundationOverride
	}

	return fmt.Sprintf("%d", crc32.ChecksumIEEE([]byte(c.Type().String()+c.address+c.networkType.String())))
}

// Address returns Candidate Address.
func (c *candidateBase) Address() string {
	return c.address
}

// Port returns Candidate Port.
func (c *candidateBase) Port() int {
	return c.port
}

// Type returns candidate type.
func (c *candidateBase) Type() CandidateType {
	return c.candidateType
}

// NetworkType returns candidate NetworkType.
func (c *candidateBase) NetworkType() NetworkType {
	return c.networkType
}

// Component returns candidate component.
func (c *candidateBase) Component() uint16 {
	return c.component
}

func (c *candidateBase) SetComponent(component uint16) {
	c.component = component
}

// LocalPreference returns the local preference for this candidate.
func (c *candidateBase) LocalPreference() uint16 { //nolint:cyclop
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

// RelatedAddress returns *CandidateRelatedAddress.
func (c *candidateBase) RelatedAddress() *CandidateRelatedAddress {
	return c.relatedAddress
}

func (c *candidateBase) TCPType() TCPType {
	return c.tcpType
}

// start runs the candidate using the provided connection.
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

var bufferPool = sync.Pool{ // nolint:gochecknoglobals
	New: func() interface{} {
		return make([]byte, receiveMTU)
	},
}

func (c *candidateBase) recvLoop(initializedCh <-chan struct{}) {
	agent := c.agent()

	defer close(c.closedCh)

	select {
	case <-initializedCh:
	case <-c.closeCh:
		return
	}

	bufferPoolBuffer := bufferPool.Get()
	defer bufferPool.Put(bufferPoolBuffer)
	buf, ok := bufferPoolBuffer.([]byte)
	if !ok {
		return
	}

	for {
		n, srcAddr, err := c.conn.ReadFrom(buf)
		if err != nil {
			if !(errors.Is(err, io.EOF) || errors.Is(err, net.ErrClosed)) {
				agent.log.Warnf("Failed to read from candidate %s: %v", c, err)
			}

			return
		}

		c.handleInboundPacket(buf[:n], srcAddr)
	}
}

func (c *candidateBase) validateSTUNTrafficCache(addr net.Addr) bool {
	if candidate, ok := c.remoteCandidateCaches[toAddrPort(addr)]; ok {
		candidate.seen(false)

		return true
	}

	return false
}

func (c *candidateBase) addRemoteCandidateCache(candidate Candidate, srcAddr net.Addr) {
	if c.validateSTUNTrafficCache(srcAddr) {
		return
	}
	c.remoteCandidateCaches[toAddrPort(srcAddr)] = candidate
}

func (c *candidateBase) handleInboundPacket(buf []byte, srcAddr net.Addr) {
	agent := c.agent()

	if stun.IsMessage(buf) {
		msg := &stun.Message{
			Raw: make([]byte, len(buf)),
		}

		// Explicitly copy raw buffer so Message can own the memory.
		copy(msg.Raw, buf)

		if err := msg.Decode(); err != nil {
			agent.log.Warnf("Failed to handle decode ICE from %s to %s: %v", c.addr(), srcAddr, err)

			return
		}

		if err := agent.loop.Run(c, func(_ context.Context) {
			// nolint: contextcheck
			agent.handleInbound(msg, c, srcAddr)
		}); err != nil {
			agent.log.Warnf("Failed to handle message: %v", err)
		}

		return
	}

	if !c.validateSTUNTrafficCache(srcAddr) {
		remoteCandidate, valid := agent.validateNonSTUNTraffic(c, srcAddr) //nolint:contextcheck
		if !valid {
			agent.log.Warnf("Discarded message from %s, not a valid remote candidate", c.addr())

			return
		}
		c.addRemoteCandidateCache(remoteCandidate, srcAddr)
	}

	// Note: This will return packetio.ErrFull if the buffer ever manages to fill up.
	if _, err := agent.buf.Write(buf); err != nil {
		agent.log.Warnf("Failed to write packet: %s", err)

		return
	}
}

// close stops the recvLoop.
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
		// If the connection is closed, we should return the error
		if errors.Is(err, io.ErrClosedPipe) {
			return n, err
		}
		c.agent().log.Infof("Failed to send packet: %v", err)

		return n, nil
	}
	c.seen(true)

	return n, nil
}

// TypePreference returns the type preference for this candidate.
func (c *candidateBase) TypePreference() uint16 {
	pref := c.Type().Preference()
	if pref == 0 {
		return 0
	}

	if c.NetworkType().IsTCP() {
		var tcpPriorityOffset uint16 = defaultTCPPriorityOffset
		if c.agent() != nil {
			tcpPriorityOffset = c.agent().tcpPriorityOffset
		}

		pref -= tcpPriorityOffset
	}

	return pref
}

// Priority computes the priority for this ICE Candidate
// See: https://www.rfc-editor.org/rfc/rfc8445#section-5.1.2.1
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

	return (1<<24)*uint32(c.TypePreference()) +
		(1<<8)*uint32(c.LocalPreference()) +
		(1<<0)*uint32(256-c.Component())
}

// Equal is used to compare two candidateBases.
func (c *candidateBase) Equal(other Candidate) bool {
	if c.addr() != other.addr() {
		if c.addr() == nil || other.addr() == nil {
			return false
		}
		if !addrEqual(c.addr(), other.addr()) {
			return false
		}
	}

	return c.NetworkType() == other.NetworkType() &&
		c.Type() == other.Type() &&
		c.Address() == other.Address() &&
		c.Port() == other.Port() &&
		c.TCPType() == other.TCPType() &&
		c.RelatedAddress().Equal(other.RelatedAddress())
}

// DeepEqual is same as Equal but also compares the extensions.
func (c *candidateBase) DeepEqual(other Candidate) bool {
	return c.Equal(other) && c.extensionsEqual(other.Extensions())
}

// String makes the candidateBase printable.
func (c *candidateBase) String() string {
	return fmt.Sprintf(
		"%s %s %s%s (resolved: %v)",
		c.NetworkType(),
		c.Type(),
		net.JoinHostPort(c.Address(), strconv.Itoa(c.Port())),
		c.relatedAddress,
		c.resolvedAddr,
	)
}

// LastReceived returns a time.Time indicating the last time
// this candidate was received.
func (c *candidateBase) LastReceived() time.Time {
	if lastReceived, ok := c.lastReceived.Load().(time.Time); ok {
		return lastReceived
	}

	return time.Time{}
}

func (c *candidateBase) setLastReceived(t time.Time) {
	c.lastReceived.Store(t)
}

// LastSent returns a time.Time indicating the last time
// this candidate was sent.
func (c *candidateBase) LastSent() time.Time {
	if lastSent, ok := c.lastSent.Load().(time.Time); ok {
		return lastSent
	}

	return time.Time{}
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

func (c *candidateBase) filterForLocationTracking() bool {
	return c.isLocationTracked
}

func (c *candidateBase) agent() *Agent {
	return c.currAgent
}

func (c *candidateBase) context() context.Context {
	return c
}

func (c *candidateBase) copy() (Candidate, error) {
	return UnmarshalCandidate(c.Marshal())
}

func removeZoneIDFromAddress(addr string) string {
	if i := strings.Index(addr, "%"); i != -1 {
		return addr[:i]
	}

	return addr
}

// Marshal returns the string representation of the ICECandidate.
func (c *candidateBase) Marshal() string {
	val := c.Foundation()
	if val == " " {
		val = ""
	}

	val = fmt.Sprintf("%s %d %s %d %s %d typ %s",
		val,
		c.Component(),
		c.NetworkType().NetworkShort(),
		c.Priority(),
		removeZoneIDFromAddress(c.Address()),
		c.Port(),
		c.Type())

	if r := c.RelatedAddress(); r != nil && r.Address != "" && r.Port != 0 {
		val = fmt.Sprintf("%s raddr %s rport %d",
			val,
			r.Address,
			r.Port)
	}

	extensions := c.marshalExtensions()

	if extensions != "" {
		val = fmt.Sprintf("%s %s", val, extensions)
	}

	return val
}

// CandidateExtension represents a single candidate extension
// as defined in https://tools.ietf.org/html/rfc5245#section-15.1
// .
type CandidateExtension struct {
	Key   string
	Value string
}

func (c *candidateBase) Extensions() []CandidateExtension {
	tcpType := c.TCPType()
	hasTCPType := 0
	if tcpType != TCPTypeUnspecified {
		hasTCPType = 1
	}

	extensions := make([]CandidateExtension, len(c.extensions)+hasTCPType)
	// We store the TCPType in c.tcpType, but we need to return it as an extension.
	if hasTCPType == 1 {
		extensions[0] = CandidateExtension{
			Key:   "tcptype",
			Value: tcpType.String(),
		}
	}

	copy(extensions[hasTCPType:], c.extensions)

	return extensions
}

// Get returns the value of the given key if it exists.
func (c *candidateBase) GetExtension(key string) (CandidateExtension, bool) {
	extension := CandidateExtension{Key: key}

	for i := range c.extensions {
		if c.extensions[i].Key == key {
			extension.Value = c.extensions[i].Value

			return extension, true
		}
	}

	// TCPType was manually set.
	if key == "tcptype" && c.TCPType() != TCPTypeUnspecified { //nolint:goconst
		extension.Value = c.TCPType().String()

		return extension, true
	}

	return extension, false
}

func (c *candidateBase) AddExtension(ext CandidateExtension) error {
	if ext.Key == "tcptype" {
		tcpType := NewTCPType(ext.Value)
		if tcpType == TCPTypeUnspecified {
			return fmt.Errorf("%w: invalid or unsupported TCPtype %s", errParseTCPType, ext.Value)
		}

		c.tcpType = tcpType

		return nil
	}

	if ext.Key == "" {
		return fmt.Errorf("%w: key is empty", errParseExtension)
	}

	// per spec, Extensions aren't explicitly unique, we only set the first one.
	// If the exteion is set multiple times.
	for i := range c.extensions {
		if c.extensions[i].Key == ext.Key {
			c.extensions[i] = ext

			return nil
		}
	}

	c.extensions = append(c.extensions, ext)

	return nil
}

func (c *candidateBase) RemoveExtension(key string) (ok bool) {
	if key == "tcptype" {
		c.tcpType = TCPTypeUnspecified
		ok = true
	}

	for i := range c.extensions {
		if c.extensions[i].Key == key {
			c.extensions = append(c.extensions[:i], c.extensions[i+1:]...)
			ok = true

			break
		}
	}

	return ok
}

// marshalExtensions returns the string representation of the candidate extensions.
func (c *candidateBase) marshalExtensions() string {
	value := ""
	exts := c.Extensions()

	for i := range exts {
		if value != "" {
			value += " "
		}

		value += exts[i].Key + " " + exts[i].Value
	}

	return value
}

// Equal returns true if the candidate extensions are equal.
func (c *candidateBase) extensionsEqual(other []CandidateExtension) bool {
	freq1 := make(map[CandidateExtension]int)
	freq2 := make(map[CandidateExtension]int)

	if len(c.extensions) != len(other) {
		return false
	}

	if len(c.extensions) == 0 {
		return true
	}

	if len(c.extensions) == 1 {
		return c.extensions[0] == other[0]
	}

	for i := range c.extensions {
		freq1[c.extensions[i]]++
		freq2[other[i]]++
	}

	for k, v := range freq1 {
		if freq2[k] != v {
			return false
		}
	}

	return true
}

func (c *candidateBase) setExtensions(extensions []CandidateExtension) {
	c.extensions = extensions
}

// UnmarshalCandidate Parses a candidate from a string
// https://datatracker.ietf.org/doc/html/rfc5245#section-15.1
func UnmarshalCandidate(raw string) (Candidate, error) { //nolint:cyclop
	// Handle candidates with the "candidate:" prefix as defined in RFC 5245 section 15.1.
	raw = strings.TrimPrefix(raw, "candidate:")

	pos := 0
	// foundation ( 1*32ice-char ) But we allow for empty foundation,
	foundation, pos, err := readCandidateCharToken(raw, pos, 32)
	if err != nil {
		return nil, fmt.Errorf("%w: %v in %s", errParseFoundation, err, raw) //nolint:errorlint // we wrap the error
	}

	// Empty foundation, not RFC 8445 compliant but seen in the wild
	if foundation == "" {
		foundation = " "
	}

	if pos >= len(raw) {
		return nil, fmt.Errorf("%w: expected component in %s", errAttributeTooShortICECandidate, raw)
	}

	// component-id ( 1*5DIGIT )
	component, pos, err := readCandidateDigitToken(raw, pos, 5)
	if err != nil {
		return nil, fmt.Errorf("%w: %v in %s", errParseComponent, err, raw) //nolint:errorlint // we wrap the error
	}

	if pos >= len(raw) {
		return nil, fmt.Errorf("%w: expected transport in %s", errAttributeTooShortICECandidate, raw)
	}

	// transport ( "UDP" / transport-extension ; from RFC 3261 ) SP
	protocol, pos := readCandidateStringToken(raw, pos)

	if pos >= len(raw) {
		return nil, fmt.Errorf("%w: expected priority in %s", errAttributeTooShortICECandidate, raw)
	}

	// priority ( 1*10DIGIT ) SP
	priority, pos, err := readCandidateDigitToken(raw, pos, 10)
	if err != nil {
		return nil, fmt.Errorf("%w: %v in %s", errParsePriority, err, raw) //nolint:errorlint // we wrap the error
	}

	if pos >= len(raw) {
		return nil, fmt.Errorf("%w: expected address in %s", errAttributeTooShortICECandidate, raw)
	}

	// connection-address SP     ;from RFC 4566
	address, pos := readCandidateStringToken(raw, pos)

	// Remove IPv6 ZoneID: https://github.com/pion/ice/pull/704
	address = removeZoneIDFromAddress(address)

	if pos >= len(raw) {
		return nil, fmt.Errorf("%w: expected port in %s", errAttributeTooShortICECandidate, raw)
	}

	// port from RFC 4566
	port, pos, err := readCandidatePort(raw, pos)
	if err != nil {
		return nil, fmt.Errorf("%w: %v in %s", errParsePort, err, raw) //nolint:errorlint // we wrap the error
	}

	// "typ" SP
	typeKey, pos := readCandidateStringToken(raw, pos)
	if typeKey != "typ" {
		return nil, fmt.Errorf("%w (%s)", ErrUnknownCandidateTyp, typeKey)
	}

	if pos >= len(raw) {
		return nil, fmt.Errorf("%w: expected candidate type in %s", errAttributeTooShortICECandidate, raw)
	}

	// SP cand-type ("host" / "srflx" / "prflx" / "relay")
	typ, pos := readCandidateStringToken(raw, pos)

	raddr, rport, pos, err := tryReadRelativeAddrs(raw, pos)
	if err != nil {
		return nil, err
	}

	tcpType := TCPTypeUnspecified
	var extensions []CandidateExtension
	var tcpTypeRaw string

	if pos < len(raw) {
		extensions, tcpTypeRaw, err = unmarshalCandidateExtensions(raw[pos:])
		if err != nil {
			return nil, fmt.Errorf("%w: %v", errParseExtension, err) //nolint:errorlint // we wrap the error
		}

		if tcpTypeRaw != "" {
			tcpType = NewTCPType(tcpTypeRaw)
			if tcpType == TCPTypeUnspecified {
				return nil, fmt.Errorf("%w: invalid or unsupported TCPtype %s", errParseTCPType, tcpTypeRaw)
			}
		}
	}

	// this code is ugly because we can't break backwards compatibility
	// with the old way of parsing candidates
	switch typ {
	case "host":
		candidate, err := NewCandidateHost(&CandidateHostConfig{
			"",
			protocol,
			address,
			port,
			uint16(component), //nolint:gosec // G115 no overflow we read 5 digits
			uint32(priority),  //nolint:gosec // G115 no overflow we read 5 digits
			foundation,
			tcpType,
			false,
		})
		if err != nil {
			return nil, err
		}

		candidate.setExtensions(extensions)

		return candidate, nil
	case "srflx":
		candidate, err := NewCandidateServerReflexive(&CandidateServerReflexiveConfig{
			"",
			protocol,
			address,
			port,
			uint16(component), //nolint:gosec // G115 no overflow we read 5 digits
			uint32(priority),  //nolint:gosec // G115 no overflow we read 5 digits
			foundation,
			raddr,
			rport,
		})
		if err != nil {
			return nil, err
		}

		candidate.setExtensions(extensions)

		return candidate, nil
	case "prflx":
		candidate, err := NewCandidatePeerReflexive(&CandidatePeerReflexiveConfig{
			"",
			protocol,
			address,
			port,
			uint16(component), //nolint:gosec // G115 no overflow we read 5 digits
			uint32(priority),  //nolint:gosec // G115 no overflow we read 5 digits
			foundation,
			raddr,
			rport,
		})
		if err != nil {
			return nil, err
		}

		candidate.setExtensions(extensions)

		return candidate, nil
	case "relay":
		candidate, err := NewCandidateRelay(&CandidateRelayConfig{
			"",
			protocol,
			address,
			port,
			uint16(component), //nolint:gosec // G115 no overflow we read 5 digits
			uint32(priority),  //nolint:gosec // G115 no overflow we read 5 digits
			foundation,
			raddr,
			rport,
			"",
			nil,
		})
		if err != nil {
			return nil, err
		}

		candidate.setExtensions(extensions)

		return candidate, nil
	default:
		return nil, fmt.Errorf("%w (%s)", ErrUnknownCandidateTyp, typ)
	}
}

// Read an ice-char token from the raw string
// ice-char = ALPHA / DIGIT / "+" / "/"
// stop reading when a space is encountered or the end of the string.
func readCandidateCharToken(raw string, start int, limit int) (string, int, error) { //nolint:cyclop
	for i, char := range raw[start:] {
		if char == 0x20 { // SP
			return raw[start : start+i], start + i + 1, nil
		}

		if i == limit {
			//nolint: err113 // handled by caller
			return "", 0, fmt.Errorf("token too long: %s expected 1x%d", raw[start:start+i], limit)
		}

		if !(char >= 'A' && char <= 'Z' ||
			char >= 'a' && char <= 'z' ||
			char >= '0' && char <= '9' ||
			char == '+' || char == '/') {
			return "", 0, fmt.Errorf("invalid ice-char token: %c", char) //nolint: err113 // handled by caller
		}
	}

	return raw[start:], len(raw), nil
}

// Read an ice string token from the raw string until a space is encountered
// Or the end of the string, we imply that ice string are UTF-8 encoded.
func readCandidateStringToken(raw string, start int) (string, int) {
	for i, char := range raw[start:] {
		if char == 0x20 { // SP
			return raw[start : start+i], start + i + 1
		}
	}

	return raw[start:], len(raw)
}

// Read a digit token from the raw string
// stop reading when a space is encountered or the end of the string.
func readCandidateDigitToken(raw string, start, limit int) (int, int, error) {
	var val int
	for i, char := range raw[start:] {
		if char == 0x20 { // SP
			return val, start + i + 1, nil
		}

		if i == limit {
			//nolint: err113 // handled by caller
			return 0, 0, fmt.Errorf("token too long: %s expected 1x%d", raw[start:start+i], limit)
		}

		if !(char >= '0' && char <= '9') {
			return 0, 0, fmt.Errorf("invalid digit token: %c", char) //nolint: err113 // handled by caller
		}

		val = val*10 + int(char-'0')
	}

	return val, len(raw), nil
}

// Read and validate RFC 4566 port from the raw string.
func readCandidatePort(raw string, start int) (int, int, error) {
	port, pos, err := readCandidateDigitToken(raw, start, 5)
	if err != nil {
		return 0, 0, err
	}

	if port > 65535 {
		return 0, 0, fmt.Errorf("invalid RFC 4566 port %d", port) //nolint: err113 // handled by caller
	}

	return port, pos, nil
}

// Read a byte-string token from the raw string
// As defined in RFC 4566  1*(%x01-09/%x0B-0C/%x0E-FF) ;any byte except NUL, CR, or LF
// we imply that extensions byte-string are UTF-8 encoded.
func readCandidateByteString(raw string, start int) (string, int, error) {
	for i, char := range raw[start:] {
		if char == 0x20 { // SP
			return raw[start : start+i], start + i + 1, nil
		}

		// 1*(%x01-09/%x0B-0C/%x0E-FF)
		if !(char >= 0x01 && char <= 0x09 ||
			char >= 0x0B && char <= 0x0C ||
			char >= 0x0E && char <= 0xFF) {
			return "", 0, fmt.Errorf("invalid byte-string character: %c", char) //nolint: err113 // handled by caller
		}
	}

	return raw[start:], len(raw), nil
}

// Read and validate raddr and rport from the raw string
// [SP rel-addr] [SP rel-port]
// defined in https://datatracker.ietf.org/doc/html/rfc5245#section-15.1
// .
func tryReadRelativeAddrs(raw string, start int) (raddr string, rport, pos int, err error) {
	key, pos := readCandidateStringToken(raw, start)

	if key != "raddr" {
		return "", 0, start, nil
	}

	if pos >= len(raw) {
		return "", 0, 0, fmt.Errorf("%w: expected raddr value in %s", errParseRelatedAddr, raw)
	}

	raddr, pos = readCandidateStringToken(raw, pos)

	if pos >= len(raw) {
		return "", 0, 0, fmt.Errorf("%w: expected rport in %s", errParseRelatedAddr, raw)
	}

	key, pos = readCandidateStringToken(raw, pos)
	if key != "rport" {
		return "", 0, 0, fmt.Errorf("%w: expected rport in %s", errParseRelatedAddr, raw)
	}

	if pos >= len(raw) {
		return "", 0, 0, fmt.Errorf("%w: expected rport value in %s", errParseRelatedAddr, raw)
	}

	rport, pos, err = readCandidatePort(raw, pos)
	if err != nil {
		return "", 0, 0, fmt.Errorf("%w: %v", errParseRelatedAddr, err) //nolint:errorlint // we wrap the error
	}

	return raddr, rport, pos, nil
}

// UnmarshalCandidateExtensions parses the candidate extensions from the raw string.
// *(SP extension-att-name SP extension-att-value)
// Where extension-att-name, and extension-att-value are byte-strings
// as defined in https://tools.ietf.org/html/rfc5245#section-15.1
func unmarshalCandidateExtensions(raw string) (extensions []CandidateExtension, rawTCPTypeRaw string, err error) {
	extensions = make([]CandidateExtension, 0)

	if raw == "" {
		return extensions, "", nil
	}

	if raw[0] == 0x20 { // SP
		return extensions, "", fmt.Errorf("%w: unexpected space %s", errParseExtension, raw)
	}

	for i := 0; i < len(raw); {
		key, next, err := readCandidateByteString(raw, i)
		if err != nil {
			return extensions, "", fmt.Errorf(
				"%w: failed to read key %v", errParseExtension, err, //nolint: errorlint // we wrap the error
			)
		}
		i = next

		// while not spec-compliant, we allow for empty values, as seen in the wild
		var value string
		if i < len(raw) {
			value, next, err = readCandidateByteString(raw, i)
			if err != nil {
				return extensions, "", fmt.Errorf(
					"%w: failed to read value %v", errParseExtension, err, //nolint: errorlint // we are wrapping the error
				)
			}
			i = next
		}

		if key == "tcptype" {
			rawTCPTypeRaw = value

			continue
		}

		extensions = append(extensions, CandidateExtension{key, value})
	}

	return extensions, rawTCPTypeRaw, nil
}
