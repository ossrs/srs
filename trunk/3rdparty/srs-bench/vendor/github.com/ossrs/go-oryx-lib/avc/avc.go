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

// The oryx AVC package includes some utilites.
// The NALU(Netowrk Abstraction Layer Unit) is suitable for transmission over network.
// We could package NALUs by AnnexB, IBMF or RTP according to different scenarios.
// 	@note AnnexB is designed for bit-oriented stream, such as MPEG-TS/HLS, please
// 		read ISO_IEC_14496-10-AVC-2003.pdf at page 211, AnnexB Byte stream Format.
//	@note IBMF is designed for file storage, such as MP4/FLV, please read
//		ISO_IEC_14496-15-AVC-format-2012.pdf at page 16, 5.2.4.1 AVC decoder
// 		configuration record.
//	@note RTP payload for H.264, defined in RFC6184 https://tools.ietf.org/html/rfc6184
//		it directly uses and extends the NAL header.
package avc

import (
	"bytes"
	"fmt"
	"github.com/ossrs/go-oryx-lib/errors"
)

// @doc ISO_IEC_14496-10-AVC-2003.pdf at page 44, 7.3.1 NAL unit syntax
type NALRefIDC uint8

// @doc ISO_IEC_14496-10-AVC-2003.pdf at page 44, 7.3.1 NAL unit syntax
type NALUType uint8

const (
	NALUTypeNonIDR                NALUType = 1  // Coded slice of a non-IDR picture slice_layer_without_partitioning_rbsp( )
	NALUTypeDataPartitionA        NALUType = 2  // Coded slice data partition A slice_data_partition_a_layer_rbsp( )
	NALUTypeDataPartitionB        NALUType = 3  // Coded slice data partition B slice_data_partition_b_layer_rbsp( )
	NALUTypeDataPartitionC        NALUType = 4  // Coded slice data partition C slice_data_partition_c_layer_rbsp( )
	NALUTypeIDR                   NALUType = 5  // Coded slice of an IDR picture slice_layer_without_partitioning_rbsp( )
	NALUTypeSEI                   NALUType = 6  // Supplemental enhancement information (SEI) sei_rbsp( )
	NALUTypeSPS                   NALUType = 7  // Sequence parameter set seq_parameter_set_rbsp( )
	NALUTypePPS                   NALUType = 8  // Picture parameter set pic_parameter_set_rbsp( )
	NALUTypeAccessUnitDelimiter   NALUType = 9  // Access unit delimiter access_unit_delimiter_rbsp( )
	NALUTypeEOSequence            NALUType = 10 // End of sequence end_of_seq_rbsp( )
	NALUTypeEOStream              NALUType = 11 // End of stream end_of_stream_rbsp( )
	NALUTypeFilterData            NALUType = 12 // Filler data filler_data_rbsp( )
	NALUTypeSPSExt                NALUType = 13 // Sequence parameter set extension seq_parameter_set_extension_rbsp( )
	NALUTypePrefixNALU            NALUType = 14 // Prefix NAL unit prefix_nal_unit_rbsp( )
	NALUTypeSubsetSPS             NALUType = 15 // Subset sequence parameter set subset_seq_parameter_set_rbsp( )
	NALUTypeLayerWithoutPartition NALUType = 19 // Coded slice of an auxiliary coded picture without partitioning slice_layer_without_partitioning_rbsp( )
	NALUTypeCodedSliceExt         NALUType = 20 // Coded slice extension slice_layer_extension_rbsp( )
)

func (v NALUType) String() string {
	switch v {
	case NALUTypeNonIDR:
		return "NonIDR"
	case NALUTypeDataPartitionA:
		return "DataPartitionA"
	case NALUTypeDataPartitionB:
		return "DataPartitionB"
	case NALUTypeDataPartitionC:
		return "DataPartitionC"
	case NALUTypeIDR:
		return "IDR"
	case NALUTypeSEI:
		return "SEI"
	case NALUTypeSPS:
		return "SPS"
	case NALUTypePPS:
		return "PPS"
	case NALUTypeAccessUnitDelimiter:
		return "AccessUnitDelimiter"
	case NALUTypeEOSequence:
		return "EOSequence"
	case NALUTypeEOStream:
		return "EOStream"
	case NALUTypeFilterData:
		return "FilterData"
	case NALUTypeSPSExt:
		return "SPSExt"
	case NALUTypePrefixNALU:
		return "PrefixNALU"
	case NALUTypeSubsetSPS:
		return "SubsetSPS"
	case NALUTypeLayerWithoutPartition:
		return "LayerWithoutPartition"
	case NALUTypeCodedSliceExt:
		return "CodedSliceExt"
	default:
		return "Forbidden"
		return fmt.Sprintf("NALU/%v", uint8(v))
	}
}

