package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

const (
	// ok, success, completed.
	success = 0
	// error when read http request
	error_system_read_request = 100
	// error when parse json
	error_system_parse_json = 101
	// request action invalid
	error_request_invalid_action = 200
	// cdn node not exists
	error_cdn_node_not_exists = 201
	// http request failed
	error_http_request_failed = 202

	// chat id not exist
	error_chat_id_not_exist = 300
	//

	client_action_on_connect = "on_connect"
	client_action_on_close = "on_close"
	session_action_on_play = "on_play"
	session_action_on_stop = "on_stop"
)

const (
	HttpJson       = "application/json"
)

type SrsError struct {
	Code int `json:"code"`
	Data interface{} `json:"data"`
}

func Response(se *SrsError) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		body, _ := json.Marshal(se)
		w.Header().Set("Content-Type", HttpJson)
		w.Write(body)
	})
}

const Example = `
SRS api callback server, Copyright (c) 2013-2016 SRS(ossrs)
Example:
	./api-server -p 8085 -s ./static-dir
See also: https://github.com/ossrs/srs
`

var StaticDir string
var cm *ChatManager
var sw *SnapshotWorker


/*
handle the clients requests: connect/disconnect vhost/app.
    for SRS hook: on_connect/on_close
    on_connect:
        when client connect to vhost/app, call the hook,
        the request in the POST data string is a object encode by json:
              {
                  "action": "on_connect",
                  "client_id": "9308h583",
                  "ip": "192.168.1.10",
    			  "vhost": "video.test.com",
                  "app": "live",
                  "tcUrl": "rtmp://video.test.com/live?key=d2fa801d08e3f90ed1e1670e6e52651a",
                  "pageUrl": "http://www.test.com/live.html"
              }
    on_close:
        when client close/disconnect to vhost/app/stream, call the hook,
        the request in the POST data string is a object encode by json:
              {
                  "action": "on_close",
                  "client_id": "9308h583",
                  "ip": "192.168.1.10",
				  "vhost": "video.test.com",
                  "app": "live",
                  "send_bytes": 10240,
				  "recv_bytes": 10240
              }
    if valid, the hook must return HTTP code 200(Stauts OK) and response
    an int value specifies the error code(0 corresponding to success):
          0
*/
type ClientMsg struct {
	Action string `json:"action"`
	ClientId string `json:"client_id"`
	Ip string `json:"ip"`
	Vhost string `json:"vhost"`
	App string `json:"app"`
}

type ClientOnConnectMsg struct {
	ClientMsg
	TcUrl string `json:"tcUrl"`
	PageUrl string `json:"pageUrl"`
}

func (v *ClientOnConnectMsg) String() string {
	return fmt.Sprintf("srs:%v, client id=%v, ip=%v, vhost=%v, app=%v, tcUrl=%v, pageUrl=%v", v.Action, v.ClientId, v.Ip, v.Vhost, v.App, v.TcUrl, v.PageUrl)
}

type ClientOnCloseMsg struct {
	ClientMsg
	SendBytes int64 `json:"send_bytes"`
	RecvBytes int64 `json:"recv_bytes"`
}

func (v *ClientOnCloseMsg) String() string {
	return fmt.Sprintf("srs:%v, client id=%v, ip=%v, vhost=%v, app=%v, send_bytes=%v, recv_bytes=%v", v.Action, v.ClientId, v.Ip, v.Vhost, v.App, v.SendBytes, v.RecvBytes)
}

type Client struct {}

func (v *Client) Parse(body []byte) (se *SrsError) {
	data := &struct {
		Action string `json:"action"`
	}{}
	if err := json.Unmarshal(body, data); err != nil {
		return &SrsError{Code: error_system_parse_json, Data: fmt.Sprintf("parse client action failed, err is %v", err.Error())}
	}

	if data.Action == client_action_on_connect {
		msg := &ClientOnConnectMsg{}
		if err := json.Unmarshal(body, msg); err != nil {
			return &SrsError{Code: error_system_parse_json, Data: fmt.Sprintf("parse client %v msg failed, err is %v", client_action_on_connect, err.Error())}
		}
		log.Println(msg)
	} else if data.Action == client_action_on_close {
		msg := &ClientOnCloseMsg{}
		if err := json.Unmarshal(body, msg); err != nil {
			return &SrsError{Code: error_system_parse_json, Data: fmt.Sprintf("parse client %v msg failed, err is %v", client_action_on_close, err.Error())}
		}
		log.Println(msg)
	}
	return nil
}

