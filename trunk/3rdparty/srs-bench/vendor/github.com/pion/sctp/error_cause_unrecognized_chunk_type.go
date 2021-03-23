package sctp

// errorCauseUnrecognizedChunkType represents an SCTP error cause
type errorCauseUnrecognizedChunkType struct {
	errorCauseHeader
	unrecognizedChunk []byte
}

func (e *errorCauseUnrecognizedChunkType) marshal() ([]byte, error) {
	e.code = unrecognizedChunkType
	e.errorCauseHeader.raw = e.unrecognizedChunk
	return e.errorCauseHeader.marshal()
}

func (e *errorCauseUnrecognizedChunkType) unmarshal(raw []byte) error {
	err := e.errorCauseHeader.unmarshal(raw)
	if err != nil {
		return err
	}

	e.unrecognizedChunk = e.errorCauseHeader.raw
	return nil
}

// String makes errorCauseUnrecognizedChunkType printable
func (e *errorCauseUnrecognizedChunkType) String() string {
	return e.errorCauseHeader.String()
}
