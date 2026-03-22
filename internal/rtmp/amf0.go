// Copyright (c) 2025 Winlin
//
// SPDX-License-Identifier: MIT
package rtmp

import (
	"bytes"
	"encoding"
	"encoding/binary"
	"fmt"
	"math"
	"sync"

	"srsx/internal/errors"
)

// Please read @doc amf0_spec_121207.pdf, @page 4, @section 2.1 Types Overview
type amf0Marker uint8

const (
	amf0MarkerNumber        amf0Marker = iota // 0
	amf0MarkerBoolean                         // 1
	amf0MarkerString                          // 2
	amf0MarkerObject                          // 3
	amf0MarkerMovieClip                       // 4
	amf0MarkerNull                            // 5
	amf0MarkerUndefined                       // 6
	amf0MarkerReference                       // 7
	amf0MarkerEcmaArray                       // 8
	amf0MarkerObjectEnd                       // 9
	amf0MarkerStrictArray                     // 10
	amf0MarkerDate                            // 11
	amf0MarkerLongString                      // 12
	amf0MarkerUnsupported                     // 13
	amf0MarkerRecordSet                       // 14
	amf0MarkerXmlDocument                     // 15
	amf0MarkerTypedObject                     // 16
	amf0MarkerAvmPlusObject                   // 17

	amf0MarkerForbidden amf0Marker = 0xff
)

func (v amf0Marker) String() string {
	switch v {
	case amf0MarkerNumber:
		return "Amf0Number"
	case amf0MarkerBoolean:
		return "amf0Boolean"
	case amf0MarkerString:
		return "Amf0String"
	case amf0MarkerObject:
		return "Amf0Object"
	case amf0MarkerNull:
		return "Null"
	case amf0MarkerUndefined:
		return "Undefined"
	case amf0MarkerReference:
		return "Reference"
	case amf0MarkerEcmaArray:
		return "EcmaArray"
	case amf0MarkerObjectEnd:
		return "ObjectEnd"
	case amf0MarkerStrictArray:
		return "StrictArray"
	case amf0MarkerDate:
		return "Date"
	case amf0MarkerLongString:
		return "LongString"
	case amf0MarkerUnsupported:
		return "Unsupported"
	case amf0MarkerXmlDocument:
		return "XmlDocument"
	case amf0MarkerTypedObject:
		return "TypedObject"
	case amf0MarkerAvmPlusObject:
		return "AvmPlusObject"
	case amf0MarkerMovieClip:
		return "MovieClip"
	case amf0MarkerRecordSet:
		return "RecordSet"
	default:
		return "Forbidden"
	}
}

// For utest to mock it.
type amf0Buffer interface {
	Bytes() []byte
	WriteByte(c byte) error
	Write(p []byte) (n int, err error)
}

var createBuffer = func() amf0Buffer {
	return &bytes.Buffer{}
}

// All AMF0 things.
type amf0Any interface {
	// Binary marshaler and unmarshaler.
	encoding.BinaryUnmarshaler
	encoding.BinaryMarshaler
	// Get the size of bytes to marshal this object.
	Size() int

	// Get the Marker of any AMF0 stuff.
	amf0Marker() amf0Marker
}

type amf0Converter struct {
	from amf0Any
}

func NewAmf0Converter(from amf0Any) *amf0Converter {
	return &amf0Converter{from: from}
}

func (v *amf0Converter) ToNumber() *amf0Number {
	return amf0AnyTo[*amf0Number](v.from)
}

func (v *amf0Converter) ToBoolean() *amf0Boolean {
	return amf0AnyTo[*amf0Boolean](v.from)
}

func (v *amf0Converter) ToString() *amf0String {
	return amf0AnyTo[*amf0String](v.from)
}

func (v *amf0Converter) ToObject() *amf0Object {
	return amf0AnyTo[*amf0Object](v.from)
}

func (v *amf0Converter) ToNull() *amf0Null {
	return amf0AnyTo[*amf0Null](v.from)
}

func (v *amf0Converter) ToUndefined() *amf0Undefined {
	return amf0AnyTo[*amf0Undefined](v.from)
}

func (v *amf0Converter) ToEcmaArray() *amf0EcmaArray {
	return amf0AnyTo[*amf0EcmaArray](v.from)
}

func (v *amf0Converter) ToStrictArray() *amf0StrictArray {
	return amf0AnyTo[*amf0StrictArray](v.from)
}

// Convert any to specified object.
func amf0AnyTo[T amf0Any](a amf0Any) T {
	var to T
	if a != nil {
		if v, ok := a.(T); ok {
			return v
		}
	}
	return to
}

