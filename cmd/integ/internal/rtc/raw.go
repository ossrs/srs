// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"context"
	"crypto/rand"
	"crypto/tls"
	"encoding/binary"
	"errors"
	"fmt"
	"net"
	"net/netip"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/pion/dtls/v3"
	"github.com/pion/dtls/v3/pkg/crypto/selfsign"
	"github.com/pion/ice/v4"
	"github.com/pion/rtcp"
	"github.com/pion/rtp"
	"github.com/pion/stun/v4"

	"srs-integ/internal/rtmp"
)

// A RawSession is a WebRTC session without pion's peer connection, for SRS's
// encrypt=false and dtls=false, which a browser or pion can't be the client
// of, and for checks of ICE itself. It sends STUN binding requests to the
// server's UDP candidate, or its TCP one with Options.TCP, until one is
// answered, then does the DTLS handshake, or none, and sends and reads RTP
// and RTCP in clear.
type RawSession struct {
	Session
	// DTLS tells whether the session did a DTLS handshake.
	DTLS bool

	offer  *answerInfo
	answer *answerInfo
	conn   net.Conn
	mux    *mux
	dconn  *dtls.Conn
}

// Close closes the DTLS connection, which tells the server with a
// close_notify alert, and the socket. It doesn't send DELETE.
func (s *RawSession) Close() error {
	if s.dconn != nil {
		s.dconn.Close()
	}
	if s.mux != nil {
		s.mux.close()
	}
	return nil
}

// rawConnect posts the offer of a raw session, publishing or playing H.264
// and Opus, answers the server's ICE over UDP or TCP and, when withDTLS is
// set, does the DTLS handshake.
func rawConnect(ctx context.Context, url string, opts *Options, publish, withDTLS bool) (*RawSession, error) {
	offer, err := Offer(publish, []string{"H264"}, []string{"opus"})
	if err != nil {
		return nil, err
	}
	s := &RawSession{Session: Session{Offer: withSetup(offer, opts.setup()), opts: opts}}
	if err := s.connect(ctx, url, withDTLS); err != nil {
		s.Close()
		return nil, err
	}
	return s, nil
}

// connect posts the session's offer, binds and does the DTLS handshake.
func (s *RawSession) connect(ctx context.Context, url string, withDTLS bool) (err error) {
	if s.offer, err = parseAnswer(s.Offer); err != nil {
		return fmt.Errorf("offer: %w", err)
	}
	if err := s.post(ctx, url); err != nil {
		return err
	}
	if s.answer, err = parseAnswer(s.Answer); err != nil {
		return fmt.Errorf("answer: %w", err)
	}
	if err := s.dial(ctx); err != nil {
		return err
	}
	if _, err := s.bind(ctx); err != nil {
		return err
	}
	if !withDTLS {
		return nil
	}

	cert, err := selfsign.GenerateSelfSigned()
	if err != nil {
		return err
	}
	cfg := &dtls.Config{
		Certificates:           []tls.Certificate{cert},
		InsecureSkipVerify:     true,
		ClientAuth:             dtls.RequireAnyClientCert,
		SRTPProtectionProfiles: []dtls.SRTPProtectionProfile{dtls.SRTP_AES128_CM_HMAC_SHA1_80},
	}
	// The server is passive unless its answer says active.
	if strings.Contains(s.Answer, "a=setup:active") {
		s.dconn, err = dtls.Server(s.mux.packetConn(), s.conn.RemoteAddr(), cfg)
	} else {
		s.dconn, err = dtls.Client(s.mux.packetConn(), s.conn.RemoteAddr(), cfg)
	}
	if err != nil {
		return err
	}
	if err := s.dconn.HandshakeContext(ctx); err != nil {
		return fmt.Errorf("DTLS handshake: %w", err)
	}
	s.DTLS = true
	return nil
}

// bind sends a STUN binding request every 50ms until the server answers
// it, and returns the answer; ErrClosed when the connection closes first.
func (s *RawSession) bind(ctx context.Context) (*stun.Message, error) {
	controlled := s.opts != nil && s.opts.Controlled
	for {
		req, err := bindingRequest(s.answer.ufrag, s.offer.ufrag, s.answer.pwd, controlled)
		if err != nil {
			return nil, err
		}
		if _, err := s.conn.Write(req.Raw); err != nil {
			return nil, err
		}
		timeout := time.NewTimer(50 * time.Millisecond)
	wait:
		for {
			select {
			case p := <-s.mux.stun:
				res := &stun.Message{Raw: p}
				if res.Decode() == nil && res.Type == stun.BindingSuccess && res.TransactionID == req.TransactionID {
					timeout.Stop()
					return res, nil
				}
			case <-timeout.C:
				break wait
			case <-s.mux.done:
				timeout.Stop()
				return nil, fmt.Errorf("STUN binding: %w", ErrClosed)
			case <-ctx.Done():
				timeout.Stop()
				return nil, fmt.Errorf("STUN binding: %w", ctx.Err())
			}
		}
	}
}

