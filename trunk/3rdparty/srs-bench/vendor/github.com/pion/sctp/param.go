package sctp

import (
	"github.com/pkg/errors"
)

type param interface {
	marshal() ([]byte, error)
	length() int
}

func buildParam(t paramType, rawParam []byte) (param, error) {
	switch t {
	case forwardTSNSupp:
		return (&paramForwardTSNSupported{}).unmarshal(rawParam)
	case supportedExt:
		return (&paramSupportedExtensions{}).unmarshal(rawParam)
	case random:
		return (&paramRandom{}).unmarshal(rawParam)
	case reqHMACAlgo:
		return (&paramRequestedHMACAlgorithm{}).unmarshal(rawParam)
	case chunkList:
		return (&paramChunkList{}).unmarshal(rawParam)
	case stateCookie:
		return (&paramStateCookie{}).unmarshal(rawParam)
	case heartbeatInfo:
		return (&paramHeartbeatInfo{}).unmarshal(rawParam)
	case outSSNResetReq:
		return (&paramOutgoingResetRequest{}).unmarshal(rawParam)
	case reconfigResp:
		return (&paramReconfigResponse{}).unmarshal(rawParam)
	default:
		return nil, errors.Errorf("Unhandled ParamType %v", t)
	}
}
