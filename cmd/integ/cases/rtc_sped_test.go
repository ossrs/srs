// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"fmt"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/report"
	"srs-integ/internal/rtc"
	"srs-integ/internal/server"
)

// rtcSpedConfig is a server with WebRTC over UDP and TCP and sped on: SRS
// answers a=ice-options:sped to an offer that has it, and carries the DTLS
// handshake in the STUN binding requests and responses, as
// draft-hancke-webrtc-sped says.
func rtcSpedConfig() server.Config {
	return server.WithRTC(server.Config{
		Listeners: []server.Listener{server.HTTPAPI, server.RTCUDP, server.RTCTCP},
		Hooks:     true,
	}, server.RTC{SPED: true})
}

// TestRtc_Sped tests SPED with the raw client in its SPED mode, which
// embeds each DTLS datagram it writes in its next checks until the server
// ACKs it, and sends it raw too, as Chrome does. SRS embeds its own flights
// in the responses. Sessions that negotiated SPED show it in the clients
// API, which is how the cases see SRS's side.
func TestRtc_Sped(t *testing.T) {
	t.Parallel()
	srs := server.Group(t, rtcSpedConfig())

	// PublishPlayInStun publishes and plays with SPED over UDP: both answers
	// have sped, the client's ClientHello and SRS's first flight go inside
	// STUN, SPED completes on both sides, and the media plays.
	check.Case(t, "PublishPlayInStun", check.Normal, func(c *check.C) {
		stream := streamName(c)
		pub := rtcSpedPublisher(c, whipURL(srs, "live", stream), nil)
		player := rtcSpedPlayer(c, whepURL(srs, "live", stream), nil)
		c.Check(hasSpedAnswer(pub.Answer), "the WHIP answer has no a=ice-options:sped")
		c.Check(hasSpedAnswer(player.Answer), "the WHEP answer has no a=ice-options:sped")
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))

		for _, s := range []struct {
			what, hook, stream string
			session            *rtc.RawSession
		}{
			{"publisher", "on_publish", stream, &pub.RawSession},
			{"player", "on_play", stream, &player.RawSession},
		} {
			checkClientSpedComplete(c, s.what, s.session)
			id := waitHook(c, srs, s.hook, s.stream).Field("client_id")
			checkServerSpedComplete(c, s.what, waitServerSped(c, srs, id, "complete"))
		}
	})

	// SpedOffFallsBack publishes and plays with SPED offered to the Common
	// server, whose sped is off by default: the answers have no sped, so
	// the client does the handshake raw, SRS shows no SPED for the
	// sessions, and the media plays.
	check.Case(t, "SpedOffFallsBack", check.Normal, func(c *check.C) {
		common := server.Common(c)
		stream := streamName(c)
		pub := rtcSpedPublisher(c, whipURL(common, "live", stream), nil)
		player := rtcSpedPlayer(c, whepURL(common, "live", stream), nil)
		c.Check(strings.Contains(pub.Offer, "sped"), "the WHIP offer has no sped")
		c.Check(!hasSpedAnswer(pub.Answer), "the WHIP answer has a=ice-options:sped with sped off")
		c.Check(!hasSpedAnswer(player.Answer), "the WHEP answer has a=ice-options:sped with sped off")
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))

		for _, s := range []struct {
			what, hook string
			session    *rtc.RawSession
		}{
			{"publisher", "on_publish", &pub.RawSession},
			{"player", "on_play", &player.RawSession},
		} {
			c.CheckEqual(s.session.SPED(), rtc.SpedStats{}, s.what+" client SPED")
			cl := apiClientOf(c, common, waitHook(c, common, s.hook, stream).Field("client_id"))
			_, ok := cl["sped"]
			c.Check(!ok, "the %v in the API has sped %v with sped off", s.what, cl["sped"])
		}
	})

	// LostFirstResponse loses the first STUN response of each session, the
	// one that carries SRS's first flight, and every DTLS datagram SRS
	// sends raw: the client's next check carries its ClientHello again, so
	// SRS embeds the same flight again, and its last flight comes inside
	// STUN too. SPED completes on both sides and the media plays.
	check.Case(t, "LostFirstResponse", check.Normal, func(c *check.C) {
		lose := func() *rtc.Faults {
			f := &rtc.Faults{}
			f.Drop(rtc.FaultIn, rtc.FaultSTUN, 1)
			f.Drop(rtc.FaultIn, rtc.FaultDTLS, spedAllRaw)
			return f
		}
		stream := streamName(c)
		pf, vf := lose(), lose()
		pub := rtcSpedPublisher(c, whipURL(srs, "live", stream), &rtc.Options{Faults: pf})
		player := rtcSpedPlayer(c, whepURL(srs, "live", stream), &rtc.Options{Faults: vf})
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))

		for _, s := range []struct {
			what, hook string
			session    *rtc.RawSession
			f          *rtc.Faults
		}{
			{"publisher", "on_publish", &pub.RawSession, pf},
			{"player", "on_play", &player.RawSession, vf},
		} {
			c.CheckEqual(s.f.Dropped(rtc.FaultIn, rtc.FaultSTUN), 1, s.what+" responses lost")
			checkClientSpedComplete(c, s.what, s.session)
			st := s.session.SPED()
			c.Check(st.EmbeddedRecv >= 2, "the %v client got %v datagrams embedded, want SRS's first and last flights", s.what, st.EmbeddedRecv)
			c.CheckEqual(st.RawRecv, 0, s.what+" client raw DTLS received while SPED was active")
			id := waitHook(c, srs, s.hook, stream).Field("client_id")
			checkServerSpedComplete(c, s.what, waitServerSped(c, srs, id, "complete"))
		}
	})

	// LostClientFlight loses the first check that carries the client's
	// flight after SRS's ServerHelloDone, the one that starts with its
	// Certificate, and every DTLS datagram the client sends raw: the
	// client embeds the flight again in its next checks until SRS ACKs it.
	// SRS gets the whole handshake inside STUN, SPED completes on both
	// sides, and the media plays.
	check.Case(t, "LostClientFlight", check.Normal, func(c *check.C) {
		lose := func() *rtc.Faults {
			var lost atomic.Bool
			f := &rtc.Faults{Lose: func(dir rtc.FaultDirection, b []byte) bool {
				typ, ok := rtc.EmbeddedHandshake(b)
				return dir == rtc.FaultOut && ok && typ == rtc.HandshakeCertificate && lost.CompareAndSwap(false, true)
			}}
			f.Drop(rtc.FaultOut, rtc.FaultDTLS, spedAllRaw)
			return f
		}
		stream := streamName(c)
		pf, vf := lose(), lose()
		pub := rtcSpedPublisher(c, whipURL(srs, "live", stream), &rtc.Options{Faults: pf})
		player := rtcSpedPlayer(c, whepURL(srs, "live", stream), &rtc.Options{Faults: vf})
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))

		for _, s := range []struct {
			what, hook string
			session    *rtc.RawSession
			f          *rtc.Faults
		}{
			{"publisher", "on_publish", &pub.RawSession, pf},
			{"player", "on_play", &player.RawSession, vf},
		} {
			c.CheckEqual(s.f.Dropped(rtc.FaultOut, rtc.FaultSTUN), 1, s.what+" checks lost")
			checkClientSpedComplete(c, s.what, s.session)
			id := waitHook(c, srs, s.hook, stream).Field("client_id")
			sped := waitServerSped(c, srs, id, "complete")
			checkServerSpedComplete(c, s.what, sped)
			c.Check(sped["embedded_recv"].(float64) >= 2, "SRS got %v datagrams embedded from the %v, want its ClientHello and last flight", sped["embedded_recv"], s.what)
			c.CheckEqual(sped["raw_recv"], float64(0), "raw DTLS SRS got from the "+s.what+" while SPED was active")
		}
	})

	// SimulatorLosesFirstResponse has SRS's handshake simulator, armed by
	// the WHIP and WHEP requests, lose SRS's first STUN response and its
	// two raw flights, as LostFirstResponse loses them at the client: SRS
	// embeds the same flight again for the next check, and its last flight
	// inside STUN too. SPED completes on both sides and the media plays.
	check.Case(t, "SimulatorLosesFirstResponse", check.Normal, func(c *check.C) {
		const drops = "&drop-stun=1&drop-dtls=2"
		stream := streamName(c)
		pub := rtcSpedPublisher(c, whipURL(srs, "live", stream)+drops, nil)
		player := rtcSpedPlayer(c, whepURL(srs, "live", stream)+drops, nil)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))

		for _, s := range []struct {
			what, hook string
			session    *rtc.RawSession
		}{
			{"publisher", "on_publish", &pub.RawSession},
			{"player", "on_play", &player.RawSession},
		} {
			checkClientSpedComplete(c, s.what, s.session)
			st := s.session.SPED()
			c.Check(st.EmbeddedRecv >= 2, "the %v client got %v datagrams embedded, want SRS's first and last flights", s.what, st.EmbeddedRecv)
			c.CheckEqual(st.RawRecv, 0, s.what+" client raw DTLS received while SPED was active")
			id := waitHook(c, srs, s.hook, stream).Field("client_id")
			checkServerSpedComplete(c, s.what, waitServerSped(c, srs, id, "complete"))
		}
	})

	// SimulatorLosesLastFlight and TcpSimulatorLosesLastFlight publish and
	// play without SPED, over UDP and over TCP, with SRS's handshake
	// simulator losing SRS's DTLS 1.2 last flight, its ChangeCipherSpec and
	// Finished, once for each session. SRS is done when it sends that flight,
	// but the client is not: it resends its own last flight, and SRS answers
	// it with its last flight again. Both complete and the media plays. The
	// connects wait for pion's resend, a second after its first flight, so
	// their time is left out of the case's.
	for _, tcp := range []bool{false, true} {
		name := "SimulatorLosesLastFlight"
		if tcp {
			name = "TcpSimulatorLosesLastFlight"
		}
		check.Case(t, name, check.Normal, func(c *check.C) {
			stream := streamName(c)
			pw, vw := &lastFlights{}, &lastFlights{}
			var pub *rtc.RawPublisher
			var player *rtc.RawPlayer
			c.Untimed(func() {
				pub = rtcLastFlightPublisher(c, whipURL(srs, "live", stream)+"&drop-finished=1", tcp, pw)
				player = rtcLastFlightPlayer(c, whepURL(srs, "live", stream)+"&drop-finished=1", tcp, vw)
			})
			sendRTC(c, pub, player)
			checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))

			for _, s := range []struct {
				what string
				w    *lastFlights
			}{
				{"publisher", pw},
				{"player", vw},
			} {
				sent, got := s.w.sent.Load(), s.w.got.Load()
				c.Check(sent >= 2, "the %v sent its last flight %v times, want a resend after SRS's was lost", s.what, sent)
				c.Check(got >= 1, "the %v got SRS's last flight %v times, want its resend", s.what, got)
				c.Check(got < sent, "the %v got SRS's last flight %v times for its %v, want the first lost", s.what, got, sent)
			}
		})
	}

	// PlayerStartsOnSpedComplete and TcpPlayerStartsOnSpedComplete play a
	// stream that is already being published, with SPED over UDP and over
	// TCP, losing SRS's DTLS 1.2 last flight, its ChangeCipherSpec and
	// Finished, raw and in the first response that embeds it. SRS is done
	// when it sends that flight, but the player is not, and drops SRTP until
	// it is, so no media may come before the player has the flight. The next
	// response embeds it again, the player ACKs it, and then the media plays.
	for _, tcp := range []bool{false, true} {
		name := "PlayerStartsOnSpedComplete"
		if tcp {
			name = "Tcp" + name
		}
		check.Case(t, name, check.Normal, func(c *check.C) {
			stream := streamName(c)
			pub := rtcSpedPublisher(c, whipURL(srs, "live", stream), &rtc.Options{TCP: tcp})
			sendRTCSamples(c, rtcSamples(c), pub)
			w := &spedLastFlight{}
			player := rtcSpedPlayer(c, whepURL(srs, "live", stream), &rtc.Options{TCP: tcp, Faults: w.faults()})
			r := waitPlayed(c, player, report.Until{AudioFrames: 5})
			c.Check(r.Audio != nil, "the player got no audio: %v", r)

			c.Check(w.lostRaw.Load() >= 1, "SRS's raw last flight was not lost")
			c.CheckEqual(int(w.lostEmbedded.Load()), 1, "responses with SRS's last flight lost")
			c.Check(w.got.Load(), "the player never got SRS's last flight")
			c.CheckEqual(int(w.early.Load()), 0, "media before the player had SRS's last flight")
			checkClientSpedComplete(c, "player", &player.RawSession)
			id := waitHook(c, srs, "on_play", stream).Field("client_id")
			checkServerSpedComplete(c, "player", waitServerSped(c, srs, id, "complete"))
		})
	}

	// TcpPublishPlayInStun publishes and plays with SPED over TCP, each
	// STUN message and DTLS datagram in its own RFC 4571 frame: both
	// answers have sped, the client's ClientHello and SRS's first flight
	// go inside STUN, SPED completes on both sides, and the media plays.
	check.Case(t, "TcpPublishPlayInStun", check.Normal, func(c *check.C) {
		stream := streamName(c)
		pub := rtcSpedPublisher(c, whipURL(srs, "live", stream), &rtc.Options{TCP: true})
		player := rtcSpedPlayer(c, whepURL(srs, "live", stream), &rtc.Options{TCP: true})
		c.Check(hasSpedAnswer(pub.Answer), "the WHIP answer has no a=ice-options:sped")
		c.Check(hasSpedAnswer(player.Answer), "the WHEP answer has no a=ice-options:sped")
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))

		for _, s := range []struct {
			what, hook string
			session    *rtc.RawSession
		}{
			{"publisher", "on_publish", &pub.RawSession},
			{"player", "on_play", &player.RawSession},
		} {
			checkClientSpedComplete(c, s.what, s.session)
			id := waitHook(c, srs, s.hook, stream).Field("client_id")
			checkServerSpedComplete(c, s.what, waitServerSped(c, srs, id, "complete"))
		}
	})

	// TcpRawDtlsLost publishes and plays with SPED over TCP and loses every
	// DTLS datagram sent raw, both ways, so the whole handshake can only
	// come inside the STUN frames: SPED completes on both sides with no
	// raw DTLS received, and the media plays.
	check.Case(t, "TcpRawDtlsLost", check.Normal, func(c *check.C) {
		lose := func() *rtc.Faults {
			f := &rtc.Faults{}
			f.Drop(rtc.FaultIn, rtc.FaultDTLS, spedAllRaw)
			f.Drop(rtc.FaultOut, rtc.FaultDTLS, spedAllRaw)
			return f
		}
		stream := streamName(c)
		pf, vf := lose(), lose()
		pub := rtcSpedPublisher(c, whipURL(srs, "live", stream), &rtc.Options{TCP: true, Faults: pf})
		player := rtcSpedPlayer(c, whepURL(srs, "live", stream), &rtc.Options{TCP: true, Faults: vf})
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))

		for _, s := range []struct {
			what, hook string
			session    *rtc.RawSession
			f          *rtc.Faults
		}{
			{"publisher", "on_publish", &pub.RawSession, pf},
			{"player", "on_play", &player.RawSession, vf},
		} {
			c.Check(s.f.Dropped(rtc.FaultIn, rtc.FaultDTLS) >= 1, "no raw DTLS from SRS was lost for the %v", s.what)
			c.Check(s.f.Dropped(rtc.FaultOut, rtc.FaultDTLS) >= 1, "no raw DTLS to SRS was lost for the %v", s.what)
			checkClientSpedComplete(c, s.what, s.session)
			st := s.session.SPED()
			c.Check(st.EmbeddedRecv >= 2, "the %v client got %v datagrams embedded, want SRS's first and last flights", s.what, st.EmbeddedRecv)
			c.CheckEqual(st.RawRecv, 0, s.what+" client raw DTLS received while SPED was active")
			id := waitHook(c, srs, s.hook, stream).Field("client_id")
			sped := waitServerSped(c, srs, id, "complete")
			checkServerSpedComplete(c, s.what, sped)
			c.Check(sped["embedded_recv"].(float64) >= 2, "SRS got %v datagrams embedded from the %v, want its ClientHello and last flight", sped["embedded_recv"], s.what)
			c.CheckEqual(sped["raw_recv"], float64(0), "raw DTLS SRS got from the "+s.what+" while SPED was active")
		}
	})

	// RandomLoss connects 10 publishers and 10 players at once, each losing
	// a quarter of its STUN and DTLS datagrams each way, at random: every
	// session completes SPED on both sides, and every player plays. The
	// connects recover on the 50ms checks, so their time is left out of
	// the case's.
	check.Case(t, "RandomLoss", check.Normal, func(c *check.C) {
		const pairs, loss = 10, 0.25
		type session struct {
			what, hook, stream string
			s                  *rtc.RawSession
			f                  *rtc.Faults
		}
		pubs, players := make([]*rtc.RawPublisher, pairs), make([]*rtc.RawPlayer, pairs)
		var sessions []session
		c.Untimed(func() {
			base, streams := streamName(c), make([]string, pairs)
			for i := range pairs {
				streams[i] = fmt.Sprintf("%v-%d", base, i)
			}
			pfs, pubErrs := spedConnectAll(pairs, loss, func(ctx context.Context, i int, o *rtc.Options) error {
				var err error
				pubs[i], err = rtc.RawPublish(ctx, whipURL(srs, "live", streams[i]), rtcSpedOptions(o), true)
				return err
			})
			vfs, playErrs := spedConnectAll(pairs, loss, func(ctx context.Context, i int, o *rtc.Options) error {
				var err error
				players[i], err = rtc.RawPlay(ctx, whepURL(srs, "live", streams[i]), rtcSpedOptions(o), true)
				return err
			})
			for i := range pairs {
				if p := pubs[i]; p != nil {
					c.Cleanup(func() { p.Close() })
				}
				if p := players[i]; p != nil {
					c.Cleanup(func() { p.Close() })
				}
			}
			for i := range pairs {
				c.Must(pubErrs[i], fmt.Sprintf("raw WHIP publish %d with SPED at %v loss", i, loss))
				c.Must(playErrs[i], fmt.Sprintf("raw WHEP play %d with SPED at %v loss", i, loss))
				sessions = append(sessions,
					session{fmt.Sprintf("publisher %d", i), "on_publish", streams[i], &pubs[i].RawSession, pfs[i]},
					session{fmt.Sprintf("player %d", i), "on_play", streams[i], &players[i].RawSession, vfs[i]})
			}
		})
		for i := range pairs {
			sendRTC(c, pubs[i], players[i])
		}
		for i := range pairs {
			checkRTCPlayed(c, waitPlayed(c, players[i], rtcPlayedUntil))
		}

		lost := 0
		for _, s := range sessions {
			for _, dir := range []rtc.FaultDirection{rtc.FaultIn, rtc.FaultOut} {
				lost += s.f.Dropped(dir, rtc.FaultSTUN) + s.f.Dropped(dir, rtc.FaultDTLS)
			}
			until(check.Scaled(time.Second), func() bool { return s.s.SPED().State == rtc.SpedComplete })
			c.CheckEqual(s.s.SPED().State.String(), rtc.SpedComplete.String(), s.what+" client SPED state")
			id := waitHook(c, srs, s.hook, s.stream).Field("client_id")
			c.CheckEqual(waitServerSped(c, srs, id, "complete")["state"], "complete", s.what+" SPED state in the API")
		}
		c.Check(lost > 0, "no STUN or DTLS datagram was lost at %v loss", loss)
	})
}

