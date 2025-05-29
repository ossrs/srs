// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package sctp

type chunk interface {
	unmarshal(raw []byte) error
	marshal() ([]byte, error)
	check() (bool, error)

	valueLength() int
}
