// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"encoding/binary"
	"hash/crc32"
	"net"
	"strings"
	"sync"
	"time"

	"github.com/pion/stun/v4"
)

const (
	// spedMaxBody is the most STUN body, after the 20-byte header and
	// before MESSAGE-INTEGRITY and FINGERPRINT, that a message with
	// embedded DTLS may have, as Chrome's kMaxStunBindingLength says.
	spedMaxBody = 1200 - 24 - 8
	// spedMaxAcks is the most CRCs in a DTLS-IN-STUN-ACK.
	spedMaxAcks = 4
	// spedMTU is the DTLS MTU with SPED, as Chrome's field trial sets it,
	// so a datagram fits one STUN message.
	spedMTU = 900
	// spedFlightGap is how long after the last DTLS write a new one starts
	// a new flight, as a retransmission does, when nothing came in between.
	spedFlightGap = 100 * time.Millisecond
	// spedCheckInterval is how often the client sends a check while SPED
	// is not done, close to Chrome's 48ms.
	spedCheckInterval = 50 * time.Millisecond
)

// A SpedState is a state of SPED on one side, as Chrome names them.
type SpedState int

const (
	// SpedNone is a session without SPED: not offered, or not answered.
	SpedNone SpedState = iota
	// SpedTentative is before the peer sent a SPED attribute.
	SpedTentative
	// SpedConfirmed is after the peer sent one.
	SpedConfirmed
	// SpedPending is after the DTLS handshake, until the peer ACKs the last
	// flight.
	SpedPending
	// SpedComplete is after SPED is done.
	SpedComplete
	// SpedOff is when the peer sent neither attribute first: DTLS goes raw.
	SpedOff
)

func (s SpedState) String() string {
	switch s {
	case SpedTentative:
		return "tentative"
	case SpedConfirmed:
		return "confirmed"
	case SpedPending:
		return "pending"
	case SpedComplete:
		return "complete"
	case SpedOff:
		return "off"
	}
	return "none"
}

// SpedStats is the state of one side of SPED and what it counted while SPED
// was active: the DTLS datagrams it embedded in STUN messages, and those it
// received embedded and raw.
type SpedStats struct {
	State                               SpedState
	EmbeddedSent, EmbeddedRecv, RawRecv int
}

// A spedLink is one side of SPED, the client's or a server's: it keeps the
// current flight's DTLS datagrams not yet ACKed, which it embeds one per
// STUN message, in turn, and the CRCs of the DTLS datagrams it received,
// which it ACKs. It sends each DTLS datagram raw too, once the pair is
// writable. As a stun.Setter it adds its ACK and DATA to a message.
type spedLink struct {
	// raw sends a datagram to the peer.
	raw func([]byte) (int, error)
	// in is the embedded DTLS datagrams for DTLS to read.
	in chan []byte
	// captured is closed when the first DTLS datagram is written.
	captured chan struct{}

	mu          sync.Mutex
	st          SpedState
	writable    bool
	flight      [][]byte
	next        int
	acks        []uint32
	readSince   bool
	lastWrite   time.Time
	captureOnce sync.Once
	counts      SpedStats
}

// newSpedLink makes a side that sends raw with raw, at once when writable
// is set, as on a server, or once setWritable is called.
func newSpedLink(raw func([]byte) (int, error), writable bool) *spedLink {
	return &spedLink{raw: raw, in: make(chan []byte, 64), captured: make(chan struct{}), st: SpedTentative,
		writable: writable}
}

// active tells whether SPED still embeds and ACKs. The caller holds mu.
func (l *spedLink) active() bool {
	return l.st == SpedTentative || l.st == SpedConfirmed || l.st == SpedPending
}

// stop ends SPED in a state, and forgets the flight and the ACKs. The
// caller holds mu.
func (l *spedLink) stop(st SpedState) {
	l.st, l.flight, l.next, l.acks = st, nil, 0, nil
}

// write captures a DTLS datagram DTLS writes, into the current flight or a
// new one, and sends it raw unless the pair is not writable yet.
func (l *spedLink) write(b []byte) (int, error) {
	l.mu.Lock()
	if l.active() {
		now := time.Now()
		if l.readSince || now.Sub(l.lastWrite) > spedFlightGap {
			l.flight, l.next = nil, 0
		}
		l.flight = append(l.flight, append([]byte(nil), b...))
		l.readSince, l.lastWrite = false, now
		l.captureOnce.Do(func() { close(l.captured) })
	}
	hold := !l.writable
	l.mu.Unlock()
	if hold {
		return len(b), nil
	}
	return l.raw(b)
}

// setWritable marks the pair writable, after the first response, and sends
// the flight held until then raw.
func (l *spedLink) setWritable() {
	l.mu.Lock()
	if l.writable {
		l.mu.Unlock()
		return
	}
	l.writable = true
	held := append([][]byte(nil), l.flight...)
	l.mu.Unlock()
	for _, b := range held {
		l.raw(b)
	}
}