// spedAllRaw is a count of DTLS datagrams to drop that is more than a
// session sends or gets, so every raw one is lost.
const spedAllRaw = 1000

// spedConnectAll connects n sessions at once, each with its own Faults that
// lose datagrams at a rate, and at most 3s to connect, and returns the
// Faults and each connect's error.
func spedConnectAll(n int, loss float64, connect func(ctx context.Context, i int, o *rtc.Options) error) ([]*rtc.Faults, []error) {
	faults, errs := make([]*rtc.Faults, n), make([]error, n)
	var wg sync.WaitGroup
	for i := range n {
		faults[i] = &rtc.Faults{Loss: loss}
		wg.Add(1)
		go func() {
			defer wg.Done()
			ctx, cancel := context.WithTimeout(context.Background(), check.Scaled(3*time.Second))
			defer cancel()
			errs[i] = connect(ctx, i, &rtc.Options{Faults: faults[i]})
		}()
	}
	wg.Wait()
	return faults, errs
}

// lastFlights counts the DTLS 1.2 last flights, each a datagram with a
// ChangeCipherSpec, that a session sent and got.
type lastFlights struct {
	sent, got atomic.Int32
}

// faults are Faults that lose nothing and count the last flights.
func (w *lastFlights) faults() *rtc.Faults {
	return &rtc.Faults{Lose: func(dir rtc.FaultDirection, b []byte) bool {
		if rtc.HasChangeCipherSpec(b) {
			if dir == rtc.FaultOut {
				w.sent.Add(1)
			} else {
				w.got.Add(1)
			}
		}
		return false
	}}
}

