package gortsplib

import (
	"context"
	"fmt"
	"log"
	"net"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/google/uuid"
	"github.com/pion/rtcp"
	"github.com/pion/rtp"

	"github.com/bluenviron/gortsplib/v4/pkg/base"
	"github.com/bluenviron/gortsplib/v4/pkg/description"
	"github.com/bluenviron/gortsplib/v4/pkg/format"
	"github.com/bluenviron/gortsplib/v4/pkg/headers"
	"github.com/bluenviron/gortsplib/v4/pkg/liberrors"
	"github.com/bluenviron/gortsplib/v4/pkg/rtcpreceiver"
	"github.com/bluenviron/gortsplib/v4/pkg/rtcpsender"
	"github.com/bluenviron/gortsplib/v4/pkg/rtptime"
	"github.com/bluenviron/gortsplib/v4/pkg/sdp"
)

type readFunc func([]byte) bool

func stringsReverseIndex(s, substr string) int {
	for i := len(s) - 1 - len(substr); i >= 0; i-- {
		if s[i:i+len(substr)] == substr {
			return i
		}
	}
	return -1
}

// used for all methods except SETUP
func getPathAndQuery(u *base.URL, isAnnounce bool) (string, string) {
	if !isAnnounce {
		// FFmpeg format
		if strings.HasSuffix(u.RawQuery, "/") {
			return u.Path, u.RawQuery[:len(u.RawQuery)-1]
		}

		// GStreamer format
		if len(u.Path) > 1 && strings.HasSuffix(u.Path, "/") {
			return u.Path[:len(u.Path)-1], u.RawQuery
		}
	}

	return u.Path, u.RawQuery
}

// used for SETUP when playing
func getPathAndQueryAndTrackID(u *base.URL) (string, string, string, error) {
	// FFmpeg format
	i := stringsReverseIndex(u.RawQuery, "/trackID=")
	if i >= 0 {
		path := u.Path
		query := u.RawQuery[:i]
		trackID := u.RawQuery[i+len("/trackID="):]
		return path, query, trackID, nil
	}

	// GStreamer format
	i = stringsReverseIndex(u.Path, "/trackID=")
	if i >= 0 {
		path := u.Path[:i]
		query := u.RawQuery
		trackID := u.Path[i+len("/trackID="):]
		return path, query, trackID, nil
	}

	// no track ID and a trailing slash.
	// this happens when trying to read a MPEG-TS stream with FFmpeg.
	if strings.HasSuffix(u.RawQuery, "/") {
		return u.Path, u.RawQuery[:len(u.RawQuery)-1], "0", nil
	}
	if len(u.Path) >= 1 && strings.HasSuffix(u.Path[1:], "/") {
		return u.Path[:len(u.Path)-1], u.RawQuery, "0", nil
	}

	// special case for empty path
	if u.Path == "" || u.Path == "/" {
		return u.Path, u.RawQuery, "0", nil
	}

	// no slash at the end of the path.
	return "", "", "", liberrors.ErrServerInvalidSetupPath{}
}

// used for SETUP when recording
func findMediaByURL(
	medias []*description.Media,
	path string,
	query string,
	u *base.URL,
) *description.Media {
	for _, media := range medias {
		if strings.HasPrefix(media.Control, "rtsp://") ||
			strings.HasPrefix(media.Control, "rtsps://") {
			if media.Control == u.String() {
				return media
			}
		} else {
			// FFmpeg format
			u1 := &base.URL{
				Scheme:   u.Scheme,
				Host:     u.Host,
				Path:     path,
				RawQuery: query,
			}
			if query != "" {
				u1.RawQuery += "/" + media.Control
			} else {
				u1.Path += "/" + media.Control
			}
			if u1.String() == u.String() {
				return media
			}

			// GStreamer format
			u2 := &base.URL{
				Scheme:   u.Scheme,
				Host:     u.Host,
				Path:     path + "/" + media.Control,
				RawQuery: query,
			}
			if u2.String() == u.String() {
				return media
			}
		}
	}

	return nil
}

func findMediaByTrackID(medias []*description.Media, trackID string) *description.Media {
	if trackID == "" {
		return medias[0]
	}

	tmp, err := strconv.ParseUint(trackID, 10, 31)
	if err != nil {
		return nil
	}
	id := int(tmp)

	if len(medias) <= id {
		return nil
	}

	return medias[id]
}

func findFirstSupportedTransportHeader(s *Server, tsh headers.Transports) *headers.Transport {
	// Per RFC2326 section 12.39, client specifies transports in order of preference.
	// Filter out the ones we don't support and then pick first supported transport.
	for _, tr := range tsh {
		isMulticast := tr.Delivery != nil && *tr.Delivery == headers.TransportDeliveryMulticast
		if tr.Protocol == headers.TransportProtocolUDP &&
			((!isMulticast && s.udpRTPListener == nil) ||
				(isMulticast && s.MulticastIPRange == "")) {
			continue
		}
		return &tr
	}
	return nil
}

func generateRTPInfo(
	now time.Time,
	setuppedMediasOrdered []*serverSessionMedia,
	setuppedStream *ServerStream,
	setuppedPath string,
	u *base.URL,
) (headers.RTPInfo, bool) {
	var ri headers.RTPInfo

	for _, sm := range setuppedMediasOrdered {
		entry := setuppedStream.rtpInfoEntry(sm.media, now)
		if entry == nil {
			entry = &headers.RTPInfoEntry{}
		}
		entry.URL = (&base.URL{
			Scheme: u.Scheme,
			Host:   u.Host,
			Path: setuppedPath + "/trackID=" +
				strconv.FormatInt(int64(setuppedStream.medias[sm.media].trackID), 10),
		}).String()
		ri = append(ri, entry)
	}

	if len(ri) == 0 {
		return nil, false
	}

	return ri, true
}

// ServerSessionState is a state of a ServerSession.
type ServerSessionState int

// states.
const (
	ServerSessionStateInitial ServerSessionState = iota
	ServerSessionStatePrePlay
	ServerSessionStatePlay
	ServerSessionStatePreRecord
	ServerSessionStateRecord
)

