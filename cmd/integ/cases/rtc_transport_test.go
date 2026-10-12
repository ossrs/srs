// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"net"
	"net/http"
	"regexp"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/rtc"
	"srs-integ/internal/server"
)

// rtcTransport adds the transport cases to TestRtc_Common: ICE over UDP and
// over TCP (RFC 6544, each packet framed as RFC 4571 says), the candidates
// SRS gives, its ICE checks, and the DTLS roles. Common runs WebRTC over UDP
// and TCP (protocol all), with dtls_role passive and stun_strict_check off,
// both their defaults.
func rtcTransport(t *testing.T) {
	// AnswerCandidates checks the candidates of an answer: the host
	// candidate 127.0.0.1 on SRS's UDP port, and on its TCP port as a
	// passive TCP candidate, which the client dials.
	check.Case(t, "AnswerCandidates", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := rtcAnswered(c, whipURL(srs, "live", streamName(c)), rtcOffer(c, true, []string{"H264"}, []string{"opus"}))
		c.CheckEqual(fmt.Sprint(candidateList(s.Answer)), fmt.Sprint([]string{
			"udp " + srs.Addr(server.RTCUDP), "tcp " + srs.Addr(server.RTCTCP) + " passive",
		}), "candidates")
	})

	// TcpPublishPlay publishes and plays with ICE only over TCP: both
	// sessions select a TCP pair, and the player plays the stream.
	check.Case(t, "TcpPublishPlay", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), &rtc.Options{TCP: true})
		player := rtcPlayer(c, whepURL(srs, "live", stream), &rtc.Options{TCP: true})
		c.CheckEqual(pub.Transport(), "tcp", "publisher transport")
		c.CheckEqual(player.Transport(), "tcp", "player transport")
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})

	// TcpPublishUdpPlay publishes over TCP and plays over UDP.
	check.Case(t, "TcpPublishUdpPlay", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), &rtc.Options{TCP: true})
		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		c.CheckEqual(pub.Transport(), "tcp", "publisher transport")
		c.CheckEqual(player.Transport(), "udp", "player transport")
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})

	// UdpPublishTcpPlay publishes over UDP and plays over TCP.
	check.Case(t, "UdpPublishTcpPlay", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), nil)
		player := rtcPlayer(c, whepURL(srs, "live", stream), &rtc.Options{TCP: true})
		c.CheckEqual(pub.Transport(), "udp", "publisher transport")
		c.CheckEqual(player.Transport(), "tcp", "player transport")
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})

	// TcpSecondConnectionRejected opens a second TCP connection for a
	// publisher's session, which is connected over TCP: SRS closes it
	// without answering its binding request, and the session plays on.
	check.Case(t, "TcpSecondConnectionRejected", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		pub := rtcPublisher(c, whipURL(srs, "live", stream), &rtc.Options{TCP: true})
		_, err := rtcBind(c, pub.Offer, pub.Answer, &rtc.Options{TCP: true}, 300*time.Millisecond)
		c.Check(errors.Is(err, rtc.ErrClosed), "second TCP connection: %v, want closed", err)

		player := rtcPlayer(c, whepURL(srs, "live", stream), nil)
		sendRTC(c, pub, player)
		checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
	})

	// TcpBinding binds a session that nothing connected yet over TCP: SRS
	// answers with the address it sees, the client's. A connection closed
	// before DTLS frees the session for another one, which binds too.
	check.Case(t, "TcpBinding", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := rtcAnswered(c, whipURL(srs, "live", streamName(c)), rtcOffer(c, true, []string{"H264"}, []string{"opus"}))
		for i := 1; i <= 2; i++ {
			b, err := rtcBind(c, s.Offer, s.Answer, &rtc.Options{TCP: true}, 300*time.Millisecond)
			if c.CheckNoError(err, fmt.Sprintf("TCP binding %d", i)) {
				c.CheckEqual(b.Mapped, b.Local, fmt.Sprintf("binding %d: mapped address", i))
			}
		}
	})

	// TcpUnknownSession binds over TCP with a username of no session: SRS
	// closes the connection.
	check.Case(t, "TcpUnknownSession", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := rtcAnswered(c, whipURL(srs, "live", streamName(c)), rtcOffer(c, true, []string{"H264"}, []string{"opus"}))
		_, err := rtcBind(c, s.Offer, withICE(s.Answer, "nosuchuf", ""), &rtc.Options{TCP: true}, 300*time.Millisecond)
		c.Check(errors.Is(err, rtc.ErrClosed), "unknown username: %v, want closed", err)
	})

	// TcpFirstPacketNotStun sends an RTP packet as the first frame of a TCP
	// connection: SRS closes it, and binds the next one.
	check.Case(t, "TcpFirstPacketNotStun", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		rtp := []byte{0x80, 96, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0xaa}
		checkClosed(c, rtcTCPWrite(c, srs, rtp), "an RTP packet first")
		checkRTCBinds(c, srs)
	})

	// TcpOversizePacket sends a frame longer than SRS takes, 1500 bytes:
	// SRS closes the connection, and binds the next one.
	check.Case(t, "TcpOversizePacket", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		conn := rtcTCPWrite(c, srs, make([]byte, 2000))
		checkClosed(c, conn, "a frame of 2000 bytes")
		checkRTCBinds(c, srs)
	})

	// TcpDisconnectEndsSession closes the TCP connection of a publisher's
	// session after DTLS, without a close_notify alert: SRS ends the session
	// at once, unpublishes the stream, and it can be published again.
	check.Case(t, "TcpDisconnectEndsSession", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		defer cancel()
		pub, err := rtc.RawPublish(ctx, whipURL(srs, "live", stream)+"&encrypt=false", rtcOptions(&rtc.Options{TCP: true}), true)
		c.Must(err, "raw WHIP publish over TCP")
		c.Cleanup(func() { pub.Close() })
		h := waitHook(c, srs, "on_publish", stream)
		clientPath := "/api/v1/clients/" + h.Field("client_id")
		c.CheckEqual(apiCode(c, srs, clientPath), 0, "the publisher in the API")

		pub.Abort()
		waitHook(c, srs, "on_unpublish", stream)
		c.CheckNoError(untilGone(c, srs, clientPath), "the publisher after its connection closed")
		rtcPublisher(c, whipURL(srs, "live", stream), nil)
	})

	// UdpBinding binds a session over UDP: SRS answers with the address it
	// sees, the client's.
	check.Case(t, "UdpBinding", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := rtcAnswered(c, whipURL(srs, "live", streamName(c)), rtcOffer(c, true, []string{"H264"}, []string{"opus"}))
		b, err := rtcBind(c, s.Offer, s.Answer, nil, 300*time.Millisecond)
		if c.CheckNoError(err, "UDP binding") {
			c.CheckEqual(b.Mapped, b.Local, "mapped address")
		}
	})

	// UdpBindingWrongPassword signs a binding request with another password:
	// SRS doesn't answer it, but answers the one signed with the answer's.
	check.Case(t, "UdpBindingWrongPassword", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := rtcAnswered(c, whipURL(srs, "live", streamName(c)), rtcOffer(c, true, []string{"H264"}, []string{"opus"}))
		_, err := rtcBind(c, s.Offer, withICE(s.Answer, "", "wrongpassword0123456789"), nil, 150*time.Millisecond)
		c.Check(errors.Is(err, context.DeadlineExceeded), "wrong password: %v, want no answer", err)
		_, err = rtcBind(c, s.Offer, s.Answer, nil, 300*time.Millisecond)
		c.CheckNoError(err, "the answer's password")
	})

	// UdpUnknownSession binds over UDP with a username of no session: SRS
	// doesn't answer.
	check.Case(t, "UdpUnknownSession", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := rtcAnswered(c, whipURL(srs, "live", streamName(c)), rtcOffer(c, true, []string{"H264"}, []string{"opus"}))
		_, err := rtcBind(c, s.Offer, withICE(s.Answer, "nosuchuf", ""), nil, 150*time.Millisecond)
		c.Check(errors.Is(err, context.DeadlineExceeded), "unknown username: %v, want no answer", err)
	})

	// ControlledAccepted binds as an ICE-controlled agent: with
	// stun_strict_check off, SRS answers it.
	check.Case(t, "ControlledAccepted", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := rtcAnswered(c, whipURL(srs, "live", streamName(c)), rtcOffer(c, true, []string{"H264"}, []string{"opus"}))
		_, err := rtcBind(c, s.Offer, s.Answer, &rtc.Options{Controlled: true}, 300*time.Millisecond)
		c.CheckNoError(err, "a controlled agent's binding")
	})

	// DtlsAnswerPassive checks SRS takes its dtls_role, passive by default,
	// for an actpass offer.
	check.Case(t, "DtlsAnswerPassive", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := rtcAnswered(c, whipURL(srs, "live", streamName(c)), rtcOffer(c, true, []string{"H264"}, []string{"opus"}))
		c.CheckEqual(setups(s.Answer), "passive", "answer setup")
	})

	// DtlsOfferActive offers setup active: SRS answers passive, the DTLS
	// server, and the stream plays.
	check.Case(t, "DtlsOfferActive", check.Normal, func(c *check.C) {
		rtcSetupPlays(c, server.Common(c), "active", "passive")
	})

	// DtlsOfferPassive offers setup passive: SRS answers active, the DTLS
	// client, and the stream plays.
	check.Case(t, "DtlsOfferPassive", check.Normal, func(c *check.C) {
		rtcSetupPlays(c, server.Common(c), "passive", "active")
	})

	// ApiAsCandidate posts to the API by an IPv4 address that isn't a
	// loopback one: with api_as_candidates on, its default, that address is
	// a candidate too, on the UDP and the TCP port.
	check.Case(t, "ApiAsCandidate", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := rtcAnsweredAt(c, srs, "192.0.2.10", streamName(c))
		c.CheckEqual(fmt.Sprint(candidateList(s.Answer)), fmt.Sprint([]string{
			"udp " + srs.Addr(server.RTCUDP), "tcp " + srs.Addr(server.RTCTCP) + " passive",
			"udp 192.0.2.10:" + fmt.Sprint(srs.Ports[server.RTCUDP]), "tcp 192.0.2.10:" + fmt.Sprint(srs.Ports[server.RTCTCP]) + " passive",
		}), "candidates")
	})

	// ApiDomainKept posts to the API by a host name: with keep_api_domain
	// on and resolve_api_domain off, the name is a candidate as it is.
	check.Case(t, "ApiDomainKept", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := rtcAnsweredAt(c, srs, "media.example.com", streamName(c))
		list := candidateList(s.Answer)
		for _, want := range []string{
			"udp media.example.com:" + fmt.Sprint(srs.Ports[server.RTCUDP]),
			"tcp media.example.com:" + fmt.Sprint(srs.Ports[server.RTCTCP]) + " passive",
			"udp " + srs.Addr(server.RTCUDP),
		} {
			c.Check(contains(list, want), "candidates %v have no %q", list, want)
		}
	})

	// EipWithPort sets the candidate with a port, eip=<ip>:<port>: SRS puts
	// that port in both its UDP and TCP candidates for the address.
	check.Case(t, "EipWithPort", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		s := rtcAnswered(c, whipURL(srs, "live", streamName(c))+"&eip=10.20.30.40:12345", rtcOffer(c, true, []string{"H264"}, []string{"opus"}))
		list := candidateList(s.Answer)
		for _, want := range []string{"udp 10.20.30.40:12345", "tcp 10.20.30.40:12345 passive", "udp " + srs.Addr(server.RTCUDP)} {
			c.Check(contains(list, want), "candidates %v have no %q", list, want)
		}
	})

	// IceCredentials sets the server's ICE ufrag and pwd by the query: the
	// answer has them, and SRS checks bindings with that password.
	check.Case(t, "IceCredentials", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		stream := streamName(c)
		ufrag, pwd := "uf"+stream[len(stream)-6:], "pwd0123456789abcdefghij"
		s := rtcAnswered(c, whipURL(srs, "live", stream)+"&ice-ufrag="+ufrag+"&ice-pwd="+pwd,
			rtcOffer(c, true, []string{"H264"}, []string{"opus"}))
		c.Check(strings.Contains(s.Answer, "a=ice-ufrag:"+ufrag+"\r\n"), "answer has no ice-ufrag %v", ufrag)
		c.Check(strings.Contains(s.Answer, "a=ice-pwd:"+pwd+"\r\n"), "answer has no ice-pwd %v", pwd)
		_, err := rtcBind(c, s.Offer, s.Answer, nil, 300*time.Millisecond)
		c.CheckNoError(err, "binding with the query's credentials")
	})

	// IceCredentialsInvalid sets an ice-ufrag shorter than 4 characters, and
	// an ice-pwd shorter than 22: SRS refuses each offer.
	check.Case(t, "IceCredentialsInvalid", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		offer := rtcOffer(c, true, []string{"H264"}, []string{"opus"})
		for _, q := range []string{"&ice-ufrag=abc", "&ice-pwd=short0123456789"} {
			ctx, cancel := context.WithTimeout(context.Background(), time.Second)
			s, err := rtc.Post(ctx, whipURL(srs, "live", streamName(c))+q, offer, rtcOptions(nil))
			cancel()
			rtcDeleteLater(c, s, err)
			var se *rtc.StatusError
			if c.Check(errors.As(err, &se), "%v: %v, want a refusal", q, err) {
				c.Check(se.Code != http.StatusCreated && strings.Contains(se.Body, "RtcInvalidIce"), "%v: status %v %q", q, se.Code, se.Body)
			}
		}
	})
}