// Discovery the amf0 object from the bytes b.
func Amf0Discovery(p []byte) (a amf0Any, err error) {
	if len(p) < 1 {
		return nil, errors.Errorf("require 1 bytes only %v", len(p))
	}
	m := amf0Marker(p[0])

	switch m {
	case amf0MarkerNumber:
		return NewAmf0Number(0), nil
	case amf0MarkerBoolean:
		return NewAmf0Boolean(false), nil
	case amf0MarkerString:
		return NewAmf0String(""), nil
	case amf0MarkerObject:
		return NewAmf0Object(), nil
	case amf0MarkerNull:
		return NewAmf0Null(), nil
	case amf0MarkerUndefined:
		return NewAmf0Undefined(), nil
	case amf0MarkerReference:
	case amf0MarkerEcmaArray:
		return NewAmf0EcmaArray(), nil
	case amf0MarkerObjectEnd:
		return &amf0ObjectEOF{}, nil
	case amf0MarkerStrictArray:
		return NewAmf0StrictArray(), nil
	case amf0MarkerDate, amf0MarkerLongString, amf0MarkerUnsupported, amf0MarkerXmlDocument,
		amf0MarkerTypedObject, amf0MarkerAvmPlusObject, amf0MarkerForbidden, amf0MarkerMovieClip,
		amf0MarkerRecordSet:
		return nil, errors.Errorf("Marker %v is not supported", m)
	}
	return nil, errors.Errorf("Marker %v is invalid", m)
}

// The UTF8 string, please read @doc amf0_spec_121207.pdf, @page 3, @section 1.3.1 Strings and UTF-8
type amf0UTF8 string

func (v *amf0UTF8) Size() int {
	return 2 + len(string(*v))
}

func (v *amf0UTF8) UnmarshalBinary(data []byte) (err error) {
	var p []byte
	if p = data; len(p) < 2 {
		return errors.Errorf("require 2 bytes only %v", len(p))
	}
	size := uint16(p[0])<<8 | uint16(p[1])

	if p = data[2:]; len(p) < int(size) {
		return errors.Errorf("require %v bytes only %v", int(size), len(p))
	}
	*v = amf0UTF8(string(p[:size]))

	return
}

func (v *amf0UTF8) MarshalBinary() (data []byte, err error) {
	data = make([]byte, v.Size())

	size := uint16(len(string(*v)))
	data[0] = byte(size >> 8)
	data[1] = byte(size)

	if size > 0 {
		copy(data[2:], []byte(*v))
	}

	return
}

// The number object, please read @doc amf0_spec_121207.pdf, @page 5, @section 2.2 Number Type
type amf0Number float64

func NewAmf0Number(f float64) *amf0Number {
	v := amf0Number(f)
	return &v
}

func (v *amf0Number) amf0Marker() amf0Marker {
	return amf0MarkerNumber
}

func (v *amf0Number) Size() int {
	return 1 + 8
}

func (v *amf0Number) UnmarshalBinary(data []byte) (err error) {
	var p []byte
	if p = data; len(p) < 9 {
		return errors.Errorf("require 9 bytes only %v", len(p))
	}
	if m := amf0Marker(p[0]); m != amf0MarkerNumber {
		return errors.Errorf("Amf0Number amf0Marker %v is illegal", m)
	}

	f := binary.BigEndian.Uint64(p[1:])
	*v = amf0Number(math.Float64frombits(f))
	return
}

func (v *amf0Number) MarshalBinary() (data []byte, err error) {
	data = make([]byte, 9)
	data[0] = byte(amf0MarkerNumber)
	f := math.Float64bits(float64(*v))
	binary.BigEndian.PutUint64(data[1:], f)
	return
}

// The string objet, please read @doc amf0_spec_121207.pdf, @page 5, @section 2.4 String Type
type amf0String string

func NewAmf0String(s string) *amf0String {
	v := amf0String(s)
	return &v
}

func (v *amf0String) amf0Marker() amf0Marker {
	return amf0MarkerString
}

func (v *amf0String) Size() int {
	u := amf0UTF8(*v)
	return 1 + u.Size()
}

func (v *amf0String) UnmarshalBinary(data []byte) (err error) {
	var p []byte
	if p = data; len(p) < 1 {
		return errors.Errorf("require 1 bytes only %v", len(p))
	}
	if m := amf0Marker(p[0]); m != amf0MarkerString {
		return errors.Errorf("Amf0String amf0Marker %v is illegal", m)
	}

	var sv amf0UTF8
	if err = sv.UnmarshalBinary(p[1:]); err != nil {
		return errors.WithMessage(err, "utf8")
	}
	*v = amf0String(string(sv))
	return
}

