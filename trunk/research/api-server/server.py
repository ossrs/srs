#!/usr/bin/python
'''
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
'''

"""
the api-server is a default demo server for srs to call
when srs get some event, for example, when client connect
to srs, srs can invoke the http api of the api-server
"""

import sys
# reload sys model to enable the getdefaultencoding method.
reload(sys)
# set the default encoding to utf-8
# using exec to set the encoding, to avoid error in IDE.
exec("sys.setdefaultencoding('utf-8')")
assert sys.getdefaultencoding().lower() == "utf-8"

import os, json, time, datetime, cherrypy, threading

# simple log functions.
def trace(msg):
    date = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print "[%s][trace] %s"%(date, msg)

# enable crossdomain access for js-client
# define the following method:
#   def OPTIONS(self, *args, **kwargs)
#       enable_crossdomain()
# invoke this method to enable js to request crossdomain.
def enable_crossdomain():
    cherrypy.response.headers["Access-Control-Allow-Origin"] = "*"
    cherrypy.response.headers["Access-Control-Allow-Methods"] = "GET, POST, HEAD, PUT, DELETE"
    # generate allow headers for crossdomain.
    allow_headers = ["Cache-Control", "X-Proxy-Authorization", "X-Requested-With", "Content-Type"]
    cherrypy.response.headers["Access-Control-Allow-Headers"] = ",".join(allow_headers)

# error codes definition
class Error:
    # ok, success, completed.
    success = 0
    # error when parse json
    system_parse_json = 100
    # request action invalid
    request_invalid_action = 200
    # cdn node not exists
    cdn_node_not_exists = 201

'''
handle the clients requests: connect/disconnect vhost/app.
'''
class RESTClients(object):
    exposed = True

    def GET(self):
        enable_crossdomain()

        clients = {}
        return json.dumps(clients)

    '''
    for SRS hook: on_connect/on_close
    on_connect:
        when client connect to vhost/app, call the hook,
        the request in the POST data string is a object encode by json:
              {
                  "action": "on_connect",
                  "client_id": 1985,
                  "ip": "192.168.1.10", "vhost": "video.test.com", "app": "live",
                  "pageUrl": "http://www.test.com/live.html"
              }
    on_close:
        when client close/disconnect to vhost/app/stream, call the hook,
        the request in the POST data string is a object encode by json:
              {
                  "action": "on_close",
                  "client_id": 1985,
                  "ip": "192.168.1.10", "vhost": "video.test.com", "app": "live"
              }
    if valid, the hook must return HTTP code 200(Stauts OK) and response
    an int value specifies the error code(0 corresponding to success):
          0
    '''
    def POST(self):
        enable_crossdomain()

        # return the error code in str
        code = Error.success

        req = cherrypy.request.body.read()
        trace("post to clients, req=%s"%(req))
        try:
            json_req = json.loads(req)
        except Exception, ex:
            code = Error.system_parse_json
            trace("parse the request to json failed, req=%s, ex=%s, code=%s"%(req, ex, code))
            return str(code)

        action = json_req["action"]
        if action == "on_connect":
            code = self.__on_connect(json_req)
        elif action == "on_close":
            code = self.__on_close(json_req)
        else:
            trace("invalid request action: %s"%(json_req["action"]))
            code = Error.request_invalid_action

        return str(code)

    def OPTIONS(self, *args, **kwargs):
        enable_crossdomain()

    def __on_connect(self, req):
        code = Error.success

        trace("srs %s: client id=%s, ip=%s, vhost=%s, app=%s, pageUrl=%s"%(
            req["action"], req["client_id"], req["ip"], req["vhost"], req["app"], req["pageUrl"]
        ))

        # TODO: process the on_connect event

        return code

    def __on_close(self, req):
        code = Error.success

        trace("srs %s: client id=%s, ip=%s, vhost=%s, app=%s"%(
            req["action"], req["client_id"], req["ip"], req["vhost"], req["app"]
        ))

        # TODO: process the on_close event

        return code

