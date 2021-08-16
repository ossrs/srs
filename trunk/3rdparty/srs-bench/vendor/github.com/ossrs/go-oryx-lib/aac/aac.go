// The MIT License (MIT)
//
// Copyright (c) 2013-2017 Oryx(ossrs)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The oryx AAC package includes some utilites.
package aac

import (
	"github.com/ossrs/go-oryx-lib/errors"
)

// The ADTS is a format of AAC.
// We can encode the RAW AAC frame in ADTS muxer.
// We can also decode the ADTS data to RAW AAC frame.
type ADTS interface {
	// Set the ASC, the codec information.
	// Before encoding raw frame, user must set the asc.
	SetASC(asc []byte) (err error)
	// Encode the raw aac frame to adts data.
	// @remark User must set the asc first.
	Encode(raw []byte) (adts []byte, err error)

	// Decode the adts data to raw frame.
	// @remark User can get the asc after decode ok.
	// @remark When left if not nil, user must decode it again.
	Decode(adts []byte) (raw, left []byte, err error)
	// Get the ASC, the codec information.
	// When decode a adts data or set the asc, user can use this API to get it.
	ASC() *AudioSpecificConfig
}

// The AAC object type in RAW AAC frame.
// Refer to @doc ISO_IEC_14496-3-AAC-2001.pdf, @page 23, @section 1.5.1.1 Audio object type definition
type ObjectType uint8

const (
	ObjectTypeForbidden ObjectType = iota

	ObjectTypeMain
	ObjectTypeLC
	ObjectTypeSSR

	ObjectTypeHE   ObjectType = 5  // HE=LC+SBR
	ObjectTypeHEv2 ObjectType = 29 // HEv2=LC+SBR+PS
)

func (v ObjectType) String() string {
	switch v {
	case ObjectTypeMain:
		return "Main"
	case ObjectTypeLC:
		return "LC"
	case ObjectTypeSSR:
		return "SSR"
	case ObjectTypeHE:
		return "HE"
	case ObjectTypeHEv2:
		return "HEv2"
	default:
		return "Forbidden"
	}
}

func (v ObjectType) ToProfile() Profile {
	switch v {
	case ObjectTypeMain:
		return ProfileMain
	case ObjectTypeHE, ObjectTypeHEv2, ObjectTypeLC:
		return ProfileLC
	case ObjectTypeSSR:
		return ProfileSSR
	default:
		return ProfileForbidden
	}
}

// The profile of AAC in ADTS.
// Refer to @doc ISO_IEC_13818-7-AAC-2004.pdf, @page 40, @section 7.1 Profiles
type Profile uint8

const (
	ProfileMain Profile = iota
	ProfileLC
	ProfileSSR
	ProfileForbidden
)

func (v Profile) String() string {
	switch v {
	case ProfileMain:
		return "Main"
	case ProfileLC:
		return "LC"
	case ProfileSSR:
		return "SSR"
	default:
		return "Forbidden"
	}
}

func (v Profile) ToObjectType() ObjectType {
	switch v {
	case ProfileMain:
		return ObjectTypeMain
	case ProfileLC:
		return ObjectTypeLC
	case ProfileSSR:
		return ObjectTypeSSR
	default:
		return ObjectTypeForbidden
	}
}

// The aac sample rate index.
// Refer to @doc ISO_IEC_13818-7-AAC-2004.pdf, @page 46, @section Table 35 – Sampling frequency
type SampleRateIndex uint8

const (
	SampleRateIndex96kHz SampleRateIndex = iota
	SampleRateIndex88kHz
	SampleRateIndex64kHz
	SampleRateIndex48kHz
	SampleRateIndex44kHz
	SampleRateIndex32kHz
	SampleRateIndex24kHz
	SampleRateIndex22kHz
	SampleRateIndex16kHz
	SampleRateIndex12kHz
	SampleRateIndex11kHz
	SampleRateIndex8kHz
	SampleRateIndex7kHz
	SampleRateIndexReserved0
	SampleRateIndexReserved1
	SampleRateIndexReserved2
	SampleRateIndexReserved3
	SampleRateIndexForbidden
)

