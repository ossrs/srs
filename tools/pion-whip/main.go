// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"os/signal"
	"runtime/debug"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/pion/interceptor"
	"github.com/pion/webrtc/v4"
	"github.com/pion/webrtc/v4/pkg/media"
)

// logger prints to stderr the lines at or below the -loglevel, as FFmpeg does.
type logger struct {
	level int
}

func newLogger(level string) *logger {
	return &logger{level: logLevelIndex(level)}
}

func (l *logger) printf(level, format string, args ...interface{}) {
	if logLevelIndex(level) > l.level {
		return
	}
	fmt.Fprintf(os.Stderr, format+"\n", args...)
}

// stats counts what the publisher sent, per kind, from the goroutine that reads the input.
type stats struct {
	videoFrames, audioFrames, bytes int64
}

func (s *stats) add(video bool, n int) {
	if video {
		atomic.AddInt64(&s.videoFrames, 1)
	} else {
		atomic.AddInt64(&s.audioFrames, 1)
	}
	atomic.AddInt64(&s.bytes, int64(n))
}

func (s *stats) snapshot() (video, audio, bytes int64) {
	return atomic.LoadInt64(&s.videoFrames), atomic.LoadInt64(&s.audioFrames), atomic.LoadInt64(&s.bytes)
}

func main() {
	o, err := parseOptions(os.Args[1:])
	if err != nil {
		fmt.Fprintf(os.Stderr, "pion-whip: %v\n", err)
		os.Exit(1)
	}
	if o.Rtx, err = rtxFromEnv(os.Getenv); err != nil {
		fmt.Fprintf(os.Stderr, "pion-whip: %v\n", err)
		os.Exit(1)
	}

	lg := newLogger(o.LogLevel)
	if !o.HideBanner {
		lg.printf("info", "pion-whip, a WHIP publisher with FFmpeg-style options, built with %s", pionVersion())
	}

	if err := publish(o, lg); err != nil {
		lg.printf("error", "pion-whip: %v", err)
		os.Exit(1)
	}
}

// pionVersion names the pion/webrtc module this binary was built with.
func pionVersion() string {
	if info, ok := debug.ReadBuildInfo(); ok {
		for _, dep := range info.Deps {
			if dep.Path == "github.com/pion/webrtc/v4" {
				return "pion/webrtc " + dep.Version
			}
		}
	}
	return "pion/webrtc"
}

// connectTimeout bounds the wait for ICE and DTLS after the answer, so a candidate this host cannot reach fails
// fast instead of publishing into the void.
const connectTimeout = 15 * time.Second

