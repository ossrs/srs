// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"context"
	"crypto/tls"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"net/netip"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/pion/dtls/v3"
	"github.com/pion/dtls/v3/pkg/crypto/selfsign"
	"github.com/pion/rtcp"
	"github.com/pion/rtp"
	"github.com/pion/stun/v4"

	"srs-integ/internal/report"
)

// TestOffer checks the offers: the direction, the codecs in order, an SSRC
// for each section a publisher sends, and no candidates.
func TestOffer(t *testing.T) {
	pub, err := Offer(true, []string{"VP9", "H264"}, []string{"PCMU", "opus"})
	if err != nil {
		t.Fatal(err)
	}
	video, audio := section(pub, "video"), section(pub, "audio")
	if !strings.Contains(video, "a=sendonly") || !strings.Contains(audio, "a=sendonly") {
		t.Errorf("publish offer is not sendonly:\n%v", pub)
	}
	if !before(video, "VP9/90000", "H264/90000") || strings.Contains(video, "AV1") {
		t.Errorf("video codecs want VP9 then H264 only:\n%v", video)
	}
	if !before(audio, "PCMU/8000", "opus/48000/2") {
		t.Errorf("audio codecs want PCMU then opus:\n%v", audio)
	}
	for _, s := range []string{video, audio} {
		if !strings.Contains(s, "a=ssrc:") {
			t.Errorf("publish section has no SSRC:\n%v", s)
		}
	}
	if strings.Contains(pub, "a=candidate") || !strings.Contains(pub, "a=ice-ufrag:") || !strings.Contains(pub, "a=group:BUNDLE") {
		t.Errorf("offer wants ICE credentials and BUNDLE, no candidates:\n%v", pub)
	}

	play, err := Offer(false, []string{"H264"}, nil)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(section(play, "video"), "a=recvonly") || section(play, "audio") != "" {
		t.Errorf("play offer wants a recvonly video section only:\n%v", play)
	}
	if _, err := Offer(true, []string{"NOPE"}, nil); err == nil {
		t.Error("an unknown codec made an offer")
	}
}

// TestPost checks an offer posted without a peer connection: the answer and
// Location of a 201, and the status and response of an error.
func TestPost(t *testing.T) {
	sfu := newFakeSFU(t)
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	offer, err := Offer(true, []string{"H264"}, []string{"opus"})
	if err != nil {
		t.Fatal(err)
	}
	s, err := Post(ctx, sfu.URL+"/rtc/v1/whip/?app=live&stream=s", offer, nil)
	if err != nil {
		t.Fatal(err)
	}
	if s.PC != nil || s.Offer != offer || !strings.Contains(s.Answer, "a=fingerprint:") || s.Response.StatusCode != http.StatusCreated {
		t.Errorf("session %+v, want the offer, the answer of a 201 and no peer connection", s)
	}
	if !strings.HasPrefix(s.Location, sfu.URL+"/rtc/v1/whip/?action=delete&") {
		t.Errorf("location %q", s.Location)
	}
	if reqs := sfu.requests(); len(reqs) != 1 || reqs[0].Body != offer || reqs[0].ContentType != "application/sdp" {
		t.Errorf("requests %+v, want the offer as application/sdp", reqs)
	}

	sfu.reject = &StatusError{Code: http.StatusBadRequest, Body: "bad sdp"}
	s, err = Post(ctx, sfu.URL+"/rtc/v1/whip/?app=live&stream=s", "not an sdp", nil)
	var se *StatusError
	if !errors.As(err, &se) || se.Code != http.StatusBadRequest || se.Body != "bad sdp" {
		t.Fatalf("error %v, want status 400 with its body", err)
	}
	if s == nil || s.Response == nil || s.Response.StatusCode != http.StatusBadRequest {
		t.Errorf("session %+v, want the 400 response", s)
	}
}

