// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"strings"
	"testing"
)

const (
	whipURL = "http://127.0.0.1:1985/rtc/v1/whip/?app=live&stream=test"
	whepURL = "http://127.0.0.1:1985/rtc/v1/whep/?app=live&stream=test"
)

func TestParseOptionsPublishTakesOneMP4Input(t *testing.T) {
	o, err := parseOptions([]string{
		"-hide_banner", "-loglevel", "debug",
		"-re", "-stream_loop", "-1", "-i", "source.mp4",
		"-f", "whip", whipURL,
	})
	if err != nil {
		t.Fatalf("parseOptions: %v", err)
	}
	if !o.HideBanner {
		t.Errorf("HideBanner = false, want true")
	}
	if o.LogLevel != "debug" {
		t.Errorf("LogLevel = %q, want debug", o.LogLevel)
	}
	want := []inputOption{{Format: "mp4", Realtime: true, StreamLoop: -1, Path: "source.mp4"}}
	if len(o.Inputs) != 1 || o.Inputs[0] != want[0] {
		t.Errorf("Inputs = %+v, want %+v", o.Inputs, want)
	}
	if o.OutputFormat != "whip" || o.Output != whipURL {
		t.Errorf("output = %q %q, want whip %q", o.OutputFormat, o.Output, whipURL)
	}
}

func TestParseOptionsPlayTakesWhepInputAndNullOutput(t *testing.T) {
	o, err := parseOptions([]string{"-f", "whep", "-i", whepURL, "-f", "null", "-"})
	if err != nil {
		t.Fatalf("parseOptions: %v", err)
	}
	if o.HideBanner {
		t.Errorf("HideBanner = true, want false")
	}
	if o.LogLevel != "info" {
		t.Errorf("LogLevel = %q, want the default info", o.LogLevel)
	}
	want := []inputOption{{Format: "whep", Path: whepURL}}
	if len(o.Inputs) != 1 || o.Inputs[0] != want[0] {
		t.Errorf("Inputs = %+v, want %+v", o.Inputs, want)
	}
	if o.OutputFormat != "null" || o.Output != "-" {
		t.Errorf("output = %q %q, want null -", o.OutputFormat, o.Output)
	}
}

func TestParseOptionsLetsInputFormatOverrideTheExtension(t *testing.T) {
	// -f mp4 overrides the .bin extension, and -stream_loop without -re is kept.
	o, err := parseOptions([]string{"-stream_loop", "2", "-f", "mp4", "-i", "raw.bin", "-f", "whip", whipURL})
	if err != nil {
		t.Fatalf("parseOptions: %v", err)
	}
	want := []inputOption{{Format: "mp4", StreamLoop: 2, Path: "raw.bin"}}
	if len(o.Inputs) != 1 || o.Inputs[0] != want[0] {
		t.Errorf("Inputs = %+v, want %+v", o.Inputs, want)
	}
}