// @doc ISO_IEC_14496-10-AVC-2003.pdf at page 60, 7.4.1 NAL unit semantics
type NALUHeader struct {
	// The 2-bits nal_ref_idc.
	NALRefIDC NALRefIDC
	// The 5-bits nal_unit_type.
	NALUType NALUType
}

func NewNALUHeader() *NALUHeader {
	return &NALUHeader{}
}

func (v *NALUHeader) String() string {
	return fmt.Sprintf("%v, NRI=%v", v.NALUType, v.NALRefIDC)
}

func (v *NALUHeader) Size() int {
	return 1
}

func (v *NALUHeader) UnmarshalBinary(data []byte) error {
	if len(data) < 1 {
		return errors.New("empty NALU")
	}
	v.NALRefIDC = NALRefIDC(uint8(data[0]>>5) & 0x03)
	v.NALUType = NALUType(uint8(data[0]) & 0x1f)
	return nil
}

func (v *NALUHeader) MarshalBinary() ([]byte, error) {
	return []byte{
		byte(v.NALRefIDC)<<5 | byte(v.NALUType),
	}, nil
}

// @doc ISO_IEC_14496-10-AVC-2003.pdf at page 60, 7.4.1 NAL unit semantics
type NALU struct {
	*NALUHeader
	Data []byte
}

func NewNALU() *NALU {
	return &NALU{NALUHeader: NewNALUHeader()}
}

func (v *NALU) String() string {
	return fmt.Sprintf("%v, size=%vB", v.NALUHeader, len(v.Data))
}

func (v *NALU) Size() int {
	return 1 + len(v.Data)
}

func (v *NALU) UnmarshalBinary(data []byte) error {
	if err := v.NALUHeader.UnmarshalBinary(data); err != nil {
		return errors.WithMessage(err, "unmarshal")
	}

	v.Data = data[1:]
	return nil
}

func (v *NALU) MarshalBinary() ([]byte, error) {
	b, err := v.NALUHeader.MarshalBinary()
	if err != nil {
		return nil, errors.WithMessage(err, "marshal")
	}

	if len(v.Data) == 0 {
		return b, nil
	}
	return append(b, v.Data...), nil
}

// @doc Annex A Profiles and levels, ISO_IEC_14496-10-AVC-2003.pdf, page 205.
// @note that it's uint8 in IBMF, but extended in other specs, so we use uint16.
type AVCProfile uint16

const (
	// @see ffmpeg, libavcodec/avcodec.h:2713
	AVCProfileBaseline            AVCProfile = 66
	AVCProfileConstrainedBaseline AVCProfile = 578
	AVCProfileMain                AVCProfile = 77
	AVCProfileExtended            AVCProfile = 88
	AVCProfileHigh                AVCProfile = 100
	AVCProfileHigh10              AVCProfile = 110
	AVCProfileHigh10Intra         AVCProfile = 2158
	AVCProfileHigh422             AVCProfile = 122
	AVCProfileHigh422Intra        AVCProfile = 2170
	AVCProfileHigh444             AVCProfile = 144
	AVCProfileHigh444Predictive   AVCProfile = 244
	AVCProfileHigh444Intra        AVCProfile = 2192
)

func (v AVCProfile) String() string {
	switch v {
	case AVCProfileBaseline:
		return "Baseline"
	case AVCProfileConstrainedBaseline:
		return "ConstrainedBaseline"
	case AVCProfileMain:
		return "Main"
	case AVCProfileExtended:
		return "Extended"
	case AVCProfileHigh:
		return "High"
	case AVCProfileHigh10:
		return "High10"
	case AVCProfileHigh10Intra:
		return "High10Intra"
	case AVCProfileHigh422:
		return "High422"
	case AVCProfileHigh422Intra:
		return "High422Intra"
	case AVCProfileHigh444:
		return "High444"
	case AVCProfileHigh444Predictive:
		return "High444Predictive"
	case AVCProfileHigh444Intra:
		return "High444Intra"
	default:
		return "Forbidden"
	}
}

// @doc Annex A Profiles and levels, ISO_IEC_14496-10-AVC-2003.pdf, page 207.
type AVCLevel uint8

