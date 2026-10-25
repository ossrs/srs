package main

import (
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/bluenviron/gortsplib/v5/pkg/format"
	"github.com/pion/ice/v4"
	"github.com/pion/interceptor"
	"github.com/pion/interceptor/pkg/report"
	"github.com/pion/rtcp"
	"github.com/pion/rtp"
	"github.com/pion/webrtc/v4"
)

// rtcAPIPort is SRS's HTTP API port, which serves WHIP and WHEP, and rtcPort
// its RTC server's UDP port, both as in the shipped rtc.conf.
const (
	rtcAPIPort = "1985"
	rtcPort    = "8000"
)

// The codecs the WebRTC clients offer: H.264 with packetization mode 1 and
// Opus, with no feedback, since the clients send no NACK, PLI or receiver
// reports.
var (
	rtcVideoCodec = webrtc.RTPCodecParameters{RTPCodecCapability: webrtc.RTPCodecCapability{MimeType: webrtc.MimeTypeH264, ClockRate: 90000,
		SDPFmtpLine: "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f"}, PayloadType: 106}
	rtcAudioCodec = webrtc.RTPCodecParameters{RTPCodecCapability: webrtc.RTPCodecCapability{MimeType: webrtc.MimeTypeOpus, ClockRate: 48000, Channels: 2,
		SDPFmtpLine: "minptime=10;useinbandfec=1"}, PayloadType: 111}
)

// rtcAPI is one lean pion API per process: H.264 and Opus only, no
// interceptors (no NACK, receiver reports or TWCC; the WHIP publisher sends
// its own sender reports), and ICE only over UDP on the
// loopback interface, since the load shares SRS's network namespace. pion
// copies the media engine for each peer connection, so one API serves all.
var rtcAPI = sync.OnceValues(func() (*webrtc.API, error) {
	m := &webrtc.MediaEngine{}
	if err := m.RegisterCodec(rtcVideoCodec, webrtc.RTPCodecTypeVideo); err != nil {
		return nil, err
	}
	if err := m.RegisterCodec(rtcAudioCodec, webrtc.RTPCodecTypeAudio); err != nil {
		return nil, err
	}
	var se webrtc.SettingEngine
	se.SetNetworkTypes([]webrtc.NetworkType{webrtc.NetworkTypeUDP4})
	se.SetIncludeLoopbackCandidate(true)
	se.SetIPFilter(func(ip net.IP) bool { return ip.IsLoopback() })
	se.SetICEMulticastDNSMode(ice.MulticastDNSModeDisabled)
	ir, err := rtcInterceptors(m)
	if err != nil {
		return nil, err
	}
	return webrtc.NewAPI(webrtc.WithMediaEngine(m), webrtc.WithSettingEngine(se), webrtc.WithInterceptorRegistry(ir)), nil
})

// rtcInterceptors are pion's default interceptors without its sender reports:
// NACK, receiver reports, the simulcast header extensions, stats and TWCC. The
// WHIP publisher sends its own sender reports on the pace's clock; pion's
// would be a second series from its own estimate of the RTP time, and SRS
// takes each track's rate from the last two reports of either.
func rtcInterceptors(m *webrtc.MediaEngine) (*interceptor.Registry, error) {
	ir := &interceptor.Registry{}
	if err := webrtc.ConfigureNack(m, ir); err != nil {
		return nil, err
	}
	receiver, err := report.NewReceiverInterceptor()
	if err != nil {
		return nil, err
	}
	ir.Add(receiver)
	if err := webrtc.ConfigureSimulcastExtensionHeaders(m); err != nil {
		return nil, err
	}
	if err := webrtc.ConfigureStatsInterceptor(ir); err != nil {
		return nil, err
	}
	if err := webrtc.ConfigureTWCCSender(m, ir); err != nil {
		return nil, err
	}
	return ir, nil
}

// rtcHTTP posts the offers; its timeout is a net.Error, so a player dials
// again while SRS's API is slow in a connect burst.
var rtcHTTP = &http.Client{Timeout: 10 * time.Second}

