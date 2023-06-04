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
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"math/rand"
	"net/http"
	"os"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/pion/transport/v2/vnet"
	"github.com/pion/webrtc/v3"

	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/flv"
	"github.com/ossrs/go-oryx-lib/logger"
	"github.com/pion/interceptor"
	"github.com/pion/rtcp"
	"github.com/pion/rtp"
)

// Test for https://github.com/ossrs/srs/pull/2483
func TestPR2483_RtcStatApi_PublisherOnly(t *testing.T) {
	if err := filterTestError(func() error {
		streamSuffix := fmt.Sprintf("publish-only-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			var once sync.Once
			api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
				i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
					once.Do(func() {
						stat := newStatAPI(ctx).Streams().FilterByStreamSuffix(p.streamSuffix)
						logger.Tf(ctx, "Check publishing, streams=%v, stream=%v", len(stat.streams), stat.stream)
						if stat.stream != nil {
							cancel() // done
						}
					})
					return i.nextRTCPReader.Read(buf, attributes)
				}
			}))
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()); err != nil {
		t.Errorf("err %+v", err)
	}
}

// Veirfy https://github.com/ossrs/srs/issues/2371
func TestBugfix2371_PublishWithNack(t *testing.T) {
	ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
	if err := filterTestError(func() error {
		streamSuffix := fmt.Sprintf("bugfix-2371-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerMiniCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = func(s *webrtc.SessionDescription) error {
				if n := strings.Count(s.SDP, "nack"); n != 2 {
					return errors.Errorf("invalid %v nack", n)
				}
				return nil
			}
			p.onAnswer = func(s *webrtc.SessionDescription) error {
				if n := strings.Count(s.SDP, "nack"); n != 2 {
					return errors.Errorf("invalid %v nack", n)
				}
				cancel()
				return nil
			}
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		if err := p.Setup(*srsVnetClientIP); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()); err != nil {
		t.Errorf("err %+v", err)
	}
}

// Veirfy https://github.com/ossrs/srs/issues/2371
func TestBugfix2371_PublishWithoutNack(t *testing.T) {
	ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
	if err := filterTestError(func() error {
		streamSuffix := fmt.Sprintf("bugfix-2371-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerMiniCodecsWithoutNack, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = func(s *webrtc.SessionDescription) error {
				if n := strings.Count(s.SDP, "nack"); n != 0 {
					return errors.Errorf("invalid %v nack", n)
				}
				return nil
			}
			p.onAnswer = func(s *webrtc.SessionDescription) error {
				if n := strings.Count(s.SDP, "nack"); n != 0 {
					return errors.Errorf("invalid %v nack", n)
				}
				cancel()
				return nil
			}
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		if err := p.Setup(*srsVnetClientIP); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()); err != nil {
		t.Errorf("err %+v", err)
	}
}

// Veirfy https://github.com/ossrs/srs/issues/2371
func TestBugfix2371_PlayWithNack(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	var r0, r1, r2, r3 error
	defer func(ctx context.Context) {
		if err := filterTestError(ctx.Err(), r0, r1, r2, r3); err != nil {
			t.Errorf("Fail for err %+v", err)
		} else {
			logger.Tf(ctx, "test done with err %+v", err)
		}
	}(ctx)

	var resources []io.Closer
	defer func() {
		for _, resource := range resources {
			_ = resource.Close()
		}
	}()

	var wg sync.WaitGroup
	defer wg.Wait()

	// The event notify.
	var thePublisher *testPublisher
	var thePlayer *testPlayer

	mainReady, mainReadyCancel := context.WithCancel(context.Background())
	publishReady, publishReadyCancel := context.WithCancel(context.Background())

	// Objects init.
	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		doInit := func() (err error) {
			streamSuffix := fmt.Sprintf("basic-publish-play-%v-%v", os.Getpid(), rand.Int())

			// Initialize player with private api.
			if thePlayer, err = newTestPlayer(registerMiniCodecs, func(play *testPlayer) error {
				play.streamSuffix = streamSuffix
				play.onOffer = func(s *webrtc.SessionDescription) error {
					if n := strings.Count(s.SDP, "nack"); n != 2 {
						return errors.Errorf("invalid %v nack", n)
					}
					return nil
				}
				play.onAnswer = func(s *webrtc.SessionDescription) error {
					if n := strings.Count(s.SDP, "nack"); n != 2 {
						return errors.Errorf("invalid %v nack", n)
					}
					cancel()
					return nil
				}
				resources = append(resources, play)
				return play.Setup(*srsVnetClientIP)
			}); err != nil {
				return err
			}

			// Initialize publisher with private api.
			if thePublisher, err = newTestPublisher(registerMiniCodecs, func(pub *testPublisher) error {
				pub.streamSuffix = streamSuffix
				pub.iceReadyCancel = publishReadyCancel
				resources = append(resources, pub)
				return pub.Setup(*srsVnetClientIP)
			}); err != nil {
				return err
			}

			// Init done.
			mainReadyCancel()

			<-ctx.Done()
			return nil
		}

		if err := doInit(); err != nil {
			r1 = err
		}
	}()

	// Run publisher.
	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		select {
		case <-ctx.Done():
		case <-mainReady.Done():
			r2 = thePublisher.Run(logger.WithContext(ctx), cancel)
			logger.Tf(ctx, "pub done")
		}
	}()

	// Run player.
	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		select {
		case <-ctx.Done():
		case <-publishReady.Done():
			r3 = thePlayer.Run(logger.WithContext(ctx), cancel)
			logger.Tf(ctx, "play done")
		}
	}()
}

// Veirfy https://github.com/ossrs/srs/issues/2371
func TestBugfix2371_PlayWithoutNack(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	var r0, r1, r2, r3 error
	defer func(ctx context.Context) {
		if err := filterTestError(ctx.Err(), r0, r1, r2, r3); err != nil {
			t.Errorf("Fail for err %+v", err)
		} else {
			logger.Tf(ctx, "test done with err %+v", err)
		}
	}(ctx)

	var resources []io.Closer
	defer func() {
		for _, resource := range resources {
			_ = resource.Close()
		}
	}()

	var wg sync.WaitGroup
	defer wg.Wait()

	// The event notify.
	var thePublisher *testPublisher
	var thePlayer *testPlayer

	mainReady, mainReadyCancel := context.WithCancel(context.Background())
	publishReady, publishReadyCancel := context.WithCancel(context.Background())

	// Objects init.
	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		doInit := func() (err error) {
			streamSuffix := fmt.Sprintf("basic-publish-play-%v-%v", os.Getpid(), rand.Int())

			// Initialize player with private api.
			if thePlayer, err = newTestPlayer(registerMiniCodecsWithoutNack, func(play *testPlayer) error {
				play.streamSuffix = streamSuffix
				play.onOffer = func(s *webrtc.SessionDescription) error {
					if n := strings.Count(s.SDP, "nack"); n != 0 {
						return errors.Errorf("invalid %v nack", n)
					}
					return nil
				}
				play.onAnswer = func(s *webrtc.SessionDescription) error {
					if n := strings.Count(s.SDP, "nack"); n != 0 {
						return errors.Errorf("invalid %v nack", n)
					}
					cancel()
					return nil
				}
				resources = append(resources, play)
				return play.Setup(*srsVnetClientIP)
			}); err != nil {
				return err
			}

			// Initialize publisher with private api.
			if thePublisher, err = newTestPublisher(registerMiniCodecs, func(pub *testPublisher) error {
				pub.streamSuffix = streamSuffix
				pub.iceReadyCancel = publishReadyCancel
				resources = append(resources, pub)
				return pub.Setup(*srsVnetClientIP)
			}); err != nil {
				return err
			}

			// Init done.
			mainReadyCancel()

			<-ctx.Done()
			return nil
		}

		if err := doInit(); err != nil {
			r1 = err
		}
	}()

	// Run publisher.
	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		select {
		case <-ctx.Done():
		case <-mainReady.Done():
			r2 = thePublisher.Run(logger.WithContext(ctx), cancel)
			logger.Tf(ctx, "pub done")
		}
	}()

	// Run player.
	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		select {
		case <-ctx.Done():
		case <-publishReady.Done():
			r3 = thePlayer.Run(logger.WithContext(ctx), cancel)
			logger.Tf(ctx, "play done")
		}
	}()
}

