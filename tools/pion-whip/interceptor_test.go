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

// loggerHarness binds one local stream on a nackLogger, captures its log lines
// and the packets it forwards, and feeds it RTCP as pion's chain would.
type loggerHarness struct {
	log     bytes.Buffer
	written []rtp.Header
	logger  *nackLogger
	writer  interceptor.RTPWriter
}

func newLoggerHarness(info *interceptor.StreamInfo) *loggerHarness {
	h := &loggerHarness{}
	h.logger = newNackLogger(func(format string, args ...interface{}) {
		fmt.Fprintf(&h.log, format+"\n", args...)
	})
	h.writer = h.logger.BindLocalStream(info, interceptor.RTPWriterFunc(
		func(header *rtp.Header, payload []byte, _ interceptor.Attributes) (int, error) {
			h.written = append(h.written, *header)
			return len(payload), nil
		}))
	return h
}

func (h *loggerHarness) write(t *testing.T, ssrc uint32, pt uint8, seq uint16, payload []byte) {
	t.Helper()
	header := &rtp.Header{Version: 2, SSRC: ssrc, PayloadType: pt, SequenceNumber: seq}
	if _, err := h.writer.Write(header, payload, nil); err != nil {
		t.Fatalf("write ssrc %d seq %d: %v", ssrc, seq, err)
	}
}

// feedRTCP passes one marshaled RTCP packet through the logger's RTCP reader
// and returns the bytes the reader handed back.
func (h *loggerHarness) feedRTCP(t *testing.T, pkt rtcp.Packet) []byte {
	t.Helper()
	raw, err := pkt.Marshal()
	if err != nil {
		t.Fatalf("marshal rtcp: %v", err)
	}
	reader := h.logger.BindRTCPReader(interceptor.RTCPReaderFunc(
		func(b []byte, a interceptor.Attributes) (int, interceptor.Attributes, error) {
			return copy(b, raw), a, nil
		}))
	buf := make([]byte, 1500)
	n, _, err := reader.Read(buf, nil)
	if err != nil {
		t.Fatalf("read rtcp: %v", err)
	}
	return buf[:n]
}

func nackFor(ssrc uint32, seqs ...uint16) *rtcp.TransportLayerNack {
	return &rtcp.TransportLayerNack{SenderSSRC: 1, MediaSSRC: ssrc, Nacks: rtcp.NackPairsFromSequenceNumbers(seqs)}
}

func TestNackLoggerLogsNackReceivedAndPlainResend(t *testing.T) {
	// The answer negotiated no RTX, so the stream has no RTX SSRC and pion's
	// responder resends the original packet on the media SSRC.
	h := newLoggerHarness(&interceptor.StreamInfo{SSRC: 1000, PayloadType: 106, MimeType: "video/H264"})
	for seq := uint16(10); seq <= 12; seq++ {
		h.write(t, 1000, 106, seq, []byte{0x65, 0x88})
	}
	if h.log.Len() != 0 {
		t.Fatalf("original packets were logged: %q", h.log.String())
	}

	raw := h.feedRTCP(t, nackFor(1000, 11, 12))
	if want, _ := nackFor(1000, 11, 12).Marshal(); !bytes.Equal(raw, want) {
		t.Errorf("RTCP was not forwarded unchanged: %x, want %x", raw, want)
	}
	if !strings.Contains(h.log.String(), "NACK received ssrc=1000, seqs=[11,12]\n") {
		t.Errorf("log lacks the NACK received line: %q", h.log.String())
	}

	h.write(t, 1000, 106, 11, []byte{0x65, 0x88})
	if !strings.Contains(h.log.String(), "Resend plain seq=11, ssrc=1000, pt=106\n") {
		t.Errorf("log lacks the plain resend line: %q", h.log.String())
	}
	if strings.Contains(h.log.String(), "Resend RTX") {
		t.Errorf("plain resend logged as RTX: %q", h.log.String())
	}
	if len(h.written) != 4 {
		t.Errorf("forwarded %d packets, want 4", len(h.written))
	}
}

func TestNackLoggerLogsRtxResendWithTheOsn(t *testing.T) {
	// The answer negotiated RTX, so pion's responder answers on the RTX SSRC with
	// the RTX payload type and the original sequence as the two-byte OSN.
	h := newLoggerHarness(&interceptor.StreamInfo{
		SSRC: 1000, PayloadType: 106, SSRCRetransmission: 1001, PayloadTypeRetransmission: 107, MimeType: "video/H264",
	})
	h.write(t, 1000, 106, 20, []byte{0x65, 0x88})
	h.feedRTCP(t, nackFor(1000, 20))
	h.write(t, 1001, 107, 5, []byte{0x00, 0x14, 0x65, 0x88})

	if !strings.Contains(h.log.String(), "NACK received ssrc=1000, seqs=[20]\n") {
		t.Errorf("log lacks the NACK received line: %q", h.log.String())
	}
	if !strings.Contains(h.log.String(), "Resend RTX seq=5, ssrc=1001, pt=107, osn=20\n") {
		t.Errorf("log lacks the RTX resend line: %q", h.log.String())
	}
	if strings.Contains(h.log.String(), "Resend plain") {
		t.Errorf("RTX resend logged as plain: %q", h.log.String())
	}
	if len(h.written) != 2 {
		t.Errorf("forwarded %d packets, want 2", len(h.written))
	}
}

// Nothing but a NACK and its resend is ever logged.
func TestNackLoggerIgnoresOtherRtcpAndUnrequestedDuplicates(t *testing.T) {
	h := newLoggerHarness(&interceptor.StreamInfo{SSRC: 1000, PayloadType: 106, MimeType: "video/H264"})
	h.write(t, 1000, 106, 10, []byte{0x65, 0x88})

	report := &rtcp.ReceiverReport{SSRC: 1}
	raw := h.feedRTCP(t, report)
	if want, _ := report.Marshal(); !bytes.Equal(raw, want) {
		t.Errorf("receiver report was not forwarded unchanged: %x, want %x", raw, want)
	}
	// A NACK for a stream this peer does not send is not this peer's event.
	h.feedRTCP(t, nackFor(2000, 10))
	// A duplicate write that answers no NACK is not a resend.
	h.write(t, 1000, 106, 10, []byte{0x65, 0x88})

	if h.log.Len() != 0 {
		t.Errorf("unexpected log lines: %q", h.log.String())
	}
	if len(h.written) != 2 {
		t.Errorf("forwarded %d packets, want 2", len(h.written))
	}
}
