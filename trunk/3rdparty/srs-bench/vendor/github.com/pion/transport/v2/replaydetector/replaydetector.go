// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package replaydetector provides packet replay detection algorithm.
package replaydetector

// ReplayDetector is the interface of sequence replay detector.
type ReplayDetector interface {
	// Check returns true if given sequence number is not replayed.
	// Call accept() to mark the packet is received properly.
	Check(seq uint64) (accept func(), ok bool)
}

type slidingWindowDetector struct {
	latestSeq  uint64
	maxSeq     uint64
	windowSize uint
	mask       *fixedBigInt
}

// New creates ReplayDetector.
// Created ReplayDetector doesn't allow wrapping.
// It can handle monotonically increasing sequence number up to
// full 64bit number. It is suitable for DTLS replay protection.
func New(windowSize uint, maxSeq uint64) ReplayDetector {
	return &slidingWindowDetector{
		maxSeq:     maxSeq,
		windowSize: windowSize,
		mask:       newFixedBigInt(windowSize),
	}
}

func (d *slidingWindowDetector) Check(seq uint64) (accept func(), ok bool) {
	if seq > d.maxSeq {
		// Exceeded upper limit.
		return func() {}, false
	}

	if seq <= d.latestSeq {
		if d.latestSeq >= uint64(d.windowSize)+seq {
			return func() {}, false
		}
		if d.mask.Bit(uint(d.latestSeq-seq)) != 0 {
			// The sequence number is duplicated.
			return func() {}, false
		}
	}

	return func() {
		if seq > d.latestSeq {
			// Update the head of the window.
			d.mask.Lsh(uint(seq - d.latestSeq))
			d.latestSeq = seq
		}
		diff := (d.latestSeq - seq) % d.maxSeq
		d.mask.SetBit(uint(diff))
	}, true
}

// WithWrap creates ReplayDetector allowing sequence wrapping.
// This is suitable for short bit width counter like SRTP and SRTCP.
func WithWrap(windowSize uint, maxSeq uint64) ReplayDetector {
	return &wrappedSlidingWindowDetector{
		maxSeq:     maxSeq,
		windowSize: windowSize,
		mask:       newFixedBigInt(windowSize),
	}
}

type wrappedSlidingWindowDetector struct {
	latestSeq  uint64
	maxSeq     uint64
	windowSize uint
	mask       *fixedBigInt
	init       bool
}

func (d *wrappedSlidingWindowDetector) Check(seq uint64) (accept func(), ok bool) {
	if seq > d.maxSeq {
		// Exceeded upper limit.
		return func() {}, false
	}
	if !d.init {
		if seq != 0 {
			d.latestSeq = seq - 1
		} else {
			d.latestSeq = d.maxSeq
		}
		d.init = true
	}

	diff := int64(d.latestSeq) - int64(seq)
	// Wrap the number.
	if diff > int64(d.maxSeq)/2 {
		diff -= int64(d.maxSeq + 1)
	} else if diff <= -int64(d.maxSeq)/2 {
		diff += int64(d.maxSeq + 1)
	}

	if diff >= int64(d.windowSize) {
		// Too old.
		return func() {}, false
	}
	if diff >= 0 {
		if d.mask.Bit(uint(diff)) != 0 {
			// The sequence number is duplicated.
			return func() {}, false
		}
	}

	return func() {
		if diff < 0 {
			// Update the head of the window.
			d.mask.Lsh(uint(-diff))
			d.latestSeq = seq
		}
		d.mask.SetBit(uint(d.latestSeq - seq))
	}, true
}
