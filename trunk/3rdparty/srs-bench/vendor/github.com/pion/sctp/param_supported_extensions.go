package sctp

type paramSupportedExtensions struct {
	paramHeader
	ChunkTypes []chunkType
}

func (s *paramSupportedExtensions) marshal() ([]byte, error) {
	s.typ = supportedExt
	s.raw = make([]byte, len(s.ChunkTypes))
	for i, c := range s.ChunkTypes {
		s.raw[i] = byte(c)
	}

	return s.paramHeader.marshal()
}

func (s *paramSupportedExtensions) unmarshal(raw []byte) (param, error) {
	err := s.paramHeader.unmarshal(raw)
	if err != nil {
		return nil, err
	}

	for _, t := range s.raw {
		s.ChunkTypes = append(s.ChunkTypes, chunkType(t))
	}

	return s, nil
}
