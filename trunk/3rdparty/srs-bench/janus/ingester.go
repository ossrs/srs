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
package janus

import (
	"context"
	"io"
	"os"
	"strings"
	"time"

	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	"github.com/pion/interceptor"
	"github.com/pion/rtp"
	"github.com/pion/sdp/v3"
	"github.com/pion/webrtc/v3"
	"github.com/pion/webrtc/v3/pkg/media"
	"github.com/pion/webrtc/v3/pkg/media/h264reader"
	"github.com/pion/webrtc/v3/pkg/media/oggreader"
)

type videoIngester struct {
	sourceVideo       string
	fps               int
	markerInterceptor *rtpInterceptor
	sVideoTrack       *webrtc.TrackLocalStaticSample
	sVideoSender      *webrtc.RTPSender
	ready             context.Context
	readyCancel       context.CancelFunc
}

func newVideoIngester(sourceVideo string) *videoIngester {
	v := &videoIngester{markerInterceptor: &rtpInterceptor{}, sourceVideo: sourceVideo}
	v.ready, v.readyCancel = context.WithCancel(context.Background())
	return v
}

func (v *videoIngester) Close() error {
	v.readyCancel()
	if v.sVideoSender != nil {
		_ = v.sVideoSender.Stop()
	}
	return nil
}

func (v *videoIngester) AddTrack(pc *webrtc.PeerConnection, fps int) error {
	v.fps = fps

	mimeType, trackID := "video/H264", "video"
	if strings.HasSuffix(v.sourceVideo, ".ivf") {
		mimeType = "video/VP8"
	}

	var err error
	v.sVideoTrack, err = webrtc.NewTrackLocalStaticSample(
		webrtc.RTPCodecCapability{MimeType: mimeType, ClockRate: 90000}, trackID, "pion",
	)
	if err != nil {
		return errors.Wrapf(err, "Create video track")
	}

	v.sVideoSender, err = pc.AddTrack(v.sVideoTrack)
	if err != nil {
		return errors.Wrapf(err, "Add video track")
	}
	return err
}

func (v *videoIngester) Ingest(ctx context.Context) error {
	source, sender, track, fps := v.sourceVideo, v.sVideoSender, v.sVideoTrack, v.fps

	f, err := os.Open(source)
	if err != nil {
		return errors.Wrapf(err, "Open file %v", source)
	}
	defer f.Close()

	// TODO: FIXME: Support ivf for vp8.
	h264, err := h264reader.NewReader(f)
	if err != nil {
		return errors.Wrapf(err, "Open h264 %v", source)
	}

	enc := sender.GetParameters().Encodings[0]
	codec := sender.GetParameters().Codecs[0]
	headers := sender.GetParameters().HeaderExtensions
	logger.Tf(ctx, "Video %v, tbn=%v, fps=%v, ssrc=%v, pt=%v, header=%v",
		codec.MimeType, codec.ClockRate, fps, enc.SSRC, codec.PayloadType, headers)

	// OK, we are ready.
	v.readyCancel()

	clock := newWallClock()
	sampleDuration := time.Duration(uint64(time.Millisecond) * 1000 / uint64(fps))
	for ctx.Err() == nil {
		var sps, pps *h264reader.NAL
		var oFrames []*h264reader.NAL
		for ctx.Err() == nil {
			frame, err := h264.NextNAL()
			if err == io.EOF {
				return io.EOF
			}
			if err != nil {
				return errors.Wrapf(err, "Read h264")
			}

			oFrames = append(oFrames, frame)
			logger.If(ctx, "NALU %v PictureOrderCount=%v, ForbiddenZeroBit=%v, RefIdc=%v, %v bytes",
				frame.UnitType.String(), frame.PictureOrderCount, frame.ForbiddenZeroBit, frame.RefIdc, len(frame.Data))

			if frame.UnitType == h264reader.NalUnitTypeSPS {
				sps = frame
			} else if frame.UnitType == h264reader.NalUnitTypePPS {
				pps = frame
			} else {
				break
			}
		}

		var frames []*h264reader.NAL
		// Package SPS/PPS to STAP-A
		if sps != nil && pps != nil {
			stapA := packageAsSTAPA(sps, pps)
			frames = append(frames, stapA)
		}
		// Append other original frames.
		for _, frame := range oFrames {
			if frame.UnitType != h264reader.NalUnitTypeSPS && frame.UnitType != h264reader.NalUnitTypePPS {
				frames = append(frames, frame)
			}
		}

		// Covert frames to sample(buffers).
		for i, frame := range frames {
			sample := media.Sample{Data: frame.Data, Duration: sampleDuration}
			// Use the sample timestamp for frames.
			if i != len(frames)-1 {
				sample.Duration = 0
			}

			// For STAP-A, set marker to false, to make Chrome happy.
			if ri := v.markerInterceptor; ri.rtpWriter == nil {
				ri.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					// TODO: Should we decode to check whether SPS/PPS?
					if len(payload) > 0 && payload[0]&0x1f == 24 {
						header.Marker = false // 24, STAP-A
					}
					return ri.nextRTPWriter.Write(header, payload, attributes)
				}
			}

			if err = track.WriteSample(sample); err != nil {
				return errors.Wrapf(err, "Write sample")
			}
		}

		if d := clock.Tick(sampleDuration); d > 0 {
			time.Sleep(d)
		}
	}

	return ctx.Err()
}