func rtcURL(apiAddr, kind, app, stream string) string {
	return fmt.Sprintf("http://%s/rtc/v1/%s/?app=%s&stream=%s", apiAddr, kind, app, stream)
}

// rtcTimeout is a connect that did not finish in time, a net.Error that
// times out, like a dial timeout.
type rtcTimeout string

func (e rtcTimeout) Error() string   { return string(e) + " timed out" }
func (e rtcTimeout) Timeout() bool   { return true }
func (e rtcTimeout) Temporary() bool { return true }

var errRTCStopped = errors.New("stopped")

// rtcSession is one peer connection and its state: connected closes once,
// failed gets the state that ended it.
type rtcSession struct {
	pc        *webrtc.PeerConnection
	connected chan struct{}
	failed    chan webrtc.PeerConnectionState
	// location is the WHIP or WHEP resource of the answer, resolved against
	// the request URL, or empty.
	location string
}

// delete sends DELETE to the session's resource, which SRS handles before
// it returns, unlike the DTLS close alert, which it reads later from UDP.
func (s *rtcSession) delete() error {
	if s.location == "" {
		return nil
	}
	req, err := http.NewRequest(http.MethodDelete, s.location, nil)
	if err != nil {
		return err
	}
	res, err := rtcHTTP.Do(req)
	if err != nil {
		return err
	}
	io.Copy(io.Discard, res.Body)
	res.Body.Close()
	if res.StatusCode/100 != 2 {
		return fmt.Errorf("DELETE %s: status %d", s.location, res.StatusCode)
	}
	return nil
}

// ended is the error of a session whose peer connection failed or closed, or
// nil while it lives.
func (s *rtcSession) ended() error {
	select {
	case st := <-s.failed:
		return fmt.Errorf("peer connection %v", st)
	default:
		return nil
	}
}

func newRTCSession() (*rtcSession, error) {
	api, err := rtcAPI()
	if err != nil {
		return nil, err
	}
	pc, err := api.NewPeerConnection(webrtc.Configuration{})
	if err != nil {
		return nil, err
	}
	s := &rtcSession{pc: pc, connected: make(chan struct{}), failed: make(chan webrtc.PeerConnectionState, 1)}
	var once sync.Once
	pc.OnConnectionStateChange(func(st webrtc.PeerConnectionState) {
		switch st {
		case webrtc.PeerConnectionStateConnected:
			once.Do(func() { close(s.connected) })
		case webrtc.PeerConnectionStateFailed, webrtc.PeerConnectionStateClosed:
			select {
			case s.failed <- st:
			default:
			}
		}
	})
	return s, nil
}

// connect posts the offer of the session's transceivers to url, by WHIP or
// WHEP, and waits until ICE and DTLS connect, the connection fails, timeout
// passes or stop.
func (s *rtcSession) connect(url string, timeout time.Duration, stop <-chan struct{}) error {
	timer := time.NewTimer(timeout)
	defer timer.Stop()
	offer, err := s.pc.CreateOffer(nil)
	if err != nil {
		return err
	}
	gathered := webrtc.GatheringCompletePromise(s.pc)
	if err := s.pc.SetLocalDescription(offer); err != nil {
		return err
	}
	select {
	case <-gathered:
	case <-timer.C:
		return rtcTimeout("gathering candidates")
	case <-stop:
		return errRTCStopped
	}
	res, err := rtcHTTP.Post(url, "application/sdp", strings.NewReader(s.pc.LocalDescription().SDP))
	if err != nil {
		return err
	}
	answer, err := io.ReadAll(res.Body)
	res.Body.Close()
	if err != nil {
		return err
	}
	if res.StatusCode != http.StatusCreated {
		return fmt.Errorf("%s: status %d: %s", url, res.StatusCode, strings.TrimSpace(string(answer)))
	}
	if loc := res.Header.Get("Location"); loc != "" {
		u, err := res.Request.URL.Parse(loc)
		if err != nil {
			return fmt.Errorf("location %q: %w", loc, err)
		}
		s.location = u.String()
	}
	if err := s.pc.SetRemoteDescription(webrtc.SessionDescription{Type: webrtc.SDPTypeAnswer, SDP: string(answer)}); err != nil {
		return fmt.Errorf("set the answer: %w", err)
	}
	select {
	case <-s.connected:
		return nil
	case st := <-s.failed:
		return fmt.Errorf("peer connection %v", st)
	case <-timer.C:
		return rtcTimeout("ICE and DTLS")
	case <-stop:
		return errRTCStopped
	}
}

