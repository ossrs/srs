// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package obu

import "errors"

var (
	// ErrInvalidOBUHeader is returned when an OBU header has forbidden bits set.
	ErrInvalidOBUHeader = errors.New("invalid OBU header")
	// ErrShortHeader is returned when an OBU header is not large enough.
	// This can happen when an extension header is expected but not present.
	ErrShortHeader = errors.New("OBU header is not large enough")
)
