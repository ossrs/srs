package webrtc

const (
	// Unknown defines default public constant to use for "enum" like struct
	// comparisons when no value was defined.
	Unknown    = iota
	unknownStr = "unknown"
	ssrcStr    = "ssrc"

	// Equal to UDP MTU
	receiveMTU = 1460

	// simulcastProbeCount is the amount of RTP Packets
	// that handleUndeclaredSSRC will read and try to dispatch from
	// mid and rid values
	simulcastProbeCount = 10

	// simulcastMaxProbeRoutines is how many active routines can be used to probe
	// If the total amount of incoming SSRCes exceeds this new requests will be ignored
	simulcastMaxProbeRoutines = 25

	mediaSectionApplication = "application"

	rtpOutboundMTU = 1200
)
