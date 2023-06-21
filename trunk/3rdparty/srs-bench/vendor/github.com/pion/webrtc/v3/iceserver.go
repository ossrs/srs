// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build !js
// +build !js

package webrtc

import (
	"encoding/json"

	"github.com/pion/stun"
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

func (s ICEServer) parseURL(i int) (*stun.URI, error) {
	return stun.ParseURI(s.URLs[i])
}

func (s ICEServer) validate() error {
	_, err := s.urls()
	return err
}

func (s ICEServer) urls() ([]*stun.URI, error) {
	urls := []*stun.URI{}

	for i := range s.URLs {
		url, err := s.parseURL(i)
		if err != nil {
			return nil, &rtcerr.InvalidAccessError{Err: err}
		}

		if url.Scheme == stun.SchemeTypeTURN || url.Scheme == stun.SchemeTypeTURNS {
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

func iceserverUnmarshalUrls(val interface{}) (*[]string, error) {
	s, ok := val.([]interface{})
	if !ok {
		return nil, errInvalidICEServer
	}
	out := make([]string, len(s))
	for idx, url := range s {
		out[idx], ok = url.(string)
		if !ok {
			return nil, errInvalidICEServer
		}
	}
	return &out, nil
}

func iceserverUnmarshalOauth(val interface{}) (*OAuthCredential, error) {
	c, ok := val.(map[string]interface{})
	if !ok {
		return nil, errInvalidICEServer
	}
	MACKey, ok := c["MACKey"].(string)
	if !ok {
		return nil, errInvalidICEServer
	}
	AccessToken, ok := c["AccessToken"].(string)
	if !ok {
		return nil, errInvalidICEServer
	}
	return &OAuthCredential{
		MACKey:      MACKey,
		AccessToken: AccessToken,
	}, nil
}

func (s *ICEServer) iceserverUnmarshalFields(m map[string]interface{}) error {
	if val, ok := m["urls"]; ok {
		u, err := iceserverUnmarshalUrls(val)
		if err != nil {
			return err
		}
		s.URLs = *u
	} else {
		s.URLs = []string{}
	}

	if val, ok := m["username"]; ok {
		s.Username, ok = val.(string)
		if !ok {
			return errInvalidICEServer
		}
	}
	if val, ok := m["credentialType"]; ok {
		ct, ok := val.(string)
		if !ok {
			return errInvalidICEServer
		}
		tpe, err := newICECredentialType(ct)
		if err != nil {
			return err
		}
		s.CredentialType = tpe
	} else {
		s.CredentialType = ICECredentialTypePassword
	}
	if val, ok := m["credential"]; ok {
		switch s.CredentialType {
		case ICECredentialTypePassword:
			s.Credential = val
		case ICECredentialTypeOauth:
			c, err := iceserverUnmarshalOauth(val)
			if err != nil {
				return err
			}
			s.Credential = *c
		default:
			return errInvalidICECredentialTypeString
		}
	}
	return nil
}

// UnmarshalJSON parses the JSON-encoded data and stores the result
func (s *ICEServer) UnmarshalJSON(b []byte) error {
	var tmp interface{}
	err := json.Unmarshal(b, &tmp)
	if err != nil {
		return err
	}
	if m, ok := tmp.(map[string]interface{}); ok {
		return s.iceserverUnmarshalFields(m)
	}
	return errInvalidICEServer
}

// MarshalJSON returns the JSON encoding
func (s ICEServer) MarshalJSON() ([]byte, error) {
	m := make(map[string]interface{})
	m["urls"] = s.URLs
	if s.Username != "" {
		m["username"] = s.Username
	}
	if s.Credential != nil {
		m["credential"] = s.Credential
	}
	m["credentialType"] = s.CredentialType
	return json.Marshal(m)
}