// publish negotiates one WHIP session with the output URL, sends the input's samples until the input ends, a signal
// arrives or the connection fails, and prints the summary.
func publish(o *options, lg *logger) error {
	in := o.Inputs[0]
	whipURL := o.Output

	file, err := os.Open(in.Path)
	if err != nil {
		return fmt.Errorf("open %s: %w", in.Path, err)
	}
	defer file.Close()
	reader, err := newMP4Reader(file)
	if err != nil {
		return fmt.Errorf("%s: %w", in.Path, err)
	}
	lg.printf("info", "Input #0, mp4, from '%s'", in.Path)

	mediaEngine, err := newMediaEngine(o.Rtx)
	if err != nil {
		return err
	}

	// The NACK logger goes first, so it sits between pion's NACK responder and the wire on the RTP writers and reads
	// the RTCP closest to the wire; see nackLogger.
	registry := &interceptor.Registry{}
	nackLog := newNackLogger(func(format string, args ...interface{}) {
		lg.printf("verbose", format, args...)
	})
	registry.Add(nackLog)
	if err := webrtc.RegisterDefaultInterceptors(mediaEngine, registry); err != nil {
		return fmt.Errorf("register interceptors: %w", err)
	}

	// Loopback candidates let the scripts run SRS with candidate 127.0.0.1.
	settings := webrtc.SettingEngine{}
	settings.SetIncludeLoopbackCandidate(true)

	api := webrtc.NewAPI(
		webrtc.WithMediaEngine(mediaEngine),
		webrtc.WithInterceptorRegistry(registry),
		webrtc.WithSettingEngine(settings),
	)
	pc, err := api.NewPeerConnection(webrtc.Configuration{})
	if err != nil {
		return fmt.Errorf("create peer connection: %w", err)
	}
	defer pc.Close()

	video, audio, err := newTracks()
	if err != nil {
		return err
	}
	senders, err := addSendonlyTracks(pc, video, audio)
	if err != nil {
		return err
	}
	// Drain the RTCP of each sender, or the interceptor chain never sees the NACKs and pion never answers them.
	for _, sender := range senders {
		go func(sender *webrtc.RTPSender) {
			buf := make([]byte, 1500)
			for {
				if _, _, err := sender.Read(buf); err != nil {
					return
				}
			}
		}(sender)
	}

	// failed receives the reason the session ended before its time; connected closes once media can flow.
	failed := make(chan error, 1)
	fail := func(err error) {
		select {
		case failed <- err:
		default:
		}
	}
	connected := make(chan struct{})
	var connectOnce sync.Once

	pc.OnICEConnectionStateChange(func(state webrtc.ICEConnectionState) {
		lg.printf("debug", "ICE connection state: %s", state)
	})
	pc.SCTP().Transport().OnStateChange(func(state webrtc.DTLSTransportState) {
		lg.printf("debug", "DTLS state: %s", state)
	})
	pc.OnConnectionStateChange(func(state webrtc.PeerConnectionState) {
		lg.printf("debug", "Peer connection state: %s", state)
		switch state {
		case webrtc.PeerConnectionStateConnected:
			connectOnce.Do(func() { close(connected) })
		case webrtc.PeerConnectionStateFailed:
			fail(errors.New("peer connection failed"))
		case webrtc.PeerConnectionStateClosed:
			fail(errors.New("peer connection closed"))
		}
	})

	offer, err := pc.CreateOffer(nil)
	if err != nil {
		return fmt.Errorf("create offer: %w", err)
	}
	gathered := webrtc.GatheringCompletePromise(pc)
	if err := pc.SetLocalDescription(offer); err != nil {
		return fmt.Errorf("set local description: %w", err)
	}
	<-gathered
	offerSDP := pc.LocalDescription().SDP
	lg.printf("debug", "Offer SDP:\n%s", offerSDP)

	if o.Rtx {
		lg.printf("info", "Retransmission: rtx offered, SRS chooses the format (%s=on)", envRtx)
	} else {
		lg.printf("info", "Retransmission: plain forced, rtx not offered (%s=off)", envRtx)
	}
	lg.printf("info", "Output #0, whip, to '%s'", whipURL)
	answerSDP, sessionURL, err := postOffer(whipURL, offerSDP)
	if err != nil {
		return err
	}
	lg.printf("debug", "Answer SDP:\n%s", answerSDP)
	// The session URL carries the SRS username, which a script passes to the NACK API to drop packets.
	lg.printf("debug", "Session URL: %s", sessionURL)

	if pt, apt, ok := rtxOfAnswer(answerSDP); ok {
		lg.printf("debug", "RTX negotiated: answer keeps rtx pt=%d apt=%d, retransmission is RTX on the RTX SSRC", pt, apt)
	} else {
		lg.printf("debug", "RTX not negotiated, retransmission is plain on the media SSRC")
	}

	if err := pc.SetRemoteDescription(webrtc.SessionDescription{Type: webrtc.SDPTypeAnswer, SDP: answerSDP}); err != nil {
		return fmt.Errorf("set remote description: %w", err)
	}

	// Send only once connected, so the first key frame is not dropped before DTLS is up.
	select {
	case <-connected:
	case err := <-failed:
		deleteSession(sessionURL, lg)
		return err
	case <-time.After(connectTimeout):
		deleteSession(sessionURL, lg)
		return fmt.Errorf("not connected after %s", connectTimeout)
	}

	var st stats
	stop := make(chan struct{})
	ended := make(chan error, 1)
	go func() { ended <- send(reader, in, video, audio, &st, stop) }()

	err = wait(failed, ended, lg, &st, nackLog)
	close(stop)
	pc.Close()
	deleteSession(sessionURL, lg)

	videoFrames, audioFrames, bytes := st.snapshot()
	packets, nacks, plain, rtx := nackLog.Stats()
	lg.printf("info", "Sent video=%d, audio=%d frames, %d packets, %d bytes, NACK received=%d, resent plain=%d, RTX=%d",
		videoFrames, audioFrames, packets, bytes, nacks, plain, rtx)
	if err != nil {
		return err
	}
	if packets == 0 {
		return errors.New("no RTP packet sent")
	}
	return nil
}

