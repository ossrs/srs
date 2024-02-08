// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package types

// AuthenticationType controls what authentication method is using during the handshake
type AuthenticationType int

// AuthenticationType Enums
const (
	AuthenticationTypeCertificate AuthenticationType = iota + 1
	AuthenticationTypePreSharedKey
	AuthenticationTypeAnonymous
)