// Veirfy https://github.com/ossrs/srs/issues/2371
func TestBugfix2371_RTMP2RTC_PlayWithNack(t *testing.T) {
	if err := filterTestError(func() error {
		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		p, err := newTestPlayer(registerMiniCodecs, func(play *testPlayer) error {
			play.onOffer = func(s *webrtc.SessionDescription) error {
				if n := strings.Count(s.SDP, "nack"); n != 2 {
					return errors.Errorf("invalid %v nack", n)
				}
				return nil
			}
			play.onAnswer = func(s *webrtc.SessionDescription) error {
				if n := strings.Count(s.SDP, "nack"); n != 2 {
					return errors.Errorf("invalid %v nack", n)
				}
				cancel()
				return nil
			}
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		if err := p.Setup(*srsVnetClientIP); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()); err != nil {
		t.Errorf("err %+v", err)
	}
}

// Basic use scenario, publish a stream.
func TestRtcBasic_PublishOnly(t *testing.T) {
	if err := filterTestError(func() error {
		streamSuffix := fmt.Sprintf("publish-only-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			var nnRTCP, nnRTP int64
			api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					nnRTP++
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
			api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
				i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
					if nnRTCP++; nnRTCP >= int64(*srsPublishOKPackets) && nnRTP >= int64(*srsPublishOKPackets) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v RTP read %v RTCP packets", nnRTP, nnRTCP)
					return i.nextRTCPReader.Read(buf, attributes)
				}
			}))
		}, func(api *testWebRTCAPI) {
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed {
					return true
				}
				logger.Tf(ctx, "Chunk %v, ok=%v %v bytes", chunk, ok, len(c.UserData()))
				return true
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()); err != nil {
		t.Errorf("err %+v", err)
	}
}

// Basic use scenario, publish a stream, then play it.
func TestRtcBasic_PublishPlay(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	var r0, r1, r2, r3 error
	defer func(ctx context.Context) {
		if err := filterTestError(ctx.Err(), r0, r1, r2, r3); err != nil {
			t.Errorf("Fail for err %+v", err)
		} else {
			logger.Tf(ctx, "test done with err %+v", err)
		}
	}(ctx)

	var resources []io.Closer
	defer func() {
		for _, resource := range resources {
			_ = resource.Close()
		}
	}()

	var wg sync.WaitGroup
	defer wg.Wait()

	// The event notify.
	var thePublisher *testPublisher
	var thePlayer *testPlayer

	mainReady, mainReadyCancel := context.WithCancel(context.Background())
	publishReady, publishReadyCancel := context.WithCancel(context.Background())

	// Objects init.
	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		doInit := func() (err error) {
			streamSuffix := fmt.Sprintf("basic-publish-play-%v-%v", os.Getpid(), rand.Int())

			// Initialize player with private api.
			if thePlayer, err = newTestPlayer(registerDefaultCodecs, func(play *testPlayer) error {
				play.streamSuffix = streamSuffix
				resources = append(resources, play)

				var nnPlayWriteRTCP, nnPlayReadRTCP, nnPlayWriteRTP, nnPlayReadRTP uint64
				return play.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
					api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
						i.rtpReader = func(payload []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
							if nnPlayReadRTP++; nnPlayReadRTP >= uint64(*srsPlayOKPackets) {
								cancel() // Completed.
							}
							logger.Tf(ctx, "Play rtp=(recv:%v, send:%v), rtcp=(recv:%v send:%v) packets",
								nnPlayReadRTP, nnPlayWriteRTP, nnPlayReadRTCP, nnPlayWriteRTCP)
							return i.nextRTPReader.Read(payload, attributes)
						}
					}))
					api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
						i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
							nn, attr, err := i.nextRTCPReader.Read(buf, attributes)
							nnPlayReadRTCP++
							return nn, attr, err
						}
						i.rtcpWriter = func(pkts []rtcp.Packet, attributes interceptor.Attributes) (int, error) {
							nn, err := i.nextRTCPWriter.Write(pkts, attributes)
							nnPlayWriteRTCP++
							return nn, err
						}
					}))
				})
			}); err != nil {
				return err
			}

			// Initialize publisher with private api.
			if thePublisher, err = newTestPublisher(registerDefaultCodecs, func(pub *testPublisher) error {
				pub.streamSuffix = streamSuffix
				pub.iceReadyCancel = publishReadyCancel
				resources = append(resources, pub)

				var nnPubWriteRTCP, nnPubReadRTCP, nnPubWriteRTP, nnPubReadRTP uint64
				return pub.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
					api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
						i.rtpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
							nn, attr, err := i.nextRTPReader.Read(buf, attributes)
							nnPubReadRTP++
							return nn, attr, err
						}
						i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
							nn, err := i.nextRTPWriter.Write(header, payload, attributes)
							nnPubWriteRTP++
							logger.Tf(ctx, "Publish rtp=(recv:%v, send:%v), rtcp=(recv:%v send:%v) packets",
								nnPubReadRTP, nnPubWriteRTP, nnPubReadRTCP, nnPubWriteRTCP)
							return nn, err
						}
					}))
					api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
						i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
							nn, attr, err := i.nextRTCPReader.Read(buf, attributes)
							nnPubReadRTCP++
							return nn, attr, err
						}
						i.rtcpWriter = func(pkts []rtcp.Packet, attributes interceptor.Attributes) (int, error) {
							nn, err := i.nextRTCPWriter.Write(pkts, attributes)
							nnPubWriteRTCP++
							return nn, err
						}
					}))
				})
			}); err != nil {
				return err
			}

			// Init done.
			mainReadyCancel()

			<-ctx.Done()
			return nil
		}

		if err := doInit(); err != nil {
			r1 = err
		}
	}()

	// Run publisher.
	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		select {
		case <-ctx.Done():
		case <-mainReady.Done():
			r2 = thePublisher.Run(logger.WithContext(ctx), cancel)
			logger.Tf(ctx, "pub done")
		}
	}()

	// Run player.
	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		select {
		case <-ctx.Done():
		case <-publishReady.Done():
			r3 = thePlayer.Run(logger.WithContext(ctx), cancel)
			logger.Tf(ctx, "play done")
		}
	}()
}

