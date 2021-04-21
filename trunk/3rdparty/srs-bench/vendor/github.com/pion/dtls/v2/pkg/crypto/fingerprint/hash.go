package fingerprint

import (
	"crypto"
	"errors"
)

var errInvalidHashAlgorithm = errors.New("fingerprint: invalid hash algorithm")

func nameToHash() map[string]crypto.Hash {
	return map[string]crypto.Hash{
		"md5":     crypto.MD5,    // [RFC3279]
		"sha-1":   crypto.SHA1,   // [RFC3279]
		"sha-224": crypto.SHA224, // [RFC4055]
		"sha-256": crypto.SHA256, // [RFC4055]
		"sha-384": crypto.SHA384, // [RFC4055]
		"sha-512": crypto.SHA512, // [RFC4055]
	}
}

// HashFromString allows looking up a hash algorithm by it's string representation
func HashFromString(s string) (crypto.Hash, error) {
	if h, ok := nameToHash()[s]; ok {
		return h, nil
	}
	return 0, errInvalidHashAlgorithm
}

// StringFromHash allows looking up a string representation of the crypto.Hash.
func StringFromHash(hash crypto.Hash) (string, error) {
	for s, h := range nameToHash() {
		if h == hash {
			return s, nil
		}
	}
	return "", errInvalidHashAlgorithm
}
