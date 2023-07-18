// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package atomic contains custom atomic types
package atomic

import "sync/atomic"

// Error is an atomic error
type Error struct {
	v atomic.Value
}

// Store updates the value of the atomic variable
func (a *Error) Store(err error) {
	a.v.Store(struct{ error }{err})
}

// Load retrieves the current value of the atomic variable
func (a *Error) Load() error {
	err, _ := a.v.Load().(struct{ error })
	return err.error
}