// handle the clients requests: connect/disconnect vhost/app.
func ClientServe(w http.ResponseWriter, r *http.Request)  {
	if r.Method == "GET" {
		res := struct {}{}
		body, _ := json.Marshal(res)
		w.Write(body)
	} else if r.Method == "POST" {
		body, err := ioutil.ReadAll(r.Body)
		if err != nil {
			Response(&SrsError{Code: error_system_read_request, Data: fmt.Sprintf("read request body failed, err is %v", err)}).ServeHTTP(w, r)
			return
		}
		log.Println(fmt.Sprintf("post to clients, req=%v", string(body)))
		c := &Client{}
		if se := c.Parse(body); se != nil {
			Response(se).ServeHTTP(w, r)
			return
		}
		Response(&SrsError{Code: 0, Data: nil}).ServeHTTP(w, r)
		return
	}
}


/*
   for SRS hook: on_publish/on_unpublish
   on_publish:
       when client(encoder) publish to vhost/app/stream, call the hook,
       the request in the POST data string is a object encode by json:
             {
                 "action": "on_publish",
                 "client_id": "9308h583",
                 "ip": "192.168.1.10",
				 "vhost": "video.test.com",
				 "app": "live",
                 "stream": "livestream",
				 "param":"?token=xxx&salt=yyy"
             }
   on_unpublish:
       when client(encoder) stop publish to vhost/app/stream, call the hook,
       the request in the POST data string is a object encode by json:
             {
                 "action": "on_unpublish",
                 "client_id": "9308h583",
                 "ip": "192.168.1.10",
				 "vhost": "video.test.com",
                 "app": "live",
                 "stream": "livestream",
				 "param":"?token=xxx&salt=yyy"
             }
   if valid, the hook must return HTTP code 200(Stauts OK) and response
   an int value specifies the error code(0 corresponding to success):
         0
*/
type StreamMsg struct {
	Action string `json:"action"`
	ClientId string `json:"client_id"`
	Ip string `json:"ip"`
	Vhost string `json:"vhost"`
	App string `json:"app"`
	Stream string `json:"stream"`
	Param string `json:"param"`
}

func (v *StreamMsg) String() string {
	return fmt.Sprintf("srs %v: client id=%v, ip=%v, vhost=%v, app=%v, stream=%v, param=%v", v.Action, v.ClientId, v.Ip, v.Vhost, v.App, v.Stream, v.Param)
}

type Stream struct {}

func (v *Stream) Parse(body []byte) (se *SrsError) {
	msg := &StreamMsg{}
	if err := json.Unmarshal(body, msg); err != nil {
		return &SrsError{Code: error_system_parse_json, Data: fmt.Sprintf("parse stream msg failed, err is %v", err.Error())}
	}
	log.Println(msg)
	return nil
}

// handle the streams requests: publish/unpublish stream.

func StreamServe(w http.ResponseWriter, r *http.Request) {
	if r.Method == "GET" {
		res := struct {}{}
		body, _ := json.Marshal(res)
		w.Write(body)
	} else if r.Method == "POST" {
		body, err := ioutil.ReadAll(r.Body)
		if err != nil {
			Response(&SrsError{Code: error_system_read_request, Data: fmt.Sprintf("read request body failed, err is %v", err)}).ServeHTTP(w, r)
			return
		}
		log.Println(fmt.Sprintf("post to streams, req=%v", string(body)))
		c := &Stream{}
		if se := c.Parse(body); se != nil {
			Response(se).ServeHTTP(w, r)
			return
		}
		Response(&SrsError{Code: 0, Data: nil}).ServeHTTP(w, r)
		return
	}
}


/*
   for SRS hook: on_play/on_stop
   on_play:
       when client(encoder) publish to vhost/app/stream, call the hook,
       the request in the POST data string is a object encode by json:
             {
                 "action": "on_play",
                 "client_id": "9308h583",
                 "ip": "192.168.1.10",
				 "vhost": "video.test.com",
				 "app": "live",
                 "stream": "livestream",
                 "param":"?token=xxx&salt=yyy",
                 "pageUrl": "http://www.test.com/live.html"
             }
   on_stop:
       when client(encoder) stop publish to vhost/app/stream, call the hook,
       the request in the POST data string is a object encode by json:
             {
                 "action": "on_stop",
                 "client_id": "9308h583",
                 "ip": "192.168.1.10",
				 "vhost": "video.test.com",
				 "app": "live",
                 "stream": "livestream",
				 "param":"?token=xxx&salt=yyy"
             }
   if valid, the hook must return HTTP code 200(Stauts OK) and response
   an int value specifies the error code(0 corresponding to success):
         0
*/

