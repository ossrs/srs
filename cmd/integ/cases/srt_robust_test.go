// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"bytes"
	"encoding/binary"
	"errors"
	"math/rand"
	"os"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"testing"
	"time"

	"github.com/datarhei/gosrt/packet"

	"srs-integ/internal/check"
	"srs-integ/internal/media"
	"srs-integ/internal/mock"
	"srs-integ/internal/server"
	"srs-integ/internal/srt"
)

// srtRobustWait is how long a raw caller waits for an answer libsrt sends at
// once on loopback, or to be sure none comes.
const srtRobustWait = 50 * time.Millisecond

// srtRobustLatency is the latency of a raw publisher and the player of its
// stream, short so a message reaches the player soon.
const srtRobustLatency = 20 * time.Millisecond

// srtRobustDefault is the stream of the robust server's default_streamid,
// which a caller that sends no stream ID plays.
const srtRobustDefault = "robust-default"

// TestSrt_Robust sends SRS's SRT port what no library sends, from raw UDP
// sockets that speak SRT by hand, and checks that SRS stays up and serves
// the next caller. SRS's code sees only the sockets libsrt accepts and the
// stream ID of each, so libsrt answers or drops everything before a
// connection, and handles every control packet of one:
//   - garbage, a truncated induction, data before the handshake, and a packet
//     over libsrt's 1500 bytes are dropped with no answer;
//   - a conclusion cut in its HSREQ, or whose HSREQ block has a wrong length
//     or whose stream ID block has none, is refused as rogue or unknown; one
//     cut in its stream ID block, or whose block is longer than the packet,
//     or with blocks of unknown types, connects with the block ignored, so
//     SRS takes default_streamid;
//   - a stream ID over 512 bytes is refused; one of 511 bytes publishes;
//   - on a publisher's own connection, a control packet of any type with no
//     body, a short one or a whole packet of random bytes leaves its stream
//     as it was, but a shutdown ends it, and new key material loses the
//     messages after it: what a peer may do to its own connection;
//   - an ACK or a NAK of a packet SRS never sent ends the connection, and a
//     drop request of a few packets passes;
//   - a flood of inductions from 200 sockets is answered statelessly, and the
//     next caller connects at once.
func TestSrt_Robust(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, srtImpairConfig(server.SRTOptions{
		PeerIdleTimeout: srtFailIdle, DefaultStreamID: srtID("live/"+srtRobustDefault, "request"),
	}))
	addr := srs.Addr(server.SRT)

	check.Case(t, "Garbage", check.Normal, func(c *check.C) {
		seed := time.Now().UnixNano()
		c.Cleanup(func() {
			if c.Failed() {
				c.Logf("seed %v", seed)
			}
		})
		rnd := rand.New(rand.NewSource(seed))
		for i := 0; i < 10; i++ {
			raw := dialRaw(c, addr)
			for j := 0; j < 50; j++ {
				b := make([]byte, 1+rnd.Intn(1500))
				rnd.Read(b)
				raw.Write(b)
			}
		}
		checkSRTAlive(c, srs)
	})

	check.Case(t, "InductionTruncated", check.Normal, func(c *check.C) {
		raw := dialRaw(c, addr)
		hs := raw.Induction()
		for n := 1; n < len(hs); n++ {
			raw.Write(hs[:n])
		}
		checkRawUnanswered(c, raw, "a truncated induction")
		_, err := raw.Induct(time.Second)
		c.CheckNoError(err, "the whole induction")
		checkSRTAlive(c, srs)
	})

	// libsrt reads a packet into a buffer of its MSS, 1500 bytes, and drops
	// a larger one.
	check.Case(t, "InductionOversized", check.Normal, func(c *check.C) {
		for _, size := range []int{1500, 9000} {
			raw := dialRaw(c, addr)
			hs := raw.Induction()
			raw.Write(append(hs, make([]byte, size-len(hs))...))
			_, err := raw.ReadHandshake(srtRobustWait)
			if size <= 1500 {
				c.CheckNoError(err, "an induction of 1500 bytes")
			} else {
				c.Check(srt.IsTimeout(err), "an induction of %v bytes: %v, want no answer", size, err)
			}
		}
		checkSRTAlive(c, srs)
	})

	// The conclusion is 64 bytes of handshake, the HSREQ block of 16 and the
	// stream ID block.
	check.Case(t, "ConclusionTruncated", check.Normal, func(c *check.C) {
		stream := streamName(c)
		for _, n := range []int{20, 63, 64, 66, 79, 80, 84, 90} {
			raw := dialRaw(c, addr)
			cut := func(hs []byte) []byte { return hs[:n] }
			what := "a conclusion cut at " + strconv.Itoa(n)
			switch {
			case n < 64:
				ans := sendRawConclusion(c, raw, stream, cut)
				c.Check(ans == nil, "%v answered with %v", what, handshakeOf(ans))
			case n < 80:
				checkRawRefused(c, sendRawConclusion(c, raw, stream, cut), srt.RejectRogue, what)
			default:
				checkRawDefault(c, srs, raw, stream, cut, what)
			}
		}
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Stream: stream})), 0, "hooks of the truncated conclusions")
		checkSRTAlive(c, srs)
	})

	// A reason of 0 connects with no stream ID, and -1 with the stream ID cut
	// to the block's 16 bytes, a player of the first 5 bytes of the stream.
	// An HSREQ block of 4 words is read as one of 3 and a fourth word, the
	// stream ID block's header, so the stream ID is lost.
	check.Case(t, "ExtensionLengthLies", check.Normal, func(c *check.C) {
		stream := streamName(c)
		for _, l := range []struct {
			block  int
			words  uint16
			reason int
		}{
			{0, 0, srt.RejectRogue}, {0, 4, 0}, {0, 0xffff, srt.RejectRogue},
			{1, 0, srt.RejectUnknown}, {1, 4, -1}, {1, 0xffff, 0},
		} {
			raw := dialRaw(c, addr)
			lie := func(hs []byte) []byte {
				// The length of the HSREQ block at 64, or of the stream ID's
				// after it.
				binary.BigEndian.PutUint16(hs[64+l.block*16+2:], l.words)
				return hs
			}
			what := "block " + strconv.Itoa(l.block) + " of " + strconv.Itoa(int(l.words)) + " words"
			switch l.reason {
			case 0:
				checkRawDefault(c, srs, raw, stream, lie, what)
			case -1:
				ans := sendRawConclusion(c, raw, stream, lie)
				if c.Check(ans != nil && ans.HandshakeType == packet.HSTYPE_CONCLUSION, "%v answered with %v, want connected", what, handshakeOf(ans)) {
					raw.Peer = ans.SRTSocketId
					waitHook(c, srs, "on_play", stream[:5])
					raw.Shutdown()
				}
			default:
				checkRawRefused(c, sendRawConclusion(c, raw, stream, lie), l.reason, what)
			}
		}
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Stream: stream})), 0, "hooks of the lying conclusions")
		checkSRTAlive(c, srs)
	})

	check.Case(t, "ConclusionUnknownExtensions", check.Normal, func(c *check.C) {
		stream := streamName(c)
		raw := dialRaw(c, addr)
		ans := sendRawConclusion(c, raw, stream, func(hs []byte) []byte {
			for len(hs) < 1400 {
				hs = append(hs, 0x77, 0x77, 0, 15)
				hs = append(hs, make([]byte, 60)...)
			}
			return hs
		})
		if c.Check(ans != nil && ans.HandshakeType == packet.HSTYPE_CONCLUSION, "answered with %v", handshakeOf(ans)) {
			raw.Peer = ans.SRTSocketId
			waitHook(c, srs, "on_publish", stream)
			raw.Shutdown()
			waitHook(c, srs, "on_unpublish", stream)
		}
		checkSRTAlive(c, srs)
	})

	check.Case(t, "StreamIdOverMax", check.Normal, func(c *check.C) {
		stream := streamName(c)
		for _, size := range []int{srt.MaxStreamID + 1, srt.MaxStreamID + 4, 1200} {
			raw := dialRaw(c, addr)
			err := raw.Connect(srtPadID(stream, size), srtRobustLatency, time.Second)
			var rej *srt.RejectError
			if c.Check(errors.As(err, &rej), "a stream id of %v bytes: %v, want refused", size, err) {
				c.CheckEqual(rej.Reason, srt.RejectUnknown, "reject reason")
			}
		}
		c.CheckEqual(len(srs.Hooks.Requests(mock.Match{Stream: stream})), 0, "hooks of the long stream ids")
		checkSRTAlive(c, srs)
	})

	check.Case(t, "StreamId"+strconv.Itoa(srt.MaxStreamID-1), check.Normal, func(c *check.C) {
		stream := streamName(c)
		pub := dialSRT(c, srs, srt.Options{StreamID: srtPadID(stream, srt.MaxStreamID-1)})
		defer pub.Close()
		c.Check(waitSRTPublish(srs, stream), "a stream id of %v bytes did not publish", srt.MaxStreamID-1)
		checkSRTAlive(c, srs)
	})

	// To the listener, to random sockets, and to a live connection's socket
	// from another address, which libsrt drops as not its peer's.
	check.Case(t, "DataBeforeHandshake", check.Normal, func(c *check.C) {
		stream := streamName(c)
		play := collectSRT(srtPlayer(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request"), Latency: srtRobustLatency}, stream))
		pub := startRawPublisher(c, srs, stream)
		raw := dialRaw(c, addr)
		for i := uint32(0); i < 20; i++ {
			for _, dest := range []uint32{0, rand.Uint32(), pub.Peer} {
				raw.Write(srt.RawData(i, i+1, i*1000, dest, make([]byte, srt.MessageSize)))
			}
		}
		checkRawUnanswered(c, raw, "data before the handshake")
		msgs := srtRobustMessages(c, 3)
		for _, m := range msgs {
			c.Must(pub.SendData(m), "send")
		}
		checkRawStream(c, play, msgs)
		checkSRTAlive(c, srs)
	})

	for _, ct := range srtRobustControls {
		check.Case(t, "Control_"+ct.name, check.Normal, func(c *check.C) {
			stream := streamName(c)
			play := collectSRT(srtPlayer(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request"), Latency: srtRobustLatency}, stream))
			pub := startRawPublisher(c, srs, stream)
			msgs := srtRobustMessages(c, 4)
			c.Must(pub.SendData(msgs[0]), "send")
			for i, body := range [][]byte{nil, make([]byte, 4), randBytes(1456)} {
				c.Must(pub.SendControl(ct.typ, ct.subtype, rand.Uint32(), body), "send the control")
				c.Must(pub.SendData(msgs[i+1]), "send")
			}
			switch ct.effect {
			case srtControlHarmless:
				checkRawStream(c, play, msgs)
			case srtControlEnds:
				waitHook(c, srs, "on_unpublish", stream)
			}
			checkSRTAlive(c, srs)
		})
	}

	// A drop request of the next 10 packets, as a sender gives up messages
	// it could not send in time: libsrt gives them up, and the next caller
	// connects at once.
	check.Case(t, "DropReqNarrow", check.Normal, func(c *check.C) {
		stream := streamName(c)
		play := collectSRT(srtPlayer(c, srs, srt.Options{StreamID: srtID("live/"+stream, "request"), Latency: srtRobustLatency}, stream))
		pub := startRawPublisher(c, srs, stream)
		msgs := srtRobustMessages(c, 4)
		c.Must(pub.SendData(msgs[0]), "send")
		checkRawStream(c, play, msgs[:1])
		c.Must(pub.SendControl(srtDropReq, 0, 0, srtDropRange(pub.Seq(), 10)), "send the drop request")
		start := time.Now()
		checkSRTAlive(c, srs)
		c.Check(time.Since(start) < check.Scaled(200*time.Millisecond), "the next caller took %v", time.Since(start))
	})

	// SRS sends nothing to a publisher, so an ACK or a NAK of its packets is
	// of packets it never sent, which libsrt takes for an attack or a bug.
	// The full ACK is of 7 words, its first the sequence number.
	for _, ct := range []struct {
		name string
		typ  packet.CtrlType
		size int
	}{{"AckOfUnsent", packet.CTRLTYPE_ACK, 28}, {"NakOfUnsent", packet.CTRLTYPE_NAK, 4}} {
		check.Case(t, ct.name, check.Normal, func(c *check.C) {
			stream := streamName(c)
			pub := startRawPublisher(c, srs, stream)
			c.Must(pub.SendData(srtRobustMessages(c, 1)[0]), "send")
			body := append(srtSeqBody((pub.ISN+1000)&0x7fffffff), make([]byte, ct.size-4)...)
			c.Must(pub.SendControl(ct.typ, 0, 1, body), "send the control")
			waitHookWithin(c, srs, "on_unpublish", stream, check.Scaled(200*time.Millisecond))
			checkSRTAlive(c, srs)
		})
	}

	check.Case(t, "InductionFlood", check.Normal, func(c *check.C) {
		var raws []*srt.Raw
		for i := 0; i < 200; i++ {
			raw := dialRaw(c, addr)
			raw.Write(raw.Induction())
			raws = append(raws, raw)
		}
		answered := 0
		for _, raw := range raws {
			if _, err := raw.ReadHandshake(srtRobustWait); err == nil {
				answered++
			}
		}
		c.CheckEqual(answered, len(raws), "inductions answered")
		start := time.Now()
		checkSRTAlive(c, srs)
		c.Check(time.Since(start) < check.Scaled(200*time.Millisecond), "the next caller took %v", time.Since(start))
	})
}

// srtDropReq is the control type of a drop request.
const srtDropReq packet.CtrlType = 7

// srtSeqBody is the body of an ACK or a NAK of one sequence number: a lite
// ACK, or a loss report of one packet.
func srtSeqBody(seq uint32) []byte {
	b := make([]byte, 4)
	binary.BigEndian.PutUint32(b, seq)
	return b
}

// srtDropRange is the body of a drop request of n packets from seq.
func srtDropRange(seq uint32, n uint32) []byte {
	b := make([]byte, 8)
	binary.BigEndian.PutUint32(b, seq)
	binary.BigEndian.PutUint32(b[4:], (seq+n-1)&0x7fffffff)
	return b
}

// What a control packet with a random body does to a publisher's own
// connection.
const (
	// srtControlHarmless leaves the stream as it was.
	srtControlHarmless = iota
	// srtControlEnds ends the connection.
	srtControlEnds
	// srtControlOwn may lose the messages after it: new key material
	// decrypts nothing.
	srtControlOwn
)

// srtRobustControl is a control packet type and subtype, and its effect.
type srtRobustControl struct {
	name    string
	typ     packet.CtrlType
	subtype uint16
	effect  int
}

// srtRobustControls are the control types of SRT with a random body, but
// for the ACK, NAK and drop request, whose numbers decide what they do, a
// handshake on a live connection, the user-defined type with each extension
// subtype, and types nobody defines.
var srtRobustControls = []srtRobustControl{
	{"Handshake", packet.CTRLTYPE_HANDSHAKE, 0, srtControlHarmless},
	{"Keepalive", packet.CTRLTYPE_KEEPALIVE, 0, srtControlHarmless},
	{"CongestionWarning", packet.CTRLTYPE_WARN, 0, srtControlHarmless},
	{"Shutdown", packet.CTRLTYPE_SHUTDOWN, 0, srtControlEnds},
	{"AckAck", packet.CTRLTYPE_ACKACK, 0, srtControlHarmless},
	{"PeerError", 8, 0, srtControlHarmless},
	{"UserHsReq", packet.CTRLTYPE_USER, 1, srtControlHarmless},
	{"UserHsRsp", packet.CTRLTYPE_USER, 2, srtControlHarmless},
	{"UserKmReq", packet.CTRLTYPE_USER, 3, srtControlOwn},
	{"UserKmRsp", packet.CTRLTYPE_USER, 4, srtControlOwn},
	{"UserSid", packet.CTRLTYPE_USER, 5, srtControlHarmless},
	{"UserCongestion", packet.CTRLTYPE_USER, 6, srtControlHarmless},
	{"UserFilter", packet.CTRLTYPE_USER, 7, srtControlHarmless},
	{"UserGroup", packet.CTRLTYPE_USER, 8, srtControlHarmless},
	{"UserUnknown", packet.CTRLTYPE_USER, 0x7777, srtControlHarmless},
	{"Unknown", 0x1234, 0, srtControlHarmless},
}

// dialRaw opens a raw SRT socket to addr, closed when the case ends.
func dialRaw(c *check.C, addr string) *srt.Raw {
	c.Helper()
	raw, err := srt.DialRaw(addr)
	c.Must(err, "raw socket")
	c.Cleanup(func() { raw.Close() })
	return raw
}

// startRawPublisher connects a raw publisher of a stream and waits for its
// on_publish; it shuts the connection down when the case ends.
func startRawPublisher(c *check.C, srs *server.Server, stream string) *srt.Raw {
	c.Helper()
	raw := dialRaw(c, srs.Addr(server.SRT))
	c.Must(raw.Connect(srtID("live/"+stream, "publish"), srtRobustLatency, time.Second), "raw publisher")
	c.Cleanup(func() { raw.Shutdown() })
	waitHook(c, srs, "on_publish", stream)
	return raw
}

// sendRawConclusion sends the conclusion of a publisher of a stream, changed
// by edit, with the listener's cookie, and returns the answer, nil for none.
func sendRawConclusion(c *check.C, raw *srt.Raw, stream string, edit func([]byte) []byte) *packet.CIFHandshake {
	c.Helper()
	cookie, err := raw.Induct(time.Second)
	c.Must(err, "induction")
	raw.Write(edit(raw.Conclusion(cookie, srtID("live/"+stream, "publish"), srtRobustLatency)))
	ans, err := raw.ReadHandshake(srtRobustWait)
	if srt.IsTimeout(err) {
		return nil
	}
	c.Must(err, "read the answer")
	return ans
}

// checkRawRefused checks that a conclusion was refused for a reason.
func checkRawRefused(c *check.C, ans *packet.CIFHandshake, reason int, what string) {
	c.Helper()
	if c.Check(ans != nil, "%v answered nothing, want refused", what) {
		c.CheckEqual(int(ans.HandshakeType.Val()), reason, what+" refused")
	}
}

// srtRobustDefaultMu lets one case at a time connect a player of
// srtRobustDefault, so its on_play is the next one.
var srtRobustDefaultMu sync.Mutex

// checkRawDefault sends the conclusion of a publisher of a stream, changed by
// edit, and checks that it connected with no stream ID, so that SRS plays
// its default_streamid; then it shuts the connection down.
func checkRawDefault(c *check.C, srs *server.Server, raw *srt.Raw, stream string, edit func([]byte) []byte, what string) {
	c.Helper()
	srtRobustDefaultMu.Lock()
	defer srtRobustDefaultMu.Unlock()
	m := mock.Match{Action: "on_play", Stream: srtRobustDefault}
	n := len(srs.Hooks.Requests(m))

	ans := sendRawConclusion(c, raw, stream, edit)
	if !c.Check(ans != nil && ans.HandshakeType == packet.HSTYPE_CONCLUSION, "%v answered with %v, want connected", what, handshakeOf(ans)) {
		return
	}
	raw.Peer = ans.SRTSocketId
	waitHookCount(c, srs, m, n+1)
	raw.Shutdown()
}

// checkRawStream checks that the player got the messages, one after another.
func checkRawStream(c *check.C, play *srtGot, msgs [][]byte) {
	c.Helper()
	want := bytes.Join(msgs, nil)
	got := play.waitFor(check.Scaled(300*time.Millisecond), func(got []byte) bool { return bytes.Contains(got, want) })
	c.Check(bytes.Contains(got, want), "the player got %v of %v messages in order", countMessages(got, msgs), len(msgs))
}

// checkRawUnanswered checks that the listener answers nothing.
func checkRawUnanswered(c *check.C, raw *srt.Raw, what string) {
	c.Helper()
	p, err := raw.Read(srtRobustWait)
	if err == nil {
		c.Errorf("%v answered with %v", what, p.String())
		return
	}
	c.Check(srt.IsTimeout(err), "read: %v", err)
}

// waitSRTPublish waits up to 200ms, scaled, for the on_publish of a stream.
func waitSRTPublish(srs *server.Server, stream string) bool {
	deadline := time.Now().Add(check.Scaled(200 * time.Millisecond))
	for time.Now().Before(deadline) {
		if len(srs.Hooks.Requests(mock.Match{Action: "on_publish", Stream: stream})) > 0 {
			return true
		}
		time.Sleep(5 * time.Millisecond)
	}
	return false
}

// checkSRTAlive checks that the SRS process runs and that a new caller
// publishes.
func checkSRTAlive(c *check.C, srs *server.Server) {
	c.Helper()
	c.Must(syscall.Kill(srs.PID(), 0), "SRS is not running")
	checkSRTServes(c, srs, srt.Options{})
}

// srtPadID is a publish stream ID of a stream, padded by a parameter to size
// bytes.
func srtPadID(stream string, size int) string {
	id := srtID("live/"+stream, "publish") + ",pad="
	return id + strings.Repeat("x", size-len(id))
}

// srtRobustMessages are the first n messages of the TS test file.
func srtRobustMessages(c *check.C, n int) [][]byte {
	c.Helper()
	b, err := os.ReadFile(media.Path(c, media.H264HighAACTS))
	c.Must(err, "read the media")
	var msgs [][]byte
	for i := 0; i < n; i++ {
		msgs = append(msgs, b[i*srt.MessageSize:(i+1)*srt.MessageSize])
	}
	return msgs
}

func countMessages(got []byte, msgs [][]byte) int {
	n := 0
	for _, m := range msgs {
		if bytes.Contains(got, m) {
			n++
		}
	}
	return n
}

func randBytes(n int) []byte {
	b := make([]byte, n)
	rand.Read(b)
	return b
}

func handshakeOf(hs *packet.CIFHandshake) string {
	if hs == nil {
		return "none"
	}
	return hs.HandshakeType.String() + " " + strconv.FormatUint(uint64(hs.HandshakeType.Val()), 10)
}
