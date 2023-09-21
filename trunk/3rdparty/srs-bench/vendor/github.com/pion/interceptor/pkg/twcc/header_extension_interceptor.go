// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package twcc

import (
	"errors"
	"sync/atomic"

	"github.com/pion/interceptor"
	"github.com/pion/rtp"
)

var errHeaderIsNil = errors.New("header is nil")

// HeaderExtensionInterceptorFactory is a interceptor.Factory for a HeaderExtensionInterceptor
type HeaderExtensionInterceptorFactory struct{}

// NewInterceptor constructs a new HeaderExtensionInterceptor
func (h *HeaderExtensionInterceptorFactory) NewInterceptor(_ string) (interceptor.Interceptor, error) {
	return &HeaderExtensionInterceptor{}, nil
}

// NewHeaderExtensionInterceptor returns a HeaderExtensionInterceptorFactory
func NewHeaderExtensionInterceptor() (*HeaderExtensionInterceptorFactory, error) {
	return &HeaderExtensionInterceptorFactory{}, nil
}

// HeaderExtensionInterceptor adds transport wide sequence numbers as header extension to each RTP packet
type HeaderExtensionInterceptor struct {
	interceptor.NoOp
	nextSequenceNr uint32
}

const transportCCURI = "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01"

// BindLocalStream returns a writer that adds a rtp.TransportCCExtension
// header with increasing sequence numbers to each outgoing packet.
func (h *HeaderExtensionInterceptor) BindLocalStream(info *interceptor.StreamInfo, writer interceptor.RTPWriter) interceptor.RTPWriter {
	var hdrExtID uint8
	for _, e := range info.RTPHeaderExtensions {
		if e.URI == transportCCURI {
			hdrExtID = uint8(e.ID)
			break
		}
	}
	if hdrExtID == 0 { // Don't add header extension if ID is 0, because 0 is an invalid extension ID
		return writer
	}
	return interceptor.RTPWriterFunc(func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
		sequenceNumber := atomic.AddUint32(&h.nextSequenceNr, 1) - 1

		tcc, err := (&rtp.TransportCCExtension{TransportSequence: uint16(sequenceNumber)}).Marshal()
		if err != nil {
			return 0, err
		}
		if header == nil {
			return 0, errHeaderIsNil
		}
		err = header.SetExtension(hdrExtID, tcc)
		if err != nil {
			return 0, err
		}
		return writer.Write(header, payload, attributes)
	})
}
