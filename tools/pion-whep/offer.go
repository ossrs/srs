// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"fmt"
	"strings"

	"github.com/pion/webrtc/v4"
)

// addRecvTransceivers adds the recvonly video and audio transceivers of the player. With rtx, the offer carries
// pion's default codecs, an rtx payload with apt for every video codec included, as a browser's does, so SRS may
// answer RTX. Without it, the video transceiver prefers those same codecs minus the rtx entries, so the offer
// carries no rtx and no apt, and SRS can answer only plain retransmission whatever its nack_prefer_rtx says.
func addRecvTransceivers(pc *webrtc.PeerConnection, rtx bool) error {
	for _, kind := range []webrtc.RTPCodecType{webrtc.RTPCodecTypeVideo, webrtc.RTPCodecTypeAudio} {
		transceiver, err := pc.AddTransceiverFromKind(kind, webrtc.RTPTransceiverInit{Direction: webrtc.RTPTransceiverDirectionRecvonly})
		if err != nil {
			return fmt.Errorf("add %s transceiver: %w", kind, err)
		}
		if rtx || kind != webrtc.RTPCodecTypeVideo {
			continue
		}

		// The receiver's parameters carry every codec the media engine registered for the kind; keep all but rtx.
		var codecs []webrtc.RTPCodecParameters
		for _, codec := range transceiver.Receiver().GetParameters().Codecs {
			if !strings.EqualFold(codec.MimeType, webrtc.MimeTypeRTX) {
				codecs = append(codecs, codec)
			}
		}
		if err := transceiver.SetCodecPreferences(codecs); err != nil {
			return fmt.Errorf("prefer %d video codecs without rtx: %w", len(codecs), err)
		}
	}
	return nil
}
