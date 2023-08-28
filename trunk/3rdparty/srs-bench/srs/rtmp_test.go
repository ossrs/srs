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
	"bytes"
	"context"
	"fmt"
	"github.com/pkg/errors"
	"math/rand"
	"os"
	"sync"
	"testing"
	"time"

	"github.com/ossrs/go-oryx-lib/avc"
	"github.com/ossrs/go-oryx-lib/flv"
	"github.com/ossrs/go-oryx-lib/logger"
	"github.com/ossrs/go-oryx-lib/rtmp"
	"github.com/pion/interceptor"
)

func TestRtmpPublishPlay(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	var r0, r1 error
	err := func() error {
		publisher := NewRTMPPublisher()
		defer publisher.Close()

		player := NewRTMPPlayer()
		defer player.Close()

		// Connect to RTMP URL.
		streamSuffix := fmt.Sprintf("rtmp-regression-%v-%v", os.Getpid(), rand.Int())
		rtmpUrl := fmt.Sprintf("rtmp://%v/live/%v", *srsServer, streamSuffix)

		if err := publisher.Publish(ctx, rtmpUrl); err != nil {
			return err
		}

		if err := player.Play(ctx, rtmpUrl); err != nil {
			return err
		}

		// Check packets.
		var wg sync.WaitGroup
		defer wg.Wait()

		wg.Add(1)
		go func() {
			defer wg.Done()
			var nnPackets int
			player.onRecvPacket = func(m *rtmp.Message, a *flv.AudioFrame, v *flv.VideoFrame) error {
				logger.Tf(ctx, "got %v packet, %v %vms %vB",
					nnPackets, m.MessageType, m.Timestamp, len(m.Payload))
				if nnPackets += 1; nnPackets > 50 {
					cancel()
				}
				return nil
			}
			if r1 = player.Consume(ctx); r1 != nil {
				cancel()
			}
		}()

		wg.Add(1)
		go func() {
			defer wg.Done()
			publisher.onSendPacket = func(m *rtmp.Message) error {
				time.Sleep(1 * time.Millisecond)
				return nil
			}
			if r0 = publisher.Ingest(ctx, *srsPublishAvatar); r0 != nil {
				cancel()
			}
		}()

		return nil
	}()
	if err := filterTestError(ctx.Err(), err, r0, r1); err != nil {
		t.Errorf("err %+v", err)
	}
}

func TestRtmpPublish_RtcPlay(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	var r0, r1 error
	err := func() (err error) {
		streamSuffix := fmt.Sprintf("rtmp-regression-%v-%v", os.Getpid(), rand.Int())
		rtmpUrl := fmt.Sprintf("%v://%v%v-%v", srsSchema, *srsServer, *srsStream, streamSuffix)

		// Publisher connect to a RTMP stream.
		publisher := NewRTMPPublisher()
		defer publisher.Close()

		if err := publisher.Publish(ctx, rtmpUrl); err != nil {
			return err
		}

		// Setup the RTC player.
		var thePlayer *testPlayer
		if thePlayer, err = newTestPlayer(registerMiniCodecs, func(play *testPlayer) error {
			play.streamSuffix = streamSuffix
			var nnPlayReadRTP uint64
			return play.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
				api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
					i.rtpReader = func(payload []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
						nn, attr, err := i.nextRTPReader.Read(payload, attributes)
						if err != nil {
							return nn, attr, err
						}

						if nnPlayReadRTP++; nnPlayReadRTP >= uint64(*srsPlayOKPackets) {
							cancel() // Completed.
						}
						logger.Tf(ctx, "Play RECV RTP #%v %vB", nnPlayReadRTP, nn)
						return nn, attr, err
					}
				}))
			})
		}); err != nil {
			return err
		}
		defer thePlayer.Close()

		// Run publisher and players.
		var wg sync.WaitGroup
		defer wg.Wait()

		var playerIceReady context.Context
		playerIceReady, thePlayer.iceReadyCancel = context.WithCancel(ctx)

		wg.Add(1)
		go func() {
			defer wg.Done()
			if r1 = thePlayer.Run(logger.WithContext(ctx), cancel); r1 != nil {
				cancel()
			}
			logger.Tf(ctx, "player done")
		}()

		wg.Add(1)
		go func() {
			defer wg.Done()

			// Wait for player ready.
			select {
			case <-ctx.Done():
				return
			case <-playerIceReady.Done():
			}

			publisher.onSendPacket = func(m *rtmp.Message) error {
				time.Sleep(100 * time.Microsecond)
				return nil
			}
			if r0 = publisher.Ingest(ctx, *srsPublishAvatar); r0 != nil {
				cancel()
			}
			logger.Tf(ctx, "publisher done")
		}()

		return nil
	}()
	if err := filterTestError(ctx.Err(), err, r0, r1); err != nil {
		t.Errorf("err %+v", err)
	}
}