'''
handle the streams requests: publish/unpublish stream.
'''
class RESTStreams(object):
    exposed = True

    def GET(self):
        enable_crossdomain()

        streams = {}
        return json.dumps(streams)

    '''
    for SRS hook: on_publish/on_unpublish
    on_publish:
        when client(encoder) publish to vhost/app/stream, call the hook,
        the request in the POST data string is a object encode by json:
              {
                  "action": "on_publish",
                  "client_id": 1985,
                  "ip": "192.168.1.10", "vhost": "video.test.com", "app": "live",
                  "stream": "livestream"
              }
    on_unpublish:
        when client(encoder) stop publish to vhost/app/stream, call the hook,
        the request in the POST data string is a object encode by json:
              {
                  "action": "on_unpublish",
                  "client_id": 1985,
                  "ip": "192.168.1.10", "vhost": "video.test.com", "app": "live",
                  "stream": "livestream"
              }
    if valid, the hook must return HTTP code 200(Stauts OK) and response
    an int value specifies the error code(0 corresponding to success):
          0
    '''
    def POST(self):
        enable_crossdomain()

        # return the error code in str
        code = Error.success

        req = cherrypy.request.body.read()
        trace("post to streams, req=%s"%(req))
        try:
            json_req = json.loads(req)
        except Exception, ex:
            code = Error.system_parse_json
            trace("parse the request to json failed, req=%s, ex=%s, code=%s"%(req, ex, code))
            return str(code)

        action = json_req["action"]
        if action == "on_publish":
            code = self.__on_publish(json_req)
        elif action == "on_unpublish":
            code = self.__on_unpublish(json_req)
        else:
            trace("invalid request action: %s"%(json_req["action"]))
            code = Error.request_invalid_action

        return str(code)

    def OPTIONS(self, *args, **kwargs):
        enable_crossdomain()

    def __on_publish(self, req):
        code = Error.success

        trace("srs %s: client id=%s, ip=%s, vhost=%s, app=%s, stream=%s"%(
            req["action"], req["client_id"], req["ip"], req["vhost"], req["app"], req["stream"]
        ))

        # TODO: process the on_publish event

        return code

    def __on_unpublish(self, req):
        code = Error.success

        trace("srs %s: client id=%s, ip=%s, vhost=%s, app=%s, stream=%s"%(
            req["action"], req["client_id"], req["ip"], req["vhost"], req["app"], req["stream"]
        ))

        # TODO: process the on_unpublish event

        return code

'''
handle the sessions requests: client play/stop stream
'''
class RESTSessions(object):
    exposed = True

    def GET(self):
        enable_crossdomain()

        sessions = {}
        return json.dumps(sessions)

    '''
    for SRS hook: on_play/on_stop
    on_play:
        when client(encoder) publish to vhost/app/stream, call the hook,
        the request in the POST data string is a object encode by json:
              {
                  "action": "on_play",
                  "client_id": 1985,
                  "ip": "192.168.1.10", "vhost": "video.test.com", "app": "live",
                  "stream": "livestream"
              }
    on_stop:
        when client(encoder) stop publish to vhost/app/stream, call the hook,
        the request in the POST data string is a object encode by json:
              {
                  "action": "on_stop",
                  "client_id": 1985,
                  "ip": "192.168.1.10", "vhost": "video.test.com", "app": "live",
                  "stream": "livestream"
              }
    if valid, the hook must return HTTP code 200(Stauts OK) and response
    an int value specifies the error code(0 corresponding to success):
          0
    '''
    def POST(self):
        enable_crossdomain()

        # return the error code in str
        code = Error.success

        req = cherrypy.request.body.read()
        trace("post to sessions, req=%s"%(req))
        try:
            json_req = json.loads(req)
        except Exception, ex:
            code = Error.system_parse_json
            trace("parse the request to json failed, req=%s, ex=%s, code=%s"%(req, ex, code))
            return str(code)

        action = json_req["action"]
        if action == "on_play":
            code = self.__on_play(json_req)
        elif action == "on_stop":
            code = self.__on_stop(json_req)
        else:
            trace("invalid request action: %s"%(json_req["action"]))
            code = Error.request_invalid_action

        return str(code)

    def OPTIONS(self, *args, **kwargs):
        enable_crossdomain()

    def __on_play(self, req):
        code = Error.success

        trace("srs %s: client id=%s, ip=%s, vhost=%s, app=%s, stream=%s"%(
            req["action"], req["client_id"], req["ip"], req["vhost"], req["app"], req["stream"]
        ))

        # TODO: process the on_play event

        return code

    def __on_stop(self, req):
        code = Error.success

        trace("srs %s: client id=%s, ip=%s, vhost=%s, app=%s, stream=%s"%(
            req["action"], req["client_id"], req["ip"], req["vhost"], req["app"], req["stream"]
        ))

        # TODO: process the on_stop event

        return code
        
# the rest dvrs, when dvr got keyframe, call this api.
class RESTDvrs(object):
    exposed = True
    def __init__(self):
        pass
    # the dvrs POST api specified in following.
    #
    # when dvr got an keyframe, call the hook,
    # the request in the POST data string is a object encode by json:
    #       {
    #           "action": "on_dvr_keyframe",
    #           "vhost": "video.test.com", "app": "live",
    #           "stream": "livestream"
    #       }
    #
    # if valid, the hook must return HTTP code 200(Stauts OK) and response
    # an int value specifies the error code(0 corresponding to success):
    #       0
    def POST(self):
        enable_crossdomain()

        req = cherrypy.request.body.read()
        trace("post to sessions, req=%s"%(req))
        try:
            json_req = json.loads(req)
        except Exception, ex:
            code = Error.system_parse_json
            trace("parse the request to json failed, req=%s, ex=%s, code=%s"%(req, ex, code))
            return str(code)
            
        action = json_req["action"]
        if action == "on_dvr_keyframe":
            code = self.__on_dvr_keyframe(json_req)
        else:
            code = Errors.request_invalid_action
            trace("invalid request action: %s, code=%s"%(json_req["action"], code))
            
        return str(code)
        
    def __on_dvr_keyframe(self, req):
        code = Error.success

        trace("srs %s: vhost=%s, app=%s, stream=%s"%(
            req["action"], req["vhost"], req["app"], req["stream"]
        ))

        # TODO: process the on_dvr_keyframe event

        return code

