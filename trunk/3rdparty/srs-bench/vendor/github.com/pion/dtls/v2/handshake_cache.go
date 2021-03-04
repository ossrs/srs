package dtls

import (
	"sync"
)

type handshakeCacheItem struct {
	typ             handshakeType
	isClient        bool
	epoch           uint16
	messageSequence uint16
	data            []byte
}

type handshakeCachePullRule struct {
	typ      handshakeType
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

func (h *handshakeCache) push(data []byte, epoch, messageSequence uint16, typ handshakeType, isClient bool) bool { //nolint
	h.mu.Lock()
	defer h.mu.Unlock()

	for _, i := range h.cache {
		if i.messageSequence == messageSequence &&
			i.isClient == isClient {
			return false
		}
	}

	h.cache = append(h.cache, &handshakeCacheItem{
		data:            append([]byte{}, data...),
		epoch:           epoch,
		messageSequence: messageSequence,
		typ:             typ,
		isClient:        isClient,
	})
	return true
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
func (h *handshakeCache) fullPullMap(startSeq int, rules ...handshakeCachePullRule) (int, map[handshakeType]handshakeMessage, bool) {
	h.mu.Lock()
	defer h.mu.Unlock()

	ci := make(map[handshakeType]*handshakeCacheItem)
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
	out := make(map[handshakeType]handshakeMessage)
	seq := startSeq
	for _, r := range rules {
		t := r.typ
		i := ci[t]
		if i == nil {
			continue
		}
		rawHandshake := &handshake{}
		if err := rawHandshake.Unmarshal(i.data); err != nil {
			return startSeq, nil, false
		}
		if uint16(seq) != rawHandshake.handshakeHeader.messageSequence {
			// There is a gap. Some messages are not arrived.
			return startSeq, nil, false
		}
		seq++
		out[t] = rawHandshake.handshakeMessage
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
func (h *handshakeCache) sessionHash(hf hashFunc, epoch uint16, additional ...[]byte) ([]byte, error) {
	merged := []byte{}

	// Order defined by https://tools.ietf.org/html/rfc5246#section-7.3
	handshakeBuffer := h.pull(
		handshakeCachePullRule{handshakeTypeClientHello, epoch, true, false},
		handshakeCachePullRule{handshakeTypeServerHello, epoch, false, false},
		handshakeCachePullRule{handshakeTypeCertificate, epoch, false, false},
		handshakeCachePullRule{handshakeTypeServerKeyExchange, epoch, false, false},
		handshakeCachePullRule{handshakeTypeCertificateRequest, epoch, false, false},
		handshakeCachePullRule{handshakeTypeServerHelloDone, epoch, false, false},
		handshakeCachePullRule{handshakeTypeCertificate, epoch, true, false},
		handshakeCachePullRule{handshakeTypeClientKeyExchange, epoch, true, false},
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
