// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"bytes"
	"fmt"
	"strings"
	"testing"

	"github.com/pion/interceptor"
	"github.com/pion/rtcp"
	"github.com/pion/rtp"
)

// playHarness runs a nackLogger as pion's chain would on the player: RTCP the NACK generator writes goes through
// its RTCP writer, and packets off the wire go through the RTP reader it binds for each remote stream. It captures
// the log lines and the RTCP it forwards.
type playHarness struct {
	log       bytes.Buffer
	logger    *nackLogger
	rtcp      interceptor.RTCPWriter
	forwarded []rtcp.Packet
}

func newPlayHarness() *playHarness {
	h := &playHarness{}
	h.logger = newNackLogger(func(format string, args ...interface{}) {
		fmt.Fprintf(&h.log, format+"\n", args...)
	})
	h.rtcp = h.logger.BindRTCPWriter(interceptor.RTCPWriterFunc(
		func(pkts []rtcp.Packet, _ interceptor.Attributes) (int, error) {
			h.forwarded = append(h.forwarded, pkts...)
			return len(pkts), nil
		}))
	return h
}

// send writes RTCP as pion's NACK generator would.
func (h *playHarness) send(t *testing.T, pkt rtcp.Packet) {
	t.Helper()
	if _, err := h.rtcp.Write([]rtcp.Packet{pkt}, nil); err != nil {
		t.Fatalf("write rtcp: %v", err)
	}
}

// receive passes one RTP packet, as it came off the wire on the stream ssrc, through the reader the logger binds
// for that stream, and returns the bytes the reader handed back.
func (h *playHarness) receive(t *testing.T, ssrc uint32, pt uint8, seq uint16, payload []byte) []byte {
	t.Helper()
	pkt := &rtp.Packet{
		Header:  rtp.Header{Version: 2, SSRC: ssrc, PayloadType: pt, SequenceNumber: seq, Timestamp: 90000},
		Payload: payload,
	}
	raw, err := pkt.Marshal()
	if err != nil {
		t.Fatalf("marshal rtp: %v", err)
	}
	reader := h.logger.BindRemoteStream(&interceptor.StreamInfo{SSRC: ssrc, PayloadType: pt}, interceptor.RTPReaderFunc(
		func(b []byte, a interceptor.Attributes) (int, interceptor.Attributes, error) {
			return copy(b, raw), a, nil
		}))
	buf := make([]byte, 1500)
	n, _, err := reader.Read(buf, nil)
	if err != nil {
		t.Fatalf("read rtp ssrc %d seq %d: %v", ssrc, seq, err)
	}
	if !bytes.Equal(buf[:n], raw) {
		t.Errorf("packet ssrc %d seq %d changed: %x, want %x", ssrc, seq, buf[:n], raw)
	}
	return buf[:n]
}

func nackFor(ssrc uint32, seqs ...uint16) *rtcp.TransportLayerNack {
	return &rtcp.TransportLayerNack{SenderSSRC: 1, MediaSSRC: ssrc, Nacks: rtcp.NackPairsFromSequenceNumbers(seqs)}
}

// Every NACK the generator writes is logged with the media SSRC and the sequences it asks for, in the compact form
// of the SRS detail logs, and forwarded unchanged; other RTCP is forwarded without a line.
func TestNackLoggerLogsNackSent(t *testing.T) {
	h := newPlayHarness()
	h.send(t, nackFor(1000, 10, 11, 12))
	h.send(t, &rtcp.ReceiverReport{SSRC: 1})

	if !strings.Contains(h.log.String(), "NACK sent ssrc=1000, seqs=[10,11,12]\n") {
		t.Errorf("log lacks the NACK sent line: %q", h.log.String())
	}
	if n := strings.Count(h.log.String(), "NACK sent"); n != 1 {
		t.Errorf("%d NACK sent lines, want 1 (the receiver report is not one): %q", n, h.log.String())
	}
	if len(h.forwarded) != 2 {
		t.Errorf("forwarded %d RTCP packets, want 2", len(h.forwarded))
	}
}

