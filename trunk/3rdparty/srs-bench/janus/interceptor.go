// The MIT License (MIT)
//
// # Copyright (c) 2021 Winlin
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
package janus

import (
	"github.com/pion/interceptor"
	"github.com/pion/rtcp"
	"github.com/pion/rtp"
)

type rtpInterceptorOptionFunc func(i *rtpInterceptor)

type rtpInteceptorFactory struct {
	p *rtpInterceptor
}

func (v *rtpInteceptorFactory) NewInterceptor(id string) (interceptor.Interceptor, error) {
	return v.p, nil
}

// Common RTP packet interceptor for benchmark.
// @remark Should never merge with rtcpInterceptor, because they has the same Write interface.
type rtpInterceptor struct {
	// If rtpReader is nil, use the default next one to read.
	rtpReader     interceptor.RTPReaderFunc
	nextRTPReader interceptor.RTPReader
	// If rtpWriter is nil, use the default next one to write.
	rtpWriter     interceptor.RTPWriterFunc
	nextRTPWriter interceptor.RTPWriter
	// Other common fields.
	bypassInterceptor
}

func newRTPInterceptor(options ...rtpInterceptorOptionFunc) *rtpInteceptorFactory {
	v := &rtpInterceptor{}
	for _, opt := range options {
		opt(v)
	}
	return &rtpInteceptorFactory{v}
}

func (v *rtpInterceptor) BindLocalStream(info *interceptor.StreamInfo, writer interceptor.RTPWriter) interceptor.RTPWriter {
	v.nextRTPWriter = writer
	return v // Handle all RTP
}

func (v *rtpInterceptor) Write(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
	if v.rtpWriter != nil {
		return v.rtpWriter(header, payload, attributes)
	}
	return v.nextRTPWriter.Write(header, payload, attributes)
}

func (v *rtpInterceptor) UnbindLocalStream(info *interceptor.StreamInfo) {
}

func (v *rtpInterceptor) BindRemoteStream(info *interceptor.StreamInfo, reader interceptor.RTPReader) interceptor.RTPReader {
	v.nextRTPReader = reader
	return v // Handle all RTP
}

func (v *rtpInterceptor) Read(b []byte, a interceptor.Attributes) (int, interceptor.Attributes, error) {
	if v.rtpReader != nil {
		return v.rtpReader(b, a)
	}
	return v.nextRTPReader.Read(b, a)
}

func (v *rtpInterceptor) UnbindRemoteStream(info *interceptor.StreamInfo) {
}

type rtcpInterceptorOptionFunc func(i *rtcpInterceptor)

type rtcpInteceptorFactory struct {
	p *rtcpInterceptor
}

func (v *rtcpInteceptorFactory) NewInterceptor(id string) (interceptor.Interceptor, error) {
	return v.p, nil
}

// Common RTCP packet interceptor for benchmark.
// @remark Should never merge with rtpInterceptor, because they has the same Write interface.
type rtcpInterceptor struct {
	// If rtcpReader is nil, use the default next one to read.
	rtcpReader     interceptor.RTCPReaderFunc
	nextRTCPReader interceptor.RTCPReader
	// If rtcpWriter is nil, use the default next one to write.
	rtcpWriter     interceptor.RTCPWriterFunc
	nextRTCPWriter interceptor.RTCPWriter
	// Other common fields.
	bypassInterceptor
}

func newRTCPInterceptor(options ...rtcpInterceptorOptionFunc) *rtcpInteceptorFactory {
	v := &rtcpInterceptor{}
	for _, opt := range options {
		opt(v)
	}
	return &rtcpInteceptorFactory{v}
}

func (v *rtcpInterceptor) BindRTCPReader(reader interceptor.RTCPReader) interceptor.RTCPReader {
	v.nextRTCPReader = reader
	return v // Handle all RTCP
}

func (v *rtcpInterceptor) Read(b []byte, a interceptor.Attributes) (int, interceptor.Attributes, error) {
	if v.rtcpReader != nil {
		return v.rtcpReader(b, a)
	}
	return v.nextRTCPReader.Read(b, a)
}

func (v *rtcpInterceptor) BindRTCPWriter(writer interceptor.RTCPWriter) interceptor.RTCPWriter {
	v.nextRTCPWriter = writer
	return v // Handle all RTCP
}

func (v *rtcpInterceptor) Write(pkts []rtcp.Packet, attributes interceptor.Attributes) (int, error) {
	if v.rtcpWriter != nil {
		return v.rtcpWriter(pkts, attributes)
	}
	return v.nextRTCPWriter.Write(pkts, attributes)
}

// Do nothing.
type bypassInterceptor struct {
	interceptor.Interceptor
}

func (v *bypassInterceptor) BindRTCPReader(reader interceptor.RTCPReader) interceptor.RTCPReader {
	return reader
}

func (v *bypassInterceptor) BindRTCPWriter(writer interceptor.RTCPWriter) interceptor.RTCPWriter {
	return writer
}

func (v *bypassInterceptor) BindLocalStream(info *interceptor.StreamInfo, writer interceptor.RTPWriter) interceptor.RTPWriter {
	return writer
}

func (v *bypassInterceptor) UnbindLocalStream(info *interceptor.StreamInfo) {
}

func (v *bypassInterceptor) BindRemoteStream(info *interceptor.StreamInfo, reader interceptor.RTPReader) interceptor.RTPReader {
	return reader
}

func (v *bypassInterceptor) UnbindRemoteStream(info *interceptor.StreamInfo) {
}

func (v *bypassInterceptor) Close() error {
	return nil
}
