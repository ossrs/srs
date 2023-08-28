// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

import (
	"encoding/json"
)

// BundlePolicy affects which media tracks are negotiated if the remote
// endpoint is not bundle-aware, and what ICE candidates are gathered. If the
// remote endpoint is bundle-aware, all media tracks and data channels are
// bundled onto the same transport.
type BundlePolicy int

const (
	// BundlePolicyBalanced indicates to gather ICE candidates for each
	// media type in use (audio, video, and data). If the remote endpoint is
	// not bundle-aware, negotiate only one audio and video track on separate
	// transports.
	BundlePolicyBalanced BundlePolicy = iota + 1

	// BundlePolicyMaxCompat indicates to gather ICE candidates for each
	// track. If the remote endpoint is not bundle-aware, negotiate all media
	// tracks on separate transports.
	BundlePolicyMaxCompat

	// BundlePolicyMaxBundle indicates to gather ICE candidates for only
	// one track. If the remote endpoint is not bundle-aware, negotiate only
	// one media track.
	BundlePolicyMaxBundle
)

// This is done this way because of a linter.
const (
	bundlePolicyBalancedStr  = "balanced"
	bundlePolicyMaxCompatStr = "max-compat"
	bundlePolicyMaxBundleStr = "max-bundle"
)

func newBundlePolicy(raw string) BundlePolicy {
	switch raw {
	case bundlePolicyBalancedStr:
		return BundlePolicyBalanced
	case bundlePolicyMaxCompatStr:
		return BundlePolicyMaxCompat
	case bundlePolicyMaxBundleStr:
		return BundlePolicyMaxBundle
	default:
		return BundlePolicy(Unknown)
	}
}

func (t BundlePolicy) String() string {
	switch t {
	case BundlePolicyBalanced:
		return bundlePolicyBalancedStr
	case BundlePolicyMaxCompat:
		return bundlePolicyMaxCompatStr
	case BundlePolicyMaxBundle:
		return bundlePolicyMaxBundleStr
	default:
		return ErrUnknownType.Error()
	}
}

// UnmarshalJSON parses the JSON-encoded data and stores the result
func (t *BundlePolicy) UnmarshalJSON(b []byte) error {
	var val string
	if err := json.Unmarshal(b, &val); err != nil {
		return err
	}

	*t = newBundlePolicy(val)
	return nil
}

// MarshalJSON returns the JSON encoding
func (t BundlePolicy) MarshalJSON() ([]byte, error) {
	return json.Marshal(t.String())
}
