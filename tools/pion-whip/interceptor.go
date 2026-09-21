// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"encoding/binary"
	"strconv"
	"strings"
	"sync"

	"github.com/pion/interceptor"
	"github.com/pion/rtcp"
	"github.com/pion/rtp"
)

// nackLogger is an interceptor that logs one line per NACK event, so a test can
// verify retransmission from the tool's log alone. Register it before pion's
// default interceptors: it then sits between the NACK responder and the wire and
// sees every resend the responder writes, and it reads the RTCP closest to the
// wire, so it sees every NACK before the responder does. It never changes a
// packet. On publish it logs, through logf:
//
//	NACK received ssrc=<media>, seqs=[<seq>,...]
//	Resend plain seq=<seq>, ssrc=<media>, pt=<pt>
//	Resend RTX seq=<rtx seq>, ssrc=<rtx>, pt=<rtx pt>, osn=<seq>
//
// A resend is a write of a sequence that a NACK requested: on the media SSRC it
// is plain, on the RTX SSRC of the stream it is RTX with the original sequence
// in the two-byte OSN that starts the payload. pion tells the RTX SSRC of a
// stream in its stream info once the answer negotiated rtx; without rtx the
// info carries none and the responder resends on the media SSRC.
type nackLogger struct {
	interceptor.NoOp
	logf func(format string, args ...interface{})

	mu sync.Mutex
	// streams holds the media SSRC of every bound local stream, so a NACK for a stream this peer does not send is
	// not this peer's event.
	streams map[uint32]bool
	// rtxToMedia maps the RTX SSRC of a bound stream to its media SSRC, from the negotiated stream info.
	rtxToMedia map[uint32]uint32
	// requested holds, per media SSRC, the sequences NACKs asked for and not yet resent.
	requested map[uint32]map[uint16]bool
	// Counters for the final summary.
	packets, nacksReceived, resentPlain, resentRtx int
}

func newNackLogger(logf func(format string, args ...interface{})) *nackLogger {
	return &nackLogger{
		logf:       logf,
		streams:    make(map[uint32]bool),
		rtxToMedia: make(map[uint32]uint32),
		requested:  make(map[uint32]map[uint16]bool),
	}
}

// NewInterceptor makes the logger its own factory for interceptor.Registry.
func (v *nackLogger) NewInterceptor(id string) (interceptor.Interceptor, error) {
	return v, nil
}

// Stats returns how many RTP packets were written, how many NACKs were received and how many sequences were
// resent, plain and RTX.
func (v *nackLogger) Stats() (packets, nacksReceived, resentPlain, resentRtx int) {
	v.mu.Lock()
	defer v.mu.Unlock()
	return v.packets, v.nacksReceived, v.resentPlain, v.resentRtx
}

// BindRTCPReader logs every Generic NACK the peer sends for a stream this side sends, then hands the packets back
// unchanged to the chain above.
func (v *nackLogger) BindRTCPReader(reader interceptor.RTCPReader) interceptor.RTCPReader {
	return interceptor.RTCPReaderFunc(func(b []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
		n, attrs, err := reader.Read(b, attributes)
		if err != nil {
			return n, attrs, err
		}
		if pkts, err := rtcp.Unmarshal(b[:n]); err == nil {
			for _, pkt := range pkts {
				if nack, ok := pkt.(*rtcp.TransportLayerNack); ok {
					v.onNackReceived(nack)
				}
			}
		}
		return n, attrs, nil
	})
}

func (v *nackLogger) onNackReceived(nack *rtcp.TransportLayerNack) {
	var seqs []uint16
	for _, pair := range nack.Nacks {
		seqs = append(seqs, pair.PacketList()...)
	}

	v.mu.Lock()
	defer v.mu.Unlock()
	if !v.streams[nack.MediaSSRC] {
		return
	}
	set := v.requested[nack.MediaSSRC]
	if set == nil {
		set = make(map[uint16]bool)
		v.requested[nack.MediaSSRC] = set
	}
	for _, seq := range seqs {
		set[seq] = true
	}
	v.nacksReceived++
	v.logf("NACK received ssrc=%d, seqs=[%s]", nack.MediaSSRC, joinSeqs(seqs))
}

// BindLocalStream remembers the media SSRC of the stream and its RTX SSRC when the answer negotiated rtx, then
// watches every packet written to the stream, the originals and the resends the NACK responder writes, and logs the
// ones that answer a requested sequence. The packet is written unchanged.
func (v *nackLogger) BindLocalStream(info *interceptor.StreamInfo, writer interceptor.RTPWriter) interceptor.RTPWriter {
	v.mu.Lock()
	v.streams[info.SSRC] = true
	if info.SSRCRetransmission != 0 {
		v.rtxToMedia[info.SSRCRetransmission] = info.SSRC
	}
	v.mu.Unlock()

	return interceptor.RTPWriterFunc(func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
		v.onWrite(header, payload)
		return writer.Write(header, payload, attributes)
	})
}

// UnbindLocalStream forgets the stream.
func (v *nackLogger) UnbindLocalStream(info *interceptor.StreamInfo) {
	v.mu.Lock()
	defer v.mu.Unlock()
	delete(v.streams, info.SSRC)
	delete(v.rtxToMedia, info.SSRCRetransmission)
	delete(v.requested, info.SSRC)
}

func (v *nackLogger) onWrite(header *rtp.Header, payload []byte) {
	v.mu.Lock()
	defer v.mu.Unlock()
	v.packets++

	if media, isRtx := v.rtxToMedia[header.SSRC]; isRtx {
		// An RTX packet too short for an OSN is a padding probe, which answers no NACK.
		if len(payload) < 2 {
			return
		}
		osn := binary.BigEndian.Uint16(payload[:2])
		if !v.takeRequested(media, osn) {
			return
		}
		v.resentRtx++
		v.logf("Resend RTX seq=%d, ssrc=%d, pt=%d, osn=%d", header.SequenceNumber, header.SSRC, header.PayloadType, osn)
		return
	}

	if !v.takeRequested(header.SSRC, header.SequenceNumber) {
		return
	}
	v.resentPlain++
	v.logf("Resend plain seq=%d, ssrc=%d, pt=%d", header.SequenceNumber, header.SSRC, header.PayloadType)
}

// takeRequested reports whether seq was requested for the media SSRC and forgets it, so a second write that answers
// no NACK is not a resend. The caller holds the lock.
func (v *nackLogger) takeRequested(mediaSSRC uint32, seq uint16) bool {
	set := v.requested[mediaSSRC]
	if set == nil || !set[seq] {
		return false
	}
	delete(set, seq)
	return true
}

func joinSeqs(seqs []uint16) string {
	parts := make([]string, 0, len(seqs))
	for _, seq := range seqs {
		parts = append(parts, strconv.Itoa(int(seq)))
	}
	return strings.Join(parts, ",")
}