global_arm_server_id = os.getpid();
class ArmServer:
    def __init__(self):
        global global_arm_server_id
        global_arm_server_id += 1
        
        self.id = str(global_arm_server_id)
        self.ip = None
        self.device_id = None
        
        self.public_ip = cherrypy.request.remote.ip
        self.heartbeat = time.time()
        
        self.clients = 0
    
    def dead(self):
        dead_time_seconds = 20
        if time.time() - self.heartbeat > dead_time_seconds:
            return True
        return False
    
    def json_dump(self):
        data = {}
        data["id"] = self.id
        data["ip"] = self.ip
        data["device_id"] = self.device_id
        data["public_ip"] = self.public_ip
        data["heartbeat"] = self.heartbeat
        data["heartbeat_h"] = time.strftime("%Y-%m-%d %H:%M:%S",time.localtime(self.heartbeat))
        data["summaries"] = "http://%s:1985/api/v1/summaries"%(self.ip)
        return data
        
'''
the server list
'''
class RESTServers(object):
    exposed = True
    
    def __init__(self):
        self.__nodes = []
        
        self.__last_update = datetime.datetime.now();
        
        self.__lock = threading.Lock()
        
    def __get_node(self, device_id):
        for node in self.__nodes:
            if node.device_id == device_id:
                return node
        return None
        
    def __refresh_nodes(self):
        while len(self.__nodes) > 0:
            has_dead_node = False
            for node in self.__nodes:
                if node.dead():
                    self.__nodes.remove(node)
                    has_dead_node = True
            if not has_dead_node:
                break

    def __json_dump_nodes(self, peers):
        data = []
        for node in peers:
            data.append(node.json_dump())
        return data

    def __get_peers_for_play(self, device_id):
        peers = []
        for node in self.__nodes:
            if node.device_id == device_id:
                peers.append(node)
        return peers
        
    def __select_peer(self, peers, device_id):
        target = None
        for peer in peers:
            if target is None or target.clients > peer.clients:
                target = peer
        if target is None:
            return None
        target.clients += 1
        return target.ip

    '''
    post to update server ip.
    request body: the new raspberry-pi server ip. TODO: FIXME: more info.
    '''
    def POST(self):
        enable_crossdomain()
        
        try:
            self.__lock.acquire()

            req = cherrypy.request.body.read()
            trace("post to nodes, req=%s"%(req))
            try:
                json_req = json.loads(req)
            except Exception, ex:
                code = Error.system_parse_json
                trace("parse the request to json failed, req=%s, ex=%s, code=%s"%(req, ex, code))
                return json.dumps({"code":code, "data": None})
                
            device_id = json_req["device_id"]
            node = self.__get_node(device_id)
            if node is None:
                node = ArmServer()
                self.__nodes.append(node)
                
            node.ip = json_req["ip"]
            node.device_id = device_id
            node.public_ip = cherrypy.request.remote.ip
            node.heartbeat = time.time()
            
            return json.dumps({"code":Error.success, "data": {"id":node.id}})
        finally:
            self.__lock.release()
    
    '''
    id canbe:
        pi: the pi demo, raspberry-pi default demo.
            device_id: the id of device to get.
            action: canbe play or mgmt, play to play the inest stream, mgmt to get api/v1/versions.
            stream: the stream to play, for example, live/livestream for http://server:8080/live/livestream.html
        meeting: the meeting demo. jump to web meeting if index is None.
            device_id: the id of device to get.
            local: whether view the local raspberry-pi stream. if "true", redirect to the local(internal) api server.
            index: the meeting stream index, dynamic get the streams from root.api.v1.chats.get_url_by_index(index)
        gslb: the gslb to get edge ip
            device_id: the id of device to get.
        ingest: deprecated, alias for pi.
    '''
    def GET(self, id=None, action="play", stream="live/livestream", index=None, local="false", device_id=None):
        enable_crossdomain()
        
        try:
            self.__lock.acquire()
        
            self.__refresh_nodes()
            data = self.__json_dump_nodes(self.__nodes)
            
            server_ip = "demo.chnvideo.com"
            ip = cherrypy.request.remote.ip
            if type is not None:
                peers = self.__get_peers_for_play(device_id)
                if len(peers) > 0:
                    server_ip = self.__select_peer(peers, device_id)
            
            # demo, srs meeting urls.
            if id == "meeting":
                if index is None:
                    url = "http://%s:8085"%(server_ip)
                elif local == "true":
                    url = "http://%s:8085/api/v1/servers?id=%s&index=%s&local=false"%(server_ip, id, index)
                else:
                    rtmp_url = root.api.v1.chats.get_url_by_index(index)
                    if rtmp_url is None:
                        return "meeting stream not found"
                    urls = rtmp_url.replace("...vhost...", "?vhost=").replace("rtmp://", "").split("/")
                    hls_url = "http://%s:8080/%s/%s.m3u8"%(urls[0].strip(":19350").strip(":1935"), urls[1].split("?")[0], urls[2])
                    return self.__generate_hls(hls_url)
            # raspberry-pi urls.
            elif id == "ingest" or id == "pi":
                if action == "play":
                    url = "http://%s:8080/%s.html"%(server_ip, stream)
                elif action == "rtmp":
                    url = "../../players/srs_player.html?server=%s&vhost=%s&app=%s&stream=%s&autostart=true"%(server_ip, server_ip, stream.split("/")[0], stream.split("/")[1])
                elif action == "hls":
                    hls_url = "http://%s:8080/%s.m3u8"%(server_ip, stream);
                    if stream.startswith("http://"):
                        hls_url = stream;
                    return self.__generate_hls(hls_url.replace(".m3u8.m3u8", ".m3u8"))
                else:
                    url = "http://%s:8080/api/v1/versions"%(server_ip)
            elif id == "gslb":
                return json.dumps({"code":Error.success, "data": {
                    "edge":server_ip, "client":ip, 
                    "peers":self.__json_dump_nodes(peers),
                    "streams": {
                        "pi": {
                            "livestream": {
                                "sales-pi-hls": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-sales-arm&stream=live/livestream",
                                "dev-pi-hls": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-dev-arm&stream=live/livestream"
                            },
                            "cztv": {
                                "sales-pi-hls": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-sales-arm&stream=live/rtmp_cztv01-sd",
                                "dev-pi-hls": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-dev-arm&stream=live/rtmp_cztv01-sd"
                            }
                        },
                        "hiwifi": {
                            "hls": {
                                "dev-livestream": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-dev-hiwifi&stream=live/livestream",
                                "sales-livestream": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-sales-hiwifi&stream=live/livestream"
                            },
                            "rtmp":{
                                "dev-livestream": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-dev-hiwifi&stream=live/livestream",
                                "sales-livestream": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-sales-hiwifi&stream=live/livestream"
                            },
                            "meiyi": {
                                "rtmp": {
                                    "avatar": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-sales-hiwifi&stream=live/avatar",
                                    "MenInBlack3": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-sales-hiwifi&stream=live/MenInBlack3",
                                    "skyfall": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-sales-hiwifi&stream=live/skyfall",
                                    "SpiderMan": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-sales-hiwifi&stream=live/SpiderMan",
                                    "thehobbit": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-sales-hiwifi&stream=live/thehobbit",
                                    "thorthedarkworld": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-sales-hiwifi&stream=live/thorthedarkworld",
                                    "transformers": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-sales-hiwifi&stream=live/transformers"
                                },
                                "hls": {
                                    "avatar": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-sales-hiwifi&stream=live/avatar",
                                    "MenInBlack3": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-sales-hiwifi&stream=live/MenInBlack3",
                                    "skyfall": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-sales-hiwifi&stream=live/skyfall",
                                    "SpiderMan": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-sales-hiwifi&stream=live/SpiderMan",
                                    "thehobbit": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-sales-hiwifi&stream=live/thehobbit",
                                    "thorthedarkworld": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-sales-hiwifi&stream=live/thorthedarkworld",
                                    "transformers": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-sales-hiwifi&stream=live/transformers"
                                }
                            }
                        },
                        "cubieboard": {
                            "meiyi": {
                                "rtmp": {
                                    "livesteam": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-meiyi-cubieboard&stream=live/livestream",
                                    "stream1": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-meiyi-cubieboard&stream=live/stream1",
                                    "stream2": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-meiyi-cubieboard&stream=live/stream2",
                                    "stream3": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-meiyi-cubieboard&stream=live/stream3",
                                    "stream4": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-meiyi-cubieboard&stream=live/stream4",
                                    "stream5": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-meiyi-cubieboard&stream=live/stream5",
                                    "stream6": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-meiyi-cubieboard&stream=live/stream6",
                                    "stream7": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-meiyi-cubieboard&stream=live/stream7"
                                },
                                "hls": {
                                    "livesteam": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-meiyi-cubieboard&stream=live/livestream",
                                    "stream1": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-meiyi-cubieboard&stream=live/stream1",
                                    "stream2": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-meiyi-cubieboard&stream=live/stream2",
                                    "stream3": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-meiyi-cubieboard&stream=live/stream3",
                                    "stream4": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-meiyi-cubieboard&stream=live/stream4",
                                    "stream5": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-meiyi-cubieboard&stream=live/stream5",
                                    "stream6": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-meiyi-cubieboard&stream=live/stream6",
                                    "stream7": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-meiyi-cubieboard&stream=live/stream7"
                                }
                            },
                            "meiyi-bk": {
                                "rtmp": {
                                    "livesteam": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-meiyi-cubieboard-bk&stream=live/livestream",
                                    "stream1": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-meiyi-cubieboard-bk&stream=live/stream1",
                                    "stream2": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-meiyi-cubieboard-bk&stream=live/stream2",
                                    "stream3": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-meiyi-cubieboard-bk&stream=live/stream3",
                                    "stream4": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-meiyi-cubieboard-bk&stream=live/stream4",
                                    "stream5": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-meiyi-cubieboard-bk&stream=live/stream5",
                                    "stream6": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-meiyi-cubieboard-bk&stream=live/stream6",
                                    "stream7": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-meiyi-cubieboard-bk&stream=live/stream7"
                                },
                                "hls": {
                                    "livesteam": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-meiyi-cubieboard-bk&stream=live/livestream",
                                    "stream1": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-meiyi-cubieboard-bk&stream=live/stream1",
                                    "stream2": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-meiyi-cubieboard-bk&stream=live/stream2",
                                    "stream3": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-meiyi-cubieboard-bk&stream=live/stream3",
                                    "stream4": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-meiyi-cubieboard-bk&stream=live/stream4",
                                    "stream5": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-meiyi-cubieboard-bk&stream=live/stream5",
                                    "stream6": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-meiyi-cubieboard-bk&stream=live/stream6",
                                    "stream7": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-meiyi-cubieboard-bk&stream=live/stream7"
                                }
                            },
                            "meiyi-dev1": {
                                "rtmp": {
                                    "livesteam": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-meiyi-cubieboard-dev1&stream=live/livestream"
                                },
                                "hls": {
                                    "livesteam": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-meiyi-cubieboard-dev1&stream=live/livestream"
                                }
                            },
                            "meiyi-dev2": {
                                "rtmp": {
                                    "livesteam": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=rtmp&device_id=chnvideo-meiyi-cubieboard-dev2&stream=live/livestream"
                                },
                                "hls": {
                                    "livesteam": "http://demo.chnvideo.com:8085/api/v1/servers?id=ingest&action=hls&device_id=chnvideo-meiyi-cubieboard-dev2&stream=live/livestream"
                                }
                            }
                        }
                    }
                }})
            # others, default.
            else:
                return json.dumps(data)
            #return "id=%s, action=%s, stream=%s, url=%s, index=%s, local=%s"%(id, action, stream, url, index, local)
            raise cherrypy.HTTPRedirect(url)
        finally:
            self.__lock.release()

    def DELETE(self, id):
        enable_crossdomain()
        raise cherrypy.HTTPError(405, "Not allowed.")

    def PUT(self, id):
        enable_crossdomain()
        raise cherrypy.HTTPError(405, "Not allowed.")

    def OPTIONS(self, *args, **kwargs):
        enable_crossdomain()
    
    def __generate_hls(self, hls_url):
        return SrsUtility().hls_html(hls_url)