func (v SampleRateIndex) String() string {
	switch v {
	case SampleRateIndex96kHz:
		return "96kHz"
	case SampleRateIndex88kHz:
		return "88kHz"
	case SampleRateIndex64kHz:
		return "64kHz"
	case SampleRateIndex48kHz:
		return "48kHz"
	case SampleRateIndex44kHz:
		return "44kHz"
	case SampleRateIndex32kHz:
		return "32kHz"
	case SampleRateIndex24kHz:
		return "24kHz"
	case SampleRateIndex22kHz:
		return "22kHz"
	case SampleRateIndex16kHz:
		return "16kHz"
	case SampleRateIndex12kHz:
		return "12kHz"
	case SampleRateIndex11kHz:
		return "11kHz"
	case SampleRateIndex8kHz:
		return "8kHz"
	case SampleRateIndex7kHz:
		return "7kHz"
	case SampleRateIndexReserved0, SampleRateIndexReserved1, SampleRateIndexReserved2, SampleRateIndexReserved3:
		return "Reserved"
	default:
		return "Forbidden"
	}
}

func (v SampleRateIndex) ToHz() int {
	aacSR := []int{
		96000, 88200, 64000, 48000,
		44100, 32000, 24000, 22050,
		16000, 12000, 11025, 8000,
		7350, 0, 0, 0,
		/* To avoid overflow by forbidden */
		0,
	}
	return aacSR[v]
}

// The aac channel.
// Refer to @doc ISO_IEC_13818-7-AAC-2004.pdf, @page 72, @section Table 42 – Implicit speaker mapping
type Channels uint8

const (
	ChannelForbidden Channels = iota
	// center front speaker
	// FFMPEG: mono           FC
	ChannelMono
	// left, right front speakers
	// FFMPEG: stereo         FL+FR
	ChannelStereo
	// center front speaker, left, right front speakers
	// FFMPEG: 2.1            FL+FR+LFE
	// FFMPEG: 3.0            FL+FR+FC
	// FFMPEG: 3.0(back)      FL+FR+BC
	Channel3
	// center front speaker, left, right center front speakers, rear surround
	// FFMPEG: 4.0            FL+FR+FC+BC
	// FFMPEG: quad           FL+FR+BL+BR
	// FFMPEG: quad(side)     FL+FR+SL+SR
	// FFMPEG: 3.1            FL+FR+FC+LFE
	Channel4
	// center front speaker, left, right front speakers, left surround, right surround rear speakers
	// FFMPEG: 5.0            FL+FR+FC+BL+BR
	// FFMPEG: 5.0(side)      FL+FR+FC+SL+SR
	// FFMPEG: 4.1            FL+FR+FC+LFE+BC
	Channel5
	// center front speaker, left, right front speakers, left surround, right surround rear speakers,
	// front low frequency effects speaker
	// FFMPEG: 5.1            FL+FR+FC+LFE+BL+BR
	// FFMPEG: 5.1(side)      FL+FR+FC+LFE+SL+SR
	// FFMPEG: 6.0            FL+FR+FC+BC+SL+SR
	// FFMPEG: 6.0(front)     FL+FR+FLC+FRC+SL+SR
	// FFMPEG: hexagonal      FL+FR+FC+BL+BR+BC
	Channel5_1 // speakers: 6
	// center front speaker, left, right center front speakers, left, right outside front speakers,
	// left surround, right surround rear speakers, front low frequency effects speaker
	// FFMPEG: 7.1            FL+FR+FC+LFE+BL+BR+SL+SR
	// FFMPEG: 7.1(wide)      FL+FR+FC+LFE+BL+BR+FLC+FRC
	// FFMPEG: 7.1(wide-side) FL+FR+FC+LFE+FLC+FRC+SL+SR
	Channel7_1 // speakers: 7
	// FFMPEG: 6.1            FL+FR+FC+LFE+BC+SL+SR
	// FFMPEG: 6.1(back)      FL+FR+FC+LFE+BL+BR+BC
	// FFMPEG: 6.1(front)     FL+FR+LFE+FLC+FRC+SL+SR
	// FFMPEG: 7.0            FL+FR+FC+BL+BR+SL+SR
	// FFMPEG: 7.0(front)     FL+FR+FC+FLC+FRC+SL+SR
)

