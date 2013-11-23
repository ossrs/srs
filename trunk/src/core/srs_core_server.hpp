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

#ifndef SRS_CORE_SERVER_HPP
#define SRS_CORE_SERVER_HPP

/*
#include <srs_core_server.hpp>
*/

#include <srs_core.hpp>

#include <vector>

#include <st.h>

#include <srs_core_reload.hpp>

class SrsServer;
class SrsConnection;

enum SrsListenerType 
{
	SrsListenerStream = 0,
	SrsListenerApi
};

class SrsListener
{
public:
	SrsListenerType type;
private:
	int fd;
	st_netfd_t stfd;
	int port;
	SrsServer* server;
	st_thread_t tid;
	bool loop;
public:
	SrsListener(SrsServer* _server, SrsListenerType _type);
	virtual ~SrsListener();
public:
	virtual int listen(int port);
private:
	virtual void listen_cycle();
	static void* listen_thread(void* arg);
};

class SrsServer : public SrsReloadHandler
{
	friend class SrsListener;
private:
	std::vector<SrsConnection*> conns;
	std::vector<SrsListener*> listeners;
	bool signal_reload;
public:
	SrsServer();
	virtual ~SrsServer();
public:
	virtual int initialize();
	virtual int listen();
	virtual int cycle();
	virtual void remove(SrsConnection* conn);
	virtual void on_signal(int signo);
private:
	virtual void close_listeners();
	virtual int accept_client(SrsListenerType type, st_netfd_t client_stfd);
public:
	virtual int on_reload_listen();
};
	
SrsServer* _server();

#endif