// srsAnswer is an answer as SRS writes it, with a candidate it can't be
// reached on before the loopback one.
const srsAnswer = "v=0\r\no=SRS/8.0.60 1 2 IN IP4 0.0.0.0\r\ns=SRSPlaySession\r\nt=0 0\r\na=ice-lite\r\n" +
	"a=group:BUNDLE 0 1\r\na=msid-semantic: WMS live/s\r\n" +
	"m=audio 9 UDP/TLS/RTP/SAVPF 111\r\nc=IN IP4 0.0.0.0\r\na=ice-ufrag:srvu1234\r\na=ice-pwd:srvpwd0123456789abcdef0123\r\n" +
	"a=fingerprint:sha-256 AA:BB\r\na=setup:passive\r\na=mid:0\r\na=sendonly\r\na=rtcp-mux\r\n" +
	"a=rtpmap:111 opus/48000/2\r\na=ssrc:2222 cname:x\r\n" +
	"a=candidate:0 1 udp 2130706431 10.20.30.40 14100 typ host generation 0\r\n" +
	"a=candidate:1 1 udp 2130706431 127.0.0.1 14100 typ host generation 0\r\n" +
	"a=candidate:2 1 tcp 2130706431 127.0.0.1 14101 typ host tcptype passive\r\n" +
	"m=video 9 UDP/TLS/RTP/SAVPF 106 107\r\nc=IN IP4 0.0.0.0\r\na=ice-ufrag:srvu1234\r\na=ice-pwd:srvpwd0123456789abcdef0123\r\n" +
	"a=mid:1\r\na=sendonly\r\na=rtcp-mux\r\na=rtpmap:106 H264/90000\r\na=rtpmap:107 rtx/90000\r\na=ssrc:1111 cname:x\r\n"

func TestParseAnswer(t *testing.T) {
	a, err := parseAnswer(srsAnswer)
	if err != nil {
		t.Fatal(err)
	}
	if a.ufrag != "srvu1234" || a.pwd != "srvpwd0123456789abcdef0123" {
		t.Errorf("credentials %q %q", a.ufrag, a.pwd)
	}
	want := []netip.AddrPort{netip.MustParseAddrPort("10.20.30.40:14100"), netip.MustParseAddrPort("127.0.0.1:14100")}
	if fmt.Sprint(a.candidates) != fmt.Sprint(want) {
		t.Errorf("candidates %v, want %v", a.candidates, want)
	}
	if tcp := []netip.AddrPort{netip.MustParseAddrPort("127.0.0.1:14101")}; fmt.Sprint(a.tcpCandidates) != fmt.Sprint(tcp) {
		t.Errorf("TCP candidates %v, want %v", a.tcpCandidates, tcp)
	}
	if len(a.medias) != 2 {
		t.Fatalf("%v media sections, want 2", len(a.medias))
	}
	if m := a.medias[0]; m.kind != "audio" || m.ssrc != 2222 || m.codecs[111] != (answerCodec{"opus", 48000}) {
		t.Errorf("audio %+v", m)
	}
	if m := a.medias[1]; m.kind != "video" || m.ssrc != 1111 || m.codecs[106] != (answerCodec{"h264", 90000}) || m.codecs[107] != (answerCodec{"rtx", 90000}) {
		t.Errorf("video %+v", m)
	}
	if _, err := parseAnswer("v=0\r\nnot an answer"); err == nil {
		t.Error("an answer without ICE credentials parsed")
	}
}

// TestBindingRequest checks the request a server answers: the username of
// both fragments, the integrity of the server's password, a fingerprint, and
// the attributes of a controlling agent that nominates the pair.
func TestBindingRequest(t *testing.T) {
	m, err := bindingRequest("srvu", "cliu", "srvpwd", false)
	if err != nil {
		t.Fatal(err)
	}
	d := &stun.Message{Raw: append([]byte(nil), m.Raw...)}
	if err := d.Decode(); err != nil {
		t.Fatal(err)
	}
	if d.Type != stun.BindingRequest {
		t.Errorf("type %v", d.Type)
	}
	var u stun.Username
	if err := u.GetFrom(d); err != nil || u.String() != "srvu:cliu" {
		t.Errorf("username %q %v, want srvu:cliu", u, err)
	}
	if err := stun.NewShortTermIntegrity("srvpwd").Check(d); err != nil {
		t.Errorf("integrity: %v", err)
	}
	if err := stun.Fingerprint.Check(d); err != nil {
		t.Errorf("fingerprint: %v", err)
	}
	for _, a := range []stun.AttrType{stun.AttrICEControlling, stun.AttrUseCandidate, stun.AttrPriority} {
		if !d.Contains(a) {
			t.Errorf("no %v", a)
		}
	}
}

