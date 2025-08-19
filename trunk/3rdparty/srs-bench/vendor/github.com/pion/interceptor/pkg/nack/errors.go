// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package nack

import "github.com/pion/interceptor/internal/rtpbuffer"

// ErrInvalidSize is returned by newReceiveLog/newRTPBuffer, when an incorrect buffer size is supplied.
var ErrInvalidSize = rtpbuffer.ErrInvalidSize
