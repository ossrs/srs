package webrtc

// ICEParameters includes the ICE username fragment
// and password and other ICE-related parameters.
type ICEParameters struct {
	UsernameFragment string `json:"usernameFragment"`
	Password         string `json:"password"`
	ICELite          bool   `json:"iceLite"`
}
