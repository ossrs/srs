package main

import (
	"bytes"
	"encoding/binary"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

// flvTag is one FLV tag with its previous tag size, as SRS writes it.
func flvTag(typeID byte, timestamp uint32, payload []byte) []byte {
	var b bytes.Buffer
	size := uint32(len(payload))
	b.Write([]byte{typeID, byte(size >> 16), byte(size >> 8), byte(size)})
	b.Write([]byte{byte(timestamp >> 16), byte(timestamp >> 8), byte(timestamp), byte(timestamp >> 24)})
	b.Write([]byte{0, 0, 0})
	b.Write(payload)
	var previous [4]byte
	binary.BigEndian.PutUint32(previous[:], size+11)
	b.Write(previous[:])
	return b.Bytes()
}

// The HTTP-FLV player GETs /<app>/<stream>.flv and reports the audio and
// video tags of the chunked body with their timestamps; script data is not
// media.
func TestFLVPlayerReportsMediaTags(t *testing.T) {
	gotPath := make(chan string, 1)
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		gotPath <- r.URL.Path
		w.Header().Set("Content-Type", "video/x-flv")
		w.Write([]byte{'F', 'L', 'V', 1, 5, 0, 0, 0, 9, 0, 0, 0, 0})
		w.Write(flvTag(18, 0, []byte("onMetaData")))
		w.(http.Flusher).Flush()
		w.Write(flvTag(9, 40, []byte{0x17, 1, 2}))
		w.(http.Flusher).Flush()
		w.Write(flvTag(8, 0x1000002, []byte{0xaf, 1}))
		w.(http.Flusher).Flush()
		<-r.Context().Done()
	}))
	defer server.Close()
	c, err := dialFLV(strings.TrimPrefix(server.URL, "http://"), "live")
	if err != nil {
		t.Fatal(err)
	}
	stop := make(chan struct{})
	report := make(chan rtmpMessage, 8)
	done := make(chan error, 1)
	go func() { done <- c.play("perf-run", stop, report) }()
	want := []rtmpMessage{{typeID: 9, timestamp: 40, payload: []byte{0x17, 1, 2}}, {typeID: 8, timestamp: 0x1000002, payload: []byte{0xaf, 1}}}
	for _, w := range want {
		select {
		case m := <-report:
			if m.typeID != w.typeID || m.timestamp != w.timestamp || !bytes.Equal(m.payload, w.payload) {
				t.Fatalf("tag = %+v, want %+v", m, w)
			}
		case <-time.After(3 * time.Second):
			t.Fatal("no tag reported")
		}
	}
	if path := <-gotPath; path != "/live/perf-run.flv" {
		t.Fatalf("path = %q", path)
	}
	close(stop)
	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("stopped player returned %v", err)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("player did not stop")
	}
}

// A stream SRS does not serve is an error, not a player that waits.
func TestFLVPlayerRejectsMissingStream(t *testing.T) {
	server := httptest.NewServer(http.NotFoundHandler())
	defer server.Close()
	c, err := dialFLV(strings.TrimPrefix(server.URL, "http://"), "live")
	if err != nil {
		t.Fatal(err)
	}
	err = c.play("missing", make(chan struct{}), make(chan rtmpMessage, 1))
	if err == nil || !strings.Contains(err.Error(), "404") {
		t.Fatalf("err = %v, want a 404 error", err)
	}
}

// A body that is not FLV is an error.
func TestFLVPlayerRejectsBadHeader(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte("not an flv stream"))
	}))
	defer server.Close()
	c, err := dialFLV(strings.TrimPrefix(server.URL, "http://"), "live")
	if err != nil {
		t.Fatal(err)
	}
	if err := c.play("perf-run", make(chan struct{}), make(chan rtmpMessage, 1)); err == nil {
		t.Fatal("bad FLV header was accepted")
	}
}

// Each scenario's player protocol has a dialer, except a publisher scenario,
// which has no player; an unknown one is refused.
func TestPlayerDialers(t *testing.T) {
	for name, s := range scenarios {
		if s.Unit == "publishers" {
			if s.Player != "" {
				t.Errorf("%s: publisher scenario has player %q", name, s.Player)
			}
			continue
		}
		if _, err := playerDialer(s.Player); err != nil {
			t.Errorf("%s: %v", name, err)
		}
	}
	if _, err := playerDialer("missing"); err == nil {
		t.Fatal("unknown player was accepted")
	}
}
