package rtcp

import (
	"encoding/binary"
	"reflect"
	"unsafe"
)

// These functions implement an introspective structure
// serializer/deserializer, designed to allow RTCP packet
// Structs to be self-describing. They currently work with
// fields of type uint8, uint16, uint32, and uint64 (and
// types derived from them).
//
// - Unexported fields will take up space in the encoded
//   array, but wil be set to zero when written, and ignore
//   when read.
//
// - Fields that are marked with the tag `encoding:"omit"`
//   will be ignored when reading and writing data.
//
// For example:
//
//   type Example struct {
//     A uint32
//     B bool   `encoding:"omit"`
//     _ uint64
//     C uint16
//   }
//
// "A" will be encoded as four bytes, in network order. "B"
// will not be encoded at all. The anonymous uint64 will
// encode as 8 bytes of value "0", followed by two bytes
// encoding "C" in network order.

type packetBuffer struct {
	bytes []byte
}

const omit = "omit"

// Writes the structure passed to into the buffer that
// PacketBuffer is initialized with. This function will
// modify the PacketBuffer.bytes slice to exclude those
// bytes that have been written into.
//
func (b *packetBuffer) write(v interface{}) error { //nolint:gocognit
	value := reflect.ValueOf(v)

	// Indirect is safe to call on non-pointers, and
	// will simply return the same value in such cases
	value = reflect.Indirect(value)

	switch value.Kind() {
	case reflect.Uint8:
		if len(b.bytes) < 1 {
			return errWrongMarshalSize
		}
		if value.CanInterface() {
			b.bytes[0] = byte(value.Uint())
		}
		b.bytes = b.bytes[1:]
	case reflect.Uint16:
		if len(b.bytes) < 2 {
			return errWrongMarshalSize
		}
		if value.CanInterface() {
			binary.BigEndian.PutUint16(b.bytes, uint16(value.Uint()))
		}
		b.bytes = b.bytes[2:]
	case reflect.Uint32:
		if len(b.bytes) < 4 {
			return errWrongMarshalSize
		}
		if value.CanInterface() {
			binary.BigEndian.PutUint32(b.bytes, uint32(value.Uint()))
		}
		b.bytes = b.bytes[4:]
	case reflect.Uint64:
		if len(b.bytes) < 8 {
			return errWrongMarshalSize
		}
		if value.CanInterface() {
			binary.BigEndian.PutUint64(b.bytes, value.Uint())
		}
		b.bytes = b.bytes[8:]
	case reflect.Slice:
		for i := 0; i < value.Len(); i++ {
			if value.Index(i).CanInterface() {
				if err := b.write(value.Index(i).Interface()); err != nil {
					return err
				}
			} else {
				b.bytes = b.bytes[value.Index(i).Type().Size():]
			}
		}
	case reflect.Struct:
		for i := 0; i < value.NumField(); i++ {
			encoding := value.Type().Field(i).Tag.Get("encoding")
			if encoding == omit {
				continue
			}
			if value.Field(i).CanInterface() {
				if err := b.write(value.Field(i).Interface()); err != nil {
					return err
				}
			} else {
				advance := int(value.Field(i).Type().Size())
				if len(b.bytes) < advance {
					return errWrongMarshalSize
				}
				b.bytes = b.bytes[advance:]
			}
		}
	default:
		return errBadStructMemberType
	}
	return nil
}

// Reads bytes from the buffer as necessary to populate
// the structure passed as a parameter. This function will
// modify the PacketBuffer.bytes slice to exclude those
// bytes that have already been read.
func (b *packetBuffer) read(v interface{}) error { //nolint:gocognit
	ptr := reflect.ValueOf(v)
	if ptr.Kind() != reflect.Ptr {
		return errBadReadParameter
	}
	value := reflect.Indirect(ptr)

	// If this is an interface, we need to make it concrete before using it
	if value.Kind() == reflect.Interface {
		value = reflect.ValueOf(value.Interface())
	}
	value = reflect.Indirect(value)

	switch value.Kind() {
	case reflect.Uint8:
		if len(b.bytes) < 1 {
			return errWrongMarshalSize
		}
		value.SetUint(uint64(b.bytes[0]))
		b.bytes = b.bytes[1:]

	case reflect.Uint16:
		if len(b.bytes) < 2 {
			return errWrongMarshalSize
		}
		value.SetUint(uint64(binary.BigEndian.Uint16(b.bytes)))
		b.bytes = b.bytes[2:]

	case reflect.Uint32:
		if len(b.bytes) < 4 {
			return errWrongMarshalSize
		}
		value.SetUint(uint64(binary.BigEndian.Uint32(b.bytes)))
		b.bytes = b.bytes[4:]

	case reflect.Uint64:
		if len(b.bytes) < 8 {
			return errWrongMarshalSize
		}
		value.SetUint(binary.BigEndian.Uint64(b.bytes))
		b.bytes = b.bytes[8:]

	case reflect.Slice:
		// If we encounter a slice, we consume the rest of the data
		// in the buffer and load it into the slice.
		for len(b.bytes) > 0 {
			newElementPtr := reflect.New(value.Type().Elem())
			if err := b.read(newElementPtr.Interface()); err != nil {
				return err
			}
			if value.CanSet() {
				value.Set(reflect.Append(value, reflect.Indirect(newElementPtr)))
			}
		}

	case reflect.Struct:
		for i := 0; i < value.NumField(); i++ {
			encoding := value.Type().Field(i).Tag.Get("encoding")
			if encoding == omit {
				continue
			}
			if value.Field(i).CanInterface() {
				field := value.Field(i)
				newFieldPtr := reflect.NewAt(field.Type(), unsafe.Pointer(field.UnsafeAddr())) //nolint:gosec // This is the only way to get a typed pointer to a structure's field
				if err := b.read(newFieldPtr.Interface()); err != nil {
					return err
				}
			} else {
				advance := int(value.Field(i).Type().Size())
				if len(b.bytes) < advance {
					return errWrongMarshalSize
				}
				b.bytes = b.bytes[advance:]
			}
		}

	default:
		return errBadStructMemberType
	}
	return nil
}

// Consumes `size` bytes and returns them as an
// independent PacketBuffer
func (b *packetBuffer) split(size int) packetBuffer {
	if size > len(b.bytes) {
		size = len(b.bytes)
	}
	newBuffer := packetBuffer{bytes: b.bytes[:size]}
	b.bytes = b.bytes[size:]
	return newBuffer
}

// Returns the size that a structure will encode into.
// This fuction doesn't check that Write() will succeed,
// and may return unexpectedly large results for those
// structures that Write() will fail on
func wireSize(v interface{}) int {
	value := reflect.ValueOf(v)
	// Indirect is safe to call on non-pointers, and
	// will simply return the same value in such cases
	value = reflect.Indirect(value)
	size := int(0)

	switch value.Kind() {
	case reflect.Slice:
		for i := 0; i < value.Len(); i++ {
			if value.Index(i).CanInterface() {
				size += wireSize(value.Index(i).Interface())
			} else {
				size += int(value.Index(i).Type().Size())
			}
		}

	case reflect.Struct:
		for i := 0; i < value.NumField(); i++ {
			encoding := value.Type().Field(i).Tag.Get("encoding")
			if encoding == omit {
				continue
			}
			if value.Field(i).CanInterface() {
				size += wireSize(value.Field(i).Interface())
			} else {
				size += int(value.Field(i).Type().Size())
			}
		}

	default:
		size = int(value.Type().Size())
	}
	return size
}
