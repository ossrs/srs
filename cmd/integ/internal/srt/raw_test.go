// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package srt

import (
	"bytes"
	"errors"
	"strings"
	"testing"
	"time"

	"github.com/datarhei/gosrt/packet"
)

// A raw caller connects to a gosrt and a zsiec listener with its stream ID,
// sends messages they read whole and in order, and ends the connection with
// a shutdown, which closes the listener's link.
func TestRawConnectsSendsAndShutsDown(t *testing.T) {
	for _, ln := range testListeners {
		t.Run(ln.name, func(t *testing.T) {
			got := make(chan string, 1)
			msgs := make(chan []byte, 10)
			closed := make(chan error, 1)
			addr := ln.listen(t, "", 0, func(c peer, streamID string) {
				got <- streamID
				b := make([]byte, 2048)
				for {
					n, err := c.Read(b)
					if err != nil {
						closed <- err
						return
					}
					msgs <- append([]byte(nil), b[:n]...)
				}
			})

			raw, err := DialRaw(addr)
			if err != nil {
				t.Fatal(err)
			}
			defer raw.Close()
			if err := raw.Connect("#!::r=live/raw,m=publish", 120*time.Millisecond, time.Second); err != nil {
				t.Fatal(err)
			}
			if raw.Peer == 0 {
				t.Error("no socket ID of the listener")
			}
			if id := <-got; id != "#!::r=live/raw,m=publish" {
				t.Errorf("stream ID %q", id)
			}

			for i := 0; i < 3; i++ {
				if err := raw.SendData(bytes.Repeat([]byte{byte('a' + i)}, MessageSize)); err != nil {
					t.Fatal(err)
				}
			}
			for i := 0; i < 3; i++ {
				select {
				case m := <-msgs:
					if !bytes.Equal(m, bytes.Repeat([]byte{byte('a' + i)}, MessageSize)) {
						t.Errorf("message %v: %v bytes of %q", i, len(m), m[:1])
					}
				case <-time.After(time.Second):
					t.Fatalf("message %v not read", i)
				}
			}

			if err := raw.Shutdown(); err != nil {
				t.Fatal(err)
			}
			select {
			case <-closed:
			case <-time.After(time.Second):
				t.Error("the listener's link not closed by the shutdown")
			}
		})
	}
}

// A stream ID over MaxStreamID, which no library sends, goes out as it is;
// a listener that cannot carry it refuses or drops the handshake.
func TestRawLongStreamID(t *testing.T) {
	addr := listenGosrt(t, "", 0, func(c peer, streamID string) {})
	raw, err := DialRaw(addr)
	if err != nil {
		t.Fatal(err)
	}
	defer raw.Close()
	cookie, err := raw.Induct(time.Second)
	if err != nil {
		t.Fatal(err)
	}
	hs := raw.Conclusion(cookie, strings.Repeat("x", 600), 0)
	if len(hs) < 64+16+4+600 {
		t.Errorf("conclusion of %v bytes, want the whole stream ID", len(hs))
	}
}

// The packets the builders make read back as what they were given.
func TestRawPackets(t *testing.T) {
	p, err := packet.NewPacketFromData(nil, RawControl(packet.CTRLTYPE_USER, 5, 7, 9, 11, []byte("body")))
	if err != nil {
		t.Fatal(err)
	}
	h := p.Header()
	if !h.IsControlPacket || h.ControlType != packet.CTRLTYPE_USER || h.SubType != 5 || h.TypeSpecific != 7 ||
		h.Timestamp != 9 || h.DestinationSocketId != 11 || string(p.Data()) != "body" {
		t.Errorf("control packet read back as %v", p)
	}

	p, err = packet.NewPacketFromData(nil, RawData(100, 3, 9, 11, []byte("ts")))
	if err != nil {
		t.Fatal(err)
	}
	h = p.Header()
	if h.IsControlPacket || h.PacketSequenceNumber.Val() != 100 || h.MessageNumber != 3 || !h.OrderFlag ||
		h.PacketPositionFlag != packet.SinglePacket || h.DestinationSocketId != 11 || string(p.Data()) != "ts" {
		t.Errorf("data packet read back as %v", p)
	}
}

// A read with nothing to read times out.
func TestRawReadTimeout(t *testing.T) {
	addr := listenGosrt(t, "", 0, func(c peer, streamID string) {})
	raw, err := DialRaw(addr)
	if err != nil {
		t.Fatal(err)
	}
	defer raw.Close()
	if _, err := raw.Read(20 * time.Millisecond); !IsTimeout(err) {
		t.Errorf("read: %v, want a timeout", err)
	}
	if IsTimeout(errors.New("other")) {
		t.Error("another error is a timeout")
	}
}