// media returns the answer's media section of a kind, and the payload type
// of a codec in it.
func (s *RawSession) media(kind, codec string) (*answerMedia, uint8, error) {
	for i := range s.answer.medias {
		m := &s.answer.medias[i]
		if m.kind != kind {
			continue
		}
		for pt, c := range m.codecs {
			if c.name == codec {
				return m, pt, nil
			}
		}
	}
	return nil, 0, fmt.Errorf("the answer has no %v %v", kind, codec)
}

// A RawPublisher sends H.264 and Opus in clear RTP.
type RawPublisher struct {
	RawSession
	*pliCounter

	mu           sync.Mutex
	video, audio rawTrack
}

// A rawTrack is a track a raw publisher sends: its packets, SSRC and payload
// type.
type rawTrack struct {
	sender *sender
	ssrc   uint32
	pt     uint8
}

// RawPublish connects a raw publisher by WHIP to url, such as
// http://host:1985/rtc/v1/whip/?app=live&stream=s&encrypt=false, with a DTLS
// handshake when withDTLS is set, and returns once the server answered STUN
// and the handshake is done. It sends with the offer's SSRCs and the
// answer's payload types.
func RawPublish(ctx context.Context, url string, opts *Options, withDTLS bool) (*RawPublisher, error) {
	s, err := rawConnect(ctx, url, opts, true, withDTLS)
	if err != nil {
		return nil, err
	}
	p := &RawPublisher{RawSession: *s, pliCounter: newPLICounter()}
	for _, t := range []struct {
		kind, codec string
		track       *rawTrack
		sender      *sender
	}{{"video", "h264", &p.video, newVideoSender()}, {"audio", "opus", &p.audio, newAudioSender()}} {
		_, pt, err := p.media(t.kind, t.codec)
		if err != nil {
			p.Close()
			return nil, err
		}
		var ssrc uint32
		for _, m := range p.offer.medias {
			if m.kind == t.kind {
				ssrc = m.ssrc
			}
		}
		if ssrc == 0 {
			p.Close()
			return nil, fmt.Errorf("the offer has no %v SSRC", t.kind)
		}
		*t.track = rawTrack{sender: t.sender, ssrc: ssrc, pt: pt}
	}
	go p.readRTCP()
	return p, nil
}

// readRTCP counts the PLIs among the RTCP packets the server sends.
func (p *RawPublisher) readRTCP() {
	for {
		select {
		case b := <-p.mux.media:
			if !isRTCP(b) {
				continue
			}
			if pkts, err := rtcp.Unmarshal(b); err == nil {
				p.countPLIs(pkts)
			}
		case <-p.mux.done:
			return
		}
	}
}

// Write sends one sample, as the RTP packets of its track.
func (p *RawPublisher) Write(s Sample) error {
	p.mu.Lock()
	defer p.mu.Unlock()
	t := &p.audio
	if s.Video {
		t = &p.video
	}
	for _, pkt := range t.sender.packets(s) {
		pkt.SSRC, pkt.PayloadType = t.ssrc, t.pt
		b, err := pkt.Marshal()
		if err != nil {
			return err
		}
		if _, err := p.conn.Write(b); err != nil {
			return err
		}
	}
	return nil
}

// Send writes samples in order, paced by pacer like ffmpeg -re, or at once
// when pacer is nil, until they end or ctx ends.
func (p *RawPublisher) Send(ctx context.Context, samples []Sample, pacer *rtmp.Pacer) error {
	return send(ctx, p.Write, samples, pacer)
}

// A RawPlayer reads clear RTP from the server and analyzes it.
type RawPlayer struct {
	RawSession
	*analysis
}

// RawPlay connects a raw player by WHEP to url, with a DTLS handshake when
// withDTLS is set, and analyzes the RTP of the answer's H.264 and Opus it
// gets until it closes.
func RawPlay(ctx context.Context, url string, opts *Options, withDTLS bool) (*RawPlayer, error) {
	s, err := rawConnect(ctx, url, opts, false, withDTLS)
	if err != nil {
		return nil, err
	}
	p := &RawPlayer{RawSession: *s, analysis: newAnalysis()}
	go p.readRTP()
	return p, nil
}