// TestMux checks the packets are sorted by their first byte.
func TestMux(t *testing.T) {
	a, b := net.Pipe()
	defer b.Close()
	m := newMux(a)
	defer m.close()
	go func() {
		for _, p := range [][]byte{{0x00, 1}, {0x16, 2}, {0x80, 3}, {0xff, 4}, {0x01, 5}} {
			b.Write(p)
		}
	}()
	for _, w := range []struct {
		ch   chan []byte
		want byte
	}{{m.stun, 1}, {m.dtls, 2}, {m.media, 3}, {m.stun, 5}} {
		select {
		case p := <-w.ch:
			if p[1] != w.want {
				t.Errorf("packet %v, want %v", p, w.want)
			}
		case <-time.After(time.Second):
			t.Fatalf("no packet %v", w.want)
		}
	}
}

// TestRawPlay plays clear RTP from a fake ICE-lite server, with and without
// DTLS, over UDP and over TCP, and asks it for a keyframe.
func TestRawPlay(t *testing.T) {
	for _, tcp := range []bool{false, true} {
		for _, withDTLS := range []bool{false, true} {
			t.Run(fmt.Sprintf("TCP=%v/DTLS=%v", tcp, withDTLS), func(t *testing.T) {
				t.Parallel()
				srv := newFakeRawOn(t, withDTLS, tcp)
				ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
				defer cancel()
				p, err := RawPlay(ctx, srv.URL+"/rtc/v1/whep/?app=live&stream=s", &Options{TCP: tcp}, withDTLS)
				if err != nil {
					t.Fatal(err)
				}
				defer p.Close()
				if p.DTLS != withDTLS || p.Response.StatusCode != http.StatusCreated || !strings.Contains(p.Offer, "a=recvonly") {
					t.Errorf("session DTLS %v, status %v", p.DTLS, p.Response.StatusCode)
				}
				if u := srv.username(); !strings.HasPrefix(u, "srvu1234:") {
					t.Errorf("STUN username %q", u)
				}
				if got := srv.network(); got != map[bool]string{false: "udp", true: "tcp"}[tcp] {
					t.Errorf("the session came over %v", got)
				}
				if err := p.RequestKeyframe(); err != nil {
					t.Fatal(err)
				}
				if err := srv.waitPLI(ctx, 1111); err != nil {
					t.Fatal(err)
				}
				srv.sendSamples(t, readSamples(t, 2*time.Second))
				r, err := p.Until(ctx, report.Until{VideoHeader: true, Keyframes: 2, VideoFrames: 50, AudioFrames: 100})
				if err != nil {
					t.Fatal(err)
				}
				checkTwoSeconds(t, r)
				if p.Packets() < 150 {
					t.Errorf("%v packets", p.Packets())
				}
			})
		}
	}
}

// TestRawPublish publishes clear RTP to a fake ICE-lite server, with and
// without DTLS, over UDP and over TCP, and counts the server's PLIs.
func TestRawPublish(t *testing.T) {
	for _, tcp := range []bool{false, true} {
		for _, withDTLS := range []bool{false, true} {
			t.Run(fmt.Sprintf("TCP=%v/DTLS=%v", tcp, withDTLS), func(t *testing.T) {
				t.Parallel()
				srv := newFakeRawOn(t, withDTLS, tcp)
				ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
				defer cancel()
				p, err := RawPublish(ctx, srv.URL+"/rtc/v1/whip/?app=live&stream=s", &Options{TCP: tcp}, withDTLS)
				if err != nil {
					t.Fatal(err)
				}
				defer p.Close()
				if p.DTLS != withDTLS || !strings.Contains(p.Offer, "a=sendonly") {
					t.Errorf("session DTLS %v", p.DTLS)
				}
				srv.sendPLI(t)
				if err := p.WaitPLI(ctx, 0); err != nil {
					t.Fatal(err)
				}
				if err := p.Send(ctx, readSamples(t, 2*time.Second), burstPacer(tcp)); err != nil {
					t.Fatal(err)
				}
				r, err := srv.analysis.Until(ctx, report.Until{VideoHeader: true, Keyframes: 2, VideoFrames: 50, AudioFrames: 99})
				if err != nil {
					t.Fatal(err)
				}
				checkTwoSeconds(t, r)
			})
		}
	}
}