// received notes a DTLS datagram received, raw or embedded, to ACK it.
func (l *spedLink) received(b []byte, embedded bool) {
	l.mu.Lock()
	defer l.mu.Unlock()
	l.receivedLocked(b, embedded)
}

// receivedLocked is received with mu held.
func (l *spedLink) receivedLocked(b []byte, embedded bool) {
	if !l.active() {
		return
	}
	if embedded {
		l.counts.EmbeddedRecv++
	} else {
		l.counts.RawRecv++
	}
	l.readSince = true
	crc := crc32.ChecksumIEEE(b)
	for _, c := range l.acks {
		if c == crc {
			return
		}
	}
	l.acks = append(l.acks, crc)
	if len(l.acks) > spedMaxAcks {
		l.acks = l.acks[1:]
	}
}

// onMessage takes the SPED attributes of an authenticated STUN message
// from the peer: it feeds embedded DTLS to DTLS and ACKs it, removes the
// ACKed datagrams from the flight, and moves the state.
func (l *spedLink) onMessage(m *stun.Message) {
	data, errData := m.Get(stun.AttrDtlsInStun)
	ack, errAck := m.Get(stun.AttrDtlsInStunAck)
	hasData, hasAck := errData == nil, errAck == nil
	var feed []byte

	l.mu.Lock()
	switch {
	case !l.active():
	case !hasData && !hasAck && l.st == SpedPending:
		l.stop(SpedComplete)
	case !hasData && !hasAck:
		l.stop(SpedOff)
	default:
		if l.st == SpedTentative {
			l.st = SpedConfirmed
		}
		// RFC 7983: only DTLS, first byte 20 to 63, is fed.
		if hasData && len(data) > 0 && data[0] >= 20 && data[0] <= 63 {
			feed = append([]byte(nil), data...)
			l.receivedLocked(feed, true)
		}
		if hasAck && len(ack)%4 == 0 && len(ack)/4 <= spedMaxAcks {
			l.prune(ack)
		}
		if l.st == SpedPending && len(l.flight) == 0 {
			l.stop(SpedComplete)
		}
	}
	l.mu.Unlock()

	if feed != nil {
		select {
		case l.in <- feed:
		default:
		}
	}
}

// prune removes the datagrams of the flight whose CRC is in an ACK. The
// caller holds mu.
func (l *spedLink) prune(ack []byte) {
	kept := l.flight[:0]
	for _, b := range l.flight {
		crc, acked := crc32.ChecksumIEEE(b), false
		for i := 0; i < len(ack); i += 4 {
			if binary.BigEndian.Uint32(ack[i:]) == crc {
				acked = true
				break
			}
		}
		if !acked {
			kept = append(kept, b)
		}
	}
	l.flight = kept
	if l.next >= len(l.flight) {
		l.next = 0
	}
}

// AddTo adds, while SPED is active, the ACK, empty when there is nothing to
// ACK, and the next datagram of the flight when it fits.
func (l *spedLink) AddTo(m *stun.Message) error {
	l.mu.Lock()
	if !l.active() {
		l.mu.Unlock()
		return nil
	}
	ack := make([]byte, 4*len(l.acks))
	for i, crc := range l.acks {
		binary.BigEndian.PutUint32(ack[4*i:], crc)
	}
	var data []byte
	if len(l.flight) > 0 {
		b := l.flight[l.next]
		l.next = (l.next + 1) % len(l.flight)
		// The body so far, the ACK, and the DATA padded to 4 bytes.
		if int(m.Length)+4+len(ack)+4+(len(b)+3)/4*4 <= spedMaxBody {
			data = b
			l.counts.EmbeddedSent++
		}
	}
	l.mu.Unlock()

	m.Add(stun.AttrDtlsInStunAck, ack)
	if data != nil {
		m.Add(stun.AttrDtlsInStun, data)
	}
	return nil
}

// handshakeDone notes the DTLS handshake is done: SPED is pending until the
// peer ACKs the last flight, or complete when it already did.
func (l *spedLink) handshakeDone() {
	l.mu.Lock()
	defer l.mu.Unlock()
	if !l.active() {
		return
	}
	l.st = SpedPending
	if len(l.flight) == 0 {
		l.stop(SpedComplete)
	}
}

// mediaReceived notes an RTP or RTCP packet from the peer, which completes
// SPED once the handshake is done, as the peer has the keys.
func (l *spedLink) mediaReceived() {
	l.mu.Lock()
	defer l.mu.Unlock()
	if l.st == SpedPending {
		l.stop(SpedComplete)
	}
}

func (l *spedLink) state() SpedState {
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.st
}

func (l *spedLink) stats() SpedStats {
	l.mu.Lock()
	defer l.mu.Unlock()
	s := l.counts
	s.State = l.st
	return s
}

// SPED is the session's side of SPED, with the state SpedNone when the
// session doesn't use it.
func (s *RawSession) SPED() SpedStats {
	if s.link == nil {
		return SpedStats{}
	}
	return s.link.stats()
}