class SrsUtility:
    def hls_html(self, hls_url):
        return """
<h1>%s</h1>
<video width="640" height="360"
        autoplay controls autobuffer 
        src="%s"
        type="application/vnd.apple.mpegurl">
</video>"""%(hls_url, hls_url);

global_cdn_id = os.getpid();
class CdnNode:
    def __init__(self):
        global global_cdn_id
        global_cdn_id += 1
        
        self.id = str(global_cdn_id)
        self.ip = None
        self.origin = None
        self.os = None
        self.srs_status = None
        
        self.public_ip = cherrypy.request.remote.ip
        self.heartbeat = time.time()
        
        self.clients = 0
    
    def dead(self):
        dead_time_seconds = 10
        if time.time() - self.heartbeat > dead_time_seconds:
            return True
        return False
    
    def json_dump(self):
        data = {}
        data["id"] = self.id
        data["ip"] = self.ip
        data["origin"] = self.origin
        data["os"] = self.os
        data["srs_status"] = self.srs_status
        data["public_ip"] = self.public_ip
        data["heartbeat"] = self.heartbeat
        data["heartbeat_h"] = time.strftime("%Y-%m-%d %H:%M:%S",time.localtime(self.heartbeat))
        data["clients"] = self.clients
        data["summaries"] = "http://%s:1985/api/v1/summaries"%(self.ip)
        return data
        