func TestRtmpPublish_MultipleSequences(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	var r0, r1, r2 error
	err := func() error {
		publisher := NewRTMPPublisher()
		defer publisher.Close()

		player := NewRTMPPlayer()
		defer player.Close()

		// Connect to RTMP URL.
		streamSuffix := fmt.Sprintf("rtmp-multi-spspps-%v-%v", os.Getpid(), rand.Int())
		rtmpUrl := fmt.Sprintf("rtmp://%v/live/%v", *srsServer, streamSuffix)

		if err := publisher.Publish(ctx, rtmpUrl); err != nil {
			return err
		}

		if err := player.Play(ctx, rtmpUrl); err != nil {
			return err
		}

		// Check packets.
		var wg sync.WaitGroup
		defer wg.Wait()

		wg.Add(1)
		go func() {
			defer wg.Done()
			var nnPackets int
			var previousAvccr *avc.AVCDecoderConfigurationRecord
			player.onRecvPacket = func(m *rtmp.Message, a *flv.AudioFrame, v *flv.VideoFrame) error {
				if m.MessageType == rtmp.MessageTypeAudio || v.FrameType != flv.VideoFrameTypeKeyframe ||
					v.Trait != flv.VideoFrameTraitSequenceHeader {
					return nil
				}

				avccr := avc.NewAVCDecoderConfigurationRecord()
				if err := avccr.UnmarshalBinary(v.Raw); err != nil {
					return err
				}

				// Ingore the duplicated sps/pps.
				if IsAvccrEquals(previousAvccr, avccr) {
					return nil
				}
				previousAvccr = avccr

				logger.Tf(ctx, "got %v sps/pps, %v %vms %vB, sps=%v, pps=%v, %v, %v",
					nnPackets, m.MessageType, m.Timestamp, len(m.Payload), len(avccr.SequenceParameterSetNALUnits),
					len(avccr.PictureParameterSetNALUnits), avccr.AVCProfileIndication, avccr.AVCLevelIndication)
				if nnPackets++; nnPackets >= 2 {
					cancel()
				}
				return nil
			}
			if r1 = player.Consume(ctx); r1 != nil {
				cancel()
			}
		}()

		wg.Add(1)
		go func() {
			defer wg.Done()
			var nnPackets int
			ctxAvatar, cancelAvatar := context.WithCancel(ctx)
			publisher.onSendPacket = func(m *rtmp.Message) error {
				if m.MessageType == rtmp.MessageTypeVideo {
					nnPackets++
				}
				if nnPackets > 10 {
					cancelAvatar()
				}
				return nil
			}

			publisher.closeTransportWhenIngestDone = false
			if r0 = publisher.Ingest(ctxAvatar, *srsPublishBBB); r0 != nil {
				cancel()
			}

			publisher.closeTransportWhenIngestDone = true
			if r2 = publisher.Ingest(ctx, *srsPublishAvatar); r2 != nil {
				cancel()
			}
		}()

		return nil
	}()
	if err := filterTestError(ctx.Err(), err, r0, r1, r2); err != nil {
		t.Errorf("err %+v", err)
	}
}

