// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package datachannel

import "errors"

var (
	// ErrDataChannelMessageTooShort means that the data isn't long enough to be a valid DataChannel message
	ErrDataChannelMessageTooShort = errors.New("DataChannel message is not long enough to determine type")

	// ErrInvalidPayloadProtocolIdentifier means that we got a DataChannel messages with a Payload Protocol Identifier
	// we don't know how to handle
	ErrInvalidPayloadProtocolIdentifier = errors.New("DataChannel message Payload Protocol Identifier is value we can't handle")

	// ErrInvalidChannelType means that the remote requested a channel type that we don't support
	ErrInvalidChannelType = errors.New("invalid Channel Type")

	// ErrInvalidMessageType is returned when a DataChannel Message has a type we don't support
	ErrInvalidMessageType = errors.New("invalid Message Type")

	// ErrExpectedAndActualLengthMismatch is when the declared length and actual length don't match
	ErrExpectedAndActualLengthMismatch = errors.New("expected and actual length do not match")

	// ErrUnexpectedDataChannelType is when a message type does not match the expected type
	ErrUnexpectedDataChannelType = errors.New("expected and actual message type does not match")
)
