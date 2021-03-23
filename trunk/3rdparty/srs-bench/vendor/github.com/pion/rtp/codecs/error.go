package codecs

import "errors"

var (
	errShortPacket          = errors.New("packet is not large enough")
	errNilPacket            = errors.New("invalid nil packet")
	errTooManyPDiff         = errors.New("too many PDiff")
	errTooManySpatialLayers = errors.New("too many spatial layers")
	errUnhandledNALUType    = errors.New("NALU Type is unhandled")
)
