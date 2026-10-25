// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Package rtc is the suite's WebRTC client, built on pion: a publisher and a
// player that connect by WHIP and WHEP, or by SRS's legacy JSON API, over ICE
// on the loopback interface, and an analyzer that fills the stream report from
// the RTP packets a player gets.
package rtc

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"strings"
	"sync"
	"time"

	"github.com/pion/ice/v4"
	"github.com/pion/interceptor"
	"github.com/pion/interceptor/pkg/nack"
	"github.com/pion/interceptor/pkg/report"
	"github.com/pion/transport/v4"
	"github.com/pion/transport/v4/stdnet"
	"github.com/pion/webrtc/v4"
)

// Options are the settings of a session.
type Options struct {
	// Client sends the signaling requests; nil is http.DefaultClient.
	Client *http.Client
	// Header holds more request headers, such as Authorization.
	Header http.Header
	// Legacy is a stream URL, such as webrtc://host/live/stream. When it is
	// set, the session posts its offer as JSON to SRS's legacy API, such as
	// /rtc/v1/publish/ or /rtc/v1/play/, instead of WHIP or WHEP.
	Legacy string
	// Video and Audio are the codecs the session offers, in order, by their
	// names in SDP: H264, H265, VP9 or AV1, and opus, PCMU or PCMA. Nil
	// offers H264 or opus; an empty, non-nil list offers no track of that
	// kind.
	Video, Audio []string
	// TCP makes ICE go only over TCP, to the server's passive TCP candidate,
	// with each packet framed as RFC 4571 says; otherwise only over UDP.
	TCP bool
	// Setup is the DTLS setup the posted offer states, active or passive,
	// in place of actpass, so the server takes the other role.
	Setup string
	// Controlled makes a raw session's STUN binding requests those of an
	// ICE-controlled agent, which doesn't nominate the pair.
	Controlled bool
	// LocalPort is the local port of a raw session's socket, such as the
	// port of an earlier session's closed socket; 0 lets the system pick.
	LocalPort uint16
	// RTX offers RTX, RFC 4588, for each video codec: a publisher then sends
	// the packets the server asks for again on its RTX stream, and a player
	// takes them on the server's.
	RTX bool
	// TWCC makes a publisher send the transport-wide sequence number in each
	// RTP packet, which the server acknowledges in its TWCC feedback.
	TWCC bool
	// SenderReports makes a publisher send the Sender Reports of its tracks
	// itself, in place of pion's, which come once a second of wall time:
	// two before a track's first packet, a second of media apart, then one
	// each further second of media. Each has the RTP time of a decode time
	// and the NTP time of the same decode time from the publisher's start,
	// so a server that maps the tracks to one clock by their reports, such
	// as SRS converting WebRTC to RTMP, can map each from its first packet.
	SenderReports bool
	// NoSenderReports makes a publisher send no Sender Report at all, its
	// own or pion's, like a sender that hasn't sent one yet.
	NoSenderReports bool
	// AudioReportShift is added to the NTP time of the audio Sender
	// Reports, so the audio maps that much later than the video sent with
	// it.
	AudioReportShift time.Duration
	// Rebinder takes the session's UDP socket, and can move it to another
	// local port while the session runs.
	Rebinder *Rebinder
	// Dropper loses RTP packets of the session in its UDP socket, as a
	// network does.
	Dropper *Dropper
}

func (o *Options) client() *http.Client {
	if o == nil || o.Client == nil {
		return http.DefaultClient
	}
	return o.Client
}

// A StatusError is a WHIP or WHEP answer that is not 201 Created.
type StatusError struct {
	Code int
	Body string
}

func (e *StatusError) Error() string {
	return fmt.Sprintf("status %v: %q", e.Code, e.Body)
}

// A CodeError is a legacy API answer with a code other than 0.
type CodeError struct {
	Code int
	Body string
}

func (e *CodeError) Error() string {
	return fmt.Sprintf("code %v: %q", e.Code, e.Body)
}

// A Session is one peer connection and the signaling that set it up.
type Session struct {
	PC *webrtc.PeerConnection
	// Offer and Answer are the SDPs: the offer as it was posted.
	Offer, Answer string
	// Response is the signaling answer. Its body is read and closed.
	Response *http.Response
	// Location is the WHIP or WHEP resource, resolved against the request
	// URL; it is empty for the legacy API.
	Location string
	// SessionID is the sessionid of a legacy answer.
	SessionID string
	// Recorder records the RTP and RTCP of the peer connection.
	Recorder *Recorder

	opts *Options
}