// readRTP analyzes the RTP packets of the answer's codecs.
func (p *RawPlayer) readRTP() {
	for {
		var b []byte
		select {
		case b = <-p.mux.media:
		case <-p.mux.done:
			return
		}
		if isRTCP(b) {
			continue
		}
		var pkt rtp.Packet
		if pkt.Unmarshal(b) != nil {
			continue
		}
		for _, m := range p.answer.medias {
			if c, ok := m.codecs[pkt.PayloadType]; ok && (c.name == "h264" || c.name == "opus") {
				p.add(c.name, c.rate, &pkt)
				break
			}
		}
	}
}

// RequestKeyframe sends a picture loss indication, in clear RTCP, for the
// video the server sends.
func (p *RawPlayer) RequestKeyframe() error {
	m, _, err := p.media("video", "h264")
	if err != nil {
		return err
	}
	if m.ssrc == 0 {
		return errors.New("the answer has no video SSRC")
	}
	b, err := rtcp.Marshal([]rtcp.Packet{&rtcp.PictureLossIndication{SenderSSRC: 1, MediaSSRC: m.ssrc}})
	if err != nil {
		return err
	}
	_, err = p.conn.Write(b)
	return err
}

// An answerInfo is what a raw session needs of an SDP: the ICE credentials,
// the UDP and the TCP candidates, and each media section.
type answerInfo struct {
	ufrag, pwd    string
	candidates    []netip.AddrPort
	tcpCandidates []netip.AddrPort
	medias        []answerMedia
}

// An answerMedia is a media section: its kind, its first SSRC, if any, and
// its codecs by payload type.
type answerMedia struct {
	kind   string
	ssrc   uint32
	codecs map[uint8]answerCodec
}

// An answerCodec is a codec of an rtpmap: its name in lower case and clock
// rate.
type answerCodec struct {
	name string
	rate uint32
}

// parseAnswer reads an SDP, an answer or an offer.
func parseAnswer(sdp string) (*answerInfo, error) {
	a := &answerInfo{}
	seen := map[netip.AddrPort]bool{}
	var m *answerMedia
	for _, line := range strings.Split(sdp, "\n") {
		line = strings.TrimSpace(line)
		if kind, ok := strings.CutPrefix(line, "m="); ok {
			kind, _, _ = strings.Cut(kind, " ")
			a.medias = append(a.medias, answerMedia{kind: kind, codecs: map[uint8]answerCodec{}})
			m = &a.medias[len(a.medias)-1]
			continue
		}
		attr, ok := strings.CutPrefix(line, "a=")
		if !ok {
			continue
		}
		name, value, _ := strings.Cut(attr, ":")
		switch name {
		case "ice-ufrag":
			if a.ufrag == "" {
				a.ufrag = value
			}
		case "ice-pwd":
			if a.pwd == "" {
				a.pwd = value
			}
		case "candidate":
			// foundation component transport priority address port typ type
			f := strings.Fields(value)
			if len(f) < 6 {
				continue
			}
			tcp := strings.EqualFold(f[2], "tcp")
			if !tcp && !strings.EqualFold(f[2], "udp") {
				continue
			}
			ip, err1 := netip.ParseAddr(f[4])
			port, err2 := strconv.ParseUint(f[5], 10, 16)
			if err1 != nil || err2 != nil {
				continue
			}
			if c := netip.AddrPortFrom(ip, uint16(port)); tcp {
				a.tcpCandidates = append(a.tcpCandidates, c)
			} else if !seen[c] {
				seen[c] = true
				a.candidates = append(a.candidates, c)
			}
		case "rtpmap":
			if m == nil {
				continue
			}
			pt, enc, _ := strings.Cut(value, " ")
			codec, rate, _ := strings.Cut(enc, "/")
			rate, _, _ = strings.Cut(rate, "/")
			n, err1 := strconv.ParseUint(pt, 10, 8)
			r, err2 := strconv.ParseUint(rate, 10, 32)
			if err1 == nil && err2 == nil {
				m.codecs[uint8(n)] = answerCodec{strings.ToLower(codec), uint32(r)}
			}
		case "ssrc":
			if m == nil || m.ssrc != 0 {
				continue
			}
			id, _, _ := strings.Cut(value, " ")
			if n, err := strconv.ParseUint(id, 10, 32); err == nil {
				m.ssrc = uint32(n)
			}
		}
	}
	if a.ufrag == "" || a.pwd == "" {
		return nil, errors.New("no ICE credentials")
	}
	return a, nil
}

// bindingRequest is a STUN binding request to the server, as an ICE
// controlling agent that nominates the pair, or as a controlled one, which
// doesn't.
func bindingRequest(serverUfrag, clientUfrag, serverPwd string, controlled bool) (*stun.Message, error) {
	var b [8]byte
	rand.Read(b[:])
	tie := binary.BigEndian.Uint64(b[:])
	setters := []stun.Setter{stun.TransactionID, stun.BindingRequest, stun.NewUsername(serverUfrag + ":" + clientUfrag)}
	if controlled {
		setters = append(setters, ice.AttrControlled(tie), ice.PriorityAttr(2130706431))
	} else {
		setters = append(setters, ice.AttrControlling(tie), ice.PriorityAttr(2130706431), ice.UseCandidate())
	}
	return stun.Build(append(setters, stun.NewShortTermIntegrity(serverPwd), stun.Fingerprint)...)
}

