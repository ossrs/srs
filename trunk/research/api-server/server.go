package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"
)

type SrsCommonResponse struct {
	Code int         `json:"code"`
	Data interface{} `json:"data"`
}

func SrsWriteErrorResponse(w http.ResponseWriter, err error) {
	w.WriteHeader(http.StatusInternalServerError)
	w.Write([]byte(err.Error()))
}

func SrsWriteDataResponse(w http.ResponseWriter, data interface{}) {
	j, err := json.Marshal(data)
	if err != nil {
		SrsWriteErrorResponse(w, fmt.Errorf("marshal %v, err %v", err))
		return
	}

	w.Header().Set("Content-Type", "application/json")
	w.Write(j)
}

var StaticDir string
var sw *SnapshotWorker

// SrsCommonRequest is the common fields of request messages from SRS HTTP callback.
type SrsCommonRequest struct {
	Action   string `json:"action"`
	ClientId string `json:"client_id"`
	Ip       string `json:"ip"`
	Vhost    string `json:"vhost"`
	App      string `json:"app"`
}

func (v *SrsCommonRequest) String() string {
	return fmt.Sprintf("action=%v, client_id=%v, ip=%v, vhost=%v", v.Action, v.ClientId, v.Ip, v.Vhost)
}

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
type SrsClientRequest struct {
	SrsCommonRequest
	// For on_connect message
	TcUrl   string `json:"tcUrl"`
	PageUrl string `json:"pageUrl"`
	// For on_close message
	SendBytes int64 `json:"send_bytes"`
	RecvBytes int64 `json:"recv_bytes"`
}

func (v *SrsClientRequest) IsOnConnect() bool {
	return v.Action == "on_connect"
}

func (v *SrsClientRequest) IsOnClose() bool {
	return v.Action == "on_close"
}

