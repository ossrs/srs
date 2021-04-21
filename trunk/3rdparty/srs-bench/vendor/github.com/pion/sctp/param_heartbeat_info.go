package sctp

type paramHeartbeatInfo struct {
	paramHeader
	heartbeatInformation []byte
}

func (h *paramHeartbeatInfo) marshal() ([]byte, error) {
	h.typ = heartbeatInfo
	h.raw = h.heartbeatInformation
	return h.paramHeader.marshal()
}

func (h *paramHeartbeatInfo) unmarshal(raw []byte) (param, error) {
	err := h.paramHeader.unmarshal(raw)
	if err != nil {
		return nil, err
	}
	h.heartbeatInformation = h.raw
	return h, nil
}