// TestRawSRTP plays and publishes with SRTP to a fake ICE-lite server, over
// UDP and over TCP. The publisher sends what its rewrite function returns in
// place of each packet, protected, and what its tamper function returns in
// place of each protected one; WriteRTCP protects any bytes after a first
// RTCP header, and WriteRaw sends bytes as they are.
func TestRawSRTP(t *testing.T) {
	for _, tcp := range []bool{false, true} {
		t.Run(fmt.Sprintf("TCP=%v/Play", tcp), func(t *testing.T) {
			t.Parallel()
			srv := newFakeRawWith(t, true, tcp, true)
			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			defer cancel()
			p, err := RawPlay(ctx, srv.URL+"/rtc/v1/whep/?app=live&stream=s", &Options{TCP: tcp, SRTP: true}, true)
			if err != nil {
				t.Fatal(err)
			}
			defer p.Close()
			if err := p.RequestKeyframe(); err != nil {
				t.Fatal(err)
			}
			if err := srv.waitPLI(ctx, 1111); err != nil {
				t.Fatal(err)
			}
			srv.sendSamples(t, readSamples(t, 2*time.Second))
			r, err := p.Until(ctx, report.Until{VideoHeader: true, Keyframes: 2, VideoFrames: 50, AudioFrames: 100})
			if err != nil {
				t.Fatal(err)
			}
			checkTwoSeconds(t, r)
		})

		t.Run(fmt.Sprintf("TCP=%v/Publish", tcp), func(t *testing.T) {
			t.Parallel()
			srv := newFakeRawWith(t, true, tcp, true)
			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			defer cancel()
			p, err := RawPublish(ctx, srv.URL+"/rtc/v1/whip/?app=live&stream=s", &Options{TCP: tcp, SRTP: true}, true)
			if err != nil {
				t.Fatal(err)
			}
			defer p.Close()
			srv.sendPLI(t)
			if err := p.WaitPLI(ctx, 0); err != nil {
				t.Fatal(err)
			}
			samples := readSamples(t, 3*time.Second)
			var first, rest []Sample
			for _, s := range samples {
				if s.DTS < 2*time.Second {
					first = append(first, s)
				} else {
					rest = append(rest, s)
				}
			}
			if err := p.Send(ctx, first, burstPacer(tcp)); err != nil {
				t.Fatal(err)
			}
			r, err := srv.analysis.Until(ctx, report.Until{VideoHeader: true, Keyframes: 2, VideoFrames: 50, AudioFrames: 99})
			if err != nil {
				t.Fatal(err)
			}
			checkTwoSeconds(t, r)
			if bad, _ := srv.received(); bad != 0 {
				t.Errorf("%v packets the server couldn't unprotect", bad)
			}

			// The rewrite function sends no audio, and the tamper function
			// sends each video packet twice: as it is, and with its last
			// byte, of the authentication tag, changed, which the server
			// can't unprotect.
			var mu sync.Mutex
			var video int
			p.Rewrite(func(b []byte, isVideo bool) [][]byte {
				if !isVideo {
					return nil
				}
				return [][]byte{b}
			})
			p.Tamper(func(b []byte, isVideo bool) [][]byte {
				if !isVideo {
					return [][]byte{b}
				}
				mu.Lock()
				video++
				mu.Unlock()
				bad := append([]byte(nil), b...)
				bad[len(bad)-1] ^= 0xff
				return [][]byte{bad, b}
			})
			audio := r.Audio.Frames
			if err := p.Send(ctx, rest, burstPacer(tcp)); err != nil {
				t.Fatal(err)
			}
			r, err = srv.analysis.Until(ctx, report.Until{VideoFrames: 75})
			if err != nil {
				t.Fatal(err)
			}
			if r.Audio.Frames != audio {
				t.Errorf("%v audio frames after the rewrite, want %v", r.Audio.Frames, audio)
			}
			p.Rewrite(nil)
			p.Tamper(nil)

			// An RTCP packet with bytes after its header that aren't RTCP,
			// and RTP that SRTP didn't protect.
			junk := []byte{0x80, 200, 0, 1, 0, 0, 0, 1, 0xde, 0xad}
			if err := p.WriteRTCP(junk); err != nil {
				t.Fatal(err)
			}
			if err := p.WriteRaw([]byte{0x80, 96, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0xde, 0xad}); err != nil {
				t.Fatal(err)
			}
			mu.Lock()
			want := video + 1
			mu.Unlock()
			for {
				bad, rtcps := srv.received()
				if bad == want && len(rtcps) > 0 && string(rtcps[len(rtcps)-1]) == string(junk) {
					break
				}
				if ctx.Err() != nil {
					t.Fatalf("%v packets the server couldn't unprotect, want %v; RTCP %x", bad, want, rtcps)
				}
				time.Sleep(10 * time.Millisecond)
			}
		})
	}
}