func (v Channels) String() string {
	switch v {
	case ChannelMono:
		return "Mono(FC)"
	case ChannelStereo:
		return "Stereo(FL+FR)"
	case Channel3:
		return "FL+FR+FC"
	case Channel4:
		return "FL+FR+FC+BC"
	case Channel5:
		return "FL+FR+FC+SL+SR"
	case Channel5_1:
		return "FL+FR+FC+LFE+SL+SR"
	case Channel7_1:
		return "FL+FR+FC+LFE+BL+BR+SL+SR"
	default:
		return "Forbidden"
	}
}

// Please use NewADTS() and interface ADTS instead.
// It's only exposed for example.
type ADTSImpl struct {
	asc AudioSpecificConfig
}

func NewADTS() (ADTS, error) {
	return &ADTSImpl{}, nil
}

func (v *ADTSImpl) SetASC(asc []byte) (err error) {
	return v.asc.UnmarshalBinary(asc)
}

func (v *ADTSImpl) Encode(raw []byte) (data []byte, err error) {
	if err = v.asc.validate(); err != nil {
		return nil, errors.WithMessage(err, "adts encode")
	}

	// write the ADTS header.
	// Refer to @doc ISO_IEC_13818-7-AAC-2004.pdf, @page 26, @section 6.2 Audio Data Transport Stream, ADTS
	// byte_alignment()

	// adts_fixed_header:
	//      12bits syncword,
	//      16bits left.
	// adts_variable_header:
	//      28bits
	//      12+16+28=56bits
	// adts_error_check:
	//      16bits if protection_absent
	//      56+16=72bits
	// if protection_absent:
	//      require(7bytes)=56bits
	// else
	//      require(9bytes)=72bits
	aacFixedHeader := make([]byte, 7)
	p := aacFixedHeader

	// Syncword 12 bslbf
	p[0] = byte(0xff)
	// 4bits left.
	// Refer to @doc ISO_IEC_13818-7-AAC-2004.pdf, @page 27, @section 6.2.1 Fixed Header of ADTS
	// ID 1 bslbf
	// Layer 2 uimsbf
	// protection_absent 1 bslbf
	p[1] = byte(0xf1)

	// profile 2 uimsbf
	// sampling_frequency_index 4 uimsbf
	// private_bit 1 bslbf
	// channel_configuration 3 uimsbf
	// original/copy 1 bslbf
	// home 1 bslbf
	profile := v.asc.Object.ToProfile()
	p[2] = byte((profile<<6)&0xc0) | byte((v.asc.SampleRate<<2)&0x3c) | byte((v.asc.Channels>>2)&0x01)

	// 4bits left.
	// Refer to @doc ISO_IEC_13818-7-AAC-2004.pdf, @page 27, @section 6.2.2 Variable Header of ADTS
	// copyright_identification_bit 1 bslbf
	// copyright_identification_start 1 bslbf
	aacFrameLength := uint16(len(raw) + len(aacFixedHeader))
	p[3] = byte((v.asc.Channels<<6)&0xc0) | byte((aacFrameLength>>11)&0x03)

	// aac_frame_length 13 bslbf: Length of the frame including headers and error_check in bytes.
	// use the left 2bits as the 13 and 12 bit,
	// the aac_frame_length is 13bits, so we move 13-2=11.
	p[4] = byte(aacFrameLength >> 3)
	// adts_buffer_fullness 11 bslbf
	p[5] = byte(aacFrameLength<<5) & byte(0xe0)

	// no_raw_data_blocks_in_frame 2 uimsbf
	p[6] = byte(0xfc)

	return append(p, raw...), nil
}

