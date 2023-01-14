package main

import (
	"context"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"os/exec"
	"path"
	"sync"
	"time"
)

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
./objs/ffmpeg/bin/ffmpeg -i rtmp://127.0.0.1/live...vhost...__defaultVhost__/panda -vf fps=1 -vcodec png -f image2 -an -y -vframes 5 -y /Users/mengxiaowei/jdcloud/mt/srs/trunk/research/api-server/static-dir/live/panda-%03d.png
*/

func (v *SnapshotWorker) Serve() {
	for {
		time.Sleep(time.Second)
		v.snapshots.Range(func(key, value any) bool {
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
				if err := os.MkdirAll(path.Base(outputDir), 0777); err != nil {
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
