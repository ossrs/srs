// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build !js
// +build !js

package webrtc

// GetConnectionStats is a helper method to return the associated stats for a given PeerConnection
func (r StatsReport) GetConnectionStats(conn *PeerConnection) (PeerConnectionStats, bool) {
	statsID := conn.getStatsID()
	stats, ok := r[statsID]
	if !ok {
		return PeerConnectionStats{}, false
	}

	pcStats, ok := stats.(PeerConnectionStats)
	if !ok {
		return PeerConnectionStats{}, false
	}
	return pcStats, true
}

// GetDataChannelStats is a helper method to return the associated stats for a given DataChannel
func (r StatsReport) GetDataChannelStats(dc *DataChannel) (DataChannelStats, bool) {
	statsID := dc.getStatsID()
	stats, ok := r[statsID]
	if !ok {
		return DataChannelStats{}, false
	}

	dcStats, ok := stats.(DataChannelStats)
	if !ok {
		return DataChannelStats{}, false
	}
	return dcStats, true
}

// GetICECandidateStats is a helper method to return the associated stats for a given ICECandidate
func (r StatsReport) GetICECandidateStats(c *ICECandidate) (ICECandidateStats, bool) {
	statsID := c.statsID
	stats, ok := r[statsID]
	if !ok {
		return ICECandidateStats{}, false
	}

	candidateStats, ok := stats.(ICECandidateStats)
	if !ok {
		return ICECandidateStats{}, false
	}
	return candidateStats, true
}

// GetICECandidatePairStats is a helper method to return the associated stats for a given ICECandidatePair
func (r StatsReport) GetICECandidatePairStats(c *ICECandidatePair) (ICECandidatePairStats, bool) {
	statsID := c.statsID
	stats, ok := r[statsID]
	if !ok {
		return ICECandidatePairStats{}, false
	}

	candidateStats, ok := stats.(ICECandidatePairStats)
	if !ok {
		return ICECandidatePairStats{}, false
	}
	return candidateStats, true
}

// GetCertificateStats is a helper method to return the associated stats for a given Certificate
func (r StatsReport) GetCertificateStats(c *Certificate) (CertificateStats, bool) {
	statsID := c.statsID
	stats, ok := r[statsID]
	if !ok {
		return CertificateStats{}, false
	}

	certificateStats, ok := stats.(CertificateStats)
	if !ok {
		return CertificateStats{}, false
	}
	return certificateStats, true
}

// GetCodecStats is a helper method to return the associated stats for a given Codec
func (r StatsReport) GetCodecStats(c *RTPCodecParameters) (CodecStats, bool) {
	statsID := c.statsID
	stats, ok := r[statsID]
	if !ok {
		return CodecStats{}, false
	}

	codecStats, ok := stats.(CodecStats)
	if !ok {
		return CodecStats{}, false
	}
	return codecStats, true
}
