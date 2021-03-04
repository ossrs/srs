package dtls

type compressionMethodID byte

const (
	compressionMethodNull compressionMethodID = 0
)

type compressionMethod struct {
	id compressionMethodID
}

func compressionMethods() map[compressionMethodID]*compressionMethod {
	return map[compressionMethodID]*compressionMethod{
		compressionMethodNull: {id: compressionMethodNull},
	}
}

func defaultCompressionMethods() []*compressionMethod {
	return []*compressionMethod{
		compressionMethods()[compressionMethodNull],
	}
}

func decodeCompressionMethods(buf []byte) ([]*compressionMethod, error) {
	if len(buf) < 1 {
		return nil, errDTLSPacketInvalidLength
	}
	compressionMethodsCount := int(buf[0])
	c := []*compressionMethod{}
	for i := 0; i < compressionMethodsCount; i++ {
		if len(buf) <= i+1 {
			return nil, errBufferTooSmall
		}
		id := compressionMethodID(buf[i+1])
		if compressionMethod, ok := compressionMethods()[id]; ok {
			c = append(c, compressionMethod)
		}
	}
	return c, nil
}

func encodeCompressionMethods(c []*compressionMethod) []byte {
	out := []byte{byte(len(c))}
	for i := len(c); i > 0; i-- {
		out = append(out, byte(c[i-1].id))
	}
	return out
}
