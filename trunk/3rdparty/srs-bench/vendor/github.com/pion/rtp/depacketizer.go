package rtp

// Depacketizer depacketizes a RTP payload, removing any RTP specific data from the payload
type Depacketizer interface {
	Unmarshal(packet []byte) ([]byte, error)
	// Checks if the packet is at the beginning of a partition.  This
	// should return false if the result could not be determined, in
	// which case the caller will detect timestamp discontinuities.
	IsPartitionHead(payload []byte) bool
	// Checks if the packet is at the end of a partition.  This should
	// return false if the result could not be determined.
	IsPartitionTail(marker bool, payload []byte) bool
}
