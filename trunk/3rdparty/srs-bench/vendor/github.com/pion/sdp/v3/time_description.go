// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package sdp

import (
	"strconv"
)

// TimeDescription describes "t=", "r=" fields of the session description
// which are used to specify the start and stop times for a session as well as
// repeat intervals and durations for the scheduled session.
type TimeDescription struct {
	// t=<start-time> <stop-time>
	// https://tools.ietf.org/html/rfc4566#section-5.9
	Timing Timing

	// r=<repeat interval> <active duration> <offsets from start-time>
	// https://tools.ietf.org/html/rfc4566#section-5.10
	RepeatTimes []RepeatTime
}

// Timing defines the "t=" field's structured representation for the start and
// stop times.
type Timing struct {
	StartTime uint64
	StopTime  uint64
}

func (t Timing) String() string {
	return stringFromMarshal(t.marshalInto, t.marshalSize)
}

func (t Timing) marshalInto(b []byte) []byte {
	b = append(strconv.AppendUint(b, t.StartTime, 10), ' ')

	return strconv.AppendUint(b, t.StopTime, 10)
}

func (t Timing) marshalSize() (size int) {
	return lenUint(t.StartTime) + 1 + lenUint(t.StopTime)
}

// RepeatTime describes the "r=" fields of the session description which
// represents the intervals and durations for repeated scheduled sessions.
type RepeatTime struct {
	Interval int64
	Duration int64
	Offsets  []int64
}

func (r RepeatTime) String() string {
	return stringFromMarshal(r.marshalInto, r.marshalSize)
}

func (r RepeatTime) marshalInto(b []byte) []byte {
	b = strconv.AppendInt(b, r.Interval, 10)
	b = append(b, ' ')
	b = strconv.AppendInt(b, r.Duration, 10)
	for _, value := range r.Offsets {
		b = append(b, ' ')
		b = strconv.AppendInt(b, value, 10)
	}

	return b
}

func (r RepeatTime) marshalSize() (size int) {
	size = lenInt(r.Interval)
	size += 1 + lenInt(r.Duration)
	for _, o := range r.Offsets {
		size += 1 + lenInt(o)
	}

	return
}