// Delete sends DELETE to the session's Location, which ends the session on
// the server, and returns the answer with its body read and closed.
func (s *Session) Delete(ctx context.Context) (*http.Response, error) {
	if s.Location == "" {
		return nil, errors.New("the session has no location to DELETE")
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodDelete, s.Location, nil)
	if err != nil {
		return nil, err
	}
	addHeader(req, s.opts)
	res, err := s.opts.client().Do(req)
	if err != nil {
		return nil, err
	}
	io.Copy(io.Discard, res.Body)
	res.Body.Close()
	return res, nil
}

// Username is the username of the session's STUN binding requests, the
// server's ICE ufrag and the client's with a colon between, which is how
// the server names the session.
func (s *Session) Username() (string, error) {
	answer, err := parseAnswer(s.Answer)
	if err != nil {
		return "", fmt.Errorf("answer: %w", err)
	}
	offer, err := parseAnswer(s.Offer)
	if err != nil {
		return "", fmt.Errorf("offer: %w", err)
	}
	return answer.ufrag + ":" + offer.ufrag, nil
}

// Close closes the peer connection, which tells the server with a DTLS
// close_notify alert. It doesn't send DELETE.
func (s *Session) Close() error {
	if s.PC == nil {
		return nil
	}
	return s.PC.Close()
}

// iceCheckInterval is how often ICE checks candidates and pings the pair.
const iceCheckInterval = 20 * time.Millisecond

// nackInterval is how often a session's NACK generator asks for the
// packets it misses, and maxNacks how many times it asks for each.
const (
	nackInterval = 20 * time.Millisecond
	maxNacks     = 10
)

// newPeerConnection makes a peer connection that offers the codecs of the
// options, with RTX when they say, and pion's default interceptors (NACK,
// reports, TWCC feedback; the NACK generator asks every 20ms rather than
// 100ms), the transport-wide sequence numbers when the options say, and a
// recorder under them all; with ICE only over UDP, or only over TCP, on the
// loopback interface, where the test servers listen.
func newPeerConnection(opts *Options) (*webrtc.PeerConnection, *Recorder, error) {
	video, audio, err := sessionCodecs(opts)
	if err != nil {
		return nil, nil, err
	}
	m := &webrtc.MediaEngine{}
	for _, key := range append(video, audio...) {
		c := codecTable[key]
		if err := m.RegisterCodec(webrtc.RTPCodecParameters{RTPCodecCapability: c.capability, PayloadType: c.pt}, c.kind); err != nil {
			return nil, nil, err
		}
		if opts == nil || !opts.RTX || c.kind != webrtc.RTPCodecTypeVideo {
			continue
		}
		rtx := webrtc.RTPCodecCapability{MimeType: webrtc.MimeTypeRTX, ClockRate: 90000, SDPFmtpLine: fmt.Sprintf("apt=%d", c.pt)}
		if err := m.RegisterCodec(webrtc.RTPCodecParameters{RTPCodecCapability: rtx, PayloadType: c.pt + 1}, c.kind); err != nil {
			return nil, nil, err
		}
	}
	// The first interceptor is the one next to the network.
	rec := NewRecorder()
	ir := &interceptor.Registry{}
	ir.Add(rec)
	interceptors := []webrtc.InterceptorOption{webrtc.WithNackGeneratorOptions(
		nack.GeneratorInterval(nackInterval), nack.GeneratorMaxNacksPerPacket(maxNacks))}
	if opts != nil && (opts.SenderReports || opts.NoSenderReports) {
		// The publisher sends its own, or none; pion's would map the RTP time
		// of its last packet to the wall clock, which a burst moves off.
		interceptors = append(interceptors, webrtc.WithReportSenderOptions(report.SenderInterval(time.Hour)))
	}
	if err := webrtc.RegisterDefaultInterceptorsWithOptions(m, ir, interceptors...); err != nil {
		return nil, nil, err
	}
	if opts != nil && opts.TWCC {
		if err := webrtc.ConfigureTWCCHeaderExtensionSender(m, ir); err != nil {
			return nil, nil, err
		}
	}
	var se webrtc.SettingEngine
	network := webrtc.NetworkTypeUDP4
	if opts != nil && opts.TCP {
		// pion dials the server's passive TCP candidates from active ones.
		network = webrtc.NetworkTypeTCP4
	}
	se.SetNetworkTypes([]webrtc.NetworkType{network})
	se.SetIncludeLoopbackCandidate(true)
	se.SetIPFilter(func(ip net.IP) bool { return ip.IsLoopback() })
	se.SetICEMulticastDNSMode(ice.MulticastDNSModeDisabled)
	if opts != nil && (opts.Rebinder != nil || opts.Dropper != nil) {
		n, err := stdnet.NewNet()
		if err != nil {
			return nil, nil, err
		}
		var nw transport.Net = n
		if opts.Rebinder != nil {
			nw = &rebindNet{Net: n, r: opts.Rebinder}
		}
		if opts.Dropper != nil {
			nw = &dropNet{Net: nw, d: opts.Dropper}
		}
		se.SetNet(nw)
	}
	// pion checks candidates every 200ms while it connects, or at the
	// disconnected timeout when that is shorter: it pings, and nominates the
	// pair that answered on its next check, so a connection takes 200ms. With
	// 20ms, it takes about 20ms. Once connected, it pings on each check too,
	// so the pair answers every 20ms; a pair that is quiet for longer is only
	// reported disconnected, and never fails, since the failed timeout is off.
	se.SetICETimeouts(iceCheckInterval, 0, iceCheckInterval)
	api := webrtc.NewAPI(webrtc.WithMediaEngine(m), webrtc.WithInterceptorRegistry(ir), webrtc.WithSettingEngine(se))
	pc, err := api.NewPeerConnection(webrtc.Configuration{})
	return pc, rec, err
}

