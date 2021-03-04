package sctp

// At the initialization of the association, the sender of the INIT or
// INIT ACK chunk MAY include this OPTIONAL parameter to inform its peer
// that it is able to support the Forward TSN chunk
//
//   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |    Parameter Type = 49152     |  Parameter Length = 4         |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

type paramForwardTSNSupported struct {
	paramHeader
}

func (f *paramForwardTSNSupported) marshal() ([]byte, error) {
	f.typ = forwardTSNSupp
	f.raw = []byte{}
	return f.paramHeader.marshal()
}

func (f *paramForwardTSNSupported) unmarshal(raw []byte) (param, error) {
	err := f.paramHeader.unmarshal(raw)
	if err != nil {
		return nil, err
	}
	return f, nil
}
