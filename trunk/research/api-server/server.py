#!/usr/bin/python
'''
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

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

import os, json, time, datetime, cherrypy, threading, urllib2, shlex, subprocess
import cherrypy.process.plugins

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
                  "tcUrl": "rtmp://video.test.com/live?key=d2fa801d08e3f90ed1e1670e6e52651a",
                  "pageUrl": "http://www.test.com/live.html"
              }
    on_close:
        when client close/disconnect to vhost/app/stream, call the hook,
        the request in the POST data string is a object encode by json:
              {
                  "action": "on_close",
                  "client_id": 1985,
                  "ip": "192.168.1.10", "vhost": "video.test.com", "app": "live",
                  "send_bytes": 10240, "recv_bytes": 10240
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

        trace("srs %s: client id=%s, ip=%s, vhost=%s, app=%s, tcUrl=%s, pageUrl=%s"%(
            req["action"], req["client_id"], req["ip"], req["vhost"], req["app"], req["tcUrl"], req["pageUrl"]
        ))

        # TODO: process the on_connect event

        return code

    def __on_close(self, req):
        code = Error.success

        trace("srs %s: client id=%s, ip=%s, vhost=%s, app=%s, send_bytes=%s, recv_bytes=%s"%(
            req["action"], req["client_id"], req["ip"], req["vhost"], req["app"], req["send_bytes"], req["recv_bytes"]
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
handle the dvrs requests: dvr stream.
'''
class RESTDvrs(object):
    exposed = True

    def GET(self):
        enable_crossdomain()

        dvrs = {}
        return json.dumps(dvrs)

    '''
    for SRS hook: on_dvr
    on_dvr:
        when srs reap a dvr file, call the hook,
        the request in the POST data string is a object encode by json:
              {
                  "action": "on_dvr",
                  "client_id": 1985,
                  "ip": "192.168.1.10", "vhost": "video.test.com", "app": "live",
                  "stream": "livestream",
                  "cwd": "/usr/local/srs",
                  "file": "./objs/nginx/html/live/livestream.1420254068776.flv"
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
        trace("post to dvrs, req=%s"%(req))
        try:
            json_req = json.loads(req)
        except Exception, ex:
            code = Error.system_parse_json
            trace("parse the request to json failed, req=%s, ex=%s, code=%s"%(req, ex, code))
            return str(code)

        action = json_req["action"]
        if action == "on_dvr":
            code = self.__on_dvr(json_req)
        else:
            trace("invalid request action: %s"%(json_req["action"]))
            code = Error.request_invalid_action

        return str(code)

    def OPTIONS(self, *args, **kwargs):
        enable_crossdomain()

    def __on_dvr(self, req):
        code = Error.success

        trace("srs %s: client id=%s, ip=%s, vhost=%s, app=%s, stream=%s, cwd=%s, file=%s"%(
            req["action"], req["client_id"], req["ip"], req["vhost"], req["app"], req["stream"],
            req["cwd"], req["file"]
        ))

        # TODO: process the on_dvr event

        return code


'''
handle the hls proxy requests: hls stream.
'''
class RESTProxy(object):
    exposed = True

    '''
    for SRS hook: on_hls_notify
    on_hls_notify:
        when srs reap a ts file of hls, call this hook,
        used to push file to cdn network, by get the ts file from cdn network.
        so we use HTTP GET and use the variable following:
              [app], replace with the app.
              [stream], replace with the stream.
              [ts_url], replace with the ts url.
        ignore any return data of server.
    '''
    def GET(self, *args, **kwargs):
        enable_crossdomain()
        
        url = "http://" + "/".join(args);
        print "start to proxy url: %s"%url
        
        f = None
        try:
            f = urllib2.urlopen(url)
            f.read()
        except:
            print "error proxy url: %s"%url
        finally:
            if f: f.close()
            print "completed proxy url: %s"%url
        return url

'''
handle the hls requests: hls stream.
'''
class RESTHls(object):
    exposed = True

    '''
    for SRS hook: on_hls_notify
    on_hls_notify:
        when srs reap a ts file of hls, call this hook,
        used to push file to cdn network, by get the ts file from cdn network.
        so we use HTTP GET and use the variable following:
              [app], replace with the app.
              [stream], replace with the stream.
              [ts_url], replace with the ts url.
        ignore any return data of server.
    '''
    def GET(self, *args, **kwargs):
        enable_crossdomain()

        hls = {
            "args": args,
            "kwargs": kwargs
        }
        return json.dumps(hls)

    '''
    for SRS hook: on_hls
    on_hls:
        when srs reap a dvr file, call the hook,
        the request in the POST data string is a object encode by json:
              {
                  "action": "on_dvr",
                  "client_id": 1985,
                  "ip": "192.168.1.10", 
                  "vhost": "video.test.com", 
                  "app": "live",
                  "stream": "livestream",
                  "duration": 9.68, // in seconds
                  "cwd": "/usr/local/srs",
                  "file": "./objs/nginx/html/live/livestream.1420254068776-100.ts",
                  "seq_no": 100
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
        trace("post to hls, req=%s"%(req))
        try:
            json_req = json.loads(req)
        except Exception, ex:
            code = Error.system_parse_json
            trace("parse the request to json failed, req=%s, ex=%s, code=%s"%(req, ex, code))
            return str(code)

        action = json_req["action"]
        if action == "on_hls":
            code = self.__on_hls(json_req)
        else:
            trace("invalid request action: %s"%(json_req["action"]))
            code = Error.request_invalid_action

        return str(code)

    def OPTIONS(self, *args, **kwargs):
        enable_crossdomain()

    def __on_hls(self, req):
        code = Error.success

        trace("srs %s: client id=%s, ip=%s, vhost=%s, app=%s, stream=%s, duration=%s, cwd=%s, file=%s, seq_no=%s"%(
            req["action"], req["client_id"], req["ip"], req["vhost"], req["app"], req["stream"], req["duration"],
            req["cwd"], req["file"], req["seq_no"]
        ))

        # TODO: process the on_hls event

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
                  "stream": "livestream",
                  "pageUrl": "http://www.test.com/live.html"
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

        trace("srs %s: client id=%s, ip=%s, vhost=%s, app=%s, stream=%s, pageUrl=%s"%(
            req["action"], req["client_id"], req["ip"], req["vhost"], req["app"], req["stream"], req["pageUrl"]
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

global_arm_server_id = os.getpid();
class ArmServer:
    def __init__(self):
        global global_arm_server_id
        global_arm_server_id += 1
        
        self.id = str(global_arm_server_id)
        self.ip = None
        self.device_id = None
        self.summaries = None
        
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
        data["summaries"] = self.summaries
        data["public_ip"] = self.public_ip
        data["heartbeat"] = self.heartbeat
        data["heartbeat_h"] = time.strftime("%Y-%m-%d %H:%M:%S",time.localtime(self.heartbeat))
        data["api"] = "http://%s:1985/api/v1/summaries"%(self.ip)
        data["console"] = "http://ossrs.net/console/ng_index.html#/summaries?host=%s&port=1985"%(self.ip)
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
            if "summaries" in json_req:
                node.summaries = json_req["summaries"]
            node.device_id = device_id
            node.public_ip = cherrypy.request.remote.ip
            node.heartbeat = time.time()
            
            return json.dumps({"code":Error.success, "data": {"id":node.id}})
        finally:
            self.__lock.release()
    
    '''
    get all servers which report to this api-server.
    '''
    def GET(self, id=None):
        enable_crossdomain()
        
        try:
            self.__lock.acquire()
        
            self.__refresh_nodes()
            
            data = []
            for node in self.__nodes:
                if id == None or node.id == str(id) or node.device_id == str(id):
                    data.append(node.json_dump())
            
            return json.dumps(data)
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

'''
the snapshot api,
to start a snapshot when encoder start publish stream,
stop the snapshot worker when stream finished.
'''
class RESTSnapshots(object):
    exposed = True
    
    def __init__(self):
        pass

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
            code = worker.snapshot_create(json_req)
        elif action == "on_unpublish":
            code = worker.snapshot_destroy(json_req)
        else:
            trace("invalid request action: %s"%(json_req["action"]))
            code = Error.request_invalid_action

        return str(code)

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
        self.hls = RESTHls()
        self.proxy = RESTProxy()
        self.chats = RESTChats()
        self.servers = RESTServers()
        self.snapshots = RESTSnapshots()
    def GET(self):
        enable_crossdomain();
        return json.dumps({"code":Error.success, "urls":{
            "clients": "for srs http callback, to handle the clients requests: connect/disconnect vhost/app.", 
            "streams": "for srs http callback, to handle the streams requests: publish/unpublish stream.",
            "sessions": "for srs http callback, to handle the sessions requests: client play/stop stream",
            "dvrs": "for srs http callback, to handle the dvr requests: dvr stream.",
            "chats": "for srs demo meeting, the chat streams, public chat room.",
            "servers": {
                "summary": "for srs raspberry-pi and meeting demo",
                "GET": "get the current raspberry-pi servers info",
                "POST ip=node_ip&device_id=device_id": "the new raspberry-pi server info."
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
    print "SRS api callback server, Copyright (c) 2013-2015 SRS(simple-rtmp-server)"
    print "Usage: python %s <port>"%(sys.argv[0])
    print "    port: the port to listen at."
    print "For example:"
    print "    python %s 8085"%(sys.argv[0])
    print ""
    print "See also: https://github.com/simple-rtmp-server/srs"
    sys.exit(1)

# parse port from user options.
port = int(sys.argv[1])
static_dir = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), "static-dir"))
trace("api server listen at port: %s, static_dir: %s"%(port, static_dir))


discard = open("/dev/null", "rw")
'''
create process by specifies command.
@param command the command str to start the process.
@param stdout_fd an int fd specifies the stdout fd.
@param stderr_fd an int fd specifies the stderr fd.
@param log_file a file object specifies the additional log to write to. ignore if None.
@return a Popen object created by subprocess.Popen().
'''
def create_process(command, stdout_fd, stderr_fd):
    # log the original command
    msg = "process start command: %s"%(command);

    # to avoid shell injection, directly use the command, no need to filter.
    args = shlex.split(str(command));
    process = subprocess.Popen(args, stdout=stdout_fd, stderr=stderr_fd);

    return process;
'''
isolate thread for srs worker, to do some job in background,
for example, to snapshot thumbnail of RTMP stream.
'''
class SrsWorker(cherrypy.process.plugins.SimplePlugin):
    def __init__(self, bus):
        cherrypy.process.plugins.SimplePlugin.__init__(self, bus);
        self.__snapshots = {}

    def start(self):
        print "srs worker thread started"

    def stop(self):
        print "srs worker thread stopped"

    def main(self):
        for url in self.__snapshots:
            snapshot = self.__snapshots[url]
            
            diff = time.time() - snapshot['timestamp']
            process = snapshot['process']
            
            # aborted.
            if process is not None and snapshot['abort']:
                process.kill()
                process.poll()
                del self.__snapshots[url]
                print 'abort snapshot %s'%snapshot['cmd']
                break

            # how many snapshots to output.
            vframes = 5
            # the expire in seconds for ffmpeg to snapshot.
            expire = 1
            # the timeout to kill ffmpeg.
            kill_ffmpeg_timeout = 30 * expire
            # the ffmpeg binary path
            ffmpeg = "./objs/ffmpeg/bin/ffmpeg"
            # the best url for thumbnail.
            besturl = os.path.join(static_dir, "%s/%s-best.png"%(snapshot['app'], snapshot['stream']))
            # the lambda to generate the thumbnail with index.
            lgo = lambda dir, app, stream, index: os.path.join(dir, "%s/%s-%03d.png"%(app, stream, index))
            # the output for snapshot command
            output = os.path.join(static_dir, "%s/%s-%%03d.png"%(snapshot['app'], snapshot['stream']))
            # the ffmepg command to snapshot
            cmd = '%s -i %s -vf fps=1 -vcodec png -f image2 -an -y -vframes %s -y %s'%(ffmpeg, url, vframes, output)
            
            # already snapshoted and not expired.
            if process is not None and diff < expire:
                continue
            
            # terminate the active process
            if process is not None:
                # the poll will set the process.returncode
                process.poll()

                # None incidates the process hasn't terminate yet.
                if process.returncode is not None:
                    # process terminated with error.
                    if process.returncode != 0:
                        print 'process terminated with error=%s, cmd=%s'%(process.returncode, snapshot['cmd'])
                    # process terminated normally.
                    else:
                        # guess the best one.
                        bestsize = 0
                        for i in range(0, vframes):
                            output = lgo(static_dir, snapshot['app'], snapshot['stream'], i + 1)
                            fsize = os.path.getsize(output)
                            if bestsize < fsize:
                                os.system("rm -f '%s'"%besturl)
                                os.system("ln -sf '%s' '%s'"%(output, besturl))
                                bestsize = fsize
                        print 'the best thumbnail is %s'%besturl
                else:
                    # wait for process to terminate, timeout is N*expire.
                    if diff < kill_ffmpeg_timeout:
                        continue
                    # kill the process when user cancel.
                    else:
                        process.kill()
                        print 'kill the process %s'%snapshot['cmd']
                
            # create new process to snapshot.
            print 'snapshot by: %s'%cmd
            
            process = create_process(cmd, discard.fileno(), discard.fileno())
            snapshot['process'] = process
            snapshot['cmd'] = cmd
            snapshot['timestamp'] = time.time()
        pass;
        
    # {"action":"on_publish","client_id":108,"ip":"127.0.0.1","vhost":"__defaultVhost__","app":"live","stream":"livestream"}
    # ffmpeg -i rtmp://127.0.0.1:1935/live?vhost=dev/stream -vf fps=1 -vcodec png -f image2 -an -y -vframes 3 -y static-dir/live/livestream-%03d.png
    def snapshot_create(self, req):
        url = "rtmp://127.0.0.1/%s...vhost...%s/%s"%(req['app'], req['vhost'], req['stream'])
        if url in self.__snapshots:
            print 'ignore exists %s'%url
            return Error.success
            
        req['process'] = None
        req['abort'] = False
        req['timestamp'] = time.time()
        self.__snapshots[url] = req
        return Error.success
        
    # {"action":"on_unpublish","client_id":108,"ip":"127.0.0.1","vhost":"__defaultVhost__","app":"live","stream":"livestream"}
    def snapshot_destroy(self, req):
        url = "rtmp://127.0.0.1/%s...vhost...%s/%s"%(req['app'], req['vhost'], req['stream'])
        if url in self.__snapshots:
            snapshot = self.__snapshots[url]
            snapshot['abort'] = True
        return Error.success

# subscribe the plugin to cherrypy.
worker = SrsWorker(cherrypy.engine)
worker.subscribe();

# disable the autoreloader to make it more simple.
cherrypy.engine.autoreload.unsubscribe();

# cherrypy config.
conf = {
    'global': {
        'server.shutdown_timeout': 3,
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

