// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package rtc

import (
	"context"
	"encoding/binary"
	"fmt"
	"slices"
	"strings"
	"sync"

	"github.com/pion/interceptor"
	"github.com/pion/rtcp"
	"github.com/pion/rtp"
	"github.com/pion/sdp/v3"
)

// A Recorder is the interceptor under all of pion's in a session: it
// records the RTP and RTCP packets as the network carries them, so a case
// sees what pion hides, such as a lost packet that a retransmission filled,
// the NACKs pion's generator sends, or the RTX stream pion unwraps.
type Recorder struct {
	interceptor.NoOp

	mu      sync.Mutex
	rec     Record
	changed chan struct{}
}

// A Record is what a Recorder has seen so far.
type Record struct {
	// In and Out are the RTP streams the session received and sent, by
	// SSRC.
	In, Out map[uint32]*Stream
	// RTCPIn and RTCPOut are the RTCP packets it received and sent.
	RTCPIn, RTCPOut []rtcp.Packet
	// TWCC are the transport-wide sequence numbers of the RTP packets it
	// sent, in order.
	TWCC []uint16
}

// A Stream is the log of one RTP stream.
type Stream struct {
	SSRC uint32
	// MimeType is its codec as negotiated, such as video/H264, or video/rtx
	// for the RTX stream of a video track.
	MimeType string
	Packets  int
	// FirstTimestamp is the RTP time of the first packet, which maps the
	// stream to the times of the Sender Reports.
	FirstTimestamp uint32
	// Missed are the sequence numbers that a later packet skipped, in
	// order: the packets that didn't come in order.
	Missed []uint16
	// Late are the missed ones that came afterwards, in the order they came.
	Late []uint16
	// Repeated are the sequence numbers that came again: a duplicate, or a
	// packet the session sent again.
	Repeated []uint16
	// OSNs are the original sequence numbers an RTX stream's packets carry,
	// in order.
	OSNs []uint16

	started bool
	// highest is the highest sequence number so far, past the 16-bit wrap.
	highest int64
	missing map[int64]bool
}

// maxSkip is the largest jump of sequence numbers whose skipped ones are
// logged as missed; a larger one is a new start, not a loss.
const maxSkip = 1000

// add logs a packet's sequence number and, for the first, its RTP time, and
// for an RTX stream the original one at the start of its payload.
func (s *Stream) add(seq uint16, ts uint32, payload []byte) {
	s.Packets++
	if strings.HasSuffix(strings.ToLower(s.MimeType), "/rtx") && len(payload) >= 2 {
		s.OSNs = append(s.OSNs, binary.BigEndian.Uint16(payload))
	}
	if !s.started {
		s.FirstTimestamp = ts
		s.started, s.highest, s.missing = true, int64(seq), map[int64]bool{}
		return
	}
	n := s.highest + int64(int16(seq-uint16(s.highest)))
	switch {
	case n > s.highest:
		if n-s.highest <= maxSkip {
			for m := s.highest + 1; m < n; m++ {
				s.missing[m] = true
				s.Missed = append(s.Missed, uint16(m))
			}
		}
		s.highest = n
	case s.missing[n]:
		delete(s.missing, n)
		s.Late = append(s.Late, seq)
	default:
		s.Repeated = append(s.Repeated, seq)
	}
}

// copy is a copy of the log, without its state.
func (s *Stream) copy() *Stream {
	return &Stream{SSRC: s.SSRC, MimeType: s.MimeType, Packets: s.Packets, FirstTimestamp: s.FirstTimestamp,
		Missed: slices.Clone(s.Missed), Late: slices.Clone(s.Late), Repeated: slices.Clone(s.Repeated), OSNs: slices.Clone(s.OSNs)}
}

// NewRecorder makes a recorder.
func NewRecorder() *Recorder {
	return &Recorder{rec: Record{In: map[uint32]*Stream{}, Out: map[uint32]*Stream{}}, changed: make(chan struct{})}
}

// NewInterceptor returns the recorder: a session has one.
func (r *Recorder) NewInterceptor(string) (interceptor.Interceptor, error) {
	return r, nil
}

// update changes the record with the recorder locked, and wakes the waiters.
func (r *Recorder) update(f func(rec *Record)) {
	r.mu.Lock()
	defer r.mu.Unlock()
	f(&r.rec)
	close(r.changed)
	r.changed = make(chan struct{})
}

// stream returns the log of a stream, made with its MIME type.
func stream(streams map[uint32]*Stream, ssrc uint32, mime string) *Stream {
	s := streams[ssrc]
	if s == nil {
		s = &Stream{SSRC: ssrc, MimeType: mime}
		streams[ssrc] = s
	}
	return s
}

// BindRemoteStream logs the RTP packets of a received stream.
func (r *Recorder) BindRemoteStream(info *interceptor.StreamInfo, reader interceptor.RTPReader) interceptor.RTPReader {
	return interceptor.RTPReaderFunc(func(b []byte, a interceptor.Attributes) (int, interceptor.Attributes, error) {
		n, a, err := reader.Read(b, a)
		if err != nil {
			return n, a, err
		}
		var h rtp.Header
		off, err := h.Unmarshal(b[:n])
		if err != nil {
			return n, a, nil
		}
		r.update(func(rec *Record) {
			stream(rec.In, h.SSRC, info.MimeType).add(h.SequenceNumber, h.Timestamp, b[off:n])
		})
		return n, a, nil
	})
}