func TestRtmpPublish_MultipleSequences_RtcPlay(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	var r0, r1, r2 error
	err := func() (err error) {
		streamSuffix := fmt.Sprintf("rtmp-regression-%v-%v", os.Getpid(), rand.Int())
		rtmpUrl := fmt.Sprintf("%v://%v%v-%v", srsSchema, *srsServer, *srsStream, streamSuffix)

		// Publisher connect to a RTMP stream.
		publisher := NewRTMPPublisher()
		defer publisher.Close()

		if err := publisher.Publish(ctx, rtmpUrl); err != nil {
			return err
		}

		// Setup the RTC player.
		var thePlayer *testPlayer
		if thePlayer, err = newTestPlayer(registerMiniCodecs, func(play *testPlayer) error {
			play.streamSuffix = streamSuffix
			var nnSpsPps uint64
			var previousSpsPps []byte
			return play.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
				api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
					i.rtpReader = func(payload []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
						nn, attr, err := i.nextRTPReader.Read(payload, attributes)
						if err != nil {
							return nn, attr, err
						}

						annexb, nalus, err := DemuxRtpSpsPps(payload[:nn])
						if err != nil || len(nalus) == 0 ||
							(nalus[0].NALUType != avc.NALUTypeSPS && nalus[0].NALUType != avc.NALUTypePPS) ||
							bytes.Equal(annexb, previousSpsPps) {
							return nn, attr, err
						}

						previousSpsPps = annexb
						if nnSpsPps++; nnSpsPps >= 2 {
							cancel() // Completed.
						}
						logger.Tf(ctx, "Play RECV SPS/PPS #%v %vB %v", nnSpsPps, nn, nalus[0].NALUType)
						return nn, attr, err
					}
				}))
			})
		}); err != nil {
			return err
		}
		defer thePlayer.Close()

		// Run publisher and players.
		var wg sync.WaitGroup
		defer wg.Wait()

		var playerIceReady context.Context
		playerIceReady, thePlayer.iceReadyCancel = context.WithCancel(ctx)

		wg.Add(1)
		go func() {
			defer wg.Done()
			if r1 = thePlayer.Run(logger.WithContext(ctx), cancel); r1 != nil {
				cancel()
			}
			logger.Tf(ctx, "player done")
		}()

		wg.Add(1)
		go func() {
			defer wg.Done()

			// Wait for player ready.
			select {
			case <-ctx.Done():
				return
			case <-playerIceReady.Done():
			}

			var nnPackets int
			ctxAvatar, cancelAvatar := context.WithCancel(ctx)
			publisher.onSendPacket = func(m *rtmp.Message) error {
				if m.MessageType == rtmp.MessageTypeVideo {
					nnPackets++
				}
				if nnPackets > 10 {
					cancelAvatar()
				}
				return nil
			}

			publisher.closeTransportWhenIngestDone = false
			if r0 = publisher.Ingest(ctxAvatar, *srsPublishBBB); r0 != nil {
				cancel()
			}

			publisher.closeTransportWhenIngestDone = true
			if r2 = publisher.Ingest(ctx, *srsPublishAvatar); r2 != nil {
				cancel()
			}
			logger.Tf(ctx, "publisher done")
		}()

		return nil
	}()
	if err := filterTestError(ctx.Err(), err, r0, r1, r2); err != nil {
		t.Errorf("err %+v", err)
	}
}

func TestRtmpPublish_HttpFlvPlay(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	var r0, r1 error
	err := func() error {
		publisher := NewRTMPPublisher()
		defer publisher.Close()

		player := NewFLVPlayer()
		defer player.Close()

		// Connect to RTMP URL.
		streamSuffix := fmt.Sprintf("rtmp-regression-%v-%v", os.Getpid(), rand.Int())
		rtmpUrl := fmt.Sprintf("rtmp://%v/live/%v", *srsServer, streamSuffix)
		flvUrl := fmt.Sprintf("http://%v/live/%v.flv", *srsHttpServer, streamSuffix)

		if err := publisher.Publish(ctx, rtmpUrl); err != nil {
			return err
		}

		if err := player.Play(ctx, flvUrl); err != nil {
			return err
		}

		// Check packets.
		var wg sync.WaitGroup
		defer wg.Wait()

		publisherReady, publisherReadyCancel := context.WithCancel(context.Background())
		wg.Add(1)
		go func() {
			defer wg.Done()
			time.Sleep(30 * time.Millisecond) // Wait for publisher to push sequence header.
			publisherReadyCancel()
		}()

		wg.Add(1)
		go func() {
			defer wg.Done()
			<-publisherReady.Done()

			var nnPackets int
			player.onRecvHeader = func(hasAudio, hasVideo bool) error {
				return nil
			}
			player.onRecvTag = func(tp flv.TagType, size, ts uint32, tag []byte) error {
				logger.Tf(ctx, "got %v tag, %v %vms %vB", nnPackets, tp, ts, len(tag))
				if nnPackets += 1; nnPackets > 50 {
					cancel()
				}
				return nil
			}
			if r1 = player.Consume(ctx); r1 != nil {
				cancel()
			}
		}()

		wg.Add(1)
		go func() {
			defer wg.Done()
			publisher.onSendPacket = func(m *rtmp.Message) error {
				time.Sleep(1 * time.Millisecond)
				return nil
			}
			if r0 = publisher.Ingest(ctx, *srsPublishAvatar); r0 != nil {
				cancel()
			}
		}()

		return nil
	}()
	if err := filterTestError(ctx.Err(), err, r0, r1); err != nil {
		t.Errorf("err %+v", err)
	}
}