// A mux reads the packets of one peer and sorts them by their first byte, as
// RFC 7983 says: STUN, DTLS, and RTP or RTCP. A packet that nobody reads in
// time is dropped, as a full socket buffer drops it.
type mux struct {
	conn  net.Conn
	stun  chan []byte
	dtls  chan []byte
	media chan []byte
	// done is closed when the mux stops reading.
	done chan struct{}

	once    sync.Once
	closing chan struct{}
}

// newMux starts reading conn.
func newMux(conn net.Conn) *mux {
	m := &mux{conn: conn, stun: make(chan []byte, 16), dtls: make(chan []byte, 64), media: make(chan []byte, 4096),
		done: make(chan struct{}), closing: make(chan struct{})}
	go m.read()
	return m
}

func (m *mux) read() {
	defer close(m.done)
	for {
		b := make([]byte, 1500)
		n, err := m.conn.Read(b)
		if err != nil {
			return
		}
		if n == 0 {
			continue
		}
		var ch chan []byte
		switch c := b[0]; {
		case c <= 3:
			ch = m.stun
		case c >= 20 && c <= 63:
			ch = m.dtls
		case c >= 128 && c <= 191:
			ch = m.media
		default:
			continue
		}
		select {
		case ch <- b[:n]:
		case <-m.closing:
			return
		default:
		}
	}
}

// close stops reading and closes the connection.
func (m *mux) close() {
	m.once.Do(func() {
		close(m.closing)
		m.conn.Close()
	})
}

// packetConn is the DTLS packets as a net.PacketConn, for pion/dtls. Its
// Close doesn't close the mux.
func (m *mux) packetConn() net.PacketConn {
	return &dtlsConn{m: m, closed: make(chan struct{}), changed: make(chan struct{})}
}

// A dtlsConn reads the DTLS packets of a mux and writes to its connection.
type dtlsConn struct {
	m      *mux
	once   sync.Once
	closed chan struct{}

	mu       sync.Mutex
	deadline time.Time
	// changed is closed when the deadline changes.
	changed chan struct{}
}

func (c *dtlsConn) ReadFrom(b []byte) (int, net.Addr, error) {
	for {
		c.mu.Lock()
		deadline, changed := c.deadline, c.changed
		c.mu.Unlock()
		var timer *time.Timer
		var timeout <-chan time.Time
		if !deadline.IsZero() {
			d := time.Until(deadline)
			if d <= 0 {
				return 0, nil, os.ErrDeadlineExceeded
			}
			timer = time.NewTimer(d)
			timeout = timer.C
		}
		n, addr, err, again := c.wait(b, timeout, changed)
		if timer != nil {
			timer.Stop()
		}
		if !again {
			return n, addr, err
		}
	}
}

// wait reads one DTLS packet, unless the deadline passes, changes, or the
// connection closes; again tells that the deadline changed.
func (c *dtlsConn) wait(b []byte, timeout <-chan time.Time, changed chan struct{}) (int, net.Addr, error, bool) {
	select {
	case p := <-c.m.dtls:
		return copy(b, p), c.m.conn.RemoteAddr(), nil, false
	case <-timeout:
		return 0, nil, os.ErrDeadlineExceeded, false
	case <-changed:
		return 0, nil, nil, true
	case <-c.closed:
		return 0, nil, net.ErrClosed, false
	case <-c.m.done:
		return 0, nil, net.ErrClosed, false
	}
}

func (c *dtlsConn) WriteTo(b []byte, _ net.Addr) (int, error) { return c.m.conn.Write(b) }

func (c *dtlsConn) Close() error {
	c.once.Do(func() { close(c.closed) })
	return nil
}

func (c *dtlsConn) LocalAddr() net.Addr { return c.m.conn.LocalAddr() }

func (c *dtlsConn) SetDeadline(t time.Time) error { return c.SetReadDeadline(t) }

func (c *dtlsConn) SetReadDeadline(t time.Time) error {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.deadline = t
	close(c.changed)
	c.changed = make(chan struct{})
	return nil
}

func (c *dtlsConn) SetWriteDeadline(time.Time) error { return nil }

// isRTCP tells an RTCP packet from an RTP one by its packet type, as RFC 5761
// says.
func isRTCP(b []byte) bool {
	return len(b) >= 2 && b[1] >= 192 && b[1] <= 223
}
