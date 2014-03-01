/*
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
*/

#ifndef SRS_CORE_RELOAD_HPP
#define SRS_CORE_RELOAD_HPP

/*
#include <srs_kernel_reload.hpp>
*/
#include <srs_core.hpp>

#include <string>

/**
* the handler for config reload.
*/
class ISrsReloadHandler
{
public:
	ISrsReloadHandler();
	virtual ~ISrsReloadHandler();
public:
	virtual int on_reload_listen();
	virtual int on_reload_pithy_print();
	virtual int on_reload_vhost_removed(std::string vhost);
	virtual int on_reload_gop_cache(std::string vhost);
	virtual int on_reload_queue_length(std::string vhost);
	virtual int on_reload_forward(std::string vhost);
	virtual int on_reload_hls(std::string vhost);
	virtual int on_reload_transcode(std::string vhost);
};

#endif