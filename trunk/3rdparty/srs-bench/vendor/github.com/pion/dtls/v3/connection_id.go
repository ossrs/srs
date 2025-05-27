// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package dtls

import (
	"crypto/rand"

	"github.com/pion/dtls/v3/pkg/protocol"
	"github.com/pion/dtls/v3/pkg/protocol/extension"
	"github.com/pion/dtls/v3/pkg/protocol/handshake"
	"github.com/pion/dtls/v3/pkg/protocol/recordlayer"
)

// RandomCIDGenerator is a random Connection ID generator where CID is the
// specified size. Specifying a size of 0 will indicate to peers that sending a
// Connection ID is not necessary.
func RandomCIDGenerator(size int) func() []byte {
	return func() []byte {
		cid := make([]byte, size)
		if _, err := rand.Read(cid); err != nil {
			panic(err) //nolint -- nonrecoverable
		}

		return cid
	}
}

// OnlySendCIDGenerator enables sending Connection IDs negotiated with a peer,
// but indicates to the peer that sending Connection IDs in return is not
// necessary.
func OnlySendCIDGenerator() func() []byte {
	return func() []byte {
		return nil
	}
}

// cidDatagramRouter extracts connection IDs from incoming datagram payloads and
// uses them to route to the proper connection.
// NOTE: properly routing datagrams based on connection IDs requires using
// constant size connection IDs.
func cidDatagramRouter(size int) func([]byte) (string, bool) {
	return func(packet []byte) (string, bool) {
		pkts, err := recordlayer.ContentAwareUnpackDatagram(packet, size)
		if err != nil || len(pkts) < 1 {
			return "", false
		}
		for _, pkt := range pkts {
			h := &recordlayer.Header{
				ConnectionID: make([]byte, size),
			}
			if err := h.Unmarshal(pkt); err != nil {
				continue
			}
			if h.ContentType != protocol.ContentTypeConnectionID {
				continue
			}

			return string(h.ConnectionID), true
		}

		return "", false
	}
}

// cidConnIdentifier extracts connection IDs from outgoing ServerHello records
// and associates them with the associated connection.
// NOTE: a ServerHello should always be the first record in a datagram if
// multiple are present, so we avoid iterating through all packets if the first
// is not a ServerHello.
func cidConnIdentifier() func([]byte) (string, bool) { //nolint:cyclop
	return func(packet []byte) (string, bool) {
		pkts, err := recordlayer.UnpackDatagram(packet)
		if err != nil || len(pkts) < 1 {
			return "", false
		}
		var h recordlayer.Header
		if hErr := h.Unmarshal(pkts[0]); hErr != nil {
			return "", false
		}
		if h.ContentType != protocol.ContentTypeHandshake {
			return "", false
		}
		var hh handshake.Header
		var sh handshake.MessageServerHello
		for _, pkt := range pkts {
			if hhErr := hh.Unmarshal(pkt[recordlayer.FixedHeaderSize:]); hhErr != nil {
				continue
			}
			if err = sh.Unmarshal(pkt[recordlayer.FixedHeaderSize+handshake.HeaderLength:]); err == nil {
				break
			}
		}
		if err != nil {
			return "", false
		}
		for _, ext := range sh.Extensions {
			if e, ok := ext.(*extension.ConnectionID); ok {
				return string(e.CID), true
			}
		}

		return "", false
	}
}
