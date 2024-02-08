package rtp

import (
	"encoding/binary"
	"fmt"
	"io"
)

const (
	headerExtensionProfileOneByte = 0xBEDE
	headerExtensionProfileTwoByte = 0x1000
	headerExtensionIDReserved     = 0xF
)

// HeaderExtension represents an RTP extension header.
type HeaderExtension interface {
	Set(id uint8, payload []byte) error
	GetIDs() []uint8
	Get(id uint8) []byte
	Del(id uint8) error

	Unmarshal(buf []byte) (int, error)
	Marshal() ([]byte, error)
	MarshalTo(buf []byte) (int, error)
	MarshalSize() int
}

// OneByteHeaderExtension is an RFC8285 one-byte header extension.
type OneByteHeaderExtension struct {
	payload []byte
}

// Set sets the extension payload for the specified ID.
func (e *OneByteHeaderExtension) Set(id uint8, buf []byte) error {
	if id < 1 || id > 14 {
		return fmt.Errorf("%w actual(%d)", errRFC8285OneByteHeaderIDRange, id)
	}
	if len(buf) > 16 {
		return fmt.Errorf("%w actual(%d)", errRFC8285OneByteHeaderSize, len(buf))
	}

	for n := 4; n < len(e.payload); {
		if e.payload[n] == 0x00 { // padding
			n++
			continue
		}

		extid := e.payload[n] >> 4
		len := int(e.payload[n]&^0xF0 + 1)
		n++

		if extid == id {
			e.payload = append(e.payload[:n+1], append(buf, e.payload[n+1+len:]...)...)
			return nil
		}
		n += len
	}
	e.payload = append(e.payload, (id<<4 | uint8(len(buf)-1)))
	e.payload = append(e.payload, buf...)
	binary.BigEndian.PutUint16(e.payload[2:4], binary.BigEndian.Uint16(e.payload[2:4])+1)
	return nil
}

// GetIDs returns the available IDs.
func (e *OneByteHeaderExtension) GetIDs() []uint8 {
	ids := make([]uint8, 0, binary.BigEndian.Uint16(e.payload[2:4]))
	for n := 4; n < len(e.payload); {
		if e.payload[n] == 0x00 { // padding
			n++
			continue
		}

		extid := e.payload[n] >> 4
		len := int(e.payload[n]&^0xF0 + 1)
		n++

		if extid == headerExtensionIDReserved {
			break
		}

		ids = append(ids, extid)
		n += len
	}
	return ids
}

// Get returns the payload of the extension with the given ID.
func (e *OneByteHeaderExtension) Get(id uint8) []byte {
	for n := 4; n < len(e.payload); {
		if e.payload[n] == 0x00 { // padding
			n++
			continue
		}

		extid := e.payload[n] >> 4
		len := int(e.payload[n]&^0xF0 + 1)
		n++

		if extid == id {
			return e.payload[n : n+len]
		}
		n += len
	}
	return nil
}

// Del deletes the extension with the specified ID.
func (e *OneByteHeaderExtension) Del(id uint8) error {
	for n := 4; n < len(e.payload); {
		if e.payload[n] == 0x00 { // padding
			n++
			continue
		}

		extid := e.payload[n] >> 4
		len := int(e.payload[n]&^0xF0 + 1)

		if extid == id {
			e.payload = append(e.payload[:n], e.payload[n+1+len:]...)
			return nil
		}
		n += len + 1
	}
	return errHeaderExtensionNotFound
}

// Unmarshal parses the extension payload.
func (e *OneByteHeaderExtension) Unmarshal(buf []byte) (int, error) {
	profile := binary.BigEndian.Uint16(buf[0:2])
	if profile != headerExtensionProfileOneByte {
		return 0, fmt.Errorf("%w actual(%x)", errHeaderExtensionNotFound, buf[0:2])
	}
	e.payload = buf
	return len(buf), nil
}

// Marshal returns the extension payload.
func (e OneByteHeaderExtension) Marshal() ([]byte, error) {
	return e.payload, nil
}

// MarshalTo writes the extension payload to the given buffer.
func (e OneByteHeaderExtension) MarshalTo(buf []byte) (int, error) {
	size := e.MarshalSize()
	if size > len(buf) {
		return 0, io.ErrShortBuffer
	}
	return copy(buf, e.payload), nil
}

// MarshalSize returns the size of the extension payload.
func (e OneByteHeaderExtension) MarshalSize() int {
	return len(e.payload)
}

// TwoByteHeaderExtension is an RFC8285 two-byte header extension.
type TwoByteHeaderExtension struct {
	payload []byte
}

