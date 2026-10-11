// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package gb

import (
	"bytes"
	"context"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
	"time"

	"github.com/pion/rtp"

	"srs-integ/internal/media"
	"srs-integ/internal/rtmp"
)

// readFile reads the frames of a test media file.
func readFile(t *testing.T, name string) ([]Frame, string) {
	t.Helper()
	f, err := os.Open(media.Path(t, name))
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	frames, vcodec, err := ReadFLV(f)
	if err != nil {
		t.Fatal(err)
	}
	return frames, vcodec
}

// nalTypes is the NAL unit types of an Annex B frame.
func nalTypes(vcodec string, b []byte) []int {
	var types []int
	for i := 0; i+3 < len(b); i++ {
		if b[i] != 0 || b[i+1] != 0 || b[i+2] != 1 {
			continue
		}
		h := b[i+3]
		if vcodec == "hevc" {
			types = append(types, int(h>>1&0x3f))
		} else {
			types = append(types, int(h&0x1f))
		}
		i += 3
	}
	return types
}

func TestReadFLV(t *testing.T) {
	for _, tc := range []struct {
		file, vcodec string
		keyNALs      []int // the first NAL types of a keyframe
	}{
		{media.H264BaselineAAC, "h264", []int{7, 8}},
		{media.HEVCAAC, "hevc", []int{32, 33, 34}},
	} {
		t.Run(tc.vcodec, func(t *testing.T) {
			frames, vcodec := readFile(t, tc.file)
			if vcodec != tc.vcodec {
				t.Fatalf("codec %q, want %q", vcodec, tc.vcodec)
			}
			var videos, audios, keys int
			var lastDTS time.Duration
			for i, f := range frames {
				if f.DTS < lastDTS-50*time.Millisecond {
					t.Fatalf("frame %v at %v after %v", i, f.DTS, lastDTS)
				}
				lastDTS = f.DTS
				if !f.Video {
					audios++
					// The ADTS header: the sync word, AAC LC, 44.1 kHz, stereo,
					// and the frame length.
					if len(f.Data) < 8 || f.Data[0] != 0xff || f.Data[1]&0xf0 != 0xf0 {
						t.Fatalf("audio frame %v: no ADTS header: % x", i, f.Data[:min(8, len(f.Data))])
					}
					if p, freq, ch := f.Data[2]>>6, f.Data[2]>>2&0x0f, (f.Data[2]&1)<<2|f.Data[3]>>6; p != 1 || freq != 4 || ch != 2 {
						t.Fatalf("audio frame %v: profile %v, frequency index %v, channels %v", i, p, freq, ch)
					}
					if n := int(f.Data[3]&3)<<11 | int(f.Data[4])<<3 | int(f.Data[5]>>5); n != len(f.Data) {
						t.Fatalf("audio frame %v: ADTS length %v, frame %v", i, n, len(f.Data))
					}
					continue
				}
				videos++
				if f.PTS < f.DTS {
					t.Fatalf("video frame %v: PTS %v before DTS %v", i, f.PTS, f.DTS)
				}
				types := nalTypes(vcodec, f.Data)
				if len(types) == 0 || !bytes.HasPrefix(f.Data, []byte{0, 0, 0, 1}) {
					t.Fatalf("video frame %v is not Annex B: % x", i, f.Data[:min(8, len(f.Data))])
				}
				if f.Key {
					keys++
					if len(types) <= len(tc.keyNALs) || fmt.Sprint(types[:len(tc.keyNALs)]) != fmt.Sprint(tc.keyNALs) {
						t.Fatalf("keyframe %v: NALs %v, want %v first", i, types, tc.keyNALs)
					}
				}
			}
			// 30s at 25 fps with a keyframe a second, and AAC at 44.1 kHz.
			if videos != 750 || keys != 30 || audios < 1290 || audios > 1294 {
				t.Fatalf("%v video frames, %v keyframes, %v audio frames", videos, keys, audios)
			}
			if f := firstVideo(frames); !f.Key {
				t.Fatalf("the first video frame is not a keyframe")
			}
		})
	}
}

