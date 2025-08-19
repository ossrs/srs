// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build debug
// +build debug

package stun

import "fmt"

// AttrOverflowErr occurs when len(v) > Max.
type AttrOverflowErr struct {
	Type AttrType
	Max  int
	Got  int
}

func (e AttrOverflowErr) Error() string {
	return fmt.Sprintf("incorrect length of %s attribute: %d exceeds maximum %d",
		e.Type, e.Got, e.Max,
	)
}

// AttrLengthErr means that length for attribute is invalid.
type AttrLengthErr struct {
	Attr     AttrType
	Got      int
	Expected int
}

func (e AttrLengthErr) Error() string {
	return fmt.Sprintf("incorrect length of %s attribute: got %d, expected %d",
		e.Attr,
		e.Got,
		e.Expected,
	)
}
