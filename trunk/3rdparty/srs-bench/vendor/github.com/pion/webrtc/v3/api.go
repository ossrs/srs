// +build !js

package webrtc

import (
	"github.com/pion/interceptor"
	"github.com/pion/logging"
)

// API bundles the global functions of the WebRTC and ORTC API.
// Some of these functions are also exported globally using the
// defaultAPI object. Note that the global version of the API
// may be phased out in the future.
type API struct {
	settingEngine *SettingEngine
	mediaEngine   *MediaEngine
	interceptor   interceptor.Interceptor
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

	if a.settingEngine.LoggerFactory == nil {
		a.settingEngine.LoggerFactory = logging.NewDefaultLoggerFactory()
	}

	if a.mediaEngine == nil {
		a.mediaEngine = &MediaEngine{}
	}

	if a.interceptor == nil {
		a.interceptor = &interceptor.NoOp{}
	}

	return a
}

// WithMediaEngine allows providing a MediaEngine to the API.
// Settings can be changed after passing the engine to an API.
func WithMediaEngine(m *MediaEngine) func(a *API) {
	return func(a *API) {
		if m != nil {
			a.mediaEngine = m
		} else {
			a.mediaEngine = &MediaEngine{}
		}
	}
}

// WithSettingEngine allows providing a SettingEngine to the API.
// Settings should not be changed after passing the engine to an API.
func WithSettingEngine(s SettingEngine) func(a *API) {
	return func(a *API) {
		a.settingEngine = &s
	}
}

// WithInterceptorRegistry allows providing Interceptors to the API.
// Settings should not be changed after passing the registry to an API.
func WithInterceptorRegistry(interceptorRegistry *interceptor.Registry) func(a *API) {
	return func(a *API) {
		a.interceptor = interceptorRegistry.Build()
	}
}
