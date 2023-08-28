// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package interceptor

// NoOp is an Interceptor that does not modify any packets. It can embedded in other interceptors, so it's
// possible to implement only a subset of the methods.
type NoOp struct{}

// BindRTCPReader lets you modify any incoming RTCP packets. It is called once per sender/receiver, however this might
// change in the future. The returned method will be called once per packet batch.
func (i *NoOp) BindRTCPReader(reader RTCPReader) RTCPReader {
	return reader
}

// BindRTCPWriter lets you modify any outgoing RTCP packets. It is called once per PeerConnection. The returned method
// will be called once per packet batch.
func (i *NoOp) BindRTCPWriter(writer RTCPWriter) RTCPWriter {
	return writer
}

// BindLocalStream lets you modify any outgoing RTP packets. It is called once for per LocalStream. The returned method
// will be called once per rtp packet.
func (i *NoOp) BindLocalStream(_ *StreamInfo, writer RTPWriter) RTPWriter {
	return writer
}

// UnbindLocalStream is called when the Stream is removed. It can be used to clean up any data related to that track.
func (i *NoOp) UnbindLocalStream(_ *StreamInfo) {}

// BindRemoteStream lets you modify any incoming RTP packets. It is called once for per RemoteStream. The returned method
// will be called once per rtp packet.
func (i *NoOp) BindRemoteStream(_ *StreamInfo, reader RTPReader) RTPReader {
	return reader
}

// UnbindRemoteStream is called when the Stream is removed. It can be used to clean up any data related to that track.
func (i *NoOp) UnbindRemoteStream(_ *StreamInfo) {}

// Close closes the Interceptor, cleaning up any data if necessary.
func (i *NoOp) Close() error {
	return nil
}