'''
the cdn nodes list
'''
class RESTNodes(object):
    exposed = True
    
    def __init__(self):
        self.__nodes = []
        # @remark, if there is shared data, such as the self.__nodes,
        # we must use lock for cherrypy, or the cpu of cherrypy will high
        # and performance suffer.
        self.__lock = threading.Lock()
        
    def __get_node(self, id):
        for node in self.__nodes:
            if node.id == id:
                return node
        return None
        
    def __refresh_nodes(self):
        while len(self.__nodes) > 0:
            has_dead_node = False
            for node in self.__nodes:
                if node.dead():
                    self.__nodes.remove(node)
                    has_dead_node = True
            if not has_dead_node:
                break
    
    def __get_peers(self, target_node):
        peers = []
        for node in self.__nodes:
            if str(node.id).strip() == str(target_node.id).strip():
                continue
            if node.public_ip == target_node.public_ip and node.srs_status == "running" and node.origin != target_node.ip:
                peers.append(node)
        return peers
        
    def __get_peers_for_play(self, ip):
        peers = []
        for node in self.__nodes:
            if node.public_ip == ip and node.srs_status == "running":
                peers.append(node)
        return peers

    def __json_dump_nodes(self, peers):
        data = []
        for node in peers:
            data.append(node.json_dump())
        return data
        
    def __select_peer(self, peers, ip):
        target = None
        for peer in peers:
            if target is None or target.clients > peer.clients:
                target = peer
        if target is None:
            return None
        target.clients += 1
        return target.ip
    
    def GET(self, type=None, format=None, origin=None, vhost=None, port=None, stream=None, node_id=None):
        enable_crossdomain()
        
        try:
            self.__lock.acquire()
        
            self.__refresh_nodes()
            data = self.__json_dump_nodes(self.__nodes)
            
            ip = cherrypy.request.remote.ip
            if type is not None:
                server = origin
                peers = self.__get_peers_for_play(ip)
                if len(peers) > 0:
                    server = self.__select_peer(peers, ip)
                if type == "hls":
                    hls_url = "http://%s:%s/%s.m3u8"%(server, port, stream)
                    hls_url = hls_url.replace(".m3u8.m3u8", ".m3u8")
                    if format == "html":
                        return SrsUtility().hls_html(hls_url)
                    else:
                        #return hls_url
                        raise cherrypy.HTTPRedirect(hls_url)
                elif type == "rtmp":
                    rtmp_url = "rtmp://%s:%s/%s?vhost=%s/%s"%(server, port, stream.split("/")[0], vhost, stream.split("/")[1])
                    if format == "html":
                        html = "%s?server=%s&port=%s&vhost=%s&app=%s&stream=%s&autostart=true"%(
                            "http://demo.chnvideo.com:8085/srs/trunk/research/players/srs_player.html",
                            server, port, vhost, stream.split("/")[0], stream.split("/")[1])
                        #return html
                        raise cherrypy.HTTPRedirect(html)
                    return rtmp_url
                elif type == "gslb":
                    return json.dumps({"code":Error.success, "data": {
                        "edge":server, "client":ip, 
                        "peers":self.__json_dump_nodes(peers),
                        "streams": {
                            "cztv": {
                                "hls": "http://demo.chnvideo.com:8085/api/v1/nodes?type=hls&format=html&origin=demo.chnvideo.com&port=8080&stream=live/rtmp_cztv01-sd",
                                "rtmp": "http://demo.chnvideo.com:8085/api/v1/nodes?type=rtmp&format=html&origin=demo.chnvideo.com&vhost=android&port=1935&stream=live/rtmp_cztv01-sd"
                            },
                            "livestream": {
                                "hls": "http://demo.chnvideo.com:8085/api/v1/nodes?type=hls&format=html&origin=demo.chnvideo.com&port=8080&stream=live/livestream",
                                "rtmp": "http://demo.chnvideo.com:8085/api/v1/nodes?type=rtmp&format=html&origin=demo.chnvideo.com&vhost=demo.srs.com&port=1935&stream=live/livestream"
                            },
                            "apk": "http://demo.chnvideo.com/android.srs.apk"
                        }
                    }})
            
            return json.dumps({"code":Error.success, "data": data})
        finally:
            self.__lock.release()

    def PUT(self):
        enable_crossdomain()
        
        try:
            self.__lock.acquire()

            req = cherrypy.request.body.read()
            trace("put to nodes, req=%s"%(req))
            try:
                json_req = json.loads(req)
            except Exception, ex:
                code = Error.system_parse_json
                trace("parse the request to json failed, req=%s, ex=%s, code=%s"%(req, ex, code))
                return json.dumps({"code":code, "data": None})
            
            id = str(json_req["id"])
            node = self.__get_node(id)
            if node is None:
                code = Error.cdn_node_not_exists
                trace("cdn node not exists, req=%s, id=%s, code=%s"%(req, id, code))
                return json.dumps({"code":code, "data": None})
            
            node.heartbeat = time.time()
            node.srs_status = str(json_req["srs_status"])
            node.ip = str(json_req["ip"])
            if "origin" in json_req:
                node.origin = str(json_req["origin"]);
            node.public_ip = cherrypy.request.remote.ip
            # reset if restart.
            if node.srs_status != "running":
                node.clients = 0
                
            self.__refresh_nodes()
            peers = self.__get_peers(node)
            peers_data = self.__json_dump_nodes(peers)
                
            res = json.dumps({"code":Error.success, "data": {"id":node.id, "peers":peers_data}})
            trace(res)
            return res
        finally:
            self.__lock.release()

    def POST(self):
        enable_crossdomain()
        
        try:
            self.__lock.acquire()

            req = cherrypy.request.body.read()
            trace("post to nodes, req=%s"%(req))
            try:
                json_req = json.loads(req)
            except Exception, ex:
                code = Error.system_parse_json
                trace("parse the request to json failed, req=%s, ex=%s, code=%s"%(req, ex, code))
                return json.dumps({"code":code, "data": None})
                
            node = CdnNode()
            node.ip = str(json_req["ip"]);
            node.os = str(json_req["os"]);
            if "origin" in json_req:
                node.origin = str(json_req["origin"]);
            node.srs_status = str(json_req["srs_status"])
            self.__nodes.append(node)
                
            self.__refresh_nodes()
            peers = self.__get_peers(node)
            peers_data = self.__json_dump_nodes(peers)
            
            res = json.dumps({"code":Error.success, "data": {"id":node.id, "peers":peers_data}})
            trace(res)
            return res
        finally:
            self.__lock.release()
        
    def OPTIONS(self, *args, **kwargs):
        enable_crossdomain()

