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

#include <srs_core_server.hpp>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>

#include <algorithm>

#include <st.h>

#include <srs_core_log.hpp>
#include <srs_core_error.hpp>
#include <srs_core_client.hpp>
#include <srs_core_config.hpp>

#define SERVER_LISTEN_BACKLOG 10
#define SRS_TIME_RESOLUTION_MS 500

SrsListener::SrsListener(SrsServer* _server, SrsListenerType _type)
{
	fd = -1;
	stfd = NULL;
	
	port = 0;
	server = _server;
	type = _type;
	
	tid = NULL;
	loop = false;
}

SrsListener::~SrsListener()
{
	if (stfd) {
		st_netfd_close(stfd);
		stfd = NULL;
	}
	
	if (tid) {
		loop = false;
		st_thread_interrupt(tid);
		st_thread_join(tid, NULL);
		tid = NULL;
	}
	
	// st does not close it sometimes, 
	// close it manually.
	close(fd);
}

int SrsListener::listen(int _port)
{
	int ret = ERROR_SUCCESS;
	
	port = _port;
	
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        ret = ERROR_SOCKET_CREATE;
        srs_error("create linux socket error. ret=%d", ret);
        return ret;
	}
	srs_verbose("create linux socket success. fd=%d", fd);
    
    int reuse_socket = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse_socket, sizeof(int)) == -1) {
        ret = ERROR_SOCKET_SETREUSE;
        srs_error("setsockopt reuse-addr error. ret=%d", ret);
        return ret;
    }
    srs_verbose("setsockopt reuse-addr success. fd=%d", fd);
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (const sockaddr*)&addr, sizeof(sockaddr_in)) == -1) {
        ret = ERROR_SOCKET_BIND;
        srs_error("bind socket error. ret=%d", ret);
        return ret;
    }
    srs_verbose("bind socket success. fd=%d", fd);
    
    if (::listen(fd, SERVER_LISTEN_BACKLOG) == -1) {
        ret = ERROR_SOCKET_LISTEN;
        srs_error("listen socket error. ret=%d", ret);
        return ret;
    }
    srs_verbose("listen socket success. fd=%d", fd);
    
    if ((stfd = st_netfd_open_socket(fd)) == NULL){
        ret = ERROR_ST_OPEN_SOCKET;
        srs_error("st_netfd_open_socket open socket failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("st open socket success. fd=%d", fd);
    
    if ((tid = st_thread_create(listen_thread, this, 1, 0)) == NULL) {
        ret = ERROR_ST_CREATE_LISTEN_THREAD;
        srs_error("st_thread_create listen thread error. ret=%d", ret);
        return ret;
    }
    srs_verbose("create st listen thread success.");
    
    srs_trace("server started, listen at port=%d, fd=%d", port, fd);
	
	return ret;
}

void SrsListener::listen_cycle()
{
	int ret = ERROR_SUCCESS;
	
	log_context->generate_id();
	srs_trace("listen cycle start, port=%d, type=%d, fd=%d", port, type, fd);
	
	while (loop) {
	    st_netfd_t client_stfd = st_accept(stfd, NULL, NULL, ST_UTIME_NO_TIMEOUT);
	    
	    if(client_stfd == NULL){
	        // ignore error.
	        srs_warn("ignore accept thread stoppped for accept client error");
	        continue;
        }
	    srs_verbose("get a client. fd=%d", st_netfd_fileno(client_stfd));
    	
    	if ((ret = server->accept_client(type, client_stfd)) != ERROR_SUCCESS) {
    		srs_warn("accept client error. ret=%d", ret);
			continue;
    	}

    	srs_verbose("accept client finished. conns=%d, ret=%d", (int)conns.size(), ret);
	}
}

void* SrsListener::listen_thread(void* arg)
{
	SrsListener* obj = (SrsListener*)arg;
	srs_assert(obj != NULL);
	
	obj->loop = true;
	obj->listen_cycle();
	
	return NULL;
}

SrsServer::SrsServer()
{
	signal_reload = false;
	
	srs_assert(config);
	config->subscribe(this);
}

SrsServer::~SrsServer()
{
	config->unsubscribe(this);
	
	if (true) {
		std::vector<SrsConnection*>::iterator it;
		for (it = conns.begin(); it != conns.end(); ++it) {
			SrsConnection* conn = *it;
			srs_freep(conn);
		}
		conns.clear();
	}
	
	close_listeners();
}

int SrsServer::initialize()
{
	int ret = ERROR_SUCCESS;
    
    // use linux epoll.
    if (st_set_eventsys(ST_EVENTSYS_ALT) == -1) {
        ret = ERROR_ST_SET_EPOLL;
        srs_error("st_set_eventsys use linux epoll failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("st_set_eventsys use linux epoll success");
    
    if(st_init() != 0){
        ret = ERROR_ST_INITIALIZE;
        srs_error("st_init failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("st_init success");
	
	// set current log id.
	log_context->generate_id();
	srs_info("log set id success");
	
	return ret;
}

int SrsServer::listen()
{
	int ret = ERROR_SUCCESS;
	
	SrsConfDirective* conf = NULL;
	
	// stream service port.
	conf = config->get_listen();
	srs_assert(conf);
	
	close_listeners();
	
	for (int i = 0; i < (int)conf->args.size(); i++) {
		SrsListener* listener = new SrsListener(this, SrsListenerStream);
		listeners.push_back(listener);
		
		int port = ::atoi(conf->args.at(i).c_str());
		if ((ret = listener->listen(port)) != ERROR_SUCCESS) {
			srs_error("listen at port %d failed. ret=%d", port, ret);
			return ret;
		}
	}
	
	return ret;
}

int SrsServer::cycle()
{
	int ret = ERROR_SUCCESS;
	
	// the deamon thread, update the time cache
	while (true) {
		st_usleep(SRS_TIME_RESOLUTION_MS * 1000);
        srs_update_system_time_ms();
		
		if (signal_reload) {
			signal_reload = false;
			srs_info("get signal reload, to reload the config.");
			
			if ((ret = config->reload()) != ERROR_SUCCESS) {
				srs_error("reload config failed. ret=%d", ret);
				return ret;
			}
			srs_trace("reload config success.");
		}
	}
	
	return ret;
}

void SrsServer::remove(SrsConnection* conn)
{
	std::vector<SrsConnection*>::iterator it = std::find(conns.begin(), conns.end(), conn);
	
	if (it != conns.end()) {
		conns.erase(it);
	}
	
	srs_info("conn removed. conns=%d", (int)conns.size());
	
	// all connections are created by server,
	// so we free it here.
	srs_freep(conn);
}

void SrsServer::on_signal(int signo)
{
	if (signo == SIGNAL_RELOAD) {
		signal_reload = true;
	}

	// TODO: handle the SIGINT, SIGTERM.
}

void SrsServer::close_listeners()
{
	std::vector<SrsListener*>::iterator it;
	for (it = listeners.begin(); it != listeners.end(); ++it) {
		SrsListener* listener = *it;
		srs_freep(listener);
	}
	listeners.clear();
}

int SrsServer::accept_client(SrsListenerType type, st_netfd_t client_stfd)
{
	int ret = ERROR_SUCCESS;
	
	int max_connections = config->get_max_connections();
	if ((int)conns.size() >= max_connections) {
		int fd = st_netfd_fileno(client_stfd);
		
		srs_error("exceed the max connections, drop client: "
			"clients=%d, max=%d, fd=%d", (int)conns.size(), max_connections, fd);
			
		st_netfd_close(client_stfd);
		::close(fd);
		
		return ret;
	}
	
	SrsConnection* conn = NULL;
	if (type == SrsListenerStream) {
		conn = new SrsClient(this, client_stfd);
	} else {
		// handler others
	}
	srs_assert(conn);
	
	// directly enqueue, the cycle thread will remove the client.
	conns.push_back(conn);
	srs_verbose("add conn from port %d to vector. conns=%d", port, (int)conns.size());
	
	// cycle will start process thread and when finished remove the client.
	if ((ret = conn->start()) != ERROR_SUCCESS) {
		return ret;
	}
	srs_verbose("conn start finished. ret=%d", ret);
    
	return ret;
}

int SrsServer::on_reload_listen()
{
	return listen();
}

SrsServer* _server()
{
	static SrsServer server;
	return &server;
}