func (v *amf0String) MarshalBinary() (data []byte, err error) {
	u := amf0UTF8(*v)

	var pb []byte
	if pb, err = u.MarshalBinary(); err != nil {
		return nil, errors.WithMessage(err, "utf8")
	}

	data = append([]byte{byte(amf0MarkerString)}, pb...)
	return
}

// The AMF0 object end type, please read @doc amf0_spec_121207.pdf, @page 5, @section 2.11 Object End Type
type amf0ObjectEOF struct {
}

func (v *amf0ObjectEOF) amf0Marker() amf0Marker {
	return amf0MarkerObjectEnd
}

func (v *amf0ObjectEOF) Size() int {
	return 3
}

func (v *amf0ObjectEOF) UnmarshalBinary(data []byte) (err error) {
	p := data

	if len(p) < 3 {
		return errors.Errorf("require 3 bytes only %v", len(p))
	}

	if p[0] != 0 || p[1] != 0 || p[2] != 9 {
		return errors.Errorf("EOF amf0Marker %v is illegal", p[0:3])
	}
	return
}

func (v *amf0ObjectEOF) MarshalBinary() (data []byte, err error) {
	return []byte{0, 0, 9}, nil
}

// Use array for object and ecma array, to keep the original order.
type amf0Property struct {
	key   amf0UTF8
	value amf0Any
}

// The object-like AMF0 structure, like object and ecma array and strict array.
type amf0ObjectBase struct {
	properties []*amf0Property
	lock       sync.Mutex
}

func (v *amf0ObjectBase) Size() int {
	v.lock.Lock()
	defer v.lock.Unlock()

	var size int

	for _, p := range v.properties {
		key, value := p.key, p.value
		size += key.Size() + value.Size()
	}

	return size
}

func (v *amf0ObjectBase) Get(key string) amf0Any {
	v.lock.Lock()
	defer v.lock.Unlock()

	for _, p := range v.properties {
		if string(p.key) == key {
			return p.value
		}
	}

	return nil
}

func (v *amf0ObjectBase) Set(key string, value amf0Any) *amf0ObjectBase {
	v.lock.Lock()
	defer v.lock.Unlock()

	prop := &amf0Property{key: amf0UTF8(key), value: value}

	var ok bool
	for i, p := range v.properties {
		if string(p.key) == key {
			v.properties[i] = prop
			ok = true
		}
	}

	if !ok {
		v.properties = append(v.properties, prop)
	}

	return v
}

func (v *amf0ObjectBase) unmarshal(p []byte, eof bool, maxElems int) (err error) {
	// if no eof, elems specified by maxElems.
	if !eof && maxElems < 0 {
		return errors.Errorf("maxElems=%v without eof", maxElems)
	}
	// if eof, maxElems must be -1.
	if eof && maxElems != -1 {
		return errors.Errorf("maxElems=%v with eof", maxElems)
	}

	readOne := func() (amf0UTF8, amf0Any, error) {
		var u amf0UTF8
		if err = u.UnmarshalBinary(p); err != nil {
			return "", nil, errors.WithMessage(err, "prop name")
		}

		p = p[u.Size():]
		var a amf0Any
		if a, err = Amf0Discovery(p); err != nil {
			return "", nil, errors.WithMessage(err, fmt.Sprintf("discover prop %v", string(u)))
		}
		return u, a, nil
	}

	pushOne := func(u amf0UTF8, a amf0Any) error {
		// For object property, consume the whole bytes.
		if err = a.UnmarshalBinary(p); err != nil {
			return errors.WithMessage(err, fmt.Sprintf("unmarshal prop %v", string(u)))
		}

		v.Set(string(u), a)
		p = p[a.Size():]
		return nil
	}

	for eof {
		u, a, err := readOne()
		if err != nil {
			return errors.WithMessage(err, "read")
		}

		// For object EOF, we should only consume total 3bytes.
		if u.Size() == 2 && a.amf0Marker() == amf0MarkerObjectEnd {
			// 2 bytes is consumed by u(name), the a(eof) should only consume 1 byte.
			p = p[1:]
			return nil
		}

		if err := pushOne(u, a); err != nil {
			return errors.WithMessage(err, "push")
		}
	}

	for len(v.properties) < maxElems {
		u, a, err := readOne()
		if err != nil {
			return errors.WithMessage(err, "read")
		}

		if err := pushOne(u, a); err != nil {
			return errors.WithMessage(err, "push")
		}
	}

	return
}