func TestParseOptionsRejectsBadCommandLinesNamingTheOption(t *testing.T) {
	cases := []struct {
		name string
		args []string
		want string
	}{
		{"unknown option", []string{"-foo", "-i", "a.mp4", "-f", "whip", whipURL}, "Unrecognized option 'foo'"},
		{"framerate is gone with the raw input", []string{"-framerate", "25", "-i", "a.mp4", "-f", "whip", whipURL}, "Unrecognized option 'framerate'"},
		{"missing argument", []string{"-i", "a.mp4", "-f", "whip", whipURL, "-loglevel"}, "Missing argument for option 'loglevel'"},
		{"bad loglevel", []string{"-loglevel", "loud", "-i", "a.mp4", "-f", "whip", whipURL}, "Invalid loglevel 'loud'; use quiet, panic, fatal, error, warning, info, verbose, debug or trace"},
		{"bad stream_loop", []string{"-stream_loop", "x", "-i", "a.mp4", "-f", "whip", whipURL}, "Invalid value 'x' for option 'stream_loop'"},
		{"input option after the inputs", []string{"-i", "a.mp4", "-stream_loop", "-1", "-f", "whip", whipURL}, "Option stream_loop applies to an input and must precede -i"},
		{"second output", []string{"-i", "a.mp4", "-f", "whip", whipURL, "rtmp://x"}, "A second output 'rtmp://x' is not supported"},
		{"no output", []string{"-i", "a.mp4"}, "At least one output file must be specified"},
		{"no input", []string{"-f", "whip", whipURL}, "No input file; use -i before the output"},
		{"output without format", []string{"-i", "a.mp4", whipURL}, "Unable to choose an output format for '" + whipURL + "'; use -f whip or -f null"},
		{"unknown output format", []string{"-i", "a.mp4", "-f", "rtmp", "rtmp://x"}, "Unknown output format 'rtmp'; use whip or null"},
		{"unknown input format", []string{"-f", "h264", "-i", "a.h264", "-f", "whip", whipURL}, "Unknown input format 'h264'; use mp4 or whep"},
		{"input without format", []string{"-i", "a.h264", "-f", "whip", whipURL}, "Unable to choose an input format for 'a.h264'; use -f mp4 or -f whep"},
		{"whip with a whep input", []string{"-f", "whep", "-i", whepURL, "-f", "whip", whipURL}, "Output format whip takes one mp4 input, not whep '" + whepURL + "'"},
		{"whip with two inputs", []string{"-i", "a.mp4", "-i", "b.mp4", "-f", "whip", whipURL}, "Output format whip takes one mp4 input, not 2 inputs"},
		{"null with a file input", []string{"-i", "a.mp4", "-f", "null", "-"}, "Output format null takes one whep input, not mp4 'a.mp4'"},
		{"null with a file target", []string{"-f", "whep", "-i", whepURL, "-f", "null", "out.bin"}, "Output format null writes to -, not 'out.bin'"},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			_, err := parseOptions(c.args)
			if err == nil {
				t.Fatalf("parseOptions(%q) succeeded, want error %q", c.args, c.want)
			}
			if err.Error() != c.want {
				t.Errorf("error = %q, want %q", err.Error(), c.want)
			}
		})
	}
}

// PION_WHIP_RTX unset or on offers rtx and lets SRS choose the format. This case passes from the start: it locks in
// the accepted default.
func TestRtxFromEnvDefaultsToOn(t *testing.T) {
	for _, value := range []string{"", "on"} {
		rtx, err := rtxFromEnv(func(string) string { return value })
		if err != nil {
			t.Fatalf("PION_WHIP_RTX=%q: %v", value, err)
		}
		if !rtx {
			t.Errorf("PION_WHIP_RTX=%q: rtx = false, want true", value)
		}
	}
}

// PION_WHIP_RTX=off forces plain retransmission from the client side: the offer carries no rtx, so SRS answers
// plain whatever its nack_prefer_rtx says, and the same running SRS can be verified in both formats.
func TestRtxFromEnvOffForcesPlain(t *testing.T) {
	env := map[string]string{envRtx: "off"}
	rtx, err := rtxFromEnv(func(key string) string { return env[key] })
	if err != nil {
		t.Fatalf("PION_WHIP_RTX=off: %v", err)
	}
	if rtx {
		t.Errorf("PION_WHIP_RTX=off: rtx = true, want false")
	}
}

// Any other value is rejected by name, like a bad option, never ignored.
func TestRtxFromEnvRejectsOtherValues(t *testing.T) {
	for _, value := range []string{"plain", "1", "true", "OFF"} {
		_, err := rtxFromEnv(func(string) string { return value })
		want := "Invalid value '" + value + "' for PION_WHIP_RTX; use on or off"
		if err == nil {
			t.Errorf("PION_WHIP_RTX=%q: no error, want %q", value, want)
			continue
		}
		if !strings.Contains(err.Error(), want) {
			t.Errorf("PION_WHIP_RTX=%q: error = %q, want it to contain %q", value, err.Error(), want)
		}
	}
}