type SessionMsg struct {
	Action string `json:"action"`
	ClientId string `json:"client_id"`
	Ip string `json:"ip"`
	Vhost string `json:"vhost"`
	App string `json:"app"`
	Stream string `json:"stream"`
	Param string `json:"param"`
}

type SessionOnPlayMsg struct {
	SessionMsg
	PageUrl string `json:"pageUrl"`
}

func (v *SessionOnPlayMsg) String() string {
	return fmt.Sprintf("srs %v: client id=%v, ip=%v, vhost=%v, app=%v, stream=%v, param=%v, pageUrl=%v", v.Action, v.ClientId, v.Ip, v.Vhost, v.App, v.Stream, v.Param, v.PageUrl)
}

type SessionOnStopMsg struct {
	SessionMsg
}

func (v *SessionOnStopMsg) String() string {
	return fmt.Sprintf("srs %v: client id=%v, ip=%v, vhost=%v, app=%v, stream=%v, param=%v", v.Action, v.ClientId, v.Ip, v.Vhost, v.App, v.Stream, v.Param)
}

type Session struct {}

func (v *Session) Parse(body []byte) (se *SrsError) {
	data := &struct {
		Action string `json:"action"`
	}{}
	if err := json.Unmarshal(body, data); err != nil {
		return &SrsError{Code: error_system_parse_json, Data: fmt.Sprintf("parse session action failed, err is %v", err.Error())}
	}

	if data.Action == session_action_on_play {
		msg := &SessionOnPlayMsg{}
		if err := json.Unmarshal(body, msg); err != nil {
			return &SrsError{Code: error_system_parse_json, Data: fmt.Sprintf("parse session %v msg failed, err is %v", data.Action, err.Error())}
		}
		log.Println(msg)
	} else if data.Action == session_action_on_stop {
		msg := &SessionOnStopMsg{}
		if err := json.Unmarshal(body, msg); err != nil {
			return &SrsError{Code: error_system_parse_json, Data: fmt.Sprintf("parse session %v msg failed, err is %v", data.Action, err.Error())}
		}
		log.Println(msg)
	}
	return nil
}

// handle the sessions requests: client play/stop stream
func SessionServe(w http.ResponseWriter, r *http.Request) {
	if r.Method == "GET" {
		res := struct {}{}
		body, _ := json.Marshal(res)
		w.Write(body)
	} else if r.Method == "POST" {
		body, err := ioutil.ReadAll(r.Body)
		if err != nil {
			Response(&SrsError{Code: error_system_read_request, Data: fmt.Sprintf("read request body failed, err is %v", err)}).ServeHTTP(w, r)
			return
		}
		log.Println(fmt.Sprintf("post to sessions, req=%v", string(body)))
		c := &Session{}
		if se := c.Parse(body); se != nil {
			Response(se).ServeHTTP(w, r)
			return
		}
		Response(&SrsError{Code: 0, Data: nil}).ServeHTTP(w, r)
		return
	}
}


/*
   for SRS hook: on_dvr
   on_dvr:
       when srs reap a dvr file, call the hook,
       the request in the POST data string is a object encode by json:
             {
                 "action": "on_dvr",
                 "client_id": "9308h583",
                 "ip": "192.168.1.10",
				 "vhost": "video.test.com",
				 "app": "live",
                 "stream": "livestream",
                 "param":"?token=xxx&salt=yyy",
                 "cwd": "/usr/local/srs",
                 "file": "./objs/nginx/html/live/livestream.1420254068776.flv"
             }
   if valid, the hook must return HTTP code 200(Stauts OK) and response
   an int value specifies the error code(0 corresponding to success):
         0
*/

type DvrMsg struct {
	Action string `json:"action"`
	ClientId string `json:"client_id"`
	Ip string `json:"ip"`
	Vhost string `json:"vhost"`
	App string `json:"app"`
	Stream string `json:"stream"`
	Param string `json:"param"`
	Cwd string `json:"cwd"`
	File string `json:"file"`
}

func (v *DvrMsg) String() string {
	return fmt.Sprintf("srs %v: client id=%v, ip=%v, vhost=%v, app=%v, stream=%v, param=%v, cwd=%v, file=%v", v.Action, v.ClientId, v.Ip, v.Vhost, v.App, v.Stream, v.Param, v.Cwd, v.File)
}

type Dvr struct {}

func (v *Dvr) Parse(body []byte) (se *SrsError) {
	msg := &DvrMsg{}
	if err := json.Unmarshal(body, msg); err != nil {
		return &SrsError{Code: error_system_parse_json, Data: fmt.Sprintf("parse dvr msg failed, err is %v", err.Error())}
	}
	log.Println(msg)
	return nil
}

