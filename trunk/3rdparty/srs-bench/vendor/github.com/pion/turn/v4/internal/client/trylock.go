// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package client

import (
	"sync/atomic"
)

// TryLock implement the classic  "try-lock" operation.
type TryLock struct {
	n int32
}

// Lock tries to lock the try-lock. If successful, it returns true.
// Otherwise, it returns false immediately.
func (c *TryLock) Lock() error {
	if !atomic.CompareAndSwapInt32(&c.n, 0, 1) {
		return errDoubleLock
	}
	return nil
}

// Unlock unlocks the try-lock.
func (c *TryLock) Unlock() {
	atomic.StoreInt32(&c.n, 0)
}