const (
	AVCLevel_1  = 10
	AVCLevel_11 = 11
	AVCLevel_12 = 12
	AVCLevel_13 = 13
	AVCLevel_2  = 20
	AVCLevel_21 = 21
	AVCLevel_22 = 22
	AVCLevel_3  = 30
	AVCLevel_31 = 31
	AVCLevel_32 = 32
	AVCLevel_4  = 40
	AVCLevel_41 = 41
	AVCLevel_5  = 50
	AVCLevel_51 = 51
)

func (v AVCLevel) String() string {
	switch v {
	case AVCLevel_1:
		return "Level_1"
	case AVCLevel_11:
		return "Level_11"
	case AVCLevel_12:
		return "Level_12"
	case AVCLevel_13:
		return "Level_13"
	case AVCLevel_2:
		return "Level_2"
	case AVCLevel_21:
		return "Level_21"
	case AVCLevel_22:
		return "Level_22"
	case AVCLevel_3:
		return "Level_3"
	case AVCLevel_31:
		return "Level_31"
	case AVCLevel_32:
		return "Level_32"
	case AVCLevel_4:
		return "Level_4"
	case AVCLevel_41:
		return "Level_41"
	case AVCLevel_5:
		return "Level_5"
	case AVCLevel_51:
		return "Level_51"
	default:
		return "Forbidden"
	}
}

// @doc ISO_IEC_14496-15-AVC-format-2012.pdf at page 16, 5.2.4.1.1 Syntax
type AVCDecoderConfigurationRecord struct {
	// It contains the profile code as defined in ISO/IEC 14496-10.
	configurationVersion uint8
	// It is a byte defined exactly the same as the byte which occurs between the
	// profile_IDC and level_IDC in a sequence parameter set (SPS), as defined in
	// ISO/IEC 14496-10.
	// @remark It's 8 bits.
	AVCProfileIndication AVCProfile
	// It contains the level code as defined in ISO/IEC 14496-10.
	profileCompatibility uint8
	// It indicates the length in bytes of the NALUnitLength field in an AVC video sample
	// or AVC parameter set sample of the associated stream minus one.
	AVCLevelIndication AVCLevel
	// It indicates the length in bytes of the NALUnitLength field in an AVC video sample
	// or AVC parameter set sample of the associated stream minus one.
	LengthSizeMinusOne uint8
	// It contains a SPS NAL unit, as specified in ISO/IEC 14496-10. SPSs shall occur in
	// order of ascending parameter set identifier with gaps being allowed.
	SequenceParameterSetNALUnits []*NALU
	// It contains a PPS NAL unit, as specified in ISO/IEC 14496-10. PPSs shall occur in
	// order of ascending parameter set identifier with gaps being allowed.
	PictureParameterSetNALUnits []*NALU
	// @remark We ignore the sequenceParameterSetExtNALUnit.
}

func NewAVCDecoderConfigurationRecord() *AVCDecoderConfigurationRecord {
	v := &AVCDecoderConfigurationRecord{}
	v.configurationVersion = 0x01
	return v
}

func (v *AVCDecoderConfigurationRecord) MarshalBinary() ([]byte, error) {
	var buf bytes.Buffer
	buf.WriteByte(byte(v.configurationVersion))
	buf.WriteByte(byte(v.AVCProfileIndication))
	buf.WriteByte(byte(v.profileCompatibility))
	buf.WriteByte(byte(v.AVCLevelIndication))
	buf.WriteByte(byte(v.LengthSizeMinusOne))

	// numOfSequenceParameterSets
	buf.WriteByte(byte(len(v.SequenceParameterSetNALUnits)))
	for _, sps := range v.SequenceParameterSetNALUnits {
		b, err := sps.MarshalBinary()
		if err != nil {
			return nil, errors.WithMessage(err, "sps")
		}

		sequenceParameterSetLength := uint16(len(b))
		buf.WriteByte(byte(sequenceParameterSetLength >> 8))
		buf.WriteByte(byte(sequenceParameterSetLength))
		buf.Write(b)
	}

	// numOfPictureParameterSets
	buf.WriteByte(byte(len(v.PictureParameterSetNALUnits)))
	for _, pps := range v.PictureParameterSetNALUnits {
		b, err := pps.MarshalBinary()
		if err != nil {
			return nil, errors.WithMessage(err, "pps")
		}

		pictureParameterSetLength := uint16(len(b))
		buf.WriteByte(byte(pictureParameterSetLength >> 8))
		buf.WriteByte(byte(pictureParameterSetLength))
		buf.Write(b)
	}

	return buf.Bytes(), nil
}

