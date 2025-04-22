package h264

import (
	"fmt"

	"github.com/bluenviron/mediacommon/v2/pkg/bits"
)

const (
	maxRefFrames = 255
)

func readScalingList(buf []byte, pos *int, size int) ([]int32, bool, error) {
	lastScale := int32(8)
	nextScale := int32(8)
	scalingList := make([]int32, size)
	var useDefaultScalingMatrixFlag bool

	for j := 0; j < size; j++ {
		if nextScale != 0 {
			deltaScale, err := bits.ReadGolombSigned(buf, pos)
			if err != nil {
				return nil, false, err
			}

			nextScale = (lastScale + deltaScale + 256) % 256
			useDefaultScalingMatrixFlag = (j == 0 && nextScale == 0)
		}

		if nextScale == 0 {
			scalingList[j] = lastScale
		} else {
			scalingList[j] = nextScale
		}

		lastScale = scalingList[j]
	}

	return scalingList, useDefaultScalingMatrixFlag, nil
}

// SPS_HRD is a hypotetical reference decoder.
type SPS_HRD struct { //nolint:revive
	CpbCntMinus1                       uint32
	BitRateScale                       uint8
	CpbSizeScale                       uint8
	BitRateValueMinus1                 []uint32
	CpbSizeValueMinus1                 []uint32
	CbrFlag                            []bool
	InitialCpbRemovalDelayLengthMinus1 uint8
	CpbRemovalDelayLengthMinus1        uint8
	DpbOutputDelayLengthMinus1         uint8
	TimeOffsetLength                   uint8
}

func (h *SPS_HRD) unmarshal(buf []byte, pos *int) error {
	var err error
	h.CpbCntMinus1, err = bits.ReadGolombUnsigned(buf, pos)
	if err != nil {
		return err
	}

	err = bits.HasSpace(buf, *pos, 8)
	if err != nil {
		return err
	}

	if h.CpbCntMinus1 > 31 {
		return fmt.Errorf("invalid cpb_cnt_minus1")
	}

	h.BitRateScale = uint8(bits.ReadBitsUnsafe(buf, pos, 4))
	h.CpbSizeScale = uint8(bits.ReadBitsUnsafe(buf, pos, 4))

	h.BitRateValueMinus1 = make([]uint32, h.CpbCntMinus1+1)
	h.CpbSizeValueMinus1 = make([]uint32, h.CpbCntMinus1+1)
	h.CbrFlag = make([]bool, h.CpbCntMinus1+1)

	for i := uint32(0); i <= h.CpbCntMinus1; i++ {
		h.BitRateValueMinus1[i], err = bits.ReadGolombUnsigned(buf, pos)
		if err != nil {
			return err
		}

		h.CpbSizeValueMinus1[i], err = bits.ReadGolombUnsigned(buf, pos)
		if err != nil {
			return err
		}

		h.CbrFlag[i], err = bits.ReadFlag(buf, pos)
		if err != nil {
			return err
		}
	}

	err = bits.HasSpace(buf, *pos, 5+5+5+5)
	if err != nil {
		return err
	}

	h.InitialCpbRemovalDelayLengthMinus1 = uint8(bits.ReadBitsUnsafe(buf, pos, 5))
	h.CpbRemovalDelayLengthMinus1 = uint8(bits.ReadBitsUnsafe(buf, pos, 5))
	h.DpbOutputDelayLengthMinus1 = uint8(bits.ReadBitsUnsafe(buf, pos, 5))
	h.TimeOffsetLength = uint8(bits.ReadBitsUnsafe(buf, pos, 5))

	return nil
}

// SPS_TimingInfo is a timing info.
type SPS_TimingInfo struct { //nolint:revive
	NumUnitsInTick     uint32
	TimeScale          uint32
	FixedFrameRateFlag bool
}

func (t *SPS_TimingInfo) unmarshal(buf []byte, pos *int) error {
	err := bits.HasSpace(buf, *pos, 32+32+1)
	if err != nil {
		return err
	}

	t.NumUnitsInTick = uint32(bits.ReadBitsUnsafe(buf, pos, 32))
	t.TimeScale = uint32(bits.ReadBitsUnsafe(buf, pos, 32))
	t.FixedFrameRateFlag = bits.ReadFlagUnsafe(buf, pos)

	return nil
}