// psUnit is one unit of a PS stream: a pack header, a system header, a stream
// map, or a PES packet with its times and payload.
type psUnit struct {
	code     byte
	pts, dts int64 // 90 kHz, -1 when absent
	payload  []byte
}

// parsePS splits a PS stream into its units.
func parsePS(t *testing.T, b []byte) []psUnit {
	t.Helper()
	var out []psUnit
	for len(b) > 0 {
		if len(b) < 4 || b[0] != 0 || b[1] != 0 || b[2] != 1 {
			t.Fatalf("no start code at % x", b[:min(8, len(b))])
		}
		code := b[3]
		if code == 0xba {
			if len(b) < 14 || b[4]>>6 != 1 {
				t.Fatalf("bad pack header % x", b[:min(14, len(b))])
			}
			n := 14 + int(b[13]&7)
			out = append(out, psUnit{code: code, pts: -1, dts: -1, payload: b[4:n]})
			b = b[n:]
			continue
		}
		n := 6 + int(binary.BigEndian.Uint16(b[4:]))
		if n > len(b) {
			t.Fatalf("unit %#x of %v bytes, %v left", code, n, len(b))
		}
		u := psUnit{code: code, pts: -1, dts: -1, payload: b[6:n]}
		if code == VideoStreamID || code == AudioStreamID {
			h := b[6:n]
			if h[0]>>6 != 2 {
				t.Fatalf("PES %#x without its optional header: % x", code, h[:3])
			}
			flags, hl := h[1]>>6, int(h[2])
			if flags&2 != 0 {
				u.pts = psTime(h[3:])
			}
			if flags == 3 {
				u.dts = psTime(h[8:])
			}
			u.payload = h[3+hl:]
		}
		out = append(out, u)
		b = b[n:]
	}
	return out
}

// psTime reads a 33-bit PES time.
func psTime(b []byte) int64 {
	return int64(b[0]>>1&7)<<30 | int64(b[1])<<22 | int64(b[2]>>1)<<15 | int64(b[3])<<7 | int64(b[4]>>1)
}

func TestMuxerKeyframe(t *testing.T) {
	for _, tc := range []struct {
		vcodec string
		typ    byte
	}{{"h264", StreamTypeH264}, {"hevc", StreamTypeHEVC}} {
		t.Run(tc.vcodec, func(t *testing.T) {
			m, err := NewMuxer(tc.vcodec)
			if err != nil {
				t.Fatal(err)
			}
			data := append([]byte{0, 0, 0, 1}, bytes.Repeat([]byte{0x65, 1, 2, 3}, 100)...)
			units := parsePS(t, m.Pack(Frame{Video: true, Key: true, PTS: 1080 * time.Millisecond, DTS: time.Second, Data: data}))
			var codes []byte
			for _, u := range units {
				codes = append(codes, u.code)
			}
			if !bytes.Equal(codes, []byte{0xba, 0xbb, 0xbc, VideoStreamID}) {
				t.Fatalf("units % x, want ba bb bc e0", codes)
			}
			// The stream map: current, then its two streams, each with no
			// descriptors, then the CRC.
			psm := units[2].payload
			if psm[0]&0x80 == 0 || psm[1]&1 != 1 {
				t.Fatalf("stream map flags % x", psm[:2])
			}
			info := int(binary.BigEndian.Uint16(psm[2:]))
			es := psm[4+info:]
			n := int(binary.BigEndian.Uint16(es))
			if want := []byte{tc.typ, VideoStreamID, 0, 0, StreamTypeAAC, AudioStreamID, 0, 0}; !bytes.Equal(es[2:2+n], want) {
				t.Fatalf("streams % x, want % x", es[2:2+n], want)
			}
			if len(es) != 2+n+4 {
				t.Fatalf("stream map ends with %v bytes, want the CRC", len(es)-2-n)
			}
			pes := units[3]
			if pes.pts != 1080*90 || pes.dts != 1000*90 || !bytes.Equal(pes.payload, data) {
				t.Fatalf("PES pts %v dts %v, %v bytes", pes.pts, pes.dts, len(pes.payload))
			}
		})
	}
}

