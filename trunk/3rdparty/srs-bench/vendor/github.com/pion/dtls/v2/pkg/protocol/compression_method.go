package protocol

// CompressionMethodID is the ID for a CompressionMethod
type CompressionMethodID byte

const (
	compressionMethodNull CompressionMethodID = 0
)

// CompressionMethod represents a TLS Compression Method
type CompressionMethod struct {
	ID CompressionMethodID
}

// CompressionMethods returns all supported CompressionMethods
func CompressionMethods() map[CompressionMethodID]*CompressionMethod {
	return map[CompressionMethodID]*CompressionMethod{
		compressionMethodNull: {ID: compressionMethodNull},
	}
}

// DecodeCompressionMethods the given compression methods
func DecodeCompressionMethods(buf []byte) ([]*CompressionMethod, error) {
	if len(buf) < 1 {
		return nil, errBufferTooSmall
	}
	compressionMethodsCount := int(buf[0])
	c := []*CompressionMethod{}
	for i := 0; i < compressionMethodsCount; i++ {
		if len(buf) <= i+1 {
			return nil, errBufferTooSmall
		}
		id := CompressionMethodID(buf[i+1])
		if compressionMethod, ok := CompressionMethods()[id]; ok {
			c = append(c, compressionMethod)
		}
	}
	return c, nil
}

// EncodeCompressionMethods the given compression methods
func EncodeCompressionMethods(c []*CompressionMethod) []byte {
	out := []byte{byte(len(c))}
	for i := len(c); i > 0; i-- {
		out = append(out, byte(c[i-1].ID))
	}
	return out
}