// SPS_BitstreamRestriction are bitstream restriction infos.
type SPS_BitstreamRestriction struct { //nolint:revive
	MotionVectorsOverPicBoundariesFlag bool
	MaxBytesPerPicDenom                uint32
	MaxBitsPerMbDenom                  uint32
	Log2MaxMvLengthHorizontal          uint32
	Log2MaxMvLengthVertical            uint32
	MaxNumReorderFrames                uint32
	MaxDecFrameBuffering               uint32
}

func (r *SPS_BitstreamRestriction) unmarshal(buf []byte, pos *int) error {
	var err error
	r.MotionVectorsOverPicBoundariesFlag, err = bits.ReadFlag(buf, pos)
	if err != nil {
		return err
	}

	r.MaxBytesPerPicDenom, err = bits.ReadGolombUnsigned(buf, pos)
	if err != nil {
		return err
	}

	r.MaxBitsPerMbDenom, err = bits.ReadGolombUnsigned(buf, pos)
	if err != nil {
		return err
	}

	r.Log2MaxMvLengthHorizontal, err = bits.ReadGolombUnsigned(buf, pos)
	if err != nil {
		return err
	}

	r.Log2MaxMvLengthVertical, err = bits.ReadGolombUnsigned(buf, pos)
	if err != nil {
		return err
	}

	r.MaxNumReorderFrames, err = bits.ReadGolombUnsigned(buf, pos)
	if err != nil {
		return err
	}

	r.MaxDecFrameBuffering, err = bits.ReadGolombUnsigned(buf, pos)
	if err != nil {
		return err
	}

	return nil
}

// SPS_VUI is a video usability information.
type SPS_VUI struct { //nolint:revive
	AspectRatioInfoPresentFlag bool

	// AspectRatioInfoPresentFlag == true
	AspectRatioIdc uint8
	SarWidth       uint16
	SarHeight      uint16

	OverscanInfoPresentFlag bool

	// OverscanInfoPresentFlag == true
	OverscanAppropriateFlag    bool
	VideoSignalTypePresentFlag bool

	// VideoSignalTypePresentFlag == true
	VideoFormat                  uint8
	VideoFullRangeFlag           bool
	ColourDescriptionPresentFlag bool

	// ColourDescriptionPresentFlag == true
	ColourPrimaries         uint8
	TransferCharacteristics uint8
	MatrixCoefficients      uint8

	ChromaLocInfoPresentFlag bool

	// ChromaLocInfoPresentFlag == true
	ChromaSampleLocTypeTopField    uint32
	ChromaSampleLocTypeBottomField uint32

	TimingInfo *SPS_TimingInfo
	NalHRD     *SPS_HRD
	VclHRD     *SPS_HRD

	LowDelayHrdFlag      bool
	PicStructPresentFlag bool
	BitstreamRestriction *SPS_BitstreamRestriction
}

