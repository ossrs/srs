// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package metrics

import (
	"reflect"
	"strings"
	"testing"
)

const sample = `# HELP srs_build_info A metric with a constant '1' value.
# TYPE srs_build_info gauge
srs_build_info{server="vid-1",version="8.0.60",label="a\"b\\c\nd"} 1
# HELP srs_streams The number of streams.
# TYPE srs_streams gauge
srs_streams 3
# HELP srs_send_bytes_total Sent bytes.
# TYPE srs_send_bytes_total counter
srs_send_bytes_total 1.5e+06
`

func TestParse(t *testing.T) {
	fams, err := Parse(strings.NewReader(sample))
	if err != nil {
		t.Fatal(err)
	}
	want := []Family{
		{Name: "srs_build_info", Help: "A metric with a constant '1' value.", Type: "gauge", Samples: []Sample{
			{Name: "srs_build_info", Labels: []Label{{"server", "vid-1"}, {"version", "8.0.60"}, {"label", "a\"b\\c\nd"}}, Value: 1},
		}},
		{Name: "srs_streams", Help: "The number of streams.", Type: "gauge", Samples: []Sample{{Name: "srs_streams", Value: 3}}},
		{Name: "srs_send_bytes_total", Help: "Sent bytes.", Type: "counter", Samples: []Sample{{Name: "srs_send_bytes_total", Value: 1.5e6}}},
	}
	if !reflect.DeepEqual(fams, want) {
		t.Fatalf("got %+v\nwant %+v", fams, want)
	}

	if f := Find(fams, "srs_streams"); f == nil || f.Samples[0].Value != 3 {
		t.Errorf("Find srs_streams: %+v", f)
	}
	if f := Find(fams, "none"); f != nil {
		t.Errorf("Find none: %+v", f)
	}
	if v, ok := fams[0].Samples[0].Label("version"); !ok || v != "8.0.60" {
		t.Errorf("Label version: %q %v", v, ok)
	}
	if _, ok := fams[0].Samples[0].Label("none"); ok {
		t.Errorf("Label none found")
	}
	if got := fams[0].Samples[0].LabelNames(); !reflect.DeepEqual(got, []string{"server", "version", "label"}) {
		t.Errorf("LabelNames: %v", got)
	}
}

func TestParseErrors(t *testing.T) {
	for _, tc := range []struct{ name, text string }{
		{"UnquotedLabel", "a{b=c} 1\n"},
		{"UnescapedQuote", "# TYPE a gauge\na{b=\"x\"y\"} 1\n"},
		{"BadEscape", "# TYPE a gauge\na{b=\"\\t\"} 1\n"},
		{"UnclosedLabels", "# TYPE a gauge\na{b=\"x\" 1\n"},
		{"DuplicateLabel", "# TYPE a gauge\na{b=\"x\",b=\"y\"} 1\n"},
		{"NoValue", "# TYPE a gauge\na\n"},
		{"BadValue", "# TYPE a gauge\na x\n"},
		{"BadName", "# TYPE a gauge\n1a 1\n"},
		{"BadType", "# TYPE a meter\na 1\n"},
		{"SampleOfOtherFamily", "# TYPE a gauge\nb 1\n"},
		{"TypeAfterSample", "# TYPE a gauge\na 1\n# TYPE a gauge\n"},
		{"HelpTwice", "# HELP a x\n# HELP a y\n"},
		{"TrailingText", "# TYPE a gauge\na 1 2 3\n"},
		{"NoNewlineAtEnd", "# TYPE a gauge\na 1"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			if fams, err := Parse(strings.NewReader(tc.text)); err == nil {
				t.Errorf("parsed %q as %+v, want an error", tc.text, fams)
			}
		})
	}
}

func TestParseUntyped(t *testing.T) {
	// A sample with no HELP or TYPE before it is a family of its own, untyped.
	fams, err := Parse(strings.NewReader("a 1\nb{x=\"y\"} 2 1700000000000\n"))
	if err != nil {
		t.Fatal(err)
	}
	if len(fams) != 2 || fams[0].Type != "untyped" || fams[1].Samples[0].Value != 2 {
		t.Fatalf("got %+v", fams)
	}
}
