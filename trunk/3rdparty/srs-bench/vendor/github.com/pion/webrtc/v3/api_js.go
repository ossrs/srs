// +build js,wasm

package webrtc

// API bundles the global funcions of the WebRTC and ORTC API.
type API struct {
	settingEngine *SettingEngine
}

// NewAPI Creates a new API object for keeping semi-global settings to WebRTC objects
func NewAPI(options ...func(*API)) *API {
	a := &API{}

	for _, o := range options {
		o(a)
	}

	if a.settingEngine == nil {
		a.settingEngine = &SettingEngine{}
	}

	return a
}

// WithSettingEngine allows providing a SettingEngine to the API.
// Settings should not be changed after passing the engine to an API.
func WithSettingEngine(s SettingEngine) func(a *API) {
	return func(a *API) {
		a.settingEngine = &s
	}
}
