package codecs

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

// audioDepacketizer is a mixin for audio codec depacketizers
type audioDepacketizer struct{}

func (d *audioDepacketizer) IsPartitionTail(marker bool, payload []byte) bool {
	return true
}

func (d *audioDepacketizer) IsPartitionHead(payload []byte) bool {
	return true
}

// videoDepacketizer is a mixin for video codec depacketizers
type videoDepacketizer struct{}

func (d *videoDepacketizer) IsPartitionTail(marker bool, payload []byte) bool {
	return marker
}