// whepClient is a lean WHEP player with pion: no decode and no feedback;
// each H.264 access unit is rebuilt from RTP by its timestamps and markers for
// the lag anchor, and Opus is counted per RTP packet. The stream is named by
// the WHEP request, so the connection is made by play.
type whepClient struct {
	apiAddr string
	app     string
	timeout time.Duration
}

func dialWHEP(apiAddr, app string) (*whepClient, error) {
	if _, err := rtcAPI(); err != nil {
		return nil, err
	}
	return &whepClient{apiAddr: apiAddr, app: app, timeout: 10 * time.Second}, nil
}

// rtcTracks reads a session's remote tracks into report, until the session is
// closed; close waits for the readers, so none sends after play returns.
type rtcTracks struct {
	report chan<- rtmpMessage
	stop   <-chan struct{}
	newest atomic.Uint32
	mu     sync.Mutex
	closed bool
	wg     sync.WaitGroup
}

func (t *rtcTracks) send(m rtmpMessage) {
	select {
	case t.report <- m:
	case <-t.stop:
	}
}

// read reports each H.264 access unit as an FLV AVC payload, in milliseconds
// from the first video packet across the 32-bit wrap, and each Opus packet as
// audio at the newest video time, since the two RTP clocks start at
// unrelated values, as for RTSP.
func (t *rtcTracks) read(track *webrtc.TrackRemote, _ *webrtc.RTPReceiver) {
	t.mu.Lock()
	if t.closed {
		t.mu.Unlock()
		return
	}
	t.wg.Add(1)
	t.mu.Unlock()
	defer t.wg.Done()
	if track.Kind() != webrtc.RTPCodecTypeVideo {
		for {
			pkt, _, err := track.ReadRTP()
			if err != nil {
				return
			}
			t.send(rtmpMessage{typeID: 8, timestamp: t.newest.Load(), payload: pkt.Payload})
		}
	}
	decoder, err := (&format.H264{PayloadTyp: uint8(track.PayloadType()), PacketizationMode: 1}).CreateDecoder()
	if err != nil {
		return
	}
	clock := &rtpClock{rate: 90000}
	for {
		pkt, _, err := track.ReadRTP()
		if err != nil {
			return
		}
		ms := clock.millis(pkt.Timestamp)
		au, err := decoder.Decode(pkt)
		if err != nil {
			continue
		}
		t.newest.Store(ms)
		t.send(rtmpMessage{typeID: 9, timestamp: ms, payload: avcPayload(au)})
	}
}

func (t *rtcTracks) close(pc *webrtc.PeerConnection) {
	t.mu.Lock()
	t.closed = true
	t.mu.Unlock()
	pc.Close()
	t.wg.Wait()
}

// play requests /<app>/<stream> by WHEP and reports its frames until stop,
// which closes the peer connection and returns nil. It connects again while a
// connect times out, as when a step adds many players at once, until stop.
func (c *whepClient) play(stream string, stop <-chan struct{}, report chan<- rtmpMessage) error {
	stopped := func(err error) error {
		select {
		case <-stop:
			return nil
		default:
			return err
		}
	}
	for {
		s, err := newRTCSession()
		if err != nil {
			return err
		}
		tracks := &rtcTracks{report: report, stop: stop}
		for _, kind := range []webrtc.RTPCodecType{webrtc.RTPCodecTypeVideo, webrtc.RTPCodecTypeAudio} {
			if _, err := s.pc.AddTransceiverFromKind(kind, webrtc.RTPTransceiverInit{Direction: webrtc.RTPTransceiverDirectionRecvonly}); err != nil {
				s.pc.Close()
				return err
			}
		}
		s.pc.OnTrack(tracks.read)
		err = s.connect(rtcURL(c.apiAddr, "whep", c.app, stream), c.timeout, stop)
		if err != nil {
			tracks.close(s.pc)
			var netErr net.Error
			if errors.Is(err, errRTCStopped) || !errors.As(err, &netErr) || !netErr.Timeout() {
				return stopped(err)
			}
			select {
			case <-stop:
				return nil
			default:
			}
			continue
		}
		defer tracks.close(s.pc)
		select {
		case <-stop:
			return nil
		case st := <-s.failed:
			return stopped(fmt.Errorf("peer connection %v", st))
		}
	}
}

