// The MIT License (MIT)
//
// Copyright (c) 2021 Winlin
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
package srs

import (
	"github.com/pion/interceptor"
	"github.com/pion/rtcp"
	"github.com/pion/rtp"
)

type RTPInterceptorOptionFunc func(i *RTPInterceptor)

// Common RTP packet interceptor for benchmark.
// @remark Should never merge with RTCPInterceptor, because they has the same Write interface.
type RTPInterceptor struct {
	// If rtpReader is nil, use the default next one to read.
	rtpReader     interceptor.RTPReaderFunc
	nextRTPReader interceptor.RTPReader
	// If rtpWriter is nil, use the default next one to write.
	rtpWriter     interceptor.RTPWriterFunc
	nextRTPWriter interceptor.RTPWriter
	// Other common fields.
	BypassInterceptor
}

func NewRTPInterceptor(options ...RTPInterceptorOptionFunc) *RTPInterceptor {
	v := &RTPInterceptor{}
	for _, opt := range options {
		opt(v)
	}
	return v
}

func (v *RTPInterceptor) BindLocalStream(info *interceptor.StreamInfo, writer interceptor.RTPWriter) interceptor.RTPWriter {
	v.nextRTPWriter = writer
	return v // Handle all RTP
}

func (v *RTPInterceptor) Write(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
	if v.rtpWriter != nil {
		return v.rtpWriter(header, payload, attributes)
	}
	return v.nextRTPWriter.Write(header, payload, attributes)
}

func (v *RTPInterceptor) UnbindLocalStream(info *interceptor.StreamInfo) {
}

func (v *RTPInterceptor) BindRemoteStream(info *interceptor.StreamInfo, reader interceptor.RTPReader) interceptor.RTPReader {
	v.nextRTPReader = reader
	return v // Handle all RTP
}

func (v *RTPInterceptor) Read(b []byte, a interceptor.Attributes) (int, interceptor.Attributes, error) {
	if v.rtpReader != nil {
		return v.rtpReader(b, a)
	}
	return v.nextRTPReader.Read(b, a)
}

func (v *RTPInterceptor) UnbindRemoteStream(info *interceptor.StreamInfo) {
}

type RTCPInterceptorOptionFunc func(i *RTCPInterceptor)

// Common RTCP packet interceptor for benchmark.
// @remark Should never merge with RTPInterceptor, because they has the same Write interface.
type RTCPInterceptor struct {
	// If rtcpReader is nil, use the default next one to read.
	rtcpReader     interceptor.RTCPReaderFunc
	nextRTCPReader interceptor.RTCPReader
	// If rtcpWriter is nil, use the default next one to write.
	rtcpWriter     interceptor.RTCPWriterFunc
	nextRTCPWriter interceptor.RTCPWriter
	// Other common fields.
	BypassInterceptor
}

func NewRTCPInterceptor(options ...RTCPInterceptorOptionFunc) *RTCPInterceptor {
	v := &RTCPInterceptor{}
	for _, opt := range options {
		opt(v)
	}
	return v
}

func (v *RTCPInterceptor) BindRTCPReader(reader interceptor.RTCPReader) interceptor.RTCPReader {
	v.nextRTCPReader = reader
	return v // Handle all RTCP
}

func (v *RTCPInterceptor) Read(b []byte, a interceptor.Attributes) (int, interceptor.Attributes, error) {
	if v.rtcpReader != nil {
		return v.rtcpReader(b, a)
	}
	return v.nextRTCPReader.Read(b, a)
}

func (v *RTCPInterceptor) BindRTCPWriter(writer interceptor.RTCPWriter) interceptor.RTCPWriter {
	v.nextRTCPWriter = writer
	return v // Handle all RTCP
}

func (v *RTCPInterceptor) Write(pkts []rtcp.Packet, attributes interceptor.Attributes) (int, error) {
	if v.rtcpWriter != nil {
		return v.rtcpWriter(pkts, attributes)
	}
	return v.nextRTCPWriter.Write(pkts, attributes)
}

// Do nothing.
type BypassInterceptor struct {
	interceptor.Interceptor
}

func (v *BypassInterceptor) BindRTCPReader(reader interceptor.RTCPReader) interceptor.RTCPReader {
	return reader
}

func (v *BypassInterceptor) BindRTCPWriter(writer interceptor.RTCPWriter) interceptor.RTCPWriter {
	return writer
}

func (v *BypassInterceptor) BindLocalStream(info *interceptor.StreamInfo, writer interceptor.RTPWriter) interceptor.RTPWriter {
	return writer
}

func (v *BypassInterceptor) UnbindLocalStream(info *interceptor.StreamInfo) {
}

func (v *BypassInterceptor) BindRemoteStream(info *interceptor.StreamInfo, reader interceptor.RTPReader) interceptor.RTPReader {
	return reader
}

func (v *BypassInterceptor) UnbindRemoteStream(info *interceptor.StreamInfo) {
}

func (v *BypassInterceptor) Close() error {
	return nil
}
