package rtp

import (
	"errors"
)

var (
	errHeaderSizeInsufficient             = errors.New("RTP header size insufficient")
	errHeaderSizeInsufficientForExtension = errors.New("RTP header size insufficient for extension")
	errTooSmall                           = errors.New("buffer too small")
	errHeaderExtensionsNotEnabled         = errors.New("h.Extension not enabled")
	errHeaderExtensionNotFound            = errors.New("extension not found")

	errRFC8285OneByteHeaderIDRange = errors.New("header extension id must be between 1 and 14 for RFC 5285 one byte extensions")
	errRFC8285OneByteHeaderSize    = errors.New("header extension payload must be 16bytes or less for RFC 5285 one byte extensions")

	errRFC8285TwoByteHeaderIDRange = errors.New("header extension id must be between 1 and 255 for RFC 5285 two byte extensions")
	errRFC8285TwoByteHeaderSize    = errors.New("header extension payload must be 255bytes or less for RFC 5285 two byte extensions")

	errRFC3550HeaderIDRange = errors.New("header extension id must be 0 for non-RFC 5285 extensions")
)
