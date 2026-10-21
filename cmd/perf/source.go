package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"strings"
)

// defaultMedia is the flat CBR source the perf image builds with perf-source.sh.
// SRS CPU follows the media bitrate, so every measurement uses a flat source.
const defaultMedia = "/srs/perf/cbr.flv"

// sourceFlatnessPath names the flatness record perf-source.sh writes beside a source.
func sourceFlatnessPath(media string) string {
	return strings.TrimSuffix(media, ".flv") + "-flatness.json"
}

// loadSource reads a source's tags and its flatness record, and refuses a source
// whose record is missing or not flat, since its runs would not be comparable.
func loadSource(media string) ([]rtmpMessage, json.RawMessage, error) {
	path := sourceFlatnessPath(media)
	record, err := os.ReadFile(path)
	if err != nil {
		return nil, nil, fmt.Errorf("source %s has no flatness record: %w", media, err)
	}
	var flatness struct {
		Flat   bool   `json:"flat"`
		Reason string `json:"reason"`
	}
	if err := json.Unmarshal(record, &flatness); err != nil {
		return nil, nil, fmt.Errorf("source %s has a bad flatness record %s: %w", media, path, err)
	}
	if !flatness.Flat {
		return nil, nil, fmt.Errorf("source %s is not flat: %s", media, flatness.Reason)
	}
	tags, err := loadFLV(media)
	if err != nil {
		return nil, nil, err
	}
	var compact bytes.Buffer
	if err := json.Compact(&compact, record); err != nil {
		return nil, nil, fmt.Errorf("source %s flatness record: %w", media, err)
	}
	return tags, json.RawMessage(compact.Bytes()), nil
}

// withSource records the source and its flatness in a result's setup.
func withSource(s setup, media string, flatness json.RawMessage) setup {
	s.Source = media
	s.SourceFlatness = flatness
	return s
}