// String implements fmt.Stringer.
func (s ServerSessionState) String() string {
	switch s {
	case ServerSessionStateInitial:
		return "initial"
	case ServerSessionStatePrePlay:
		return "prePlay"
	case ServerSessionStatePlay:
		return "play"
	case ServerSessionStatePreRecord:
		return "preRecord"
	case ServerSessionStateRecord:
		return "record"
	}
	return "unknown"
}

// ServerSession is a server-side RTSP session.
type ServerSession struct {
	s      *Server
	author *ServerConn

	secretID              string // must not be shared, allows to take ownership of the session
	ctx                   context.Context
	ctxCancel             func()
	userData              interface{}
	conns                 map[*ServerConn]struct{}
	state                 ServerSessionState
	setuppedMedias        map[*description.Media]*serverSessionMedia
	setuppedMediasOrdered []*serverSessionMedia
	tcpCallbackByChannel  map[int]readFunc
	setuppedTransport     *Transport
	setuppedStream        *ServerStream // read
	setuppedPath          string
	setuppedQuery         string
	lastRequestTime       time.Time
	tcpConn               *ServerConn
	announcedDesc         *description.Session // publish
	udpLastPacketTime     *int64               // publish
	udpCheckStreamTimer   *time.Timer
	writer                *asyncProcessor
	writerMutex           sync.RWMutex
	timeDecoder           *rtptime.GlobalDecoder2
	tcpFrame              *base.InterleavedFrame
	tcpBuffer             []byte

	// in
	chHandleRequest    chan sessionRequestReq
	chRemoveConn       chan *ServerConn
	chAsyncStartWriter chan struct{}
}

func (ss *ServerSession) initialize() {
	ctx, ctxCancel := context.WithCancel(ss.s.ctx)

	// use an UUID without dashes, since dashes confuse some clients.
	secretID := strings.ReplaceAll(uuid.New().String(), "-", "")

	ss.secretID = secretID
	ss.ctx = ctx
	ss.ctxCancel = ctxCancel
	ss.conns = make(map[*ServerConn]struct{})
	ss.lastRequestTime = ss.s.timeNow()
	ss.udpCheckStreamTimer = emptyTimer()

	ss.chHandleRequest = make(chan sessionRequestReq)
	ss.chRemoveConn = make(chan *ServerConn)
	ss.chAsyncStartWriter = make(chan struct{})

	ss.s.wg.Add(1)
	go ss.run()
}

// Close closes the ServerSession.
func (ss *ServerSession) Close() {
	ss.ctxCancel()
}

// BytesReceived returns the number of read bytes.
//
// Deprecated: replaced by Stats()
func (ss *ServerSession) BytesReceived() uint64 {
	v := uint64(0)
	for _, sm := range ss.setuppedMedias {
		v += atomic.LoadUint64(sm.bytesReceived)
	}
	return v
}

// BytesSent returns the number of written bytes.
//
// Deprecated: replaced by Stats()
func (ss *ServerSession) BytesSent() uint64 {
	v := uint64(0)
	for _, sm := range ss.setuppedMedias {
		v += atomic.LoadUint64(sm.bytesSent)
	}
	return v
}

// State returns the state of the session.
func (ss *ServerSession) State() ServerSessionState {
	return ss.state
}

// SetuppedTransport returns the transport negotiated during SETUP.
func (ss *ServerSession) SetuppedTransport() *Transport {
	return ss.setuppedTransport
}

// SetuppedStream returns the stream associated with the session.
func (ss *ServerSession) SetuppedStream() *ServerStream {
	return ss.setuppedStream
}

// SetuppedPath returns the path sent during SETUP or ANNOUNCE.
func (ss *ServerSession) SetuppedPath() string {
	return ss.setuppedPath
}

// SetuppedQuery returns the query sent during SETUP or ANNOUNCE.
func (ss *ServerSession) SetuppedQuery() string {
	return ss.setuppedQuery
}

// AnnouncedDescription returns the announced stream description.
func (ss *ServerSession) AnnouncedDescription() *description.Session {
	return ss.announcedDesc
}

// SetuppedMedias returns the setupped medias.
func (ss *ServerSession) SetuppedMedias() []*description.Media {
	ret := make([]*description.Media, len(ss.setuppedMedias))
	for i, sm := range ss.setuppedMediasOrdered {
		ret[i] = sm.media
	}
	return ret
}

// SetUserData sets some user data associated with the session.
func (ss *ServerSession) SetUserData(v interface{}) {
	ss.userData = v
}

// UserData returns some user data associated with the session.
func (ss *ServerSession) UserData() interface{} {
	return ss.userData
}