// BindLocalStream logs the RTP packets of a sent stream, and of its RTX
// stream, which the NACK responder sends on the same writer, and their
// transport-wide sequence numbers.
func (r *Recorder) BindLocalStream(info *interceptor.StreamInfo, writer interceptor.RTPWriter) interceptor.RTPWriter {
	var twcc uint8
	for _, e := range info.RTPHeaderExtensions {
		if e.URI == sdp.TransportCCURI {
			twcc = uint8(e.ID)
		}
	}
	kind, _, _ := strings.Cut(info.MimeType, "/")
	return interceptor.RTPWriterFunc(func(h *rtp.Header, payload []byte, a interceptor.Attributes) (int, error) {
		mime := info.MimeType
		if h.SSRC != info.SSRC && h.SSRC == info.SSRCRetransmission {
			mime = kind + "/rtx"
		}
		r.update(func(rec *Record) {
			stream(rec.Out, h.SSRC, mime).add(h.SequenceNumber, h.Timestamp, payload)
			if twcc == 0 {
				return
			}
			if b := h.GetExtension(twcc); b != nil {
				var e rtp.TransportCCExtension
				if e.Unmarshal(b) == nil {
					rec.TWCC = append(rec.TWCC, e.TransportSequence)
				}
			}
		})
		return writer.Write(h, payload, a)
	})
}

// BindRTCPReader logs the RTCP packets received.
func (r *Recorder) BindRTCPReader(reader interceptor.RTCPReader) interceptor.RTCPReader {
	return interceptor.RTCPReaderFunc(func(b []byte, a interceptor.Attributes) (int, interceptor.Attributes, error) {
		n, a, err := reader.Read(b, a)
		if err != nil {
			return n, a, err
		}
		if pkts, err := rtcp.Unmarshal(b[:n]); err == nil {
			r.update(func(rec *Record) { rec.RTCPIn = append(rec.RTCPIn, pkts...) })
		}
		return n, a, nil
	})
}

// BindRTCPWriter logs the RTCP packets sent.
func (r *Recorder) BindRTCPWriter(writer interceptor.RTCPWriter) interceptor.RTCPWriter {
	return interceptor.RTCPWriterFunc(func(pkts []rtcp.Packet, a interceptor.Attributes) (int, error) {
		r.update(func(rec *Record) { rec.RTCPOut = append(rec.RTCPOut, pkts...) })
		return writer.Write(pkts, a)
	})
}

// Record returns a copy of the record so far.
func (r *Recorder) Record() *Record {
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.rec.copy()
}

func (rec *Record) copy() *Record {
	c := &Record{In: map[uint32]*Stream{}, Out: map[uint32]*Stream{},
		RTCPIn: slices.Clone(rec.RTCPIn), RTCPOut: slices.Clone(rec.RTCPOut), TWCC: slices.Clone(rec.TWCC)}
	for k, s := range rec.In {
		c.In[k] = s.copy()
	}
	for k, s := range rec.Out {
		c.Out[k] = s.copy()
	}
	return c
}

// Until waits until done is true of the record, and returns a copy of it,
// with an error when ctx ends first. done runs with the recorder locked.
func (r *Recorder) Until(ctx context.Context, done func(*Record) bool) (*Record, error) {
	for {
		r.mu.Lock()
		ok, changed := done(&r.rec), r.changed
		rec := r.rec.copy()
		r.mu.Unlock()
		if ok {
			return rec, nil
		}
		select {
		case <-changed:
		case <-ctx.Done():
			return rec, fmt.Errorf("%w: the condition is not met", ctx.Err())
		}
	}
}

// find is the stream of a MIME type, in any case; of several, the one of
// the most packets.
func find(streams map[uint32]*Stream, mime string) *Stream {
	var out *Stream
	for _, s := range streams {
		if strings.EqualFold(s.MimeType, mime) && (out == nil || s.Packets > out.Packets) {
			out = s
		}
	}
	return out
}

// Received is the stream the session received of a MIME type, such as
// video/H264 or video/rtx, in any case, or nil.
func (rec *Record) Received(mime string) *Stream {
	return find(rec.In, mime)
}

// Sent is the stream the session sent of a MIME type, or nil.
func (rec *Record) Sent(mime string) *Stream {
	return find(rec.Out, mime)
}

// Nacked are the sequence numbers of the stream ssrc that the NACKs among
// pkts ask for, sorted, each once.
func Nacked(pkts []rtcp.Packet, ssrc uint32) []uint16 {
	seen := map[uint16]bool{}
	var out []uint16
	for _, p := range pkts {
		n, ok := p.(*rtcp.TransportLayerNack)
		if !ok || n.MediaSSRC != ssrc {
			continue
		}
		for _, pair := range n.Nacks {
			for _, seq := range pair.PacketList() {
				if !seen[seq] {
					seen[seq] = true
					out = append(out, seq)
				}
			}
		}
	}
	slices.Sort(out)
	return out
}

// TWCCFeedback are the transport-wide congestion control feedback packets
// among pkts.
func TWCCFeedback(pkts []rtcp.Packet) []*rtcp.TransportLayerCC {
	var out []*rtcp.TransportLayerCC
	for _, p := range pkts {
		if cc, ok := p.(*rtcp.TransportLayerCC); ok {
			out = append(out, cc)
		}
	}
	return out
}
