/*
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
*/

#include <srs_core_log.hpp>
#include <srs_core_error.hpp>
#include <srs_core_server.hpp>

#include <stdlib.h>

int main(int argc, char** argv){
	int ret = ERROR_SUCCESS;
	
	if (argc <= 1) {
		printf(RTMP_SIG_SRS_NAME" "RTMP_SIG_SRS_VERSION
			" Copyright (c) 2013 winlin\n"
			"Usage: %s <listen_port>\n" 
			"\n"
			RTMP_SIG_SRS_WEB"\n"
			RTMP_SIG_SRS_URL"\n"
			"Email: "RTMP_SIG_SRS_EMAIL"\n",
			argv[0]);
		exit(1);
	}
	
	int listen_port = ::atoi(argv[1]);
	srs_trace("listen_port=%d", listen_port);
	
	SrsServer server;
	
	if ((ret = server.initialize()) != ERROR_SUCCESS) {
		return ret;
	}
	
	if ((ret = server.listen(listen_port)) != ERROR_SUCCESS) {
		return ret;
	}
	
	if ((ret = server.cycle()) != ERROR_SUCCESS) {
		return ret;
	}
	
    return 0;
}
