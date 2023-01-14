package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"path"
	"path/filepath"
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

var StaticDir string

const Example = `
SRS api callback server, Copyright (c) 2013-2016 SRS(ossrs)
Example:
	./api-server -p 8085 -s ./static-dir
See also: https://github.com/ossrs/srs
`

//func FileServer() http.Handler {
//	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
//		upath := r.URL.Path
//		if !strings.HasPrefix(upath, "/") {
//			upath = "/" + upath
//		}
//		log.Println(fmt.Sprintf("upath=%v", upath))
//	})
//}

var cm *ChatManager
var sm *ServerManager
var sw *SnapshotWorker

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

	cm = NewChatManager()
	sm = NewServerManager()
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
	http.HandleFunc("/api/v1/proxy/", ProxyServe)

	// not support yet
	http.HandleFunc("/api/v1/chat", ChatServe)

	http.HandleFunc("/api/v1/servers", ServerServe)
	http.HandleFunc("/api/v1/servers/", ServerServe)
	http.HandleFunc("/api/v1/snapshots", SnapshotServe)
	http.HandleFunc("/api/v1/forward", ForwardServe)

	addr := fmt.Sprintf(":%v", port)
	log.Println(fmt.Sprintf("start listen on:%v", addr))
	if err := http.ListenAndServe(addr, nil); err != nil {
		log.Println(fmt.Sprintf("listen on addr:%v failed, err is %v", addr, err))
	}
}