// handle the dvrs requests: dvr stream.
func DvrServe(w http.ResponseWriter, r *http.Request) {
	if r.Method == "GET" {
		res := struct {}{}
		body, _ := json.Marshal(res)
		w.Write(body)
	} else if r.Method == "POST" {
		body, err := ioutil.ReadAll(r.Body)
		if err != nil {
			Response(&SrsError{Code: error_system_read_request, Data: fmt.Sprintf("read request body failed, err is %v", err)}).ServeHTTP(w, r)
			return
		}
		log.Println(fmt.Sprintf("post to dvrs, req=%v", string(body)))
		c := &Dvr{}
		if se := c.Parse(body); se != nil {
			Response(se).ServeHTTP(w, r)
			return
		}
		Response(&SrsError{Code: 0, Data: nil}).ServeHTTP(w, r)
		return
	}
}


/*
   for SRS hook: on_hls_notify
   on_hls_notify:
       when srs reap a ts file of hls, call this hook,
       used to push file to cdn network, by get the ts file from cdn network.
       so we use HTTP GET and use the variable following:
             [app], replace with the app.
             [stream], replace with the stream.
             [param], replace with the param.
             [ts_url], replace with the ts url.
       ignore any return data of server.

    for SRS hook: on_hls
    on_hls:
        when srs reap a dvr file, call the hook,
        the request in the POST data string is a object encode by json:
              {
                  "action": "on_dvr",
                  "client_id": "9308h583",
                  "ip": "192.168.1.10",
                  "vhost": "video.test.com",
                  "app": "live",
                  "stream": "livestream",
				  "param":"?token=xxx&salt=yyy",
                  "duration": 9.68, // in seconds
                  "cwd": "/usr/local/srs",
                  "file": "./objs/nginx/html/live/livestream.1420254068776-100.ts",
                  "seq_no": 100
              }
    if valid, the hook must return HTTP code 200(Stauts OK) and response
    an int value specifies the error code(0 corresponding to success):
          0
*/

type HlsMsg struct {
	Action string `json:"action"`
	ClientId string `json:"client_id"`
	Ip string `json:"ip"`
	Vhost string `json:"vhost"`
	App string `json:"app"`
	Stream string `json:"stream"`
	Param string `json:"param"`
	Duration float64 `json:"duration"`
	Cwd string `json:"cwd"`
	File string `json:"file"`
	SeqNo int `json:"seq_no"`
}

func (v *HlsMsg) String() string {
	return fmt.Sprintf("srs %v: client id=%v, ip=%v, vhost=%v, app=%v, stream=%v, param=%v, duration=%v, cwd=%v, file=%v, seq_no=%v", v.Action, v.ClientId, v.Ip, v.Vhost, v.App, v.Stream, v.Param, v.Duration, v.Cwd, v.File, v.SeqNo)
}

type Hls struct {}

func (v *Hls) Parse(body []byte) (se *SrsError) {
	msg := &HlsMsg{}
	if err := json.Unmarshal(body, msg); err != nil {
		return &SrsError{Code: error_system_parse_json, Data: fmt.Sprintf("parse hls msg failed, err is %v", err.Error())}
	}
	log.Println(msg)
	return nil
}

// handle the hls requests: hls stream.
func HlsServe(w http.ResponseWriter, r *http.Request) {
	log.Println(fmt.Sprintf("hls serve, uPath=%v", r.URL.Path))
	if r.Method == "GET" {
		subPath := r.URL.Path[len("/api/v1/hls/"):]
		res := struct {
			Args []string `json:"args"`
			KwArgs url.Values `json:"kwargs"`
		}{
			Args: strings.Split(subPath, "/"),
			KwArgs: r.URL.Query(),
		}
		body, _ := json.Marshal(res)
		w.Write(body)
	} else if r.Method == "POST" {
		body, err := ioutil.ReadAll(r.Body)
		if err != nil {
			Response(&SrsError{Code: error_system_read_request, Data: fmt.Sprintf("read request body failed, err is %v", err)}).ServeHTTP(w, r)
			return
		}
		log.Println(fmt.Sprintf("post to hls, req=%v", string(body)))
		c := &Hls{}
		if se := c.Parse(body); se != nil {
			Response(se).ServeHTTP(w, r)
			return
		}
		Response(&SrsError{Code: 0, Data: nil}).ServeHTTP(w, r)
		return
	}
}


