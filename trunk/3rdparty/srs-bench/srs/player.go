// The MIT License (MIT)
//
// # Copyright (c) 2021 Winlin
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
package srs

import (
	"context"
	"fmt"
	"strings"
	"sync"
	"time"

	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	"github.com/pion/interceptor"
	"github.com/pion/rtcp"
	"github.com/pion/sdp/v3"
	"github.com/pion/webrtc/v3"
	"github.com/pion/webrtc/v3/pkg/media"
	"github.com/pion/webrtc/v3/pkg/media/h264writer"
	"github.com/pion/webrtc/v3/pkg/media/ivfwriter"
	"github.com/pion/webrtc/v3/pkg/media/oggwriter"
)

// @see https://github.com/pion/webrtc/blob/master/examples/save-to-disk/main.go
func startPlay(ctx context.Context, r, dumpAudio, dumpVideo string, enableAudioLevel, enableTWCC bool, pli int) error {
	ctx = logger.WithContext(ctx)

	logger.Tf(ctx, "Run play url=%v, audio=%v, video=%v, audio-level=%v, twcc=%v",
		r, dumpAudio, dumpVideo, enableAudioLevel, enableTWCC)

	// For audio-level.
	webrtcNewPeerConnection := func(configuration webrtc.Configuration) (*webrtc.PeerConnection, error) {
		m := &webrtc.MediaEngine{}
		if err := m.RegisterDefaultCodecs(); err != nil {
			return nil, err
		}

		for _, extension := range []string{sdp.SDESMidURI, sdp.SDESRTPStreamIDURI, sdp.TransportCCURI} {
			if extension == sdp.TransportCCURI && !enableTWCC {
				continue
			}
			if err := m.RegisterHeaderExtension(webrtc.RTPHeaderExtensionCapability{URI: extension}, webrtc.RTPCodecTypeVideo); err != nil {
				return nil, err
			}
		}

		// https://github.com/pion/ion/issues/130
		// https://github.com/pion/ion-sfu/pull/373/files#diff-6f42c5ac6f8192dd03e5a17e9d109e90cb76b1a4a7973be6ce44a89ffd1b5d18R73
		for _, extension := range []string{sdp.SDESMidURI, sdp.SDESRTPStreamIDURI, sdp.AudioLevelURI} {
			if extension == sdp.AudioLevelURI && !enableAudioLevel {
				continue
			}
			if err := m.RegisterHeaderExtension(webrtc.RTPHeaderExtensionCapability{URI: extension}, webrtc.RTPCodecTypeAudio); err != nil {
				return nil, err
			}
		}

		i := &interceptor.Registry{}
		if err := webrtc.RegisterDefaultInterceptors(m, i); err != nil {
			return nil, err
		}

		api := webrtc.NewAPI(webrtc.WithMediaEngine(m), webrtc.WithInterceptorRegistry(i))
		return api.NewPeerConnection(configuration)
	}

	pc, err := webrtcNewPeerConnection(webrtc.Configuration{})
	if err != nil {
		return errors.Wrapf(err, "Create PC")
	}

	var receivers []*webrtc.RTPReceiver
	defer func() {
		pc.Close()
		for _, receiver := range receivers {
			receiver.Stop()
		}
	}()

	pc.AddTransceiverFromKind(webrtc.RTPCodecTypeAudio, webrtc.RTPTransceiverInit{
		Direction: webrtc.RTPTransceiverDirectionRecvonly,
	})
	pc.AddTransceiverFromKind(webrtc.RTPCodecTypeVideo, webrtc.RTPTransceiverInit{
		Direction: webrtc.RTPTransceiverDirectionRecvonly,
	})

	offer, err := pc.CreateOffer(nil)
	if err != nil {
		return errors.Wrapf(err, "Create Offer")
	}

	if err := pc.SetLocalDescription(offer); err != nil {
		return errors.Wrapf(err, "Set offer %v", offer)
	}

	answer, err := apiRtcRequest(ctx, "/rtc/v1/play", r, offer.SDP)
	if err != nil {
		return errors.Wrapf(err, "Api request offer=%v", offer.SDP)
	}

	if err := pc.SetRemoteDescription(webrtc.SessionDescription{
		Type: webrtc.SDPTypeAnswer, SDP: answer,
	}); err != nil {
		return errors.Wrapf(err, "Set answer %v", answer)
	}

	var da media.Writer
	var dv_vp8 media.Writer
	var dv_h264 media.Writer
	defer func() {
		if da != nil {
			da.Close()
		}
		if dv_vp8 != nil {
			dv_vp8.Close()
		}
		if dv_h264 != nil {
			dv_h264.Close()
		}
	}()

	handleTrack := func(ctx context.Context, track *webrtc.TrackRemote, receiver *webrtc.RTPReceiver) error {
		// Send a PLI on an interval so that the publisher is pushing a keyframe
		go func() {
			if track.Kind() == webrtc.RTPCodecTypeAudio {
				return
			}

			if pli <= 0 {
				return
			}

			for {
				select {
				case <-ctx.Done():
					return
				case <-time.After(time.Duration(pli) * time.Second):
					_ = pc.WriteRTCP([]rtcp.Packet{&rtcp.PictureLossIndication{
						MediaSSRC: uint32(track.SSRC()),
					}})
				}
			}
		}()

		receivers = append(receivers, receiver)

		codec := track.Codec()

		trackDesc := fmt.Sprintf("channels=%v", codec.Channels)
		if track.Kind() == webrtc.RTPCodecTypeVideo {
			trackDesc = fmt.Sprintf("fmtp=%v", codec.SDPFmtpLine)
		}
		if headers := receiver.GetParameters().HeaderExtensions; len(headers) > 0 {
			trackDesc = fmt.Sprintf("%v, header=%v", trackDesc, headers)
		}
		logger.Tf(ctx, "Got track %v, pt=%v, tbn=%v, %v",
			codec.MimeType, codec.PayloadType, codec.ClockRate, trackDesc)

		if codec.MimeType == "audio/opus" {
			if da == nil && dumpAudio != "" {
				if da, err = oggwriter.New(dumpAudio, codec.ClockRate, codec.Channels); err != nil {
					return errors.Wrapf(err, "New audio dumper")
				}
				logger.Tf(ctx, "Open ogg writer file=%v, tbn=%v, channels=%v",
					dumpAudio, codec.ClockRate, codec.Channels)
			}

			if err = writeTrackToDisk(ctx, da, track); err != nil {
				return errors.Wrapf(err, "Write audio disk")
			}
		} else if codec.MimeType == "video/VP8" {
			if dumpVideo != "" && !strings.HasSuffix(dumpVideo, ".ivf") {
				return errors.Errorf("%v should be .ivf for VP8", dumpVideo)
			}

			if dv_vp8 == nil && dumpVideo != "" {
				if dv_vp8, err = ivfwriter.New(dumpVideo); err != nil {
					return errors.Wrapf(err, "New video dumper")
				}
				logger.Tf(ctx, "Open ivf writer file=%v", dumpVideo)
			}

			if err = writeTrackToDisk(ctx, dv_vp8, track); err != nil {
				return errors.Wrapf(err, "Write video disk")
			}
		} else if codec.MimeType == "video/H264" {
			if dumpVideo != "" && !strings.HasSuffix(dumpVideo, ".h264") {
				return errors.Errorf("%v should be .h264 for H264", dumpVideo)
			}

			if dv_h264 == nil && dumpVideo != "" {
				if dv_h264, err = h264writer.New(dumpVideo); err != nil {
					return errors.Wrapf(err, "New video dumper")
				}
				logger.Tf(ctx, "Open h264 writer file=%v", dumpVideo)
			}

			if err = writeTrackToDisk(ctx, dv_h264, track); err != nil {
				return errors.Wrapf(err, "Write video disk")
			}
		} else {
			logger.Wf(ctx, "Ignore track %v pt=%v", codec.MimeType, codec.PayloadType)
		}
		return nil
	}

	ctx, cancel := context.WithCancel(ctx)
	pc.OnTrack(func(track *webrtc.TrackRemote, receiver *webrtc.RTPReceiver) {
		err = handleTrack(ctx, track, receiver)
		if err != nil {
			codec := track.Codec()
			err = errors.Wrapf(err, "Handle  track %v, pt=%v", codec.MimeType, codec.PayloadType)
			cancel()
		}
	})

	pc.OnICEConnectionStateChange(func(state webrtc.ICEConnectionState) {
		logger.If(ctx, "ICE state %v", state)

		if state == webrtc.ICEConnectionStateFailed || state == webrtc.ICEConnectionStateClosed {
			if ctx.Err() != nil {
				return
			}

			logger.Wf(ctx, "Close for ICE state %v", state)
			cancel()
		}
	})

	// Wait for event from context or tracks.
	var wg sync.WaitGroup

	wg.Add(1)
	go func() {
		defer wg.Done()

		for {
			select {
			case <-ctx.Done():
				return
			case <-time.After(5 * time.Second):
				gStatRTC.PeerConnection = pc.GetStats()
			}
		}
	}()

	wg.Wait()
	return err
}

func writeTrackToDisk(ctx context.Context, w media.Writer, track *webrtc.TrackRemote) error {
	for ctx.Err() == nil {
		pkt, _, err := track.ReadRTP()
		if err != nil {
			if ctx.Err() != nil {
				return nil
			}
			return errors.Wrapf(err, "Read RTP")
		}

		if w == nil {
			continue
		}

		if err := w.WriteRTP(pkt); err != nil {
			if len(pkt.Payload) <= 2 {
				continue
			}
			logger.Wf(ctx, "Ignore write RTP %vB err %+v", len(pkt.Payload), err)
		}
	}

	return ctx.Err()
}
