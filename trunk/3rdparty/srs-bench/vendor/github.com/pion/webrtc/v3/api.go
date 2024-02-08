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
func NewAPI(options ...func(*API)) *API {
	a := &API{
		interceptor:         &interceptor.NoOp{},
		settingEngine:       &SettingEngine{},
		mediaEngine:         &MediaEngine{},
		interceptorRegistry: &interceptor.Registry{},
	}

	for _, o := range options {
		o(a)
	}

	if a.settingEngine.LoggerFactory == nil {
		a.settingEngine.LoggerFactory = logging.NewDefaultLoggerFactory()
	}

	return a
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
