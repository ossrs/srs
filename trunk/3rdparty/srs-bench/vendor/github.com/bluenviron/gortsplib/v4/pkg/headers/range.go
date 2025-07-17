package headers

import (
	"fmt"
	"strconv"
	"strings"
	"time"

	"github.com/bluenviron/gortsplib/v4/pkg/base"
)

func leadingZero(v uint) string {
	ret := ""
	if v < 10 {
		ret += "0"
	}
	ret += strconv.FormatUint(uint64(v), 10)
	return ret
}

// RangeSMPTETime is a time expressed in SMPTE unit.
type RangeSMPTETime struct {
	Time     time.Duration
	Frame    uint
	Subframe uint
}

func (t *RangeSMPTETime) unmarshal(s string) error {
	parts := strings.Split(s, ":")
	if len(parts) != 3 && len(parts) != 4 {
		return fmt.Errorf("invalid SMPTE time (%v)", s)
	}

	tmp, err := strconv.ParseUint(parts[0], 10, 64)
	if err != nil {
		return err
	}
	hours := tmp

	tmp, err = strconv.ParseUint(parts[1], 10, 64)
	if err != nil {
		return err
	}
	mins := tmp

	tmp, err = strconv.ParseUint(parts[2], 10, 64)
	if err != nil {
		return err
	}
	seconds := tmp

	t.Time = time.Duration(seconds+mins*60+hours*3600) * time.Second

	if len(parts) == 4 {
		parts = strings.Split(parts[3], ".")
		if len(parts) == 2 {
			tmp, err := strconv.ParseUint(parts[0], 10, 64)
			if err != nil {
				return err
			}
			t.Frame = uint(tmp)

			tmp, err = strconv.ParseUint(parts[1], 10, 64)
			if err != nil {
				return err
			}
			t.Subframe = uint(tmp)
		} else {
			tmp, err := strconv.ParseUint(parts[0], 10, 64)
			if err != nil {
				return err
			}
			t.Frame = uint(tmp)
		}
	}

	return nil
}

func (t RangeSMPTETime) marshal() string {
	d := uint64(t.Time.Seconds())
	hours := d / 3600
	d %= 3600
	mins := d / 60
	secs := d % 60

	ret := strconv.FormatUint(hours, 10) + ":" + leadingZero(uint(mins)) + ":" + leadingZero(uint(secs))

	if t.Frame > 0 || t.Subframe > 0 {
		ret += ":" + leadingZero(t.Frame)

		if t.Subframe > 0 {
			ret += "." + leadingZero(t.Subframe)
		}
	}

	return ret
}

// RangeSMPTE is a range expressed in SMPTE unit.
type RangeSMPTE struct {
	Start RangeSMPTETime
	End   *RangeSMPTETime
}

func (r *RangeSMPTE) unmarshal(start string, end string) error {
	err := r.Start.unmarshal(start)
	if err != nil {
		return err
	}

	if end != "" {
		var v RangeSMPTETime
		err := v.unmarshal(end)
		if err != nil {
			return err
		}
		r.End = &v
	}

	return nil
}

func (r RangeSMPTE) marshal() string {
	ret := "smpte=" + r.Start.marshal() + "-"
	if r.End != nil {
		ret += r.End.marshal()
	}
	return ret
}

func unmarshalRangeNPTTime(d *time.Duration, s string) error {
	parts := strings.Split(s, ":")
	if len(parts) > 3 {
		return fmt.Errorf("invalid NPT time (%v)", s)
	}

	var hours uint64
	if len(parts) == 3 {
		tmp, err := strconv.ParseUint(parts[0], 10, 64)
		if err != nil {
			return err
		}
		hours = tmp
		parts = parts[1:]
	}

	var mins uint64
	if len(parts) >= 2 {
		tmp, err := strconv.ParseUint(parts[0], 10, 64)
		if err != nil {
			return err
		}
		mins = tmp
		parts = parts[1:]
	}

	tmp, err := strconv.ParseFloat(parts[0], 64)
	if err != nil {
		return err
	}
	seconds := tmp

	*d = time.Duration(seconds*float64(time.Second)) +
		time.Duration(mins*60+hours*3600)*time.Second

	return nil
}

