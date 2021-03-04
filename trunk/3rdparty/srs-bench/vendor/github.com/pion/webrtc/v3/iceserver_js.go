// +build js,wasm

package webrtc

import (
	"errors"

	"github.com/pion/ice/v2"
)

// ICEServer describes a single STUN and TURN server that can be used by
// the ICEAgent to establish a connection with a peer.
type ICEServer struct {
	URLs     []string
	Username string
	// Note: TURN is not supported in the WASM bindings yet
	Credential     interface{}
	CredentialType ICECredentialType
}

func (s ICEServer) parseURL(i int) (*ice.URL, error) {
	return ice.ParseURL(s.URLs[i])
}

func (s ICEServer) validate() ([]*ice.URL, error) {
	urls := []*ice.URL{}

	for i := range s.URLs {
		url, err := s.parseURL(i)
		if err != nil {
			return nil, err
		}

		if url.Scheme == ice.SchemeTypeTURN || url.Scheme == ice.SchemeTypeTURNS {
			return nil, errors.New("TURN is not currently supported in the JavaScript/Wasm bindings")
		}

		urls = append(urls, url)
	}

	return urls, nil
}