func TestMuxerFrames(t *testing.T) {
	m, err := NewMuxer("h264")
	if err != nil {
		t.Fatal(err)
	}

	// A frame with no reordering has only its PTS, and no stream map.
	data := []byte{0, 0, 0, 1, 0x41, 9, 9, 9}
	units := parsePS(t, m.Pack(Frame{Video: true, PTS: 40 * time.Millisecond, DTS: 40 * time.Millisecond, Data: data}))
	if len(units) != 2 || units[1].code != VideoStreamID || units[1].pts != 40*90 || units[1].dts != -1 || !bytes.Equal(units[1].payload, data) {
		t.Fatalf("units %+v", units)
	}

	// Audio.
	adts := []byte{0xff, 0xf1, 0x50, 0x80, 0x01, 0x3f, 0xfc, 1, 2}
	units = parsePS(t, m.Pack(Frame{PTS: 23 * time.Millisecond, DTS: 23 * time.Millisecond, Data: adts}))
	if len(units) != 2 || units[1].code != AudioStreamID || units[1].pts != 23*90 || !bytes.Equal(units[1].payload, adts) {
		t.Fatalf("units %+v", units)
	}

	// A frame larger than a PES packet takes several, the first with the
	// times.
	big := bytes.Repeat([]byte{7}, 150000)
	units = parsePS(t, m.Pack(Frame{Video: true, Key: true, Data: big}))
	var got []byte
	var pes int
	for _, u := range units {
		if u.code != VideoStreamID {
			continue
		}
		if pes > 0 && u.pts != -1 {
			t.Fatalf("PES %v has a PTS", pes)
		}
		pes++
		got = append(got, u.payload...)
	}
	if pes != 3 || !bytes.Equal(got, big) {
		t.Fatalf("%v PES packets with %v bytes, want 3 with %v", pes, len(got), len(big))
	}
}

func TestMuxerCodec(t *testing.T) {
	if _, err := NewMuxer("vp9"); err == nil {
		t.Fatal("vp9 accepted")
	}
}

// rtpPackets reads the RFC 4571 frames of a stream as RTP packets.
func rtpPackets(t *testing.T, b []byte) []*rtp.Packet {
	t.Helper()
	var out []*rtp.Packet
	for len(b) > 0 {
		n := int(binary.BigEndian.Uint16(b))
		if n == 0 || 2+n > len(b) {
			t.Fatalf("frame of %v bytes, %v left", n, len(b)-2)
		}
		var p rtp.Packet
		if err := p.Unmarshal(b[2 : 2+n]); err != nil {
			t.Fatal(err)
		}
		out = append(out, &p)
		b = b[2+n:]
	}
	return out
}

