// Copyright (c) 2025 Winlin
//
// SPDX-License-Identifier: MIT
package main

import "fmt"

func VersionMajor() int {
	return 1
}

// VersionMinor specifies the typical version of SRS we adapt to.
func VersionMinor() int {
	return 5
}

func VersionRevision() int {
	return 0
}

func Version() string {
	return fmt.Sprintf("%v.%v.%v", VersionMajor(), VersionMinor(), VersionRevision())
}

func Signature() string {
	return "SRSProxy"
}
