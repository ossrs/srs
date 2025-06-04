// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

// DataChannelState indicates the state of a data channel.
type DataChannelState int

const (
	// DataChannelStateUnknown is the enum's zero-value.
	DataChannelStateUnknown DataChannelState = iota

	// DataChannelStateConnecting indicates that the data channel is being
	// established. This is the initial state of DataChannel, whether created
	// with CreateDataChannel, or dispatched as a part of an DataChannelEvent.
	DataChannelStateConnecting

	// DataChannelStateOpen indicates that the underlying data transport is
	// established and communication is possible.
	DataChannelStateOpen

	// DataChannelStateClosing indicates that the procedure to close down the
	// underlying data transport has started.
	DataChannelStateClosing

	// DataChannelStateClosed indicates that the underlying data transport
	// has been closed or could not be established.
	DataChannelStateClosed
)

// This is done this way because of a linter.
const (
	dataChannelStateConnectingStr = "connecting"
	dataChannelStateOpenStr       = "open"
	dataChannelStateClosingStr    = "closing"
	dataChannelStateClosedStr     = "closed"
)

func newDataChannelState(raw string) DataChannelState {
	switch raw {
	case dataChannelStateConnectingStr:
		return DataChannelStateConnecting
	case dataChannelStateOpenStr:
		return DataChannelStateOpen
	case dataChannelStateClosingStr:
		return DataChannelStateClosing
	case dataChannelStateClosedStr:
		return DataChannelStateClosed
	default:
		return DataChannelStateUnknown
	}
}

func (t DataChannelState) String() string {
	switch t {
	case DataChannelStateConnecting:
		return dataChannelStateConnectingStr
	case DataChannelStateOpen:
		return dataChannelStateOpenStr
	case DataChannelStateClosing:
		return dataChannelStateClosingStr
	case DataChannelStateClosed:
		return dataChannelStateClosedStr
	default:
		return ErrUnknownType.Error()
	}
}

// MarshalText implements encoding.TextMarshaler.
func (t DataChannelState) MarshalText() ([]byte, error) {
	return []byte(t.String()), nil
}

// UnmarshalText implements encoding.TextUnmarshaler.
func (t *DataChannelState) UnmarshalText(b []byte) error {
	*t = newDataChannelState(string(b))

	return nil
}
