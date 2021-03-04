package server

import "errors"

var (
	errFailedToGenerateNonce                  = errors.New("failed to generate nonce")
	errFailedToSendError                      = errors.New("failed to send error message")
	errDuplicatedNonce                        = errors.New("duplicated Nonce generated, discarding request")
	errNoSuchUser                             = errors.New("no such user exists")
	errUnexpectedClass                        = errors.New("unexpected class")
	errUnexpectedMethod                       = errors.New("unexpected method")
	errFailedToHandle                         = errors.New("failed to handle")
	errUnhandledSTUNPacket                    = errors.New("unhandled STUN packet")
	errUnableToHandleChannelData              = errors.New("unable to handle ChannelData")
	errFailedToCreateSTUNPacket               = errors.New("failed to create stun message from packet")
	errFailedToCreateChannelData              = errors.New("failed to create channel data from packet")
	errRelayAlreadyAllocatedForFiveTuple      = errors.New("relay already allocated for 5-TUPLE")
	errRequestedTransportMustBeUDP            = errors.New("RequestedTransport must be UDP")
	errNoDontFragmentSupport                  = errors.New("no support for DONT-FRAGMENT")
	errRequestWithReservationTokenAndEvenPort = errors.New("Request must not contain RESERVATION-TOKEN and EVEN-PORT")
	errNoAllocationFound                      = errors.New("no allocation found")
	errNoPermission                           = errors.New("unable to handle send-indication, no permission added")
	errShortWrite                             = errors.New("packet write smaller than packet")
	errNoSuchChannelBind                      = errors.New("no such channel bind")
	errFailedWriteSocket                      = errors.New("failed writing to socket")
)
