package mpeg4video

import (
	"bytes"
	"fmt"
)

// IsValidConfig checks whether a MPEG-4 Video configuration is valid.
func IsValidConfig(config []byte) error {
	if !bytes.HasPrefix(config, []byte{0, 0, 1, byte(VisualObjectSequenceStartCode)}) {
		return fmt.Errorf("doesn't start with visual_object_sequence_start_code")
	}

	videoObjectFound := false
	videoObjectLayerFound := false

	for i := 4; i < (len(config) - 4); i++ {
		if bytes.Equal(config[i:i+3], []byte{0, 0, 1}) {
			startCode := StartCode(config[i+3])

			switch {
			case startCode >= VideoObjectStartCodeFirst && startCode <= VideoObjectStartCodeLast:
				videoObjectFound = true

			case startCode >= VideoObjectLayerStartCodeFirst && startCode <= VideoObjectLayerStartCodeLast:
				videoObjectLayerFound = true

			case startCode == VisualObjectStartCode,
				startCode == UserDataStartCode:

			default:
				return fmt.Errorf("unexpected start code: %x", startCode)
			}

			i += 3
		}
	}

	if !videoObjectFound {
		return fmt.Errorf("video object not found")
	}

	if !videoObjectLayerFound {
		return fmt.Errorf("video object layer not found")
	}

	return nil
}
