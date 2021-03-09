// The MIT License (MIT)
//
// Copyright (c) 2021 srs-bench(ossrs)
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
	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	"github.com/pion/interceptor"
	"github.com/pion/rtcp"
	"github.com/pion/rtp"
	"github.com/pion/transport/vnet"
	"math/rand"
	"os"
	"sync"
	"testing"
	"time"
)

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

	var wg sync.WaitGroup
	defer wg.Wait()

	// The event notify.
	var thePublisher *TestPublisher
	var thePlayer *TestPlayer
	mainReady, mainReadyCancel := context.WithCancel(context.Background())
	publishReady, publishReadyCancel := context.WithCancel(context.Background())

	// Objects init.
	wg.Add(1)
	go func() {
		defer wg.Done()
		defer cancel()

		doInit := func() error {
			playOK := *srsPlayOKPackets
			vnetClientIP := *srsVnetClientIP

			// Create top level test object.
			api, err := NewTestWebRTCAPI()
			if err != nil {
				return err
			}
			defer api.Close()

			streamSuffix := fmt.Sprintf("basic-publish-play-%v-%v", os.Getpid(), rand.Int())
			play := NewTestPlayer(api, func(play *TestPlayer) {
				play.streamSuffix = streamSuffix
			})
			defer play.Close()

			pub := NewTestPublisher(api, func(pub *TestPublisher) {
				pub.streamSuffix = streamSuffix
				pub.iceReadyCancel = publishReadyCancel
			})
			defer pub.Close()

			if err := api.Setup(vnetClientIP, func(api *TestWebRTCAPI) {
				var nnWriteRTP, nnReadRTP, nnWriteRTCP, nnReadRTCP int64
				api.registry.Add(NewRTPInterceptor(func(i *RTPInterceptor) {
					i.rtpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
						nn, attr, err := i.nextRTPReader.Read(buf, attributes)
						nnReadRTP++
						return nn, attr, err
					}
					i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
						nn, err := i.nextRTPWriter.Write(header, payload, attributes)

						nnWriteRTP++
						logger.Tf(ctx, "publish rtp=(read:%v write:%v), rtcp=(read:%v write:%v) packets",
							nnReadRTP, nnWriteRTP, nnReadRTCP, nnWriteRTCP)
						return nn, err
					}
				}))
				api.registry.Add(NewRTCPInterceptor(func(i *RTCPInterceptor) {
					i.rtcpReader = func(buf []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
						nn, attr, err := i.nextRTCPReader.Read(buf, attributes)
						nnReadRTCP++
						return nn, attr, err
					}
					i.rtcpWriter = func(pkts []rtcp.Packet, attributes interceptor.Attributes) (int, error) {
						nn, err := i.nextRTCPWriter.Write(pkts, attributes)
						nnWriteRTCP++
						return nn, err
					}
				}))
			}, func(api *TestWebRTCAPI) {
				var nn uint64
				api.registry.Add(NewRTPInterceptor(func(i *RTPInterceptor) {
					i.rtpReader = func(payload []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
						if nn++; nn >= uint64(playOK) {
							cancel() // Completed.
						}
						logger.Tf(ctx, "play got %v packets", nn)
						return i.nextRTPReader.Read(payload, attributes)
					}
				}))
			}); err != nil {
				return err
			}

			// Set the available objects.
			mainReadyCancel()
			thePublisher = pub
			thePlayer = play

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
			return
		case <-mainReady.Done():
		}

		doPublish := func() error {
			if err := thePublisher.Run(logger.WithContext(ctx), cancel); err != nil {
				return err
			}

			logger.Tf(ctx, "pub done")
			return nil
		}
		if err := doPublish(); err != nil {
			r2 = err
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
		case <-mainReady.Done():
		}

		select {
		case <-ctx.Done():
			return
		case <-publishReady.Done():
		}

		doPlay := func() error {
			if err := thePlayer.Run(logger.WithContext(ctx), cancel); err != nil {
				return err
			}

			logger.Tf(ctx, "play done")
			return nil
		}
		if err := doPlay(); err != nil {
			r3 = err
		}

	}()
}

