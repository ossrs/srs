// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package rtmp

import (
	"bytes"
	"fmt"
	"math"
	"strings"
	"testing"
)

func TestAmf0MarkerString(t *testing.T) {
	for _, tt := range []struct {
		marker amf0Marker
		want   string
	}{
		{amf0MarkerNumber, "Amf0Number"},
		{amf0MarkerBoolean, "amf0Boolean"},
		{amf0MarkerString, "Amf0String"},
		{amf0MarkerObject, "Amf0Object"},
		{amf0MarkerMovieClip, "MovieClip"},
		{amf0MarkerNull, "Null"},
		{amf0MarkerUndefined, "Undefined"},
		{amf0MarkerReference, "Reference"},
		{amf0MarkerEcmaArray, "EcmaArray"},
		{amf0MarkerObjectEnd, "ObjectEnd"},
		{amf0MarkerStrictArray, "StrictArray"},
		{amf0MarkerDate, "Date"},
		{amf0MarkerLongString, "LongString"},
		{amf0MarkerUnsupported, "Unsupported"},
		{amf0MarkerRecordSet, "RecordSet"},
		{amf0MarkerXmlDocument, "XmlDocument"},
		{amf0MarkerTypedObject, "TypedObject"},
		{amf0MarkerAvmPlusObject, "AvmPlusObject"},
		{amf0MarkerForbidden, "Forbidden"},
		{amf0Marker(0xee), "Forbidden"},
	} {
		if got := tt.marker.String(); got != tt.want {
			t.Fatalf("marker=%#x String()=%v, want %v", byte(tt.marker), got, tt.want)
		}
	}
}

func TestAmf0Discovery(t *testing.T) {
	for _, tt := range []struct {
		name string
		data []byte
		ok   func(Amf0Any) bool
	}{
		{"number", []byte{byte(amf0MarkerNumber)}, func(v Amf0Any) bool { _, ok := v.(Amf0Number); return ok }},
		{"boolean", []byte{byte(amf0MarkerBoolean)}, func(v Amf0Any) bool { _, ok := v.(Amf0Boolean); return ok }},
		{"string", []byte{byte(amf0MarkerString)}, func(v Amf0Any) bool { _, ok := v.(Amf0String); return ok }},
		{"object", []byte{byte(amf0MarkerObject)}, func(v Amf0Any) bool { _, ok := v.(Amf0Object); return ok }},
		{"null", []byte{byte(amf0MarkerNull)}, func(v Amf0Any) bool { _, ok := v.(Amf0Null); return ok }},
		{"undefined", []byte{byte(amf0MarkerUndefined)}, func(v Amf0Any) bool { _, ok := v.(Amf0Undefined); return ok }},
		{"ecma-array", []byte{byte(amf0MarkerEcmaArray)}, func(v Amf0Any) bool { _, ok := v.(Amf0EcmaArray); return ok }},
		{"object-end", []byte{byte(amf0MarkerObjectEnd)}, func(v Amf0Any) bool { _, ok := v.(*amf0ObjectEOF); return ok }},
		{"strict-array", []byte{byte(amf0MarkerStrictArray)}, func(v Amf0Any) bool { _, ok := v.(Amf0StrictArray); return ok }},
	} {
		t.Run(tt.name, func(t *testing.T) {
			value, err := Amf0Discovery(tt.data)
			if err != nil {
				t.Fatalf("Amf0Discovery() err=%v", err)
			}
			if !tt.ok(value) {
				t.Fatalf("Amf0Discovery()=%T", value)
			}
		})
	}

	for _, data := range [][]byte{{}, {byte(amf0MarkerReference)}, {byte(amf0MarkerDate)}, {byte(amf0MarkerForbidden)}} {
		if value, err := Amf0Discovery(data); err == nil || value != nil {
			t.Fatalf("Amf0Discovery(%v) value=%T, err=%v, want error", data, value, err)
		}
	}
}

