// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package obu

import (
	"fmt"
)

// Type represents the type of an AV1 OBU.
type Type uint8

// OBU types as defined in the AV1 specification.
// 5.3.1: https://aomediacodec.github.io/av1-spec/av1-spec.pdf#page=39
const (
	// OBUSequenceHeader av1 sequence_header_obu.
	OBUSequenceHeader = Type(1)
	// OBUTemporalDelimiter av1 temporal_delimiter_obu.
	OBUTemporalDelimiter = Type(2)
	// OBUFrameHeader av1 frame_header_obu.
	OBUFrameHeader = Type(3)
	// OBUTileGroup av1 tile_group_obu.
	OBUTileGroup = Type(4)
	// OBUMetadata av1 metadata_obu.
	OBUMetadata = Type(5)
	// OBUFrame av1 frame_obu.
	OBUFrame = Type(6)
	// OBURedundantFrameHeader av1 redundant_frame_header_obu.
	OBURedundantFrameHeader = Type(7)
	// OBUTileList av1 tile_list_obu.
	OBUTileList = Type(8)
	// OBUPadding av1 padding_obu.
	OBUPadding = Type(15)
)

//nolint:cyclop
func (o Type) String() string {
	switch o {
	case OBUSequenceHeader:
		return "OBU_SEQUENCE_HEADER"
	case OBUTemporalDelimiter:
		return "OBU_TEMPORAL_DELIMITER"
	case OBUFrameHeader:
		return "OBU_FRAME_HEADER"
	case OBUTileGroup:
		return "OBU_TILE_GROUP"
	case OBUMetadata:
		return "OBU_METADATA"
	case OBUFrame:
		return "OBU_FRAME"
	case OBURedundantFrameHeader:
		return "OBU_REDUNDANT_FRAME_HEADER"
	case OBUTileList:
		return "OBU_TILE_LIST"
	case OBUPadding:
		return "OBU_PADDING"
	default:
		return "OBU_RESERVED"
	}
}

// Header represents the header of an OBU obu_header().
// 5.3.2: https://aomediacodec.github.io/av1-spec/av1-spec.pdf#page=40
type Header struct {
	Type            Type
	ExtensionHeader *ExtensionHeader
	HasSizeField    bool
	Reserved1Bit    bool
}

// ParseOBUHeader parses an OBU header from the given data.
// 5.3.2: https://aomediacodec.github.io/av1-spec/av1-spec.pdf#page=40
/*
	obu_header() { Type
		obu_forbidden_bit     f(1)
		obu_type              f(4)
		obu_extension_flag    f(1)
		obu_has_size_field    f(1)
		obu_reserved_1bit     f(1)
		if ( obu_extension_flag == 1 )
			obu_extension_header()
		}
	}
*/
func ParseOBUHeader(data []byte) (*Header, error) {
	if len(data) < 1 {
		return nil, fmt.Errorf("%w: data is too short", ErrShortHeader)
	}

	forbiddenBit := data[0] & 0x80
	if forbiddenBit != 0 {
		return nil, fmt.Errorf("%w: forbidden bit is set", ErrInvalidOBUHeader)
	}

	obuType := Type((data[0] & 0x78) >> 3)
	obuExtensionFlag := (data[0] & 0x04) != 0
	obuHasSizeField := (data[0] & 0x02) != 0
	obuReserved1Bit := (data[0] & 0x01) != 0

	header := &Header{
		Type:         obuType,
		HasSizeField: obuHasSizeField,
		Reserved1Bit: obuReserved1Bit,
	}

	if obuExtensionFlag {
		if len(data) < 2 {
			return nil, fmt.Errorf("%w: Unexpected end of data, expected extension header", ErrShortHeader)
		}

		extensionHeader := ParseOBUExtensionHeader(data[1])
		header.ExtensionHeader = &extensionHeader
	}

	return header, nil
}

// Marshal serializes the OBU header to a byte slice.
// If the OBU has an extension header, the extension header is serialized as well.
// 5.3.2: https://aomediacodec.github.io/av1-spec/av1-spec.pdf#page=40
/*
	obu_header() { Type
		obu_forbidden_bit     f(1)
		obu_type              f(4)
		obu_extension_flag    f(1)
		obu_has_size_field    f(1)
		obu_reserved_1bit     f(1)
		if ( obu_extension_flag == 1 )
			obu_extension_header()
		}
	}
*/
func (o *Header) Marshal() []byte {
	header := make([]byte, o.Size())

	header[0] = (byte(o.Type) & 0x0f) << 3

	if o.ExtensionHeader != nil {
		header[0] |= 0x04
		header[1] = o.ExtensionHeader.Marshal()
	}

	if o.HasSizeField {
		header[0] |= 0x02
	}

	if o.Reserved1Bit {
		header[0] |= 0x01
	}

	return header
}

// Size returns the size of the OBU header in bytes.
func (o *Header) Size() int {
	size := 1
	if o.ExtensionHeader != nil {
		size++
	}

	return size
}

// ExtensionHeader represents an OBU extension header obu_extension_header().
// 5.3.3 https://aomediacodec.github.io/av1-spec/av1-spec.pdf#page=40
type ExtensionHeader struct {
	TemporalID    uint8
	SpatialID     uint8
	Reserved3Bits uint8
}

// ParseOBUExtensionHeader parses an OBU extension header from the given data.
// 5.3.3 https://aomediacodec.github.io/av1-spec/av1-spec.pdf#page=40
/*
	obu_extension_header() { Type
		temporal_id                      f(3)
		spatial_id                       f(2)
		extension_header_reserved_3bits  f(3)
	}
*/
func ParseOBUExtensionHeader(headerData byte) ExtensionHeader {
	return ExtensionHeader{
		TemporalID:    headerData >> 5,
		SpatialID:     (headerData >> 3) & 0x03,
		Reserved3Bits: headerData & 0x07,
	}
}

// Marshal serializes the OBU extension header to a byte slice.
// 5.3.3 https://aomediacodec.github.io/av1-spec/av1-spec.pdf#page=40
/*
	obu_extension_header() { Type
		temporal_id                      f(3)
		spatial_id                       f(2)
		extension_header_reserved_3bits  f(3)
	}
*/
func (o *ExtensionHeader) Marshal() byte {
	return (o.TemporalID << 5) | ((o.SpatialID & 0x3) << 3) | (o.Reserved3Bits & 0x07)
}

// OBU represents an AV1 OBU.
// 5.1 https://aomediacodec.github.io/av1-spec/av1-spec.pdf#page=39
type OBU struct {
	Header  Header
	Payload []byte
}

// Marshal serializes the OBU to low-overhead bitstream format.
// 5.2 https://aomediacodec.github.io/av1-spec/av1-spec.pdf#page=40
func (o *OBU) Marshal() []byte {
	buffer := o.Header.Marshal()

	if o.Header.HasSizeField {
		buffer = append(buffer, WriteToLeb128(uint(len(o.Payload)))...)
	}

	return append(buffer, o.Payload...)
}
