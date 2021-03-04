package webrtc

// RTPDecodingParameters provides information relating to both encoding and decoding.
// This is a subset of the RFC since Pion WebRTC doesn't implement decoding itself
// http://draft.ortc.org/#dom-rtcrtpdecodingparameters
type RTPDecodingParameters struct {
	RTPCodingParameters
}
