#!/usr/bin/python
'''
The MIT License (MIT)

Copyright (c) 2013 winlin

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

import sys;
# reload sys model to enable the getdefaultencoding method.
reload(sys);
# set the default encoding to utf-8
# using exec to set the encoding, to avoid error in IDE.
exec("sys.setdefaultencoding('utf-8')");
assert sys.getdefaultencoding().lower() == "utf-8";

import json, datetime, cherrypy

# simple log functions.
def trace(msg):
    date = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print "[%s][trace] %s"%(date, msg)

# enable crossdomain access for js-client
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

'''
handle the clients requests:
POST: create new client, handle the SRS on_connect callback.
'''
class RESTClients(object):
    exposed = True

    def GET(self):
        enable_crossdomain();

        clients = {};
        return json.dumps(clients);

    # for SRS hook: on_connect
    def POST(self):
        enable_crossdomain();

        req = cherrypy.request.body.read();
        trace("post to clients, req=%s"%(req));
        try:
            json_req = json.loads(req)
        except Exception, ex:
            trace("parse the request to json failed, req=%s, ex=%s"%(req, ex))
            return str(Error.system_parse_json);

        trace("valid clients post request success.")
        return str(Error.success);

    def OPTIONS(self):
        enable_crossdomain()

# HTTP RESTful path.
class Root(object):
    def __init__(self):
        self.api = Api()
# HTTP RESTful path.
class Api(object):
    def __init__(self):
        self.v1 = V1()
# HTTP RESTful path. to access as:
#   http://127.0.0.1:8085/api/v1/clients
class V1(object):
    def __init__(self):
        self.clients = RESTClients()

'''
main code start.
'''
# donot support use this module as library.
if __name__ != "__main__":
    raise Exception("embed not support");

# check the user options
if len(sys.argv) <= 1:
    print "SRS api callback server, Copyright (c) 2013 winlin"
    print "Usage: python %s <port>"%(sys.argv[0])
    print "    port: the port to listen at."
    print "For example:"
    print "    python %s 8085"%(sys.argv[0])
    print ""
    print "See also: https://github.com/winlinvip/simple-rtmp-server"
    sys.exit(1)

# parse port from user options.
port = int(sys.argv[1])
trace("api server listen at port: %s"%(port))

# cherrypy config.
conf = {
    'global': {
        'server.shutdown_timeout': 1,
        'server.socket_host': '0.0.0.0',
        'server.socket_port': port,
        'tools.encode.on': True,
        'tools.encode.encoding': "utf-8"
    },
    '/': {
        # for cherrypy RESTful api support
        'request.dispatch': cherrypy.dispatch.MethodDispatcher()
    }
}

# start cherrypy web engine
trace("start cherrypy server")
cherrypy.quickstart(Root(), '/', conf)
