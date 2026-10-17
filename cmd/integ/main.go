// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

// Command srs-integ is the CLI of the SRS black-box integration suite. It
// gives people and AI agents the suite's stream tools by hand: probe reads a
// stream and reports what it carries, publish sends a test file to a server.
package main

import (
	"fmt"
	"io"
	"os"
)

const usage = `Usage: srs-integ <command> [arguments]

Commands:
  probe [flags] <url>    Read a stream or an FLV file and print its stream report; -h for its flags.
  publish <file> <url>   Publish a media file in real time.
  prepare [flags]        Make the test media from doc/source.flv; -h for its flags.
  help                   Print this help.
`

func main() {
	os.Exit(run(os.Args[1:], os.Stdout, os.Stderr))
}

// run executes one command and returns the process exit code: 0 on success,
// 1 when the command fails, and 2 for a usage error.
func run(args []string, stdout, stderr io.Writer) int {
	if len(args) == 0 {
		fmt.Fprint(stderr, usage)
		return 2
	}

	switch cmd, rest := args[0], args[1:]; cmd {
	case "help", "-h", "-help", "--help":
		fmt.Fprint(stdout, usage)
		return 0
	case "probe":
		return probe(rest, stdout, stderr)
	case "prepare":
		return prepare(rest, stdout, stderr)
	case "publish":
		if len(rest) != 2 {
			fmt.Fprintln(stderr, "Usage: srs-integ publish <file> <url>")
			return 2
		}
		return fail(stderr, publish(rest[0], rest[1], stdout))
	default:
		fmt.Fprintf(stderr, "unknown command: %v\n\n%v", cmd, usage)
		return 2
	}
}

// fail prints err and returns the exit code for it.
func fail(stderr io.Writer, err error) int {
	if err == nil {
		return 0
	}
	fmt.Fprintln(stderr, err)
	return 1
}