// A fakeRaw is an ICE-lite server in the test that answers WHIP and WHEP
// with srsAnswer's credentials and codecs, on a UDP port, or a TCP port, of
// its own: it answers STUN, does the DTLS handshake as the server when
// asked, and sends and reads RTP, in clear or with SRTP.
type fakeRaw struct {
	URL      string
	withDTLS bool
	withSRTP bool
	tcp      bool
	udp      *net.UDPConn
	ln       net.Listener
	analysis *analysis

	mu         sync.Mutex
	user       string
	controlled bool
	peer       net.Conn
	mux        *mux
	plis       []uint32
	// srtp protects what the server sends and unprotects what it receives,
	// once the handshake is done; bad counts the packets it couldn't
	// unprotect, and rtcp is the RTCP packets it received, in clear.
	srtp       *srtpPair
	bad        int
	rtcp       [][]byte
	ready      chan struct{}
	vs         *sender
	as         *sender
	// closeDelay is how long the server waits, once the client closed, before
	// it closes the connection too, at closedAt.
	closeDelay time.Duration
	closedAt   time.Time
}

func newFakeRaw(t *testing.T, withDTLS bool) *fakeRaw {
	t.Helper()
	return newFakeRawOn(t, withDTLS, false)
}

// newFakeRawOn makes a fake server that takes its session over TCP when tcp
// is set, framed as RFC 4571 says, and over UDP otherwise.
func newFakeRawOn(t *testing.T, withDTLS, tcp bool) *fakeRaw {
	t.Helper()
	return newFakeRawWith(t, withDTLS, tcp, false)
}

// newFakeRawWith makes a fake server that protects its RTP and RTCP with
// SRTP after the DTLS handshake when withSRTP is set.
func newFakeRawWith(t *testing.T, withDTLS, tcp, withSRTP bool) *fakeRaw {
	t.Helper()
	udp, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	ln, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	f := &fakeRaw{withDTLS: withDTLS, withSRTP: withSRTP, tcp: tcp, udp: udp, ln: ln, analysis: newAnalysis(), ready: make(chan struct{}),
		vs: newVideoSender(), as: newAudioSender()}
	answer := strings.ReplaceAll(srsAnswer, " 14100 ", fmt.Sprintf(" %d ", udp.LocalAddr().(*net.UDPAddr).Port))
	answer = strings.ReplaceAll(answer, " 14101 ", fmt.Sprintf(" %d ", ln.Addr().(*net.TCPAddr).Port))
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		io.ReadAll(r.Body)
		w.Header().Set("Location", "/rtc/v1/whip/?action=delete&session=raw")
		w.WriteHeader(http.StatusCreated)
		io.WriteString(w, answer)
	}))
	f.URL = srv.URL
	go f.serve(t)
	t.Cleanup(func() {
		srv.Close()
		udp.Close()
		ln.Close()
		f.mu.Lock()
		defer f.mu.Unlock()
		if f.mux != nil {
			f.mux.close()
		}
	})
	return f
}