// Stats returns server session statistics.
func (ss *ServerSession) Stats() *StatsSession {
	return &StatsSession{
		BytesReceived: func() uint64 {
			v := uint64(0)
			for _, sm := range ss.setuppedMedias {
				v += atomic.LoadUint64(sm.bytesReceived)
			}
			return v
		}(),
		BytesSent: func() uint64 {
			v := uint64(0)
			for _, sm := range ss.setuppedMedias {
				v += atomic.LoadUint64(sm.bytesSent)
			}
			return v
		}(),
		RTPPacketsReceived: func() uint64 {
			v := uint64(0)
			for _, sm := range ss.setuppedMedias {
				for _, f := range sm.formats {
					v += atomic.LoadUint64(f.rtpPacketsReceived)
				}
			}
			return v
		}(),
		RTPPacketsSent: func() uint64 {
			v := uint64(0)
			for _, sm := range ss.setuppedMedias {
				for _, f := range sm.formats {
					v += atomic.LoadUint64(f.rtpPacketsSent)
				}
			}
			return v
		}(),
		RTPPacketsLost: func() uint64 {
			v := uint64(0)
			for _, sm := range ss.setuppedMedias {
				for _, f := range sm.formats {
					v += atomic.LoadUint64(f.rtpPacketsLost)
				}
			}
			return v
		}(),
		RTPPacketsInError: func() uint64 {
			v := uint64(0)
			for _, sm := range ss.setuppedMedias {
				v += atomic.LoadUint64(sm.rtpPacketsInError)
			}
			return v
		}(),
		RTPPacketsJitter: func() float64 {
			v := float64(0)
			n := float64(0)
			for _, sm := range ss.setuppedMedias {
				for _, fo := range sm.formats {
					if fo.rtcpReceiver != nil {
						stats := fo.rtcpReceiver.Stats()
						if stats != nil {
							v += stats.Jitter
							n++
						}
					}
				}
			}
			if n != 0 {
				return v / n
			}
			return 0
		}(),
		RTCPPacketsReceived: func() uint64 {
			v := uint64(0)
			for _, sm := range ss.setuppedMedias {
				v += atomic.LoadUint64(sm.rtcpPacketsReceived)
			}
			return v
		}(),
		RTCPPacketsSent: func() uint64 {
			v := uint64(0)
			for _, sm := range ss.setuppedMedias {
				v += atomic.LoadUint64(sm.rtcpPacketsSent)
			}
			return v
		}(),
		RTCPPacketsInError: func() uint64 {
			v := uint64(0)
			for _, sm := range ss.setuppedMedias {
				v += atomic.LoadUint64(sm.rtcpPacketsInError)
			}
			return v
		}(),
		Medias: func() map[*description.Media]StatsSessionMedia { //nolint:dupl
			ret := make(map[*description.Media]StatsSessionMedia, len(ss.setuppedMedias))

			for med, sm := range ss.setuppedMedias {
				ret[med] = StatsSessionMedia{
					BytesReceived:       atomic.LoadUint64(sm.bytesReceived),
					BytesSent:           atomic.LoadUint64(sm.bytesSent),
					RTPPacketsInError:   atomic.LoadUint64(sm.rtpPacketsInError),
					RTCPPacketsReceived: atomic.LoadUint64(sm.rtcpPacketsReceived),
					RTCPPacketsSent:     atomic.LoadUint64(sm.rtcpPacketsSent),
					RTCPPacketsInError:  atomic.LoadUint64(sm.rtcpPacketsInError),
					Formats: func() map[format.Format]StatsSessionFormat {
						ret := make(map[format.Format]StatsSessionFormat, len(sm.formats))

						for _, fo := range sm.formats {
							recvStats := func() *rtcpreceiver.Stats {
								if fo.rtcpReceiver != nil {
									return fo.rtcpReceiver.Stats()
								}
								return nil
							}()
							rtcpSender := func() *rtcpsender.RTCPSender {
								if ss.setuppedStream != nil {
									return ss.setuppedStream.medias[med].formats[fo.format.PayloadType()].rtcpSender
								}
								return nil
							}()
							sentStats := func() *rtcpsender.Stats {
								if rtcpSender != nil {
									return rtcpSender.Stats()
								}
								return nil
							}()

							ret[fo.format] = StatsSessionFormat{ //nolint:dupl
								RTPPacketsReceived: atomic.LoadUint64(fo.rtpPacketsReceived),
								RTPPacketsSent:     atomic.LoadUint64(fo.rtpPacketsSent),
								RTPPacketsLost:     atomic.LoadUint64(fo.rtpPacketsLost),
								LocalSSRC: func() uint32 {
									if fo.rtcpReceiver != nil {
										return *fo.rtcpReceiver.LocalSSRC
									}
									if sentStats != nil {
										return sentStats.LocalSSRC
									}
									return 0
								}(),
								RemoteSSRC: func() uint32 {
									if recvStats != nil {
										return recvStats.RemoteSSRC
									}
									return 0
								}(),
								RTPPacketsLastSequenceNumber: func() uint16 {
									if recvStats != nil {
										return recvStats.LastSequenceNumber
									}
									if sentStats != nil {
										return sentStats.LastSequenceNumber
									}
									return 0
								}(),
								RTPPacketsLastRTP: func() uint32 {
									if recvStats != nil {
										return recvStats.LastRTP
									}
									if sentStats != nil {
										return sentStats.LastRTP
									}
									return 0
								}(),
								RTPPacketsLastNTP: func() time.Time {
									if recvStats != nil {
										return recvStats.LastNTP
									}
									if sentStats != nil {
										return sentStats.LastNTP
									}
									return time.Time{}
								}(),
								RTPPacketsJitter: func() float64 {
									if recvStats != nil {
										return recvStats.Jitter
									}
									return 0
								}(),
							}
						}

						return ret
					}(),
				}
			}

			return ret
		}(),
	}
}

func (ss *ServerSession) onStreamWriteError(err error) {
	if h, ok := ss.s.Handler.(ServerHandlerOnStreamWriteError); ok {
		h.OnStreamWriteError(&ServerHandlerOnStreamWriteErrorCtx{
			Session: ss,
			Error:   err,
		})
	} else {
		log.Println(err.Error())
	}
}

func (ss *ServerSession) checkState(allowed map[ServerSessionState]struct{}) error {
	if _, ok := allowed[ss.state]; ok {
		return nil
	}

	allowedList := make([]fmt.Stringer, len(allowed))
	i := 0
	for a := range allowed {
		allowedList[i] = a
		i++
	}
	return liberrors.ErrServerInvalidState{AllowedList: allowedList, State: ss.state}
}

func (ss *ServerSession) createWriter() {
	ss.writerMutex.Lock()

	ss.writer = &asyncProcessor{
		bufferSize: func() int {
			if ss.state == ServerSessionStatePrePlay {
				return ss.s.WriteQueueSize
			}

			// when recording, writeBuffer is only used to send RTCP receiver reports,
			// that are much smaller than RTP packets and are sent at a fixed interval.
			// decrease RAM consumption by allocating less buffers.
			return 8
		}(),
	}

	ss.writer.initialize()

	ss.writerMutex.Unlock()
}

func (ss *ServerSession) startWriter() {
	ss.writer.start()
}

func (ss *ServerSession) destroyWriter() {
	ss.writer.close()

	ss.writerMutex.Lock()
	ss.writer = nil
	ss.writerMutex.Unlock()
}

