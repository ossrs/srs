// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build !js
// +build !js

package webrtc

import (
	"github.com/pion/interceptor"
	"github.com/pion/logging"
)

// API allows configuration of a PeerConnection
// with APIs that are available in the standard. This
// lets you set custom behavior via the SettingEngine, configure
// codecs via the MediaEngine and define custom media behaviors via
// Interceptors.
type API struct {
	settingEngine       *SettingEngine
	mediaEngine         *MediaEngine
	interceptorRegistry *interceptor.Registry

	interceptor interceptor.Interceptor // Generated per PeerConnection
}

// NewAPI Creates a new API object for keeping semi-global settings to WebRTC objects
//
// It uses the default Codecs and Interceptors unless you customize them
// using WithMediaEngine and WithInterceptorRegistry respectively.
func NewAPI(options ...func(*API)) *API {
	api := &API{
		interceptor:   &interceptor.NoOp{},
		settingEngine: &SettingEngine{},
	}

	for _, o := range options {
		o(api)
	}

	if api.settingEngine.LoggerFactory == nil {
		api.settingEngine.LoggerFactory = logging.NewDefaultLoggerFactory()
	}

	logger := api.settingEngine.LoggerFactory.NewLogger("api")

	if api.mediaEngine == nil {
		api.mediaEngine = &MediaEngine{}
		err := api.mediaEngine.RegisterDefaultCodecs()
		if err != nil {
			logger.Errorf("Failed to register default codecs %s", err)
		}
	}

	if api.interceptorRegistry == nil {
		api.interceptorRegistry = &interceptor.Registry{}
		err := RegisterDefaultInterceptors(api.mediaEngine, api.interceptorRegistry)
		if err != nil {
			logger.Errorf("Failed to register default interceptors %s", err)
		}
	}

	return api
}

// WithMediaEngine allows providing a MediaEngine to the API.
// Settings can be changed after passing the engine to an API.
// When a PeerConnection is created the MediaEngine is copied
// and no more changes can be made.
func WithMediaEngine(m *MediaEngine) func(a *API) {
	return func(a *API) {
		a.mediaEngine = m
		if a.mediaEngine == nil {
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
func WithInterceptorRegistry(ir *interceptor.Registry) func(a *API) {
	return func(a *API) {
		a.interceptorRegistry = ir
		if a.interceptorRegistry == nil {
			a.interceptorRegistry = &interceptor.Registry{}
		}
	}
}