/*
# object fields:
# id: an int value indicates the id of user.
# username: a str indicates the user name.
# url: a str indicates the url of user stream.
# agent: a str indicates the agent of user.
# join_date: a number indicates the join timestamp in seconds.
# join_date_str: a str specifies the formated friendly time.
# heartbeat: a number indicates the heartbeat timestamp in seconds.
# vcodec: a dict indicates the video codec info.
# acodec: a dict indicates the audio codec info.

# dead time in seconds, if exceed, remove the chat.
*/

type Chat struct {
	Id int `json:"id"`
	Username string `json:"username"`
	Url string `json:"url"`
	JoinDate int64 `json:"join_date"`
	JoinDateStr string `json:"join_date_str"`
	Heartbeat int64 `json:"heartbeat"`
}

type ChatManager struct {
	globalId int
	chats *sync.Map
	deadTime int
}

func NewChatManager() *ChatManager {
	v := &ChatManager{
		globalId: 100,
		// key is globalId, value is chat
		chats: new(sync.Map),
		deadTime: 15,
	}
	return v
}

func (v *ChatManager) List() (chats []*Chat) {
	chats = []*Chat{}
	v.chats.Range(func(key, value interface{}) bool {
		_, chat := key.(int), value.(*Chat)
		if (time.Now().Unix() - chat.Heartbeat) > int64(v.deadTime) {
			v.chats.Delete(key)
			return true
		}
		chats = append(chats, chat)
		return true
	})
	return
}

func (v *ChatManager) Update(id int) (se *SrsError) {
	value, ok := v.chats.Load(id)
	if !ok {
		return &SrsError{Code: error_chat_id_not_exist, Data: fmt.Sprintf("cannot find id:%v", id)}
	}
	c := value.(*Chat)
	c.Heartbeat = time.Now().Unix()
	log.Println(fmt.Sprintf("heartbeat chat success, id=%v", id))
	return nil
}

func (v *ChatManager) Delete(id int) (se *SrsError) {
	if _, ok := v.chats.Load(id); !ok {
		return &SrsError{Code: error_chat_id_not_exist, Data: fmt.Sprintf("cannot find id:%v", id)}
	}
	v.chats.Delete(id)
	log.Println(fmt.Sprintf("delete chat success, id=%v", id))
	return
}

func (v *ChatManager) Add(c *Chat) {
	c.Id = v.globalId
	now := time.Now()
	c.JoinDate, c.Heartbeat = now.Unix(), now.Unix()
	c.JoinDateStr = now.Format("2006-01-02 15:04:05")
	v.globalId += 1
	v.chats.Store(c.Id, c)
}

// the chat streams, public chat room.
func ChatServe(w http.ResponseWriter, r *http.Request) {
	log.Println(fmt.Sprintf("got a chat req, uPath=%v", r.URL.Path))
	if r.Method == "GET" {
		chats := cm.List()
		Response(&SrsError{Code: 0, Data: chats}).ServeHTTP(w, r)
	} else if r.Method == "POST" {
		body, err := ioutil.ReadAll(r.Body)
		if err != nil {
			Response(&SrsError{Code: error_system_read_request, Data: fmt.Sprintf("read request body failed, err is %v", err)}).ServeHTTP(w, r)
			return
		}
		c := &Chat{}
		if err := json.Unmarshal(body, c); err != nil {
			Response(&SrsError{Code: error_system_parse_json, Data: fmt.Sprintf("parse body to chat json failed, err is %v", err)})
			return
		}
		cm.Add(c)
		log.Println(fmt.Sprintf("create chat success, id=%v", c.Id))
		Response(&SrsError{Code: 0, Data: nil}).ServeHTTP(w, r)
	} else if r.Method == "PUT" {
		// TODO: parse id?
		Response(cm.Update(0)).ServeHTTP(w, r)
	} else if r.Method == "DELETE" {
		// TODO: parse id?
		Response(cm.Delete(0)).ServeHTTP(w, r)
	}
}

/*
the snapshot api,
to start a snapshot when encoder start publish stream,
stop the snapshot worker when stream finished.

{"action":"on_publish","client_id":108,"ip":"127.0.0.1","vhost":"__defaultVhost__","app":"live","stream":"livestream"}
{"action":"on_unpublish","client_id":108,"ip":"127.0.0.1","vhost":"__defaultVhost__","app":"live","stream":"livestream"}
*/

type SnapShot struct {}