func (v *ADTSImpl) Decode(data []byte) (raw, left []byte, err error) {
	// write the ADTS header.
	// Refer to @doc ISO_IEC_13818-7-AAC-2004.pdf, @page 26, @section 6.2 Audio Data Transport Stream, ADTS
	// @see https://github.com/ossrs/srs/issues/212#issuecomment-64145885
	// byte_alignment()
	p := data
	if len(p) <= 7 {
		return nil, nil, errors.Errorf("requires 7+ but only %v bytes", len(p))
	}

	// matched 12bits 0xFFF,
	// @remark, we must cast the 0xff to char to compare.
	if p[0] != 0xff || p[1]&0xf0 != 0xf0 {
		return nil, nil, errors.Errorf("invalid signature %#x", uint8(p[1]&0xf0))
	}

	// Syncword 12 bslbf
	_ = p[0]
	// 4bits left.
	// Refer to @doc ISO_IEC_13818-7-AAC-2004.pdf, @page 27, @section 6.2.1 Fixed Header of ADTS
	// ID 1 bslbf
	// layer 2 uimsbf
	// protection_absent 1 bslbf
	pat := uint8(p[1]) & 0x0f
	id := (pat >> 3) & 0x01
	//layer := (pat >> 1) & 0x03
	protectionAbsent := pat & 0x01

	// ID: MPEG identifier, set to '1' if the audio data in the ADTS stream are MPEG-2 AAC (See ISO/IEC 13818-7)
	// and set to '0' if the audio data are MPEG-4. See also ISO/IEC 11172-3, subclause 2.4.2.3.
	if id != 0x01 {
		// well, some system always use 0, but actually is aac format.
		// for example, houjian vod ts always set the aac id to 0, actually 1.
		// we just ignore it, and alwyas use 1(aac) to demux.
		id = 0x01
	}

	sfiv := uint16(p[2])<<8 | uint16(p[3])
	// profile 2 uimsbf
	// sampling_frequency_index 4 uimsbf
	// private_bit 1 bslbf
	// channel_configuration 3 uimsbf
	// original/copy 1 bslbf
	// home 1 bslbf
	profile := Profile(uint8(sfiv>>14) & 0x03)
	samplingFrequencyIndex := uint8(sfiv>>10) & 0x0f
	//private_bit := (t >> 9) & 0x01
	channelConfiguration := uint8(sfiv>>6) & 0x07
	//original := uint8(sfiv >> 5) & 0x01
	//home := uint8(sfiv >> 4) & 0x01
	// 4bits left.
	// Refer to @doc ISO_IEC_13818-7-AAC-2004.pdf, @page 27, @section 6.2.2 Variable Header of ADTS
	// copyright_identification_bit 1 bslbf
	// copyright_identification_start 1 bslbf
	//fh_copyright_identification_bit = uint8(sfiv >> 3) & 0x01
	//fh_copyright_identification_start = uint8(sfiv >> 2) & 0x01
	// frame_length 13 bslbf: Length of the frame including headers and error_check in bytes.
	// use the left 2bits as the 13 and 12 bit,
	// the frame_length is 13bits, so we move 13-2=11.
	frameLength := (sfiv << 11) & 0x1800

	abfv := uint32(p[4])<<16 | uint32(p[5])<<8 | uint32(p[6])
	p = p[7:]

	// frame_length 13 bslbf: consume the first 13-2=11bits
	// the fh2 is 24bits, so we move right 24-11=13.
	frameLength |= uint16((abfv >> 13) & 0x07ff)
	// adts_buffer_fullness 11 bslbf
	//fh_adts_buffer_fullness = (abfv >> 2) & 0x7ff
	// number_of_raw_data_blocks_in_frame 2 uimsbf
	//number_of_raw_data_blocks_in_frame = abfv & 0x03
	// adts_error_check(), 1.A.2.2.3 Error detection
	if protectionAbsent == 0 {
		if len(p) <= 2 {
			return nil, nil, errors.Errorf("requires 2+ but only %v bytes", len(p))
		}
		// crc_check 16 Rpchof
		p = p[2:]
	}

	v.asc.Object = profile.ToObjectType()
	v.asc.Channels = Channels(channelConfiguration)
	v.asc.SampleRate = SampleRateIndex(samplingFrequencyIndex)

	nbRaw := int(frameLength - 7)
	if len(p) < nbRaw {
		return nil, nil, errors.Errorf("requires %v but only %v bytes", nbRaw, len(p))
	}
	raw = p[:nbRaw]
	left = p[nbRaw:]

	if err = v.asc.validate(); err != nil {
		return nil, nil, errors.WithMessage(err, "adts decode")
	}

	return
}