func TestAmf0Converter(t *testing.T) {
	values := []struct {
		name string
		in   Amf0Any
		ok   func(Amf0Converter) bool
	}{
		{"number", NewAmf0Number(1), func(c Amf0Converter) bool { return c.ToNumber() != nil }},
		{"boolean", NewAmf0Boolean(true), func(c Amf0Converter) bool { return c.ToBoolean() != nil }},
		{"string", NewAmf0String("v"), func(c Amf0Converter) bool { return c.ToString() != nil }},
		{"object", NewAmf0Object(), func(c Amf0Converter) bool { return c.ToObject() != nil }},
		{"null", NewAmf0Null(), func(c Amf0Converter) bool { return c.ToNull() != nil }},
		{"undefined", NewAmf0Undefined(), func(c Amf0Converter) bool { return c.ToUndefined() != nil }},
		{"ecma-array", NewAmf0EcmaArray(), func(c Amf0Converter) bool { return c.ToEcmaArray() != nil }},
		{"strict-array", NewAmf0StrictArray(), func(c Amf0Converter) bool { return c.ToStrictArray() != nil }},
	}

	for _, tt := range values {
		t.Run(tt.name, func(t *testing.T) {
			converter := NewAmf0Converter(tt.in)
			if !tt.ok(converter) {
				t.Fatalf("expected successful conversion for %T", tt.in)
			}
		})
	}

	nilConverter := NewAmf0Converter(nil)
	if nilConverter.ToNumber() != nil || nilConverter.ToBoolean() != nil || nilConverter.ToString() != nil ||
		nilConverter.ToObject() != nil || nilConverter.ToNull() != nil || nilConverter.ToUndefined() != nil ||
		nilConverter.ToEcmaArray() != nil || nilConverter.ToStrictArray() != nil {
		t.Fatal("nil converter should not convert")
	}
}

func TestAmf0UTF8(t *testing.T) {
	var value amf0UTF8 = "hello"
	b, err := value.MarshalBinary()
	if err != nil {
		t.Fatalf("MarshalBinary() err=%v", err)
	}
	if value.Size() != len(b) {
		t.Fatalf("Size()=%v, len=%v", value.Size(), len(b))
	}

	var decoded amf0UTF8
	if err := decoded.UnmarshalBinary(b); err != nil {
		t.Fatalf("UnmarshalBinary() err=%v", err)
	}
	if decoded != value {
		t.Fatalf("decoded=%v, want %v", decoded, value)
	}

	for _, data := range [][]byte{{0x00}, {0x00, 0x05, 'h'}} {
		if err := decoded.UnmarshalBinary(data); err == nil {
			t.Fatalf("UnmarshalBinary(%v) should fail", data)
		}
	}
}

func TestAmf0Number(t *testing.T) {
	number := NewAmf0Number(math.Pi)
	if number.Size() != 9 || number.(*amf0Number).amf0Marker() != amf0MarkerNumber {
		t.Fatalf("unexpected number metadata")
	}

	b, err := number.MarshalBinary()
	if err != nil {
		t.Fatalf("MarshalBinary() err=%v", err)
	}

	decoded := NewAmf0Number(0)
	if err := decoded.UnmarshalBinary(b); err != nil {
		t.Fatalf("UnmarshalBinary() err=%v", err)
	}
	if got := decoded.Float64(); got != math.Pi {
		t.Fatalf("Float64()=%v, want %v", got, math.Pi)
	}

	for _, data := range [][]byte{{byte(amf0MarkerNumber)}, append([]byte{byte(amf0MarkerString)}, b[1:]...)} {
		if err := decoded.UnmarshalBinary(data); err == nil {
			t.Fatalf("UnmarshalBinary(%v) should fail", data)
		}
	}
}

func TestAmf0Boolean(t *testing.T) {
	for _, want := range []bool{false, true} {
		boolean := NewAmf0Boolean(want)
		if boolean.Size() != 2 || boolean.(*amf0Boolean).amf0Marker() != amf0MarkerBoolean {
			t.Fatalf("unexpected boolean metadata")
		}

		b, err := boolean.MarshalBinary()
		if err != nil {
			t.Fatalf("MarshalBinary() err=%v", err)
		}

		decoded := NewAmf0Boolean(!want)
		if err := decoded.UnmarshalBinary(b); err != nil {
			t.Fatalf("UnmarshalBinary() err=%v", err)
		}
		if got := decoded.Bool(); got != want {
			t.Fatalf("Bool()=%v, want %v", got, want)
		}
	}

	decoded := NewAmf0Boolean(false)
	for _, data := range [][]byte{{byte(amf0MarkerBoolean)}, {byte(amf0MarkerNumber), 1}} {
		if err := decoded.UnmarshalBinary(data); err == nil {
			t.Fatalf("UnmarshalBinary(%v) should fail", data)
		}
	}
}

