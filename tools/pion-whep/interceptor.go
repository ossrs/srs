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

// nackLogger is an interceptor that logs one line per NACK event of the player, so a test can verify
// retransmission from the tool's log alone. Register it before pion's default interceptors: it then sits inside the
// NACK generator's RTCP writer and closest to the wire on the RTP readers, so it sees every NACK the generator
// sends and every packet as it arrives, an RTX packet before pion unwraps it. It never changes a packet. It logs,
// through logf:
//
//	NACK sent ssrc=<media>, seqs=[<seq>,...]
//	Recovered plain seq=<seq>, ssrc=<media>, pt=<pt>
//	Recovered RTX seq=<rtx seq>, ssrc=<rtx>, pt=<rtx pt>, osn=<seq>
//
// A recovery is the arrival of a sequence that a NACK asked for: on the media SSRC it is plain, on the RTX SSRC
// of the stream it is RTX with the original sequence in the two-byte OSN that starts the payload. pion binds the
// RTX repair stream with the media codec and only its SSRC tells it apart, so the RTX SSRC comes from the answer's
// FID group through SetRtx.
type nackLogger struct {
	interceptor.NoOp
	logf func(format string, args ...interface{})

	mu sync.Mutex
	// requested holds, per media SSRC, the sequences NACKs asked for and not yet recovered.
	requested map[uint32]map[uint16]bool
	// rtxToMedia maps an RTX SSRC to the media SSRC it repairs, from SetRtx.
	rtxToMedia map[uint32]uint32
	// Counters for the final summary.
	nacksSent, recoveredPlain, recoveredRtx int
}

func newNackLogger(logf func(format string, args ...interface{})) *nackLogger {
	return &nackLogger{
		logf:       logf,
		requested:  make(map[uint32]map[uint16]bool),
		rtxToMedia: make(map[uint32]uint32),
	}
}

// NewInterceptor makes the logger its own factory for interceptor.Registry.
func (v *nackLogger) NewInterceptor(id string) (interceptor.Interceptor, error) {
	return v, nil
}

// SetRtx binds an RTX SSRC to its media SSRC, from the answer's FID group.
func (v *nackLogger) SetRtx(mediaSSRC, rtxSSRC uint32) {
	v.mu.Lock()
	defer v.mu.Unlock()
	v.rtxToMedia[rtxSSRC] = mediaSSRC
}

// Stats returns how many NACKs were sent and how many sequences were recovered, plain and RTX.
func (v *nackLogger) Stats() (nacksSent, recoveredPlain, recoveredRtx int) {
	v.mu.Lock()
	defer v.mu.Unlock()
	return v.nacksSent, v.recoveredPlain, v.recoveredRtx
}

// BindRTCPWriter logs every Generic NACK the chain above writes, then forwards the packets unchanged.
func (v *nackLogger) BindRTCPWriter(writer interceptor.RTCPWriter) interceptor.RTCPWriter {
	return interceptor.RTCPWriterFunc(func(pkts []rtcp.Packet, attributes interceptor.Attributes) (int, error) {
		for _, pkt := range pkts {
			nack, ok := pkt.(*rtcp.TransportLayerNack)
			if !ok {
				continue
			}
			v.onNackSent(nack)
		}
		return writer.Write(pkts, attributes)
	})
}

func (v *nackLogger) onNackSent(nack *rtcp.TransportLayerNack) {
	var seqs []uint16
	for _, pair := range nack.Nacks {
		seqs = append(seqs, pair.PacketList()...)
	}

	v.mu.Lock()
	set := v.requested[nack.MediaSSRC]
	if set == nil {
		set = make(map[uint16]bool)
		v.requested[nack.MediaSSRC] = set
	}
	for _, seq := range seqs {
		set[seq] = true
	}
	v.nacksSent++
	v.mu.Unlock()

	v.logf("NACK sent ssrc=%d, seqs=[%s]", nack.MediaSSRC, joinSeqs(seqs))
}

// BindRemoteStream watches the packets of one remote stream as they come off the wire, the media stream or the RTX
// repair stream, and logs the ones that recover a requested sequence. The packet is handed back unchanged.
func (v *nackLogger) BindRemoteStream(info *interceptor.StreamInfo, reader interceptor.RTPReader) interceptor.RTPReader {
	return interceptor.RTPReaderFunc(func(b []byte, attributes interceptor.Attributes) (int, interceptor.Attributes, error) {
		n, attrs, err := reader.Read(b, attributes)
		if err != nil {
			return n, attrs, err
		}
		v.onPacket(b[:n])
		return n, attrs, nil
	})
}

func (v *nackLogger) onPacket(raw []byte) {
	var pkt rtp.Packet
	if err := pkt.Unmarshal(raw); err != nil {
		return
	}

	v.mu.Lock()
	defer v.mu.Unlock()

	if media, isRtx := v.rtxToMedia[pkt.SSRC]; isRtx {
		// An RTX packet too short for an OSN is a padding probe, or malformed; pion drops it, so no line.
		if len(pkt.Payload) < 2 {
			return
		}
		osn := binary.BigEndian.Uint16(pkt.Payload[:2])
		if !v.takeRequested(media, osn) {
			return
		}
		v.recoveredRtx++
		v.logf("Recovered RTX seq=%d, ssrc=%d, pt=%d, osn=%d", pkt.SequenceNumber, pkt.SSRC, pkt.PayloadType, osn)
		return
	}

	if !v.takeRequested(pkt.SSRC, pkt.SequenceNumber) {
		return
	}
	v.recoveredPlain++
	v.logf("Recovered plain seq=%d, ssrc=%d, pt=%d", pkt.SequenceNumber, pkt.SSRC, pkt.PayloadType)
}

// takeRequested reports whether seq was requested for the media SSRC and forgets it, so a second copy is not a
// recovery. The caller holds the lock.
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

// rtxOfAnswer returns the media and RTX SSRC of the answer's first a=ssrc-group:FID line, media first as every
// WebRTC stack writes it, or ok false when the answer has no FID group and the session uses plain retransmission.
func rtxOfAnswer(sdp string) (mediaSSRC, rtxSSRC uint32, ok bool) {
	for _, line := range strings.Split(sdp, "\n") {
		line = strings.TrimSpace(line)
		if !strings.HasPrefix(line, "a=ssrc-group:FID ") {
			continue
		}
		fields := strings.Fields(strings.TrimPrefix(line, "a=ssrc-group:FID "))
		if len(fields) < 2 {
			continue
		}
		media, err1 := strconv.ParseUint(fields[0], 10, 32)
		rtx, err2 := strconv.ParseUint(fields[1], 10, 32)
		if err1 != nil || err2 != nil {
			continue
		}
		return uint32(media), uint32(rtx), true
	}
	return 0, 0, false
}
