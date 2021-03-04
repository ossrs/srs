package srtp

import "github.com/pion/rtp"

// cipher represents a implementation of one
// of the SRTP Specific ciphers
type srtpCipher interface {
	authTagLen() int
	getRTCPIndex([]byte) uint32

	encryptRTP([]byte, *rtp.Header, []byte, uint32) ([]byte, error)
	encryptRTCP([]byte, []byte, uint32, uint32) ([]byte, error)

	decryptRTP([]byte, []byte, *rtp.Header, uint32) ([]byte, error)
	decryptRTCP([]byte, []byte, uint32, uint32) ([]byte, error)
}
