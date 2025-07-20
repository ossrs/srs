// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package datachannel

import (
	"encoding/binary"
	"fmt"
)

/*
channelOpen represents a DATA_CHANNEL_OPEN Message

	0                   1                   2                   3
	0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1

+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Message Type |  Channel Type |            Priority           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Reliability Parameter                      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|         Label Length          |       Protocol Length         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                             Label                             |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                            Protocol                           |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
type channelOpen struct {
	ChannelType          ChannelType
	Priority             uint16
	ReliabilityParameter uint32

	Label    []byte
	Protocol []byte
}

const (
	channelOpenHeaderLength = 12
)

// ChannelType determines the reliability of the WebRTC DataChannel
type ChannelType byte

// ChannelType enums
const (
	// ChannelTypeReliable determines the Data Channel provides a
	// reliable in-order bi-directional communication.
	ChannelTypeReliable ChannelType = 0x00
	// ChannelTypeReliableUnordered determines the Data Channel
	// provides a reliable unordered bi-directional communication.
	ChannelTypeReliableUnordered ChannelType = 0x80
	// ChannelTypePartialReliableRexmit determines the Data Channel
	// provides a partially-reliable in-order bi-directional communication.
	// User messages will not be retransmitted more times than specified in the Reliability Parameter.
	ChannelTypePartialReliableRexmit ChannelType = 0x01
	// ChannelTypePartialReliableRexmitUnordered determines
	//  the Data Channel provides a partial reliable unordered bi-directional communication.
	// User messages will not be retransmitted more times than specified in the Reliability Parameter.
	ChannelTypePartialReliableRexmitUnordered ChannelType = 0x81
	// ChannelTypePartialReliableTimed determines the Data Channel
	// provides a partial reliable in-order bi-directional communication.
	// User messages might not be transmitted or retransmitted after
	// a specified life-time given in milli- seconds in the Reliability Parameter.
	// This life-time starts when providing the user message to the protocol stack.
	ChannelTypePartialReliableTimed ChannelType = 0x02
	// The Data Channel provides a partial reliable unordered bi-directional
	// communication.  User messages might not be transmitted or retransmitted
	// after a specified life-time given in milli- seconds in the Reliability Parameter.
	// This life-time starts when providing the user message to the protocol stack.
	ChannelTypePartialReliableTimedUnordered ChannelType = 0x82
)

func (c ChannelType) String() string {
	switch c {
	case ChannelTypeReliable:
	case ChannelTypeReliableUnordered:
		return "ReliableUnordered"
	case ChannelTypePartialReliableRexmit:
		return "PartialReliableRexmit"
	case ChannelTypePartialReliableRexmitUnordered:
		return "PartialReliableRexmitUnordered"
	case ChannelTypePartialReliableTimed:
		return "PartialReliableTimed"
	case ChannelTypePartialReliableTimedUnordered:
		return "PartialReliableTimedUnordered"
	}
	return "Unknown"
}

// ChannelPriority enums
const (
	ChannelPriorityBelowNormal uint16 = 128
	ChannelPriorityNormal      uint16 = 256
	ChannelPriorityHigh        uint16 = 512
	ChannelPriorityExtraHigh   uint16 = 1024
)

// Marshal returns raw bytes for the given message
func (c *channelOpen) Marshal() ([]byte, error) {
	labelLength := len(c.Label)
	protocolLength := len(c.Protocol)

	totalLen := channelOpenHeaderLength + labelLength + protocolLength
	raw := make([]byte, totalLen)

	raw[0] = uint8(dataChannelOpen)
	raw[1] = byte(c.ChannelType)
	binary.BigEndian.PutUint16(raw[2:], c.Priority)
	binary.BigEndian.PutUint32(raw[4:], c.ReliabilityParameter)
	binary.BigEndian.PutUint16(raw[8:], uint16(labelLength))
	binary.BigEndian.PutUint16(raw[10:], uint16(protocolLength))
	endLabel := channelOpenHeaderLength + labelLength
	copy(raw[channelOpenHeaderLength:endLabel], c.Label)
	copy(raw[endLabel:endLabel+protocolLength], c.Protocol)

	return raw, nil
}

// Unmarshal populates the struct with the given raw data
func (c *channelOpen) Unmarshal(raw []byte) error {
	if len(raw) < channelOpenHeaderLength {
		return fmt.Errorf("%w expected(%d) actual(%d)", ErrExpectedAndActualLengthMismatch, channelOpenHeaderLength, len(raw))
	}
	c.ChannelType = ChannelType(raw[1])
	c.Priority = binary.BigEndian.Uint16(raw[2:])
	c.ReliabilityParameter = binary.BigEndian.Uint32(raw[4:])

	labelLength := binary.BigEndian.Uint16(raw[8:])
	protocolLength := binary.BigEndian.Uint16(raw[10:])

	if expectedLen := channelOpenHeaderLength + int(labelLength) + int(protocolLength); len(raw) != expectedLen {
		return fmt.Errorf("%w expected(%d) actual(%d)", ErrExpectedAndActualLengthMismatch, expectedLen, len(raw))
	}

	c.Label = raw[channelOpenHeaderLength : channelOpenHeaderLength+labelLength]
	c.Protocol = raw[channelOpenHeaderLength+labelLength : channelOpenHeaderLength+labelLength+protocolLength]
	return nil
}

func (c channelOpen) String() string {
	return fmt.Sprintf("Open ChannelType(%s) Priority(%v) ReliabilityParameter(%d) Label(%s) Protocol(%s)", c.ChannelType, c.Priority, c.ReliabilityParameter, string(c.Label), string(c.Protocol))
}