func (v *SnapShot) Parse(body []byte) (se *SrsError) {
	msg := &StreamMsg{}
	if err := json.Unmarshal(body, msg); err != nil {
		return &SrsError{Code: error_system_parse_json, Data: fmt.Sprintf("parse snapshot msg failed, err is %v", err.Error())}
	}
	if msg.Action == "on_publish" {
		sw.Create(msg)
		return &SrsError{Code: 0, Data: nil}
	} else if msg.Action == "on_unpublish" {
		sw.Destroy(msg)
		return &SrsError{Code: 0, Data: nil}
	} else {
		return &SrsError{Code: error_request_invalid_action, Data: fmt.Sprintf("invalid req action:%v", msg.Action)}
	}
}

func SnapshotServe(w http.ResponseWriter, r *http.Request) {
	if r.Method == "POST" {
		body, err := ioutil.ReadAll(r.Body)
		if err != nil {
			Response(&SrsError{Code: error_system_read_request, Data: fmt.Sprintf("read request body failed, err is %v", err)}).ServeHTTP(w, r)
			return
		}
		log.Println(fmt.Sprintf("post to snapshot, req=%v", string(body)))
		s := &SnapShot{}
		if se := s.Parse(body); se != nil {
			Response(se).ServeHTTP(w, r)
			return
		}
		Response(&SrsError{Code: 0, Data: nil}).ServeHTTP(w, r)
	}
}

type SnapshotJob struct {
	StreamMsg
	cmd       *exec.Cmd
	abort     bool
	timestamp time.Time
	lock      *sync.RWMutex
}

func NewSnapshotJob() *SnapshotJob {
	v := &SnapshotJob{
		lock: new(sync.RWMutex),
	}
	return v
}

func (v *SnapshotJob) UpdateAbort(status bool) {
	v.lock.Lock()
	defer v.lock.Unlock()
	v.abort = status
}

func (v *SnapshotJob) IsAbort() bool {
	v.lock.RLock()
	defer v.lock.RUnlock()
	return v.abort
}

type SnapshotWorker struct {
	snapshots *sync.Map // key is stream url
	ffmpegPath string
}

func NewSnapshotWorker(ffmpegPath string) *SnapshotWorker {
	sw := &SnapshotWorker{
		snapshots: new(sync.Map),
		ffmpegPath: ffmpegPath,
	}
	return sw
}

/*
./objs/ffmpeg/bin/ffmpeg -i rtmp://127.0.0.1/live?vhost=__defaultVhost__/panda -vf fps=1 -vcodec png -f image2 -an -y -vframes 5 -y static-dir/live/panda-%03d.png
*/

func (v *SnapshotWorker) Serve() {
	for {
		time.Sleep(time.Second)
		v.snapshots.Range(func(key, value interface{}) bool {
			// range each snapshot job
			streamUrl := key.(string)
			sj := value.(*SnapshotJob)
			streamTag := fmt.Sprintf("%v/%v/%v", sj.Vhost, sj.App, sj.Stream)
			if sj.IsAbort() { // delete aborted snapshot job
				if sj.cmd != nil && sj.cmd.Process != nil {
					if err := sj.cmd.Process.Kill(); err != nil {
						log.Println(fmt.Sprintf("snapshot job:%v kill running cmd failed, err is %v", streamTag, err))
					}
				}
				v.snapshots.Delete(key)
				return true
			}

			if sj.cmd == nil { // start a ffmpeg snap cmd
				outputDir := path.Join(StaticDir, sj.App, fmt.Sprintf("%v", sj.Stream) + "-%03d.png")
				bestPng := path.Join(StaticDir, sj.App, fmt.Sprintf("%v-best.png", sj.Stream))
				if err := os.MkdirAll(path.Dir(outputDir), 0777); err != nil {
					log.Println(fmt.Sprintf("create snapshot image dir:%v failed, err is %v", path.Base(outputDir), err))
					return true
				}
				vframes := 5
				param := fmt.Sprintf("%v -i %v -vf fps=1 -vcodec png -f image2 -an -y -vframes %v -y %v", v.ffmpegPath, streamUrl, vframes, outputDir)
				timeoutCtx, _ := context.WithTimeout(context.Background(), time.Duration(30) * time.Second)
				cmd := exec.CommandContext(timeoutCtx, "/bin/bash", "-c", param)
				if err := cmd.Start(); err != nil {
					log.Println(fmt.Sprintf("start snapshot %v cmd failed, err is %v", streamTag, err))
					return true
				}
				sj.cmd = cmd
				log.Println(fmt.Sprintf("start snapshot success, cmd param=%v", param))
				go func() {
					if err := sj.cmd.Wait(); err != nil {
						log.Println(fmt.Sprintf("snapshot %v cmd wait failed, err is %v", streamTag, err))
					} else { // choose the best quality image
						bestFileSize := int64(0)
						for  i := 1; i <= vframes; i ++ {
							pic := path.Join(StaticDir, sj.App, fmt.Sprintf("%v-%03d.png", sj.Stream, i))
							fi, err := os.Stat(pic)
							if err != nil {
								log.Println(fmt.Sprintf("stat pic:%v failed, err is %v", pic, err))
								continue
							}
							if bestFileSize == 0 {
								bestFileSize = fi.Size()
							} else if fi.Size() > bestFileSize {
								os.Remove(bestPng)
								os.Link(pic, bestPng)
								bestFileSize = fi.Size()
							}
						}
						log.Println(fmt.Sprintf("%v the best thumbnail is %v", streamTag, bestPng))
					}
					sj.cmd = nil
				}()
			} else {
				log.Println(fmt.Sprintf("snapshot %v cmd process is running, status=%v", streamTag, sj.cmd.ProcessState))
			}
			return true
		})
	}
}

