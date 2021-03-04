package webrtc

// ICEComponent describes if the ice transport is used for RTP
// (or RTCP multiplexing).
type ICEComponent int

const (
	// ICEComponentRTP indicates that the ICE Transport is used for RTP (or
	// RTCP multiplexing), as defined in
	// https://tools.ietf.org/html/rfc5245#section-4.1.1.1. Protocols
	// multiplexed with RTP (e.g. data channel) share its component ID. This
	// represents the component-id value 1 when encoded in candidate-attribute.
	ICEComponentRTP ICEComponent = iota + 1

	// ICEComponentRTCP indicates that the ICE Transport is used for RTCP as
	// defined by https://tools.ietf.org/html/rfc5245#section-4.1.1.1. This
	// represents the component-id value 2 when encoded in candidate-attribute.
	ICEComponentRTCP
)

// This is done this way because of a linter.
const (
	iceComponentRTPStr  = "rtp"
	iceComponentRTCPStr = "rtcp"
)

func newICEComponent(raw string) ICEComponent {
	switch raw {
	case iceComponentRTPStr:
		return ICEComponentRTP
	case iceComponentRTCPStr:
		return ICEComponentRTCP
	default:
		return ICEComponent(Unknown)
	}
}

func (t ICEComponent) String() string {
	switch t {
	case ICEComponentRTP:
		return iceComponentRTPStr
	case ICEComponentRTCP:
		return iceComponentRTCPStr
	default:
		return ErrUnknownType.Error()
	}
}
