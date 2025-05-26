// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package rtp

// PartitionHeadChecker is the interface that checks whether the packet is keyframe or not.
type PartitionHeadChecker interface {
	IsPartitionHead([]byte) bool
}
