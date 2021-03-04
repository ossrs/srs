package rtp

// PartitionHeadChecker is the interface that checks whether the packet is keyframe or not
type PartitionHeadChecker interface {
	IsPartitionHead([]byte) bool
}
