package dtls

import "github.com/pion/dtls/v2/pkg/protocol"

func defaultCompressionMethods() []*protocol.CompressionMethod {
	return []*protocol.CompressionMethod{
		{},
	}
}