func (v *SPS_VUI) unmarshal(buf []byte, pos *int) error {
	var err error
	v.AspectRatioInfoPresentFlag, err = bits.ReadFlag(buf, pos)
	if err != nil {
		return err
	}

	if v.AspectRatioInfoPresentFlag {
		var tmp uint64
		tmp, err = bits.ReadBits(buf, pos, 8)
		if err != nil {
			return err
		}
		v.AspectRatioIdc = uint8(tmp)

		if v.AspectRatioIdc == 255 { // Extended_SAR
			err = bits.HasSpace(buf, *pos, 32)
			if err != nil {
				return err
			}

			v.SarWidth = uint16(bits.ReadBitsUnsafe(buf, pos, 16))
			v.SarHeight = uint16(bits.ReadBitsUnsafe(buf, pos, 16))
		}
	}

	v.OverscanInfoPresentFlag, err = bits.ReadFlag(buf, pos)
	if err != nil {
		return err
	}

	if v.OverscanInfoPresentFlag {
		v.OverscanAppropriateFlag, err = bits.ReadFlag(buf, pos)
		if err != nil {
			return err
		}
	}

	v.VideoSignalTypePresentFlag, err = bits.ReadFlag(buf, pos)
	if err != nil {
		return err
	}

	if v.VideoSignalTypePresentFlag {
		err = bits.HasSpace(buf, *pos, 5)
		if err != nil {
			return err
		}

		v.VideoFormat = uint8(bits.ReadBitsUnsafe(buf, pos, 3))
		v.VideoFullRangeFlag = bits.ReadFlagUnsafe(buf, pos)
		v.ColourDescriptionPresentFlag = bits.ReadFlagUnsafe(buf, pos)

		if v.ColourDescriptionPresentFlag {
			err = bits.HasSpace(buf, *pos, 24)
			if err != nil {
				return err
			}

			v.ColourPrimaries = uint8(bits.ReadBitsUnsafe(buf, pos, 8))
			v.TransferCharacteristics = uint8(bits.ReadBitsUnsafe(buf, pos, 8))
			v.MatrixCoefficients = uint8(bits.ReadBitsUnsafe(buf, pos, 8))
		}
	}

	v.ChromaLocInfoPresentFlag, err = bits.ReadFlag(buf, pos)
	if err != nil {
		return err
	}

	if v.ChromaLocInfoPresentFlag {
		v.ChromaSampleLocTypeTopField, err = bits.ReadGolombUnsigned(buf, pos)
		if err != nil {
			return err
		}

		v.ChromaSampleLocTypeBottomField, err = bits.ReadGolombUnsigned(buf, pos)
		if err != nil {
			return err
		}
	}

	timingInfoPresentFlag, err := bits.ReadFlag(buf, pos)
	if err != nil {
		return err
	}

	if timingInfoPresentFlag {
		v.TimingInfo = &SPS_TimingInfo{}
		err = v.TimingInfo.unmarshal(buf, pos)
		if err != nil {
			return err
		}
	}

	nalHrdParametersPresentFlag, err := bits.ReadFlag(buf, pos)
	if err != nil {
		return err
	}

	if nalHrdParametersPresentFlag {
		v.NalHRD = &SPS_HRD{}
		err = v.NalHRD.unmarshal(buf, pos)
		if err != nil {
			return err
		}
	}

	vclHrdParametersPresentFlag, err := bits.ReadFlag(buf, pos)
	if err != nil {
		return err
	}

	if vclHrdParametersPresentFlag {
		v.VclHRD = &SPS_HRD{}
		err = v.VclHRD.unmarshal(buf, pos)
		if err != nil {
			return err
		}
	}

	if nalHrdParametersPresentFlag || vclHrdParametersPresentFlag {
		v.LowDelayHrdFlag, err = bits.ReadFlag(buf, pos)
		if err != nil {
			return err
		}
	}

	v.PicStructPresentFlag, err = bits.ReadFlag(buf, pos)
	if err != nil {
		return err
	}

	bitstreamRestrictionFlag, err := bits.ReadFlag(buf, pos)
	if err != nil {
		return err
	}

	if bitstreamRestrictionFlag {
		v.BitstreamRestriction = &SPS_BitstreamRestriction{}
		err := v.BitstreamRestriction.unmarshal(buf, pos)
		if err != nil {
			return err
		}
	}

	return nil
}

// SPS_FrameCropping is the frame cropping part of a SPS.
type SPS_FrameCropping struct { //nolint:revive
	LeftOffset   uint32
	RightOffset  uint32
	TopOffset    uint32
	BottomOffset uint32
}

func (c *SPS_FrameCropping) unmarshal(buf []byte, pos *int) error {
	var err error
	c.LeftOffset, err = bits.ReadGolombUnsigned(buf, pos)
	if err != nil {
		return err
	}

	c.RightOffset, err = bits.ReadGolombUnsigned(buf, pos)
	if err != nil {
		return err
	}

	c.TopOffset, err = bits.ReadGolombUnsigned(buf, pos)
	if err != nil {
		return err
	}

	c.BottomOffset, err = bits.ReadGolombUnsigned(buf, pos)
	if err != nil {
		return err
	}

	return nil
}

