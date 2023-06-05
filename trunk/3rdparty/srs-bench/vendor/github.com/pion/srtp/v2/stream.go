// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package srtp

type readStream interface {
	init(child streamSession, ssrc uint32) error

	Read(buf []byte) (int, error)
	GetSSRC() uint32
}
