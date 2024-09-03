// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"fmt"
	"io/ioutil"
	"net"
	"net/http"
	"regexp"
	"strconv"
	"strings"
	"sync"

	"srs-proxy/errors"
	"srs-proxy/logger"
)

type rtcServer struct {
	// The UDP listener for WebRTC server.
	listener *net.UDPConn
	// The wait group for server.
	wg sync.WaitGroup
}

func newRTCServer(opts ...func(*rtcServer)) *rtcServer {
	v := &rtcServer{}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

func (v *rtcServer) Close() error {
	if v.listener != nil {
		_ = v.listener.Close()
	}

	v.wg.Wait()
	return nil
}

func (v *rtcServer) HandleWHIP(ctx context.Context, w http.ResponseWriter, r *http.Request) error {
	defer r.Body.Close()
	ctx = logger.WithContext(ctx)

	// Always allow CORS for all requests.
	if ok := apiCORS(ctx, w, r); ok {
		return nil
	}

	// Read remote SDP offer from body.
	remoteSDPOffer, err := ioutil.ReadAll(r.Body)
	if err != nil {
		return errors.Wrapf(err, "read remote sdp offer")
	}

	// Build the stream URL in vhost/app/stream schema.
	unifiedURL, fullURL := convertURLToStreamURL(r)
	logger.Df(ctx, "Got WebRTC WHIP from %v with %vB offer for %v", r.RemoteAddr, len(remoteSDPOffer), fullURL)

	streamURL, err := buildStreamURL(unifiedURL)
	if err != nil {
		return errors.Wrapf(err, "build stream url %v", unifiedURL)
	}

	// Pick a backend SRS server to proxy the RTMP stream.
	backend, err := srsLoadBalancer.Pick(ctx, streamURL)
	if err != nil {
		return errors.Wrapf(err, "pick backend for %v", streamURL)
	}

	if err = v.serveByBackend(ctx, w, r, backend, string(remoteSDPOffer), streamURL); err != nil {
		return errors.Wrapf(err, "serve %v with %v by backend %+v", fullURL, streamURL, backend)
	}

	return nil
}

func (v *rtcServer) HandleWHEP(ctx context.Context, w http.ResponseWriter, r *http.Request) error {
	defer r.Body.Close()
	ctx = logger.WithContext(ctx)

	// Always allow CORS for all requests.
	if ok := apiCORS(ctx, w, r); ok {
		return nil
	}

	// Read remote SDP offer from body.
	remoteSDPOffer, err := ioutil.ReadAll(r.Body)
	if err != nil {
		return errors.Wrapf(err, "read remote sdp offer")
	}

	// Build the stream URL in vhost/app/stream schema.
	unifiedURL, fullURL := convertURLToStreamURL(r)
	logger.Df(ctx, "Got WebRTC WHEP from %v with %vB offer for %v", r.RemoteAddr, len(remoteSDPOffer), fullURL)

	streamURL, err := buildStreamURL(unifiedURL)
	if err != nil {
		return errors.Wrapf(err, "build stream url %v", unifiedURL)
	}

	// Pick a backend SRS server to proxy the RTMP stream.
	backend, err := srsLoadBalancer.Pick(ctx, streamURL)
	if err != nil {
		return errors.Wrapf(err, "pick backend for %v", streamURL)
	}

	if err = v.serveByBackend(ctx, w, r, backend, string(remoteSDPOffer), streamURL); err != nil {
		return errors.Wrapf(err, "serve %v with %v by backend %+v", fullURL, streamURL, backend)
	}

	return nil
}

func (v *rtcServer) serveByBackend(ctx context.Context, w http.ResponseWriter, r *http.Request, backend *SRSServer, remoteSDPOffer string, streamURL string) error {
	// Parse HTTP port from backend.
	if len(backend.API) == 0 {
		return errors.Errorf("no http api server")
	}

	var apiPort int
	if iv, err := strconv.ParseInt(backend.API[0], 10, 64); err != nil {
		return errors.Wrapf(err, "parse http port %v", backend.API[0])
	} else {
		apiPort = int(iv)
	}

	// Connect to backend SRS server via HTTP client.
	backendURL := fmt.Sprintf("http://%v:%v%s", backend.IP, apiPort, r.URL.Path)
	if r.URL.RawQuery != "" {
		backendURL += "?" + r.URL.RawQuery
	}

	req, err := http.NewRequestWithContext(ctx, r.Method, backendURL, strings.NewReader(remoteSDPOffer))
	if err != nil {
		return errors.Wrapf(err, "create request to %v", backendURL)
	}

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return errors.Errorf("do request to %v EOF", backendURL)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK && resp.StatusCode != http.StatusCreated {
		return errors.Errorf("proxy api to %v failed, status=%v", backendURL, resp.Status)
	}

	// Copy all headers from backend to client.
	w.WriteHeader(resp.StatusCode)
	for k, v := range resp.Header {
		for _, vv := range v {
			w.Header().Add(k, vv)
		}
	}

	// Parse the local SDP answer from backend.
	b, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return errors.Wrapf(err, "read stream from %v", backendURL)
	}

	// Replace the WebRTC UDP port in answer.
	localSDPAnswer := string(b)
	for _, port := range backend.RTC {
		from := fmt.Sprintf(" %v typ host", port)
		to := fmt.Sprintf(" %v typ host", envWebRTCServer())
		localSDPAnswer = strings.Replace(localSDPAnswer, from, to, -1)
	}

	// Fetch the ice-ufrag and ice-pwd from local SDP answer.
	var iceUfrag, icePwd string
	if true {
		ufragRe := regexp.MustCompile(`a=ice-ufrag:([^\s]+)`)
		ufragMatch := ufragRe.FindStringSubmatch(localSDPAnswer)
		if len(ufragMatch) <= 1 {
			return errors.Errorf("no ice-ufrag in local sdp answer %v", localSDPAnswer)
		}
		iceUfrag = ufragMatch[1]
	}
	if true {
		pwdRe := regexp.MustCompile(`a=ice-pwd:([^\s]+)`)
		pwdMatch := pwdRe.FindStringSubmatch(localSDPAnswer)
		if len(pwdMatch) <= 1 {
			return errors.Errorf("no ice-pwd in local sdp answer %v", localSDPAnswer)
		}
		icePwd = pwdMatch[1]
	}

	// Response client with local answer.
	if _, err = w.Write([]byte(localSDPAnswer)); err != nil {
		return errors.Wrapf(err, "write local sdp answer %v", localSDPAnswer)
	}

	logger.Df(ctx, "Response local answer %vB with ice-ufrag=%v, ice-pwd=%vB",
		len(localSDPAnswer), iceUfrag, len(icePwd))
	return nil
}

func (v *rtcServer) Run(ctx context.Context) error {
	// Parse address to listen.
	endpoint := envWebRTCServer()
	if !strings.Contains(endpoint, ":") {
		endpoint = fmt.Sprintf(":%v", endpoint)
	}

	addr, err := net.ResolveUDPAddr("udp", endpoint)
	if err != nil {
		return errors.Wrapf(err, "resolve udp addr %v", endpoint)
	}

	listener, err := net.ListenUDP("udp", addr)
	if err != nil {
		return errors.Wrapf(err, "listen udp %v", addr)
	}
	v.listener = listener
	logger.Df(ctx, "WebRTC server listen at %v", addr)

	return nil
}
