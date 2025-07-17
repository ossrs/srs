package mpeg4audio

import (
	"fmt"

	"github.com/bluenviron/mediacommon/v2/pkg/bits"
)

// Config is an alias for AudioSpecificConfig.
type Config = AudioSpecificConfig

// AudioSpecificConfig is an AudioSpecificConfig.
// Specification: ISO 14496-3, 1.6.2.1
type AudioSpecificConfig struct {
	Type         ObjectType
	SampleRate   int
	ChannelCount int

	// SBR / PS specific
	ExtensionType       ObjectType
	ExtensionSampleRate int

	FrameLengthFlag    bool
	DependsOnCoreCoder bool
	CoreCoderDelay     uint16
}

// Unmarshal decodes a Config.
func (c *AudioSpecificConfig) Unmarshal(buf []byte) error {
	pos := 0
	return c.UnmarshalFromPos(buf, &pos)
}

// UnmarshalFromPos decodes a Config.
func (c *AudioSpecificConfig) UnmarshalFromPos(buf []byte, pos *int) error {
	tmp, err := bits.ReadBits(buf, pos, 5)
	if err != nil {
		return err
	}
	c.Type = ObjectType(tmp)

	switch c.Type {
	case ObjectTypeAACLC, ObjectTypeSBR, ObjectTypePS:
	default:
		return fmt.Errorf("unsupported object type: %d", c.Type)
	}

	sampleRateIndex, err := bits.ReadBits(buf, pos, 4)
	if err != nil {
		return err
	}

	switch {
	case sampleRateIndex <= 12:
		c.SampleRate = sampleRates[sampleRateIndex]

	case sampleRateIndex == 0x0F:
		tmp, err = bits.ReadBits(buf, pos, 24)
		if err != nil {
			return err
		}
		c.SampleRate = int(tmp)

	default:
		return fmt.Errorf("invalid sample rate index (%d)", sampleRateIndex)
	}

	channelConfig, err := bits.ReadBits(buf, pos, 4)
	if err != nil {
		return err
	}

	switch {
	case channelConfig == 0:
		return fmt.Errorf("not yet supported")

	case channelConfig >= 1 && channelConfig <= 6:
		c.ChannelCount = int(channelConfig)

	case channelConfig == 7:
		c.ChannelCount = 8

	default:
		return fmt.Errorf("invalid channel configuration (%d)", channelConfig)
	}

	if c.Type == ObjectTypeSBR || c.Type == ObjectTypePS {
		c.ExtensionType = c.Type

		var extensionSamplingFrequencyIndex uint64
		extensionSamplingFrequencyIndex, err = bits.ReadBits(buf, pos, 4)
		if err != nil {
			return err
		}

		switch {
		case extensionSamplingFrequencyIndex <= 12:
			c.ExtensionSampleRate = sampleRates[extensionSamplingFrequencyIndex]

		case extensionSamplingFrequencyIndex == 0x0F:
			tmp, err = bits.ReadBits(buf, pos, 24)
			if err != nil {
				return err
			}
			c.ExtensionSampleRate = int(tmp)

		default:
			return fmt.Errorf("invalid extension sample rate index (%d)", extensionSamplingFrequencyIndex)
		}

		tmp, err = bits.ReadBits(buf, pos, 5)
		if err != nil {
			return err
		}
		c.Type = ObjectType(tmp)

		if c.Type != ObjectTypeAACLC {
			return fmt.Errorf("unsupported object type: %d", c.Type)
		}
	}

	c.FrameLengthFlag, err = bits.ReadFlag(buf, pos)
	if err != nil {
		return err
	}

	c.DependsOnCoreCoder, err = bits.ReadFlag(buf, pos)
	if err != nil {
		return err
	}

	if c.DependsOnCoreCoder {
		tmp, err = bits.ReadBits(buf, pos, 14)
		if err != nil {
			return err
		}
		c.CoreCoderDelay = uint16(tmp)
	}

	extensionFlag, err := bits.ReadFlag(buf, pos)
	if err != nil {
		return err
	}

	if extensionFlag {
		return fmt.Errorf("unsupported")
	}

	return nil
}

func (c AudioSpecificConfig) marshalSizeBits() int {
	n := 5 + 4 + 2 + 1

	_, ok := reverseSampleRates[c.SampleRate]
	if !ok {
		n += 28
	} else {
		n += 4
	}

	if c.ExtensionType == ObjectTypeSBR || c.ExtensionType == ObjectTypePS {
		_, ok := reverseSampleRates[c.ExtensionSampleRate]
		if !ok {
			n += 28
		} else {
			n += 4
		}
		n += 5
	}

	if c.DependsOnCoreCoder {
		n += 14
	}

	return n
}

func (c AudioSpecificConfig) marshalSize() int {
	n := c.marshalSizeBits()

	ret := n / 8
	if (n % 8) != 0 {
		ret++
	}

	return ret
}

// Marshal encodes a Config.
func (c AudioSpecificConfig) Marshal() ([]byte, error) {
	buf := make([]byte, c.marshalSize())
	pos := 0

	err := c.marshalTo(buf, &pos)
	if err != nil {
		return nil, err
	}

	return buf, nil
}

func (c AudioSpecificConfig) marshalTo(buf []byte, pos *int) error {
	if c.ExtensionType == ObjectTypeSBR || c.ExtensionType == ObjectTypePS {
		bits.WriteBitsUnsafe(buf, pos, uint64(c.ExtensionType), 5)
	} else {
		bits.WriteBitsUnsafe(buf, pos, uint64(c.Type), 5)
	}

	sampleRateIndex, ok := reverseSampleRates[c.SampleRate]
	if !ok {
		bits.WriteBitsUnsafe(buf, pos, uint64(15), 4)
		bits.WriteBitsUnsafe(buf, pos, uint64(c.SampleRate), 24)
	} else {
		bits.WriteBitsUnsafe(buf, pos, uint64(sampleRateIndex), 4)
	}

	var channelConfig int
	switch {
	case c.ChannelCount >= 1 && c.ChannelCount <= 6:
		channelConfig = c.ChannelCount

	case c.ChannelCount == 8:
		channelConfig = 7

	default:
		return fmt.Errorf("invalid channel count (%d)", c.ChannelCount)
	}
	bits.WriteBitsUnsafe(buf, pos, uint64(channelConfig), 4)

	if c.ExtensionType == ObjectTypeSBR || c.ExtensionType == ObjectTypePS {
		sampleRateIndex, ok := reverseSampleRates[c.ExtensionSampleRate]
		if !ok {
			bits.WriteBitsUnsafe(buf, pos, uint64(0x0F), 4)
			bits.WriteBitsUnsafe(buf, pos, uint64(c.ExtensionSampleRate), 24)
		} else {
			bits.WriteBitsUnsafe(buf, pos, uint64(sampleRateIndex), 4)
		}
		bits.WriteBitsUnsafe(buf, pos, uint64(c.Type), 5)
	}

	if c.FrameLengthFlag {
		bits.WriteBitsUnsafe(buf, pos, 1, 1)
	} else {
		bits.WriteBitsUnsafe(buf, pos, 0, 1)
	}

	if c.DependsOnCoreCoder {
		bits.WriteBitsUnsafe(buf, pos, 1, 1)
	} else {
		bits.WriteBitsUnsafe(buf, pos, 0, 1)
	}

	if c.DependsOnCoreCoder {
		bits.WriteBitsUnsafe(buf, pos, uint64(c.CoreCoderDelay), 14)
	}

	*pos++ // extensionFlag

	return nil
}