func TestPublisherRTP(t *testing.T) {
	var buf bytes.Buffer
	p, err := NewPublisher(&buf, 1234567, "h264")
	if err != nil {
		t.Fatal(err)
	}
	m, _ := NewMuxer("h264")
	frames := []Frame{
		{Video: true, Key: true, PTS: 80 * time.Millisecond, DTS: 40 * time.Millisecond, Data: append([]byte{0, 0, 0, 1, 0x65}, bytes.Repeat([]byte{3}, 5000)...)},
		{PTS: 50 * time.Millisecond, DTS: 50 * time.Millisecond, Data: []byte{0xff, 0xf1, 0x50, 0x80, 0x01, 0x3f, 0xfc, 1, 2}},
	}
	for _, f := range frames {
		if err := p.WriteFrame(f); err != nil {
			t.Fatal(err)
		}
	}

	pkts := rtpPackets(t, buf.Bytes())
	if len(pkts) != 5 {
		t.Fatalf("%v packets, want 4 of the video frame and 1 of the audio", len(pkts))
	}
	var payload []byte
	for i, pkt := range pkts {
		h := pkt.Header
		if h.Version != 2 || h.PayloadType != PayloadType || h.SSRC != 1234567 || h.SequenceNumber != uint16(i) {
			t.Fatalf("packet %v: %+v", i, h)
		}
		if len(pkt.Payload) > MaxPayload {
			t.Fatalf("packet %v: %v bytes of payload", i, len(pkt.Payload))
		}
		// Each frame's packets have its PTS, and the last has the marker.
		ts, marker := uint32(80*90), i == 3
		if i == 4 {
			ts, marker = 50*90, true
		}
		if h.Timestamp != ts || h.Marker != marker {
			t.Fatalf("packet %v: timestamp %v, marker %v", i, h.Timestamp, h.Marker)
		}
		if i < 4 {
			payload = append(payload, pkt.Payload...)
		}
	}
	if want := m.Pack(frames[0]); !bytes.Equal(payload, want) {
		t.Fatalf("the video packets carry %v bytes, want its pack of %v", len(payload), len(want))
	}

	// Raw packets carry on the sequence numbers.
	buf.Reset()
	if err := p.WriteRTP([]byte{1, 2, 3}, time.Second, false); err != nil {
		t.Fatal(err)
	}
	if err := p.WriteFramed([]byte{9, 9}); err != nil {
		t.Fatal(err)
	}
	b := buf.Bytes()
	pkts = rtpPackets(t, b[:len(b)-4])
	if len(pkts) != 1 || pkts[0].SequenceNumber != 5 || pkts[0].Timestamp != 90000 || !bytes.Equal(pkts[0].Payload, []byte{1, 2, 3}) {
		t.Fatalf("raw packet %+v", pkts)
	}
	if !bytes.Equal(b[len(b)-4:], []byte{0, 2, 9, 9}) {
		t.Fatalf("framed bytes % x", b[len(b)-4:])
	}
}

func TestPublisherCodec(t *testing.T) {
	if _, err := NewPublisher(io.Discard, 1, "av1"); err == nil {
		t.Fatal("av1 accepted")
	}
}

func TestSend(t *testing.T) {
	var buf bytes.Buffer
	p, err := NewPublisher(&buf, 1, "h264")
	if err != nil {
		t.Fatal(err)
	}
	frames := []Frame{
		{Video: true, Key: true, Data: []byte{0, 0, 0, 1, 0x65}},
		{DTS: 1000 * time.Millisecond, PTS: 1000 * time.Millisecond, Data: []byte{0xff, 0xf1, 0x50, 0x80, 0x01, 0x3f, 0xfc}},
		{Video: true, DTS: 2000 * time.Millisecond, PTS: 2000 * time.Millisecond, Data: []byte{0, 0, 0, 1, 0x41}},
	}
	now := time.Unix(0, 0)
	var slept []time.Duration
	pacer := &rtmp.Pacer{
		Now: func() time.Time { return now },
		Sleep: func(ctx context.Context, d time.Duration) error {
			slept = append(slept, d)
			now = now.Add(d)
			return nil
		},
	}
	if err := p.Send(context.Background(), frames, pacer); err != nil {
		t.Fatal(err)
	}
	if fmt.Sprint(slept) != "[1s 1s]" {
		t.Fatalf("slept %v, want 1s before each later frame", slept)
	}
	if n := len(rtpPackets(t, buf.Bytes())); n != 3 {
		t.Fatalf("%v packets", n)
	}

	// Without a pacer, at once; a cancelled context stops it.
	buf.Reset()
	if err := p.Send(context.Background(), frames, nil); err != nil || len(rtpPackets(t, buf.Bytes())) != 3 {
		t.Fatalf("at once: %v", err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if err := p.Send(ctx, frames, nil); err == nil {
		t.Fatal("sent with a cancelled context")
	}
}

func TestPublishAPI(t *testing.T) {
	var got struct {
		method, path string
		body         map[string]any
	}
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		got.method, got.path, got.body = r.Method, r.URL.Path, nil
		json.NewDecoder(r.Body).Decode(&got.body)
		if got.body["id"] == "busy" {
			fmt.Fprint(w, `{"code":1028,"desc":"StreamBusy"}`)
			return
		}
		fmt.Fprint(w, `{"code":0,"port":9000,"is_tcp":true}`)
	}))
	defer srv.Close()
	ctx := context.Background()

	a, err := Publish(ctx, srv.Client(), srv.URL, "cam1", 100001)
	if err != nil {
		t.Fatal(err)
	}
	if got.method != "POST" || got.path != "/gb/v1/publish/" || got.body["id"] != "cam1" || got.body["ssrc"] != "0000100001" {
		t.Fatalf("request %+v", got)
	}
	if *a != (Answer{Port: 9000, IsTCP: true}) {
		t.Fatalf("answer %+v", a)
	}

	a, err = Publish(ctx, srv.Client(), srv.URL, "busy", 1)
	if err != nil || a.Code != 1028 || a.Desc != "StreamBusy" {
		t.Fatalf("busy answer %+v, %v", a, err)
	}

	if _, err = Post(ctx, srv.Client(), srv.URL, []byte(`{"id":"x"}`)); err != nil || got.body["id"] != "x" || len(got.body) != 1 {
		t.Fatalf("post %+v, %v", got.body, err)
	}
}