// TestRtc_Transport tests the WebRTC settings Common keeps at their
// defaults: dtls_role active, stun_strict_check on and api_as_candidates
// off, with WebRTC over UDP only (protocol udp). It shares the Fmp4Keys
// server, whose HLS and DASH settings don't touch WebRTC sessions.
func TestRtc_Transport(t *testing.T) {
	t.Parallel()
	srs := server.Shared(t, "Fmp4Keys", sharedFmp4Keys())

	// UdpOnly checks an answer of a server with WebRTC over UDP only: one
	// UDP candidate, no TCP one.
	check.Case(t, "UdpOnly", check.Normal, func(c *check.C) {
		s := rtcAnswered(c, whipURL(srs, "live", streamName(c)), rtcOffer(c, true, []string{"H264"}, []string{"opus"}))
		c.CheckEqual(fmt.Sprint(candidateList(s.Answer)), fmt.Sprint([]string{"udp " + srs.Addr(server.RTCUDP)}), "candidates")
	})

	// ApiAsCandidatesOff posts to the API by an IPv4 address: with
	// api_as_candidates off, it is not a candidate.
	check.Case(t, "ApiAsCandidatesOff", check.Normal, func(c *check.C) {
		s := rtcAnsweredAt(c, srs, "192.0.2.10", streamName(c))
		c.CheckEqual(fmt.Sprint(candidateList(s.Answer)), fmt.Sprint([]string{"udp " + srs.Addr(server.RTCUDP)}), "candidates")
	})

	// DtlsRoleActive offers actpass: SRS takes dtls_role active, the DTLS
	// client, and the stream plays. pion binds as the controlling agent,
	// which stun_strict_check accepts.
	check.Case(t, "DtlsRoleActive", check.Normal, func(c *check.C) {
		rtcSetupPlays(c, srs, "", "active")
	})

	// DtlsRoleActiveOfferActive offers setup active: SRS answers passive,
	// whatever its dtls_role, and the stream plays.
	check.Case(t, "DtlsRoleActiveOfferActive", check.Normal, func(c *check.C) {
		rtcSetupPlays(c, srs, "active", "passive")
	})

	// StrictCheckControlledRefused binds as an ICE-controlled agent: with
	// stun_strict_check on, SRS doesn't answer it, but answers the
	// controlling agent of the same session.
	check.Case(t, "StrictCheckControlledRefused", check.Normal, func(c *check.C) {
		s := rtcAnswered(c, whipURL(srs, "live", streamName(c)), rtcOffer(c, true, []string{"H264"}, []string{"opus"}))
		_, err := rtcBind(c, s.Offer, s.Answer, &rtc.Options{Controlled: true}, 150*time.Millisecond)
		c.Check(errors.Is(err, context.DeadlineExceeded), "controlled agent: %v, want no answer", err)
		_, err = rtcBind(c, s.Offer, s.Answer, nil, 300*time.Millisecond)
		c.CheckNoError(err, "controlling agent")
	})
}