// spedLastFlight loses SRS's DTLS 1.2 last flight, the datagram with its
// ChangeCipherSpec, every time it comes raw and the first time a response
// embeds it, and counts the media that comes before the session has it.
type spedLastFlight struct {
	lostRaw, lostEmbedded, early atomic.Int32
	got                          atomic.Bool
}

func (w *spedLastFlight) faults() *rtc.Faults {
	return &rtc.Faults{
		Lose: func(dir rtc.FaultDirection, b []byte) bool {
			if dir != rtc.FaultIn {
				return false
			}
			if b[0] >= 20 && b[0] <= 63 && rtc.HasChangeCipherSpec(b) {
				w.lostRaw.Add(1)
				return true
			}
			d, ok := rtc.EmbeddedDTLS(b)
			return ok && rtc.HasChangeCipherSpec(d) && w.lostEmbedded.CompareAndSwap(0, 1)
		},
		Seen: func(dir rtc.FaultDirection, b []byte) {
			if dir != rtc.FaultIn {
				return
			}
			if d, ok := rtc.EmbeddedDTLS(b); ok && rtc.HasChangeCipherSpec(d) {
				w.got.Store(true)
			}
			// RTP and RTCP, by their first byte as RFC 7983 says.
			if b[0] >= 128 && b[0] <= 191 && !w.got.Load() {
				w.early.Add(1)
			}
		},
	}
}

