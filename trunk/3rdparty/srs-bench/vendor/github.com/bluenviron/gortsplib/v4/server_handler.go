package gortsplib

import (
	"github.com/bluenviron/gortsplib/v4/pkg/base"
	"github.com/bluenviron/gortsplib/v4/pkg/description"
)

// ServerHandler is the interface implemented by all the server handlers.
type ServerHandler interface{}

// ServerHandlerOnConnOpenCtx is the context of OnConnOpen.
type ServerHandlerOnConnOpenCtx struct {
	Conn *ServerConn
}

// ServerHandlerOnConnOpen can be implemented by a ServerHandler.
type ServerHandlerOnConnOpen interface {
	// called when a connection is opened.
	OnConnOpen(*ServerHandlerOnConnOpenCtx)
}

// ServerHandlerOnConnCloseCtx is the context of OnConnClose.
type ServerHandlerOnConnCloseCtx struct {
	Conn  *ServerConn
	Error error
}

// ServerHandlerOnConnClose can be implemented by a ServerHandler.
type ServerHandlerOnConnClose interface {
	// called when a connection is closed.
	OnConnClose(*ServerHandlerOnConnCloseCtx)
}

// ServerHandlerOnSessionOpenCtx is the context OnSessionOpen.
type ServerHandlerOnSessionOpenCtx struct {
	Session *ServerSession
	Conn    *ServerConn
}

// ServerHandlerOnSessionOpen can be implemented by a ServerHandler.
type ServerHandlerOnSessionOpen interface {
	// called when a session is opened.
	OnSessionOpen(*ServerHandlerOnSessionOpenCtx)
}

// ServerHandlerOnSessionCloseCtx is the context of ServerHandlerOnSessionClose.
type ServerHandlerOnSessionCloseCtx struct {
	Session *ServerSession
	Error   error
}

// ServerHandlerOnSessionClose can be implemented by a ServerHandler.
type ServerHandlerOnSessionClose interface {
	// called when a session is closed.
	OnSessionClose(*ServerHandlerOnSessionCloseCtx)
}

// ServerHandlerOnRequest can be implemented by a ServerHandler.
type ServerHandlerOnRequest interface {
	// called when receiving a request from a connection.
	OnRequest(*ServerConn, *base.Request)
}

// ServerHandlerOnResponse can be implemented by a ServerHandler.
type ServerHandlerOnResponse interface {
	// called when sending a response to a connection.
	OnResponse(*ServerConn, *base.Response)
}

// ServerHandlerOnDescribeCtx is the context of OnDescribe.
type ServerHandlerOnDescribeCtx struct {
	Conn    *ServerConn
	Request *base.Request
	Path    string
	Query   string
}

// ServerHandlerOnDescribe can be implemented by a ServerHandler.
type ServerHandlerOnDescribe interface {
	// called when receiving a DESCRIBE request.
	OnDescribe(*ServerHandlerOnDescribeCtx) (*base.Response, *ServerStream, error)
}

// ServerHandlerOnAnnounceCtx is the context of OnAnnounce.
type ServerHandlerOnAnnounceCtx struct {
	Session     *ServerSession
	Conn        *ServerConn
	Request     *base.Request
	Path        string
	Query       string
	Description *description.Session
}

// ServerHandlerOnAnnounce can be implemented by a ServerHandler.
type ServerHandlerOnAnnounce interface {
	// called when receiving an ANNOUNCE request.
	OnAnnounce(*ServerHandlerOnAnnounceCtx) (*base.Response, error)
}

// ServerHandlerOnSetupCtx is the context of OnSetup.
type ServerHandlerOnSetupCtx struct {
	Session   *ServerSession
	Conn      *ServerConn
	Request   *base.Request
	Path      string
	Query     string
	Transport Transport
}

// ServerHandlerOnSetup can be implemented by a ServerHandler.
type ServerHandlerOnSetup interface {
	// called when receiving a SETUP request.
	// must return a Response and a stream.
	// the stream is needed to
	// - add the session the the stream's readers
	// - send the stream SSRC to the session
	OnSetup(*ServerHandlerOnSetupCtx) (*base.Response, *ServerStream, error)
}

// ServerHandlerOnPlayCtx is the context of OnPlay.
type ServerHandlerOnPlayCtx struct {
	Session *ServerSession
	Conn    *ServerConn
	Request *base.Request
	Path    string
	Query   string
}

