package webrtc

import "fmt"

// ICECandidatePair represents an ICE Candidate pair
type ICECandidatePair struct {
	statsID string
	Local   *ICECandidate
	Remote  *ICECandidate
}

func newICECandidatePairStatsID(localID, remoteID string) string {
	return fmt.Sprintf("%s-%s", localID, remoteID)
}

func (p *ICECandidatePair) String() string {
	return fmt.Sprintf("(local) %s <-> (remote) %s", p.Local, p.Remote)
}

// NewICECandidatePair returns an initialized *ICECandidatePair
// for the given pair of ICECandidate instances
func NewICECandidatePair(local, remote *ICECandidate) *ICECandidatePair {
	statsID := newICECandidatePairStatsID(local.statsID, remote.statsID)
	return &ICECandidatePair{
		statsID: statsID,
		Local:   local,
		Remote:  remote,
	}
}
