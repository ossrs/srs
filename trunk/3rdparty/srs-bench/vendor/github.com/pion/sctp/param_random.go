package sctp

type paramRandom struct {
	paramHeader
	randomData []byte
}

func (r *paramRandom) marshal() ([]byte, error) {
	r.typ = random
	r.raw = r.randomData
	return r.paramHeader.marshal()
}

func (r *paramRandom) unmarshal(raw []byte) (param, error) {
	err := r.paramHeader.unmarshal(raw)
	if err != nil {
		return nil, err
	}
	r.randomData = r.raw
	return r, nil
}