// ServerHandlerOnPlay can be implemented by a ServerHandler.
type ServerHandlerOnPlay interface {
	// called when receiving a PLAY request.
	OnPlay(*ServerHandlerOnPlayCtx) (*base.Response, error)
}

// ServerHandlerOnRecordCtx is the context of OnRecord.
type ServerHandlerOnRecordCtx struct {
	Session *ServerSession
	Conn    *ServerConn
	Request *base.Request
	Path    string
	Query   string
}

// ServerHandlerOnRecord can be implemented by a ServerHandler.
type ServerHandlerOnRecord interface {
	// called when receiving a RECORD request.
	OnRecord(*ServerHandlerOnRecordCtx) (*base.Response, error)
}

// ServerHandlerOnPauseCtx is the context of OnPause.
type ServerHandlerOnPauseCtx struct {
	Session *ServerSession
	Conn    *ServerConn
	Request *base.Request
	Path    string
	Query   string
}

// ServerHandlerOnPause can be implemented by a ServerHandler.
type ServerHandlerOnPause interface {
	// called when receiving a PAUSE request.
	OnPause(*ServerHandlerOnPauseCtx) (*base.Response, error)
}

// ServerHandlerOnGetParameterCtx is the context of OnGetParameter.
type ServerHandlerOnGetParameterCtx struct {
	Session *ServerSession
	Conn    *ServerConn
	Request *base.Request
	Path    string
	Query   string
}

// ServerHandlerOnGetParameter can be implemented by a ServerHandler.
type ServerHandlerOnGetParameter interface {
	// called when receiving a GET_PARAMETER request.
	OnGetParameter(*ServerHandlerOnGetParameterCtx) (*base.Response, error)
}

// ServerHandlerOnSetParameterCtx is the context of OnSetParameter.
type ServerHandlerOnSetParameterCtx struct {
	Session *ServerSession
	Conn    *ServerConn
	Request *base.Request
	Path    string
	Query   string
}

// ServerHandlerOnSetParameter can be implemented by a ServerHandler.
type ServerHandlerOnSetParameter interface {
	// called when receiving a SET_PARAMETER request.
	OnSetParameter(*ServerHandlerOnSetParameterCtx) (*base.Response, error)
}

// ServerHandlerOnPacketLostCtx is the context of OnPacketLost.
//
// Deprecated: replaced by ServerHandlerOnPacketsLostCtx
type ServerHandlerOnPacketLostCtx struct {
	Session *ServerSession
	Error   error
}

// ServerHandlerOnPacketLost can be implemented by a ServerHandler.
//
// Deprecated: replaced by ServerHandlerOnPacketsLost
type ServerHandlerOnPacketLost interface {
	// called when the server detects lost packets.
	OnPacketLost(*ServerHandlerOnPacketLostCtx)
}

// ServerHandlerOnPacketsLostCtx is the context of OnPacketsLost.
type ServerHandlerOnPacketsLostCtx struct {
	Session *ServerSession
	Lost    uint64
}

// ServerHandlerOnPacketsLost can be implemented by a ServerHandler.
type ServerHandlerOnPacketsLost interface {
	// called when the server detects lost packets.
	OnPacketsLost(*ServerHandlerOnPacketsLostCtx)
}

// ServerHandlerOnDecodeErrorCtx is the context of OnDecodeError.
type ServerHandlerOnDecodeErrorCtx struct {
	Session *ServerSession
	Error   error
}

// ServerHandlerOnDecodeError can be implemented by a ServerHandler.
type ServerHandlerOnDecodeError interface {
	// called when a non-fatal decode error occurs.
	OnDecodeError(*ServerHandlerOnDecodeErrorCtx)
}

// ServerHandlerOnStreamWriteErrorCtx is the context of OnStreamWriteError.
type ServerHandlerOnStreamWriteErrorCtx struct {
	Session *ServerSession
	Error   error
}

// ServerHandlerOnStreamWriteError can be implemented by a ServerHandler.
type ServerHandlerOnStreamWriteError interface {
	// called when a ServerStream is unable to write packets to a session.
	OnStreamWriteError(*ServerHandlerOnStreamWriteErrorCtx)
}
