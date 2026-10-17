// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"flag"
	"fmt"
	"io"
	"strings"
	"time"

	"srs-integ/internal/media"
)

const prepareUsage = `Usage: srs-integ prepare [-source file] [-dir dir] [-setup script]

Make the test media: transcode the source with each recipe into dir, and
regenerate only the files whose recipe, source or ffmpeg changed.

ffmpeg is $FFMPEG, then the one on PATH, then ~/.local/bin/ffmpeg. With
-setup on macOS, when none is found or it lacks libvpx-vp9 or libsvtav1, the
script builds one first, once no other test may be using it.

`

// prepare runs the prepare command and returns its exit code.
func prepare(args []string, stdout, stderr io.Writer) int {
	fs := flag.NewFlagSet("prepare", flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	source := fs.String("source", "doc/source.flv", "the source of the test media")
	dir := fs.String("dir", "cmd/integ/testdata/media", "where to write the test media")
	setup := fs.String("setup", "", "setup-ffmpeg-with-whip.sh, to build an ffmpeg with every encoder on macOS")
	usage := func() int {
		fmt.Fprint(stderr, prepareUsage)
		fs.SetOutput(stderr)
		fs.PrintDefaults()
		return 2
	}
	if err := fs.Parse(args); err != nil {
		if err == flag.ErrHelp {
			fmt.Fprint(stdout, prepareUsage)
			fs.SetOutput(stdout)
			fs.PrintDefaults()
			return 0
		}
		fmt.Fprintln(stderr, err)
		return usage()
	}
	if fs.NArg() != 0 {
		return usage()
	}

	start := time.Now()
	if _, err := media.HashSource(*source); err != nil {
		return fail(stderr, err)
	}
	ff, err := media.DefaultSetup(*setup, stdout).Ensure(media.DefaultFinder())
	if err != nil {
		return fail(stderr, err)
	}
	res, err := media.Prepare(media.Options{Source: *source, Dir: *dir, FFmpeg: ff, Log: stdout})
	if err != nil {
		return fail(stderr, err)
	}

	fmt.Fprintf(stdout, "test media in %v: %v made, %v kept, %v unavailable, in %v, by %v (%v)\n",
		*dir, len(res.Generated), len(res.Kept), len(res.Unavailable),
		time.Since(start).Round(time.Millisecond), ff.Path, ff.Version)
	if len(res.Unavailable) > 0 {
		m, err := media.ReadManifest(*dir)
		if err != nil {
			return fail(stderr, err)
		}
		var s []string
		for _, name := range res.Unavailable {
			s = append(s, fmt.Sprintf("%v (ffmpeg lacks %v)", name, m.Files[name].Unavailable))
		}
		fmt.Fprintf(stdout, "unavailable, their cases skip: %v\n", strings.Join(s, ", "))
	}
	return 0
}