// Set sets the extension payload for the specified ID.
func (e *TwoByteHeaderExtension) Set(id uint8, buf []byte) error {
	if id < 1 || id > 255 {
		return fmt.Errorf("%w actual(%d)", errRFC8285TwoByteHeaderIDRange, id)
	}
	if len(buf) > 255 {
		return fmt.Errorf("%w actual(%d)", errRFC8285TwoByteHeaderSize, len(buf))
	}

	for n := 4; n < len(e.payload); {
		if e.payload[n] == 0x00 { // padding
			n++
			continue
		}

		extid := e.payload[n]
		n++

		len := int(e.payload[n])
		n++

		if extid == id {
			e.payload = append(e.payload[:n+2], append(buf, e.payload[n+2+len:]...)...)
			return nil
		}
		n += len
	}
	e.payload = append(e.payload, id, uint8(len(buf)))
	e.payload = append(e.payload, buf...)
	binary.BigEndian.PutUint16(e.payload[2:4], binary.BigEndian.Uint16(e.payload[2:4])+1)
	return nil
}

// GetIDs returns the available IDs.
func (e *TwoByteHeaderExtension) GetIDs() []uint8 {
	ids := make([]uint8, 0, binary.BigEndian.Uint16(e.payload[2:4]))
	for n := 4; n < len(e.payload); {
		if e.payload[n] == 0x00 { // padding
			n++
			continue
		}

		extid := e.payload[n]
		n++

		len := int(e.payload[n])
		n++

		ids = append(ids, extid)
		n += len
	}
	return ids
}

// Get returns the payload of the extension with the given ID.
func (e *TwoByteHeaderExtension) Get(id uint8) []byte {
	for n := 4; n < len(e.payload); {
		if e.payload[n] == 0x00 { // padding
			n++
			continue
		}

		extid := e.payload[n]
		n++

		len := int(e.payload[n])
		n++

		if extid == id {
			return e.payload[n : n+len]
		}
		n += len
	}
	return nil
}

// Del deletes the extension with the specified ID.
func (e *TwoByteHeaderExtension) Del(id uint8) error {
	for n := 4; n < len(e.payload); {
		if e.payload[n] == 0x00 { // padding
			n++
			continue
		}

		extid := e.payload[n]

		len := int(e.payload[n+1])

		if extid == id {
			e.payload = append(e.payload[:n], e.payload[n+2+len:]...)
			return nil
		}
		n += len + 2
	}
	return errHeaderExtensionNotFound
}

// Unmarshal parses the extension payload.
func (e *TwoByteHeaderExtension) Unmarshal(buf []byte) (int, error) {
	profile := binary.BigEndian.Uint16(buf[0:2])
	if profile != headerExtensionProfileTwoByte {
		return 0, fmt.Errorf("%w actual(%x)", errHeaderExtensionNotFound, buf[0:2])
	}
	e.payload = buf
	return len(buf), nil
}

// Marshal returns the extension payload.
func (e TwoByteHeaderExtension) Marshal() ([]byte, error) {
	return e.payload, nil
}

// MarshalTo marshals the extension to the given buffer.
func (e TwoByteHeaderExtension) MarshalTo(buf []byte) (int, error) {
	size := e.MarshalSize()
	if size > len(buf) {
		return 0, io.ErrShortBuffer
	}
	return copy(buf, e.payload), nil
}

// MarshalSize returns the size of the extension payload.
func (e TwoByteHeaderExtension) MarshalSize() int {
	return len(e.payload)
}

// RawExtension represents an RFC3550 header extension.
type RawExtension struct {
	payload []byte
}

// Set sets the extension payload for the specified ID.
func (e *RawExtension) Set(id uint8, payload []byte) error {
	if id != 0 {
		return fmt.Errorf("%w actual(%d)", errRFC3550HeaderIDRange, id)
	}
	e.payload = payload
	return nil
}

// GetIDs returns the available IDs.
func (e *RawExtension) GetIDs() []uint8 {
	return []uint8{0}
}

// Get returns the payload of the extension with the given ID.
func (e *RawExtension) Get(id uint8) []byte {
	if id == 0 {
		return e.payload
	}
	return nil
}

// Del deletes the extension with the specified ID.
func (e *RawExtension) Del(id uint8) error {
	if id == 0 {
		e.payload = nil
		return nil
	}
	return fmt.Errorf("%w actual(%d)", errRFC3550HeaderIDRange, id)
}

// Unmarshal parses the extension from the given buffer.
func (e *RawExtension) Unmarshal(buf []byte) (int, error) {
	profile := binary.BigEndian.Uint16(buf[0:2])
	if profile == headerExtensionProfileOneByte || profile == headerExtensionProfileTwoByte {
		return 0, fmt.Errorf("%w actual(%x)", errHeaderExtensionNotFound, buf[0:2])
	}
	e.payload = buf
	return len(buf), nil
}

// Marshal returns the raw extension payload.
func (e RawExtension) Marshal() ([]byte, error) {
	return e.payload, nil
}

// MarshalTo marshals the extension to the given buffer.
func (e RawExtension) MarshalTo(buf []byte) (int, error) {
	size := e.MarshalSize()
	if size > len(buf) {
		return 0, io.ErrShortBuffer
	}
	return copy(buf, e.payload), nil
}

// MarshalSize returns the size of the extension when marshaled.
func (e RawExtension) MarshalSize() int {
	return len(e.payload)
}