// rtcSetupPlays publishes and plays with the DTLS setup in each offer, an
// empty one for actpass, and checks SRS answers with want and the stream
// plays.
func rtcSetupPlays(c *check.C, srs *server.Server, setup, want string) {
	c.Helper()
	stream := streamName(c)
	opts := &rtc.Options{Setup: setup}
	pub := rtcPublisher(c, whipURL(srs, "live", stream), opts)
	player := rtcPlayer(c, whepURL(srs, "live", stream), opts)
	c.CheckEqual(setups(pub.Answer), want, "publisher's answer setup")
	c.CheckEqual(setups(player.Answer), want, "player's answer setup")
	sendRTC(c, pub, player)
	checkRTCPlayed(c, waitPlayed(c, player, rtcPlayedUntil))
}

// rtcBind binds a session from a new socket, waiting at most d.
func rtcBind(c *check.C, offer, answer string, opts *rtc.Options, d time.Duration) (*rtc.Binding, error) {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), d)
	defer cancel()
	return rtc.Bind(ctx, offer, answer, opts)
}

// checkRTCBinds checks SRS binds a new session over TCP.
func checkRTCBinds(c *check.C, srs *server.Server) {
	c.Helper()
	s := rtcAnswered(c, whipURL(srs, "live", streamName(c)), rtcOffer(c, true, []string{"H264"}, []string{"opus"}))
	_, err := rtcBind(c, s.Offer, s.Answer, &rtc.Options{TCP: true}, 300*time.Millisecond)
	c.CheckNoError(err, "a new session's TCP binding")
}

