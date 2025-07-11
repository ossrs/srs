// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package vnet

type timeoutError struct {
	msg string
}

func newTimeoutError(msg string) error {
	return &timeoutError{
		msg: msg,
	}
}

func (e *timeoutError) Error() string {
	return e.msg
}

func (e *timeoutError) Timeout() bool {
	return true
}
