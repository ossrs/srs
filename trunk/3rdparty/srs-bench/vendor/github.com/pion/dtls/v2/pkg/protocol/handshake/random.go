package handshake

import (
	"crypto/rand"
	"encoding/binary"
	"time"
)

// Consts for Random in Handshake
const (
	RandomBytesLength = 28
	RandomLength      = RandomBytesLength + 4
)

// Random value that is used in ClientHello and ServerHello
//
// https://tools.ietf.org/html/rfc4346#section-7.4.1.2
type Random struct {
	GMTUnixTime time.Time
	RandomBytes [RandomBytesLength]byte
}

// MarshalFixed encodes the Handshake
func (r *Random) MarshalFixed() [RandomLength]byte {
	var out [RandomLength]byte

	binary.BigEndian.PutUint32(out[0:], uint32(r.GMTUnixTime.Unix()))
	copy(out[4:], r.RandomBytes[:])

	return out
}

// UnmarshalFixed populates the message from encoded data
func (r *Random) UnmarshalFixed(data [RandomLength]byte) {
	r.GMTUnixTime = time.Unix(int64(binary.BigEndian.Uint32(data[0:])), 0)
	copy(r.RandomBytes[:], data[4:])
}

// Populate fills the handshakeRandom with random values
// may be called multiple times
func (r *Random) Populate() error {
	r.GMTUnixTime = time.Now()

	tmp := make([]byte, RandomBytesLength)
	_, err := rand.Read(tmp)
	copy(r.RandomBytes[:], tmp)

	return err
}