// accept returns the client's connection and its first packet: the first
// UDP datagram, or the first TCP connection and its first frame.
func (f *fakeRaw) accept() (net.Conn, []byte, error) {
	buf := make([]byte, 1500)
	if f.tcp {
		c, err := f.ln.Accept()
		if err != nil {
			return nil, nil, err
		}
		fc := newFramedConn(c)
		n, err := fc.Read(buf)
		if err != nil {
			c.Close()
			return nil, nil, err
		}
		return fc, buf[:n], nil
	}
	n, addr, err := f.udp.ReadFrom(buf)
	if err != nil {
		return nil, nil, err
	}
	return &peerConn{UDPConn: f.udp, peer: addr}, buf[:n], nil
}

// serve answers the first STUN request, then the next ones through the mux,
// does the DTLS handshake when asked, and reads the media.
func (f *fakeRaw) serve(t *testing.T) {
	peer, first, err := f.accept()
	if err != nil {
		return
	}
	m := newMux(peer)
	f.mu.Lock()
	f.peer, f.mux = peer, m
	f.mu.Unlock()
	// Like a server, it closes the connection when the client is gone.
	defer func() {
		<-m.done
		f.mu.Lock()
		d := f.closeDelay
		f.mu.Unlock()
		time.Sleep(d)
		f.mu.Lock()
		f.closedAt = time.Now()
		f.mu.Unlock()
		m.close()
	}()
	f.answerSTUN(first)
	go func() {
		for {
			select {
			case p := <-m.stun:
				f.answerSTUN(p)
			case <-m.done:
				return
			}
		}
	}()
	if f.withDTLS {
		cert, err := selfsign.GenerateSelfSigned()
		if err != nil {
			t.Error(err)
			return
		}
		c, err := dtls.Server(m.packetConn(), peer.RemoteAddr(), &dtls.Config{
			Certificates:           []tls.Certificate{cert},
			ClientAuth:             dtls.RequireAnyClientCert,
			SRTPProtectionProfiles: []dtls.SRTPProtectionProfile{dtls.SRTP_AES128_CM_HMAC_SHA1_80},
		})
		if err != nil {
			t.Error(err)
			return
		}
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		if err := c.HandshakeContext(ctx); err != nil {
			t.Error(err)
			return
		}
		if f.withSRTP {
			s, err := newSRTPPair(c, false)
			if err != nil {
				t.Error(err)
				return
			}
			f.mu.Lock()
			f.srtp = s
			f.mu.Unlock()
		}
	}
	close(f.ready)
	for {
		var p []byte
		select {
		case p = <-m.media:
		case <-m.done:
			return
		}
		if f.srtp != nil {
			var err error
			if p, err = f.srtp.unprotect(p); err != nil {
				f.mu.Lock()
				f.bad++
				f.mu.Unlock()
				continue
			}
		}
		if isRTCP(p) {
			f.mu.Lock()
			f.rtcp = append(f.rtcp, p)
			f.mu.Unlock()
			pkts, err := rtcp.Unmarshal(p)
			if err != nil {
				continue
			}
			for _, pkt := range pkts {
				if pli, ok := pkt.(*rtcp.PictureLossIndication); ok {
					f.mu.Lock()
					f.plis = append(f.plis, pli.MediaSSRC)
					f.mu.Unlock()
				}
			}
			continue
		}
		var pkt rtp.Packet
		if pkt.Unmarshal(p) != nil {
			continue
		}
		switch pkt.PayloadType {
		case 106:
			f.analysis.add("h264", 90000, &pkt)
		case 111:
			f.analysis.add("opus", 48000, &pkt)
		}
	}
}

func (f *fakeRaw) answerSTUN(p []byte) {
	req := &stun.Message{Raw: append([]byte(nil), p...)}
	if req.Decode() != nil || req.Type != stun.BindingRequest {
		return
	}
	var u stun.Username
	u.GetFrom(req)
	f.mu.Lock()
	f.user = u.String()
	f.controlled = req.Contains(stun.AttrICEControlled)
	peer := f.peer
	f.mu.Unlock()
	addr, err := netip.ParseAddrPort(peer.RemoteAddr().String())
	if err != nil {
		return
	}
	res, err := stun.Build(stun.NewTransactionIDSetter(req.TransactionID), stun.BindingSuccess,
		&stun.XORMappedAddress{IP: addr.Addr().AsSlice(), Port: int(addr.Port())},
		stun.NewShortTermIntegrity("srvpwd0123456789abcdef0123"), stun.Fingerprint)
	if err == nil {
		peer.Write(res.Raw)
	}
}

