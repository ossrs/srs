// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

import "fmt"

func VersionMajor() int {
	return 1
}

func VersionMinor() int {
	return 0
}

func VersionRevision() int {
	return 0
}

func Version() string {
	return fmt.Sprintf("%v.%v.%v", VersionMajor(), VersionMinor(), VersionRevision())
}

func Signature() string {
	return "GoProxy"
}