func TestAmf0String(t *testing.T) {
	value := NewAmf0String("hello")
	if value.Size() != 8 || value.(*amf0String).amf0Marker() != amf0MarkerString {
		t.Fatalf("unexpected string metadata")
	}

	b, err := value.MarshalBinary()
	if err != nil {
		t.Fatalf("MarshalBinary() err=%v", err)
	}

	decoded := NewAmf0String("")
	if err := decoded.UnmarshalBinary(b); err != nil {
		t.Fatalf("UnmarshalBinary() err=%v", err)
	}
	if got := decoded.String(); got != "hello" {
		t.Fatalf("String()=%v, want hello", got)
	}

	for _, data := range [][]byte{{}, {byte(amf0MarkerNumber), 0, 0}, {byte(amf0MarkerString), 0, 5, 'h'}} {
		if err := decoded.UnmarshalBinary(data); err == nil {
			t.Fatalf("UnmarshalBinary(%v) should fail", data)
		}
	}
}

func TestAmf0ObjectEOF(t *testing.T) {
	eof := &amf0ObjectEOF{}
	if eof.Size() != 3 || eof.amf0Marker() != amf0MarkerObjectEnd {
		t.Fatalf("unexpected eof metadata")
	}

	b, err := eof.MarshalBinary()
	if err != nil {
		t.Fatalf("MarshalBinary() err=%v", err)
	}
	if !bytes.Equal(b, []byte{0, 0, 9}) {
		t.Fatalf("MarshalBinary()=%v", b)
	}
	for _, data := range [][]byte{b, {0, 0, 9, 1}} {
		if err := eof.UnmarshalBinary(data); err != nil {
			t.Fatalf("UnmarshalBinary(%v) err=%v", data, err)
		}
	}
	for _, data := range [][]byte{{0, 0}, {0, 1, 9}} {
		if err := eof.UnmarshalBinary(data); err == nil {
			t.Fatalf("UnmarshalBinary(%v) should fail", data)
		}
	}
}

func TestAmf0Object(t *testing.T) {
	object := NewAmf0Object().
		Set("name", NewAmf0String("stream")).
		Set("code", NewAmf0Number(100)).
		Set("ok", NewAmf0Boolean(true))
	object.Set("code", NewAmf0Number(200))

	if object.(*amf0Object).amf0Marker() != amf0MarkerObject || object.Size() == 0 {
		t.Fatalf("unexpected object metadata")
	}
	if object.Get("missing") != nil {
		t.Fatal("missing property should be nil")
	}

	b, err := object.MarshalBinary()
	if err != nil {
		t.Fatalf("MarshalBinary() err=%v", err)
	}

	decoded := NewAmf0Object()
	if err := decoded.UnmarshalBinary(b); err != nil {
		t.Fatalf("UnmarshalBinary() err=%v", err)
	}
	if got := NewAmf0Converter(decoded.Get("name")).ToString().String(); got != "stream" {
		t.Fatalf("name=%v", got)
	}
	if got := NewAmf0Converter(decoded.Get("code")).ToNumber().Float64(); got != 200 {
		t.Fatalf("code=%v", got)
	}
	if got := NewAmf0Converter(decoded.Get("ok")).ToBoolean().Bool(); !got {
		t.Fatalf("ok=%v", got)
	}

	for _, data := range [][]byte{{}, {byte(amf0MarkerString)}, {byte(amf0MarkerObject), 0, 4, 'n'}} {
		if err := decoded.UnmarshalBinary(data); err == nil {
			t.Fatalf("UnmarshalBinary(%v) should fail", data)
		}
	}

	base := &amf0ObjectBase{}
	if err := base.unmarshal(nil, false, -1); err == nil {
		t.Fatal("unmarshal without eof and negative maxElems should fail")
	}
	if err := base.unmarshal(nil, true, 0); err == nil {
		t.Fatal("unmarshal with eof and non-negative maxElems should fail")
	}
}

