// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package ntp provides conversion methods between time.Time and NTP timestamps
// stored in uint64
package ntp

import (
	"time"
)

// ToNTP converts a time.Time oboject to an uint64 NTP timestamp.
func ToNTP(t time.Time) uint64 {
	// seconds since 1st January 1900
	s := (float64(t.UnixNano()) / 1000000000) + 2208988800

	// higher 32 bits are the integer part, lower 32 bits are the fractional part
	integerPart := uint32(s)
	fractionalPart := uint32((s - float64(integerPart)) * 0xFFFFFFFF)

	return uint64(integerPart)<<32 | uint64(fractionalPart) //nolint:gosec // G115
}

// ToNTP32 converts a time.Time object to a uint32 NTP timestamp.
func ToNTP32(t time.Time) uint32 {
	return uint32(ToNTP(t) >> 16) //nolint:gosec // G115
}

// ToTime converts a uint64 NTP timestamps to a time.Time object.
func ToTime(t uint64) time.Time {
	seconds := (t & 0xFFFFFFFF00000000) >> 32
	fractional := float64(t&0x00000000FFFFFFFF) / float64(0xFFFFFFFF)
	//nolint:gosec // G115
	d := time.Duration(seconds)*time.Second + time.Duration(fractional*1e9)*time.Nanosecond

	return time.Unix(0, 0).Add(-2208988800 * time.Second).Add(d)
}

// ToTime32 converts a uint32 NTP timestamp to a time.Time object, using the
// highest 16 bit of the reference to recover the lost bits. The low 16 bits are
// not recovered.
func ToTime32(t uint32, reference time.Time) time.Time {
	referenceNTP := ToNTP(reference) & 0xFFFF000000000000
	tu64 := ((uint64(t) << 16) & 0x0000FFFFFFFF0000) | referenceNTP

	return ToTime(tu64)
}