// SPS is a H264 sequence parameter set.
// Specification: ITU-T Rec. H.264, 7.3.2.1.1
type SPS struct {
	ProfileIdc         uint8
	ConstraintSet0Flag bool
	ConstraintSet1Flag bool
	ConstraintSet2Flag bool
	ConstraintSet3Flag bool
	ConstraintSet4Flag bool
	ConstraintSet5Flag bool
	LevelIdc           uint8
	ID                 uint32

	// only for selected ProfileIdcs
	ChromaFormatIdc                 uint32
	SeparateColourPlaneFlag         bool
	BitDepthLumaMinus8              uint32
	BitDepthChromaMinus8            uint32
	QpprimeYZeroTransformBypassFlag bool

	// seqScalingListPresentFlag == true
	ScalingList4x4                 [][]int32
	UseDefaultScalingMatrix4x4Flag []bool
	ScalingList8x8                 [][]int32
	UseDefaultScalingMatrix8x8Flag []bool

	Log2MaxFrameNumMinus4 uint32
	PicOrderCntType       uint32

	// PicOrderCntType == 0
	Log2MaxPicOrderCntLsbMinus4 uint32

	// PicOrderCntType == 1
	DeltaPicOrderAlwaysZeroFlag bool
	OffsetForNonRefPic          int32
	OffsetForTopToBottomField   int32
	OffsetForRefFrames          []int32

	MaxNumRefFrames                uint32
	GapsInFrameNumValueAllowedFlag bool
	PicWidthInMbsMinus1            uint32
	PicHeightInMapUnitsMinus1      uint32
	FrameMbsOnlyFlag               bool

	// FrameMbsOnlyFlag == false
	MbAdaptiveFrameFieldFlag bool

	Direct8x8InferenceFlag bool
	FrameCropping          *SPS_FrameCropping
	VUI                    *SPS_VUI
}