func (v *SnapshotWorker) Create(sm *StreamMsg) {
	streamUrl := fmt.Sprintf("rtmp://127.0.0.1/%v?vhost=%v/%v", sm.App, sm.Vhost, sm.Stream)
	if _, ok := v.snapshots.Load(streamUrl); ok {
		return
	}
	sj := NewSnapshotJob()
	sj.StreamMsg = *sm
	sj.timestamp = time.Now()
	v.snapshots.Store(streamUrl, sj)
}

func (v *SnapshotWorker) Destroy(sm *StreamMsg) {
	streamUrl := fmt.Sprintf("rtmp://127.0.0.1/%v?vhost=%v/%v", sm.App, sm.Vhost, sm.Stream)
	value, ok := v.snapshots.Load(streamUrl)
	if ok {
		sj := value.(*SnapshotJob)
		sj.UpdateAbort(true)
		v.snapshots.Store(streamUrl, sj)
		log.Println(fmt.Sprintf("set stream:%v to destroy, update abort", sm.Stream))
	} else {
		log.Println(fmt.Sprintf("cannot find stream:%v in snapshot worker", streamUrl))
	}
	return
}

/*
handle the forward requests: dynamic forward url.
for SRS hook: on_forward
on_forward:
	when srs reap a dvr file, call the hook,
	the request in the POST data string is a object encode by json:
		  {
			  "action": "on_forward",
			  "server_id": "server_test",
			  "client_id": 1985,
			  "ip": "192.168.1.10",
			  "vhost": "video.test.com",
			  "app": "live",
			  "tcUrl": "rtmp://video.test.com/live?key=d2fa801d08e3f90ed1e1670e6e52651a",
			  "stream": "livestream",
			  "param":"?token=xxx&salt=yyy"
		  }
if valid, the hook must return HTTP code 200(Stauts OK) and response
an int value specifies the error code(0 corresponding to success):
	  0
*/

type ForwardMsg struct {
	Action string `json:"action"`
	ServerId string `json:"server_id"`
	ClientId string `json:"client_id"`
	Ip string `json:"ip"`
	Vhost string `json:"vhost"`
	App string `json:"app"`
	TcUrl string `json:"tc_url"`
	Stream string `json:"stream"`
	Param string `json:"param"`
}

func (v *ForwardMsg) String() string {
	return fmt.Sprintf("srs %v: client id=%v, ip=%v, vhost=%v, app=%v, tcUrl=%v, stream=%v, param=%v", v.Action, v.ClientId, v.Ip, v.Vhost, v.App, v.TcUrl, v.Stream, v.Param)
}

type Forward struct {}

/*
backend service config description:
   support multiple rtmp urls(custom addresses or third-party cdn service),
   url's host is slave service.
For example:
   ["rtmp://127.0.0.1:19350/test/teststream", "rtmp://127.0.0.1:19350/test/teststream?token=xxxx"]
*/
func (v *Forward) Parse(body []byte) (se *SrsError) {
	msg := &ForwardMsg{}
	if err := json.Unmarshal(body, msg); err != nil {
		return &SrsError{Code: error_system_parse_json, Data: fmt.Sprintf("parse forward msg failed, err is %v", err.Error())}
	}
	if msg.Action == "on_forward" {
		log.Println(msg)
		res := &struct {
			Urls []string `json:"urls"`
		}{
			Urls: []string{"rtmp://127.0.0.1:19350/test/teststream"},
		}
		return &SrsError{Code: 0, Data: res}
	} else {
		return &SrsError{Code: error_request_invalid_action, Data: fmt.Sprintf("invalid action:%v", msg.Action)}
	}
	return
}

