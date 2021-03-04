package sctp

import (
	"crypto/rand"
	"fmt"
)

type paramStateCookie struct {
	paramHeader
	cookie []byte
}

func newRandomStateCookie() (*paramStateCookie, error) {
	randCookie := make([]byte, 32)
	_, err := rand.Read(randCookie)
	// crypto/rand.Read returns n == len(b) if and only if err == nil.
	if err != nil {
		return nil, err
	}

	s := &paramStateCookie{
		cookie: randCookie,
	}

	return s, nil
}

func (s *paramStateCookie) marshal() ([]byte, error) {
	s.typ = stateCookie
	s.raw = s.cookie
	return s.paramHeader.marshal()
}

func (s *paramStateCookie) unmarshal(raw []byte) (param, error) {
	err := s.paramHeader.unmarshal(raw)
	if err != nil {
		return nil, err
	}
	s.cookie = s.raw
	return s, nil
}

// String makes paramStateCookie printable
func (s *paramStateCookie) String() string {
	return fmt.Sprintf("%s: %s", s.paramHeader, s.cookie)
}
