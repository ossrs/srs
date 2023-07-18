// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package nack

import (
	"math/rand"
	"sync"
	"time"

	"github.com/pion/interceptor"
	"github.com/pion/logging"
	"github.com/pion/rtcp"
)

// GeneratorInterceptorFactory is a interceptor.Factory for a GeneratorInterceptor
type GeneratorInterceptorFactory struct {
	opts []GeneratorOption
}

// NewInterceptor constructs a new ReceiverInterceptor
func (g *GeneratorInterceptorFactory) NewInterceptor(_ string) (interceptor.Interceptor, error) {
	i := &GeneratorInterceptor{
		size:        512,
		skipLastN:   0,
		interval:    time.Millisecond * 100,
		receiveLogs: map[uint32]*receiveLog{},
		close:       make(chan struct{}),
		log:         logging.NewDefaultLoggerFactory().NewLogger("nack_generator"),
	}

	for _, opt := range g.opts {
		if err := opt(i); err != nil {
			return nil, err
		}
	}

	if _, err := newReceiveLog(i.size); err != nil {
		return nil, err
	}

	return i, nil
}

// GeneratorInterceptor interceptor generates nack feedback messages.
type GeneratorInterceptor struct {
	interceptor.NoOp
	size      uint16
	skipLastN uint16
	interval  time.Duration
	m         sync.Mutex
	wg        sync.WaitGroup
	close     chan struct{}
	log       logging.LeveledLogger

	receiveLogs   map[uint32]*receiveLog
	receiveLogsMu sync.Mutex
}

// NewGeneratorInterceptor returns a new GeneratorInterceptorFactory
func NewGeneratorInterceptor(opts ...GeneratorOption) (*GeneratorInterceptorFactory, error) {
	return &GeneratorInterceptorFactory{opts}, nil
}

// BindRTCPWriter lets you modify any outgoing RTCP packets. It is called once per PeerConnection. The returned method
// will be called once per packet batch.
func (n *GeneratorInterceptor) BindRTCPWriter(writer interceptor.RTCPWriter) interceptor.RTCPWriter {
	n.m.Lock()
	defer n.m.Unlock()

	if n.isClosed() {
		return writer
	}

	n.wg.Add(1)

	go n.loop(writer)

	return writer
}

// BindRemoteStream lets you modify any incoming RTP packets. It is called once for per RemoteStream. The returned method
// will be called once per rtp packet.
func (n *GeneratorInterceptor) BindRemoteStream(info *interceptor.StreamInfo, reader interceptor.RTPReader) interceptor.RTPReader {
	if !streamSupportNack(info) {
		return reader
	}

	// error is already checked in NewGeneratorInterceptor
	receiveLog, _ := newReceiveLog(n.size)
	n.receiveLogsMu.Lock()
	n.receiveLogs[info.SSRC] = receiveLog
	n.receiveLogsMu.Unlock()

	return interceptor.RTPReaderFunc(func(b []byte, a interceptor.Attributes) (int, interceptor.Attributes, error) {
		i, attr, err := reader.Read(b, a)
		if err != nil {
			return 0, nil, err
		}

		if attr == nil {
			attr = make(interceptor.Attributes)
		}
		header, err := attr.GetRTPHeader(b[:i])
		if err != nil {
			return 0, nil, err
		}
		receiveLog.add(header.SequenceNumber)

		return i, attr, nil
	})
}

// UnbindRemoteStream is called when the Stream is removed. It can be used to clean up any data related to that track.
func (n *GeneratorInterceptor) UnbindRemoteStream(info *interceptor.StreamInfo) {
	n.receiveLogsMu.Lock()
	delete(n.receiveLogs, info.SSRC)
	n.receiveLogsMu.Unlock()
}

// Close closes the interceptor
func (n *GeneratorInterceptor) Close() error {
	defer n.wg.Wait()
	n.m.Lock()
	defer n.m.Unlock()

	if !n.isClosed() {
		close(n.close)
	}

	return nil
}

func (n *GeneratorInterceptor) loop(rtcpWriter interceptor.RTCPWriter) {
	defer n.wg.Done()

	senderSSRC := rand.Uint32() // #nosec

	ticker := time.NewTicker(n.interval)
	defer ticker.Stop()
	for {
		select {
		case <-ticker.C:
			func() {
				n.receiveLogsMu.Lock()
				defer n.receiveLogsMu.Unlock()

				for ssrc, receiveLog := range n.receiveLogs {
					missing := receiveLog.missingSeqNumbers(n.skipLastN)
					if len(missing) == 0 {
						continue
					}

					nack := &rtcp.TransportLayerNack{
						SenderSSRC: senderSSRC,
						MediaSSRC:  ssrc,
						Nacks:      rtcp.NackPairsFromSequenceNumbers(missing),
					}

					if _, err := rtcpWriter.Write([]rtcp.Packet{nack}, interceptor.Attributes{}); err != nil {
						n.log.Warnf("failed sending nack: %+v", err)
					}
				}
			}()
		case <-n.close:
			return
		}
	}
}

func (n *GeneratorInterceptor) isClosed() bool {
	select {
	case <-n.close:
		return true
	default:
		return false
	}
}
