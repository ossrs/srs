package sctp

type paramECNCapable struct {
	paramHeader
}

func (r *paramECNCapable) marshal() ([]byte, error) {
	r.typ = ecnCapable
	r.raw = []byte{}
	return r.paramHeader.marshal()
}

func (r *paramECNCapable) unmarshal(raw []byte) (param, error) {
	err := r.paramHeader.unmarshal(raw)
	if err != nil {
		return nil, err
	}
	return r, nil
}