// TestFFmpegReadsPS checks the packs against ffmpeg's PS demuxer: 2s of each
// test file, muxed and copied to FLV, have their codecs, keyframes and
// frames.
func TestFFmpegReadsPS(t *testing.T) {
	ff, err := media.FindFFmpeg()
	if err != nil {
		t.Skipf("no ffmpeg: %v", err)
	}
	for _, tc := range []struct{ file, vcodec string }{{media.H264BaselineAAC, "h264"}, {media.HEVCAAC, "hevc"}} {
		t.Run(tc.vcodec, func(t *testing.T) {
			frames, vcodec := readFile(t, tc.file)
			m, err := NewMuxer(vcodec)
			if err != nil {
				t.Fatal(err)
			}
			// A device starts with a keyframe, so the stream map comes first.
			var ps []byte
			for _, f := range fromKeyframe(frames) {
				if f.DTS >= 2*time.Second {
					break
				}
				ps = append(ps, m.Pack(f)...)
			}
			dir := t.TempDir()
			in, out := filepath.Join(dir, "in.ps"), filepath.Join(dir, "out.flv")
			if err := os.WriteFile(in, ps, 0o644); err != nil {
				t.Fatal(err)
			}
			cmd := exec.Command(ff.Path, "-hide_banner", "-loglevel", "error", "-nostdin", "-f", "mpeg", "-i", in, "-c", "copy", out)
			if b, err := cmd.CombinedOutput(); err != nil {
				t.Fatalf("ffmpeg: %v: %s", err, b)
			}
			info, err := media.ProbeFile(out)
			if err != nil {
				t.Fatal(err)
			}
			v, a := info.Video, info.Audio
			if v == nil || a == nil {
				t.Fatalf("video %+v, audio %+v", v, a)
			}
			if v.Codec != tc.vcodec || len(v.Keyframes) != 2 || v.Frames < 49 || v.Frames > 50 {
				t.Fatalf("video %v, %v keyframes, %v frames", v.Codec, len(v.Keyframes), v.Frames)
			}
			if a.Codec != "aac" || a.SampleRate != 44100 || a.Channels != 2 || a.Frames < 80 {
				t.Fatalf("audio %v %vHz %vch, %v frames", a.Codec, a.SampleRate, a.Channels, a.Frames)
			}
		})
	}
}

// firstVideo is the first video frame.
func firstVideo(frames []Frame) Frame {
	for _, f := range frames {
		if f.Video {
			return f
		}
	}
	return Frame{}
}

// fromKeyframe is the frames from the first keyframe on.
func fromKeyframe(frames []Frame) []Frame {
	for i, f := range frames {
		if f.Video && f.Key {
			return frames[i:]
		}
	}
	return nil
}
