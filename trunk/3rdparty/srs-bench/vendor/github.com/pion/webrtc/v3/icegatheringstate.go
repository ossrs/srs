package webrtc

// ICEGatheringState describes the state of the candidate gathering process.
type ICEGatheringState int

const (
	// ICEGatheringStateNew indicates that any of the ICETransports are
	// in the "new" gathering state and none of the transports are in the
	// "gathering" state, or there are no transports.
	ICEGatheringStateNew ICEGatheringState = iota + 1

	// ICEGatheringStateGathering indicates that any of the ICETransports
	// are in the "gathering" state.
	ICEGatheringStateGathering

	// ICEGatheringStateComplete indicates that at least one ICETransport
	// exists, and all ICETransports are in the "completed" gathering state.
	ICEGatheringStateComplete
)

// This is done this way because of a linter.
const (
	iceGatheringStateNewStr       = "new"
	iceGatheringStateGatheringStr = "gathering"
	iceGatheringStateCompleteStr  = "complete"
)

// NewICEGatheringState takes a string and converts it to ICEGatheringState
func NewICEGatheringState(raw string) ICEGatheringState {
	switch raw {
	case iceGatheringStateNewStr:
		return ICEGatheringStateNew
	case iceGatheringStateGatheringStr:
		return ICEGatheringStateGathering
	case iceGatheringStateCompleteStr:
		return ICEGatheringStateComplete
	default:
		return ICEGatheringState(Unknown)
	}
}

func (t ICEGatheringState) String() string {
	switch t {
	case ICEGatheringStateNew:
		return iceGatheringStateNewStr
	case ICEGatheringStateGathering:
		return iceGatheringStateGatheringStr
	case ICEGatheringStateComplete:
		return iceGatheringStateCompleteStr
	default:
		return ErrUnknownType.Error()
	}
}