// rtcLastFlightOptions are the options of a session without SPED, with
// SRTP, over UDP or TCP, that counts its last flights.
func rtcLastFlightOptions(tcp bool, w *lastFlights) *rtc.Options {
	return rtcOptions(&rtc.Options{TCP: tcp, SRTP: true, Faults: w.faults()})
}

// rtcLastFlightPublisher connects a raw WHIP publisher without SPED,
// closed when the case ends.
func rtcLastFlightPublisher(c *check.C, url string, tcp bool, w *lastFlights) *rtc.RawPublisher {
	c.Helper()
	// Long enough for pion to resend its last flight, a second after the first.
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	pub, err := rtc.RawPublish(ctx, url, rtcLastFlightOptions(tcp, w), true)
	c.Must(err, "raw WHIP publish")
	c.Cleanup(func() { pub.Close() })
	return pub
}

// rtcLastFlightPlayer connects a raw WHEP player without SPED, closed when
// the case ends.
func rtcLastFlightPlayer(c *check.C, url string, tcp bool, w *lastFlights) *rtc.RawPlayer {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	player, err := rtc.RawPlay(ctx, url, rtcLastFlightOptions(tcp, w), true)
	c.Must(err, "raw WHEP play")
	c.Cleanup(func() { player.Close() })
	return player
}

