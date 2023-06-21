// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package clientcertificate provides all the support Client Certificate types
package clientcertificate

// Type is used to communicate what
// type of certificate is being transported
//
// https://www.iana.org/assignments/tls-parameters/tls-parameters.xhtml#tls-parameters-2
type Type byte

// ClientCertificateType enums
const (
	RSASign   Type = 1
	ECDSASign Type = 64
)

// Types returns all valid ClientCertificate Types
func Types() map[Type]bool {
	return map[Type]bool{
		RSASign:   true,
		ECDSASign: true,
	}
}