// opusSourcePath names the Opus track perf-source.sh writes beside a source:
// the same 440 Hz sine, 32 kbps CBR in 20ms packets.
func opusSourcePath(media string) string {
	return strings.TrimSuffix(media, ".flv") + "-opus.ogg"
}

// opusPacketMillis is an Opus packet's duration by its TOC byte (RFC 6716
// 3.1): the frame size of its config times its frame count, or 0 when the
// packet is too short to tell.
func opusPacketMillis(p []byte) float64 {
	if len(p) == 0 {
		return 0
	}
	config := p[0] >> 3
	var frame float64
	switch {
	case config < 12:
		frame = []float64{10, 20, 40, 60}[config%4]
	case config < 16:
		frame = []float64{10, 20}[config%2]
	default:
		frame = []float64{2.5, 5, 10, 20}[config%4]
	}
	switch p[0] & 3 {
	case 0:
		return frame
	case 1, 2:
		return 2 * frame
	}
	if len(p) < 2 {
		return 0
	}
	return float64(p[1]&0x3f) * frame
}

// oggPackets splits an Ogg stream's pages into packets by their lacing
// values; a packet may span pages. It does not check the CRC.
func oggPackets(data []byte) ([][]byte, error) {
	var packets [][]byte
	var packet []byte
	for len(data) > 0 {
		if len(data) < 27 || string(data[:4]) != "OggS" {
			return nil, errors.New("not an Ogg page")
		}
		n := int(data[26])
		if len(data) < 27+n {
			return nil, errors.New("truncated Ogg segment table")
		}
		lacing, body := data[27:27+n], data[27+n:]
		size := 0
		for _, l := range lacing {
			size += int(l)
		}
		if len(body) < size {
			return nil, errors.New("truncated Ogg page")
		}
		for _, l := range lacing {
			packet = append(packet, body[:l]...)
			body = body[l:]
			if l < 255 {
				packets = append(packets, packet)
				packet = nil
			}
		}
		data = data[27+n+size:]
	}
	return packets, nil
}

// opusTracks holds each Opus track read by sharedOpus, by path.
var opusTracks sync.Map

// sharedOpus is loadOpus read once per process and shared, read-only, by
// every publisher, so thousands of WHIP publishers hold one copy of the track.
func sharedOpus(path string) ([][]byte, error) {
	if packets, ok := opusTracks.Load(path); ok {
		return packets.([][]byte), nil
	}
	packets, err := loadOpus(path)
	if err != nil {
		return nil, err
	}
	actual, _ := opusTracks.LoadOrStore(path, packets)
	return actual.([][]byte), nil
}

// loadOpus reads an Ogg Opus file's audio packets. Like the source check, it
// refuses a track that is not flat: each whole second after the first within
// 3% of the mean of those seconds; and since the publisher sends a packet
// each 20ms, a packet of another duration.
func loadOpus(path string) ([][]byte, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("opus source: %w", err)
	}
	packets, err := oggPackets(data)
	if err != nil {
		return nil, fmt.Errorf("opus source %s: %w", path, err)
	}
	if len(packets) < 2 || !strings.HasPrefix(string(packets[0]), "OpusHead") || !strings.HasPrefix(string(packets[1]), "OpusTags") {
		return nil, fmt.Errorf("opus source %s has no OpusHead and OpusTags", path)
	}
	packets = packets[2:]
	for i, p := range packets {
		if opusPacketMillis(p) != 20 {
			return nil, fmt.Errorf("opus source %s: packet %d is %vms, not 20ms", path, i, opusPacketMillis(p))
		}
	}
	whole := len(packets) / 50
	if whole < 3 {
		return nil, fmt.Errorf("opus source %s is under 3 seconds", path)
	}
	seconds := make([]float64, 0, whole-1)
	var sum float64
	for s := 1; s < whole; s++ {
		bytes := 0
		for _, p := range packets[s*50 : (s+1)*50] {
			bytes += len(p)
		}
		seconds = append(seconds, float64(bytes))
		sum += float64(bytes)
	}
	mean := sum / float64(len(seconds))
	for i, b := range seconds {
		if b < mean*0.97 || b > mean*1.03 {
			return nil, fmt.Errorf("opus source %s is not flat: second %d is %.1f kbps, mean %.1f", path, i+1, b*8/1000, mean*8/1000)
		}
	}
	return packets, nil
}

