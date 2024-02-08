// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

// CandidatePairState represent the ICE candidate pair state
type CandidatePairState int

const (
	// CandidatePairStateWaiting means a check has not been performed for
	// this pair
	CandidatePairStateWaiting = iota + 1

	// CandidatePairStateInProgress means a check has been sent for this pair,
	// but the transaction is in progress.
	CandidatePairStateInProgress

	// CandidatePairStateFailed means a check for this pair was already done
	// and failed, either never producing any response or producing an unrecoverable
	// failure response.
	CandidatePairStateFailed

	// CandidatePairStateSucceeded means a check for this pair was already
	// done and produced a successful result.
	CandidatePairStateSucceeded
)

func (c CandidatePairState) String() string {
	switch c {
	case CandidatePairStateWaiting:
		return "waiting"
	case CandidatePairStateInProgress:
		return "in-progress"
	case CandidatePairStateFailed:
		return "failed"
	case CandidatePairStateSucceeded:
		return "succeeded"
	}
	return "Unknown candidate pair state"
}
