// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import "fmt"

// CandidateRelatedAddress convey transport addresses related to the
// candidate, useful for diagnostics and other purposes.
type CandidateRelatedAddress struct {
	Address string
	Port    int
}

// String makes CandidateRelatedAddress printable
func (c *CandidateRelatedAddress) String() string {
	if c == nil {
		return ""
	}

	return fmt.Sprintf(" related %s:%d", c.Address, c.Port)
}

// Equal allows comparing two CandidateRelatedAddresses.
// The CandidateRelatedAddress are allowed to be nil.
func (c *CandidateRelatedAddress) Equal(other *CandidateRelatedAddress) bool {
	if c == nil && other == nil {
		return true
	}
	return c != nil && other != nil &&
		c.Address == other.Address &&
		c.Port == other.Port
}