func TestAmf0EcmaArray(t *testing.T) {
	array := NewAmf0EcmaArray().
		Set("name", NewAmf0String("stream")).
		Set("code", NewAmf0Number(100))

	if array.(*amf0EcmaArray).amf0Marker() != amf0MarkerEcmaArray || array.Size() == 0 {
		t.Fatalf("unexpected ecma array metadata")
	}
	if array.Get("missing") != nil {
		t.Fatal("missing property should be nil")
	}

	b, err := array.MarshalBinary()
	if err != nil {
		t.Fatalf("MarshalBinary() err=%v", err)
	}

	decoded := NewAmf0EcmaArray()
	if err := decoded.UnmarshalBinary(b); err != nil {
		t.Fatalf("UnmarshalBinary() err=%v", err)
	}
	if got := NewAmf0Converter(decoded.Get("name")).ToString().String(); got != "stream" {
		t.Fatalf("name=%v", got)
	}
	if got := NewAmf0Converter(decoded.Get("code")).ToNumber().Float64(); got != 100 {
		t.Fatalf("code=%v", got)
	}

	for _, data := range [][]byte{{}, {byte(amf0MarkerEcmaArray), 0}, {byte(amf0MarkerString), 0, 0, 0, 0}, {byte(amf0MarkerEcmaArray), 0, 0, 0, 0, 0, 4, 'n'}} {
		if err := decoded.UnmarshalBinary(data); err == nil {
			t.Fatalf("UnmarshalBinary(%v) should fail", data)
		}
	}
}

func TestAmf0StrictArray(t *testing.T) {
	array := NewAmf0StrictArray().
		Set("name", NewAmf0String("stream")).
		Set("code", NewAmf0Number(100))
	array.(*amf0StrictArray).count = 2

	if array.(*amf0StrictArray).amf0Marker() != amf0MarkerStrictArray || array.Size() == 0 {
		t.Fatalf("unexpected strict array metadata")
	}
	if array.Get("missing") != nil {
		t.Fatal("missing property should be nil")
	}

	b, err := array.MarshalBinary()
	if err != nil {
		t.Fatalf("MarshalBinary() err=%v", err)
	}

	decoded := NewAmf0StrictArray()
	if err := decoded.UnmarshalBinary(b); err != nil {
		t.Fatalf("UnmarshalBinary() err=%v", err)
	}
	if got := NewAmf0Converter(decoded.Get("name")).ToString().String(); got != "stream" {
		t.Fatalf("name=%v", got)
	}
	if got := NewAmf0Converter(decoded.Get("code")).ToNumber().Float64(); got != 100 {
		t.Fatalf("code=%v", got)
	}

	empty := append([]byte{byte(amf0MarkerStrictArray)}, 0, 0, 0, 0)
	if err := decoded.UnmarshalBinary(empty); err != nil {
		t.Fatalf("UnmarshalBinary(empty) err=%v", err)
	}
	for _, data := range [][]byte{{}, {byte(amf0MarkerStrictArray), 0}, {byte(amf0MarkerString), 0, 0, 0, 0}, {byte(amf0MarkerStrictArray), 0, 0, 0, 1, 0, 4, 'n'}} {
		if err := NewAmf0StrictArray().UnmarshalBinary(data); err == nil {
			t.Fatalf("UnmarshalBinary(%v) should fail", data)
		}
	}
}

func TestAmf0SingleMarkerObjects(t *testing.T) {
	for _, tt := range []struct {
		name   string
		value  Amf0Any
		marker amf0Marker
	}{
		{"null", NewAmf0Null(), amf0MarkerNull},
		{"undefined", NewAmf0Undefined(), amf0MarkerUndefined},
	} {
		t.Run(tt.name, func(t *testing.T) {
			if tt.value.Size() != 1 || tt.value.amf0Marker() != tt.marker {
				t.Fatalf("unexpected metadata")
			}
			b, err := tt.value.MarshalBinary()
			if err != nil {
				t.Fatalf("MarshalBinary() err=%v", err)
			}
			if err := tt.value.UnmarshalBinary(b); err != nil {
				t.Fatalf("UnmarshalBinary() err=%v", err)
			}
			for _, data := range [][]byte{{}, {byte(amf0MarkerString)}} {
				if err := tt.value.UnmarshalBinary(data); err == nil {
					t.Fatalf("UnmarshalBinary(%v) should fail", data)
				}
			}
		})
	}
}

type errorAmf0Buffer struct {
	writeByteErr bool
	writeErr     bool
}