func (v *amf0ObjectBase) marshal(b amf0Buffer) (err error) {
	v.lock.Lock()
	defer v.lock.Unlock()

	var pb []byte
	for _, p := range v.properties {
		key, value := p.key, p.value

		if pb, err = key.MarshalBinary(); err != nil {
			return errors.WithMessage(err, fmt.Sprintf("marshal %v", string(key)))
		}
		if _, err = b.Write(pb); err != nil {
			return errors.Wrapf(err, "write %v", string(key))
		}

		if pb, err = value.MarshalBinary(); err != nil {
			return errors.WithMessage(err, fmt.Sprintf("marshal value for %v", string(key)))
		}
		if _, err = b.Write(pb); err != nil {
			return errors.Wrapf(err, "marshal value for %v", string(key))
		}
	}

	return
}

// The AMF0 object, please read @doc amf0_spec_121207.pdf, @page 5, @section 2.5 Object Type
type amf0Object struct {
	amf0ObjectBase
	eof amf0ObjectEOF
}

func NewAmf0Object() *amf0Object {
	v := &amf0Object{}
	v.properties = []*amf0Property{}
	return v
}

func (v *amf0Object) amf0Marker() amf0Marker {
	return amf0MarkerObject
}

func (v *amf0Object) Size() int {
	return int(1) + v.eof.Size() + v.amf0ObjectBase.Size()
}

func (v *amf0Object) UnmarshalBinary(data []byte) (err error) {
	var p []byte
	if p = data; len(p) < 1 {
		return errors.Errorf("require 1 byte only %v", len(p))
	}
	if m := amf0Marker(p[0]); m != amf0MarkerObject {
		return errors.Errorf("Amf0Object amf0Marker %v is illegal", m)
	}
	p = p[1:]

	if err = v.unmarshal(p, true, -1); err != nil {
		return errors.WithMessage(err, "unmarshal")
	}

	return
}

func (v *amf0Object) MarshalBinary() (data []byte, err error) {
	b := createBuffer()

	if err = b.WriteByte(byte(amf0MarkerObject)); err != nil {
		return nil, errors.Wrap(err, "marshal")
	}

	if err = v.marshal(b); err != nil {
		return nil, errors.WithMessage(err, "marshal")
	}

	var pb []byte
	if pb, err = v.eof.MarshalBinary(); err != nil {
		return nil, errors.WithMessage(err, "marshal")
	}
	if _, err = b.Write(pb); err != nil {
		return nil, errors.Wrap(err, "marshal")
	}

	return b.Bytes(), nil
}

// The AMF0 ecma array, please read @doc amf0_spec_121207.pdf, @page 6, @section 2.10 ECMA Array Type
type amf0EcmaArray struct {
	amf0ObjectBase
	count uint32
	eof   amf0ObjectEOF
}

func NewAmf0EcmaArray() *amf0EcmaArray {
	v := &amf0EcmaArray{}
	v.properties = []*amf0Property{}
	return v
}

func (v *amf0EcmaArray) amf0Marker() amf0Marker {
	return amf0MarkerEcmaArray
}

func (v *amf0EcmaArray) Size() int {
	return int(1) + 4 + v.eof.Size() + v.amf0ObjectBase.Size()
}

func (v *amf0EcmaArray) UnmarshalBinary(data []byte) (err error) {
	var p []byte
	if p = data; len(p) < 5 {
		return errors.Errorf("require 5 bytes only %v", len(p))
	}
	if m := amf0Marker(p[0]); m != amf0MarkerEcmaArray {
		return errors.Errorf("EcmaArray amf0Marker %v is illegal", m)
	}
	v.count = binary.BigEndian.Uint32(p[1:])
	p = p[5:]

	if err = v.unmarshal(p, true, -1); err != nil {
		return errors.WithMessage(err, "unmarshal")
	}
	return
}

func (v *amf0EcmaArray) MarshalBinary() (data []byte, err error) {
	b := createBuffer()

	if err = b.WriteByte(byte(amf0MarkerEcmaArray)); err != nil {
		return nil, errors.Wrap(err, "marshal")
	}

	if err = binary.Write(b, binary.BigEndian, v.count); err != nil {
		return nil, errors.Wrap(err, "marshal")
	}

	if err = v.marshal(b); err != nil {
		return nil, errors.WithMessage(err, "marshal")
	}

	var pb []byte
	if pb, err = v.eof.MarshalBinary(); err != nil {
		return nil, errors.WithMessage(err, "marshal")
	}
	if _, err = b.Write(pb); err != nil {
		return nil, errors.Wrap(err, "marshal")
	}

	return b.Bytes(), nil
}