type audioIngester struct {
	sourceAudio           string
	audioLevelInterceptor *rtpInterceptor
	sAudioTrack           *webrtc.TrackLocalStaticSample
	sAudioSender          *webrtc.RTPSender
	ready                 context.Context
	readyCancel           context.CancelFunc
}

func newAudioIngester(sourceAudio string) *audioIngester {
	v := &audioIngester{audioLevelInterceptor: &rtpInterceptor{}, sourceAudio: sourceAudio}
	v.ready, v.readyCancel = context.WithCancel(context.Background())
	return v
}

func (v *audioIngester) Close() error {
	v.readyCancel() // OK we are closed, also ready.

	if v.sAudioSender != nil {
		_ = v.sAudioSender.Stop()
	}
	return nil
}

func (v *audioIngester) AddTrack(pc *webrtc.PeerConnection) error {
	var err error

	mimeType, trackID := "audio/opus", "audio"
	v.sAudioTrack, err = webrtc.NewTrackLocalStaticSample(
		webrtc.RTPCodecCapability{MimeType: mimeType, ClockRate: 48000, Channels: 2}, trackID, "pion",
	)
	if err != nil {
		return errors.Wrapf(err, "Create audio track")
	}

	v.sAudioSender, err = pc.AddTrack(v.sAudioTrack)
	if err != nil {
		return errors.Wrapf(err, "Add audio track")
	}

	return nil
}

func (v *audioIngester) Ingest(ctx context.Context) error {
	source, sender, track := v.sourceAudio, v.sAudioSender, v.sAudioTrack

	f, err := os.Open(source)
	if err != nil {
		return errors.Wrapf(err, "Open file %v", source)
	}
	defer f.Close()

	ogg, _, err := oggreader.NewWith(f)
	if err != nil {
		return errors.Wrapf(err, "Open ogg %v", source)
	}

	enc := sender.GetParameters().Encodings[0]
	codec := sender.GetParameters().Codecs[0]
	headers := sender.GetParameters().HeaderExtensions
	logger.Tf(ctx, "Audio %v, tbn=%v, channels=%v, ssrc=%v, pt=%v, header=%v",
		codec.MimeType, codec.ClockRate, codec.Channels, enc.SSRC, codec.PayloadType, headers)

	// Whether should encode the audio-level in RTP header.
	var audioLevel *webrtc.RTPHeaderExtensionParameter
	for _, h := range headers {
		if h.URI == sdp.AudioLevelURI {
			audioLevel = &h
		}
	}

	// OK, we are ready.
	v.readyCancel()

	clock := newWallClock()
	var lastGranule uint64

	for ctx.Err() == nil {
		pageData, pageHeader, err := ogg.ParseNextPage()
		if err == io.EOF {
			return io.EOF
		}
		if err != nil {
			return errors.Wrapf(err, "Read ogg")
		}

		// The amount of samples is the difference between the last and current timestamp
		sampleCount := pageHeader.GranulePosition - lastGranule
		lastGranule = pageHeader.GranulePosition
		sampleDuration := time.Duration(uint64(time.Millisecond) * 1000 * sampleCount / uint64(codec.ClockRate))

		// For audio-level, set the extensions if negotiated.
		if ri := v.audioLevelInterceptor; ri.rtpWriter == nil {
			ri.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
				if audioLevel != nil {
					audioLevelPayload, err := new(rtp.AudioLevelExtension).Marshal()
					if err != nil {
						return 0, err
					}

					_ = header.SetExtension(uint8(audioLevel.ID), audioLevelPayload)
				}

				return ri.nextRTPWriter.Write(header, payload, attributes)
			}
		}

		if err = track.WriteSample(media.Sample{Data: pageData, Duration: sampleDuration}); err != nil {
			return errors.Wrapf(err, "Write sample")
		}

		if d := clock.Tick(sampleDuration); d > 0 {
			time.Sleep(d)
		}
	}

	return ctx.Err()
}
