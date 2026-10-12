// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package media

import (
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

// extensions are the file extensions of the ffmpeg muxers whose name isn't
// one.
var extensions = map[string]string{"mpegts": ".ts", "adts": ".aac"}

// Remux copies the streams of a test media file into another container, in
// a temporary directory of tb, with ffmpeg -c copy: format is the ffmpeg
// muxer, such as "mpegts", "adts" or "mp3", and args go before the output,
// such as "-vn". It skips tb when no ffmpeg is found, and fails it when
// ffmpeg fails.
func Remux(tb testing.TB, name, format string, args ...string) string {
	tb.Helper()
	ext, ok := extensions[format]
	if !ok {
		ext = "." + format
	}
	file := strings.TrimSuffix(name, filepath.Ext(name)) + ext
	return filepath.Join(RemuxDir(tb, name, format, file, args...), file)
}

// RemuxDir is Remux for a muxer that writes several files, such as "hls" or
// "dash": it writes file, the playlist or manifest, and returns the
// temporary directory that holds it and the segments.
func RemuxDir(tb testing.TB, name, format, file string, args ...string) string {
	tb.Helper()
	src := Path(tb, name)
	ff, err := FindFFmpeg()
	if err != nil {
		tb.Skipf("remux needs ffmpeg: %v", err)
	}
	dir := tb.TempDir()
	cmd := append([]string{"-hide_banner", "-loglevel", "error", "-y", "-i", src, "-c", "copy"}, args...)
	cmd = append(cmd, "-f", format, filepath.Join(dir, file))
	// In the directory, so a file the muxer writes by a relative name, such as
	// the key of -hls_enc_key_url, goes there too.
	c := exec.Command(ff.Path, cmd...)
	c.Dir = dir
	if b, err := c.CombinedOutput(); err != nil {
		tb.Fatalf("ffmpeg %v: %v\n%s", strings.Join(cmd, " "), err, b)
	}
	return dir
}