global_chat_id = os.getpid();
'''
the chat streams, public chat room.
'''
class RESTChats(object):
    exposed = True
    global_id = 100
    
    def __init__(self):
        # object fields:
        # id: an int value indicates the id of user.
        # username: a str indicates the user name.
        # url: a str indicates the url of user stream.
        # agent: a str indicates the agent of user.
        # join_date: a number indicates the join timestamp in seconds.
        # join_date_str: a str specifies the formated friendly time.
        # heatbeat: a number indicates the heartbeat timestamp in seconds.
        # vcodec: a dict indicates the video codec info.
        # acodec: a dict indicates the audio codec info.
        self.__chats = [];
        self.__chat_lock = threading.Lock();

        # dead time in seconds, if exceed, remove the chat.
        self.__dead_time = 15;
    
    '''
    get the rtmp url of chat object. None if overflow.
    '''
    def get_url_by_index(self, index):
        index = int(index)
        if index is None or index >= len(self.__chats):
            return None;
        return self.__chats[index]["url"];

    def GET(self):
        enable_crossdomain()

        try:
            self.__chat_lock.acquire();

            chats = [];
            copy = self.__chats[:];
            for chat in copy:
                if time.time() - chat["heartbeat"] > self.__dead_time:
                    self.__chats.remove(chat);
                    continue;

                chats.append({
                    "id": chat["id"],
                    "username": chat["username"],
                    "url": chat["url"],
                    "join_date_str": chat["join_date_str"],
                    "heartbeat": chat["heartbeat"],
                });
        finally:
            self.__chat_lock.release();
            
        return json.dumps({"code":0, "data": {"now": time.time(), "chats": chats}})
        
    def POST(self):
        enable_crossdomain()
        
        req = cherrypy.request.body.read()
        chat = json.loads(req)

        global global_chat_id;
        chat["id"] = global_chat_id
        global_chat_id += 1

        chat["join_date"] = time.time();
        chat["heartbeat"] = time.time();
        chat["join_date_str"] = time.strftime("%Y-%m-%d %H:%M:%S");

        try:
            self.__chat_lock.acquire();

            self.__chats.append(chat)
        finally:
            self.__chat_lock.release();

        trace("create chat success, id=%s"%(chat["id"]))
        
        return json.dumps({"code":0, "data": chat["id"]})

    def DELETE(self, id):
        enable_crossdomain()

        try:
            self.__chat_lock.acquire();

            for chat in self.__chats:
                if str(id) != str(chat["id"]):
                    continue

                self.__chats.remove(chat)
                trace("delete chat success, id=%s"%(id))

                return json.dumps({"code":0, "data": None})
        finally:
            self.__chat_lock.release();

        raise cherrypy.HTTPError(405, "Not allowed.")

    def PUT(self, id):
        enable_crossdomain()

        try:
            self.__chat_lock.acquire();

            for chat in self.__chats:
                if str(id) != str(chat["id"]):
                    continue

                chat["heartbeat"] = time.time();
                trace("heartbeat chat success, id=%s"%(id))

                return json.dumps({"code":0, "data": None})
        finally:
            self.__chat_lock.release();

        raise cherrypy.HTTPError(405, "Not allowed.")

    def OPTIONS(self, *args, **kwargs):
        enable_crossdomain()