func (v *errorAmf0Buffer) Bytes() []byte {
	return nil
}

func (v *errorAmf0Buffer) WriteByte(byte) error {
	if v.writeByteErr {
		return fmt.Errorf("write byte")
	}
	return nil
}

func (v *errorAmf0Buffer) Write([]byte) (int, error) {
	if v.writeErr {
		return 0, fmt.Errorf("write")
	}
	return 0, nil
}

type errorAmf0Any struct {
	Amf0Any
}

func (v *errorAmf0Any) Size() int {
	return 1
}

func (v *errorAmf0Any) MarshalBinary() ([]byte, error) {
	return nil, fmt.Errorf("marshal")
}

func (v *errorAmf0Any) UnmarshalBinary([]byte) error {
	return nil
}

func (v *errorAmf0Any) amf0Marker() amf0Marker {
	return amf0MarkerNumber
}

// setBufFactory replaces the bufFactory on whichever amf0 object-like type
// underlies v. Concurrent tests can use this safely because each value carries
// its own factory.
func setBufFactory(v Amf0Any, fn func() amf0Buffer) {
	switch v := v.(type) {
	case *amf0Object:
		v.bufFactory = fn
	case *amf0EcmaArray:
		v.bufFactory = fn
	case *amf0StrictArray:
		v.bufFactory = fn
	}
}

func TestAmf0MarshalErrors(t *testing.T) {
	for _, tt := range []struct {
		name string
		make func() Amf0Any
	}{
		{"object", func() Amf0Any { return NewAmf0Object() }},
		{"ecma-array", func() Amf0Any { return NewAmf0EcmaArray() }},
		{"strict-array", func() Amf0Any { return NewAmf0StrictArray() }},
	} {
		t.Run(tt.name+" write-byte", func(t *testing.T) {
			value := tt.make()
			setBufFactory(value, func() amf0Buffer { return &errorAmf0Buffer{writeByteErr: true} })
			if _, err := value.MarshalBinary(); err == nil {
				t.Fatal("MarshalBinary() should fail")
			}
		})

		t.Run(tt.name+" write-prop", func(t *testing.T) {
			value := tt.make()
			setBufFactory(value, func() amf0Buffer { return &errorAmf0Buffer{writeErr: true} })
			switch v := value.(type) {
			case Amf0Object:
				v.Set("name", NewAmf0String("stream"))
			case Amf0EcmaArray:
				v.Set("name", NewAmf0String("stream"))
			case Amf0StrictArray:
				v.Set("name", NewAmf0String("stream"))
				v.(*amf0StrictArray).count = 1
			}
			if _, err := value.MarshalBinary(); err == nil {
				t.Fatal("MarshalBinary() should fail")
			}
		})
	}

	for _, tt := range []struct {
		name string
		make func() Amf0Any
	}{
		{"object", func() Amf0Any { return NewAmf0Object().Set("bad", &errorAmf0Any{}) }},
		{"ecma-array", func() Amf0Any { return NewAmf0EcmaArray().Set("bad", &errorAmf0Any{}) }},
		{"strict-array", func() Amf0Any {
			value := NewAmf0StrictArray().Set("bad", &errorAmf0Any{})
			value.(*amf0StrictArray).count = 1
			return value
		}},
	} {
		t.Run(tt.name+" marshal-value", func(t *testing.T) {
			if _, err := tt.make().MarshalBinary(); err == nil {
				t.Fatal("MarshalBinary() should fail")
			}
		})
	}
}

func TestAmf0UnmarshalNestedErrors(t *testing.T) {
	// Object property with unsupported marker.
	data := []byte{byte(amf0MarkerObject), 0, 3, 'b', 'a', 'd', byte(amf0MarkerDate)}
	if err := NewAmf0Object().UnmarshalBinary(data); err == nil || !strings.Contains(err.Error(), "discover prop bad") {
		t.Fatalf("err=%v, want discover prop bad", err)
	}

	// Object property with invalid payload size.
	data = []byte{byte(amf0MarkerObject), 0, 3, 'b', 'a', 'd', byte(amf0MarkerNumber), 0}
	if err := NewAmf0Object().UnmarshalBinary(data); err == nil || !strings.Contains(err.Error(), "unmarshal prop bad") {
		t.Fatalf("err=%v, want unmarshal prop bad", err)
	}
}