// spedChecks sends a check every spedCheckInterval with the link's ACK and
// DATA, from the first DTLS write, until SPED is off or complete, and takes
// the SPED attributes of each authenticated response. Once complete, it
// sends one more check without SPED attributes, which tells the server SPED
// is done, as Chrome's later checks do.
func (s *RawSession) spedChecks() {
	l := s.link
	select {
	case <-l.captured:
	case <-time.After(100 * time.Millisecond):
	case <-s.mux.done:
		return
	}
	integrity := stun.NewShortTermIntegrity(s.answer.pwd)
	sent := map[[stun.TransactionIDSize]byte]bool{}
	for {
		st := l.state()
		if st == SpedOff {
			return
		}
		var extra []stun.Setter
		if st != SpedComplete {
			extra = append(extra, l)
		}
		req, err := bindingRequest(s.answer.ufrag, s.offer.ufrag, s.answer.pwd, s.opts, extra...)
		if err != nil {
			return
		}
		sent[req.TransactionID] = true
		if _, err := s.conn.Write(req.Raw); err != nil || st == SpedComplete {
			return
		}
		timeout := time.NewTimer(spedCheckInterval)
	wait:
		for {
			select {
			case p := <-s.mux.stun:
				res := &stun.Message{Raw: p}
				if res.Decode() != nil || res.Type != stun.BindingSuccess || !sent[res.TransactionID] || integrity.Check(res) != nil {
					continue
				}
				// A duplicate response for a done transaction is ignored.
				delete(sent, res.TransactionID)
				l.setWritable()
				l.onMessage(res)
			case <-timeout.C:
				break wait
			case <-s.mux.done:
				timeout.Stop()
				return
			}
		}
	}
}

// A spedTap tells the link of each RTP or RTCP packet the session reads.
type spedTap struct {
	net.Conn
	l *spedLink
}

func (c *spedTap) Read(b []byte) (int, error) {
	n, err := c.Conn.Read(b)
	if n > 0 && b[0] >= 128 && b[0] <= 191 {
		c.l.mediaReceived()
	}
	return n, err
}

func (c *spedTap) unwrap() net.Conn { return c.Conn }

// spedPacketConn is packetConn with SPED: DTLS also reads the datagrams
// embedded in STUN, and every write goes through the link.
func (m *mux) spedPacketConn(l *spedLink) net.PacketConn {
	return &dtlsConn{m: m, link: l, closed: make(chan struct{}), changed: make(chan struct{})}
}

// HandshakeCertificate is the DTLS handshake type of Certificate, the
// first message of a DTLS 1.2 client's flight after the ServerHelloDone.
const HandshakeCertificate = 11

// EmbeddedHandshake returns the handshake type of the first record of the
// DTLS datagram a STUN message embeds, and false when b is not STUN, embeds
// nothing, or its first record is not a handshake.
func EmbeddedHandshake(b []byte) (byte, bool) {
	data, ok := EmbeddedDTLS(b)
	// A 13-byte DTLS 1.2 record header, then the handshake type, in a handshake record(22).
	if !ok || len(data) < 14 || data[0] != 22 {
		return 0, false
	}
	return data[13], true
}

// EmbeddedDTLS returns the DTLS datagram a STUN message embeds, and false
// when b is not STUN or embeds nothing.
func EmbeddedDTLS(b []byte) ([]byte, bool) {
	if !stun.IsMessage(b) {
		return nil, false
	}
	m := &stun.Message{Raw: append([]byte(nil), b...)}
	if m.Decode() != nil {
		return nil, false
	}
	data, err := m.Get(stun.AttrDtlsInStun)
	if err != nil {
		return nil, false
	}
	return data, true
}

// HasChangeCipherSpec tells whether a DTLS 1.2 datagram has a
// ChangeCipherSpec record, which only a side's last flight has.
func HasChangeCipherSpec(b []byte) bool {
	// Each record is a 13-byte header, its content type first and its body
	// length last, then the body, see RFC 6347 section 4.1.
	for len(b) >= 13 {
		if b[0] == 20 {
			return true
		}
		n := 13 + int(b[11])<<8 + int(b[12])
		if n > len(b) {
			return false
		}
		b = b[n:]
	}
	return false
}

// withIceOption returns the SDP with a token in its session-level
// a=ice-options, added if there is none, and in each media's that has one.
func withIceOption(sdp, token string) string {
	lines := strings.Split(sdp, "\r\n")
	out := make([]string, 0, len(lines)+1)
	session, added := true, false
	for _, line := range lines {
		if session && strings.HasPrefix(line, "m=") {
			session = false
			if !added {
				out, added = append(out, "a=ice-options:"+token), true
			}
		}
		if v, ok := strings.CutPrefix(line, "a=ice-options:"); ok {
			if !hasToken(v, token) {
				line += " " + token
			}
			added = added || session
		}
		out = append(out, line)
	}
	return strings.Join(out, "\r\n")
}

// hasToken tells whether a space-separated list has a token.
func hasToken(list, token string) bool {
	for _, t := range strings.Fields(list) {
		if t == token {
			return true
		}
	}
	return false
}
