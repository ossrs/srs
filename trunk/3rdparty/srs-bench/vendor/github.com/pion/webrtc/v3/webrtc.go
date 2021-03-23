// Package webrtc implements the WebRTC 1.0 as defined in W3C WebRTC specification document.
package webrtc

// SSRC represents a synchronization source
// A synchronization source is a randomly chosen
// value meant to be globally unique within a particular
// RTP session. Used to identify a single stream of media.
//
// https://tools.ietf.org/html/rfc3550#section-3
type SSRC uint32

// PayloadType identifies the format of the RTP payload and determines
// its interpretation by the application. Each codec in a RTP Session
// will have a different PayloadType
//
// https://tools.ietf.org/html/rfc3550#section-3
type PayloadType uint8
