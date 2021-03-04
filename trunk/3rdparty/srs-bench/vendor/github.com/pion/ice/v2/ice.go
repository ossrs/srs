package ice

// ConnectionState is an enum showing the state of a ICE Connection
type ConnectionState int

// List of supported States
const (
	// ConnectionStateNew ICE agent is gathering addresses
	ConnectionStateNew = iota + 1

	// ConnectionStateChecking ICE agent has been given local and remote candidates, and is attempting to find a match
	ConnectionStateChecking

	// ConnectionStateConnected ICE agent has a pairing, but is still checking other pairs
	ConnectionStateConnected

	// ConnectionStateCompleted ICE agent has finished
	ConnectionStateCompleted

	// ConnectionStateFailed ICE agent never could successfully connect
	ConnectionStateFailed

	// ConnectionStateDisconnected ICE agent connected successfully, but has entered a failed state
	ConnectionStateDisconnected

	// ConnectionStateClosed ICE agent has finished and is no longer handling requests
	ConnectionStateClosed
)

func (c ConnectionState) String() string {
	switch c {
	case ConnectionStateNew:
		return "New"
	case ConnectionStateChecking:
		return "Checking"
	case ConnectionStateConnected:
		return "Connected"
	case ConnectionStateCompleted:
		return "Completed"
	case ConnectionStateFailed:
		return "Failed"
	case ConnectionStateDisconnected:
		return "Disconnected"
	case ConnectionStateClosed:
		return "Closed"
	default:
		return "Invalid"
	}
}

// GatheringState describes the state of the candidate gathering process
type GatheringState int

const (
	// GatheringStateNew indicates candidate gatering is not yet started
	GatheringStateNew GatheringState = iota + 1

	// GatheringStateGathering indicates candidate gatering is ongoing
	GatheringStateGathering

	// GatheringStateComplete indicates candidate gatering has been completed
	GatheringStateComplete
)

func (t GatheringState) String() string {
	switch t {
	case GatheringStateNew:
		return "new"
	case GatheringStateGathering:
		return "gathering"
	case GatheringStateComplete:
		return "complete"
	default:
		return ErrUnknownType.Error()
	}
}
