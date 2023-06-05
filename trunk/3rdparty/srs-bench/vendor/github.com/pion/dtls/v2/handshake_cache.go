// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package dtls

import (
	"sync"

	"github.com/pion/dtls/v2/pkg/crypto/prf"
	"github.com/pion/dtls/v2/pkg/protocol/handshake"
)

type handshakeCacheItem struct {
	typ             handshake.Type
	isClient        bool
	epoch           uint16
	messageSequence uint16
	data            []byte
}

type handshakeCachePullRule struct {
	typ      handshake.Type
	epoch    uint16
	isClient bool
	optional bool
}

type handshakeCache struct {
	cache []*handshakeCacheItem
	mu    sync.Mutex
}

func newHandshakeCache() *handshakeCache {
	return &handshakeCache{}
}

func (h *handshakeCache) push(data []byte, epoch, messageSequence uint16, typ handshake.Type, isClient bool) {
	h.mu.Lock()
	defer h.mu.Unlock()

	h.cache = append(h.cache, &handshakeCacheItem{
		data:            append([]byte{}, data...),
		epoch:           epoch,
		messageSequence: messageSequence,
		typ:             typ,
		isClient:        isClient,
	})
}

// returns a list handshakes that match the requested rules
// the list will contain null entries for rules that can't be satisfied
// multiple entries may match a rule, but only the last match is returned (ie ClientHello with cookies)
func (h *handshakeCache) pull(rules ...handshakeCachePullRule) []*handshakeCacheItem {
	h.mu.Lock()
	defer h.mu.Unlock()

	out := make([]*handshakeCacheItem, len(rules))
	for i, r := range rules {
		for _, c := range h.cache {
			if c.typ == r.typ && c.isClient == r.isClient && c.epoch == r.epoch {
				switch {
				case out[i] == nil:
					out[i] = c
				case out[i].messageSequence < c.messageSequence:
					out[i] = c
				}
			}
		}
	}

	return out
}

// fullPullMap pulls all handshakes between rules[0] to rules[len(rules)-1] as map.
func (h *handshakeCache) fullPullMap(startSeq int, cipherSuite CipherSuite, rules ...handshakeCachePullRule) (int, map[handshake.Type]handshake.Message, bool) {
	h.mu.Lock()
	defer h.mu.Unlock()

	ci := make(map[handshake.Type]*handshakeCacheItem)
	for _, r := range rules {
		var item *handshakeCacheItem
		for _, c := range h.cache {
			if c.typ == r.typ && c.isClient == r.isClient && c.epoch == r.epoch {
				switch {
				case item == nil:
					item = c
				case item.messageSequence < c.messageSequence:
					item = c
				}
			}
		}
		if !r.optional && item == nil {
			// Missing mandatory message.
			return startSeq, nil, false
		}
		ci[r.typ] = item
	}
	out := make(map[handshake.Type]handshake.Message)
	seq := startSeq
	for _, r := range rules {
		t := r.typ
		i := ci[t]
		if i == nil {
			continue
		}
		var keyExchangeAlgorithm CipherSuiteKeyExchangeAlgorithm
		if cipherSuite != nil {
			keyExchangeAlgorithm = cipherSuite.KeyExchangeAlgorithm()
		}
		rawHandshake := &handshake.Handshake{
			KeyExchangeAlgorithm: keyExchangeAlgorithm,
		}
		if err := rawHandshake.Unmarshal(i.data); err != nil {
			return startSeq, nil, false
		}
		if uint16(seq) != rawHandshake.Header.MessageSequence {
			// There is a gap. Some messages are not arrived.
			return startSeq, nil, false
		}
		seq++
		out[t] = rawHandshake.Message
	}
	return seq, out, true
}

// pullAndMerge calls pull and then merges the results, ignoring any null entries
func (h *handshakeCache) pullAndMerge(rules ...handshakeCachePullRule) []byte {
	merged := []byte{}

	for _, p := range h.pull(rules...) {
		if p != nil {
			merged = append(merged, p.data...)
		}
	}
	return merged
}

// sessionHash returns the session hash for Extended Master Secret support
// https://tools.ietf.org/html/draft-ietf-tls-session-hash-06#section-4
func (h *handshakeCache) sessionHash(hf prf.HashFunc, epoch uint16, additional ...[]byte) ([]byte, error) {
	merged := []byte{}

	// Order defined by https://tools.ietf.org/html/rfc5246#section-7.3
	handshakeBuffer := h.pull(
		handshakeCachePullRule{handshake.TypeClientHello, epoch, true, false},
		handshakeCachePullRule{handshake.TypeServerHello, epoch, false, false},
		handshakeCachePullRule{handshake.TypeCertificate, epoch, false, false},
		handshakeCachePullRule{handshake.TypeServerKeyExchange, epoch, false, false},
		handshakeCachePullRule{handshake.TypeCertificateRequest, epoch, false, false},
		handshakeCachePullRule{handshake.TypeServerHelloDone, epoch, false, false},
		handshakeCachePullRule{handshake.TypeCertificate, epoch, true, false},
		handshakeCachePullRule{handshake.TypeClientKeyExchange, epoch, true, false},
	)

	for _, p := range handshakeBuffer {
		if p == nil {
			continue
		}

		merged = append(merged, p.data...)
	}
	for _, a := range additional {
		merged = append(merged, a...)
	}

	hash := hf()
	if _, err := hash.Write(merged); err != nil {
		return []byte{}, err
	}

	return hash.Sum(nil), nil
}