// The AMF0 strict array, please read @doc amf0_spec_121207.pdf, @page 7, @section 2.12 Strict Array Type
type amf0StrictArray struct {
	amf0ObjectBase
	count uint32
}

func NewAmf0StrictArray() *amf0StrictArray {
	v := &amf0StrictArray{}
	v.properties = []*amf0Property{}
	return v
}

func (v *amf0StrictArray) amf0Marker() amf0Marker {
	return amf0MarkerStrictArray
}

func (v *amf0StrictArray) Size() int {
	return int(1) + 4 + v.amf0ObjectBase.Size()
}

func (v *amf0StrictArray) UnmarshalBinary(data []byte) (err error) {
	var p []byte
	if p = data; len(p) < 5 {
		return errors.Errorf("require 5 bytes only %v", len(p))
	}
	if m := amf0Marker(p[0]); m != amf0MarkerStrictArray {
		return errors.Errorf("StrictArray amf0Marker %v is illegal", m)
	}
	v.count = binary.BigEndian.Uint32(p[1:])
	p = p[5:]

	if int(v.count) <= 0 {
		return
	}

	if err = v.unmarshal(p, false, int(v.count)); err != nil {
		return errors.WithMessage(err, "unmarshal")
	}
	return
}

func (v *amf0StrictArray) MarshalBinary() (data []byte, err error) {
	b := createBuffer()

	if err = b.WriteByte(byte(amf0MarkerStrictArray)); err != nil {
		return nil, errors.Wrap(err, "marshal")
	}

	if err = binary.Write(b, binary.BigEndian, v.count); err != nil {
		return nil, errors.Wrap(err, "marshal")
	}

	if err = v.marshal(b); err != nil {
		return nil, errors.WithMessage(err, "marshal")
	}

	return b.Bytes(), nil
}

// The single amf0Marker object, for all AMF0 which only has the amf0Marker, like null and undefined.
type amf0SingleMarkerObject struct {
	target amf0Marker
}

func newAmf0SingleMarkerObject(m amf0Marker) amf0SingleMarkerObject {
	return amf0SingleMarkerObject{target: m}
}

func (v *amf0SingleMarkerObject) amf0Marker() amf0Marker {
	return v.target
}

func (v *amf0SingleMarkerObject) Size() int {
	return int(1)
}

func (v *amf0SingleMarkerObject) UnmarshalBinary(data []byte) (err error) {
	var p []byte
	if p = data; len(p) < 1 {
		return errors.Errorf("require 1 byte only %v", len(p))
	}
	if m := amf0Marker(p[0]); m != v.target {
		return errors.Errorf("%v amf0Marker %v is illegal", v.target, m)
	}
	return
}

func (v *amf0SingleMarkerObject) MarshalBinary() (data []byte, err error) {
	return []byte{byte(v.target)}, nil
}

// The AMF0 null, please read @doc amf0_spec_121207.pdf, @page 6, @section 2.7 null Type
type amf0Null struct {
	amf0SingleMarkerObject
}

func NewAmf0Null() *amf0Null {
	v := amf0Null{}
	v.amf0SingleMarkerObject = newAmf0SingleMarkerObject(amf0MarkerNull)
	return &v
}

// The AMF0 undefined, please read @doc amf0_spec_121207.pdf, @page 6, @section 2.8 undefined Type
type amf0Undefined struct {
	amf0SingleMarkerObject
}

func NewAmf0Undefined() amf0Any {
	v := amf0Undefined{}
	v.amf0SingleMarkerObject = newAmf0SingleMarkerObject(amf0MarkerUndefined)
	return &v
}

// The AMF0 boolean, please read @doc amf0_spec_121207.pdf, @page 5, @section 2.3 Boolean Type
type amf0Boolean bool

func NewAmf0Boolean(b bool) amf0Any {
	v := amf0Boolean(b)
	return &v
}

func (v *amf0Boolean) amf0Marker() amf0Marker {
	return amf0MarkerBoolean
}

func (v *amf0Boolean) Size() int {
	return int(2)
}

func (v *amf0Boolean) UnmarshalBinary(data []byte) (err error) {
	var p []byte
	if p = data; len(p) < 2 {
		return errors.Errorf("require 2 bytes only %v", len(p))
	}
	if m := amf0Marker(p[0]); m != amf0MarkerBoolean {
		return errors.Errorf("BOOL amf0Marker %v is illegal", m)
	}
	if p[1] == 0 {
		*v = false
	} else {
		*v = true
	}
	return
}

func (v *amf0Boolean) MarshalBinary() (data []byte, err error) {
	var b byte
	if *v {
		b = 1
	}
	return []byte{byte(amf0MarkerBoolean), b}, nil
}