func (v *SrsClientRequest) String() string {
	var sb strings.Builder
	sb.WriteString(v.SrsCommonRequest.String())
	if v.IsOnConnect() {
		sb.WriteString(fmt.Sprintf(", tcUrl=%v, pageUrl=%v", v.TcUrl, v.PageUrl))
	} else if v.IsOnClose() {
		sb.WriteString(fmt.Sprintf(", send_bytes=%v, recv_bytes=%v", v.SendBytes, v.RecvBytes))
	}
	return sb.String()
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
type SrsStreamRequest struct {
	SrsCommonRequest
	Stream string `json:"stream"`
	Param  string `json:"param"`
}

func (v *SrsStreamRequest) String() string {
	var sb strings.Builder
	sb.WriteString(v.SrsCommonRequest.String())
	if v.IsOnPublish() || v.IsOnUnPublish() {
		sb.WriteString(fmt.Sprintf(", stream=%v, param=%v", v.Stream, v.Param))
	}
	return sb.String()
}

func (v *SrsStreamRequest) IsOnPublish() bool {
	return v.Action == "on_publish"
}

func (v *SrsStreamRequest) IsOnUnPublish() bool {
	return v.Action == "on_unpublish"
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

type SrsSessionRequest struct {
	SrsCommonRequest
	Stream string `json:"stream"`
	Param  string `json:"param"`
	// For on_play only.
	PageUrl string `json:"pageUrl"`
}

func (v *SrsSessionRequest) String() string {
	var sb strings.Builder
	sb.WriteString(v.SrsCommonRequest.String())
	if v.IsOnPlay() || v.IsOnStop() {
		sb.WriteString(fmt.Sprintf(", stream=%v, param=%v", v.Stream, v.Param))
	}
	if v.IsOnPlay() {
		sb.WriteString(fmt.Sprintf(", pageUrl=%v", v.PageUrl))
	}
	return sb.String()
}

func (v *SrsSessionRequest) IsOnPlay() bool {
	return v.Action == "on_play"
}

func (v *SrsSessionRequest) IsOnStop() bool {
	return v.Action == "on_stop"
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

type SrsDvrRequest struct {
	SrsCommonRequest
	Stream string `json:"stream"`
	Param  string `json:"param"`
	Cwd    string `json:"cwd"`
	File   string `json:"file"`
}

func (v *SrsDvrRequest) String() string {
	var sb strings.Builder
	sb.WriteString(v.SrsCommonRequest.String())
	if v.IsOnDvr() {
		sb.WriteString(fmt.Sprintf(", stream=%v, param=%v, cwd=%v, file=%v", v.Stream, v.Param, v.Cwd, v.File))
	}
	return sb.String()
}

func (v *SrsDvrRequest) IsOnDvr() bool {
	return v.Action == "on_dvr"
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
			  "action": "on_hls",
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

type SrsHlsRequest struct {
	SrsCommonRequest
	Stream   string  `json:"stream"`
	Param    string  `json:"param"`
	Duration float64 `json:"duration"`
	Cwd      string  `json:"cwd"`
	File     string  `json:"file"`
	SeqNo    int     `json:"seq_no"`
}

func (v *SrsHlsRequest) String() string {
	var sb strings.Builder
	sb.WriteString(v.SrsCommonRequest.String())
	if v.IsOnHls() {
		sb.WriteString(fmt.Sprintf(", stream=%v, param=%v, cwd=%v, file=%v, duration=%v, seq_no=%v", v.Stream, v.Param, v.Cwd, v.File, v.Duration, v.SeqNo))
	}
	return sb.String()
}

func (v *SrsHlsRequest) IsOnHls() bool {
	return v.Action == "on_hls"
}

/*
the snapshot api,
to start a snapshot when encoder start publish stream,
stop the snapshot worker when stream finished.

{"action":"on_publish","client_id":108,"ip":"127.0.0.1","vhost":"__defaultVhost__","app":"live","stream":"livestream"}
{"action":"on_unpublish","client_id":108,"ip":"127.0.0.1","vhost":"__defaultVhost__","app":"live","stream":"livestream"}
*/

type SrsSnapShotRequest struct {
	SrsCommonRequest
	Stream string `json:"stream"`
}

func (v *SrsSnapShotRequest) String() string {
	var sb strings.Builder
	sb.WriteString(v.SrsCommonRequest.String())
	if v.IsOnPublish() || v.IsOnUnPublish() {
		sb.WriteString(fmt.Sprintf(", stream=%v", v.Stream))
	}
	return sb.String()
}

func (v *SrsSnapShotRequest) IsOnPublish() bool {
	return v.Action == "on_publish"
}

func (v *SrsSnapShotRequest) IsOnUnPublish() bool {
	return v.Action == "on_unpublish"
}

type SnapshotJob struct {
	SrsSnapShotRequest
	updatedAt  time.Time
	cancelCtx  context.Context
	cancelFunc context.CancelFunc
	vframes    int
	timeout    time.Duration
}

func NewSnapshotJob() *SnapshotJob {
	v := &SnapshotJob{
		vframes: 5,
		timeout: time.Duration(30) * time.Second,
	}
	v.cancelCtx, v.cancelFunc = context.WithCancel(context.Background())
	return v
}

func (v *SnapshotJob) Tag() string {
	return fmt.Sprintf("%v/%v/%v", v.Vhost, v.App, v.Stream)
}

func (v *SnapshotJob) Abort() {
	v.cancelFunc()
	log.Println(fmt.Sprintf("cancel snapshot job %v", v.Tag()))
}

/*
./objs/ffmpeg/bin/ffmpeg -i rtmp://127.0.0.1/live/livestream \
    -vf fps=1 -vcodec png -f image2 -an -vframes 5 \
    -y static-dir/live/livestream-%03d.png
*/
func (v *SnapshotJob) do(ffmpegPath, inputUrl string) (err error) {
	outputPicDir := path.Join(StaticDir, v.App)
	if err = os.MkdirAll(outputPicDir, 0777); err != nil {
		log.Println(fmt.Sprintf("create snapshot image dir:%v failed, err is %v", outputPicDir, err))
		return
	}

	normalPicPath := path.Join(outputPicDir, fmt.Sprintf("%v", v.Stream)+"-%03d.png")
	bestPng := path.Join(outputPicDir, fmt.Sprintf("%v-best.png", v.Stream))

	params := []string{
		"-i", inputUrl,
		"-vf", "fps=1",
		"-vcodec", "png",
		"-f", "image2",
		"-an",
		"-vframes", strconv.Itoa(v.vframes),
		"-y", normalPicPath,
	}
	log.Println(fmt.Sprintf("start snapshot, cmd param=%v %v", ffmpegPath, strings.Join(params, " ")))
	timeoutCtx, _ := context.WithTimeout(v.cancelCtx, v.timeout)
	cmd := exec.CommandContext(timeoutCtx, ffmpegPath, params...)
	if err = cmd.Run(); err != nil {
		log.Println(fmt.Sprintf("run snapshot %v cmd failed, err is %v", v.Tag(), err))
		return
	}

	bestFileSize := int64(0)
	for i := 1; i <= v.vframes; i++ {
		pic := path.Join(outputPicDir, fmt.Sprintf("%v-%03d.png", v.Stream, i))
		fi, err := os.Stat(pic)
		if err != nil {
			log.Println(fmt.Sprintf("stat pic:%v failed, err is %v", pic, err))
			continue
		}
		if bestFileSize == 0 {
			bestFileSize = fi.Size()
		} else if fi.Size() > bestFileSize {
			os.Remove(bestPng)
			os.Symlink(pic, bestPng)
			bestFileSize = fi.Size()
		}
	}
	log.Println(fmt.Sprintf("%v the best thumbnail is %v", v.Tag(), bestPng))
	return
}

func (v *SnapshotJob) Serve(ffmpegPath, inputUrl string) {
	sleep := time.Duration(1) * time.Second
	for {
		v.do(ffmpegPath, inputUrl)
		select {
		case <-time.After(sleep):
			log.Println(fmt.Sprintf("%v sleep %v to redo snapshot", v.Tag(), sleep))
			break
		case <-v.cancelCtx.Done():
			log.Println(fmt.Sprintf("snapshot job %v cancelled", v.Tag()))
			return
		}
	}
}

type SnapshotWorker struct {
	snapshots  *sync.Map // key is stream url
	ffmpegPath string
}

func NewSnapshotWorker(ffmpegPath string) *SnapshotWorker {
	sw := &SnapshotWorker{
		snapshots:  new(sync.Map),
		ffmpegPath: ffmpegPath,
	}
	return sw
}

func (v *SnapshotWorker) Create(sm *SrsSnapShotRequest) {
	streamUrl := fmt.Sprintf("rtmp://127.0.0.1/%v/%v?vhost=%v", sm.App, sm.Stream, sm.Vhost)
	if _, ok := v.snapshots.Load(streamUrl); ok {
		return
	}
	sj := NewSnapshotJob()
	sj.SrsSnapShotRequest = *sm
	sj.updatedAt = time.Now()
	go sj.Serve(v.ffmpegPath, streamUrl)
	v.snapshots.Store(streamUrl, sj)
}

func (v *SnapshotWorker) Destroy(sm *SrsSnapShotRequest) {
	streamUrl := fmt.Sprintf("rtmp://127.0.0.1/%v/%v?vhost=%v", sm.App, sm.Stream, sm.Vhost)
	value, ok := v.snapshots.Load(streamUrl)
	if ok {
		sj := value.(*SnapshotJob)
		sj.Abort()
		v.snapshots.Delete(streamUrl)
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

type SrsForwardRequest struct {
	SrsCommonRequest
	TcUrl  string `json:"tc_url"`
	Stream string `json:"stream"`
	Param  string `json:"param"`
}

func (v *SrsForwardRequest) String() string {
	var sb strings.Builder
	sb.WriteString(v.SrsCommonRequest.String())
	if v.IsOnForward() {
		sb.WriteString(fmt.Sprintf(", tcUrl=%v, stream=%v, param=%v", v.TcUrl, v.Stream, v.Param))
	}
	return sb.String()
}

func (v *SrsForwardRequest) IsOnForward() bool {
	return v.Action == "on_forward"
}

func main() {
	srsBin := os.Args[0]
	if strings.HasPrefix(srsBin, "/var") {
		srsBin = "go run ."
	}

	var port int
	var ffmpegPath string
	flag.IntVar(&port, "p", 8085, "HTTP listen port. Default is 8085")
	flag.StringVar(&StaticDir, "s", "./static-dir", "HTML home for snapshot. Default is ./static-dir")
	flag.StringVar(&ffmpegPath, "ffmpeg", "/usr/local/bin/ffmpeg", "FFmpeg for snapshot. Default is /usr/local/bin/ffmpeg")
	flag.Usage = func() {
		fmt.Println("A demo api-server for SRS\n")
		fmt.Println(fmt.Sprintf("Usage: %v [flags]", srsBin))
		flag.PrintDefaults()
		fmt.Println(fmt.Sprintf("For example:"))
		fmt.Println(fmt.Sprintf(" 		%v -p 8085", srsBin))
		fmt.Println(fmt.Sprintf(" 		%v 8085", srsBin))
	}
	flag.Parse()

	log.SetFlags(log.Lshortfile | log.Ldate | log.Ltime | log.Lmicroseconds)

	// check if only one number arg
	if len(os.Args[1:]) == 1 {
		portArg := os.Args[1]
		var err error
		if port, err = strconv.Atoi(portArg); err != nil {
			log.Println(fmt.Sprintf("parse port arg:%v to int failed, err %v", portArg, err))
			flag.Usage()
			os.Exit(1)
		}
	}

	sw = NewSnapshotWorker(ffmpegPath)
	StaticDir, err := filepath.Abs(StaticDir)
	if err != nil {
		panic(err)
	}
	log.Println(fmt.Sprintf("api server listen at port:%v, static_dir:%v", port, StaticDir))

	http.Handle("/", http.FileServer(http.Dir(StaticDir)))
	http.HandleFunc("/api/v1", func(writer http.ResponseWriter, request *http.Request) {
		res := &struct {
			Code int `json:"code"`
			Urls struct {
				Clients  string `json:"clients"`
				Streams  string `json:"streams"`
				Sessions string `json:"sessions"`
				Dvrs     string `json:"dvrs"`
				Chats    string `json:"chats"`
				Servers  struct {
					Summary string `json:"summary"`
					Get     string `json:"GET"`
					Post    string `json:"POST ip=node_ip&device_id=device_id"`
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

	// handle the clients requests: connect/disconnect vhost/app.
	http.HandleFunc("/api/v1/clients", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "POST" {
			SrsWriteDataResponse(w, struct{}{})
			return
		}

		if err := func() error {
			body, err := ioutil.ReadAll(r.Body)
			if err != nil {
				return fmt.Errorf("read request body, err %v", err)
			}
			log.Println(fmt.Sprintf("post to clients, req=%v", string(body)))

			msg := &SrsClientRequest{}
			if err := json.Unmarshal(body, msg); err != nil {
				return fmt.Errorf("parse message from %v, err %v", string(body), err)
			}
			log.Println(fmt.Sprintf("Got %v", msg.String()))

			if !msg.IsOnConnect() && !msg.IsOnClose() {
				return fmt.Errorf("invalid message %v", msg.String())
			}

			SrsWriteDataResponse(w, &SrsCommonResponse{Code: 0})
			return nil
		}(); err != nil {
			SrsWriteErrorResponse(w, err)
		}
	})

	// handle the streams requests: publish/unpublish stream.
	http.HandleFunc("/api/v1/streams", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "POST" {
			SrsWriteDataResponse(w, struct{}{})
			return
		}

		if err := func() error {
			body, err := ioutil.ReadAll(r.Body)
			if err != nil {
				return fmt.Errorf("read request body, err %v", err)
			}
			log.Println(fmt.Sprintf("post to streams, req=%v", string(body)))

			msg := &SrsStreamRequest{}
			if err := json.Unmarshal(body, msg); err != nil {
				return fmt.Errorf("parse message from %v, err %v", string(body), err)
			}
			log.Println(fmt.Sprintf("Got %v", msg.String()))

			if !msg.IsOnPublish() && !msg.IsOnUnPublish() {
				return fmt.Errorf("invalid message %v", msg.String())
			}

			SrsWriteDataResponse(w, &SrsCommonResponse{Code: 0})
			return nil
		}(); err != nil {
			SrsWriteErrorResponse(w, err)
		}
	})

	// handle the sessions requests: client play/stop stream
	http.HandleFunc("/api/v1/sessions", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "POST" {
			SrsWriteDataResponse(w, struct{}{})
			return
		}

		if err := func() error {
			body, err := ioutil.ReadAll(r.Body)
			if err != nil {
				return fmt.Errorf("read request body, err %v", err)
			}
			log.Println(fmt.Sprintf("post to sessions, req=%v", string(body)))

			msg := &SrsSessionRequest{}
			if err := json.Unmarshal(body, msg); err != nil {
				return fmt.Errorf("parse message from %v, err %v", string(body), err)
			}
			log.Println(fmt.Sprintf("Got %v", msg.String()))

			if !msg.IsOnPlay() && !msg.IsOnStop() {
				return fmt.Errorf("invalid message %v", msg.String())
			}

			SrsWriteDataResponse(w, &SrsCommonResponse{Code: 0})
			return nil
		}(); err != nil {
			SrsWriteErrorResponse(w, err)
		}
	})

	// handle the dvrs requests: dvr stream.
	http.HandleFunc("/api/v1/dvrs", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "POST" {
			SrsWriteDataResponse(w, struct{}{})
			return
		}

		if err := func() error {
			body, err := ioutil.ReadAll(r.Body)
			if err != nil {
				return fmt.Errorf("read request body, err %v", err)
			}
			log.Println(fmt.Sprintf("post to dvrs, req=%v", string(body)))

			msg := &SrsDvrRequest{}
			if err := json.Unmarshal(body, msg); err != nil {
				return fmt.Errorf("parse message from %v, err %v", string(body), err)
			}
			log.Println(fmt.Sprintf("Got %v", msg.String()))

			if !msg.IsOnDvr() {
				return fmt.Errorf("invalid message %v", msg.String())
			}

			SrsWriteDataResponse(w, &SrsCommonResponse{Code: 0})
			return nil
		}(); err != nil {
			SrsWriteErrorResponse(w, err)
		}
	})

	// handle the dvrs requests: on_hls stream.
	http.HandleFunc("/api/v1/hls", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "POST" {
			SrsWriteDataResponse(w, struct{}{})
			return
		}

		if err := func() error {
			body, err := ioutil.ReadAll(r.Body)
			if err != nil {
				return fmt.Errorf("read request body, err %v", err)
			}
			log.Println(fmt.Sprintf("post to hls, req=%v", string(body)))

			msg := &SrsHlsRequest{}
			if err := json.Unmarshal(body, msg); err != nil {
				return fmt.Errorf("parse message from %v, err %v", string(body), err)
			}
			log.Println(fmt.Sprintf("Got %v", msg.String()))

			if !msg.IsOnHls() {
				return fmt.Errorf("invalid message %v", msg.String())
			}

			SrsWriteDataResponse(w, &SrsCommonResponse{Code: 0})
			return nil
		}(); err != nil {
			SrsWriteErrorResponse(w, err)
		}
	})

	// not support yet
	http.HandleFunc("/api/v1/chat", func(w http.ResponseWriter, r *http.Request) {
		SrsWriteErrorResponse(w, fmt.Errorf("not implemented"))
	})

	http.HandleFunc("/api/v1/snapshots", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "POST" {
			SrsWriteDataResponse(w, struct{}{})
			return
		}

		if err := func() error {
			body, err := ioutil.ReadAll(r.Body)
			if err != nil {
				return fmt.Errorf("read request body, err %v", err)
			}
			log.Println(fmt.Sprintf("post to snapshots, req=%v", string(body)))

			msg := &SrsSnapShotRequest{}
			if err := json.Unmarshal(body, msg); err != nil {
				return fmt.Errorf("parse message from %v, err %v", string(body), err)
			}
			log.Println(fmt.Sprintf("Got %v", msg.String()))

			if msg.IsOnPublish() {
				sw.Create(msg)
			} else if msg.IsOnUnPublish() {
				sw.Destroy(msg)
			} else {
				return fmt.Errorf("invalid message %v", msg.String())
			}

			SrsWriteDataResponse(w, &SrsCommonResponse{Code: 0})
			return nil
		}(); err != nil {
			SrsWriteErrorResponse(w, err)
		}
	})

	// handle the dynamic forward requests: on_forward stream.
	http.HandleFunc("/api/v1/forward", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "POST" {
			SrsWriteDataResponse(w, struct{}{})
			return
		}

		if err := func() error {
			body, err := ioutil.ReadAll(r.Body)
			if err != nil {
				return fmt.Errorf("read request body, err %v", err)
			}
			log.Println(fmt.Sprintf("post to forward, req=%v", string(body)))

			msg := &SrsForwardRequest{}
			if err := json.Unmarshal(body, msg); err != nil {
				return fmt.Errorf("parse message from %v, err %v", string(body), err)
			}
			log.Println(fmt.Sprintf("Got %v", msg.String()))

			if !msg.IsOnForward() {
				return fmt.Errorf("invalid message %v", msg.String())
			}

			SrsWriteDataResponse(w, &SrsCommonResponse{Code: 0, Data: &struct {
				Urls []string `json:"urls"`
			}{
				Urls: []string{"rtmp://127.0.0.1:19350/test/teststream"},
			}})
			return nil
		}(); err != nil {
			SrsWriteErrorResponse(w, err)
		}
	})

	addr := fmt.Sprintf(":%v", port)
	log.Println(fmt.Sprintf("start listen on:%v", addr))
	if err := http.ListenAndServe(addr, nil); err != nil {
		log.Println(fmt.Sprintf("listen on addr:%v failed, err is %v", addr, err))
	}
}
