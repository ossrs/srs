// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package rtmp_test

import (
	"fmt"

	"srsx/internal/rtmp"
)

func ExampleAmf0Number() {
	number := rtmp.NewAmf0Number(3.14)
	b, err := number.MarshalBinary()
	if err != nil {
		panic(err)
	}

	value, err := rtmp.Amf0Discovery(b)
	if err != nil {
		panic(err)
	}
	if err := value.UnmarshalBinary(b); err != nil {
		panic(err)
	}

	converter := rtmp.NewAmf0Converter(value)
	fmt.Println("number:", converter.ToNumber().Float64())
	fmt.Println("is string:", converter.ToString() != nil)

	// Output:
	// number: 3.14
	// is string: false
}

func ExampleAmf0Object() {
	object := rtmp.NewAmf0Object().
		Set("code", rtmp.NewAmf0Number(100)).
		Set("level", rtmp.NewAmf0String("status"))
	b, err := object.MarshalBinary()
	if err != nil {
		panic(err)
	}

	value, err := rtmp.Amf0Discovery(b)
	if err != nil {
		panic(err)
	}
	if err := value.UnmarshalBinary(b); err != nil {
		panic(err)
	}

	converter := rtmp.NewAmf0Converter(value)
	fmt.Println("code:", rtmp.NewAmf0Converter(converter.ToObject().Get("code")).ToNumber().Float64())
	fmt.Println("level:", rtmp.NewAmf0Converter(converter.ToObject().Get("level")).ToString().String())
	fmt.Println("is number:", converter.ToNumber() != nil)

	// Output:
	// code: 100
	// level: status
	// is number: false
}