// send writes the input's samples to the tracks in decode order, paced by their decode times when -re is set, and
// rewinds the input as many times as -stream_loop asks. It returns nil when the input ends or stop closes.
func send(reader *mp4Reader, in inputOption, video, audio *webrtc.TrackLocalStaticSample, st *stats, stop <-chan struct{}) error {
	start := time.Now()
	loops := in.StreamLoop
	for {
		select {
		case <-stop:
			return nil
		default:
		}

		s, err := reader.Next()
		if errors.Is(err, io.EOF) {
			if loops == 0 {
				return nil
			}
			if loops > 0 {
				loops--
			}
			reader.Rewind()
			continue
		}
		if err != nil {
			return err
		}

		if in.Realtime {
			timer := time.NewTimer(time.Until(start.Add(ticksToDuration(s.DecodeTime, s.Timescale))))
			select {
			case <-timer.C:
			case <-stop:
				timer.Stop()
				return nil
			}
		}

		track := audio
		if s.Video {
			track = video
		}
		if err := track.WriteSample(media.Sample{Data: s.Data, Duration: ticksToDuration(uint64(s.Duration), s.Timescale)}); err != nil {
			return fmt.Errorf("write sample: %w", err)
		}
		st.add(s.Video, len(s.Data))
	}
}

// ticksToDuration converts ticks at timescale Hz without overflowing on long inputs.
func ticksToDuration(ticks uint64, timescale uint32) time.Duration {
	ts := uint64(timescale)
	return time.Duration(ticks/ts)*time.Second + time.Duration((ticks%ts)*uint64(time.Second)/ts)
}

// wait blocks until the input ends, a signal arrives or the session fails, printing the frame and packet counts
// once a second so a script can see the stream flowing.
func wait(failed <-chan error, ended <-chan error, lg *logger, st *stats, nackLog *nackLogger) error {
	signals := make(chan os.Signal, 1)
	signal.Notify(signals, os.Interrupt, syscall.SIGTERM)
	defer signal.Stop(signals)

	start := time.Now()
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			video, audio, bytes := st.snapshot()
			packets, _, _, _ := nackLog.Stats()
			lg.printf("debug", "Sent frames video=%d, audio=%d, packets=%d, bytes=%d, time=%.1fs",
				video, audio, packets, bytes, time.Since(start).Seconds())
		case sig := <-signals:
			lg.printf("info", "Exiting on signal %s", sig)
			return nil
		case err := <-failed:
			return err
		case err := <-ended:
			if err != nil {
				return err
			}
			lg.printf("info", "Input ended")
			return nil
		}
	}
}

// postOffer sends the offer to the WHIP endpoint and returns the answer and the session URL from the Location
// header, resolved against the request URL, for the delete on exit.
func postOffer(whipURL, offer string) (answer, sessionURL string, err error) {
	client := &http.Client{Timeout: 10 * time.Second}
	req, err := http.NewRequest(http.MethodPost, whipURL, strings.NewReader(offer))
	if err != nil {
		return "", "", fmt.Errorf("whip request: %w", err)
	}
	req.Header.Set("Content-Type", "application/sdp")
	resp, err := client.Do(req)
	if err != nil {
		return "", "", fmt.Errorf("whip post %s: %w", whipURL, err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return "", "", fmt.Errorf("whip answer: %w", err)
	}
	if resp.StatusCode != http.StatusCreated && resp.StatusCode != http.StatusOK {
		return "", "", fmt.Errorf("whip post %s: %s: %s", whipURL, resp.Status, strings.TrimSpace(string(body)))
	}
	answer = string(body)
	if !strings.HasPrefix(answer, "v=0") {
		return "", "", fmt.Errorf("whip post %s: the answer is not an SDP: %s", whipURL, strings.TrimSpace(answer))
	}

	if location := resp.Header.Get("Location"); location != "" {
		if base, err := url.Parse(whipURL); err == nil {
			if ref, err := url.Parse(location); err == nil {
				sessionURL = base.ResolveReference(ref).String()
			}
		}
	}
	return answer, sessionURL, nil
}

// deleteSession tells the server the session is over, as WHIP asks; a failure only makes a debug line.
func deleteSession(sessionURL string, lg *logger) {
	if sessionURL == "" {
		return
	}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, http.MethodDelete, sessionURL, nil)
	if err != nil {
		return
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		lg.printf("debug", "Session delete %s: %v", sessionURL, err)
		return
	}
	resp.Body.Close()
	lg.printf("debug", "Session delete %s: %s", sessionURL, resp.Status)
}
