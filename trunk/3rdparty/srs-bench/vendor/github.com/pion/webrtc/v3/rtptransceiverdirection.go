package webrtc

// RTPTransceiverDirection indicates the direction of the RTPTransceiver.
type RTPTransceiverDirection int

const (
	// RTPTransceiverDirectionSendrecv indicates the RTPSender will offer
	// to send RTP and RTPReceiver the will offer to receive RTP.
	RTPTransceiverDirectionSendrecv RTPTransceiverDirection = iota + 1

	// RTPTransceiverDirectionSendonly indicates the RTPSender will offer
	// to send RTP.
	RTPTransceiverDirectionSendonly

	// RTPTransceiverDirectionRecvonly indicates the RTPReceiver the will
	// offer to receive RTP.
	RTPTransceiverDirectionRecvonly

	// RTPTransceiverDirectionInactive indicates the RTPSender won't offer
	// to send RTP and RTPReceiver the won't offer to receive RTP.
	RTPTransceiverDirectionInactive
)

// This is done this way because of a linter.
const (
	rtpTransceiverDirectionSendrecvStr = "sendrecv"
	rtpTransceiverDirectionSendonlyStr = "sendonly"
	rtpTransceiverDirectionRecvonlyStr = "recvonly"
	rtpTransceiverDirectionInactiveStr = "inactive"
)

// NewRTPTransceiverDirection defines a procedure for creating a new
// RTPTransceiverDirection from a raw string naming the transceiver direction.
func NewRTPTransceiverDirection(raw string) RTPTransceiverDirection {
	switch raw {
	case rtpTransceiverDirectionSendrecvStr:
		return RTPTransceiverDirectionSendrecv
	case rtpTransceiverDirectionSendonlyStr:
		return RTPTransceiverDirectionSendonly
	case rtpTransceiverDirectionRecvonlyStr:
		return RTPTransceiverDirectionRecvonly
	case rtpTransceiverDirectionInactiveStr:
		return RTPTransceiverDirectionInactive
	default:
		return RTPTransceiverDirection(Unknown)
	}
}

func (t RTPTransceiverDirection) String() string {
	switch t {
	case RTPTransceiverDirectionSendrecv:
		return rtpTransceiverDirectionSendrecvStr
	case RTPTransceiverDirectionSendonly:
		return rtpTransceiverDirectionSendonlyStr
	case RTPTransceiverDirectionRecvonly:
		return rtpTransceiverDirectionRecvonlyStr
	case RTPTransceiverDirectionInactive:
		return rtpTransceiverDirectionInactiveStr
	default:
		return ErrUnknownType.Error()
	}
}

// Revers indicate the opposite direction
func (t RTPTransceiverDirection) Revers() RTPTransceiverDirection {
	switch t {
	case RTPTransceiverDirectionSendonly:
		return RTPTransceiverDirectionRecvonly
	case RTPTransceiverDirectionRecvonly:
		return RTPTransceiverDirectionSendonly
	default:
		return t
	}
}

func haveRTPTransceiverDirectionIntersection(haystack []RTPTransceiverDirection, needle []RTPTransceiverDirection) bool {
	for _, n := range needle {
		for _, h := range haystack {
			if n == h {
				return true
			}
		}
	}
	return false
}
