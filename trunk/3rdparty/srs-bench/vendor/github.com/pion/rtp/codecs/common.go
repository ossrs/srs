// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package codecs

func minInt(a, b int) int {
	if a < b {
		return a
	}

	return b
}

// audioDepacketizer is a mixin for audio codec depacketizers.
type audioDepacketizer struct{}

func (d *audioDepacketizer) IsPartitionTail(_ bool, _ []byte) bool {
	return true
}

func (d *audioDepacketizer) IsPartitionHead(_ []byte) bool {
	return true
}

// videoDepacketizer is a mixin for video codec depacketizers.
type videoDepacketizer struct {
	zeroAllocation bool
}

func (d *videoDepacketizer) IsPartitionTail(marker bool, _ []byte) bool {
	return marker
}

// SetZeroAllocation enables Zero Allocation mode for the depacketizer
// By default the Depacketizers will allocate as they parse. These allocations
// are needed for Metadata and other optional values. If you don't need this information
// enabling SetZeroAllocation gives you higher performance at a reduced feature set.
func (d *videoDepacketizer) SetZeroAllocation(zeroAllocation bool) {
	d.zeroAllocation = zeroAllocation
}
