package interceptor

// RTPHeaderExtension represents a negotiated RFC5285 RTP header extension.
type RTPHeaderExtension struct {
	URI string
	ID  int
}

// StreamInfo is the Context passed when a StreamLocal or StreamRemote has been Binded or Unbinded
type StreamInfo struct {
	ID                  string
	Attributes          Attributes
	SSRC                uint32
	PayloadType         uint8
	RTPHeaderExtensions []RTPHeaderExtension
	MimeType            string
	ClockRate           uint32
	Channels            uint16
	SDPFmtpLine         string
	RTCPFeedback        []RTCPFeedback
}

// RTCPFeedback signals the connection to use additional RTCP packet types.
// https://draft.ortc.org/#dom-rtcrtcpfeedback
type RTCPFeedback struct {
	// Type is the type of feedback.
	// see: https://draft.ortc.org/#dom-rtcrtcpfeedback
	// valid: ack, ccm, nack, goog-remb, transport-cc
	Type string

	// The parameter value depends on the type.
	// For example, type="nack" parameter="pli" will send Picture Loss Indicator packets.
	Parameter string
}
