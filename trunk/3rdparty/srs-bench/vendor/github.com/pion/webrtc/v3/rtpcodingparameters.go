package webrtc

// RTPCodingParameters provides information relating to both encoding and decoding.
// This is a subset of the RFC since Pion WebRTC doesn't implement encoding/decoding itself
// http://draft.ortc.org/#dom-rtcrtpcodingparameters
type RTPCodingParameters struct {
	RID         string      `json:"rid"`
	SSRC        SSRC        `json:"ssrc"`
	PayloadType PayloadType `json:"payloadType"`
}
