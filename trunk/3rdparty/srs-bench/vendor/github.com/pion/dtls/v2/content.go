package dtls

// https://tools.ietf.org/html/rfc4346#section-6.2.1
type contentType uint8

const (
	contentTypeChangeCipherSpec contentType = 20
	contentTypeAlert            contentType = 21
	contentTypeHandshake        contentType = 22
	contentTypeApplicationData  contentType = 23
)

type content interface {
	contentType() contentType
	Marshal() ([]byte, error)
	Unmarshal(data []byte) error
}