func (ss *ServerSession) run() {
	defer ss.s.wg.Done()

	if h, ok := ss.s.Handler.(ServerHandlerOnSessionOpen); ok {
		h.OnSessionOpen(&ServerHandlerOnSessionOpenCtx{
			Session: ss,
			Conn:    ss.author,
		})
	}

	err := ss.runInner()

	ss.ctxCancel()

	// close all associated connections, both UDP and TCP
	// except for the ones that called TEARDOWN
	// (that are detached from the session just after the request)
	for sc := range ss.conns {
		sc.Close()

		// make sure that OnFrame() is never called after OnSessionClose()
		<-sc.done

		sc.removeSession(ss)
	}

	if ss.setuppedStream != nil {
		ss.setuppedStream.readerSetInactive(ss)
		ss.setuppedStream.readerRemove(ss)
	}

	for _, sm := range ss.setuppedMedias {
		sm.stop()
	}

	if ss.writer != nil {
		ss.destroyWriter()
	}

	ss.s.closeSession(ss)

	if h, ok := ss.s.Handler.(ServerHandlerOnSessionClose); ok {
		h.OnSessionClose(&ServerHandlerOnSessionCloseCtx{
			Session: ss,
			Error:   err,
		})
	}
}

func (ss *ServerSession) runInner() error {
	for {
		chWriterError := func() chan struct{} {
			if ss.writer != nil {
				return ss.writer.chStopped
			}
			return nil
		}()

		select {
		case req := <-ss.chHandleRequest:
			ss.lastRequestTime = ss.s.timeNow()

			if _, ok := ss.conns[req.sc]; !ok {
				ss.conns[req.sc] = struct{}{}
			}

			res, err := ss.handleRequestInner(req.sc, req.req)

			returnedSession := ss

			if err == nil || isSwitchReadFuncError(err) {
				// ANNOUNCE responses don't contain the session header.
				if req.req.Method != base.Announce &&
					req.req.Method != base.Teardown {
					if res.Header == nil {
						res.Header = make(base.Header)
					}

					res.Header["Session"] = headers.Session{
						Session: ss.secretID,
						Timeout: func() *uint {
							// timeout controls the sending of RTCP keepalives.
							// these are needed only when the client is playing
							// and transport is UDP or UDP-multicast.
							if (ss.state == ServerSessionStatePrePlay ||
								ss.state == ServerSessionStatePlay) &&
								(*ss.setuppedTransport == TransportUDP ||
									*ss.setuppedTransport == TransportUDPMulticast) {
								v := uint(ss.s.sessionTimeout / time.Second)
								return &v
							}
							return nil
						}(),
					}.Marshal()
				}

				// after a TEARDOWN, session must be unpaired with the connection
				if req.req.Method == base.Teardown {
					delete(ss.conns, req.sc)
					returnedSession = nil
				}
			}

			savedMethod := req.req.Method

			req.res <- sessionRequestRes{
				res: res,
				err: err,
				ss:  returnedSession,
			}

			if (err == nil || isSwitchReadFuncError(err)) && savedMethod == base.Teardown {
				return liberrors.ErrServerSessionTornDown{Author: req.sc.NetConn().RemoteAddr()}
			}

		case sc := <-ss.chRemoveConn:
			delete(ss.conns, sc)

			// if session is not in state RECORD or PLAY, or transport is TCP,
			// and there are no associated connections,
			// close the session.
			if ((ss.state != ServerSessionStateRecord &&
				ss.state != ServerSessionStatePlay) ||
				*ss.setuppedTransport == TransportTCP) &&
				len(ss.conns) == 0 {
				return liberrors.ErrServerSessionNotInUse{}
			}

		case <-ss.chAsyncStartWriter:
			if (ss.state == ServerSessionStateRecord ||
				ss.state == ServerSessionStatePlay) &&
				*ss.setuppedTransport == TransportTCP {
				ss.startWriter()
			}

		case <-ss.udpCheckStreamTimer.C:
			now := ss.s.timeNow()

			lft := atomic.LoadInt64(ss.udpLastPacketTime)

			// in case of RECORD, timeout happens when no RTP or RTCP packets are being received
			if ss.state == ServerSessionStateRecord {
				if now.Sub(time.Unix(lft, 0)) >= ss.s.ReadTimeout {
					return liberrors.ErrServerSessionTimedOut{}
				}

				// in case of PLAY, timeout happens when no RTSP keepalives and no RTCP packets are being received
			} else if now.Sub(ss.lastRequestTime) >= ss.s.sessionTimeout &&
				now.Sub(time.Unix(lft, 0)) >= ss.s.sessionTimeout {
				return liberrors.ErrServerSessionTimedOut{}
			}

			ss.udpCheckStreamTimer = time.NewTimer(ss.s.checkStreamPeriod)

		case <-chWriterError:
			return ss.writer.stopError

		case <-ss.ctx.Done():
			return liberrors.ErrServerTerminated{}
		}
	}
}

