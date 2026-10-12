// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package media

import (
	"slices"
	"strings"
	"testing"
)

// fakeFFmpeg has the named encoders.
func fakeFFmpeg(version string, encoders ...string) *FFmpeg {
	ff := &FFmpeg{Path: "/fake/ffmpeg", Version: version, Encoders: map[string]bool{}}
	for _, e := range encoders {
		ff.Encoders[e] = true
	}
	return ff
}

// everyEncoder is an ffmpeg like the one the setup script builds.
var everyEncoder = []string{"libx264", "libx265", "libvpx-vp9", "libsvtav1", "aac", "libmp3lame", "libopus", "opus", "pcm_mulaw", "pcm_alaw"}

func TestRecipesHaveUniqueNames(t *testing.T) {
	seen := map[string]bool{}
	for _, r := range Recipes {
		if seen[r.Name] {
			t.Errorf("recipe %v twice", r.Name)
		}
		seen[r.Name] = true
		if got, ok := RecipeOf(r.Name); !ok || got.Name != r.Name {
			t.Errorf("RecipeOf(%v) = %v, %v", r.Name, got.Name, ok)
		}
	}
	if _, ok := RecipeOf("nothing.flv"); ok {
		t.Errorf("RecipeOf found an unknown file")
	}
}

func TestEveryVideoRecipeHasTheFixedGOP(t *testing.T) {
	for _, r := range Recipes {
		args, missing := r.Args(fakeFFmpeg("v", everyEncoder...), "src.flv", "out.tmp")
		if missing != "" {
			t.Fatalf("%v: missing %v with every encoder", r.Name, missing)
		}
		line := strings.Join(args, " ")
		if !strings.Contains(line, "-t 30 ") {
			t.Errorf("%v: no 30s cut in %q", r.Name, line)
		}
		if len(r.Video) == 0 {
			if !strings.Contains(line, "-map 0:a:0") || strings.Contains(line, "0:v") {
				t.Errorf("%v: audio only, got %q", r.Name, line)
			}
			continue
		}
		for _, want := range []string{"-r 25", "-g 25", "-keyint_min 25"} {
			if !strings.Contains(line, want) {
				t.Errorf("%v: no %q in %q", r.Name, want, line)
			}
		}
		if !strings.HasSuffix(line, "-f "+r.Format+" out.tmp") {
			t.Errorf("%v: output %q", r.Name, line)
		}
	}
}

func TestArgsPicksTheFirstEncoderTheFFmpegHas(t *testing.T) {
	r, _ := RecipeOf(H264BaselineOpus)

	args, missing := r.Args(fakeFFmpeg("v", "libx264", "libopus", "opus"), "s", "o")
	if missing != "" || !slices.Contains(args, "libopus") || slices.Contains(args, "-strict") {
		t.Errorf("with libopus: %q, missing %q", args, missing)
	}

	args, missing = r.Args(fakeFFmpeg("v", "libx264", "opus"), "s", "o")
	line := strings.Join(args, " ")
	if missing != "" || !strings.Contains(line, "-c:a opus -strict -2") {
		t.Errorf("native opus: %q, missing %q", line, missing)
	}

	args, missing = r.Args(fakeFFmpeg("v", "libx264"), "s", "o")
	if args != nil || missing != "libopus or opus" {
		t.Errorf("no opus: %q, missing %q", args, missing)
	}

	r, _ = RecipeOf(AV1Opus)
	if _, missing = r.Args(fakeFFmpeg("v", "libx264", "libopus"), "s", "o"); missing != "libsvtav1" {
		t.Errorf("no AV1: missing %q", missing)
	}
}

func TestSpecChangesWithTheRecipe(t *testing.T) {
	r, _ := RecipeOf(H264HighAAC)
	if r.Spec() != r.Spec() || r.Spec() == "" {
		t.Fatalf("spec %q is not stable", r.Spec())
	}
	changed := r
	changed.Video = []Encoder{{"libx264", []string{"-g", "50"}}}
	if changed.Spec() == r.Spec() {
		t.Errorf("spec did not change with the encoder args")
	}
	other, _ := RecipeOf(H264BaselineAAC)
	if other.Spec() == r.Spec() {
		t.Errorf("two recipes have one spec")
	}
}
