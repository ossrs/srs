// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"context"
	"time"
)

// GetCandidatePairsStats returns a list of candidate pair stats.
func (a *Agent) GetCandidatePairsStats() []CandidatePairStats {
	var res []CandidatePairStats
	err := a.loop.Run(a.loop, func(_ context.Context) {
		result := make([]CandidatePairStats, 0, len(a.checklist))
		for _, cp := range a.checklist {
			stat := CandidatePairStats{
				Timestamp:         time.Now(),
				LocalCandidateID:  cp.Local.ID(),
				RemoteCandidateID: cp.Remote.ID(),
				State:             cp.state,
				Nominated:         cp.nominated,
				// PacketsSent uint32
				// PacketsReceived uint32
				// BytesSent uint64
				// BytesReceived uint64
				// LastPacketSentTimestamp time.Time
				// LastPacketReceivedTimestamp time.Time
				FirstRequestTimestamp:         cp.FirstRequestSentAt(),
				LastRequestTimestamp:          cp.LastRequestSentAt(),
				FirstResponseTimestamp:        cp.FirstReponseReceivedAt(),
				LastResponseTimestamp:         cp.LastResponseReceivedAt(),
				FirstRequestReceivedTimestamp: cp.FirstRequestReceivedAt(),
				LastRequestReceivedTimestamp:  cp.LastRequestReceivedAt(),

				TotalRoundTripTime:   cp.TotalRoundTripTime(),
				CurrentRoundTripTime: cp.CurrentRoundTripTime(),
				// AvailableOutgoingBitrate float64
				// AvailableIncomingBitrate float64
				// CircuitBreakerTriggerCount uint32
				RequestsReceived:  cp.RequestsReceived(),
				RequestsSent:      cp.RequestsSent(),
				ResponsesReceived: cp.ResponsesReceived(),
				ResponsesSent:     cp.ResponsesSent(),
				// RetransmissionsReceived uint64
				// RetransmissionsSent uint64
				// ConsentRequestsSent uint64
				// ConsentExpiredTimestamp time.Time
			}
			result = append(result, stat)
		}
		res = result
	})
	if err != nil {
		a.log.Errorf("Failed to get candidate pairs stats: %v", err)

		return []CandidatePairStats{}
	}

	return res
}

// GetSelectedCandidatePairStats returns a candidate pair stats for selected candidate pair.
// Returns false if there is no selected pair.
func (a *Agent) GetSelectedCandidatePairStats() (CandidatePairStats, bool) {
	isAvailable := false
	var res CandidatePairStats
	err := a.loop.Run(a.loop, func(_ context.Context) {
		sp := a.getSelectedPair()
		if sp == nil {
			return
		}

		isAvailable = true
		res = CandidatePairStats{
			Timestamp:         time.Now(),
			LocalCandidateID:  sp.Local.ID(),
			RemoteCandidateID: sp.Remote.ID(),
			State:             sp.state,
			Nominated:         sp.nominated,
			// PacketsSent uint32
			// PacketsReceived uint32
			// BytesSent uint64
			// BytesReceived uint64
			// LastPacketSentTimestamp time.Time
			// LastPacketReceivedTimestamp time.Time
			// FirstRequestTimestamp time.Time
			// LastRequestTimestamp time.Time
			// LastResponseTimestamp time.Time
			TotalRoundTripTime:   sp.TotalRoundTripTime(),
			CurrentRoundTripTime: sp.CurrentRoundTripTime(),
			// AvailableOutgoingBitrate float64
			// AvailableIncomingBitrate float64
			// CircuitBreakerTriggerCount uint32
			// RequestsReceived uint64
			// RequestsSent uint64
			ResponsesReceived: sp.ResponsesReceived(),
			// ResponsesSent uint64
			// RetransmissionsReceived uint64
			// RetransmissionsSent uint64
			// ConsentRequestsSent uint64
			// ConsentExpiredTimestamp time.Time
		}
	})
	if err != nil {
		a.log.Errorf("Failed to get selected candidate pair stats: %v", err)

		return CandidatePairStats{}, false
	}

	return res, isAvailable
}

// GetLocalCandidatesStats returns a list of local candidates stats.
func (a *Agent) GetLocalCandidatesStats() []CandidateStats {
	var res []CandidateStats
	err := a.loop.Run(a.loop, func(_ context.Context) {
		result := make([]CandidateStats, 0, len(a.localCandidates))
		for networkType, localCandidates := range a.localCandidates {
			for _, cand := range localCandidates {
				relayProtocol := ""
				if cand.Type() == CandidateTypeRelay {
					if cRelay, ok := cand.(*CandidateRelay); ok {
						relayProtocol = cRelay.RelayProtocol()
					}
				}
				stat := CandidateStats{
					Timestamp:     time.Now(),
					ID:            cand.ID(),
					NetworkType:   networkType,
					IP:            cand.Address(),
					Port:          cand.Port(),
					CandidateType: cand.Type(),
					Priority:      cand.Priority(),
					// URL string
					RelayProtocol: relayProtocol,
					// Deleted bool
				}
				result = append(result, stat)
			}
		}
		res = result
	})
	if err != nil {
		a.log.Errorf("Failed to get candidate pair stats: %v", err)

		return []CandidateStats{}
	}

	return res
}

// GetRemoteCandidatesStats returns a list of remote candidates stats.
func (a *Agent) GetRemoteCandidatesStats() []CandidateStats {
	var res []CandidateStats
	err := a.loop.Run(a.loop, func(_ context.Context) {
		result := make([]CandidateStats, 0, len(a.remoteCandidates))
		for networkType, remoteCandidates := range a.remoteCandidates {
			for _, c := range remoteCandidates {
				stat := CandidateStats{
					Timestamp:     time.Now(),
					ID:            c.ID(),
					NetworkType:   networkType,
					IP:            c.Address(),
					Port:          c.Port(),
					CandidateType: c.Type(),
					Priority:      c.Priority(),
					// URL string
					RelayProtocol: "",
				}
				result = append(result, stat)
			}
		}
		res = result
	})
	if err != nil {
		a.log.Errorf("Failed to get candidate pair stats: %v", err)

		return []CandidateStats{}
	}

	return res
}