func (ss *ServerSession) handleRequestInner(sc *ServerConn, req *base.Request) (*base.Response, error) {
	if ss.tcpConn != nil && sc != ss.tcpConn {
		return &base.Response{
			StatusCode: base.StatusBadRequest,
		}, liberrors.ErrServerSessionLinkedToOtherConn{}
	}

	var path string
	var query string

	switch req.Method {
	case base.Announce:
		path, query = getPathAndQuery(req.URL, true)
	case base.Pause, base.GetParameter, base.SetParameter, base.Play, base.Record:
		path, query = getPathAndQuery(req.URL, false)
	}

	switch req.Method {
	case base.Options:
		var methods []string
		if _, ok := sc.s.Handler.(ServerHandlerOnDescribe); ok {
			methods = append(methods, string(base.Describe))
		}
		if _, ok := sc.s.Handler.(ServerHandlerOnAnnounce); ok {
			methods = append(methods, string(base.Announce))
		}
		if _, ok := sc.s.Handler.(ServerHandlerOnSetup); ok {
			methods = append(methods, string(base.Setup))
		}
		if _, ok := sc.s.Handler.(ServerHandlerOnPlay); ok {
			methods = append(methods, string(base.Play))
		}
		if _, ok := sc.s.Handler.(ServerHandlerOnRecord); ok {
			methods = append(methods, string(base.Record))
		}
		if _, ok := sc.s.Handler.(ServerHandlerOnPause); ok {
			methods = append(methods, string(base.Pause))
		}
		methods = append(methods, string(base.GetParameter))
		if _, ok := sc.s.Handler.(ServerHandlerOnSetParameter); ok {
			methods = append(methods, string(base.SetParameter))
		}
		methods = append(methods, string(base.Teardown))

		return &base.Response{
			StatusCode: base.StatusOK,
			Header: base.Header{
				"Public": base.HeaderValue{strings.Join(methods, ", ")},
			},
		}, nil

	case base.Announce:
		err := ss.checkState(map[ServerSessionState]struct{}{
			ServerSessionStateInitial: {},
		})
		if err != nil {
			return &base.Response{
				StatusCode: base.StatusBadRequest,
			}, err
		}

		ct, ok := req.Header["Content-Type"]
		if !ok || len(ct) != 1 {
			return &base.Response{
				StatusCode: base.StatusBadRequest,
			}, liberrors.ErrServerContentTypeMissing{}
		}

		if ct[0] != "application/sdp" {
			return &base.Response{
				StatusCode: base.StatusBadRequest,
			}, liberrors.ErrServerContentTypeUnsupported{CT: ct}
		}

		var ssd sdp.SessionDescription
		err = ssd.Unmarshal(req.Body)
		if err != nil {
			return &base.Response{
				StatusCode: base.StatusBadRequest,
			}, liberrors.ErrServerSDPInvalid{Err: err}
		}

		var desc description.Session
		err = desc.Unmarshal(&ssd)
		if err != nil {
			return &base.Response{
				StatusCode: base.StatusBadRequest,
			}, liberrors.ErrServerSDPInvalid{Err: err}
		}

		res, err := ss.s.Handler.(ServerHandlerOnAnnounce).OnAnnounce(&ServerHandlerOnAnnounceCtx{
			Session:     ss,
			Conn:        sc,
			Request:     req,
			Path:        path,
			Query:       query,
			Description: &desc,
		})

		if res.StatusCode == base.StatusOK {
			ss.state = ServerSessionStatePreRecord
			ss.setuppedPath = path
			ss.setuppedQuery = query
			ss.announcedDesc = &desc
		}

		return res, err

	case base.Setup:
		err := ss.checkState(map[ServerSessionState]struct{}{
			ServerSessionStateInitial:   {},
			ServerSessionStatePrePlay:   {},
			ServerSessionStatePreRecord: {},
		})
		if err != nil {
			return &base.Response{
				StatusCode: base.StatusBadRequest,
			}, err
		}

		var transportHeaders headers.Transports
		err = transportHeaders.Unmarshal(req.Header["Transport"])
		if err != nil {
			return &base.Response{
				StatusCode: base.StatusBadRequest,
			}, liberrors.ErrServerTransportHeaderInvalid{Err: err}
		}

		inTH := findFirstSupportedTransportHeader(ss.s, transportHeaders)
		if inTH == nil {
			return &base.Response{
				StatusCode: base.StatusUnsupportedTransport,
			}, nil
		}

		var trackID string

		switch ss.state {
		case ServerSessionStateInitial, ServerSessionStatePrePlay: // play
			path, query, trackID, err = getPathAndQueryAndTrackID(req.URL)
			if err != nil {
				return &base.Response{
					StatusCode: base.StatusBadRequest,
				}, err
			}

			if ss.state == ServerSessionStatePrePlay && path != ss.setuppedPath {
				return &base.Response{
					StatusCode: base.StatusBadRequest,
				}, liberrors.ErrServerMediasDifferentPaths{}
			}

		default: // record
			path = ss.setuppedPath
			query = ss.setuppedQuery
		}

		var transport Transport

		if inTH.Protocol == headers.TransportProtocolUDP {
			if inTH.Delivery != nil && *inTH.Delivery == headers.TransportDeliveryMulticast {
				transport = TransportUDPMulticast
			} else {
				transport = TransportUDP
			}
		} else {
			transport = TransportTCP
		}

		if ss.setuppedTransport != nil && *ss.setuppedTransport != transport {
			return &base.Response{
				StatusCode: base.StatusBadRequest,
			}, liberrors.ErrServerMediasDifferentProtocols{}
		}

		switch transport {
		case TransportUDP:
			if inTH.ClientPorts == nil {
				return &base.Response{
					StatusCode: base.StatusBadRequest,
				}, liberrors.ErrServerTransportHeaderNoClientPorts{}
			}

		case TransportTCP:
			if inTH.InterleavedIDs != nil {
				if (inTH.InterleavedIDs[0] + 1) != inTH.InterleavedIDs[1] {
					return &base.Response{
						StatusCode: base.StatusBadRequest,
					}, liberrors.ErrServerTransportHeaderInvalidInterleavedIDs{}
				}

				if ss.isChannelPairInUse(inTH.InterleavedIDs[0]) {
					return &base.Response{
						StatusCode: base.StatusBadRequest,
					}, liberrors.ErrServerTransportHeaderInterleavedIDsInUse{}
				}
			}
		}

		switch ss.state {
		case ServerSessionStateInitial, ServerSessionStatePrePlay: // play
			if inTH.Mode != nil && *inTH.Mode != headers.TransportModePlay {
				return &base.Response{
					StatusCode: base.StatusBadRequest,
				}, liberrors.ErrServerTransportHeaderInvalidMode{Mode: inTH.Mode}
			}

		default: // record
			if transport == TransportUDPMulticast {
				return &base.Response{
					StatusCode: base.StatusUnsupportedTransport,
				}, nil
			}

			if inTH.Mode == nil || *inTH.Mode != headers.TransportModeRecord {
				return &base.Response{
					StatusCode: base.StatusBadRequest,
				}, liberrors.ErrServerTransportHeaderInvalidMode{Mode: inTH.Mode}
			}
		}

		res, stream, err := ss.s.Handler.(ServerHandlerOnSetup).OnSetup(&ServerHandlerOnSetupCtx{
			Session:   ss,
			Conn:      sc,
			Request:   req,
			Path:      path,
			Query:     query,
			Transport: transport,
		})

		// workaround to prevent a bug in rtspclientsink
		// that makes impossible for the client to receive the response
		// and send frames.
		// this was causing problems during unit tests.
		if ua, ok := req.Header["User-Agent"]; ok && len(ua) == 1 &&
			strings.HasPrefix(ua[0], "GStreamer") {
			select {
			case <-time.After(1 * time.Second):
			case <-ss.ctx.Done():
			}
		}

		if ss.state == ServerSessionStatePreRecord && stream != nil {
			panic("stream must be nil when handling publishers")
		}

		if res.StatusCode == base.StatusOK {
			var medi *description.Media

			switch ss.state {
			case ServerSessionStateInitial, ServerSessionStatePrePlay: // play
				if stream == nil {
					panic("stream cannot be nil when StatusCode is StatusOK")
				}

				medi = findMediaByTrackID(stream.Desc.Medias, trackID)
			default: // record
				medi = findMediaByURL(ss.announcedDesc.Medias, path, query, req.URL)
			}

			if medi == nil {
				return &base.Response{
					StatusCode: base.StatusBadRequest,
				}, liberrors.ErrServerMediaNotFound{}
			}

			if _, ok := ss.setuppedMedias[medi]; ok {
				return &base.Response{
					StatusCode: base.StatusBadRequest,
				}, liberrors.ErrServerMediaAlreadySetup{}
			}

			ss.setuppedTransport = &transport

			if ss.state == ServerSessionStateInitial {
				err = stream.readerAdd(ss,
					inTH.ClientPorts,
				)
				if err != nil {
					return &base.Response{
						StatusCode: base.StatusBadRequest,
					}, err
				}

				ss.state = ServerSessionStatePrePlay
				ss.setuppedPath = path
				ss.setuppedQuery = query
				ss.setuppedStream = stream
			}

			th := headers.Transport{}

			if ss.state == ServerSessionStatePrePlay {
				if stream != ss.setuppedStream {
					panic("stream cannot be different than the one returned in previous OnSetup call")
				}

				ssrc, ok := stream.localSSRC(medi)
				if ok {
					th.SSRC = &ssrc
				}
			}

			if res.Header == nil {
				res.Header = make(base.Header)
			}

			sm := &serverSessionMedia{
				ss:           ss,
				media:        medi,
				onPacketRTCP: func(_ rtcp.Packet) {},
			}
			sm.initialize()

			switch transport {
			case TransportUDP:
				sm.udpRTPReadPort = inTH.ClientPorts[0]
				sm.udpRTCPReadPort = inTH.ClientPorts[1]

				sm.udpRTPWriteAddr = &net.UDPAddr{
					IP:   ss.author.ip(),
					Zone: ss.author.zone(),
					Port: sm.udpRTPReadPort,
				}

				sm.udpRTCPWriteAddr = &net.UDPAddr{
					IP:   ss.author.ip(),
					Zone: ss.author.zone(),
					Port: sm.udpRTCPReadPort,
				}

				th.Protocol = headers.TransportProtocolUDP
				de := headers.TransportDeliveryUnicast
				th.Delivery = &de
				th.ClientPorts = inTH.ClientPorts
				th.ServerPorts = &[2]int{sc.s.udpRTPListener.port(), sc.s.udpRTCPListener.port()}

			case TransportUDPMulticast:
				th.Protocol = headers.TransportProtocolUDP
				de := headers.TransportDeliveryMulticast
				th.Delivery = &de
				v := uint(127)
				th.TTL = &v
				d := stream.medias[medi].multicastWriter.ip()
				th.Destination = &d
				th.Ports = &[2]int{ss.s.MulticastRTPPort, ss.s.MulticastRTCPPort}

			default: // TCP
				if inTH.InterleavedIDs != nil {
					sm.tcpChannel = inTH.InterleavedIDs[0]
				} else {
					sm.tcpChannel = ss.findFreeChannelPair()
				}

				th.Protocol = headers.TransportProtocolTCP
				de := headers.TransportDeliveryUnicast
				th.Delivery = &de
				th.InterleavedIDs = &[2]int{sm.tcpChannel, sm.tcpChannel + 1}
			}

			if ss.setuppedMedias == nil {
				ss.setuppedMedias = make(map[*description.Media]*serverSessionMedia)
			}
			ss.setuppedMedias[medi] = sm
			ss.setuppedMediasOrdered = append(ss.setuppedMediasOrdered, sm)

			res.Header["Transport"] = th.Marshal()
		}

		return res, err

	case base.Play:
		// play can be sent twice, allow calling it even if we're already playing
		err := ss.checkState(map[ServerSessionState]struct{}{
			ServerSessionStatePrePlay: {},
			ServerSessionStatePlay:    {},
		})
		if err != nil {
			return &base.Response{
				StatusCode: base.StatusBadRequest,
			}, err
		}

		if ss.State() == ServerSessionStatePrePlay && path != ss.setuppedPath {
			return &base.Response{
				StatusCode: base.StatusBadRequest,
			}, liberrors.ErrServerPathHasChanged{Prev: ss.setuppedPath, Cur: path}
		}

		if ss.state != ServerSessionStatePlay &&
			*ss.setuppedTransport != TransportUDPMulticast {
			ss.createWriter()
		}

		res, err := sc.s.Handler.(ServerHandlerOnPlay).OnPlay(&ServerHandlerOnPlayCtx{
			Session: ss,
			Conn:    sc,
			Request: req,
			Path:    path,
			Query:   query,
		})

		if res.StatusCode == base.StatusOK {
			if ss.state != ServerSessionStatePlay {
				ss.state = ServerSessionStatePlay

				v := ss.s.timeNow().Unix()
				ss.udpLastPacketTime = &v

				ss.timeDecoder = &rtptime.GlobalDecoder2{}
				ss.timeDecoder.Initialize()

				for _, sm := range ss.setuppedMedias {
					sm.start()
				}

				if *ss.setuppedTransport == TransportTCP {
					ss.tcpFrame = &base.InterleavedFrame{}
					ss.tcpBuffer = make([]byte, ss.s.MaxPacketSize+4)
				}

				switch *ss.setuppedTransport {
				case TransportUDP:
					ss.udpCheckStreamTimer = time.NewTimer(ss.s.checkStreamPeriod)
					ss.startWriter()

				case TransportUDPMulticast:
					ss.udpCheckStreamTimer = time.NewTimer(ss.s.checkStreamPeriod)

				default: // TCP
					ss.tcpConn = sc
					err = switchReadFuncError{true}
					// startWriter() is called by ServerConn, through chAsyncStartWriter,
					// after the response has been sent
				}

				ss.setuppedStream.readerSetActive(ss)

				rtpInfo, ok := generateRTPInfo(
					ss.s.timeNow(),
					ss.setuppedMediasOrdered,
					ss.setuppedStream,
					ss.setuppedPath,
					req.URL)

				if ok {
					if res.Header == nil {
						res.Header = make(base.Header)
					}
					res.Header["RTP-Info"] = rtpInfo.Marshal()
				}
			}
		} else {
			if ss.state != ServerSessionStatePlay &&
				*ss.setuppedTransport != TransportUDPMulticast {
				ss.destroyWriter()
			}
		}

		return res, err

	case base.Record:
		err := ss.checkState(map[ServerSessionState]struct{}{
			ServerSessionStatePreRecord: {},
		})
		if err != nil {
			return &base.Response{
				StatusCode: base.StatusBadRequest,
			}, err
		}

		if len(ss.setuppedMedias) != len(ss.announcedDesc.Medias) {
			return &base.Response{
				StatusCode: base.StatusBadRequest,
			}, liberrors.ErrServerNotAllAnnouncedMediasSetup{}
		}

		if path != ss.setuppedPath {
			return &base.Response{
				StatusCode: base.StatusBadRequest,
			}, liberrors.ErrServerPathHasChanged{Prev: ss.setuppedPath, Cur: path}
		}

		ss.createWriter()

		res, err := ss.s.Handler.(ServerHandlerOnRecord).OnRecord(&ServerHandlerOnRecordCtx{
			Session: ss,
			Conn:    sc,
			Request: req,
			Path:    path,
			Query:   query,
		})

		if res.StatusCode == base.StatusOK {
			ss.state = ServerSessionStateRecord

			v := ss.s.timeNow().Unix()
			ss.udpLastPacketTime = &v

			ss.timeDecoder = &rtptime.GlobalDecoder2{}
			ss.timeDecoder.Initialize()

			for _, sm := range ss.setuppedMedias {
				sm.start()
			}

			if *ss.setuppedTransport == TransportTCP {
				ss.tcpFrame = &base.InterleavedFrame{}
				ss.tcpBuffer = make([]byte, ss.s.MaxPacketSize+4)
			}

			switch *ss.setuppedTransport {
			case TransportUDP:
				ss.udpCheckStreamTimer = time.NewTimer(ss.s.checkStreamPeriod)
				ss.startWriter()

			default: // TCP
				ss.tcpConn = sc
				err = switchReadFuncError{true}
				// startWriter() is called by ServerConn, through chAsyncStartWriter,
				// after the response has been sent
			}
		} else {
			ss.destroyWriter()
		}

		return res, err

	case base.Pause:
		err := ss.checkState(map[ServerSessionState]struct{}{
			ServerSessionStatePrePlay:   {},
			ServerSessionStatePlay:      {},
			ServerSessionStatePreRecord: {},
			ServerSessionStateRecord:    {},
		})
		if err != nil {
			return &base.Response{
				StatusCode: base.StatusBadRequest,
			}, err
		}

		res, err := ss.s.Handler.(ServerHandlerOnPause).OnPause(&ServerHandlerOnPauseCtx{
			Session: ss,
			Conn:    sc,
			Request: req,
			Path:    path,
			Query:   query,
		})

		if res.StatusCode == base.StatusOK {
			if ss.state == ServerSessionStatePlay || ss.state == ServerSessionStateRecord {
				ss.destroyWriter()

				if ss.setuppedStream != nil {
					ss.setuppedStream.readerSetInactive(ss)
				}

				for _, sm := range ss.setuppedMedias {
					sm.stop()
				}

				ss.timeDecoder = nil

				switch ss.state {
				case ServerSessionStatePlay:
					ss.state = ServerSessionStatePrePlay

					switch *ss.setuppedTransport {
					case TransportUDP:
						ss.udpCheckStreamTimer = emptyTimer()

					case TransportUDPMulticast:
						ss.udpCheckStreamTimer = emptyTimer()

					default: // TCP
						err = switchReadFuncError{false}
						ss.tcpConn = nil
					}

				case ServerSessionStateRecord:
					switch *ss.setuppedTransport {
					case TransportUDP:
						ss.udpCheckStreamTimer = emptyTimer()

					default: // TCP
						err = switchReadFuncError{false}
						ss.tcpConn = nil
					}

					ss.state = ServerSessionStatePreRecord
				}
			}
		}

		return res, err

	case base.Teardown:
		var err error
		if (ss.state == ServerSessionStatePlay || ss.state == ServerSessionStateRecord) &&
			*ss.setuppedTransport == TransportTCP {
			err = switchReadFuncError{false}
		}

		return &base.Response{
			StatusCode: base.StatusOK,
		}, err

	case base.GetParameter:
		if h, ok := sc.s.Handler.(ServerHandlerOnGetParameter); ok {
			return h.OnGetParameter(&ServerHandlerOnGetParameterCtx{
				Session: ss,
				Conn:    sc,
				Request: req,
				Path:    path,
				Query:   query,
			})
		}

		// GET_PARAMETER is used like a ping when reading, and sometimes
		// also when publishing; reply with 200
		return &base.Response{
			StatusCode: base.StatusOK,
			Header: base.Header{
				"Content-Type": base.HeaderValue{"text/parameters"},
			},
			Body: []byte{},
		}, nil

	case base.SetParameter:
		if h, ok := sc.s.Handler.(ServerHandlerOnSetParameter); ok {
			return h.OnSetParameter(&ServerHandlerOnSetParameterCtx{
				Session: ss,
				Conn:    sc,
				Request: req,
				Path:    path,
				Query:   query,
			})
		}
	}

	return &base.Response{
		StatusCode: base.StatusNotImplemented,
	}, nil
}

