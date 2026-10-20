package main

import (
	"context"
	"errors"
	"fmt"
	"net"
	"sync/atomic"
	"time"

	"github.com/bluenviron/gortsplib/v5"
	"github.com/bluenviron/gortsplib/v5/pkg/base"
	"github.com/bluenviron/gortsplib/v5/pkg/format"
	"github.com/pion/rtp"
)

// rtspPort is SRS's RTSP listen port in the rtmp-rtsp scenario, as in the
// shipped rtsp.conf.
const rtspPort = "8554"

// rtspClient is a lean RTSP player with gortsplib: media over the RTSP
// connection (TCP interleaved, the only transport SRS plays), H.264 access
// units rebuilt from RTP for the lag anchor, and AAC counted per RTP packet
// with no decode.
type rtspClient struct {
	conn net.Conn
	addr string
	app  string
}

// dialRTSP connects first, so a dial timeout is a net.Error that dialPlayer
// retries, as for the other players; play hands the connection to gortsplib.
func dialRTSP(addr, app string) (*rtspClient, error) {
	conn, err := net.DialTimeout("tcp", addr, 5*time.Second)
	if err != nil {
		return nil, err
	}
	return &rtspClient{conn: conn, addr: addr, app: app}, nil
}

// avcPayload is an H.264 access unit as an FLV AVC frame with 4-byte NALU
// lengths, a keyframe when it has an IDR slice.
func avcPayload(au [][]byte) []byte {
	frame, size := byte(0x27), 5
	for _, nalu := range au {
		if len(nalu) > 0 && nalu[0]&0x1f == 5 {
			frame = 0x17
		}
		size += 4 + len(nalu)
	}
	b := make([]byte, 5, size)
	b[0], b[1] = frame, 1
	for _, nalu := range au {
		n := len(nalu)
		b = append(b, byte(n>>24), byte(n>>16), byte(n>>8), byte(n))
		b = append(b, nalu...)
	}
	return b
}

// rtpClock turns RTP timestamps into milliseconds from the first one, across
// the 32-bit wrap.
type rtpClock struct {
	rate    int64
	started bool
	last    uint32
	ticks   int64
}

func (c *rtpClock) millis(ts uint32) uint32 {
	if !c.started {
		c.started, c.last = true, ts
	}
	c.ticks += int64(int32(ts - c.last))
	c.last = ts
	if c.ticks < 0 {
		return 0
	}
	return uint32(c.ticks * 1000 / c.rate)
}

// play sends DESCRIBE, SETUP of the H.264 media and of the AAC media if any,
// and PLAY for /<app>/<stream>, then reports the frames until stop, which
// closes the session and returns nil. The two RTP clocks start at unrelated
// values, so lag is measured on video: an audio packet is reported at the
// newest video time.
func (c *rtspClient) play(stream string, stop <-chan struct{}, report chan<- rtmpMessage) error {
	conn := c.conn
	c.conn = nil
	tcp := gortsplib.ProtocolTCP
	client := &gortsplib.Client{
		Scheme:   "rtsp",
		Host:     c.addr,
		Protocol: &tcp,
		DialContext: func(context.Context, string, string) (net.Conn, error) {
			if conn == nil {
				return nil, errors.New("RTSP player has no connection to reuse")
			}
			given := conn
			conn = nil
			return given, nil
		},
		UserAgent:         "srs-perf/1",
		OnTransportSwitch: func(error) {},
		OnPacketsLost:     func(uint64) {},
		OnDecodeError:     func(error) {},
	}
	if err := client.Start(); err != nil {
		conn.Close()
		return err
	}
	defer client.Close()
	done := make(chan struct{})
	defer close(done)
	go func() {
		select {
		case <-stop:
			client.Close()
		case <-done:
		}
	}()
	stopped := func(err error) error {
		select {
		case <-stop:
			return nil
		default:
			return err
		}
	}
	u, err := base.ParseURL(fmt.Sprintf("rtsp://%s/%s/%s", c.addr, c.app, stream))
	if err != nil {
		return err
	}
	desc, _, err := client.Describe(u)
	if err != nil {
		return stopped(fmt.Errorf("DESCRIBE: %w", err))
	}
	var h264 *format.H264
	video := desc.FindFormat(&h264)
	if video == nil {
		return fmt.Errorf("RTSP stream has no H.264 media")
	}
	var aac *format.MPEG4Audio
	audio := desc.FindFormat(&aac)
	if _, err := client.Setup(desc.BaseURL, video, 0, 0); err != nil {
		return stopped(fmt.Errorf("SETUP: %w", err))
	}
	if audio != nil {
		if _, err := client.Setup(desc.BaseURL, audio, 0, 0); err != nil {
			return stopped(fmt.Errorf("SETUP: %w", err))
		}
	}
	decoder, err := h264.CreateDecoder()
	if err != nil {
		return err
	}
	send := func(m rtmpMessage) {
		select {
		case report <- m:
		case <-stop:
		}
	}
	clock := &rtpClock{rate: int64(h264.ClockRate())}
	var newest atomic.Uint32
	client.OnPacketRTP(video, h264, func(pkt *rtp.Packet) {
		ms := clock.millis(pkt.Timestamp)
		au, err := decoder.Decode(pkt)
		if err != nil {
			return
		}
		newest.Store(ms)
		send(rtmpMessage{typeID: 9, timestamp: ms, payload: avcPayload(au)})
	})
	if audio != nil {
		client.OnPacketRTP(audio, aac, func(pkt *rtp.Packet) {
			send(rtmpMessage{typeID: 8, timestamp: newest.Load(), payload: pkt.Payload})
		})
	}
	if _, err := client.Play(nil); err != nil {
		return stopped(fmt.Errorf("PLAY: %w", err))
	}
	return stopped(client.Wait())
}