// The srs-server is DTLS server(passive), srs-bench is DTLS client which is active mode.
//     No.1  srs-bench: ClientHello
//     No.2 srs-server: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//     No.3  srs-bench: Certificate, ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//     No.4 srs-server: ChangeCipherSpec, Finished
func TestRtcDTLS_ClientActive_Default(t *testing.T) {
	if err := filterTestError(func() error {
		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		publishOK, vnetClientIP := *srsPublishOKPackets, *srsVnetClientIP

		// Create top level test object.
		api, err := NewTestWebRTCAPI()
		if err != nil {
			return err
		}
		defer api.Close()

		streamSuffix := fmt.Sprintf("dtls-passive-no-arq-%v-%v", os.Getpid(), rand.Int())
		p := NewTestPublisher(api, func(p *TestPublisher) {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupActive
		})
		defer p.Close()

		if err := api.Setup(vnetClientIP, func(api *TestWebRTCAPI) {
			var nn int64
			api.registry.Add(NewRTPInterceptor(func(i *RTPInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					if nn++; nn >= int64(publishOK) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v packets", nn)
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
		}, func(api *TestWebRTCAPI) {
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := NewChunkMessageType(c)
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
//     No.1 srs-server: ClientHello
//     No.2  srs-bench: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//     No.3 srs-server: Certificate, ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//     No.4  srs-bench: ChangeCipherSpec, Finished
func TestRtcDTLS_ClientPassive_Default(t *testing.T) {
	if err := filterTestError(func() error {
		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		publishOK, vnetClientIP := *srsPublishOKPackets, *srsVnetClientIP

		// Create top level test object.
		api, err := NewTestWebRTCAPI()
		if err != nil {
			return err
		}
		defer api.Close()

		streamSuffix := fmt.Sprintf("dtls-active-no-arq-%v-%v", os.Getpid(), rand.Int())
		p := NewTestPublisher(api, func(p *TestPublisher) {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
		})
		defer p.Close()

		if err := api.Setup(vnetClientIP, func(api *TestWebRTCAPI) {
			var nn int64
			api.registry.Add(NewRTPInterceptor(func(i *RTPInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					if nn++; nn >= int64(publishOK) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v packets", nn)
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
		}, func(api *TestWebRTCAPI) {
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := NewChunkMessageType(c)
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
		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		publishOK, vnetClientIP := *srsPublishOKPackets, *srsVnetClientIP

		// Create top level test object.
		api, err := NewTestWebRTCAPI()
		if err != nil {
			return err
		}
		defer api.Close()

		streamSuffix := fmt.Sprintf("dtls-active-no-arq-%v-%v", os.Getpid(), rand.Int())
		p := NewTestPublisher(api, func(p *TestPublisher) {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupActive
		})
		defer p.Close()

		if err := api.Setup(vnetClientIP, func(api *TestWebRTCAPI) {
			var nn int64
			api.registry.Add(NewRTPInterceptor(func(i *RTPInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					if nn++; nn >= int64(publishOK) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v packets", nn)
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
		}, func(api *TestWebRTCAPI) {
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := NewChunkMessageType(c)
				if !parsed || chunk.chunk != ChunkTypeDTLS {
					return true
				}

				// Copy the alert to server, ignore error.
				if chunk.content == DTLSContentTypeAlert {
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
		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		publishOK, vnetClientIP := *srsPublishOKPackets, *srsVnetClientIP

		// Create top level test object.
		api, err := NewTestWebRTCAPI()
		if err != nil {
			return err
		}
		defer api.Close()

		streamSuffix := fmt.Sprintf("dtls-active-no-arq-%v-%v", os.Getpid(), rand.Int())
		p := NewTestPublisher(api, func(p *TestPublisher) {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
		})
		defer p.Close()

		if err := api.Setup(vnetClientIP, func(api *TestWebRTCAPI) {
			var nn int64
			api.registry.Add(NewRTPInterceptor(func(i *RTPInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					if nn++; nn >= int64(publishOK) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v packets", nn)
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
		}, func(api *TestWebRTCAPI) {
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := NewChunkMessageType(c)
				if !parsed || chunk.chunk != ChunkTypeDTLS {
					return true
				}

				// Copy the alert to server, ignore error.
				if chunk.content == DTLSContentTypeAlert {
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
//        No.3 srs-server: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//        No.4  srs-bench: Certificate, ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//        No.5 srs-server: ChangeCipherSpec, Finished
//
// @remark The pion is active, so it can be consider a benchmark for DTLS server.
func TestRtcDTLS_ClientActive_ARQ_ClientHello_ByDropped_ClientHello(t *testing.T) {
	var r0 error
	err := func() error {
		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		publishOK, vnetClientIP := *srsPublishOKPackets, *srsVnetClientIP

		// Create top level test object.
		api, err := NewTestWebRTCAPI()
		if err != nil {
			return err
		}
		defer api.Close()

		streamSuffix := fmt.Sprintf("dtls-active-arq-client-hello-%v-%v", os.Getpid(), rand.Int())
		p := NewTestPublisher(api, func(p *TestPublisher) {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupActive
		})
		defer p.Close()

		if err := api.Setup(vnetClientIP, func(api *TestWebRTCAPI) {
			var nn int64
			api.registry.Add(NewRTPInterceptor(func(i *RTPInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					if nn++; nn >= int64(publishOK) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v packets", nn)
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
		}, func(api *TestWebRTCAPI) {
			nnClientHello, nnMaxDrop := 0, 1
			var lastClientHello *DTLSRecord

			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := NewChunkMessageType(c)
				if !parsed || chunk.chunk != ChunkTypeDTLS || chunk.content != DTLSContentTypeHandshake || chunk.handshake != DTLSHandshakeTypeClientHello {
					return true
				}

				record, err := NewDTLSRecord(c.UserData())
				if err != nil {
					return true
				}

				if lastClientHello != nil && record.Equals(lastClientHello) {
					r0 = errors.Errorf("dup record %v", record)
				}
				lastClientHello = record

				nnClientHello++
				ok = (nnClientHello > nnMaxDrop)
				logger.Tf(ctx, "NN=%v, Chunk %v, %v, ok=%v %v bytes", nnClientHello, chunk, record, ok, len(c.UserData()))
				return
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()
	if err := filterTestError(err, r0); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS client, srs-bench is DTLS server which is passive mode.
// [Drop] No.1 srs-server: ClientHello(Epoch=0, Sequence=0)
// [ARQ]  No.2 srs-server: ClientHello(Epoch=0, Sequence=1)
//        No.3  srs-bench: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//        No.4 srs-server: Certificate, ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//        No.5  srs-bench: ChangeCipherSpec, Finished
//
// @remark If retransmit the ClientHello, with the same epoch+sequence, peer will request HelloVerifyRequest, then
// openssl will create a new ClientHello with increased sequence. It's ok, but waste a lots of duplicated ClientHello
// packets, so we fail the test, requires the epoch+sequence never dup, even for ARQ.
func TestRtcDTLS_ClientPassive_ARQ_ClientHello_ByDropped_ClientHello(t *testing.T) {
	var r0 error
	err := func() error {
		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		publishOK, vnetClientIP := *srsPublishOKPackets, *srsVnetClientIP

		// Create top level test object.
		api, err := NewTestWebRTCAPI()
		if err != nil {
			return err
		}
		defer api.Close()

		streamSuffix := fmt.Sprintf("dtls-passive-arq-client-hello-%v-%v", os.Getpid(), rand.Int())
		p := NewTestPublisher(api, func(p *TestPublisher) {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
		})
		defer p.Close()

		if err := api.Setup(vnetClientIP, func(api *TestWebRTCAPI) {
			var nn int64
			api.registry.Add(NewRTPInterceptor(func(i *RTPInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					if nn++; nn >= int64(publishOK) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v packets", nn)
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
		}, func(api *TestWebRTCAPI) {
			nnClientHello, nnMaxDrop := 0, 1
			var lastClientHello *DTLSRecord

			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := NewChunkMessageType(c)
				if !parsed || chunk.chunk != ChunkTypeDTLS || chunk.content != DTLSContentTypeHandshake || chunk.handshake != DTLSHandshakeTypeClientHello {
					return true
				}

				record, err := NewDTLSRecord(c.UserData())
				if err != nil {
					return true
				}

				if lastClientHello != nil && record.Equals(lastClientHello) {
					r0 = errors.Errorf("dup record %v", record)
				}
				lastClientHello = record

				nnClientHello++
				ok = (nnClientHello > nnMaxDrop)
				logger.Tf(ctx, "NN=%v, Chunk %v, %v, ok=%v %v bytes", nnClientHello, chunk, record, ok, len(c.UserData()))
				return
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()
	if err := filterTestError(err, r0); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS server, srs-bench is DTLS client which is active mode.
//        No.1  srs-bench: ClientHello(Epoch=0, Sequence=0)
// [Drop] No.2 srs-server: ServerHello(Epoch=0, Sequence=0), Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
// [ARQ]  No.2  srs-bench: ClientHello(Epoch=0, Sequence=1)
// [ARQ]  No.3 srs-server: ServerHello(Epoch=0, Sequence=5), Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//        No.4  srs-bench: Certificate, ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//        No.5 srs-server: ChangeCipherSpec, Finished
//
// @remark The pion is active, so it can be consider a benchmark for DTLS server.
func TestRtcDTLS_ClientActive_ARQ_ClientHello_ByDropped_ServerHello(t *testing.T) {
	var r0, r1 error
	err := func() error {
		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		publishOK, vnetClientIP := *srsPublishOKPackets, *srsVnetClientIP

		// Create top level test object.
		api, err := NewTestWebRTCAPI()
		if err != nil {
			return err
		}
		defer api.Close()

		streamSuffix := fmt.Sprintf("dtls-active-arq-client-hello-%v-%v", os.Getpid(), rand.Int())
		p := NewTestPublisher(api, func(p *TestPublisher) {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupActive
		})
		defer p.Close()

		if err := api.Setup(vnetClientIP, func(api *TestWebRTCAPI) {
			var nn int64
			api.registry.Add(NewRTPInterceptor(func(i *RTPInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					if nn++; nn >= int64(publishOK) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v packets", nn)
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
		}, func(api *TestWebRTCAPI) {
			nnServerHello, nnMaxDrop := 0, 1
			var lastClientHello, lastServerHello *DTLSRecord

			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := NewChunkMessageType(c)
				if !parsed || chunk.chunk != ChunkTypeDTLS || chunk.content != DTLSContentTypeHandshake ||
					(chunk.handshake != DTLSHandshakeTypeClientHello && chunk.handshake != DTLSHandshakeTypeServerHello) {
					return true
				}

				record, err := NewDTLSRecord(c.UserData())
				if err != nil {
					return true
				}

				if chunk.handshake == DTLSHandshakeTypeClientHello {
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
				ok = (nnServerHello > nnMaxDrop)
				logger.Tf(ctx, "NN=%v, Chunk %v, %v, ok=%v %v bytes", nnServerHello, chunk, record, ok, len(c.UserData()))
				return
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()
	if err := filterTestError(err, r0, r1); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS client, srs-bench is DTLS server which is passive mode.
//        No.1 srs-server: ClientHello(Epoch=0, Sequence=0)
// [Drop] No.2  srs-bench: ServerHello(Epoch=0, Sequence=0), Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
// [ARQ]  No.2 srs-server: ClientHello(Epoch=0, Sequence=1)
// [ARQ]  No.3  srs-bench: ServerHello(Epoch=0, Sequence=5), Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//        No.4 srs-server: Certificate, ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//        No.5  srs-bench: ChangeCipherSpec, Finished
//
// @remark If retransmit the ClientHello, with the same epoch+sequence, peer will request HelloVerifyRequest, then
// openssl will create a new ClientHello with increased sequence. It's ok, but waste a lots of duplicated ClientHello
// packets, so we fail the test, requires the epoch+sequence never dup, even for ARQ.
func TestRtcDTLS_ClientPassive_ARQ_ClientHello_ByDropped_ServerHello(t *testing.T) {
	var r0, r1 error
	err := func() error {
		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		publishOK, vnetClientIP := *srsPublishOKPackets, *srsVnetClientIP

		// Create top level test object.
		api, err := NewTestWebRTCAPI()
		if err != nil {
			return err
		}
		defer api.Close()

		streamSuffix := fmt.Sprintf("dtls-passive-arq-client-hello-%v-%v", os.Getpid(), rand.Int())
		p := NewTestPublisher(api, func(p *TestPublisher) {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
		})
		defer p.Close()

		if err := api.Setup(vnetClientIP, func(api *TestWebRTCAPI) {
			var nn int64
			api.registry.Add(NewRTPInterceptor(func(i *RTPInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					if nn++; nn >= int64(publishOK) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v packets", nn)
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
		}, func(api *TestWebRTCAPI) {
			nnServerHello, nnMaxDrop := 0, 1
			var lastClientHello, lastServerHello *DTLSRecord

			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := NewChunkMessageType(c)
				if !parsed || chunk.chunk != ChunkTypeDTLS || chunk.content != DTLSContentTypeHandshake ||
					(chunk.handshake != DTLSHandshakeTypeClientHello && chunk.handshake != DTLSHandshakeTypeServerHello) {
					return true
				}

				record, err := NewDTLSRecord(c.UserData())
				if err != nil {
					return true
				}

				if chunk.handshake == DTLSHandshakeTypeClientHello {
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
				ok = (nnServerHello > nnMaxDrop)
				logger.Tf(ctx, "NN=%v, Chunk %v, %v, ok=%v %v bytes", nnServerHello, chunk, record, ok, len(c.UserData()))
				return
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()
	if err := filterTestError(err, r0, r1); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS server, srs-bench is DTLS client which is active mode.
//        No.1  srs-bench: ClientHello
//        No.2 srs-server: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
// [Drop] No.3  srs-bench: Certificate(Epoch=0, Sequence=0), ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
// [ARQ]  No.4  srs-bench: Certificate(Epoch=0, Sequence=5), ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//        No.5 srs-server: ChangeCipherSpec, Finished
//
// @remark The pion is active, so it can be consider a benchmark for DTLS server.
func TestRtcDTLS_ClientActive_ARQ_Certificate_ByDropped_Certificate(t *testing.T) {
	var r0 error
	err := func() error {
		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		publishOK, vnetClientIP := *srsPublishOKPackets, *srsVnetClientIP

		// Create top level test object.
		api, err := NewTestWebRTCAPI()
		if err != nil {
			return err
		}
		defer api.Close()

		streamSuffix := fmt.Sprintf("dtls-active-arq-certificate-%v-%v", os.Getpid(), rand.Int())
		p := NewTestPublisher(api, func(p *TestPublisher) {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupActive
		})
		defer p.Close()

		if err := api.Setup(vnetClientIP, func(api *TestWebRTCAPI) {
			var nn int64
			api.registry.Add(NewRTPInterceptor(func(i *RTPInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					if nn++; nn >= int64(publishOK) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v packets", nn)
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
		}, func(api *TestWebRTCAPI) {
			nnCertificate, nnMaxDrop := 0, 1
			var lastCertificate *DTLSRecord

			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := NewChunkMessageType(c)
				if !parsed || chunk.chunk != ChunkTypeDTLS || chunk.content != DTLSContentTypeHandshake || chunk.handshake != DTLSHandshakeTypeCertificate {
					return true
				}

				record, err := NewDTLSRecord(c.UserData())
				if err != nil {
					return true
				}

				if lastCertificate != nil && lastCertificate.Equals(record) {
					r0 = errors.Errorf("dup record %v", record)
				}
				lastCertificate = record

				nnCertificate++
				ok = (nnCertificate > nnMaxDrop)
				logger.Tf(ctx, "NN=%v, Chunk %v, %v, ok=%v %v bytes", nnCertificate, chunk, record, ok, len(c.UserData()))
				return
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()
	if err := filterTestError(err, r0); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS client, srs-bench is DTLS server which is passive mode.
//        No.1 srs-server: ClientHello
//        No.2  srs-bench: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
// [Drop] No.3 srs-server: Certificate(Epoch=0, Sequence=0), ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
// [ARQ]  No.4 srs-server: Certificate(Epoch=0, Sequence=5), ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
//        No.5  srs-bench: ChangeCipherSpec, Finished
//
// @remark If retransmit the Certificate, with the same epoch+sequence, peer will drop the message. It's ok right now, but
// wast some packets, so we check the epoch+sequence which should never dup, even for ARQ.
func TestRtcDTLS_ClientPassive_ARQ_Certificate_ByDropped_Certificate(t *testing.T) {
	var r0 error
	err := func() error {
		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		publishOK, vnetClientIP := *srsPublishOKPackets, *srsVnetClientIP

		// Create top level test object.
		api, err := NewTestWebRTCAPI()
		if err != nil {
			return err
		}
		defer api.Close()

		streamSuffix := fmt.Sprintf("dtls-passive-arq-certificate-%v-%v", os.Getpid(), rand.Int())
		p := NewTestPublisher(api, func(p *TestPublisher) {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
		})
		defer p.Close()

		if err := api.Setup(vnetClientIP, func(api *TestWebRTCAPI) {
			var nn int64
			api.registry.Add(NewRTPInterceptor(func(i *RTPInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					if nn++; nn >= int64(publishOK) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v packets", nn)
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
		}, func(api *TestWebRTCAPI) {
			nnCertificate, nnMaxDrop := 0, 1
			var lastCertificate *DTLSRecord

			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := NewChunkMessageType(c)
				if !parsed || chunk.chunk != ChunkTypeDTLS || chunk.content != DTLSContentTypeHandshake || chunk.handshake != DTLSHandshakeTypeCertificate {
					return true
				}

				record, err := NewDTLSRecord(c.UserData())
				if err != nil {
					return true
				}

				if lastCertificate != nil && lastCertificate.Equals(record) {
					r0 = errors.Errorf("dup record %v", record)
				}
				lastCertificate = record

				nnCertificate++
				ok = (nnCertificate > nnMaxDrop)
				logger.Tf(ctx, "NN=%v, Chunk %v, %v, ok=%v %v bytes", nnCertificate, chunk, record, ok, len(c.UserData()))
				return
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()
	if err := filterTestError(err, r0); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS server, srs-bench is DTLS client which is active mode.
//        No.1  srs-bench: ClientHello
//        No.2 srs-server: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//        No.3  srs-bench: Certificate(Epoch=0, Sequence=0), ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
// [Drop] No.5 srs-server: ChangeCipherSpec, Finished
// [ARQ]  No.6  srs-bench: Certificate(Epoch=0, Sequence=5), ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
// [ARQ]  No.7 srs-server: ChangeCipherSpec, Finished
//
// @remark The pion is active, so it can be consider a benchmark for DTLS server.
func TestRtcDTLS_ClientActive_ARQ_Certificate_ByDropped_ChangeCipherSpec(t *testing.T) {
	var r0, r1 error
	err := func() error {
		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		publishOK, vnetClientIP := *srsPublishOKPackets, *srsVnetClientIP

		// Create top level test object.
		api, err := NewTestWebRTCAPI()
		if err != nil {
			return err
		}
		defer api.Close()

		streamSuffix := fmt.Sprintf("dtls-active-arq-certificate-%v-%v", os.Getpid(), rand.Int())
		p := NewTestPublisher(api, func(p *TestPublisher) {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupActive
		})
		defer p.Close()

		if err := api.Setup(vnetClientIP, func(api *TestWebRTCAPI) {
			var nn int64
			api.registry.Add(NewRTPInterceptor(func(i *RTPInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					if nn++; nn >= int64(publishOK) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v packets", nn)
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
		}, func(api *TestWebRTCAPI) {
			nnCertificate, nnMaxDrop := 0, 1
			var lastChangeCipherSepc, lastCertifidate *DTLSRecord

			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := NewChunkMessageType(c)
				if !parsed || (!chunk.IsChangeCipherSpec() && !chunk.IsCertificate()) {
					return true
				}

				record, err := NewDTLSRecord(c.UserData())
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
				ok = (nnCertificate > nnMaxDrop)
				logger.Tf(ctx, "NN=%v, Chunk %v, %v, ok=%v %v bytes", nnCertificate, chunk, record, ok, len(c.UserData()))
				return
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()
	if err := filterTestError(err, r0, r1); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS client, srs-bench is DTLS server which is passive mode.
//        No.1  srs-server: ClientHello
//        No.2 srs-bench: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone
//        No.3  srs-server: Certificate(Epoch=0, Sequence=0), ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
// [Drop] No.5 srs-bench: ChangeCipherSpec, Finished
// [ARQ]  No.6  srs-server: Certificate(Epoch=0, Sequence=5), ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished
// [ARQ]  No.7 srs-bench: ChangeCipherSpec, Finished
//
// @remark If retransmit the Certificate, with the same epoch+sequence, peer will drop the message, and never generate the
// ChangeCipherSpec, which will cause DTLS fail.
func TestRtcDTLS_ClientPassive_ARQ_Certificate_ByDropped_ChangeCipherSpec(t *testing.T) {
	var r0, r1 error
	err := func() error {
		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		publishOK, vnetClientIP := *srsPublishOKPackets, *srsVnetClientIP

		// Create top level test object.
		api, err := NewTestWebRTCAPI()
		if err != nil {
			return err
		}
		defer api.Close()

		streamSuffix := fmt.Sprintf("dtls-passive-arq-certificate-%v-%v", os.Getpid(), rand.Int())
		p := NewTestPublisher(api, func(p *TestPublisher) {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
		})
		defer p.Close()

		if err := api.Setup(vnetClientIP, func(api *TestWebRTCAPI) {
			var nn int64
			api.registry.Add(NewRTPInterceptor(func(i *RTPInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					if nn++; nn >= int64(publishOK) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v packets", nn)
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
		}, func(api *TestWebRTCAPI) {
			nnCertificate, nnMaxDrop := 0, 1
			var lastChangeCipherSepc, lastCertifidate *DTLSRecord

			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := NewChunkMessageType(c)
				if !parsed || (!chunk.IsChangeCipherSpec() && !chunk.IsCertificate()) {
					return true
				}

				record, err := NewDTLSRecord(c.UserData())
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
				ok = (nnCertificate > nnMaxDrop)
				logger.Tf(ctx, "NN=%v, Chunk %v, %v, ok=%v %v bytes", nnCertificate, chunk, record, ok, len(c.UserData()))
				return
			})
		}); err != nil {
			return err
		}

		return p.Run(ctx, cancel)
	}()
	if err := filterTestError(err, r0, r1); err != nil {
		t.Errorf("err %+v", err)
	}
}

// The srs-server is DTLS client(client), srs-bench is DTLS server which is passive mode.
// Drop all DTLS packets when got ClientHello, to test the server ARQ thread cleanup.
func TestRtcDTLS_ClientPassive_ARQ_DropAllAfter_ClientHello(t *testing.T) {
	if err := filterTestError(func() error {
		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		vnetClientIP, dtlsDropPackets := *srsVnetClientIP, *srsDTLSDropPackets

		// Create top level test object.
		api, err := NewTestWebRTCAPI()
		if err != nil {
			return err
		}
		defer api.Close()

		streamSuffix := fmt.Sprintf("dtls-passive-no-arq-%v-%v", os.Getpid(), rand.Int())
		p := NewTestPublisher(api, func(p *TestPublisher) {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
		})
		defer p.Close()

		if err := api.Setup(vnetClientIP, func(api *TestWebRTCAPI) {
			nnDrop, dropAll := 0, false
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := NewChunkMessageType(c)
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

					if nnDrop++; nnDrop >= dtlsDropPackets {
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
		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		vnetClientIP, dtlsDropPackets := *srsVnetClientIP, *srsDTLSDropPackets

		// Create top level test object.
		api, err := NewTestWebRTCAPI()
		if err != nil {
			return err
		}
		defer api.Close()

		streamSuffix := fmt.Sprintf("dtls-passive-no-arq-%v-%v", os.Getpid(), rand.Int())
		p := NewTestPublisher(api, func(p *TestPublisher) {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
		})
		defer p.Close()

		if err := api.Setup(vnetClientIP, func(api *TestWebRTCAPI) {
			nnDrop, dropAll := 0, false
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := NewChunkMessageType(c)
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

					if nnDrop++; nnDrop >= dtlsDropPackets {
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
		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		vnetClientIP, dtlsDropPackets := *srsVnetClientIP, *srsDTLSDropPackets

		// Create top level test object.
		api, err := NewTestWebRTCAPI()
		if err != nil {
			return err
		}
		defer api.Close()

		streamSuffix := fmt.Sprintf("dtls-passive-no-arq-%v-%v", os.Getpid(), rand.Int())
		p := NewTestPublisher(api, func(p *TestPublisher) {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
		})
		defer p.Close()

		if err := api.Setup(vnetClientIP, func(api *TestWebRTCAPI) {
			nnDrop, dropAll := 0, false
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := NewChunkMessageType(c)
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

					if nnDrop++; nnDrop >= dtlsDropPackets {
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
		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		vnetClientIP, dtlsDropPackets := *srsVnetClientIP, *srsDTLSDropPackets

		// Create top level test object.
		api, err := NewTestWebRTCAPI()
		if err != nil {
			return err
		}
		defer api.Close()

		streamSuffix := fmt.Sprintf("dtls-passive-no-arq-%v-%v", os.Getpid(), rand.Int())
		p := NewTestPublisher(api, func(p *TestPublisher) {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
		})
		defer p.Close()

		if err := api.Setup(vnetClientIP, func(api *TestWebRTCAPI) {
			nnDrop, dropAll := 0, false
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := NewChunkMessageType(c)
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

					if nnDrop++; nnDrop >= dtlsDropPackets {
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
		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		publishOK, vnetClientIP, dtlsDropPackets := *srsPublishOKPackets, *srsVnetClientIP, *srsDTLSDropPackets

		// Create top level test object.
		api, err := NewTestWebRTCAPI()
		if err != nil {
			return err
		}
		defer api.Close()

		streamSuffix := fmt.Sprintf("dtls-passive-no-arq-%v-%v", os.Getpid(), rand.Int())
		p := NewTestPublisher(api, func(p *TestPublisher) {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
		})
		defer p.Close()

		if err := api.Setup(vnetClientIP, func(api *TestWebRTCAPI) {
			var nn int64
			api.registry.Add(NewRTPInterceptor(func(i *RTPInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					if nn++; nn >= int64(publishOK) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v packets", nn)
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
		}, func(api *TestWebRTCAPI) {
			nnDropClientHello, nnDropCertificate := 0, 0
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := NewChunkMessageType(c)
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
						if nnDropCertificate >= dtlsDropPackets {
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
	var r0 error
	err := func() error {
		ctx, cancel := context.WithTimeout(logger.WithContext(context.Background()), time.Duration(*srsTimeout)*time.Millisecond)
		publishOK, vnetClientIP := *srsPublishOKPackets, *srsVnetClientIP

		// Create top level test object.
		api, err := NewTestWebRTCAPI()
		if err != nil {
			return err
		}
		defer api.Close()

		streamSuffix := fmt.Sprintf("dtls-passive-no-arq-%v-%v", os.Getpid(), rand.Int())
		p := NewTestPublisher(api, func(p *TestPublisher) {
			p.streamSuffix = streamSuffix
			p.onOffer = testUtilSetupPassive
		})
		defer p.Close()

		if err := api.Setup(vnetClientIP, func(api *TestWebRTCAPI) {
			var nn int64
			api.registry.Add(NewRTPInterceptor(func(i *RTPInterceptor) {
				i.rtpWriter = func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
					if nn++; nn >= int64(publishOK) {
						cancel() // Send enough packets, done.
					}
					logger.Tf(ctx, "publish write %v packets", nn)
					return i.nextRTPWriter.Write(header, payload, attributes)
				}
			}))
		}, func(api *TestWebRTCAPI) {
			nnDropClientHello, nnDropCertificate := 0, 0
			var firstCertificate time.Time
			api.router.AddChunkFilter(func(c vnet.Chunk) (ok bool) {
				chunk, parsed := NewChunkMessageType(c)
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
	if err := filterTestError(err, r0); err != nil {
		t.Errorf("err %+v", err)
	}
}
