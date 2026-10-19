package main

import (
	"bytes"
	"crypto/rand"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"math"
	"net"
	"os"
	"sync"
	"time"
)

func amfString(value string) []byte {
	b := make([]byte, 3+len(value))
	b[0] = 2
	binary.BigEndian.PutUint16(b[1:3], uint16(len(value)))
	copy(b[3:], value)
	return b
}

func amfNumber(value float64) []byte {
	b := make([]byte, 9)
	binary.BigEndian.PutUint64(b[1:], math.Float64bits(value))
	return b
}

func amfObject(pairs ...string) []byte {
	var b bytes.Buffer
	b.WriteByte(3)
	for i := 0; i+1 < len(pairs); i += 2 {
		var n [2]byte
		binary.BigEndian.PutUint16(n[:], uint16(len(pairs[i])))
		b.Write(n[:])
		b.WriteString(pairs[i])
		b.Write(amfString(pairs[i+1]))
	}
	b.Write([]byte{0, 0, 9})
	return b.Bytes()
}

type rtmpClient struct {
	conn net.Conn
	r    *rtmpReader
	w    *rtmpWriter
	id   uint32
}

func dialRTMP(addr, app string) (*rtmpClient, error) {
	conn, err := net.DialTimeout("tcp", addr, 5*time.Second)
	if err != nil {
		return nil, err
	}
	conn.SetDeadline(time.Now().Add(10 * time.Second))
	c := &rtmpClient{conn: conn, r: newRTMPReader(conn), w: newRTMPWriter(conn)}
	var c0c1 [1537]byte
	c0c1[0] = 3
	if _, err = rand.Read(c0c1[9:]); err == nil {
		_, err = conn.Write(c0c1[:])
	}
	var s0s1s2 [3073]byte
	if err == nil {
		_, err = io.ReadFull(conn, s0s1s2[:])
	}
	if err == nil && s0s1s2[0] != 3 {
		err = errors.New("unsupported RTMP handshake version")
	}
	if err == nil {
		_, err = conn.Write(s0s1s2[1:1537])
	}
	if err != nil {
		conn.Close()
		return nil, err
	}
	payload := append(amfString("connect"), amfNumber(1)...)
	payload = append(payload, amfObject("app", app, "tcUrl", "rtmp://"+addr+"/"+app, "flashVer", "srs-perf/1")...)
	if err = c.w.write(3, rtmpMessage{typeID: 20, payload: payload}); err == nil {
		_, err = c.waitCommand("_result")
	}
	if err == nil {
		payload = append(amfString("createStream"), amfNumber(2)...)
		payload = append(payload, 5)
		err = c.w.write(3, rtmpMessage{typeID: 20, payload: payload})
	}
	if err == nil {
		var response rtmpMessage
		response, err = c.waitCommand("_result")
		if err == nil {
			c.id, err = parseStreamID(response.payload)
		}
	}
	if err != nil {
		conn.Close()
		return nil, err
	}
	return c, nil
}

func (c *rtmpClient) waitCommand(name string) (rtmpMessage, error) {
	for {
		m, err := c.r.read()
		if err != nil {
			return m, err
		}
		if m.typeID != 20 && m.typeID != 17 {
			continue
		}
		payload := m.payload
		if m.typeID == 17 && len(payload) > 0 {
			payload = payload[1:]
		}
		if len(payload) < 3 || payload[0] != 2 {
			continue
		}
		n := int(binary.BigEndian.Uint16(payload[1:3]))
		if len(payload) >= n+3 && string(payload[3:n+3]) == name {
			return m, nil
		}
		if len(payload) >= n+3 && string(payload[3:n+3]) == "_error" {
			return m, errors.New("RTMP command rejected")
		}
	}
}

func parseStreamID(payload []byte) (uint32, error) {
	if len(payload) < 3 || payload[0] != 2 {
		return 0, errors.New("missing createStream result")
	}
	n := int(binary.BigEndian.Uint16(payload[1:3]))
	offset := 3 + n + 9
	if len(payload) < offset+10 || payload[offset] != 5 || payload[offset+1] != 0 {
		return 0, errors.New("invalid createStream result")
	}
	id := math.Float64frombits(binary.BigEndian.Uint64(payload[offset+2 : offset+10]))
	if id < 1 || id > math.MaxUint32 {
		return 0, errors.New("invalid RTMP stream ID")
	}
	return uint32(id), nil
}

func (c *rtmpClient) command(name, stream string) error {
	payload := append(amfString(name), amfNumber(0)...)
	payload = append(payload, 5)
	payload = append(payload, amfString(stream)...)
	if name == "publish" {
		payload = append(payload, amfString("live")...)
	}
	return c.w.write(8, rtmpMessage{typeID: 20, streamID: c.id, payload: payload})
}

type mediaClock struct {
	mu     sync.RWMutex
	newest uint32
}

func (c *mediaClock) set(ts uint32) { c.mu.Lock(); c.newest = ts; c.mu.Unlock() }
func (c *mediaClock) get() uint32   { c.mu.RLock(); defer c.mu.RUnlock(); return c.newest }

func publishRTMP(c *rtmpClient, stream string, tags []rtmpMessage, clock *mediaClock, stop <-chan struct{}) error {
	defer c.conn.Close()
	if err := c.command("publish", stream); err != nil {
		return err
	}
	if _, err := c.waitCommand("onStatus"); err != nil {
		return err
	}
	c.conn.SetDeadline(time.Time{})
	start := time.Now()
	duration := loopDuration(tags)
	for loop := uint32(0); ; loop++ {
		for _, original := range tags {
			tag := original
			ts := loop*duration + tag.timestamp
			wait := time.Until(start.Add(time.Duration(ts) * time.Millisecond))
			if wait > 0 {
				timer := time.NewTimer(wait)
				select {
				case <-stop:
					timer.Stop()
					return nil
				case <-timer.C:
				}
			}
			select {
			case <-stop:
				return nil
			default:
			}
			tag.timestamp = ts
			tag.streamID = c.id
			csid := 6
			if tag.typeID == 8 {
				csid = 4
			}
			if err := c.w.write(csid, tag); err != nil {
				return err
			}
			clock.set(ts)
		}
	}
}

func playRTMP(c *rtmpClient, stream string, stop <-chan struct{}, report chan<- rtmpMessage) error {
	defer c.conn.Close()
	if err := c.command("play", stream); err != nil {
		return err
	}
	c.conn.SetDeadline(time.Time{})
	done := make(chan struct{})
	defer close(done)
	go func() {
		select {
		case <-stop:
			c.conn.Close()
		case <-done:
		}
	}()
	for {
		m, err := c.r.read()
		if err != nil {
			select {
			case <-stop:
				return nil
			default:
			}
			return err
		}
		if m.typeID == 8 || m.typeID == 9 {
			select {
			case report <- m:
			case <-stop:
				return nil
			}
		}
	}
}

func loadFLV(path string) ([]rtmpMessage, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("open FLV: %w", err)
	}
	defer f.Close()
	return readFLVTags(f)
}
