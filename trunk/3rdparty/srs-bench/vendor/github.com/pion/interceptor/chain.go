// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package interceptor

// Chain is an interceptor that runs all child interceptors in order.
type Chain struct {
	interceptors []Interceptor
}

// NewChain returns a new Chain interceptor.
func NewChain(interceptors []Interceptor) *Chain {
	return &Chain{interceptors: interceptors}
}

// BindRTCPReader lets you modify any incoming RTCP packets. It is called once per sender/receiver, however this might
// change in the future. The returned method will be called once per packet batch.
func (i *Chain) BindRTCPReader(reader RTCPReader) RTCPReader {
	for _, interceptor := range i.interceptors {
		reader = interceptor.BindRTCPReader(reader)
	}

	return reader
}

// BindRTCPWriter lets you modify any outgoing RTCP packets. It is called once per PeerConnection. The returned method
// will be called once per packet batch.
func (i *Chain) BindRTCPWriter(writer RTCPWriter) RTCPWriter {
	for _, interceptor := range i.interceptors {
		writer = interceptor.BindRTCPWriter(writer)
	}

	return writer
}

// BindLocalStream lets you modify any outgoing RTP packets. It is called once for per LocalStream. The returned method
// will be called once per rtp packet.
func (i *Chain) BindLocalStream(ctx *StreamInfo, writer RTPWriter) RTPWriter {
	for _, interceptor := range i.interceptors {
		writer = interceptor.BindLocalStream(ctx, writer)
	}

	return writer
}

// UnbindLocalStream is called when the Stream is removed. It can be used to clean up any data related to that track.
func (i *Chain) UnbindLocalStream(ctx *StreamInfo) {
	for _, interceptor := range i.interceptors {
		interceptor.UnbindLocalStream(ctx)
	}
}

// BindRemoteStream lets you modify any incoming RTP packets. It is called once for per RemoteStream. The returned method
// will be called once per rtp packet.
func (i *Chain) BindRemoteStream(ctx *StreamInfo, reader RTPReader) RTPReader {
	for _, interceptor := range i.interceptors {
		reader = interceptor.BindRemoteStream(ctx, reader)
	}

	return reader
}

// UnbindRemoteStream is called when the Stream is removed. It can be used to clean up any data related to that track.
func (i *Chain) UnbindRemoteStream(ctx *StreamInfo) {
	for _, interceptor := range i.interceptors {
		interceptor.UnbindRemoteStream(ctx)
	}
}

// Close closes the Interceptor, cleaning up any data if necessary.
func (i *Chain) Close() error {
	var errs []error
	for _, interceptor := range i.interceptors {
		errs = append(errs, interceptor.Close())
	}

	return flattenErrs(errs)
}