// Unmarshal decodes a SPS from bytes.
func (s *SPS) Unmarshal(buf []byte) error {
	if len(buf) < 1 {
		return fmt.Errorf("not enough bits")
	}

	if NALUType(buf[0]&0x1F) != NALUTypeSPS {
		return fmt.Errorf("not a SPS")
	}

	buf = EmulationPreventionRemove(buf[1:])

	if len(buf) < 3 {
		return fmt.Errorf("not enough bits")
	}

	s.ProfileIdc = buf[0]
	s.ConstraintSet0Flag = (buf[1] >> 7) == 1
	s.ConstraintSet1Flag = (buf[1] >> 6 & 0x01) == 1
	s.ConstraintSet2Flag = (buf[1] >> 5 & 0x01) == 1
	s.ConstraintSet3Flag = (buf[1] >> 4 & 0x01) == 1
	s.ConstraintSet4Flag = (buf[1] >> 3 & 0x01) == 1
	s.ConstraintSet5Flag = (buf[1] >> 2 & 0x01) == 1
	s.LevelIdc = buf[2]

	buf = buf[3:]
	pos := 0

	var err error
	s.ID, err = bits.ReadGolombUnsigned(buf, &pos)
	if err != nil {
		return err
	}

	switch s.ProfileIdc {
	case 100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135:
		s.ChromaFormatIdc, err = bits.ReadGolombUnsigned(buf, &pos)
		if err != nil {
			return err
		}

		if s.ChromaFormatIdc == 3 {
			s.SeparateColourPlaneFlag, err = bits.ReadFlag(buf, &pos)
			if err != nil {
				return err
			}
		} else {
			s.SeparateColourPlaneFlag = false
		}

		s.BitDepthLumaMinus8, err = bits.ReadGolombUnsigned(buf, &pos)
		if err != nil {
			return err
		}

		s.BitDepthChromaMinus8, err = bits.ReadGolombUnsigned(buf, &pos)
		if err != nil {
			return err
		}

		s.QpprimeYZeroTransformBypassFlag, err = bits.ReadFlag(buf, &pos)
		if err != nil {
			return err
		}

		var seqScalingMatrixPresentFlag bool
		seqScalingMatrixPresentFlag, err = bits.ReadFlag(buf, &pos)
		if err != nil {
			return err
		}

		if seqScalingMatrixPresentFlag {
			var lim int
			if s.ChromaFormatIdc != 3 {
				lim = 8
			} else {
				lim = 12
			}

			for i := 0; i < lim; i++ {
				var seqScalingListPresentFlag bool
				seqScalingListPresentFlag, err = bits.ReadFlag(buf, &pos)
				if err != nil {
					return err
				}

				if seqScalingListPresentFlag {
					if i < 6 {
						var scalingList []int32
						var useDefaultScalingMatrixFlag bool
						scalingList, useDefaultScalingMatrixFlag, err = readScalingList(buf, &pos, 16)
						if err != nil {
							return err
						}

						s.ScalingList4x4 = append(s.ScalingList4x4, scalingList)
						s.UseDefaultScalingMatrix4x4Flag = append(s.UseDefaultScalingMatrix4x4Flag,
							useDefaultScalingMatrixFlag)
					} else {
						var scalingList []int32
						var useDefaultScalingMatrixFlag bool
						scalingList, useDefaultScalingMatrixFlag, err = readScalingList(buf, &pos, 64)
						if err != nil {
							return err
						}

						s.ScalingList8x8 = append(s.ScalingList8x8, scalingList)
						s.UseDefaultScalingMatrix8x8Flag = append(s.UseDefaultScalingMatrix8x8Flag,
							useDefaultScalingMatrixFlag)
					}
				}
			}
		}

	default:
		s.ChromaFormatIdc = 1
		s.SeparateColourPlaneFlag = false
		s.BitDepthLumaMinus8 = 0
		s.BitDepthChromaMinus8 = 0
		s.QpprimeYZeroTransformBypassFlag = false
	}

	s.Log2MaxFrameNumMinus4, err = bits.ReadGolombUnsigned(buf, &pos)
	if err != nil {
		return err
	}

	s.PicOrderCntType, err = bits.ReadGolombUnsigned(buf, &pos)
	if err != nil {
		return err
	}

	switch s.PicOrderCntType {
	case 0:
		s.Log2MaxPicOrderCntLsbMinus4, err = bits.ReadGolombUnsigned(buf, &pos)
		if err != nil {
			return err
		}

		s.DeltaPicOrderAlwaysZeroFlag = false
		s.OffsetForNonRefPic = 0
		s.OffsetForTopToBottomField = 0
		s.OffsetForRefFrames = nil

	case 1:
		s.Log2MaxPicOrderCntLsbMinus4 = 0

		s.DeltaPicOrderAlwaysZeroFlag, err = bits.ReadFlag(buf, &pos)
		if err != nil {
			return err
		}

		s.OffsetForNonRefPic, err = bits.ReadGolombSigned(buf, &pos)
		if err != nil {
			return err
		}

		s.OffsetForTopToBottomField, err = bits.ReadGolombSigned(buf, &pos)
		if err != nil {
			return err
		}

		var numRefFramesInPicOrderCntCycle uint32
		numRefFramesInPicOrderCntCycle, err = bits.ReadGolombUnsigned(buf, &pos)
		if err != nil {
			return err
		}

		if numRefFramesInPicOrderCntCycle > maxRefFrames {
			return fmt.Errorf("num_ref_frames_in_pic_order_cnt_cycle exceeds %d", maxRefFrames)
		}

		s.OffsetForRefFrames = make([]int32, numRefFramesInPicOrderCntCycle)
		for i := uint32(0); i < numRefFramesInPicOrderCntCycle; i++ {
			var v int32
			v, err = bits.ReadGolombSigned(buf, &pos)
			if err != nil {
				return err
			}

			s.OffsetForRefFrames[i] = v
		}

	case 2:
		s.Log2MaxPicOrderCntLsbMinus4 = 0
		s.DeltaPicOrderAlwaysZeroFlag = false
		s.OffsetForNonRefPic = 0
		s.OffsetForTopToBottomField = 0
		s.OffsetForRefFrames = nil

	default:
		return fmt.Errorf("invalid pic_order_cnt_type: %d", s.PicOrderCntType)
	}

	s.MaxNumRefFrames, err = bits.ReadGolombUnsigned(buf, &pos)
	if err != nil {
		return err
	}

	s.GapsInFrameNumValueAllowedFlag, err = bits.ReadFlag(buf, &pos)
	if err != nil {
		return err
	}

	s.PicWidthInMbsMinus1, err = bits.ReadGolombUnsigned(buf, &pos)
	if err != nil {
		return err
	}

	s.PicHeightInMapUnitsMinus1, err = bits.ReadGolombUnsigned(buf, &pos)
	if err != nil {
		return err
	}

	s.FrameMbsOnlyFlag, err = bits.ReadFlag(buf, &pos)
	if err != nil {
		return err
	}

	if !s.FrameMbsOnlyFlag {
		s.MbAdaptiveFrameFieldFlag, err = bits.ReadFlag(buf, &pos)
		if err != nil {
			return err
		}
	} else {
		s.MbAdaptiveFrameFieldFlag = false
	}

	s.Direct8x8InferenceFlag, err = bits.ReadFlag(buf, &pos)
	if err != nil {
		return err
	}

	frameCroppingFlag, err := bits.ReadFlag(buf, &pos)
	if err != nil {
		return err
	}

	if frameCroppingFlag {
		s.FrameCropping = &SPS_FrameCropping{}
		err = s.FrameCropping.unmarshal(buf, &pos)
		if err != nil {
			return err
		}
	} else {
		s.FrameCropping = nil
	}

	vuiParametersPresentFlag, err := bits.ReadFlag(buf, &pos)
	if err != nil {
		return err
	}

	if vuiParametersPresentFlag {
		s.VUI = &SPS_VUI{}
		err := s.VUI.unmarshal(buf, &pos)
		if err != nil {
			return err
		}
	} else {
		s.VUI = nil
	}

	return nil
}

