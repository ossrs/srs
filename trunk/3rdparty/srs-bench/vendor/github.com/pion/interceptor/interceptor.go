// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package interceptor contains the Interceptor interface, with some useful interceptors that should be safe to use
// in most cases.
package interceptor

import (
	"io"

	"github.com/pion/rtcp"
	"github.com/pion/rtp"
)

// Factory provides an interface for constructing interceptors
type Factory interface {
	NewInterceptor(id string) (Interceptor, error)
}

// Interceptor can be used to add functionality to you PeerConnections by modifying any incoming/outgoing rtp/rtcp
// packets, or sending your own packets as needed.
type Interceptor interface {
	// BindRTCPReader lets you modify any incoming RTCP packets. It is called once per sender/receiver, however this might
	// change in the future. The returned method will be called once per packet batch.
	BindRTCPReader(reader RTCPReader) RTCPReader

	// BindRTCPWriter lets you modify any outgoing RTCP packets. It is called once per PeerConnection. The returned method
	// will be called once per packet batch.
	BindRTCPWriter(writer RTCPWriter) RTCPWriter

	// BindLocalStream lets you modify any outgoing RTP packets. It is called once for per LocalStream. The returned method
	// will be called once per rtp packet.
	BindLocalStream(info *StreamInfo, writer RTPWriter) RTPWriter

	// UnbindLocalStream is called when the Stream is removed. It can be used to clean up any data related to that track.
	UnbindLocalStream(info *StreamInfo)

	// BindRemoteStream lets you modify any incoming RTP packets. It is called once for per RemoteStream. The returned method
	// will be called once per rtp packet.
	BindRemoteStream(info *StreamInfo, reader RTPReader) RTPReader

	// UnbindRemoteStream is called when the Stream is removed. It can be used to clean up any data related to that track.
	UnbindRemoteStream(info *StreamInfo)

	io.Closer
}

// RTPWriter is used by Interceptor.BindLocalStream.
type RTPWriter interface {
	// Write a rtp packet
	Write(header *rtp.Header, payload []byte, attributes Attributes) (int, error)
}

// RTPReader is used by Interceptor.BindRemoteStream.
type RTPReader interface {
	// Read a rtp packet
	Read([]byte, Attributes) (int, Attributes, error)
}

// RTCPWriter is used by Interceptor.BindRTCPWriter.
type RTCPWriter interface {
	// Write a batch of rtcp packets
	Write(pkts []rtcp.Packet, attributes Attributes) (int, error)
}

// RTCPReader is used by Interceptor.BindRTCPReader.
type RTCPReader interface {
	// Read a batch of rtcp packets
	Read([]byte, Attributes) (int, Attributes, error)
}

// RTPWriterFunc is an adapter for RTPWrite interface
type RTPWriterFunc func(header *rtp.Header, payload []byte, attributes Attributes) (int, error)

// RTPReaderFunc is an adapter for RTPReader interface
type RTPReaderFunc func([]byte, Attributes) (int, Attributes, error)

// RTCPWriterFunc is an adapter for RTCPWriter interface
type RTCPWriterFunc func(pkts []rtcp.Packet, attributes Attributes) (int, error)

// RTCPReaderFunc is an adapter for RTCPReader interface
type RTCPReaderFunc func([]byte, Attributes) (int, Attributes, error)

// Write a rtp packet
func (f RTPWriterFunc) Write(header *rtp.Header, payload []byte, attributes Attributes) (int, error) {
	return f(header, payload, attributes)
}

// Read a rtp packet
func (f RTPReaderFunc) Read(b []byte, a Attributes) (int, Attributes, error) {
	return f(b, a)
}

// Write a batch of rtcp packets
func (f RTCPWriterFunc) Write(pkts []rtcp.Packet, attributes Attributes) (int, error) {
	return f(pkts, attributes)
}

// Read a batch of rtcp packets
func (f RTCPReaderFunc) Read(b []byte, a Attributes) (int, Attributes, error) {
	return f(b, a)
}