// rtcSpedOptions are the options with SPED, and with SRTP, as SRS encrypts.
func rtcSpedOptions(opts *rtc.Options) *rtc.Options {
	o := rtcOptions(opts)
	o.SPED, o.SRTP = true, true
	return o
}

// rtcSpedPublisher connects a raw WHIP publisher that offers SPED, closed
// when the case ends.
func rtcSpedPublisher(c *check.C, url string, opts *rtc.Options) *rtc.RawPublisher {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	pub, err := rtc.RawPublish(ctx, url, rtcSpedOptions(opts), true)
	c.Must(err, "raw WHIP publish with SPED")
	c.Cleanup(func() { pub.Close() })
	return pub
}

// rtcSpedPlayer connects a raw WHEP player that offers SPED, closed when
// the case ends.
func rtcSpedPlayer(c *check.C, url string, opts *rtc.Options) *rtc.RawPlayer {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	player, err := rtc.RawPlay(ctx, url, rtcSpedOptions(opts), true)
	c.Must(err, "raw WHEP play with SPED")
	c.Cleanup(func() { player.Close() })
	return player
}

// hasSpedAnswer tells whether an SDP has sped in an a=ice-options line.
func hasSpedAnswer(sdp string) bool {
	for _, l := range strings.Split(sdp, "\n") {
		if v, ok := strings.CutPrefix(strings.TrimSpace(l), "a=ice-options:"); ok {
			for _, t := range strings.Fields(v) {
				if t == "sped" {
					return true
				}
			}
		}
	}
	return false
}