// Width returns the video width.
func (s SPS) Width() int {
	var subWidthC uint32
	switch {
	case s.ChromaFormatIdc == 1 && !s.SeparateColourPlaneFlag:
		subWidthC = 2

	case s.ChromaFormatIdc == 2 && !s.SeparateColourPlaneFlag:
		subWidthC = 2

	case s.ChromaFormatIdc == 3 && !s.SeparateColourPlaneFlag:
		subWidthC = 1
	}

	var chromaArrayType uint32
	if !s.SeparateColourPlaneFlag {
		chromaArrayType = s.ChromaFormatIdc
	} else {
		chromaArrayType = 0
	}

	var cropUnitX uint32
	if chromaArrayType == 0 {
		cropUnitX = 0
	} else {
		cropUnitX = subWidthC
	}

	picWidthInSamplesL := ((s.PicWidthInMbsMinus1 + 1) * 16)

	if s.FrameCropping != nil {
		return int(picWidthInSamplesL - cropUnitX*(s.FrameCropping.LeftOffset+s.FrameCropping.RightOffset))
	}

	return int(picWidthInSamplesL)
}

// Height returns the video height.
func (s SPS) Height() int {
	var subHeightC uint32
	switch {
	case s.ChromaFormatIdc == 1 && !s.SeparateColourPlaneFlag:
		subHeightC = 2

	case s.ChromaFormatIdc == 2 && !s.SeparateColourPlaneFlag:
		subHeightC = 1

	case s.ChromaFormatIdc == 3 && !s.SeparateColourPlaneFlag:
		subHeightC = 1
	}

	var frameMbsOnlyFlagUint32 uint32
	if s.FrameMbsOnlyFlag {
		frameMbsOnlyFlagUint32 = 1
	}

	var chromaArrayType uint32
	if !s.SeparateColourPlaneFlag {
		chromaArrayType = s.ChromaFormatIdc
	} else {
		chromaArrayType = 0
	}

	var cropUnitY uint32
	if chromaArrayType == 0 {
		cropUnitY = 2 - frameMbsOnlyFlagUint32
	} else {
		cropUnitY = subHeightC * (2 - frameMbsOnlyFlagUint32)
	}

	frameHeightInMbs := (2 - frameMbsOnlyFlagUint32) * (s.PicHeightInMapUnitsMinus1 + 1)

	if s.FrameCropping != nil {
		return int(16*frameHeightInMbs - cropUnitY*(s.FrameCropping.TopOffset+s.FrameCropping.BottomOffset))
	}

	picHeightInMbs := frameHeightInMbs // / (1 + s.FieldPicFlag)
	picHeightInSamplesL := picHeightInMbs * 16

	return int(picHeightInSamplesL)
}

// FPS returns the frames per second of the video.
func (s SPS) FPS() float64 {
	if s.VUI == nil || s.VUI.TimingInfo == nil {
		return 0
	}

	return float64(s.VUI.TimingInfo.TimeScale) / (2 * float64(s.VUI.TimingInfo.NumUnitsInTick))
}