func (v *AVCDecoderConfigurationRecord) UnmarshalBinary(data []byte) error {
	b := data
	if len(b) < 6 {
		return errors.Errorf("requires 6+ only %v bytes", len(b))
	}

	v.configurationVersion = uint8(b[0])
	v.AVCProfileIndication = AVCProfile(uint8(b[1]))
	v.profileCompatibility = uint8(b[2])
	v.AVCLevelIndication = AVCLevel(uint8(b[3]))
	v.LengthSizeMinusOne = uint8(b[4]) & 0x03
	b = b[5:]

	numOfSequenceParameterSets := uint8(b[0]) & 0x1f
	b = b[1:]
	for i := 0; i < int(numOfSequenceParameterSets); i++ {
		if len(b) < 2 {
			return errors.Errorf("requires 2+ only %v bytes", len(b))
		}
		sequenceParameterSetLength := int(uint16(b[0])<<8 | uint16(b[1]))
		b = b[2:]

		if len(b) < sequenceParameterSetLength {
			return errors.Errorf("requires %v only %v bytes", sequenceParameterSetLength, len(b))
		}
		sps := NewNALU()
		if err := sps.UnmarshalBinary(b[:sequenceParameterSetLength]); err != nil {
			return errors.WithMessage(err, "unmarshal")
		}
		b = b[sequenceParameterSetLength:]

		v.SequenceParameterSetNALUnits = append(v.SequenceParameterSetNALUnits, sps)
	}

	if len(b) < 1 {
		return errors.New("no PPS length")
	}
	numOfPictureParameterSets := uint8(b[0])
	b = b[1:]
	for i := 0; i < int(numOfPictureParameterSets); i++ {
		if len(b) < 2 {
			return errors.Errorf("requiers 2+ only %v bytes", len(b))
		}

		pictureParameterSetLength := int(uint16(b[0])<<8 | uint16(b[1]))
		b = b[2:]

		if len(b) < pictureParameterSetLength {
			return errors.Errorf("requires %v only %v bytes", pictureParameterSetLength, len(b))
		}
		pps := NewNALU()
		if err := pps.UnmarshalBinary(b[:pictureParameterSetLength]); err != nil {
			return errors.WithMessage(err, "unmarshal")
		}
		b = b[pictureParameterSetLength:]

		v.PictureParameterSetNALUnits = append(v.PictureParameterSetNALUnits, pps)
	}
	return nil
}

// @doc ISO_IEC_14496-15-AVC-format-2012.pdf at page 20, 5.3.4.2 Sample format
type AVCSample struct {
	lengthSizeMinusOne uint8
	NALUs              []*NALU
}

func NewAVCSample(lengthSizeMinusOne uint8) *AVCSample {
	return &AVCSample{lengthSizeMinusOne: lengthSizeMinusOne}
}

func (v *AVCSample) MarshalBinary() ([]byte, error) {
	sizeOfNALU := int(v.lengthSizeMinusOne) + 1

	var buf bytes.Buffer
	for _, nalu := range v.NALUs {
		b, err := nalu.MarshalBinary()
		if err != nil {
			return nil, errors.WithMessage(err, "write")
		}

		length := uint64(len(b))
		for i := 0; i < sizeOfNALU; i++ {
			buf.WriteByte(byte(length >> uint8(8*(sizeOfNALU-1-i))))
		}
		buf.Write(b)
	}

	return buf.Bytes(), nil
}

func (v *AVCSample) UnmarshalBinary(data []byte) error {
	sizeOfNALU := int(v.lengthSizeMinusOne) + 1
	for b := data; len(b) > 0; {
		if len(b) < sizeOfNALU {
			return errors.Errorf("requires %v+ only %v bytes", sizeOfNALU, len(b))
		}

		var length uint64
		for i := 0; i < sizeOfNALU; i++ {
			length |= uint64(b[i]) << uint8(8*(sizeOfNALU-1-i))
		}
		b = b[sizeOfNALU:]

		if len(b) < int(length) {
			return errors.Errorf("requires %v only %v bytes", length, len(b))
		}

		nalu := NewNALU()
		if err := nalu.UnmarshalBinary(b[:length]); err != nil {
			return errors.WithMessage(err, "unmarshal")
		}
		b = b[length:]

		v.NALUs = append(v.NALUs, nalu)
	}
	return nil
}
