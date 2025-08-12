// Package mpeg4audio contains utilities to work with MPEG-4 audio codecs.
package mpeg4audio

const (
	// MaxAccessUnitSize is the maximum size of an access unit.
	MaxAccessUnitSize = 5 * 1024

	// SamplesPerAccessUnit is the number of samples contained inside an access unit.
	SamplesPerAccessUnit = 1024
)
