// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package signaturehash

import "errors"

var (
	errNoAvailableSignatureSchemes = errors.New("connection can not be created, no SignatureScheme satisfy this Config")
	errInvalidSignatureAlgorithm   = errors.New("invalid signature algorithm")
	errInvalidHashAlgorithm        = errors.New("invalid hash algorithm")
)