func (ss *ServerSession) isChannelPairInUse(channel int) bool {
	for _, sm := range ss.setuppedMedias {
		if (sm.tcpChannel+1) == channel || sm.tcpChannel == channel || sm.tcpChannel == (channel+1) {
			return true
		}
	}
	return false
}

func (ss *ServerSession) findFreeChannelPair() int {
	for i := 0; ; i += 2 { // prefer even channels
		if !ss.isChannelPairInUse(i) {
			return i
		}
	}
}

// OnPacketRTPAny sets a callback that is called when a RTP packet is read from any setupped media.
func (ss *ServerSession) OnPacketRTPAny(cb OnPacketRTPAnyFunc) {
	for _, sm := range ss.setuppedMedias {
		cmedia := sm.media
		for _, forma := range sm.media.Formats {
			ss.OnPacketRTP(sm.media, forma, func(pkt *rtp.Packet) {
				cb(cmedia, forma, pkt)
			})
		}
	}
}

// OnPacketRTCPAny sets a callback that is called when a RTCP packet is read from any setupped media.
func (ss *ServerSession) OnPacketRTCPAny(cb OnPacketRTCPAnyFunc) {
	for _, sm := range ss.setuppedMedias {
		cmedia := sm.media
		ss.OnPacketRTCP(sm.media, func(pkt rtcp.Packet) {
			cb(cmedia, pkt)
		})
	}
}

