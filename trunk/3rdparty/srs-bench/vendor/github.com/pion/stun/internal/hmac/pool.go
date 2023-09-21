// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package hmac

import ( //nolint:gci
	"crypto/sha1" //nolint:gosec
	"crypto/sha256"
	"hash"
	"sync"
)

func (h *hmac) resetTo(key []byte) {
	h.outer.Reset()
	h.inner.Reset()
	blocksize := h.inner.BlockSize()

	// Reset size and zero of ipad and opad.
	h.ipad = append(h.ipad[:0], make([]byte, blocksize)...)
	h.opad = append(h.opad[:0], make([]byte, blocksize)...)

	if len(key) > blocksize {
		// If key is too big, hash it.
		h.outer.Write(key) //nolint:errcheck,gosec
		key = h.outer.Sum(nil)
	}
	copy(h.ipad, key)
	copy(h.opad, key)
	for i := range h.ipad {
		h.ipad[i] ^= 0x36
	}
	for i := range h.opad {
		h.opad[i] ^= 0x5c
	}
	h.inner.Write(h.ipad) //nolint:errcheck,gosec

	h.marshaled = false
}

var hmacSHA1Pool = &sync.Pool{ //nolint:gochecknoglobals
	New: func() interface{} {
		h := New(sha1.New, make([]byte, sha1.BlockSize))
		return h
	},
}

// AcquireSHA1 returns new HMAC from pool.
func AcquireSHA1(key []byte) hash.Hash {
	h := hmacSHA1Pool.Get().(*hmac) //nolint:forcetypeassert
	assertHMACSize(h, sha1.Size, sha1.BlockSize)
	h.resetTo(key)
	return h
}

// PutSHA1 puts h to pool.
func PutSHA1(h hash.Hash) {
	hm := h.(*hmac) //nolint:forcetypeassert
	assertHMACSize(hm, sha1.Size, sha1.BlockSize)
	hmacSHA1Pool.Put(hm)
}

var hmacSHA256Pool = &sync.Pool{ //nolint:gochecknoglobals
	New: func() interface{} {
		h := New(sha256.New, make([]byte, sha256.BlockSize))
		return h
	},
}

// AcquireSHA256 returns new HMAC from SHA256 pool.
func AcquireSHA256(key []byte) hash.Hash {
	h := hmacSHA256Pool.Get().(*hmac) //nolint:forcetypeassert
	assertHMACSize(h, sha256.Size, sha256.BlockSize)
	h.resetTo(key)
	return h
}

// PutSHA256 puts h to SHA256 pool.
func PutSHA256(h hash.Hash) {
	hm := h.(*hmac) //nolint:forcetypeassert
	assertHMACSize(hm, sha256.Size, sha256.BlockSize)
	hmacSHA256Pool.Put(hm)
}

// assertHMACSize panics if h.size != size or h.blocksize != blocksize.
//
// Put and Acquire functions are internal functions to project, so
// checking it via such assert is optimal.
func assertHMACSize(h *hmac, size, blocksize int) { //nolint:unparam
	if h.Size() != size || h.BlockSize() != blocksize {
		panic("BUG: hmac size invalid") //nolint
	}
}