func (v *ADTSImpl) ASC() *AudioSpecificConfig {
	return &v.asc
}

// Convert the ASC(Audio Specific Configuration).
// Refer to @doc ISO_IEC_14496-3-AAC-2001.pdf, @page 33, @section 1.6.2.1 AudioSpecificConfig
type AudioSpecificConfig struct {
	Object     ObjectType      // AAC object type.
	SampleRate SampleRateIndex // AAC sample rate, not the FLV sampling rate.
	Channels   Channels        // AAC channel configuration.
}

func (v *AudioSpecificConfig) validate() (err error) {
	switch v.Object {
	case ObjectTypeMain, ObjectTypeLC, ObjectTypeSSR, ObjectTypeHE, ObjectTypeHEv2:
	default:
		return errors.Errorf("invalid object %#x", uint8(v.Object))
	}

	if v.SampleRate < SampleRateIndex88kHz || v.SampleRate > SampleRateIndex7kHz {
		return errors.Errorf("invalid sample-rate %#x", uint8(v.SampleRate))
	}

	if v.Channels < ChannelMono || v.Channels > Channel7_1 {
		return errors.Errorf("invalid channels %#x", uint8(v.Channels))
	}
	return
}

func (v *AudioSpecificConfig) UnmarshalBinary(data []byte) (err error) {
	// AudioSpecificConfig
	// Refer to @doc ISO_IEC_14496-3-AAC-2001.pdf, @page 33, @section 1.6.2.1 AudioSpecificConfig
	//
	// only need to decode the first 2bytes:
	// audioObjectType, 5bits.
	// samplingFrequencyIndex, aac_sample_rate, 4bits.
	// channelConfiguration, aac_channels, 4bits
	//
	// @see SrsAacTransmuxer::write_audio
	if len(data) < 2 {
		return errors.Errorf("requires 2 but only %v bytes", len(data))
	}

	t0, t1 := uint8(data[0]), uint8(data[1])

	v.Object = ObjectType((t0 >> 3) & 0x1f)
	v.SampleRate = SampleRateIndex(((t0 << 1) & 0x0e) | ((t1 >> 7) & 0x01))
	v.Channels = Channels((t1 >> 3) & 0x0f)

	return v.validate()
}

func (v *AudioSpecificConfig) MarshalBinary() (data []byte, err error) {
	if err = v.validate(); err != nil {
		return
	}

	// AudioSpecificConfig
	// Refer to @doc ISO_IEC_14496-3-AAC-2001.pdf, @page 33, @section 1.6.2.1 AudioSpecificConfig
	//
	// only need to decode the first 2bytes:
	// audioObjectType, 5bits.
	// samplingFrequencyIndex, aac_sample_rate, 4bits.
	// channelConfiguration, aac_channels, 4bits
	return []byte{
		byte(byte(v.Object)&0x1f)<<3 | byte(byte(v.SampleRate)&0x0e)>>1,
		byte(byte(v.SampleRate)&0x01)<<7 | byte(byte(v.Channels)&0x0f)<<3,
	}, nil
}