func (f *fakeRaw) username() string {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.user
}

// network is the network the session came over, udp or tcp.
func (f *fakeRaw) network() string {
	f.mu.Lock()
	defer f.mu.Unlock()
	if f.peer == nil {
		return ""
	}
	return f.peer.RemoteAddr().Network()
}

// isControlled tells whether the last binding request was from an
// ICE-controlled agent.
func (f *fakeRaw) isControlled() bool {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.controlled
}

// waitPLI waits until a PLI for ssrc arrives.
func (f *fakeRaw) waitPLI(ctx context.Context, ssrc uint32) error {
	for ctx.Err() == nil {
		f.mu.Lock()
		plis := append([]uint32(nil), f.plis...)
		f.mu.Unlock()
		for _, s := range plis {
			if s == ssrc {
				return nil
			}
		}
		time.Sleep(5 * time.Millisecond)
	}
	return fmt.Errorf("no PLI for %v", ssrc)
}

func (f *fakeRaw) waitReady(t *testing.T) net.Conn {
	t.Helper()
	select {
	case <-f.ready:
	case <-time.After(5 * time.Second):
		t.Fatal("the session is not ready")
	}
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.peer
}

// sendSamples sends samples as clear RTP with the answer's payload types
// and SSRCs, paced by burstPacer.
func (f *fakeRaw) sendSamples(t *testing.T, samples []Sample) {
	t.Helper()
	peer := f.waitReady(t)
	pacer := burstPacer(f.tcp)
	for _, s := range samples {
		if pacer != nil {
			if err := pacer.Wait(context.Background(), uint32(s.DTS/time.Millisecond)); err != nil {
				t.Fatal(err)
			}
		}
		sd, pt, ssrc := f.as, uint8(111), uint32(2222)
		if s.Video {
			sd, pt, ssrc = f.vs, 106, 1111
		}
		for _, p := range sd.packets(s) {
			p.PayloadType, p.SSRC = pt, ssrc
			b, err := p.Marshal()
			if err != nil {
				t.Fatal(err)
			}
			peer.Write(f.protect(t, b))
		}
	}
}

// protect returns a packet as the server sends it.
func (f *fakeRaw) protect(t *testing.T, b []byte) []byte {
	t.Helper()
	if f.srtp == nil {
		return b
	}
	b, err := f.srtp.protect(b)
	if err != nil {
		t.Fatal(err)
	}
	return b
}

// received returns how many packets the server couldn't unprotect, and the
// RTCP packets it received.
func (f *fakeRaw) received() (int, [][]byte) {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.bad, append([][]byte(nil), f.rtcp...)
}

// sendPLI sends a PLI to the publisher.
func (f *fakeRaw) sendPLI(t *testing.T) {
	t.Helper()
	peer := f.waitReady(t)
	b, err := rtcp.Marshal([]rtcp.Packet{&rtcp.PictureLossIndication{SenderSSRC: 1, MediaSSRC: 3333}})
	if err != nil {
		t.Fatal(err)
	}
	peer.Write(f.protect(t, b))
}

// A peerConn is a UDP socket as a connection to one peer.
type peerConn struct {
	*net.UDPConn
	peer net.Addr
}

func (c *peerConn) Read(b []byte) (int, error) {
	for {
		n, addr, err := c.UDPConn.ReadFrom(b)
		if err != nil || addr.String() == c.peer.String() {
			return n, err
		}
	}
}

func (c *peerConn) Write(b []byte) (int, error) { return c.UDPConn.WriteTo(b, c.peer) }
func (c *peerConn) RemoteAddr() net.Addr        { return c.peer }

// section returns the media section of a kind in an SDP, or "".
func section(sdp, kind string) string {
	for _, s := range strings.Split(sdp, "\nm=")[1:] {
		if strings.HasPrefix(s, kind) {
			return s
		}
	}
	return ""
}

// before tells whether a and b are in s, a first.
func before(s, a, b string) bool {
	i, j := strings.Index(s, a), strings.Index(s, b)
	return i >= 0 && j > i
}
