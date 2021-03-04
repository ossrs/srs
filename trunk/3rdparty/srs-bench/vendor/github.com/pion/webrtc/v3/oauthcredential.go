package webrtc

// OAuthCredential represents OAuth credential information which is used by
// the STUN/TURN client to connect to an ICE server as defined in
// https://tools.ietf.org/html/rfc7635. Note that the kid parameter is not
// located in OAuthCredential, but in ICEServer's username member.
type OAuthCredential struct {
	// MACKey is a base64-url encoded format. It is used in STUN message
	// integrity hash calculation.
	MACKey string

	// AccessToken is a base64-encoded format. This is an encrypted
	// self-contained token that is opaque to the application.
	AccessToken string
}