// connect offers the session's tracks, sends the offer to url, takes the
// answer and waits until ICE and DTLS are connected. On an error it closes
// the peer connection.
func (s *Session) connect(ctx context.Context, rawURL string) (err error) {
	defer func() {
		if err != nil {
			s.PC.Close()
		}
	}()
	// The state goes back and forth between connected and disconnected when
	// the pair is quiet for a check interval.
	connected := make(chan struct{})
	var once sync.Once
	failed := make(chan webrtc.PeerConnectionState, 1)
	s.PC.OnConnectionStateChange(func(st webrtc.PeerConnectionState) {
		switch st {
		case webrtc.PeerConnectionStateConnected:
			once.Do(func() { close(connected) })
		case webrtc.PeerConnectionStateFailed, webrtc.PeerConnectionStateClosed:
			select {
			case failed <- st:
			default:
			}
		}
	})

	offer, err := s.PC.CreateOffer(nil)
	if err != nil {
		return err
	}
	gathered := webrtc.GatheringCompletePromise(s.PC)
	if err := s.PC.SetLocalDescription(offer); err != nil {
		return err
	}
	select {
	case <-gathered:
	case <-ctx.Done():
		return fmt.Errorf("gather candidates: %w", ctx.Err())
	}
	// pion takes its DTLS role from the answer's setup, so the posted offer
	// may state another setup than its local description.
	s.Offer = withSetup(s.PC.LocalDescription().SDP, s.opts.setup())

	err = s.post(ctx, rawURL)
	if err != nil {
		return err
	}
	if err := s.PC.SetRemoteDescription(webrtc.SessionDescription{Type: webrtc.SDPTypeAnswer, SDP: s.Answer}); err != nil {
		return fmt.Errorf("set the answer: %w", err)
	}

	select {
	case <-connected:
		return nil
	case st := <-failed:
		return fmt.Errorf("peer connection %v", st)
	case <-ctx.Done():
		return fmt.Errorf("connect ICE and DTLS: %w", ctx.Err())
	}
}

// Offer makes the SDP offer of a session that publishes (sendonly) or plays
// (recvonly): a video and an audio section that offer the codecs, named as in
// SDP, such as H264, H265, VP9, AV1, opus, PCMU or PCMA, in that order,
// without feedback, the video from payload type 96. A section without
// codecs is left out. The offer has no candidates, and its
// peer connection is closed: it is for checking the answer, not for
// connecting.
func Offer(publish bool, video, audio []string) (string, error) {
	m := &webrtc.MediaEngine{}
	pt := webrtc.PayloadType(96)
	for _, kind := range []struct {
		kind  webrtc.RTPCodecType
		names []string
	}{{webrtc.RTPCodecTypeVideo, video}, {webrtc.RTPCodecTypeAudio, audio}} {
		for _, name := range kind.names {
			c, ok := codecTable[strings.ToLower(name)]
			if !ok || c.kind != kind.kind {
				return "", fmt.Errorf("no %v codec %v", kind.kind, name)
			}
			p := webrtc.RTPCodecParameters{RTPCodecCapability: c.capability, PayloadType: c.pt}
			p.RTCPFeedback = nil
			if c.kind == webrtc.RTPCodecTypeVideo {
				p.PayloadType, pt = pt, pt+1
			}
			if err := m.RegisterCodec(p, kind.kind); err != nil {
				return "", err
			}
		}
	}
	pc, err := webrtc.NewAPI(webrtc.WithMediaEngine(m)).NewPeerConnection(webrtc.Configuration{})
	if err != nil {
		return "", err
	}
	defer pc.Close()
	dir := webrtc.RTPTransceiverDirectionRecvonly
	if publish {
		dir = webrtc.RTPTransceiverDirectionSendonly
	}
	for _, k := range []struct {
		kind  webrtc.RTPCodecType
		names []string
	}{{webrtc.RTPCodecTypeVideo, video}, {webrtc.RTPCodecTypeAudio, audio}} {
		if len(k.names) == 0 {
			continue
		}
		if _, err := pc.AddTransceiverFromKind(k.kind, webrtc.RTPTransceiverInit{Direction: dir}); err != nil {
			return "", err
		}
	}
	offer, err := pc.CreateOffer(nil)
	if err != nil {
		return "", err
	}
	return offer.SDP, nil
}

