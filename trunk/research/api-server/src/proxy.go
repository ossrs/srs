package main

import (
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
)

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
*/

type Proxy struct {
	proxyUrl string
}

func (v *Proxy) Serve(notifyPath string) (se *SrsError) {
	v.proxyUrl = fmt.Sprintf("http://%v", notifyPath)
	log.Println(fmt.Sprintf("start to proxy url:%v", v.proxyUrl))
	resp, err := http.Get(v.proxyUrl)
	if err != nil {
		return &SrsError{error_http_request_failed, fmt.Sprintf("get %v failed, err is %v", v.proxyUrl, err)}
	}
	defer resp.Body.Close()
	if _, err = ioutil.ReadAll(resp.Body); err != nil {
		return &SrsError{error_system_read_request, fmt.Sprintf("read proxy body failed, err is %v", err)}
	}
	log.Println(fmt.Sprintf("completed proxy url:%v", v.proxyUrl))
	return nil
}

// handle the hls proxy requests: hls stream.
func ProxyServe(w http.ResponseWriter, r *http.Request) {
	if r.Method == "GET" {
		subPath := r.URL.Path[len("/api/v1/proxy/"):]
		c := &Proxy{}
		if se := c.Serve(subPath); se != nil {
			Response(se).ServeHTTP(w, r)
			return
		}
		w.Write([]byte(c.proxyUrl))
	}
}