// rtcTCPWrite connects to SRS's WebRTC TCP port and sends p as one frame,
// after its length in 2 bytes; the connection is closed when the case ends.
func rtcTCPWrite(c *check.C, srs *server.Server, p []byte) net.Conn {
	c.Helper()
	conn, err := net.DialTimeout("tcp", srs.Addr(server.RTCTCP), time.Second)
	c.Must(err, "dial the WebRTC TCP port")
	c.Cleanup(func() { conn.Close() })
	frame := binary.BigEndian.AppendUint16(nil, uint16(len(p)))
	_, err = conn.Write(append(frame, p...))
	c.Must(err, "write the frame")
	return conn
}

// rtcAnsweredAt posts a WHIP offer to the API by another host, such as
// 192.0.2.10 or a name, through a client that dials the API for any host,
// so SRS sees that host in the request.
func rtcAnsweredAt(c *check.C, srs *server.Server, host, stream string) *rtc.Session {
	c.Helper()
	url := fmt.Sprintf("http://%v:%v/rtc/v1/whip/?app=live&stream=%v", host, srs.Ports[server.HTTPAPI], stream)
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	s, err := rtc.Post(ctx, url, rtcOffer(c, true, []string{"H264"}, []string{"opus"}), &rtc.Options{Client: apiClient(c, srs)})
	rtcDeleteLater(c, s, err)
	c.Must(err, "post the offer to "+host)
	return s
}

