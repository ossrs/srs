// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package version

import "fmt"

func VersionMajor() int {
	return 8
}

func VersionMinor() int {
	return 0
}

func VersionRevision() int {
	return 78
}

func Version() string {
	return fmt.Sprintf("%v.%v.%v", VersionMajor(), VersionMinor(), VersionRevision())
}

func Signature() string {
	return "SRSX"
}
