// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package rtmp

import (
	"encoding/binary"
	"errors"
	"fmt"
	"math"
	"sort"
)

// AMF0 markers.
const (
	amf0Number      = 0x00
	amf0Boolean     = 0x01
	amf0String      = 0x02
	amf0Object      = 0x03
	amf0Null        = 0x05
	amf0Undefined   = 0x06
	amf0ECMAArray   = 0x08
	amf0ObjectEnd   = 0x09
	amf0StrictArray = 0x0a
	amf0Date        = 0x0b
	amf0LongString  = 0x0c
)

// Undefined is the AMF0 undefined value.
type Undefined struct{}

// ECMAArray is an AMF0 ECMA array, such as the onMetaData values. An AMF0
// object is a map[string]any.
type ECMAArray map[string]any

// EncodeAMF0 encodes values in AMF0: a float64 or any integer as a number, a
// bool, a string, nil as null, Undefined, a map[string]any as an object with
// sorted keys, an ECMAArray and a []any as a strict array.
func EncodeAMF0(values ...any) ([]byte, error) {
	var b []byte
	for _, v := range values {
		var err error
		if b, err = appendAMF0(b, v); err != nil {
			return nil, err
		}
	}
	return b, nil
}

func appendAMF0(b []byte, v any) ([]byte, error) {
	switch v := v.(type) {
	case nil:
		return append(b, amf0Null), nil
	case Undefined:
		return append(b, amf0Undefined), nil
	case bool:
		if v {
			return append(b, amf0Boolean, 1), nil
		}
		return append(b, amf0Boolean, 0), nil
	case string:
		if len(v) > math.MaxUint16 {
			b = append(b, amf0LongString)
			b = binary.BigEndian.AppendUint32(b, uint32(len(v)))
			return append(b, v...), nil
		}
		b = append(b, amf0String)
		return appendUTF8(b, v), nil
	case map[string]any:
		b = append(b, amf0Object)
		return appendProperties(b, v)
	case ECMAArray:
		b = append(b, amf0ECMAArray)
		b = binary.BigEndian.AppendUint32(b, uint32(len(v)))
		return appendProperties(b, v)
	case []any:
		b = append(b, amf0StrictArray)
		b = binary.BigEndian.AppendUint32(b, uint32(len(v)))
		for _, e := range v {
			var err error
			if b, err = appendAMF0(b, e); err != nil {
				return nil, err
			}
		}
		return b, nil
	}
	if n, ok := number(v); ok {
		b = append(b, amf0Number)
		return binary.BigEndian.AppendUint64(b, math.Float64bits(n)), nil
	}
	return nil, fmt.Errorf("AMF0 can't encode %T", v)
}

func number(v any) (float64, bool) {
	switch v := v.(type) {
	case float64:
		return v, true
	case float32:
		return float64(v), true
	case int:
		return float64(v), true
	case int32:
		return float64(v), true
	case int64:
		return float64(v), true
	case uint8:
		return float64(v), true
	case uint32:
		return float64(v), true
	case uint64:
		return float64(v), true
	}
	return 0, false
}

func appendUTF8(b []byte, s string) []byte {
	b = binary.BigEndian.AppendUint16(b, uint16(len(s)))
	return append(b, s...)
}

func appendProperties(b []byte, m map[string]any) ([]byte, error) {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	for _, k := range keys {
		b = appendUTF8(b, k)
		var err error
		if b, err = appendAMF0(b, m[k]); err != nil {
			return nil, err
		}
	}
	return append(b, 0, 0, amf0ObjectEnd), nil
}

var errShortAMF0 = errors.New("short AMF0 value")

// DecodeAMF0 decodes every AMF0 value in b: a number or a date as float64, a
// bool, a string, null as nil, Undefined, an object as map[string]any, an
// ECMAArray and a strict array as []any.
func DecodeAMF0(b []byte) ([]any, error) {
	var values []any
	for len(b) > 0 {
		v, rest, err := decodeAMF0(b)
		if err != nil {
			return values, err
		}
		values = append(values, v)
		b = rest
	}
	return values, nil
}

func decodeAMF0(b []byte) (any, []byte, error) {
	if len(b) < 1 {
		return nil, nil, errShortAMF0
	}
	marker, b := b[0], b[1:]
	switch marker {
	case amf0Number:
		if len(b) < 8 {
			return nil, nil, errShortAMF0
		}
		return math.Float64frombits(binary.BigEndian.Uint64(b)), b[8:], nil
	case amf0Date:
		if len(b) < 10 {
			return nil, nil, errShortAMF0
		}
		return math.Float64frombits(binary.BigEndian.Uint64(b)), b[10:], nil
	case amf0Boolean:
		if len(b) < 1 {
			return nil, nil, errShortAMF0
		}
		return b[0] != 0, b[1:], nil
	case amf0String:
		s, rest, err := decodeUTF8(b)
		return s, rest, err
	case amf0LongString:
		if len(b) < 4 {
			return nil, nil, errShortAMF0
		}
		n := binary.BigEndian.Uint32(b)
		if uint64(len(b)-4) < uint64(n) {
			return nil, nil, errShortAMF0
		}
		return string(b[4 : 4+n]), b[4+n:], nil
	case amf0Null:
		return nil, b, nil
	case amf0Undefined:
		return Undefined{}, b, nil
	case amf0Object:
		return decodeProperties(b)
	case amf0ECMAArray:
		if len(b) < 4 {
			return nil, nil, errShortAMF0
		}
		m, rest, err := decodeProperties(b[4:])
		return ECMAArray(m), rest, err
	case amf0StrictArray:
		if len(b) < 4 {
			return nil, nil, errShortAMF0
		}
		n := binary.BigEndian.Uint32(b)
		b = b[4:]
		arr := []any{}
		for i := uint32(0); i < n; i++ {
			v, rest, err := decodeAMF0(b)
			if err != nil {
				return nil, nil, err
			}
			arr = append(arr, v)
			b = rest
		}
		return arr, b, nil
	}
	return nil, nil, fmt.Errorf("unknown AMF0 marker 0x%02x", marker)
}

func decodeUTF8(b []byte) (string, []byte, error) {
	if len(b) < 2 {
		return "", nil, errShortAMF0
	}
	n := int(binary.BigEndian.Uint16(b))
	if len(b)-2 < n {
		return "", nil, errShortAMF0
	}
	return string(b[2 : 2+n]), b[2+n:], nil
}

// decodeProperties decodes the properties of an object, up to its end marker.
func decodeProperties(b []byte) (map[string]any, []byte, error) {
	m := map[string]any{}
	for {
		if len(b) >= 3 && b[0] == 0 && b[1] == 0 && b[2] == amf0ObjectEnd {
			return m, b[3:], nil
		}
		k, rest, err := decodeUTF8(b)
		if err != nil {
			return nil, nil, err
		}
		v, rest, err := decodeAMF0(rest)
		if err != nil {
			return nil, nil, err
		}
		m[k] = v
		b = rest
	}
}