// opusPacer sends the Opus packets in step with the source: packet k is due
// at k*20ms of publisher time, at RTP time k*960, looping over the file.
type opusPacer struct {
	packets [][]byte
	next    uint64
}

func (p *opusPacer) due(ts uint32, send func(rtpTime uint32, payload []byte)) {
	for ; p.next*20 <= uint64(ts); p.next++ {
		send(uint32(p.next*960), p.packets[p.next%uint64(len(p.packets))])
	}
}

// rtcReportInterval is how often the WHIP publisher sends a sender report of
// each track, as a browser does about each second. SRS converts WebRTC to
// RTMP only once two reports of each track put its RTP time on the wall clock.
const rtcReportInterval = time.Second

// rtpCount is a sent track's SSRC and its packets and payload bytes so far,
// as a sender report gives them.
type rtpCount struct {
	ssrc            uint32
	packets, octets uint32
}

func (c *rtpCount) sent(payload int) {
	c.packets++
	c.octets += uint32(payload)
}

// ntpTime is t as a 64-bit NTP time: seconds since 1900 and a 32-bit fraction.
func ntpTime(t time.Time) uint64 {
	const unixToNTP = 2208988800
	secs := uint64(t.Unix() + unixToNTP)
	frac := uint64(t.Nanosecond()) << 32 / 1_000_000_000
	return secs<<32 | frac
}

// senderReports are the video and audio sender reports at wall time now, with
// the publisher time at now, since the pace began at begun: RTP time 90 per
// ms for H.264 and 48 for Opus, the clocks the publisher stamps its packets
// with. It is not the newest tag's time, which a late send puts behind now:
// SRS takes each track's rate from its last two reports, so one late report
// would change the rate and shift the stream's timestamps.
func senderReports(now, begun time.Time, video, audio rtpCount) []rtcp.Packet {
	ntp := ntpTime(now)
	us := now.Sub(begun).Microseconds()
	return []rtcp.Packet{
		&rtcp.SenderReport{SSRC: video.ssrc, NTPTime: ntp, RTPTime: uint32(us * 90 / 1000), PacketCount: video.packets, OctetCount: video.octets},
		&rtcp.SenderReport{SSRC: audio.ssrc, NTPTime: ntp, RTPTime: uint32(us * 48 / 1000), PacketCount: audio.packets, OctetCount: audio.octets},
	}
}

// reportClock says when the next sender reports are due: with the first frame,
// then once per rtcReportInterval.
type reportClock struct {
	last time.Time
}

func (r *reportClock) due(now time.Time) bool {
	if !r.last.IsZero() && now.Sub(r.last) < rtcReportInterval {
		return false
	}
	r.last = now
	return true
}

// avcSequenceHeader is the SPS and PPS of the source's AVC sequence header.
func avcSequenceHeader(tags []rtmpMessage) (sps, pps []byte, err error) {
	for _, tag := range tags {
		if tag.typeID == 9 && len(tag.payload) > 5 && tag.payload[0]&0x0f == 7 && tag.payload[1] == 0 {
			return parseAVCConfig(tag.payload[5:])
		}
	}
	return nil, nil, errors.New("source has no AVC sequence header")
}

