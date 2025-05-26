// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build !js
// +build !js

package deadline

import (
	"time"
)

func afterFunc(d time.Duration, f func()) timer {
	return time.AfterFunc(d, f)
}
