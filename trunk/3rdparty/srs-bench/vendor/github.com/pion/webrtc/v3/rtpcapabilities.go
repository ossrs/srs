package webrtc

// RTPCapabilities represents the capabilities of a transceiver
//
// https://w3c.github.io/webrtc-pc/#rtcrtpcapabilities
type RTPCapabilities struct {
	Codecs           []RTPCodecCapability
	HeaderExtensions []RTPHeaderExtensionCapability
}