// OnPacketRTP sets a callback that is called when a RTP packet is read.
func (ss *ServerSession) OnPacketRTP(medi *description.Media, forma format.Format, cb OnPacketRTPFunc) {
	sm := ss.setuppedMedias[medi]
	st := sm.formats[forma.PayloadType()]
	st.onPacketRTP = cb
}

// OnPacketRTCP sets a callback that is called when a RTCP packet is read.
func (ss *ServerSession) OnPacketRTCP(medi *description.Media, cb OnPacketRTCPFunc) {
	sm := ss.setuppedMedias[medi]
	sm.onPacketRTCP = cb
}

func (ss *ServerSession) writePacketRTP(medi *description.Media, payloadType uint8, byts []byte) error {
	sm := ss.setuppedMedias[medi]
	sf := sm.formats[payloadType]

	ss.writerMutex.RLock()
	defer ss.writerMutex.RUnlock()

	if ss.writer == nil {
		return nil
	}

	ok := ss.writer.push(func() error {
		return sf.writePacketRTPInQueue(byts)
	})
	if !ok {
		return liberrors.ErrServerWriteQueueFull{}
	}

	return nil
}

// WritePacketRTP writes a RTP packet to the session.
func (ss *ServerSession) WritePacketRTP(medi *description.Media, pkt *rtp.Packet) error {
	byts := make([]byte, ss.s.MaxPacketSize)
	n, err := pkt.MarshalTo(byts)
	if err != nil {
		return err
	}
	byts = byts[:n]

	return ss.writePacketRTP(medi, pkt.PayloadType, byts)
}

