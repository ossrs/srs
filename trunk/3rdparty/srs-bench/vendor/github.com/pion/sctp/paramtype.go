package sctp

import (
	"encoding/binary"
	"fmt"

	"github.com/pkg/errors"
)

// paramType represents a SCTP INIT/INITACK parameter
type paramType uint16

const (
	heartbeatInfo      paramType = 1     // Heartbeat Info	[RFC4960]
	ipV4Addr           paramType = 5     // IPv4 IP	[RFC4960]
	ipV6Addr           paramType = 6     // IPv6 IP	[RFC4960]
	stateCookie        paramType = 7     // State Cookie	[RFC4960]
	unrecognizedParam  paramType = 8     // Unrecognized Parameters	[RFC4960]
	cookiePreservative paramType = 9     // Cookie Preservative	[RFC4960]
	hostNameAddr       paramType = 11    // Host Name IP	[RFC4960]
	supportedAddrTypes paramType = 12    // Supported IP Types	[RFC4960]
	outSSNResetReq     paramType = 13    // Outgoing SSN Reset Request Parameter	[RFC6525]
	incSSNResetReq     paramType = 14    // Incoming SSN Reset Request Parameter	[RFC6525]
	ssnTSNResetReq     paramType = 15    // SSN/TSN Reset Request Parameter	[RFC6525]
	reconfigResp       paramType = 16    // Re-configuration Response Parameter	[RFC6525]
	addOutStreamsReq   paramType = 17    // Add Outgoing Streams Request Parameter	[RFC6525]
	addIncStreamsReq   paramType = 18    // Add Incoming Streams Request Parameter	[RFC6525]
	random             paramType = 32770 // Random (0x8002)	[RFC4805]
	chunkList          paramType = 32771 // Chunk List (0x8003)	[RFC4895]
	reqHMACAlgo        paramType = 32772 // Requested HMAC Algorithm Parameter (0x8004)	[RFC4895]
	padding            paramType = 32773 // Padding (0x8005)
	supportedExt       paramType = 32776 // Supported Extensions (0x8008)	[RFC5061]
	forwardTSNSupp     paramType = 49152 // Forward TSN supported (0xC000)	[RFC3758]
	addIPAddr          paramType = 49153 // Add IP IP (0xC001)	[RFC5061]
	delIPAddr          paramType = 49154 // Delete IP IP (0xC002)	[RFC5061]
	errClauseInd       paramType = 49155 // Error Cause Indication (0xC003)	[RFC5061]
	setPriAddr         paramType = 49156 // Set Primary IP (0xC004)	[RFC5061]
	successInd         paramType = 49157 // Success Indication (0xC005)	[RFC5061]
	adaptLayerInd      paramType = 49158 // Adaptation Layer Indication (0xC006)	[RFC5061]
)

func parseParamType(raw []byte) (paramType, error) {
	if len(raw) < 2 {
		return paramType(0), errors.New("packet to short")
	}
	return paramType(binary.BigEndian.Uint16(raw)), nil
}

func (p paramType) String() string {
	switch p {
	case heartbeatInfo:
		return "Heartbeat Info"
	case ipV4Addr:
		return "IPv4 IP"
	case ipV6Addr:
		return "IPv6 IP"
	case stateCookie:
		return "State Cookie"
	case unrecognizedParam:
		return "Unrecognized Parameters"
	case cookiePreservative:
		return "Cookie Preservative"
	case hostNameAddr:
		return "Host Name IP"
	case supportedAddrTypes:
		return "Supported IP Types"
	case outSSNResetReq:
		return "Outgoing SSN Reset Request Parameter"
	case incSSNResetReq:
		return "Incoming SSN Reset Request Parameter"
	case ssnTSNResetReq:
		return "SSN/TSN Reset Request Parameter"
	case reconfigResp:
		return "Re-configuration Response Parameter"
	case addOutStreamsReq:
		return "Add Outgoing Streams Request Parameter"
	case addIncStreamsReq:
		return "Add Incoming Streams Request Parameter"
	case random:
		return "Random"
	case chunkList:
		return "Chunk List"
	case reqHMACAlgo:
		return "Requested HMAC Algorithm Parameter"
	case padding:
		return "Padding"
	case supportedExt:
		return "Supported Extensions"
	case forwardTSNSupp:
		return "Forward TSN supported"
	case addIPAddr:
		return "Add IP IP"
	case delIPAddr:
		return "Delete IP IP"
	case errClauseInd:
		return "Error Cause Indication"
	case setPriAddr:
		return "Set Primary IP"
	case successInd:
		return "Success Indication"
	case adaptLayerInd:
		return "Adaptation Layer Indication"
	default:
		return fmt.Sprintf("Unknown ParamType: %d", p)
	}
}