# HTTP RESTful path.
class Root(object):
    exposed = True

    def __init__(self):
        self.api = Api()
    def GET(self):
        enable_crossdomain();
        return json.dumps({"code":Error.success, "urls":{"api":"the api root"}})
    def OPTIONS(self, *args, **kwargs):
        enable_crossdomain();
# HTTP RESTful path.
class Api(object):
    exposed = True

    def __init__(self):
        self.v1 = V1()
    def GET(self):
        enable_crossdomain();
        return json.dumps({"code":Error.success, 
            "urls": {
                "v1": "the api version 1.0"
            }
        });
    def OPTIONS(self, *args, **kwargs):
        enable_crossdomain();
# HTTP RESTful path. to access as:
#   http://127.0.0.1:8085/api/v1/clients
class V1(object):
    exposed = True

    def __init__(self):
        self.clients = RESTClients()
        self.streams = RESTStreams()
        self.sessions = RESTSessions()
        self.dvrs = RESTDvrs()
        self.chats = RESTChats()
        self.servers = RESTServers()
        self.nodes = RESTNodes()
    def GET(self):
        enable_crossdomain();
        return json.dumps({"code":Error.success, "urls":{
            "clients": "for srs http callback, to handle the clients requests: connect/disconnect vhost/app.", 
            "streams": "for srs http callback, to handle the streams requests: publish/unpublish stream.",
            "sessions": "for srs http callback, to handle the sessions requests: client play/stop stream",
            "chats": "for srs demo meeting, the chat streams, public chat room.",
            "nodes": {
                "summary": "for srs cdn node",
                "POST ip=node_ip&os=node_os": "register a new node",
                "GET": "get the active edge nodes",
                "GET type=gslb&origin=demo.chnvideo.com": "get the gslb edge ip",
                "GET type=hls&format=html&origin=demo.chnvideo.com&port=8080&stream=live/livestream": "get the play url, html for hls",
                "GET type=rtmp&format=html&origin=demo.chnvideo.com&vhost=demo.srs.com&port=1935&stream=live/livestream": "get the play url, for rtmp"
            },
            "servers": {
                "summary": "for srs raspberry-pi and meeting demo",
                "GET": "get the current raspberry-pi servers info",
                "GET id=gslb&device_id=chnvideo-sales-arm": "get the gslb edge ip",
                "POST ip=node_ip&device_id=device_id": "the new raspberry-pi server info.",
                "GET id=ingest&action=play&stream=live/livestream": "play the ingest HLS stream on raspberry-pi",
                "GET id=ingest&action=rtmp&stream=live/livestream": "play the ingest RTMP stream on raspberry-pi",
                "GET id=ingest&action=hls&stream=live/livestream": "play the ingest HLS stream on raspberry-pi",
                "GET id=ingest&action=mgmt": "open the HTTP api url of raspberry-pi",
                "GET id=meeting": "redirect to local raspberry-pi meeting url(local ignored)",
                "GET id=meeting&local=false&index=0": "play the first(index=0) meeting HLS stream on demo.chnvideo.com(not local)",
                "GET id=meeting&local=true&index=0": "play the first(index=0) meeting HLS stream on local server(local x86/x64 server), warn: raspberry-pi donot support HLS meeting."
            }
        }});
    def OPTIONS(self, *args, **kwargs):
        enable_crossdomain();