// apiClient is an HTTP client that dials SRS's HTTP API for any host.
func apiClient(c *check.C, srs *server.Server) *http.Client {
	tr := &http.Transport{DialContext: func(ctx context.Context, network, _ string) (net.Conn, error) {
		var d net.Dialer
		return d.DialContext(ctx, network, srs.Addr(server.HTTPAPI))
	}}
	c.Cleanup(tr.CloseIdleConnections)
	return &http.Client{Transport: tr}
}

// candidateRe is an SDP candidate line: its transport, address, port and
// the rest of the line.
var candidateRe = regexp.MustCompile(`^a=candidate:\S+ \d+ (\S+) \d+ (\S+) (\d+) typ host(.*)$`)

// candidateList is each candidate of an answer, once each, in order, as
// "udp <address>:<port>" or "tcp <address>:<port> <tcptype>".
func candidateList(answer string) []string {
	var out []string
	seen := map[string]bool{}
	for _, l := range strings.Split(answer, "\n") {
		m := candidateRe.FindStringSubmatch(strings.TrimSpace(l))
		if m == nil {
			continue
		}
		v := strings.ToLower(m[1]) + " " + net.JoinHostPort(m[2], m[3])
		if _, tcptype, ok := strings.Cut(m[4], "tcptype "); ok {
			v += " " + strings.Fields(tcptype)[0]
		}
		if !seen[v] {
			seen[v] = true
			out = append(out, v)
		}
	}
	return out
}

// setups is the DTLS setup of an SDP's sections, or the different ones
// joined by commas.
func setups(sdp string) string {
	var out []string
	for _, l := range strings.Split(sdp, "\n") {
		if v, ok := strings.CutPrefix(strings.TrimSpace(l), "a=setup:"); ok && !contains(out, v) {
			out = append(out, v)
		}
	}
	return strings.Join(out, ",")
}

// withICE returns an answer with another ice-ufrag, ice-pwd or both; an
// empty one stays.
func withICE(answer, ufrag, pwd string) string {
	lines := strings.Split(answer, "\n")
	for i, l := range lines {
		if ufrag != "" && strings.HasPrefix(l, "a=ice-ufrag:") {
			lines[i] = "a=ice-ufrag:" + ufrag + "\r"
		}
		if pwd != "" && strings.HasPrefix(l, "a=ice-pwd:") {
			lines[i] = "a=ice-pwd:" + pwd + "\r"
		}
	}
	return strings.Join(lines, "\n")
}

func contains(list []string, v string) bool {
	for _, s := range list {
		if s == v {
			return true
		}
	}
	return false
}