// The answer negotiated no RTX, so SRS resends the original packet on the media SSRC: a packet whose sequence a
// NACK asked for is a plain recovery, logged once when it arrives. A sequence never asked for and a second copy of
// the recovered one are not recoveries.
func TestNackLoggerLogsPlainRecovery(t *testing.T) {
	h := newPlayHarness()
	h.receive(t, 1000, 106, 10, []byte{0x65, 0x88})
	h.send(t, nackFor(1000, 11))
	h.receive(t, 1000, 106, 50, []byte{0x65, 0x88})
	h.receive(t, 1000, 106, 11, []byte{0x65, 0x88})
	h.receive(t, 1000, 106, 11, []byte{0x65, 0x88})

	if !strings.Contains(h.log.String(), "Recovered plain seq=11, ssrc=1000, pt=106\n") {
		t.Errorf("log lacks the plain recovery line: %q", h.log.String())
	}
	if n := strings.Count(h.log.String(), "Recovered"); n != 1 {
		t.Errorf("%d Recovered lines, want 1: %q", n, h.log.String())
	}
	if strings.Contains(h.log.String(), "Recovered RTX") {
		t.Errorf("plain recovery logged as RTX: %q", h.log.String())
	}
}

// The answer negotiated RTX, so SRS answers on the RTX SSRC of the FID group with the RTX payload type and the
// original sequence as the two-byte OSN: an RTX packet whose OSN a NACK asked for is an RTX recovery, logged as
// sent, before pion unwraps it. An RTX packet whose OSN was never asked for, such as a padding probe, and one too
// short to carry an OSN are forwarded without a line and without a panic.
func TestNackLoggerLogsRtxRecoveryWithTheOsn(t *testing.T) {
	h := newPlayHarness()
	h.logger.SetRtx(1000, 1001)
	h.receive(t, 1000, 106, 19, []byte{0x65, 0x88})
	h.send(t, nackFor(1000, 20))
	h.receive(t, 1001, 107, 5, []byte{0x00, 0x05, 0x65})
	h.receive(t, 1001, 107, 6, []byte{0x00})
	h.receive(t, 1001, 107, 7, []byte{0x00, 0x14, 0x65, 0x88})

	if !strings.Contains(h.log.String(), "Recovered RTX seq=7, ssrc=1001, pt=107, osn=20\n") {
		t.Errorf("log lacks the RTX recovery line: %q", h.log.String())
	}
	if n := strings.Count(h.log.String(), "Recovered"); n != 1 {
		t.Errorf("%d Recovered lines, want 1: %q", n, h.log.String())
	}
	if strings.Contains(h.log.String(), "Recovered plain") {
		t.Errorf("RTX recovery logged as plain: %q", h.log.String())
	}
}

// The FID group of the answer names the media SSRC first and the RTX SSRC second; an answer without one means the
// session uses plain retransmission.
func TestRtxOfAnswer(t *testing.T) {
	answer := "v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 106 105\r\na=rtpmap:105 rtx/90000\r\na=fmtp:105 apt=106\r\n" +
		"a=ssrc-group:FID 200001 200002\r\na=ssrc:200001 cname:s\r\na=ssrc:200002 cname:s\r\n"
	if media, rtx, ok := rtxOfAnswer(answer); !ok || media != 200001 || rtx != 200002 {
		t.Errorf("FID: media=%d rtx=%d ok=%v, want 200001 200002 true", media, rtx, ok)
	}

	plain := "v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 106\r\na=ssrc:200001 cname:s\r\n"
	if media, rtx, ok := rtxOfAnswer(plain); ok || media != 0 || rtx != 0 {
		t.Errorf("no FID: media=%d rtx=%d ok=%v, want 0 0 false", media, rtx, ok)
	}
}