// checkClientSpedComplete waits at most 1s until the client's SPED is
// complete, and checks the client embedded its datagrams and got SRS's
// first flight embedded: SPED carried the handshake, not only the raw
// copies.
func checkClientSpedComplete(c *check.C, what string, s *rtc.RawSession) {
	c.Helper()
	until(check.Scaled(time.Second), func() bool { return s.SPED().State == rtc.SpedComplete })
	st := s.SPED()
	c.CheckEqual(st.State.String(), rtc.SpedComplete.String(), what+" client SPED state")
	c.Check(st.EmbeddedSent >= 1, "the %v client embedded %v datagrams, want its ClientHello", what, st.EmbeddedSent)
	c.Check(st.EmbeddedRecv >= 1, "the %v client got %v datagrams embedded, want SRS's first flight", what, st.EmbeddedRecv)
}

// waitServerSped waits at most 1s until the sped of a client in the API is
// in a state, and returns it.
func waitServerSped(c *check.C, srs *server.Server, id, state string) map[string]any {
	c.Helper()
	var sped map[string]any
	until(check.Scaled(time.Second), func() bool {
		sped, _ = apiClientOf(c, srs, id)["sped"].(map[string]any)
		return sped != nil && sped["state"] == state
	})
	if sped == nil {
		c.Fatalf("client %v has no sped in the API", id)
	}
	return sped
}

// checkServerSpedComplete checks SRS's side of a session's SPED: complete,
// with the client's ClientHello received inside STUN, SRS's first flight
// sent inside STUN, and the time to its handshake measured.
func checkServerSpedComplete(c *check.C, what string, sped map[string]any) {
	c.Helper()
	c.CheckEqual(sped["state"], "complete", what+" SPED state in the API")
	n := func(k string) float64 { v, _ := sped[k].(float64); return v }
	c.Check(n("embedded_recv") >= 1, "SRS got %v datagrams embedded from the %v, want its ClientHello", sped["embedded_recv"], what)
	c.Check(n("embedded_sent") >= 1, "SRS embedded %v datagrams to the %v, want its first flight", sped["embedded_sent"], what)
	c.Check(n("handshake_ms") >= 0, "SRS's %v handshake_ms %v, want the time to its handshake", what, sped["handshake_ms"])
}
