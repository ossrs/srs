package dtls

import ( //nolint:gci
	"crypto"
	"crypto/md5"  //nolint:gosec
	"crypto/sha1" //nolint:gosec
	"crypto/sha256"
	"crypto/sha512"
)

// hashAlgorithm is used to indicate the hash algorithm used
// https://www.iana.org/assignments/tls-parameters/tls-parameters.xhtml#tls-parameters-18
type hashAlgorithm uint16

// Supported hash hash algorithms
const (
	hashAlgorithmMD2     hashAlgorithm = 0 // Blacklisted
	hashAlgorithmMD5     hashAlgorithm = 1 // Blacklisted
	hashAlgorithmSHA1    hashAlgorithm = 2 // Blacklisted
	hashAlgorithmSHA224  hashAlgorithm = 3
	hashAlgorithmSHA256  hashAlgorithm = 4
	hashAlgorithmSHA384  hashAlgorithm = 5
	hashAlgorithmSHA512  hashAlgorithm = 6
	hashAlgorithmEd25519 hashAlgorithm = 8
)

// String makes hashAlgorithm printable
func (h hashAlgorithm) String() string {
	switch h {
	case hashAlgorithmMD2:
		return "md2"
	case hashAlgorithmMD5:
		return "md5" // [RFC3279]
	case hashAlgorithmSHA1:
		return "sha-1" // [RFC3279]
	case hashAlgorithmSHA224:
		return "sha-224" // [RFC4055]
	case hashAlgorithmSHA256:
		return "sha-256" // [RFC4055]
	case hashAlgorithmSHA384:
		return "sha-384" // [RFC4055]
	case hashAlgorithmSHA512:
		return "sha-512" // [RFC4055]
	case hashAlgorithmEd25519:
		return "null"
	default:
		return "unknown or unsupported hash algorithm"
	}
}

func (h hashAlgorithm) digest(b []byte) []byte {
	switch h {
	case hashAlgorithmMD5:
		hash := md5.Sum(b) // #nosec
		return hash[:]
	case hashAlgorithmSHA1:
		hash := sha1.Sum(b) // #nosec
		return hash[:]
	case hashAlgorithmSHA224:
		hash := sha256.Sum224(b)
		return hash[:]
	case hashAlgorithmSHA256:
		hash := sha256.Sum256(b)
		return hash[:]
	case hashAlgorithmSHA384:
		hash := sha512.Sum384(b)
		return hash[:]
	case hashAlgorithmSHA512:
		hash := sha512.Sum512(b)
		return hash[:]
	default:
		return nil
	}
}

func (h hashAlgorithm) insecure() bool {
	switch h {
	case hashAlgorithmMD2, hashAlgorithmMD5, hashAlgorithmSHA1:
		return true
	default:
		return false
	}
}

func (h hashAlgorithm) cryptoHash() crypto.Hash {
	switch h {
	case hashAlgorithmMD5:
		return crypto.MD5
	case hashAlgorithmSHA1:
		return crypto.SHA1
	case hashAlgorithmSHA224:
		return crypto.SHA224
	case hashAlgorithmSHA256:
		return crypto.SHA256
	case hashAlgorithmSHA384:
		return crypto.SHA384
	case hashAlgorithmSHA512:
		return crypto.SHA512
	case hashAlgorithmEd25519:
		return crypto.Hash(0)
	default:
		return crypto.Hash(0)
	}
}

func hashAlgorithms() map[hashAlgorithm]struct{} {
	return map[hashAlgorithm]struct{}{
		hashAlgorithmMD5:     {},
		hashAlgorithmSHA1:    {},
		hashAlgorithmSHA224:  {},
		hashAlgorithmSHA256:  {},
		hashAlgorithmSHA384:  {},
		hashAlgorithmSHA512:  {},
		hashAlgorithmEd25519: {},
	}
}
