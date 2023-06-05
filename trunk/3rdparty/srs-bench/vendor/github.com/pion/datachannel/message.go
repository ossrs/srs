package datachannel

import (
	"fmt"
)

// message is a parsed DataChannel message
type message interface {
	Marshal() ([]byte, error)
	Unmarshal([]byte) error
}

// messageType is the first byte in a DataChannel message that specifies type
type messageType byte

// DataChannel Message Types
const (
	dataChannelAck  messageType = 0x02
	dataChannelOpen messageType = 0x03
)

func (t messageType) String() string {
	switch t {
	case dataChannelAck:
		return "DataChannelAck"
	case dataChannelOpen:
		return "DataChannelOpen"
	default:
		return fmt.Sprintf("Unknown MessageType: %d", t)
	}
}

// parse accepts raw input and returns a DataChannel message
func parse(raw []byte) (message, error) {
	if len(raw) == 0 {
		return nil, ErrDataChannelMessageTooShort
	}

	var msg message
	switch messageType(raw[0]) {
	case dataChannelOpen:
		msg = &channelOpen{}
	case dataChannelAck:
		msg = &channelAck{}
	default:
		return nil, fmt.Errorf("%w %v", ErrInvalidMessageType, messageType(raw[0]))
	}

	if err := msg.Unmarshal(raw); err != nil {
		return nil, err
	}

	return msg, nil
}

// parseExpectDataChannelOpen parses a DataChannelOpen message
// or throws an error
func parseExpectDataChannelOpen(raw []byte) (*channelOpen, error) {
	if len(raw) == 0 {
		return nil, ErrDataChannelMessageTooShort
	}

	if actualTyp := messageType(raw[0]); actualTyp != dataChannelOpen {
		return nil, fmt.Errorf("%w expected(%s) actual(%s)", ErrUnexpectedDataChannelType, actualTyp, dataChannelOpen)
	}

	msg := &channelOpen{}
	if err := msg.Unmarshal(raw); err != nil {
		return nil, err
	}

	return msg, nil
}