// The srs-server is DTLS server(passive), srs-bench is DTLS client which is active mode.
//
//	No.1  srs-bench: ClientHello
//	No.2 srs-server: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//	No.3  srs-bench: Certificate, ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//	No.4 srs-server: ChangeCipherSpec, Finished
func TestRtcDTLS_ClientActive_Default(t *testing.T) {
	if err := filterTestError(func() error {
		streamSuffix := fmt.Sprintf("dtls-passive-no-arq-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupActive
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			var nnRTCP, nnRTP int64
			api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					nnRTP++
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
			api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
				i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
					if nnRTCP++; nnRTCP >= int64(*srsPublishOKPackets) && nnRTP >= int64(*srsPublishOKPackets) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v RTP read %v RTCP packets", nnRTP, nnRTCP)
					return i.nextRTCPReader.Read(buf, attributes)
				}
			}))
		}, func(api *testWebRTCAPI) {
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed {
					return true
				}
				logger.Tf(ctx, "Chunk %v, ok=%v %v bytes", chunk, ok, len(c.UserData()))
				return true
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS client(client), srs-bench is DTLS server which is passive mode.
//
//	No.1 srs-server: ClientHello
//	No.2  srs-bench: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//	No.3 srs-server: Certificate, ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//	No.4  srs-bench: ChangeCipherSpec, Finished
func TestRtcDTLS_ClientPassive_Default(t *testing.T) {
	if err := filterTestError(func() error {
		streamSuffix := fmt.Sprintf("dtls-active-no-arq-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			var nnRTCP, nnRTP int64
			api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					nnRTP++
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
			api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
				i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
					if nnRTCP++; nnRTCP >= int64(*srsPublishOKPackets) && nnRTP >= int64(*srsPublishOKPackets) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v RTP read %v RTCP packets", nnRTP, nnRTCP)
					return i.nextRTCPReader.Read(buf, attributes)
				}
			}))
		}, func(api *testWebRTCAPI) {
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed {
					return true
				}
				logger.Tf(ctx, "Chunk %v, ok=%v %v bytes", chunk, ok, len(c.UserData()))
				return true
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS server(passive), srs-bench is DTLS client which is active mode.
//
//	No.1  srs-bench: ClientHello
//	No.2 srs-server: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//	No.3  srs-bench: Certificate, ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//	No.4 srs-server: ChangeCipherSpec, Finished
//
// We utilized a large certificate to evaluate DTLS, which resulted in the fragmentation of the protocol.
func TestRtcDTLS_ClientActive_With_Large_Rsa_Certificate(t *testing.T) {
	if err := filterTestError(func() error {
		streamSuffix := fmt.Sprintf("dtls-passive-no-arq-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupActive
			return nil
		}, createLargeRsaCertificate)
		if err != nil {
			return err
		}
		defer p.Close()

		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			var nnRTCP, nnRTP int64
			api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					nnRTP++
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
			api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
				i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
					if nnRTCP++; nnRTCP >= int64(*srsPublishOKPackets) && nnRTP >= int64(*srsPublishOKPackets) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v RTP read %v RTCP packets", nnRTP, nnRTCP)
					return i.nextRTCPReader.Read(buf, attributes)
				}
			}))
		}, func(api *testWebRTCAPI) {
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed {
					return true
				}
				logger.Tf(ctx, "Chunk %v, ok=%v %v bytes", chunk, ok, len(c.UserData()))
				return true
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS client(client), srs-bench is DTLS server which is passive mode.
//
//	No.1 srs-server: ClientHello
//	No.2  srs-bench: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//	No.3 srs-server: Certificate, ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//	No.4  srs-bench: ChangeCipherSpec, Finished
//
// We utilized a large certificate to evaluate DTLS, which resulted in the fragmentation of the protocol.
func TestRtcDTLS_ClientPassive_With_Large_Rsa_Certificate(t *testing.T) {
	if err := filterTestError(func() error {
		streamSuffix := fmt.Sprintf("dtls-active-no-arq-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
			return nil
		}, createLargeRsaCertificate)
		if err != nil {
			return err
		}
		defer p.Close()

		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			var nnRTCP, nnRTP int64
			api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					nnRTP++
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
			api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
				i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
					if nnRTCP++; nnRTCP >= int64(*srsPublishOKPackets) && nnRTP >= int64(*srsPublishOKPackets) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v RTP read %v RTCP packets", nnRTP, nnRTCP)
					return i.nextRTCPReader.Read(buf, attributes)
				}
			}))
		}, func(api *testWebRTCAPI) {
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed {
					return true
				}
				logger.Tf(ctx, "Chunk %v, ok=%v %v bytes", chunk, ok, len(c.UserData()))
				return true
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS server(passive), srs-bench is DTLS client which is active mode.
//
//	No.1  srs-bench: ClientHello
//	No.2 srs-server: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//	No.3  srs-bench: Certificate, ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//	No.4 srs-server: ChangeCipherSpec, Finished
//
// We utilized a large certificate to evaluate DTLS, which resulted in the fragmentation of the protocol.
func TestRtcDTLS_ClientActive_With_Large_Ecdsa_Certificate(t *testing.T) {
	if err := filterTestError(func() error {
		streamSuffix := fmt.Sprintf("dtls-passive-no-arq-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupActive
			return nil
		}, createLargeEcdsaCertificate)
		if err != nil {
			return err
		}
		defer p.Close()

		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			var nnRTCP, nnRTP int64
			api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					nnRTP++
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
			api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
				i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
					if nnRTCP++; nnRTCP >= int64(*srsPublishOKPackets) && nnRTP >= int64(*srsPublishOKPackets) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v RTP read %v RTCP packets", nnRTP, nnRTCP)
					return i.nextRTCPReader.Read(buf, attributes)
				}
			}))
		}, func(api *testWebRTCAPI) {
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed {
					return true
				}
				logger.Tf(ctx, "Chunk %v, ok=%v %v bytes", chunk, ok, len(c.UserData()))
				return true
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS client(client), srs-bench is DTLS server which is passive mode.
//
//	No.1 srs-server: ClientHello
//	No.2  srs-bench: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//	No.3 srs-server: Certificate, ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//	No.4  srs-bench: ChangeCipherSpec, Finished
//
// We utilized a large certificate to evaluate DTLS, which resulted in the fragmentation of the protocol.
func TestRtcDTLS_ClientPassive_With_Large_Ecdsa_Certificate(t *testing.T) {
	if err := filterTestError(func() error {
		streamSuffix := fmt.Sprintf("dtls-active-no-arq-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
			return nil
		}, createLargeEcdsaCertificate)
		if err != nil {
			return err
		}
		defer p.Close()

		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			var nnRTCP, nnRTP int64
			api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					nnRTP++
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
			api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
				i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
					if nnRTCP++; nnRTCP >= int64(*srsPublishOKPackets) && nnRTP >= int64(*srsPublishOKPackets) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v RTP read %v RTCP packets", nnRTP, nnRTCP)
					return i.nextRTCPReader.Read(buf, attributes)
				}
			}))
		}, func(api *testWebRTCAPI) {
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed {
					return true
				}
				logger.Tf(ctx, "Chunk %v, ok=%v %v bytes", chunk, ok, len(c.UserData()))
				return true
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS server, srs-bench is DTLS client which is active mode.
// When srs-bench close the PC, it will send DTLS alert and might retransmit it.
func TestRtcDTLS_ClientActive_Duplicated_Alert(t *testing.T) {
	if err := filterTestError(func() error {
		streamSuffix := fmt.Sprintf("dtls-active-no-arq-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupActive
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			var nnRTCP, nnRTP int64
			api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					nnRTP++
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
			api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
				i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
					if nnRTCP++; nnRTCP >= int64(*srsPublishOKPackets) && nnRTP >= int64(*srsPublishOKPackets) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v RTP read %v RTCP packets", nnRTP, nnRTCP)
					return i.nextRTCPReader.Read(buf, attributes)
				}
			}))
		}, func(api *testWebRTCAPI) {
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed || chunk.chunk != chunkTypeDTLS {
					return true
				}

				// Copy the alert to server, ignore error.
				if chunk.content == dtlsContentTypeAlert {
					_, _ = api.proxy.Deliver(c.SourceAddr(), c.DestinationAddr(), c.UserData())
					_, _ = api.proxy.Deliver(c.SourceAddr(), c.DestinationAddr(), c.UserData())
				}

				logger.Tf(ctx, "Chunk %v, ok=%v %v bytes", chunk, ok, len(c.UserData()))
				return true
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS client, srs-bench is DTLS server which is passive mode.
// When srs-bench close the PC, it will send DTLS alert and might retransmit it.
func TestRtcDTLS_ClientPassive_Duplicated_Alert(t *testing.T) {
	if err := filterTestError(func() error {
		streamSuffix := fmt.Sprintf("dtls-active-no-arq-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			var nnRTCP, nnRTP int64
			api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					nnRTP++
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
			api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
				i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
					if nnRTCP++; nnRTCP >= int64(*srsPublishOKPackets) && nnRTP >= int64(*srsPublishOKPackets) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v RTP read %v RTCP packets", nnRTP, nnRTCP)
					return i.nextRTCPReader.Read(buf, attributes)
				}
			}))
		}, func(api *testWebRTCAPI) {
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed || chunk.chunk != chunkTypeDTLS {
					return true
				}

				// Copy the alert to server, ignore error.
				if chunk.content == dtlsContentTypeAlert {
					_, _ = api.proxy.Deliver(c.SourceAddr(), c.DestinationAddr(), c.UserData())
					_, _ = api.proxy.Deliver(c.SourceAddr(), c.DestinationAddr(), c.UserData())
				}

				logger.Tf(ctx, "Chunk %v, ok=%v %v bytes", chunk, ok, len(c.UserData()))
				return true
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS server, srs-bench is DTLS client which is active mode.
// [Drop] No.1  srs-bench: ClientHello(Epoch=0, Sequence=0)
// [ARQ]  No.2  srs-bench: ClientHello(Epoch=0, Sequence=1)
//
//	No.3 srs-server: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//	No.4  srs-bench: Certificate, ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//	No.5 srs-server: ChangeCipherSpec, Finished
//
// @remark The pion is active, so it can be consider a benchmark for DTLS server.
func TestRtcDTLS_ClientActive_ARQ_ClientHello_ByDropped_ClientHello(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	var r0 error
	err := func() error {
		streamSuffix := fmt.Sprintf("dtls-active-arq-client-hello-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupActive
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			var nnRTCP, nnRTP int64
			api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					nnRTP++
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
			api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
				i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
					if nnRTCP++; nnRTCP >= int64(*srsPublishOKPackets) && nnRTP >= int64(*srsPublishOKPackets) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v RTP read %v RTCP packets", nnRTP, nnRTCP)
					return i.nextRTCPReader.Read(buf, attributes)
				}
			}))
		}, func(api *testWebRTCAPI) {
			nnClientHello, nnMaxDrop := 0, 1
			var lastClientHello *dtlsRecord

			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed || chunk.chunk != chunkTypeDTLS || chunk.content != dtlsContentTypeHandshake || chunk.handshake != dtlsHandshakeTypeClientHello {
					return true
				}

				record, err := newDTLSRecord(c.UserData())
				if err != nil {
					return true
				}

				if lastClientHello != nil && record.Equals(lastClientHello) {
					r0 = errors.Errorf("dup record %v", record)
				}
				lastClientHello = record

				nnClientHello++
				ok = nnClientHello > nnMaxDrop
				logger.Tf(ctx, "NN=%v, Chunk %v, %v, ok=%v %v bytes", nnClientHello, chunk, record, ok, len(c.UserData()))
				return
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()
	if err := filterTestError(ctx.Err(), err, r0); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS client, srs-bench is DTLS server which is passive mode.
// [Drop] No.1 srs-server: ClientHello(Epoch=0, Sequence=0)
// [ARQ]  No.2 srs-server: ClientHello(Epoch=0, Sequence=1)
//
//	No.3  srs-bench: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//	No.4 srs-server: Certificate, ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//	No.5  srs-bench: ChangeCipherSpec, Finished
//
// @remark If retransmit the ClientHello, with the same epoch+sequence, peer will request HelloVerifyRequest, then
// openssl will create a new ClientHello with increased sequence. It's ok, but waste a lots of duplicated ClientHello
// packets, so we fail the test, requires the epoch+sequence never dup, even for ARQ.
func TestRtcDTLS_ClientPassive_ARQ_ClientHello_ByDropped_ClientHello(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	var r0 error
	err := func() error {
		streamSuffix := fmt.Sprintf("dtls-passive-arq-client-hello-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			var nnRTCP, nnRTP int64
			api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					nnRTP++
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
			api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
				i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
					if nnRTCP++; nnRTCP >= int64(*srsPublishOKPackets) && nnRTP >= int64(*srsPublishOKPackets) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v RTP read %v RTCP packets", nnRTP, nnRTCP)
					return i.nextRTCPReader.Read(buf, attributes)
				}
			}))
		}, func(api *testWebRTCAPI) {
			nnClientHello, nnMaxDrop := 0, 1
			var lastClientHello *dtlsRecord

			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed || chunk.chunk != chunkTypeDTLS || chunk.content != dtlsContentTypeHandshake || chunk.handshake != dtlsHandshakeTypeClientHello {
					return true
				}

				record, err := newDTLSRecord(c.UserData())
				if err != nil {
					return true
				}

				if lastClientHello != nil && record.Equals(lastClientHello) {
					r0 = errors.Errorf("dup record %v", record)
				}
				lastClientHello = record

				nnClientHello++
				ok = nnClientHello > nnMaxDrop
				logger.Tf(ctx, "NN=%v, Chunk %v, %v, ok=%v %v bytes", nnClientHello, chunk, record, ok, len(c.UserData()))
				return
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()
	if err := filterTestError(ctx.Err(), err, r0); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS server, srs-bench is DTLS client which is active mode.
//
//	No.1  srs-bench: ClientHello(Epoch=0, Sequence=0)
//
// [Drop] No.2 srs-server: ServerHello(Epoch=0, Sequence=0), Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
// [ARQ]  No.2  srs-bench: ClientHello(Epoch=0, Sequence=1)
// [ARQ]  No.3 srs-server: ServerHello(Epoch=0, Sequence=5), Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//
//	No.4  srs-bench: Certificate, ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//	No.5 srs-server: ChangeCipherSpec, Finished
//
// @remark The pion is active, so it can be consider a benchmark for DTLS server.
func TestRtcDTLS_ClientActive_ARQ_ClientHello_ByDropped_ServerHello(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	var r0, r1 error
	err := func() error {
		streamSuffix := fmt.Sprintf("dtls-active-arq-client-hello-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupActive
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			var nnRTCP, nnRTP int64
			api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					nnRTP++
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
			api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
				i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
					if nnRTCP++; nnRTCP >= int64(*srsPublishOKPackets) && nnRTP >= int64(*srsPublishOKPackets) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v RTP read %v RTCP packets", nnRTP, nnRTCP)
					return i.nextRTCPReader.Read(buf, attributes)
				}
			}))
		}, func(api *testWebRTCAPI) {
			nnServerHello, nnMaxDrop := 0, 1
			var lastClientHello, lastServerHello *dtlsRecord

			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed || chunk.chunk != chunkTypeDTLS || chunk.content != dtlsContentTypeHandshake ||
					(chunk.handshake != dtlsHandshakeTypeClientHello && chunk.handshake != dtlsHandshakeTypeServerHello) {
					return true
				}

				record, err := newDTLSRecord(c.UserData())
				if err != nil {
					return true
				}

				if chunk.handshake == dtlsHandshakeTypeClientHello {
					if lastClientHello != nil && record.Equals(lastClientHello) {
						r0 = errors.Errorf("dup record %v", record)
					}
					lastClientHello = record
					return true
				}

				if lastServerHello != nil && record.Equals(lastServerHello) {
					r1 = errors.Errorf("dup record %v", record)
				}
				lastServerHello = record

				nnServerHello++
				ok = nnServerHello > nnMaxDrop
				logger.Tf(ctx, "NN=%v, Chunk %v, %v, ok=%v %v bytes", nnServerHello, chunk, record, ok, len(c.UserData()))
				return
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()
	if err := filterTestError(ctx.Err(), err, r0, r1); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS client, srs-bench is DTLS server which is passive mode.
//
//	No.1 srs-server: ClientHello(Epoch=0, Sequence=0)
//
// [Drop] No.2  srs-bench: ServerHello(Epoch=0, Sequence=0), Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
// [ARQ]  No.2 srs-server: ClientHello(Epoch=0, Sequence=1)
// [ARQ]  No.3  srs-bench: ServerHello(Epoch=0, Sequence=5), Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//
//	No.4 srs-server: Certificate, ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//	No.5  srs-bench: ChangeCipherSpec, Finished
//
// @remark If retransmit the ClientHello, with the same epoch+sequence, peer will request HelloVerifyRequest, then
// openssl will create a new ClientHello with increased sequence. It's ok, but waste a lots of duplicated ClientHello
// packets, so we fail the test, requires the epoch+sequence never dup, even for ARQ.
func TestRtcDTLS_ClientPassive_ARQ_ClientHello_ByDropped_ServerHello(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	var r0, r1 error
	err := func() error {
		streamSuffix := fmt.Sprintf("dtls-passive-arq-client-hello-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			var nnRTCP, nnRTP int64
			api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					nnRTP++
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
			api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
				i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
					if nnRTCP++; nnRTCP >= int64(*srsPublishOKPackets) && nnRTP >= int64(*srsPublishOKPackets) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v RTP read %v RTCP packets", nnRTP, nnRTCP)
					return i.nextRTCPReader.Read(buf, attributes)
				}
			}))
		}, func(api *testWebRTCAPI) {
			nnServerHello, nnMaxDrop := 0, 1
			var lastClientHello, lastServerHello *dtlsRecord

			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed || chunk.chunk != chunkTypeDTLS || chunk.content != dtlsContentTypeHandshake ||
					(chunk.handshake != dtlsHandshakeTypeClientHello && chunk.handshake != dtlsHandshakeTypeServerHello) {
					return true
				}

				record, err := newDTLSRecord(c.UserData())
				if err != nil {
					return true
				}

				if chunk.handshake == dtlsHandshakeTypeClientHello {
					if lastClientHello != nil && record.Equals(lastClientHello) {
						r0 = errors.Errorf("dup record %v", record)
					}
					lastClientHello = record
					return true
				}

				if lastServerHello != nil && record.Equals(lastServerHello) {
					r1 = errors.Errorf("dup record %v", record)
				}
				lastServerHello = record

				nnServerHello++
				ok = nnServerHello > nnMaxDrop
				logger.Tf(ctx, "NN=%v, Chunk %v, %v, ok=%v %v bytes", nnServerHello, chunk, record, ok, len(c.UserData()))
				return
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()
	if err := filterTestError(ctx.Err(), err, r0, r1); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS server, srs-bench is DTLS client which is active mode.
//
//	No.1  srs-bench: ClientHello
//	No.2 srs-server: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//
// [Drop] No.3  srs-bench: Certificate(Epoch=0, Sequence=0), ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
// [ARQ]  No.4  srs-bench: Certificate(Epoch=0, Sequence=5), ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//
//	No.5 srs-server: ChangeCipherSpec, Finished
//
// @remark The pion is active, so it can be consider a benchmark for DTLS server.
func TestRtcDTLS_ClientActive_ARQ_Certificate_ByDropped_Certificate(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	var r0 error
	err := func() error {
		streamSuffix := fmt.Sprintf("dtls-active-arq-certificate-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupActive
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			var nnRTCP, nnRTP int64
			api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					nnRTP++
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
			api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
				i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
					if nnRTCP++; nnRTCP >= int64(*srsPublishOKPackets) && nnRTP >= int64(*srsPublishOKPackets) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v RTP read %v RTCP packets", nnRTP, nnRTCP)
					return i.nextRTCPReader.Read(buf, attributes)
				}
			}))
		}, func(api *testWebRTCAPI) {
			nnCertificate, nnMaxDrop := 0, 1
			var lastCertificate *dtlsRecord

			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed || chunk.chunk != chunkTypeDTLS || chunk.content != dtlsContentTypeHandshake || chunk.handshake != dtlsHandshakeTypeCertificate {
					return true
				}

				record, err := newDTLSRecord(c.UserData())
				if err != nil {
					return true
				}

				if lastCertificate != nil && lastCertificate.Equals(record) {
					r0 = errors.Errorf("dup record %v", record)
				}
				lastCertificate = record

				nnCertificate++
				ok = nnCertificate > nnMaxDrop
				logger.Tf(ctx, "NN=%v, Chunk %v, %v, ok=%v %v bytes", nnCertificate, chunk, record, ok, len(c.UserData()))
				return
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()
	if err := filterTestError(ctx.Err(), err, r0); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS client, srs-bench is DTLS server which is passive mode.
//
//	No.1 srs-server: ClientHello
//	No.2  srs-bench: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//
// [Drop] No.3 srs-server: Certificate(Epoch=0, Sequence=0), ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
// [ARQ]  No.4 srs-server: Certificate(Epoch=0, Sequence=5), ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//
//	No.5  srs-bench: ChangeCipherSpec, Finished
//
// @remark If retransmit the Certificate, with the same epoch+sequence, peer will drop the message. It's ok right now, but
// wast some packets, so we check the epoch+sequence which should never dup, even for ARQ.
func TestRtcDTLS_ClientPassive_ARQ_Certificate_ByDropped_Certificate(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	var r0 error
	err := func() error {
		streamSuffix := fmt.Sprintf("dtls-passive-arq-certificate-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			var nnRTCP, nnRTP int64
			api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					nnRTP++
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
			api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
				i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
					if nnRTCP++; nnRTCP >= int64(*srsPublishOKPackets) && nnRTP >= int64(*srsPublishOKPackets) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v RTP read %v RTCP packets", nnRTP, nnRTCP)
					return i.nextRTCPReader.Read(buf, attributes)
				}
			}))
		}, func(api *testWebRTCAPI) {
			nnCertificate, nnMaxDrop := 0, 1
			var lastCertificate *dtlsRecord

			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed || chunk.chunk != chunkTypeDTLS || chunk.content != dtlsContentTypeHandshake || chunk.handshake != dtlsHandshakeTypeCertificate {
					return true
				}

				record, err := newDTLSRecord(c.UserData())
				if err != nil {
					return true
				}

				if lastCertificate != nil && lastCertificate.Equals(record) {
					r0 = errors.Errorf("dup record %v", record)
				}
				lastCertificate = record

				nnCertificate++
				ok = nnCertificate > nnMaxDrop
				logger.Tf(ctx, "NN=%v, Chunk %v, %v, ok=%v %v bytes", nnCertificate, chunk, record, ok, len(c.UserData()))
				return
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()
	if err := filterTestError(ctx.Err(), err, r0); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS server, srs-bench is DTLS client which is active mode.
//
//	No.1  srs-bench: ClientHello
//	No.2 srs-server: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//	No.3  srs-bench: Certificate(Epoch=0, Sequence=0), ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//
// [Drop] No.5 srs-server: ChangeCipherSpec, Finished
// [ARQ]  No.6  srs-bench: Certificate(Epoch=0, Sequence=5), ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
// [ARQ]  No.7 srs-server: ChangeCipherSpec, Finished
//
// @remark The pion is active, so it can be consider a benchmark for DTLS server.
func TestRtcDTLS_ClientActive_ARQ_Certificate_ByDropped_ChangeCipherSpec(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	var r0, r1 error
	err := func() error {
		streamSuffix := fmt.Sprintf("dtls-active-arq-certificate-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupActive
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			var nnRTCP, nnRTP int64
			api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					nnRTP++
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
			api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
				i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
					if nnRTCP++; nnRTCP >= int64(*srsPublishOKPackets) && nnRTP >= int64(*srsPublishOKPackets) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v RTP read %v RTCP packets", nnRTP, nnRTCP)
					return i.nextRTCPReader.Read(buf, attributes)
				}
			}))
		}, func(api *testWebRTCAPI) {
			nnCertificate, nnMaxDrop := 0, 1
			var lastChangeCipherSepc, lastCertifidate *dtlsRecord

			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed || (!chunk.IsChangeCipherSpec() && !chunk.IsCertificate()) {
					return true
				}

				record, err := newDTLSRecord(c.UserData())
				if err != nil {
					return true
				}

				if chunk.IsCertificate() {
					if lastCertifidate != nil && record.Equals(lastCertifidate) {
						r0 = errors.Errorf("dup record %v", record)
					}
					lastCertifidate = record
					return true
				}

				if lastChangeCipherSepc != nil && lastChangeCipherSepc.Equals(record) {
					r1 = errors.Errorf("dup record %v", record)
				}
				lastChangeCipherSepc = record

				nnCertificate++
				ok = nnCertificate > nnMaxDrop
				logger.Tf(ctx, "NN=%v, Chunk %v, %v, ok=%v %v bytes", nnCertificate, chunk, record, ok, len(c.UserData()))
				return
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()
	if err := filterTestError(ctx.Err(), err, r0, r1); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS client, srs-bench is DTLS server which is passive mode.
//
//	No.1  srs-server: ClientHello
//	No.2 srs-bench: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//	No.3  srs-server: Certificate(Epoch=0, Sequence=0), ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//
// [Drop] No.5 srs-bench: ChangeCipherSpec, Finished
// [ARQ]  No.6  srs-server: Certificate(Epoch=0, Sequence=5), ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
// [ARQ]  No.7 srs-bench: ChangeCipherSpec, Finished
//
// @remark If retransmit the Certificate, with the same epoch+sequence, peer will drop the message, and never generate the
// ChangeCipherSpec, which will cause DTLS fail.
func TestRtcDTLS_ClientPassive_ARQ_Certificate_ByDropped_ChangeCipherSpec(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	var r0, r1 error
	err := func() error {
		streamSuffix := fmt.Sprintf("dtls-passive-arq-certificate-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			var nnRTCP, nnRTP int64
			api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					nnRTP++
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
			api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
				i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
					if nnRTCP++; nnRTCP >= int64(*srsPublishOKPackets) && nnRTP >= int64(*srsPublishOKPackets) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v RTP read %v RTCP packets", nnRTP, nnRTCP)
					return i.nextRTCPReader.Read(buf, attributes)
				}
			}))
		}, func(api *testWebRTCAPI) {
			nnCertificate, nnMaxDrop := 0, 1
			var lastChangeCipherSepc, lastCertifidate *dtlsRecord

			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed || (!chunk.IsChangeCipherSpec() && !chunk.IsCertificate()) {
					return true
				}

				record, err := newDTLSRecord(c.UserData())
				if err != nil {
					return true
				}

				if chunk.IsCertificate() {
					if lastCertifidate != nil && record.Equals(lastCertifidate) {
						r0 = errors.Errorf("dup record %v", record)
					}
					lastCertifidate = record
					return true
				}

				if lastChangeCipherSepc != nil && lastChangeCipherSepc.Equals(record) {
					r1 = errors.Errorf("dup record %v", record)
				}
				lastChangeCipherSepc = record

				nnCertificate++
				ok = nnCertificate > nnMaxDrop
				logger.Tf(ctx, "NN=%v, Chunk %v, %v, ok=%v %v bytes", nnCertificate, chunk, record, ok, len(c.UserData()))
				return
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()
	if err := filterTestError(ctx.Err(), err, r0, r1); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS client(client), srs-bench is DTLS server which is passive mode.
// Drop all DTLS packets when got ClientHello, to test the server ARQ thread cleanup.
func TestRtcDTLS_ClientPassive_ARQ_DropAllAfter_ClientHello(t *testing.T) {
	if err := filterTestError(func() error {
		streamSuffix := fmt.Sprintf("dtls-passive-no-arq-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			nnDrop, dropAll := 0, false
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed {
					return true
				}

				if chunk.IsHandshake() {
					if chunk.IsClientHello() {
						dropAll = true
					}

					if !dropAll {
						return true
					}

					if nnDrop++; nnDrop >= *srsDTLSDropPackets {
						cancel() // Done, server transmit 5 Client Hello.
					}

					logger.Tf(ctx, "N=%v, Drop chunk %v %v bytes", nnDrop, chunk, len(c.UserData()))
					return false
				}

				return true
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS client(client), srs-bench is DTLS server which is passive mode.
// Drop all DTLS packets when got ServerHello, to test the server ARQ thread cleanup.
func TestRtcDTLS_ClientPassive_ARQ_DropAllAfter_ServerHello(t *testing.T) {
	if err := filterTestError(func() error {
		streamSuffix := fmt.Sprintf("dtls-passive-no-arq-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			nnDrop, dropAll := 0, false
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed {
					return true
				}

				if chunk.IsHandshake() {
					if chunk.IsServerHello() {
						dropAll = true
					}

					if !dropAll {
						return true
					}

					if nnDrop++; nnDrop >= *srsDTLSDropPackets {
						cancel() // Done, server transmit 5 Client Hello.
					}

					logger.Tf(ctx, "N=%v, Drop chunk %v %v bytes", nnDrop, chunk, len(c.UserData()))
					return false
				}

				return true
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS client(client), srs-bench is DTLS server which is passive mode.
// Drop all DTLS packets when got Certificate, to test the server ARQ thread cleanup.
func TestRtcDTLS_ClientPassive_ARQ_DropAllAfter_Certificate(t *testing.T) {
	if err := filterTestError(func() error {
		streamSuffix := fmt.Sprintf("dtls-passive-no-arq-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			nnDrop, dropAll := 0, false
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed {
					return true
				}

				if chunk.IsHandshake() {
					if chunk.IsCertificate() {
						dropAll = true
					}

					if !dropAll {
						return true
					}

					if nnDrop++; nnDrop >= *srsDTLSDropPackets {
						cancel() // Done, server transmit 5 Client Hello.
					}

					logger.Tf(ctx, "N=%v, Drop chunk %v %v bytes", nnDrop, chunk, len(c.UserData()))
					return false
				}

				return true
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS client(client), srs-bench is DTLS server which is passive mode.
// Drop all DTLS packets when got ChangeCipherSpec, to test the server ARQ thread cleanup.
func TestRtcDTLS_ClientPassive_ARQ_DropAllAfter_ChangeCipherSpec(t *testing.T) {
	if err := filterTestError(func() error {
		streamSuffix := fmt.Sprintf("dtls-passive-no-arq-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			nnDrop, dropAll := 0, false
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed {
					return true
				}

				if chunk.IsHandshake() || chunk.IsChangeCipherSpec() {
					if chunk.IsChangeCipherSpec() {
						dropAll = true
					}

					if !dropAll {
						return true
					}

					if nnDrop++; nnDrop >= *srsDTLSDropPackets {
						cancel() // Done, server transmit 5 Client Hello.
					}

					logger.Tf(ctx, "N=%v, Drop chunk %v %v bytes", nnDrop, chunk, len(c.UserData()))
					return false
				}

				return true
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS client(client), srs-bench is DTLS server which is passive mode.
// For very bad network, we drop 4 ClientHello consume about 750ms, then drop 4 Certificate
// which also consume about 750ms, but finally should be done successfully.
func TestRtcDTLS_ClientPassive_ARQ_VeryBadNetwork(t *testing.T) {
	if err := filterTestError(func() error {
		streamSuffix := fmt.Sprintf("dtls-passive-no-arq-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			var nnRTCP, nnRTP int64
			api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					nnRTP++
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
			api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
				i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
					if nnRTCP++; nnRTCP >= int64(*srsPublishOKPackets) && nnRTP >= int64(*srsPublishOKPackets) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v RTP read %v RTCP packets", nnRTP, nnRTCP)
					return i.nextRTCPReader.Read(buf, attributes)
				}
			}))
		}, func(api *testWebRTCAPI) {
			nnDropClientHello, nnDropCertificate := 0, 0
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed {
					return true
				}

				if chunk.IsHandshake() {
					if !chunk.IsClientHello() && !chunk.IsCertificate() {
						return true
					}

					if chunk.IsClientHello() {
						if nnDropClientHello >= 4 {
							return true
						}
						nnDropClientHello++
					}

					if chunk.IsCertificate() {
						if nnDropCertificate >= *srsDTLSDropPackets {
							return true
						}
						nnDropCertificate++
					}

					logger.Tf(ctx, "N=%v/%v, Drop chunk %v %v bytes", nnDropClientHello, nnDropCertificate, chunk, len(c.UserData()))
					return false
				}

				return true
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS client(client), srs-bench is DTLS server which is passive mode.
// If we retransmit 2 ClientHello packets, consumed 150ms, server might wait at 200ms.
// Then we retransmit the Certificate, server reset the timer and retransmit it in 50ms, not 200ms.
func TestRtcDTLS_ClientPassive_ARQ_Certificate_After_ClientHello(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	var r0 error
	err := func() error {
		streamSuffix := fmt.Sprintf("dtls-passive-no-arq-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			var nnRTCP, nnRTP int64
			api.registry.Add(newRTPInterceptor(func(i *rtpInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					nnRTP++
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
			api.registry.Add(newRTCPInterceptor(func(i *rtcpInterceptor) {
				i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
					if nnRTCP++; nnRTCP >= int64(*srsPublishOKPackets) && nnRTP >= int64(*srsPublishOKPackets) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v RTP read %v RTCP packets", nnRTP, nnRTCP)
					return i.nextRTCPReader.Read(buf, attributes)
				}
			}))
		}, func(api *testWebRTCAPI) {
			nnDropClientHello, nnDropCertificate := 0, 0
			var firstCertificate time.Time
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := newChunkMessageType(c)
				if !parsed {
					return true
				}

				if chunk.IsHandshake() {
					if !chunk.IsClientHello() && !chunk.IsCertificate() {
						return true
					}

					if chunk.IsClientHello() {
						if nnDropClientHello > 3 {
							return true
						}
						nnDropClientHello++
					}

					if chunk.IsCertificate() {
						if nnDropCertificate == 0 {
							firstCertificate = time.Now()
						} else if nnDropCertificate == 1 {
							if duration := time.Now().Sub(firstCertificate); duration > 150*time.Millisecond {
								r0 = fmt.Errorf("ARQ between ClientHello and Certificate too large %v", duration)
							} else {
								logger.Tf(ctx, "ARQ between ClientHello and Certificate is %v", duration)
							}
							cancel()
						}
						nnDropCertificate++
					}

					logger.Tf(ctx, "N=%v/%v, Drop chunk %v %v bytes", nnDropClientHello, nnDropCertificate, chunk, len(c.UserData()))
					return false
				}

				return true
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()
	if err := filterTestError(ctx.Err(), err, r0); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS server, srs-bench is DTLS client which is active mode.
// This case is used to test the corruption of DTLS packets, which is expected to result in a failed handshake. In this
// case, we corrupt the ClientHello packet sent by srs-bench.
// Note that the passive mode is not being tested as the focus is solely on testing srs-server.
//
//     [Corrupt] No.1  srs-bench: ClientHello(Epoch=0, Sequence=0), change length from 129 to 0xf.
//     No.2 srs-server: Alert (Level: Fatal, Description: Illegal Parameter)
func TestRtcDTLS_ClientActive_Corrupt_ClientHello(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	r0 := fmt.Errorf("DTLS should failed.")
	err := func() error {
		streamSuffix := fmt.Sprintf("dtls-active-corrupt-client-hello-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupActive
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		p.onDTLSStateChange = func(state webrtc.DTLSTransportState) {
			if state == webrtc.DTLSTransportStateFailed {
				logger.Tf(ctx, "Got expected DTLS failed message, reset err to ok")
				r0, p.ignorePCStateError, p.ignoreDTLSStateError = nil, true, true
				cancel()
			}
		}

		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			nnClientHello := 0
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				b, chunk, parsed, record, err := newChunkAll(c)
				if !parsed || chunk.chunk != chunkTypeDTLS || chunk.content != dtlsContentTypeHandshake ||
					chunk.handshake != dtlsHandshakeTypeClientHello || err != nil {
					return true
				}

				b[14], b[15], b[16], nnClientHello = 0, 0, 0xf, nnClientHello+1
				logger.Tf(ctx, "NN=%v, Chunk %v, %v, %v bytes", nnClientHello, chunk, record, len(c.UserData()))
				return true
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()
	if err := filterTestError(ctx.Err(), err, r0); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS server, srs-bench is DTLS client which is active mode.
// This case is used to test the corruption of DTLS packets, which is expected to result in a failed handshake. In this
// case, we corrupt the ClientHello packet sent by srs-bench.
// Note that the passive mode is not being tested as the focus is solely on testing srs-server.
//
//     No.1  srs-bench: ClientHello
//     No.2 srs-server: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//     [Corrupt] No.3  srs-bench: Certificate, ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//     No.4 srs-server: Alert (Level: Fatal, Description: Illegal Parameter)
// [Corrupt] No.1  srs-bench: ClientHello(Epoch=0, Sequence=0), change length from 129 to 0xf.
// No.2 srs-server: Alert (Level: Fatal, Description: Illegal Parameter)
func TestRtcDTLS_ClientActive_Corrupt_Certificate(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	r0 := fmt.Errorf("DTLS should failed.")
	err := func() error {
		streamSuffix := fmt.Sprintf("dtls-active-corrupt-certificate-%v-%v", os.Getpid(), rand.Int())
		p, err := newTestPublisher(registerDefaultCodecs, func(p *testPublisher) error {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupActive
			return nil
		})
		if err != nil {
			return err
		}
		defer p.Close()

		p.onDTLSStateChange = func(state webrtc.DTLSTransportState) {
			if state == webrtc.DTLSTransportStateFailed {
				logger.Tf(ctx, "Got expected DTLS failed message, reset err to ok")
				r0, p.ignorePCStateError, p.ignoreDTLSStateError = nil, true, true
				cancel()
			}
		}

		if err := p.Setup(*srsVnetClientIP, func(api *testWebRTCAPI) {
			nnClientHello := 0
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				b, chunk, parsed, record, err := newChunkAll(c)
				if !parsed || chunk.chunk != chunkTypeDTLS || chunk.content != dtlsContentTypeHandshake ||
					chunk.handshake != dtlsHandshakeTypeCertificate || err != nil {
					return true
				}

				b[14], b[15], b[16], nnClientHello = 0, 0, 0xf, nnClientHello+1
				logger.Tf(ctx, "NN=%v, Chunk %v, %v, %v bytes", nnClientHello, chunk, record, len(c.UserData()))
				return true
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()
	if err := filterTestError(ctx.Err(), err, r0); err != nil {
		t.Errorf("err %+v", err)
	}
}

func TestRTCServerVersion(t *testing.T) {
	api := fmt.Sprintf("http://%v:1985/api/v1/versions", *srsServer)
	req, err := http.NewRequest("POST", api, nil)
	if err != nil {
		t.Errorf("Request %v", api)
		return
	}

	res, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Errorf("Do request %v", api)
		return
	}

	b, err := ioutil.ReadAll(res.Body)
	if err != nil {
		t.Errorf("Read body of %v", api)
		return
	}

	obj := struct {
		Code   int    `json:"code"`
		Server string `json:"server"`
		Data   struct {
			Major    int    `json:"major"`
			Minor    int    `json:"minor"`
			Revision int    `json:"revision"`
			Version  string `json:"version"`
		} `json:"data"`
	}{}
	if err := json.Unmarshal(b, &obj); err != nil {
		t.Errorf("Parse %v", string(b))
		return
	}
	if obj.Code != 0 {
		t.Errorf("Server err code=%v, server=%v", obj.Code, obj.Server)
		return
	}
	if obj.Data.Major == 0 && obj.Data.Minor == 0 {
		t.Errorf("Invalid version %v", obj.Data)
		return
	}
}

func TestRtcPublish_HttpFlvPlay(t *testing.T) {
	ctx := logger.WithContext(context.Background())
	ctx, cancel := context.WithTimeout(ctx, time.Duration(*srsTimeout)*time.Millisecond)

	var r0, r1, r2, r3 error
	defer func(ctx context.Context) {
		if err := filterTestError(ctx.Err(), r0, r1, r2, r3); err != nil {
			t.Errorf("Fail for err %+v", err)
		} else {
			logger.Tf(ctx, "test done with err %+v", err)
		}
	}(ctx)

	var resources []io.Closer
	defer func() {
		for _, resource := range resources {
			_ = resource.Close()
		}
	}()

	var wg sync.WaitGroup
	defer wg.Wait()

	// The event notify.
	var thePublisher *testPublisher

	mainReady, mainReadyCancel := context.WithCancel(context.Background())
	publishReady, publishReadyCancel := context.WithCancel(context.Background())

	streamSuffix := fmt.Sprintf("basic-publish-flvplay-%v-%v", os.Getpid(), rand.Int())
	// Objects init.
	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		doInit := func() (err error) {
			// Initialize publisher with private api.
			if thePublisher, err = newTestPublisher(registerDefaultCodecs, func(pub *testPublisher) error {
				pub.streamSuffix = streamSuffix
				pub.iceReadyCancel = publishReadyCancel
				resources = append(resources, pub)

				return pub.Setup(*srsVnetClientIP)
			}); err != nil {
				return err
			}

			// Init done.
			mainReadyCancel()

			<-ctx.Done()
			return nil
		}

		if err := doInit(); err != nil {
			r1 = err
		}
	}()

	// Run publisher.
	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		select {
		case <-ctx.Done():
		case <-mainReady.Done():
			r2 = thePublisher.Run(logger.WithContext(ctx), cancel)
			logger.Tf(ctx, "pub done")
		}
	}()

	// Run player.
	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		select {
		case <-ctx.Done():
			return
		case <-publishReady.Done():
		}

		player := NewFLVPlayer()
		defer player.Close()

		r3 = func() error {
			flvUrl := fmt.Sprintf("http://%v%v-%v.flv", *srsHttpServer, *srsStream, streamSuffix)
			if err := player.Play(ctx, flvUrl); err != nil {
				return err
			}

			var nnVideo, nnAudio int
			var hasVideo, hasAudio bool
			player.onRecvHeader = func(ha, hv bool) error {
				hasAudio, hasVideo = ha, hv
				return nil
			}
			player.onRecvTag = func(tagType flv.TagType, size, timestamp uint32, tag []byte) error {
				if tagType == flv.TagTypeAudio {
					nnAudio++
				} else if tagType == flv.TagTypeVideo {
					nnVideo++
				}
				logger.Tf(ctx, "got %v tag, %v %vms %vB", nnVideo+nnAudio, tagType, timestamp, len(tag))

				if audioPacketsOK, videoPacketsOK := !hasAudio || nnAudio >= 10, !hasVideo || nnVideo >= 10; audioPacketsOK && videoPacketsOK {
					logger.Tf(ctx, "Flv recv %v/%v audio, %v/%v video", hasAudio, nnAudio, hasVideo, nnVideo)
					cancel()
				}
				return nil
			}
			if err := player.Consume(ctx); err != nil {
				return err
			}

			return nil
		}()
	}()
}
