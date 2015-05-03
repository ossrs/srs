/*
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
*/

#ifndef SRS_APP_CONN_HPP
#define SRS_APP_CONN_HPP

/*
#include <srs_app_conn.hpp>
*/

#include <srs_core.hpp>

#include <string>

#include <srs_app_st.hpp>
#include <srs_app_thread.hpp>
#include <srs_app_kbps.hpp>

class SrsConnection;

/**
 * the manager for connection.
 */
class IConnectionManager
{
public:
    IConnectionManager();
    virtual ~IConnectionManager();
public:
    /**
     * remove the specified connection.
     */
    virtual void remove(SrsConnection* c) = 0;
};

/**
* the basic connection of SRS,
* all connections accept from listener must extends from this base class,
* server will add the connection to manager, and delete it when remove.
*/
class SrsConnection : public virtual ISrsThreadHandler, public virtual IKbpsDelta
{
private:
    /**
    * each connection start a green thread,
    * when thread stop, the connection will be delete by server.
    */
    SrsThread* pthread;
    /**
    * the id of connection.
    */
    int id;
protected:
    /**
    * the manager object to manage the connection.
    */
    IConnectionManager* manager;
    /**
    * the underlayer st fd handler.
    */
    st_netfd_t stfd;
    /**
    * the ip of client.
    */
    std::string ip;
public:
    SrsConnection(IConnectionManager* cm, st_netfd_t c);
    virtual ~SrsConnection();
public:
    /**
    * start the client green thread.
    * when server get a client from listener, 
    * 1. server will create an concrete connection(for instance, RTMP connection),
    * 2. then add connection to its connection manager,
    * 3. start the client thread by invoke this start()
    * when client cycle thread stop, invoke the on_thread_stop(), which will use server
    * to remove the client by server->remove(this).
    */
    virtual int start();
    /**
    * the thread cycle function,
    * when serve connection completed, terminate the loop which will terminate the thread,
    * thread will invoke the on_thread_stop() when it terminated.
    */
    virtual int cycle();
    /**
    * when the thread cycle finished, thread will invoke the on_thread_stop(),
    * which will remove self from server, server will remove the connection from manager 
    * then delete the connection.
    */
    virtual void on_thread_stop();
public:
    /**
    * get the srs id which identify the client.
    */
    virtual int srs_id();
protected:
    /**
    * for concrete connection to do the cycle.
    */
    virtual int do_cycle() = 0;
private:
    /**
    * when delete the connection, stop the connection,
    * close the underlayer socket, delete the thread.
    */
    virtual void stop();
};

#endif
