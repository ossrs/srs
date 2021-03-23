package turn

import "errors"

var (
	errRelayAddressInvalid           = errors.New("turn: RelayAddress must be valid IP to use RelayAddressGeneratorStatic")
	errNoAvailableConns              = errors.New("turn: PacketConnConfigs and ConnConfigs are empty, unable to proceed")
	errConnUnset                     = errors.New("turn: PacketConnConfig must have a non-nil Conn")
	errListenerUnset                 = errors.New("turn: ListenerConfig must have a non-nil Listener")
	errListeningAddressInvalid       = errors.New("turn: RelayAddressGenerator has invalid ListeningAddress")
	errRelayAddressGeneratorUnset    = errors.New("turn: RelayAddressGenerator in RelayConfig is unset")
	errMaxRetriesExceeded            = errors.New("turn: max retries exceeded")
	errMaxPortNotZero                = errors.New("turn: MaxPort must be not 0")
	errMinPortNotZero                = errors.New("turn: MaxPort must be not 0")
	errNilConn                       = errors.New("turn: conn cannot not be nil")
	errTODO                          = errors.New("turn: TODO")
	errAlreadyListening              = errors.New("turn: already listening")
	errFailedToClose                 = errors.New("turn: Server failed to close")
	errFailedToRetransmitTransaction = errors.New("turn: failed to retransmit transaction")
	errAllRetransmissionsFailed      = errors.New("all retransmissions failed for")
	errChannelBindNotFound           = errors.New("no binding found for channel")
	errSTUNServerAddressNotSet       = errors.New("STUN server address is not set for the client")
	errOneAllocateOnly               = errors.New("only one Allocate() caller is allowed")
	errAlreadyAllocated              = errors.New("already allocated")
	errNonSTUNMessage                = errors.New("non-STUN message from STUN server")
	errFailedToDecodeSTUN            = errors.New("failed to decode STUN message")
	errUnexpectedSTUNRequestMessage  = errors.New("unexpected STUN request message")
)
