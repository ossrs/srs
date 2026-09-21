// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"errors"
	"fmt"
	"strconv"
	"strings"
	"time"
)

// inputOption is the one -i input with the FFmpeg input options that preceded it.
type inputOption struct {
	// Format is whep, from -f before the -i or inferred from an http or https URL; the only input format.
	Format string
	// Path is the WHEP URL.
	Path string
}

// options is the parsed command line, the same FFmpeg-style subset as pion-whip reads, for the play direction:
// pion-whep -f whep -i <url> -t <seconds> -f null -
type options struct {
	HideBanner bool
	LogLevel   string
	// Rtx is whether the offer carries rtx, from the PION_WHEP_RTX environment variable, on by default; off forces
	// plain retransmission from the client side, whatever nack_prefer_rtx SRS runs with.
	Rtx    bool
	Inputs []inputOption
	// Duration is -t: how long to play before exiting, zero to play until interrupted.
	Duration     time.Duration
	OutputFormat string // null
	Output       string // -
}

var logLevels = []string{"quiet", "panic", "fatal", "error", "warning", "info", "verbose", "debug", "trace"}

// parseOptions parses args, the command line without the program name, with FFmpeg's placement rules: -f before
// the -i names the input format, -f after it names the output format, and the first bare argument is the single
// output. Playing takes exactly one WHEP input and the null output. The messages follow FFmpeg's wording, and
// pion-whip's, so the scripts read alike; an option outside the subset is rejected by name, never ignored.
func parseOptions(args []string) (*options, error) {
	o := &options{LogLevel: "info"}
	var pendingFormat string
	outputSeen := false

	for i := 0; i < len(args); i++ {
		arg := args[i]
		if !strings.HasPrefix(arg, "-") || arg == "-" {
			if outputSeen {
				return nil, fmt.Errorf("A second output '%s' is not supported", arg)
			}
			if pendingFormat == "" {
				return nil, fmt.Errorf("Unable to choose an output format for '%s'; use -f null", arg)
			}
			if pendingFormat != "null" {
				return nil, fmt.Errorf("Unknown output format '%s'; use null", pendingFormat)
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
			if logLevelIndex(v) < 0 {
				return nil, fmt.Errorf("Invalid loglevel '%s'; use %s or %s", v, strings.Join(logLevels[:len(logLevels)-1], ", "), logLevels[len(logLevels)-1])
			}
			o.LogLevel = v
		case "t":
			v, err := value()
			if err != nil {
				return nil, err
			}
			seconds, err := strconv.ParseFloat(v, 64)
			if err != nil || seconds < 0 {
				return nil, fmt.Errorf("Invalid value '%s' for option 't'", v)
			}
			o.Duration = time.Duration(seconds * float64(time.Second))
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
				lower := strings.ToLower(v)
				if strings.HasPrefix(lower, "http://") || strings.HasPrefix(lower, "https://") {
					format = "whep"
				} else {
					return nil, fmt.Errorf("Unable to choose an input format for '%s'; use -f whep", v)
				}
			}
			if format != "whep" {
				return nil, fmt.Errorf("Unknown input format '%s'; use whep", format)
			}
			o.Inputs = append(o.Inputs, inputOption{Format: format, Path: v})
			pendingFormat = ""
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
	if len(o.Inputs) != 1 {
		return nil, fmt.Errorf("Output format null takes one whep input, not %d inputs", len(o.Inputs))
	}
	if o.Output != "-" {
		return nil, fmt.Errorf("Output format null writes to -, not '%s'", o.Output)
	}
	return o, nil
}

// logLevelIndex returns the position of level in logLevels, quiet first, or -1 for an unknown name.
func logLevelIndex(level string) int {
	for i, s := range logLevels {
		if s == level {
			return i
		}
	}
	return -1
}

// envRtx is the environment variable that selects the retransmission format the tool offers, because FFmpeg has
// no option for it and the command line stays FFmpeg's: on, or unset, offers rtx as a browser does and lets SRS
// choose; off omits rtx from the offer so the session can only use plain retransmission.
const envRtx = "PION_WHEP_RTX"

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
