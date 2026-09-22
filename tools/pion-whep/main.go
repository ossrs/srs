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
	"sync/atomic"
	"syscall"
	"time"

	"github.com/pion/interceptor"
	"github.com/pion/webrtc/v4"
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

// stats counts what the player received, per kind, from every goroutine that reads a track.
type stats struct {
	videoPackets, audioPackets, bytes int64
}

func (s *stats) add(kind webrtc.RTPCodecType, n int) {
	if kind == webrtc.RTPCodecTypeVideo {
		atomic.AddInt64(&s.videoPackets, 1)
	} else {
		atomic.AddInt64(&s.audioPackets, 1)
	}
	atomic.AddInt64(&s.bytes, int64(n))
}

func (s *stats) snapshot() (video, audio, bytes int64) {
	return atomic.LoadInt64(&s.videoPackets), atomic.LoadInt64(&s.audioPackets), atomic.LoadInt64(&s.bytes)
}

func main() {
	o, err := parseOptions(os.Args[1:])
	if err != nil {
		fmt.Fprintf(os.Stderr, "pion-whep: %v\n", err)
		os.Exit(1)
	}
	if o.Rtx, err = rtxFromEnv(os.Getenv); err != nil {
		fmt.Fprintf(os.Stderr, "pion-whep: %v\n", err)
		os.Exit(1)
	}

	lg := newLogger(o.LogLevel)
	if !o.HideBanner {
		lg.printf("info", "pion-whep, a WHEP player with FFmpeg-style options, built with %s", pionVersion())
	}

	if err := play(o, lg); err != nil {
		lg.printf("error", "pion-whep: %v", err)
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

// play negotiates one WHEP session with the input URL, receives its tracks until -t elapses, a signal arrives or
// the connection fails, and prints the summary.
func play(o *options, lg *logger) error {
	whepURL := o.Inputs[0].Path

	mediaEngine := &webrtc.MediaEngine{}
	if err := mediaEngine.RegisterDefaultCodecs(); err != nil {
		return fmt.Errorf("register codecs: %w", err)
	}

	// The NACK logger goes first, so it sits inside the NACK generator's RTCP writer and closest to the wire on
	// the RTP readers; see nackLogger.
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

	if err := addRecvTransceivers(pc, o.Rtx); err != nil {
		return err
	}

	// failed receives the reason the session ended before its time.
	failed := make(chan error, 1)
	fail := func(err error) {
		select {
		case failed <- err:
		default:
		}
	}

	pc.OnICEConnectionStateChange(func(state webrtc.ICEConnectionState) {
		lg.printf("debug", "ICE connection state: %s", state)
	})
	pc.SCTP().Transport().OnStateChange(func(state webrtc.DTLSTransportState) {
		lg.printf("debug", "DTLS state: %s", state)
	})
	pc.OnConnectionStateChange(func(state webrtc.PeerConnectionState) {
		lg.printf("debug", "Peer connection state: %s", state)
		switch state {
		case webrtc.PeerConnectionStateFailed:
			fail(errors.New("peer connection failed"))
		case webrtc.PeerConnectionStateClosed:
			fail(errors.New("peer connection closed"))
		}
	})

	var st stats
	var trackIndex int32
	pc.OnTrack(func(track *webrtc.TrackRemote, receiver *webrtc.RTPReceiver) {
		index := atomic.AddInt32(&trackIndex, 1) - 1
		codec := track.Codec()
		lg.printf("info", "Stream #0:%d: %s: %s, pt=%d, ssrc=%d, clock=%d, rtx ssrc=%d",
			index, kindName(track.Kind()), codec.MimeType, codec.PayloadType, track.SSRC(), codec.ClockRate, track.RtxSSRC())

		// Drain RTCP so the interceptor chain keeps running for this receiver.
		go func() {
			for {
				if _, _, err := receiver.ReadRTCP(); err != nil {
					return
				}
			}
		}()
		go func() {
			for {
				pkt, _, err := track.ReadRTP()
				if err != nil {
					if !errors.Is(err, io.EOF) {
						lg.printf("debug", "Stream #0:%d read: %v", index, err)
					}
					return
				}
				st.add(track.Kind(), pkt.MarshalSize())
			}
		}()
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
	lg.printf("info", "Input #0, whep, from '%s'", whepURL)
	answerSDP, sessionURL, err := postOffer(whepURL, offerSDP)
	if err != nil {
		return err
	}
	lg.printf("debug", "Answer SDP:\n%s", answerSDP)
	// The session URL carries the SRS username, which a script passes to the NACK API to drop packets.
	lg.printf("debug", "Session URL: %s", sessionURL)

	if media, rtx, ok := rtxOfAnswer(answerSDP); ok {
		nackLog.SetRtx(media, rtx)
		lg.printf("debug", "RTX negotiated: FID media ssrc=%d, rtx ssrc=%d", media, rtx)
	} else {
		lg.printf("debug", "RTX not negotiated, retransmission is plain on the media SSRC")
	}

	if err := pc.SetRemoteDescription(webrtc.SessionDescription{Type: webrtc.SDPTypeAnswer, SDP: answerSDP}); err != nil {
		return fmt.Errorf("set remote description: %w", err)
	}

	err = wait(o.Duration, failed, lg, &st)
	pc.Close()
	deleteSession(sessionURL, lg)

	video, audio, bytes := st.snapshot()
	nacks, plain, rtx := nackLog.Stats()
	lg.printf("info", "Received video=%d, audio=%d packets, %d bytes, NACK sent=%d, recovered plain=%d, RTX=%d",
		video, audio, bytes, nacks, plain, rtx)
	if err != nil {
		return err
	}
	if video+audio == 0 {
		return errors.New("no RTP packet received")
	}
	return nil
}

// wait blocks until the duration elapses, zero meaning until interrupted, a signal arrives, or the session fails,
// printing the packet counts once a second so a script can see the stream flowing.
func wait(duration time.Duration, failed <-chan error, lg *logger, st *stats) error {
	signals := make(chan os.Signal, 1)
	signal.Notify(signals, os.Interrupt, syscall.SIGTERM)
	defer signal.Stop(signals)

	var deadline <-chan time.Time
	if duration > 0 {
		deadline = time.After(duration)
	}
	start := time.Now()
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			video, audio, bytes := st.snapshot()
			lg.printf("debug", "Received packets video=%d, audio=%d, bytes=%d, time=%.1fs",
				video, audio, bytes, time.Since(start).Seconds())
		case <-deadline:
			return nil
		case sig := <-signals:
			lg.printf("info", "Exiting on signal %s", sig)
			return nil
		case err := <-failed:
			return err
		}
	}
}

// postOffer sends the offer to the WHEP endpoint and returns the answer and the session URL from the Location
// header, resolved against the request URL, for the delete on exit.
func postOffer(whepURL, offer string) (answer, sessionURL string, err error) {
	client := &http.Client{Timeout: 10 * time.Second}
	req, err := http.NewRequest(http.MethodPost, whepURL, strings.NewReader(offer))
	if err != nil {
		return "", "", fmt.Errorf("whep request: %w", err)
	}
	req.Header.Set("Content-Type", "application/sdp")
	resp, err := client.Do(req)
	if err != nil {
		return "", "", fmt.Errorf("whep post %s: %w", whepURL, err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return "", "", fmt.Errorf("whep answer: %w", err)
	}
	if resp.StatusCode != http.StatusCreated && resp.StatusCode != http.StatusOK {
		return "", "", fmt.Errorf("whep post %s: %s: %s", whepURL, resp.Status, strings.TrimSpace(string(body)))
	}
	answer = string(body)
	if !strings.HasPrefix(answer, "v=0") {
		return "", "", fmt.Errorf("whep post %s: the answer is not an SDP: %s", whepURL, strings.TrimSpace(answer))
	}

	if location := resp.Header.Get("Location"); location != "" {
		if base, err := url.Parse(whepURL); err == nil {
			if ref, err := url.Parse(location); err == nil {
				sessionURL = base.ResolveReference(ref).String()
			}
		}
	}
	return answer, sessionURL, nil
}

// deleteSession tells the server the session is over, as WHEP asks; a failure only makes a debug line.
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

func kindName(kind webrtc.RTPCodecType) string {
	if kind == webrtc.RTPCodecTypeVideo {
		return "Video"
	}
	return "Audio"
}
