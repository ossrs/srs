// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package media

import (
	"os"
	"path/filepath"
	"testing"
)

// TestRemux remuxes a test file into MPEG-TS: every packet starts with the
// sync byte.
func TestRemux(t *testing.T) {
	path := Remux(t, H264HighAAC, "mpegts")
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if len(b) == 0 || len(b)%188 != 0 {
		t.Fatalf("%v bytes, want whole 188-byte packets", len(b))
	}
	for i := 0; i < len(b); i += 188 {
		if b[i] != 0x47 {
			t.Fatalf("packet at %v starts with %#x, want 0x47", i, b[i])
		}
	}
}

// TestRemuxDir remuxes a test file into HLS: the playlist and its segments
// are in the returned directory.
func TestRemuxDir(t *testing.T) {
	dir := RemuxDir(t, H264HighAAC, "hls", "index.m3u8", "-hls_time", "10", "-hls_playlist_type", "vod")
	for _, name := range []string{"index.m3u8", "index0.ts", "index2.ts"} {
		if _, err := os.Stat(filepath.Join(dir, name)); err != nil {
			t.Error(err)
		}
	}
}

// TestRemuxDirRelativeFiles keeps a file the muxer writes by a relative
// name, such as an HLS key, in the directory, not in the test's.
func TestRemuxDirRelativeFiles(t *testing.T) {
	dir := RemuxDir(t, H264HighAAC, "hls", "index.m3u8", "-t", "1", "-hls_enc", "1",
		"-hls_enc_key", "0123456789abcdef", "-hls_enc_key_url", "remux-test.key")
	if _, err := os.Stat(filepath.Join(dir, "remux-test.key")); err != nil {
		t.Error(err)
	}
	if _, err := os.Stat("remux-test.key"); err == nil {
		os.Remove("remux-test.key")
		t.Error("the key is in the test's directory")
	}
}
