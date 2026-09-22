// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"strings"
	"testing"
	"time"
)

const testWhepURL = "http://127.0.0.1:1985/rtc/v1/whep/?app=live&stream=rtx"

// The play command line with every option in the subset: each one lands on the input or the
// output it precedes, as FFmpeg places them.
func TestParseOptionsPlayTakesWhepInputAndNullOutput(t *testing.T) {
	o, err := parseOptions([]string{
		"-hide_banner", "-loglevel", "debug", "-f", "whep", "-i", testWhepURL, "-t", "10", "-f", "null", "-",
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
	if len(o.Inputs) != 1 || o.Inputs[0].Format != "whep" || o.Inputs[0].Path != testWhepURL {
		t.Errorf("Inputs = %+v, want one whep input %q", o.Inputs, testWhepURL)
	}
	if o.Duration != 10*time.Second {
		t.Errorf("Duration = %v, want 10s", o.Duration)
	}
	if o.OutputFormat != "null" || o.Output != "-" {
		t.Errorf("output = %q %q, want null -", o.OutputFormat, o.Output)
	}
}

// Without -f before -i an http or https URL is a WHEP input, the log level is info as in FFmpeg, the banner is
// printed, and without -t the tool plays until interrupted.
func TestParseOptionsInfersWhepFromTheURL(t *testing.T) {
	o, err := parseOptions([]string{"-i", testWhepURL, "-f", "null", "-"})
	if err != nil {
		t.Fatalf("parseOptions: %v", err)
	}
	if o.HideBanner {
		t.Errorf("HideBanner = true, want false")
	}
	if o.LogLevel != "info" {
		t.Errorf("LogLevel = %q, want info", o.LogLevel)
	}
	if len(o.Inputs) != 1 || o.Inputs[0].Format != "whep" || o.Inputs[0].Path != testWhepURL {
		t.Errorf("Inputs = %+v, want one whep input %q", o.Inputs, testWhepURL)
	}
	if o.Duration != 0 {
		t.Errorf("Duration = %v, want 0", o.Duration)
	}
}

// A fractional -t is seconds, as in FFmpeg.
func TestParseOptionsDurationIsSeconds(t *testing.T) {
	o, err := parseOptions([]string{"-i", testWhepURL, "-t", "2.5", "-f", "null", "-"})
	if err != nil {
		t.Fatalf("parseOptions: %v", err)
	}
	if o.Duration != 2500*time.Millisecond {
		t.Errorf("Duration = %v, want 2.5s", o.Duration)
	}
}

// A bad command line is rejected with FFmpeg's wording, the same as pion-whip's, naming the option, value or file,
// so a script never has an option silently ignored. The publish-only input options are outside this subset.
func TestParseOptionsRejectsBadCommandLinesNamingTheOption(t *testing.T) {
	cases := []struct {
		args []string
		want string
	}{
		{[]string{"-re", "-i", testWhepURL, "-f", "null", "-"}, "Unrecognized option 're'"},
		{[]string{"-stream_loop", "-1", "-i", testWhepURL, "-f", "null", "-"}, "Unrecognized option 'stream_loop'"},
		{[]string{"-framerate", "25", "-i", testWhepURL, "-f", "null", "-"}, "Unrecognized option 'framerate'"},
		{[]string{"-i", testWhepURL, "-i", "http://second/", "-f", "null", "-"}, "one whep input, not 2 inputs"},
		{[]string{"-i", testWhepURL, "-f", "null", "-", "out2"}, "A second output 'out2' is not supported"},
		{[]string{"-f", "null", "-"}, "No input file"},
		{[]string{"-i", testWhepURL}, "At least one output file must be specified"},
		{[]string{"-f", "whip", "-i", testWhepURL, "-f", "null", "-"}, "Unknown input format 'whip'; use whep"},
		{[]string{"-i", "video.mp4", "-f", "null", "-"}, "Unable to choose an input format for 'video.mp4'; use -f whep"},
		{[]string{"-i", testWhepURL, "-"}, "Unable to choose an output format for '-'; use -f null"},
		{[]string{"-i", testWhepURL, "-f", "flv", "out.flv"}, "Unknown output format 'flv'; use null"},
		{[]string{"-i", testWhepURL, "-f", "null", "out.txt"}, "Output format null writes to -, not 'out.txt'"},
		{[]string{"-loglevel", "chatty", "-i", testWhepURL, "-f", "null", "-"}, "Invalid loglevel 'chatty'"},
		{[]string{"-i", testWhepURL, "-t", "soon", "-f", "null", "-"}, "Invalid value 'soon' for option 't'"},
		{[]string{"-i", testWhepURL, "-t", "-3", "-f", "null", "-"}, "Invalid value '-3' for option 't'"},
		{[]string{"-i", testWhepURL, "-f"}, "Missing argument for option 'f'"},
	}
	for _, c := range cases {
		_, err := parseOptions(c.args)
		if err == nil {
			t.Errorf("%v: no error, want %q", c.args, c.want)
			continue
		}
		if !strings.Contains(err.Error(), c.want) {
			t.Errorf("%v: error = %q, want it to contain %q", c.args, err.Error(), c.want)
		}
	}
}

// PION_WHEP_RTX chooses the retransmission format the tool offers, since FFmpeg has no option for it. Unset or on
// offers rtx for every video codec as a browser does and lets SRS choose. On is the
// default, so the tool offers rtx when the variable is absent.
func TestRtxFromEnvDefaultsToOn(t *testing.T) {
	for _, value := range []string{"", "on"} {
		rtx, err := rtxFromEnv(func(string) string { return value })
		if err != nil {
			t.Fatalf("PION_WHEP_RTX=%q: %v", value, err)
		}
		if !rtx {
			t.Errorf("PION_WHEP_RTX=%q: rtx = false, want true", value)
		}
	}
}

// PION_WHEP_RTX=off forces plain retransmission from the client side: the offer carries no rtx, so SRS answers
// plain whatever its nack_prefer_rtx says, and the same running SRS can be verified in both formats.
func TestRtxFromEnvOffForcesPlain(t *testing.T) {
	env := map[string]string{envRtx: "off"}
	rtx, err := rtxFromEnv(func(key string) string { return env[key] })
	if err != nil {
		t.Fatalf("PION_WHEP_RTX=off: %v", err)
	}
	if rtx {
		t.Errorf("PION_WHEP_RTX=off: rtx = true, want false")
	}
}

// Any other value is rejected by name, like a bad option, never ignored.
func TestRtxFromEnvRejectsOtherValues(t *testing.T) {
	for _, value := range []string{"plain", "1", "true", "OFF"} {
		_, err := rtxFromEnv(func(string) string { return value })
		want := "Invalid value '" + value + "' for PION_WHEP_RTX; use on or off"
		if err == nil {
			t.Errorf("PION_WHEP_RTX=%q: no error, want %q", value, want)
			continue
		}
		if !strings.Contains(err.Error(), want) {
			t.Errorf("PION_WHEP_RTX=%q: error = %q, want it to contain %q", value, err.Error(), want)
		}
	}
}
