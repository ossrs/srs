package mpeg4audio

// ObjectType is a MPEG-4 Audio object type.
// Specification: ISO 14496-3, Table 1.17
type ObjectType int

// supported types.
const (
	ObjectTypeAACLC ObjectType = 2
	ObjectTypeSBR   ObjectType = 5
	ObjectTypePS    ObjectType = 29
)
