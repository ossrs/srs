// +build !js

package webrtc

import (
	"github.com/pion/ice/v2"
	"github.com/pion/webrtc/v3/pkg/rtcerr"
)

// ICEServer describes a single STUN and TURN server that can be used by
// the ICEAgent to establish a connection with a peer.
type ICEServer struct {
	URLs           []string          `json:"urls"`
	Username       string            `json:"username,omitempty"`
	Credential     interface{}       `json:"credential,omitempty"`
	CredentialType ICECredentialType `json:"credentialType,omitempty"`
}

func (s ICEServer) parseURL(i int) (*ice.URL, error) {
	return ice.ParseURL(s.URLs[i])
}

func (s ICEServer) validate() error {
	_, err := s.urls()
	return err
}

func (s ICEServer) urls() ([]*ice.URL, error) {
	urls := []*ice.URL{}

	for i := range s.URLs {
		url, err := s.parseURL(i)
		if err != nil {
			return nil, &rtcerr.InvalidAccessError{Err: err}
		}

		if url.Scheme == ice.SchemeTypeTURN || url.Scheme == ice.SchemeTypeTURNS {
			// https://www.w3.org/TR/webrtc/#set-the-configuration (step #11.3.2)
			if s.Username == "" || s.Credential == nil {
				return nil, &rtcerr.InvalidAccessError{Err: ErrNoTurnCredentials}
			}
			url.Username = s.Username

			switch s.CredentialType {
			case ICECredentialTypePassword:
				// https://www.w3.org/TR/webrtc/#set-the-configuration (step #11.3.3)
				password, ok := s.Credential.(string)
				if !ok {
					return nil, &rtcerr.InvalidAccessError{Err: ErrTurnCredentials}
				}
				url.Password = password

			case ICECredentialTypeOauth:
				// https://www.w3.org/TR/webrtc/#set-the-configuration (step #11.3.4)
				if _, ok := s.Credential.(OAuthCredential); !ok {
					return nil, &rtcerr.InvalidAccessError{Err: ErrTurnCredentials}
				}

			default:
				return nil, &rtcerr.InvalidAccessError{Err: ErrTurnCredentials}
			}
		}

		urls = append(urls, url)
	}

	return urls, nil
}
