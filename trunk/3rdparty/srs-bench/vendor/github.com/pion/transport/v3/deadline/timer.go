// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package deadline

import (
	"time"
)

type timer interface {
	Stop() bool
	Reset(time.Duration) bool
}