'''
main code start.
'''
# donot support use this module as library.
if __name__ != "__main__":
    raise Exception("embed not support")

# check the user options
if len(sys.argv) <= 1:
    print "SRS api callback server, Copyright (c) 2013-2014 winlin"
    print "Usage: python %s <port>"%(sys.argv[0])
    print "    port: the port to listen at."
    print "For example:"
    print "    python %s 8085"%(sys.argv[0])
    print ""
    print "See also: https://github.com/winlinvip/simple-rtmp-server"
    sys.exit(1)

# parse port from user options.
port = int(sys.argv[1])
static_dir = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), "static-dir"))
trace("api server listen at port: %s, static_dir: %s"%(port, static_dir))

# cherrypy config.
conf = {
    'global': {
        'server.shutdown_timeout': 1,
        'server.socket_host': '0.0.0.0',
        'server.socket_port': port,
        'tools.encode.on': True,
        'tools.staticdir.on': True,
        'tools.encode.encoding': "utf-8",
        #'server.thread_pool': 2, # single thread server.
    },
    '/': {
        'tools.staticdir.dir': static_dir,
        'tools.staticdir.index': "index.html",
        # for cherrypy RESTful api support
        'request.dispatch': cherrypy.dispatch.MethodDispatcher()
    }
}

# start cherrypy web engine
trace("start cherrypy server")
root = Root()
cherrypy.quickstart(root, '/', conf)

