// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

import (
	"encoding/json"
	"fmt"
)

// ICECredentialType indicates the type of credentials used to connect to
// an ICE server.
type ICECredentialType int

const (
	// ICECredentialTypePassword describes username and password based
	// credentials as described in https://tools.ietf.org/html/rfc5389.
	ICECredentialTypePassword ICECredentialType = iota

	// ICECredentialTypeOauth describes token based credential as described
	// in https://tools.ietf.org/html/rfc7635.
	ICECredentialTypeOauth
)

// This is done this way because of a linter.
const (
	iceCredentialTypePasswordStr = "password"
	iceCredentialTypeOauthStr    = "oauth"
)

func newICECredentialType(raw string) (ICECredentialType, error) {
	switch raw {
	case iceCredentialTypePasswordStr:
		return ICECredentialTypePassword, nil
	case iceCredentialTypeOauthStr:
		return ICECredentialTypeOauth, nil
	default:
		return ICECredentialTypePassword, errInvalidICECredentialTypeString
	}
}

func (t ICECredentialType) String() string {
	switch t {
	case ICECredentialTypePassword:
		return iceCredentialTypePasswordStr
	case ICECredentialTypeOauth:
		return iceCredentialTypeOauthStr
	default:
		return ErrUnknownType.Error()
	}
}

// UnmarshalJSON parses the JSON-encoded data and stores the result
func (t *ICECredentialType) UnmarshalJSON(b []byte) error {
	var val string
	if err := json.Unmarshal(b, &val); err != nil {
		return err
	}

	tmp, err := newICECredentialType(val)
	if err != nil {
		return fmt.Errorf("%w: (%s)", err, val)
	}

	*t = tmp
	return nil
}

// MarshalJSON returns the JSON encoding
func (t ICECredentialType) MarshalJSON() ([]byte, error) {
	return json.Marshal(t.String())
}
