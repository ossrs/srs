// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package frame is deprecated.
package frame

import (
	"github.com/pion/rtp/codecs/av1/frame"
)

// AV1 represents a collection of OBUs given a stream of AV1 Packets.
// Each AV1 RTP Packet is a collection of OBU Elements. Each OBU Element may be a full OBU, or just a fragment of one.
// AV1 provides the tools to construct a collection of OBUs from a collection of OBU Elements. This structure
// contains an internal cache and should be used for the entire RTP Stream.
//
// Deprecated: moved into codecs/av1/frame.
type AV1 = frame.AV1
