// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package stun

import "errors"

// DecodeErr records an error and place when it is occurred.
//
//nolint:errname
type DecodeErr struct {
	Place   DecodeErrPlace
	Message string
}

// IsInvalidCookie returns true if error means that magic cookie
// value is invalid.
func (e DecodeErr) IsInvalidCookie() bool {
	return e.Place == DecodeErrPlace{"message", "cookie"}
}

// IsPlaceParent reports if error place parent is p.
func (e DecodeErr) IsPlaceParent(p string) bool {
	return e.Place.Parent == p
}

// IsPlaceChildren reports if error place children is c.
func (e DecodeErr) IsPlaceChildren(c string) bool {
	return e.Place.Children == c
}

// IsPlace reports if error place is p.
func (e DecodeErr) IsPlace(p DecodeErrPlace) bool {
	return e.Place == p
}

// DecodeErrPlace records a place where error is occurred.
type DecodeErrPlace struct {
	Parent   string
	Children string
}

func (p DecodeErrPlace) String() string {
	return p.Parent + "/" + p.Children
}

func (e DecodeErr) Error() string {
	return "BadFormat for " + e.Place.String() + ": " + e.Message
}

func newDecodeErr(parent, children, message string) *DecodeErr {
	return &DecodeErr{
		Place:   DecodeErrPlace{Parent: parent, Children: children},
		Message: message,
	}
}

func newAttrDecodeErr(children, message string) *DecodeErr {
	return newDecodeErr("attribute", children, message)
}

// ErrAttributeSizeInvalid means that decoded attribute size is invalid.
var ErrAttributeSizeInvalid = errors.New("attribute size is invalid")

// ErrAttributeSizeOverflow means that decoded attribute size is too big.
var ErrAttributeSizeOverflow = errors.New("attribute size overflow")
