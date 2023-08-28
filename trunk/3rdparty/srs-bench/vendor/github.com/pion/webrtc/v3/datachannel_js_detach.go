// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build js && wasm
// +build js,wasm

package webrtc

import (
	"errors"
)

type detachedDataChannel struct {
	dc *DataChannel

	read chan DataChannelMessage
	done chan struct{}
}

func newDetachedDataChannel(dc *DataChannel) *detachedDataChannel {
	read := make(chan DataChannelMessage)
	done := make(chan struct{})

	// Wire up callbacks
	dc.OnMessage(func(msg DataChannelMessage) {
		read <- msg // pion/webrtc/projects/15
	})

	// pion/webrtc/projects/15

	return &detachedDataChannel{
		dc:   dc,
		read: read,
		done: done,
	}
}

func (c *detachedDataChannel) Read(p []byte) (int, error) {
	n, _, err := c.ReadDataChannel(p)
	return n, err
}

func (c *detachedDataChannel) ReadDataChannel(p []byte) (int, bool, error) {
	select {
	case <-c.done:
		return 0, false, errors.New("Reader closed")
	case msg := <-c.read:
		n := copy(p, msg.Data)
		if n < len(msg.Data) {
			return n, msg.IsString, errors.New("Read buffer to small")
		}
		return n, msg.IsString, nil
	}
}

func (c *detachedDataChannel) Write(p []byte) (n int, err error) {
	return c.WriteDataChannel(p, false)
}

func (c *detachedDataChannel) WriteDataChannel(p []byte, isString bool) (n int, err error) {
	if isString {
		err = c.dc.SendText(string(p))
		return len(p), err
	}

	err = c.dc.Send(p)

	return len(p), err
}

func (c *detachedDataChannel) Close() error {
	close(c.done)

	return c.dc.Close()
}