func (ss *ServerSession) writePacketRTCP(medi *description.Media, byts []byte) error {
	sm := ss.setuppedMedias[medi]

	ss.writerMutex.RLock()
	defer ss.writerMutex.RUnlock()

	if ss.writer == nil {
		return nil
	}

	ok := ss.writer.push(func() error {
		return sm.writePacketRTCPInQueue(byts)
	})
	if !ok {
		return liberrors.ErrServerWriteQueueFull{}
	}

	return nil
}

// WritePacketRTCP writes a RTCP packet to the session.
func (ss *ServerSession) WritePacketRTCP(medi *description.Media, pkt rtcp.Packet) error {
	byts, err := pkt.Marshal()
	if err != nil {
		return err
	}

	return ss.writePacketRTCP(medi, byts)
}

// PacketPTS returns the PTS of an incoming RTP packet.
// It is computed by decoding the packet timestamp and sychronizing it with other tracks.
//
// Deprecated: replaced by PacketPTS2.
func (ss *ServerSession) PacketPTS(medi *description.Media, pkt *rtp.Packet) (time.Duration, bool) {
	sm := ss.setuppedMedias[medi]
	sf := sm.formats[pkt.PayloadType]

	v, ok := ss.timeDecoder.Decode(sf.format, pkt)
	if !ok {
		return 0, false
	}

	return multiplyAndDivide(time.Duration(v), time.Second, time.Duration(sf.format.ClockRate())), true
}

// PacketPTS2 returns the PTS of an incoming RTP packet.
// It is computed by decoding the packet timestamp and sychronizing it with other tracks.
func (ss *ServerSession) PacketPTS2(medi *description.Media, pkt *rtp.Packet) (int64, bool) {
	sm := ss.setuppedMedias[medi]
	sf := sm.formats[pkt.PayloadType]
	return ss.timeDecoder.Decode(sf.format, pkt)
}

// PacketNTP returns the NTP timestamp of an incoming RTP packet.
// The NTP timestamp is computed from RTCP sender reports.
func (ss *ServerSession) PacketNTP(medi *description.Media, pkt *rtp.Packet) (time.Time, bool) {
	sm := ss.setuppedMedias[medi]
	sf := sm.formats[pkt.PayloadType]
	return sf.rtcpReceiver.PacketNTP(pkt.Timestamp)
}

func (ss *ServerSession) handleRequest(req sessionRequestReq) (*base.Response, *ServerSession, error) {
	select {
	case ss.chHandleRequest <- req:
		res := <-req.res
		return res.res, res.ss, res.err

	case <-ss.ctx.Done():
		return &base.Response{
			StatusCode: base.StatusBadRequest,
		}, req.sc.session, liberrors.ErrServerTerminated{}
	}
}

func (ss *ServerSession) removeConn(sc *ServerConn) {
	select {
	case ss.chRemoveConn <- sc:
	case <-ss.ctx.Done():
	}
}

func (ss *ServerSession) asyncStartWriter() {
	select {
	case ss.chAsyncStartWriter <- struct{}{}:
	case <-ss.ctx.Done():
	}
}
