// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"errors"
	"fmt"
	"strconv"
	"strings"
)

// inputOption is one -i input with the FFmpeg input options that preceded it.
type inputOption struct {
	// Format is mp4 or whep, from -f before the -i or from the file extension.
	Format string
	// Realtime is -re: read the input at its native rate.
	Realtime bool
	// StreamLoop is -stream_loop: how many extra times to loop the input, -1 forever.
	StreamLoop int
	// Path is the file path, or the WHEP URL for the whep format.
	Path string
}

// options is the parsed command line, an FFmpeg-style subset.
type options struct {
	HideBanner bool
	LogLevel   string
	// Rtx is whether the offer carries rtx, from the PION_WHIP_RTX environment variable, on by default; off forces
	// plain retransmission from the client side.
	Rtx          bool
	Inputs       []inputOption
	OutputFormat string // whip or null
	Output       string // the WHIP URL, or - for null
}

var logLevels = []string{"quiet", "panic", "fatal", "error", "warning", "info", "verbose", "debug", "trace"}

func logLevelIndex(level string) int {
	for i, s := range logLevels {
		if s == level {
			return i
		}
	}
	return -1
}

// envRtx is the environment variable that selects the retransmission format the tool offers, because FFmpeg has
// no option for it and the command line stays FFmpeg's: on, or unset, offers rtx with a FID group as a browser does
// and lets SRS choose; off omits rtx from the offer so the session can only use plain retransmission.
const envRtx = "PION_WHIP_RTX"

// rtxFromEnv reads envRtx through getenv: "" or "on" is true, "off" is false, and anything else is an error
// naming the variable and the value, never ignored, as with a bad option.
func rtxFromEnv(getenv func(string) string) (bool, error) {
	switch value := getenv(envRtx); value {
	case "", "on":
		return true, nil
	case "off":
		return false, nil
	default:
		return false, fmt.Errorf("Invalid value '%s' for %s; use on or off", value, envRtx)
	}
}

// parseOptions parses args, the command line without the program name, with
// FFmpeg's placement rules: an input option applies to the -i that follows it,
// -f before an -i names the input format, -f after the last -i names the output
// format, and the first bare argument is the single output. Publishing takes
// exactly one MP4 input, playing exactly one WHEP input. The messages follow
// FFmpeg's wording so the scripts read alike.
func parseOptions(args []string) (*options, error) {
	o := &options{LogLevel: "info"}
	var pending inputOption // input options waiting for the next -i
	var pendingInputOpt string
	var pendingFormat string
	outputSeen := false

	for i := 0; i < len(args); i++ {
		arg := args[i]
		if !strings.HasPrefix(arg, "-") || arg == "-" {
			if outputSeen {
				return nil, fmt.Errorf("A second output '%s' is not supported", arg)
			}
			if pendingInputOpt != "" {
				return nil, fmt.Errorf("Option %s applies to an input and must precede -i", pendingInputOpt)
			}
			if pendingFormat == "" {
				return nil, fmt.Errorf("Unable to choose an output format for '%s'; use -f whip or -f null", arg)
			}
			if pendingFormat != "whip" && pendingFormat != "null" {
				return nil, fmt.Errorf("Unknown output format '%s'; use whip or null", pendingFormat)
			}
			o.OutputFormat, o.Output, outputSeen, pendingFormat = pendingFormat, arg, true, ""
			continue
		}

		name := strings.TrimPrefix(arg, "-")
		value := func() (string, error) {
			if i+1 >= len(args) {
				return "", fmt.Errorf("Missing argument for option '%s'", name)
			}
			i++
			return args[i], nil
		}
		switch name {
		case "hide_banner":
			o.HideBanner = true
		case "loglevel":
			v, err := value()
			if err != nil {
				return nil, err
			}
			if !contains(logLevels, v) {
				return nil, fmt.Errorf("Invalid loglevel '%s'; use %s or %s", v, strings.Join(logLevels[:len(logLevels)-1], ", "), logLevels[len(logLevels)-1])
			}
			o.LogLevel = v
		case "re":
			pending.Realtime = true
			if pendingInputOpt == "" {
				pendingInputOpt = name
			}
		case "stream_loop":
			v, err := value()
			if err != nil {
				return nil, err
			}
			n, err := strconv.Atoi(v)
			if err != nil || n < -1 {
				return nil, fmt.Errorf("Invalid value '%s' for option 'stream_loop'", v)
			}
			pending.StreamLoop = n
			if pendingInputOpt == "" {
				pendingInputOpt = name
			}
		case "f":
			v, err := value()
			if err != nil {
				return nil, err
			}
			pendingFormat = v
		case "i":
			v, err := value()
			if err != nil {
				return nil, err
			}
			format := pendingFormat
			if format == "" {
				if strings.HasSuffix(strings.ToLower(v), ".mp4") {
					format = "mp4"
				} else {
					return nil, fmt.Errorf("Unable to choose an input format for '%s'; use -f mp4 or -f whep", v)
				}
			}
			if format != "mp4" && format != "whep" {
				return nil, fmt.Errorf("Unknown input format '%s'; use mp4 or whep", format)
			}
			pending.Format, pending.Path = format, v
			o.Inputs = append(o.Inputs, pending)
			pending, pendingInputOpt, pendingFormat = inputOption{}, "", ""
		default:
			return nil, fmt.Errorf("Unrecognized option '%s'", name)
		}
	}

	if !outputSeen {
		return nil, errors.New("At least one output file must be specified")
	}
	if len(o.Inputs) == 0 {
		return nil, errors.New("No input file; use -i before the output")
	}
	switch o.OutputFormat {
	case "whip":
		for _, in := range o.Inputs {
			if in.Format != "mp4" {
				return nil, fmt.Errorf("Output format whip takes one mp4 input, not %s '%s'", in.Format, in.Path)
			}
		}
		if len(o.Inputs) != 1 {
			return nil, fmt.Errorf("Output format whip takes one mp4 input, not %d inputs", len(o.Inputs))
		}
	case "null":
		for _, in := range o.Inputs {
			if in.Format != "whep" {
				return nil, fmt.Errorf("Output format null takes one whep input, not %s '%s'", in.Format, in.Path)
			}
		}
		if len(o.Inputs) != 1 {
			return nil, fmt.Errorf("Output format null takes one whep input, not %d inputs", len(o.Inputs))
		}
		if o.Output != "-" {
			return nil, fmt.Errorf("Output format null writes to -, not '%s'", o.Output)
		}
	}
	return o, nil
}

func contains(list []string, v string) bool {
	for _, s := range list {
		if s == v {
			return true
		}
	}
	return false
}
