// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package rtmp

import (
	"bytes"
	"reflect"
	"strings"
	"testing"
)

func TestAMF0RoundTrip(t *testing.T) {
	long := strings.Repeat("x", 70000)
	values := []any{
		float64(1.5),
		true,
		false,
		"connect",
		long,
		nil,
		Undefined{},
		map[string]any{"app": "live", "n": float64(3), "ok": true, "sub": map[string]any{"a": nil}},
		ECMAArray{"duration": float64(30), "encoder": "Lavf"},
		[]any{float64(1), "two", nil},
	}
	b, err := EncodeAMF0(values...)
	if err != nil {
		t.Fatalf("encode: %v", err)
	}
	got, err := DecodeAMF0(b)
	if err != nil {
		t.Fatalf("decode: %v", err)
	}
	if !reflect.DeepEqual(got, values) {
		t.Errorf("round trip got %#v, want %#v", got, values)
	}
}

func TestAMF0EncodeBytes(t *testing.T) {
	cases := []struct {
		name  string
		value any
		want  []byte
	}{
		{"Number", float64(1), []byte{0x00, 0x3f, 0xf0, 0, 0, 0, 0, 0, 0}},
		{"Int", 1, []byte{0x00, 0x3f, 0xf0, 0, 0, 0, 0, 0, 0}},
		{"Bool", true, []byte{0x01, 0x01}},
		{"String", "ab", []byte{0x02, 0x00, 0x02, 'a', 'b'}},
		{"Null", nil, []byte{0x05}},
		{"Undefined", Undefined{}, []byte{0x06}},
		{"Object", map[string]any{"b": nil, "a": true}, []byte{0x03, 0, 1, 'a', 0x01, 0x01, 0, 1, 'b', 0x05, 0, 0, 0x09}},
		{"ECMAArray", ECMAArray{"a": nil}, []byte{0x08, 0, 0, 0, 1, 0, 1, 'a', 0x05, 0, 0, 0x09}},
		{"StrictArray", []any{true}, []byte{0x0a, 0, 0, 0, 1, 0x01, 0x01}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			got, err := EncodeAMF0(tc.value)
			if err != nil {
				t.Fatalf("encode: %v", err)
			}
			if !bytes.Equal(got, tc.want) {
				t.Errorf("got % x, want % x", got, tc.want)
			}
		})
	}
}

func TestAMF0EncodeUnsupported(t *testing.T) {
	if _, err := EncodeAMF0(struct{}{}); err == nil {
		t.Errorf("encode of a struct: no error")
	}
}

func TestAMF0DecodeDate(t *testing.T) {
	b := []byte{0x0b, 0x3f, 0xf0, 0, 0, 0, 0, 0, 0, 0, 0}
	got, err := DecodeAMF0(b)
	if err != nil || len(got) != 1 || got[0] != float64(1) {
		t.Errorf("date got %v, %v; want [1]", got, err)
	}
}

func TestAMF0DecodeErrors(t *testing.T) {
	cases := map[string][]byte{
		"ShortNumber":     {0x00, 0x3f},
		"ShortString":     {0x02, 0x00, 0x05, 'a'},
		"UnknownMarker":   {0x42},
		"ObjectNoEnd":     {0x03, 0, 1, 'a', 0x05},
		"ShortStrictSize": {0x0a, 0, 0},
	}
	for name, b := range cases {
		t.Run(name, func(t *testing.T) {
			if got, err := DecodeAMF0(b); err == nil {
				t.Errorf("decode % x got %v, want an error", b, got)
			}
		})
	}
}