// Post sends an offer by WHIP or WHEP, or by the legacy API when
// opts.Legacy is set, without a peer connection. The session has the answer,
// the response and the Location, and no peer connection. An answer other
// than 201 is a *StatusError, with the session and its Response.
func Post(ctx context.Context, url, offer string, opts *Options) (*Session, error) {
	s := &Session{Offer: offer, opts: opts}
	return s, s.post(ctx, url)
}

// post sends the session's offer by WHIP or WHEP, or by the legacy API.
func (s *Session) post(ctx context.Context, url string) error {
	if s.opts != nil && s.opts.Legacy != "" {
		return s.signalLegacy(ctx, url)
	}
	return s.signal(ctx, url)
}

// signal posts the offer by WHIP or WHEP.
func (s *Session) signal(ctx context.Context, rawURL string) error {
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, rawURL, strings.NewReader(s.Offer))
	if err != nil {
		return err
	}
	req.Header.Set("Content-Type", "application/sdp")
	addHeader(req, s.opts)
	res, body, err := do(s.opts.client(), req)
	if err != nil {
		return err
	}
	s.Response = res
	if res.StatusCode != http.StatusCreated {
		return &StatusError{Code: res.StatusCode, Body: string(body)}
	}
	s.Answer = string(body)
	if loc := res.Header.Get("Location"); loc != "" {
		u, err := req.URL.Parse(loc)
		if err != nil {
			return fmt.Errorf("location %q: %w", loc, err)
		}
		s.Location = u.String()
	}
	return nil
}

// signalLegacy posts the offer as JSON to SRS's legacy API.
func (s *Session) signalLegacy(ctx context.Context, rawURL string) error {
	u, err := url.Parse(rawURL)
	if err != nil {
		return err
	}
	api := *u
	api.RawQuery = ""
	b, err := json.Marshal(map[string]string{"api": api.String(), "streamurl": s.opts.Legacy, "sdp": s.Offer})
	if err != nil {
		return err
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, rawURL, bytes.NewReader(b))
	if err != nil {
		return err
	}
	req.Header.Set("Content-Type", "application/json")
	addHeader(req, s.opts)
	res, body, err := do(s.opts.client(), req)
	if err != nil {
		return err
	}
	s.Response = res
	if res.StatusCode != http.StatusOK {
		return &StatusError{Code: res.StatusCode, Body: string(body)}
	}
	var v struct {
		Code      int    `json:"code"`
		SDP       string `json:"sdp"`
		SessionID string `json:"sessionid"`
	}
	if err := json.Unmarshal(body, &v); err != nil {
		return fmt.Errorf("answer %q: %w", body, err)
	}
	if v.Code != 0 {
		return &CodeError{Code: v.Code, Body: string(body)}
	}
	s.Answer, s.SessionID = v.SDP, v.SessionID
	return nil
}

// do sends a request and reads the whole answer.
func do(client *http.Client, req *http.Request) (*http.Response, []byte, error) {
	res, err := client.Do(req)
	if err != nil {
		return nil, nil, err
	}
	defer res.Body.Close()
	body, err := io.ReadAll(res.Body)
	if err != nil {
		return nil, nil, fmt.Errorf("read the answer: %w", err)
	}
	return res, body, nil
}

func addHeader(req *http.Request, opts *Options) {
	if opts == nil {
		return
	}
	for k, vs := range opts.Header {
		for _, v := range vs {
			req.Header.Add(k, v)
		}
	}
}