// publishWHIP publishes the source to /<app>/<stream> by WHIP: its H.264
// frames, with the SPS and PPS before each IDR, at RTP time 90 times the tag
// time, and the source's Opus track in step, paced and looped like the RTMP
// publisher. It reads and drops SRS's RTCP, such as a PLI for a new player;
// the source has an IDR every 2s. It sends a sender report of each track with
// the first frame and each second, the only RTCP it sends, so SRS can convert
// the stream to RTMP.
//
// It ends with an error when the peer connection fails while it sends, and
// deletes a resource SRS made for a connect that failed, so a publisher that
// tries again is not refused as busy.
func publishWHIP(apiAddr, app, stream, media string, tags []rtmpMessage, clock *mediaClock, stop <-chan struct{}) error {
	opus, err := sharedOpus(opusSourcePath(media))
	if err != nil {
		return err
	}
	sps, pps, err := avcSequenceHeader(tags)
	if err != nil {
		return err
	}
	encoder, err := (&format.H264{PayloadTyp: uint8(rtcVideoCodec.PayloadType), PacketizationMode: 1}).CreateEncoder()
	if err != nil {
		return err
	}
	video, err := webrtc.NewTrackLocalStaticRTP(rtcVideoCodec.RTPCodecCapability, "video", "srs-perf")
	if err != nil {
		return err
	}
	audio, err := webrtc.NewTrackLocalStaticRTP(rtcAudioCodec.RTPCodecCapability, "audio", "srs-perf")
	if err != nil {
		return err
	}
	s, err := newRTCSession()
	if err != nil {
		return err
	}
	defer s.pc.Close()
	var senders []*webrtc.RTPSender
	for _, track := range []webrtc.TrackLocal{video, audio} {
		tr, err := s.pc.AddTransceiverFromTrack(track, webrtc.RTPTransceiverInit{Direction: webrtc.RTPTransceiverDirectionSendonly})
		if err != nil {
			return err
		}
		senders = append(senders, tr.Sender())
		go func() {
			for {
				if _, _, err := tr.Sender().ReadRTCP(); err != nil {
					return
				}
			}
		}()
	}
	if err := s.connect(rtcURL(apiAddr, "whip", app, stream), 10*time.Second, stop); err != nil {
		s.delete()
		if errors.Is(err, errRTCStopped) {
			return nil
		}
		return err
	}
	pacer := &opusPacer{packets: opus}
	videoCount := rtpCount{ssrc: uint32(senders[0].GetParameters().Encodings[0].SSRC)}
	audioCount := rtpCount{ssrc: uint32(senders[1].GetParameters().Encodings[0].SSRC)}
	var reports reportClock
	var audioSeq uint16
	var sendErr error
	// A ramp starts the next step's publisher of the same stream as soon as
	// this one returns, so the resource is deleted first.
	err = paceSource(tags, clock, stop, func(tag rtmpMessage) error {
		if err := s.ended(); err != nil {
			return err
		}
		if now := time.Now(); reports.due(now) {
			if err := s.pc.WriteRTCP(senderReports(now, clock.begunAt(), videoCount, audioCount)); err != nil {
				return err
			}
		}
		pacer.due(tag.timestamp, func(rtpTime uint32, payload []byte) {
			audioSeq++
			if err := audio.WriteRTP(&rtp.Packet{Header: rtp.Header{Version: 2, PayloadType: uint8(rtcAudioCodec.PayloadType), SequenceNumber: audioSeq, Timestamp: rtpTime}, Payload: payload}); err != nil && sendErr == nil {
				sendErr = err
			}
			audioCount.sent(len(payload))
		})
		if sendErr != nil {
			return sendErr
		}
		if !isAVCFrame(tag) {
			return nil
		}
		nalus, err := avccNALUs(tag.payload[5:])
		if err != nil {
			return err
		}
		if tag.payload[0]>>4 == 1 {
			nalus = append([][]byte{sps, pps}, nalus...)
		}
		pkts, err := encoder.Encode(nalus)
		if err != nil {
			return err
		}
		for _, pkt := range pkts {
			pkt.Timestamp = tag.timestamp * 90
			if err := video.WriteRTP(pkt); err != nil {
				return err
			}
			videoCount.sent(len(pkt.Payload))
		}
		return nil
	})
	if deleteErr := s.delete(); err == nil {
		err = deleteErr
	}
	return err
}
