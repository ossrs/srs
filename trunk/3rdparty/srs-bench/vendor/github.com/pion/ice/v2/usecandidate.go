package ice

import "github.com/pion/stun"

// UseCandidateAttr represents USE-CANDIDATE attribute.
type UseCandidateAttr struct{}

// AddTo adds USE-CANDIDATE attribute to message.
func (UseCandidateAttr) AddTo(m *stun.Message) error {
	m.Add(stun.AttrUseCandidate, nil)
	return nil
}

// IsSet returns true if USE-CANDIDATE attribute is set.
func (UseCandidateAttr) IsSet(m *stun.Message) bool {
	_, err := m.Get(stun.AttrUseCandidate)
	return err == nil
}

// UseCandidate is shorthand for UseCandidateAttr.
func UseCandidate() UseCandidateAttr {
	return UseCandidateAttr{}
}
