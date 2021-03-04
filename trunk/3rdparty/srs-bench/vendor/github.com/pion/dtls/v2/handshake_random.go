package dtls

import (
	"crypto/rand"
	"encoding/binary"
	"time"
)

const (
	randomBytesLength     = 28
	handshakeRandomLength = randomBytesLength + 4
)

// https://tools.ietf.org/html/rfc4346#section-7.4.1.2
type handshakeRandom struct {
	gmtUnixTime time.Time
	randomBytes [randomBytesLength]byte
}

func (h *handshakeRandom) marshalFixed() [handshakeRandomLength]byte {
	var out [handshakeRandomLength]byte

	binary.BigEndian.PutUint32(out[0:], uint32(h.gmtUnixTime.Unix()))
	copy(out[4:], h.randomBytes[:])

	return out
}

func (h *handshakeRandom) unmarshalFixed(data [handshakeRandomLength]byte) {
	h.gmtUnixTime = time.Unix(int64(binary.BigEndian.Uint32(data[0:])), 0)
	copy(h.randomBytes[:], data[4:])
}

// populate fills the handshakeRandom with random values
// may be called multiple times
func (h *handshakeRandom) populate() error {
	h.gmtUnixTime = time.Now()

	tmp := make([]byte, randomBytesLength)
	_, err := rand.Read(tmp)
	copy(h.randomBytes[:], tmp)

	return err
}