func TestRtmpPublish_HttpFlvPlayNoAudio(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	var r0, r1 error
	err := func() error {
		publisher := NewRTMPPublisher()
		defer publisher.Close()

		// Set publisher to drop audio.
		publisher.hasAudio = false

		player := NewFLVPlayer()
		defer player.Close()

		// Connect to RTMP URL.
		streamSuffix := fmt.Sprintf("rtmp-regression-%v-%v", os.Getpid(), rand.Int())
		rtmpUrl := fmt.Sprintf("rtmp://%v/live/%v", *srsServer, streamSuffix)
		flvUrl := fmt.Sprintf("http://%v/live/%v.flv", *srsHttpServer, streamSuffix)

		if err := publisher.Publish(ctx, rtmpUrl); err != nil {
			return err
		}

		if err := player.Play(ctx, flvUrl); err != nil {
			return err
		}

		// Check packets.
		var wg sync.WaitGroup
		defer wg.Wait()

		publisherReady, publisherReadyCancel := context.WithCancel(context.Background())
		wg.Add(1)
		go func() {
			defer wg.Done()
			time.Sleep(30 * time.Millisecond) // Wait for publisher to push sequence header.
			publisherReadyCancel()
		}()

		wg.Add(1)
		go func() {
			defer wg.Done()
			<-publisherReady.Done()

			var nnPackets int
			player.onRecvHeader = func(hasAudio, hasVideo bool) error {
				return nil
			}
			player.onRecvTag = func(tp flv.TagType, size, ts uint32, tag []byte) error {
				if tp == flv.TagTypeAudio {
					return errors.New("should no audio")
				}
				logger.Tf(ctx, "got %v tag, %v %vms %vB", nnPackets, tp, ts, len(tag))
				if nnPackets += 1; nnPackets > 50 {
					cancel()
				}
				return nil
			}
			if r1 = player.Consume(ctx); r1 != nil {
				cancel()
			}
		}()

		wg.Add(1)
		go func() {
			defer wg.Done()
			publisher.onSendPacket = func(m *rtmp.Message) error {
				time.Sleep(1 * time.Millisecond)
				return nil
			}
			if r0 = publisher.Ingest(ctx, *srsPublishAvatar); r0 != nil {
				cancel()
			}
		}()

		return nil
	}()
	if err := filterTestError(ctx.Err(), err, r0, r1); err != nil {
		t.Errorf("err %+v", err)
	}
}

func TestRtmpPublish_HttpFlvPlayNoVideo(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	var r0, r1 error
	err := func() error {
		publisher := NewRTMPPublisher()
		defer publisher.Close()

		// Set publisher to drop video.
		publisher.hasVideo = false

		player := NewFLVPlayer()
		defer player.Close()

		// Connect to RTMP URL.
		streamSuffix := fmt.Sprintf("rtmp-regression-%v-%v", os.Getpid(), rand.Int())
		rtmpUrl := fmt.Sprintf("rtmp://%v/live/%v", *srsServer, streamSuffix)
		flvUrl := fmt.Sprintf("http://%v/live/%v.flv", *srsHttpServer, streamSuffix)

		if err := publisher.Publish(ctx, rtmpUrl); err != nil {
			return err
		}

		if err := player.Play(ctx, flvUrl); err != nil {
			return err
		}

		// Check packets.
		var wg sync.WaitGroup
		defer wg.Wait()

		publisherReady, publisherReadyCancel := context.WithCancel(context.Background())
		wg.Add(1)
		go func() {
			defer wg.Done()
			time.Sleep(30 * time.Millisecond) // Wait for publisher to push sequence header.
			publisherReadyCancel()
		}()

		wg.Add(1)
		go func() {
			defer wg.Done()
			<-publisherReady.Done()

			var nnPackets int
			player.onRecvHeader = func(hasAudio, hasVideo bool) error {
				return nil
			}
			player.onRecvTag = func(tp flv.TagType, size, ts uint32, tag []byte) error {
				if tp == flv.TagTypeVideo {
					return errors.New("should no video")
				}
				logger.Tf(ctx, "got %v tag, %v %vms %vB", nnPackets, tp, ts, len(tag))
				if nnPackets += 1; nnPackets > 50 {
					cancel()
				}
				return nil
			}
			if r1 = player.Consume(ctx); r1 != nil {
				cancel()
			}
		}()

		wg.Add(1)
		go func() {
			defer wg.Done()
			publisher.onSendPacket = func(m *rtmp.Message) error {
				time.Sleep(1 * time.Millisecond)
				return nil
			}
			if r0 = publisher.Ingest(ctx, *srsPublishAvatar); r0 != nil {
				cancel()
			}
		}()

		return nil
	}()
	if err := filterTestError(ctx.Err(), err, r0, r1); err != nil {
		t.Errorf("err %+v", err)
	}
}