func marshalRangeNPTTime(d time.Duration) string {
	return strconv.FormatFloat(d.Seconds(), 'f', -1, 64)
}

// RangeNPT is a range expressed in NPT units.
type RangeNPT struct {
	Start time.Duration
	End   *time.Duration
}

func (r *RangeNPT) unmarshal(start string, end string) error {
	err := unmarshalRangeNPTTime(&r.Start, start)
	if err != nil {
		return err
	}

	if end != "" {
		var v time.Duration
		err := unmarshalRangeNPTTime(&v, end)
		if err != nil {
			return err
		}
		r.End = &v
	}

	return nil
}

func (r RangeNPT) marshal() string {
	ret := "npt=" + marshalRangeNPTTime(r.Start) + "-"
	if r.End != nil {
		ret += marshalRangeNPTTime(*r.End)
	}
	return ret
}

func unmarshalRangeUTCTime(t *time.Time, s string) error {
	tmp, err := time.Parse("20060102T150405Z", s)
	if err != nil {
		return err
	}
	*t = tmp
	return nil
}

func marshalRangeUTCTime(t time.Time) string {
	return t.Format("20060102T150405Z")
}

// RangeUTC is a range expressed in UTC units.
type RangeUTC struct {
	Start time.Time
	End   *time.Time
}

func (r *RangeUTC) unmarshal(start string, end string) error {
	err := unmarshalRangeUTCTime(&r.Start, start)
	if err != nil {
		return err
	}

	if end != "" {
		var v time.Time
		err := unmarshalRangeUTCTime(&v, end)
		if err != nil {
			return err
		}
		r.End = &v
	}

	return nil
}

func (r RangeUTC) marshal() string {
	ret := "clock=" + marshalRangeUTCTime(r.Start) + "-"
	if r.End != nil {
		ret += marshalRangeUTCTime(*r.End)
	}
	return ret
}

// RangeValue can be
// - RangeSMPTE
// - RangeNPT
// - RangeUTC
type RangeValue interface {
	unmarshal(string, string) error
	marshal() string
}

func rangeValueUnmarshal(s RangeValue, v string) error {
	parts := strings.Split(v, "-")
	if len(parts) != 2 {
		return fmt.Errorf("invalid value (%v)", v)
	}

	return s.unmarshal(parts[0], parts[1])
}

// Range is a Range header.
type Range struct {
	// range expressed in a certain unit.
	Value RangeValue

	// time at which the operation is to be made effective.
	Time *time.Time
}

// Unmarshal decodes a Range header.
func (h *Range) Unmarshal(v base.HeaderValue) error {
	if len(v) == 0 {
		return fmt.Errorf("value not provided")
	}

	if len(v) > 1 {
		return fmt.Errorf("value provided multiple times (%v)", v)
	}

	v0 := v[0]

	kvs, err := keyValParse(v0, ';')
	if err != nil {
		return err
	}

	specFound := false

	for k, v := range kvs {
		switch k {
		case "smpte":
			s := &RangeSMPTE{}
			err := rangeValueUnmarshal(s, v)
			if err != nil {
				return err
			}

			specFound = true
			h.Value = s

		case "npt":
			s := &RangeNPT{}
			err := rangeValueUnmarshal(s, v)
			if err != nil {
				return err
			}

			specFound = true
			h.Value = s

		case "clock":
			s := &RangeUTC{}
			err := rangeValueUnmarshal(s, v)
			if err != nil {
				return err
			}

			specFound = true
			h.Value = s

		case "time":
			var t time.Time
			err := unmarshalRangeUTCTime(&t, v)
			if err != nil {
				return err
			}

			h.Time = &t
		}
	}

	if !specFound {
		return fmt.Errorf("value not found (%v)", v[0])
	}

	return nil
}

// Marshal encodes a Range header.
func (h Range) Marshal() base.HeaderValue {
	v := h.Value.marshal()
	if h.Time != nil {
		v += ";time=" + marshalRangeUTCTime(*h.Time)
	}
	return base.HeaderValue{v}
}