func ForwardServe(w http.ResponseWriter, r *http.Request)  {
	if r.Method == "GET" {
		res := struct {}{}
		body, _ := json.Marshal(res)
		w.Write(body)
	} else if r.Method == "POST" {
		body, err := ioutil.ReadAll(r.Body)
		if err != nil {
			Response(&SrsError{Code: error_system_read_request, Data: fmt.Sprintf("read request body failed, err is %v", err)}).ServeHTTP(w, r)
			return
		}
		log.Println(fmt.Sprintf("post to forward, req=%v", string(body)))
		c := &Forward{}
		if se := c.Parse(body); se != nil {
			Response(se).ServeHTTP(w, r)
			return
		}
		Response(&SrsError{Code: 0, Data: nil}).ServeHTTP(w, r)
		return
	}
}

func main()  {
	var port int
	var ffmpegPath string
	flag.IntVar(&port, "p", 8085, "use -p to specify listen port, default is 8085")
	flag.StringVar(&StaticDir, "s", "./static-dir", "use -s to specify static-dir, default is ./static-dir")
	flag.StringVar(&ffmpegPath, "ffmpeg", "./objs/ffmpeg/bin/ffmpeg", "use -ffmpeg to specify ffmpegPath, default is ./objs/ffmpeg/bin/ffmpeg")
	flag.Usage = func() {
		fmt.Fprintln(flag.CommandLine.Output(), "Usage: apiServer [flags]")
		flag.PrintDefaults()
		fmt.Fprintln(flag.CommandLine.Output(), Example)
	}
	flag.Parse()

	if len(os.Args[1:]) == 0 {
		flag.Usage()
		os.Exit(0)
	}

	log.SetFlags(log.Lshortfile | log.Ldate | log.Ltime | log.Lmicroseconds)
	cm = NewChatManager()
	sw = NewSnapshotWorker(ffmpegPath)
	go sw.Serve()

	if len(StaticDir) == 0 {
		curAbsDir, _ := filepath.Abs(filepath.Dir(os.Args[0]))
		StaticDir = path.Join(curAbsDir, "./static-dir")
	} else {
		StaticDir, _ = filepath.Abs(StaticDir)
	}
	log.Println(fmt.Sprintf("api server listen at port:%v, static_dir:%v", port, StaticDir))

	http.Handle("/", http.FileServer(http.Dir(StaticDir)))
	http.HandleFunc("/api/v1", func(writer http.ResponseWriter, request *http.Request) {
		res := &struct {
			Code int `json:"code"`
			Urls struct{
				Clients string `json:"clients"`
				Streams string `json:"streams"`
				Sessions string `json:"sessions"`
				Dvrs string `json:"dvrs"`
				Chats string `json:"chats"`
				Servers struct{
					Summary string `json:"summary"`
					Get string `json:"GET"`
					Post string `json:"POST ip=node_ip&device_id=device_id"`
				}
			} `json:"urls"`
		}{
			Code: 0,
		}
		res.Urls.Clients = "for srs http callback, to handle the clients requests: connect/disconnect vhost/app."
		res.Urls.Streams = "for srs http callback, to handle the streams requests: publish/unpublish stream."
		res.Urls.Sessions = "for srs http callback, to handle the sessions requests: client play/stop stream."
		res.Urls.Dvrs = "for srs http callback, to handle the dvr requests: dvr stream."
		//res.Urls.Chats = "for srs demo meeting, the chat streams, public chat room."
		res.Urls.Servers.Summary = "for srs raspberry-pi and meeting demo."
		res.Urls.Servers.Get = "get the current raspberry-pi servers info."
		res.Urls.Servers.Post = "the new raspberry-pi server info."
		// TODO: no snapshots
		body, _ := json.Marshal(res)
		writer.Write(body)
	})

	http.HandleFunc("/api/v1/clients", ClientServe)
	http.HandleFunc("/api/v1/streams", StreamServe)
	http.HandleFunc("/api/v1/sessions", SessionServe)
	http.HandleFunc("/api/v1/dvrs", DvrServe)
	http.HandleFunc("/api/v1/hls", HlsServe)
	http.HandleFunc("/api/v1/hls/", HlsServe)

	// not support yet
	http.HandleFunc("/api/v1/chat", ChatServe)

	http.HandleFunc("/api/v1/snapshots", SnapshotServe)
	http.HandleFunc("/api/v1/forward", ForwardServe)

	addr := fmt.Sprintf(":%v", port)
	log.Println(fmt.Sprintf("start listen on:%v", addr))
	if err := http.ListenAndServe(addr, nil); err != nil {
		log.Println(fmt.Sprintf("listen on addr:%v failed, err is %v", addr, err))
	}
}