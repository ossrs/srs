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

#include <srs_librtmp.hpp>

#include <stdlib.h>

// for srs-librtmp, @see https://github.com/simple-rtmp-server/srs/issues/213
#ifndef _WIN32
#include <sys/time.h>
#endif

#include <string>
#include <sstream>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_lib_simple_socket.hpp>
#include <srs_rtmp_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_rtmp_amf0.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_file.hpp>
#include <srs_lib_bandwidth.hpp>
#include <srs_raw_avc.hpp>

// kernel module.
ISrsLog* _srs_log = new ISrsLog();
ISrsThreadContext* _srs_context = new ISrsThreadContext();

/**
* export runtime context.
*/
struct Context
{
    std::string url;
    std::string tcUrl;
    std::string host;
    std::string ip;
    std::string port;
    std::string vhost;
    std::string app;
    std::string stream;
    std::string param;

    // extra request object for connect to server, NULL to ignore.
    SrsRequest* req;
    
    // the message received cache,
    // for example, when got aggregate message,
    // the context will parse to videos/audios,
    // and return one by one.
    std::vector<SrsCommonMessage*> msgs;
    
    SrsRtmpClient* rtmp;
    SimpleSocketStream* skt;
    int stream_id;
    
    // the remux raw codec.
    SrsRawH264Stream avc_raw;
    SrsRawAacStream aac_raw;

    // for h264 raw stream, 
    // @see: https://github.com/simple-rtmp-server/srs/issues/66#issuecomment-62240521
    SrsStream h264_raw_stream;
    // about SPS, @see: 7.3.2.1.1, H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 62
    std::string h264_sps;
    std::string h264_pps;
    // whether the sps and pps sent,
    // @see https://github.com/simple-rtmp-server/srs/issues/203
    bool h264_sps_pps_sent;
    // only send the ssp and pps when both changed.
    // @see https://github.com/simple-rtmp-server/srs/issues/204
    bool h264_sps_changed;
    bool h264_pps_changed;
    // for aac raw stream,
    // @see: https://github.com/simple-rtmp-server/srs/issues/212#issuecomment-64146250
    SrsStream aac_raw_stream;
    // the aac sequence header.
    std::string aac_specific_config;
    
    Context() {
        rtmp = NULL;
        skt = NULL;
        req = NULL;
        stream_id = 0;
        h264_sps_pps_sent = false;
        h264_sps_changed = false;
        h264_pps_changed = false;
    }
    virtual ~Context() {
        srs_freep(req);
        srs_freep(rtmp);
        srs_freep(skt);
        
        std::vector<SrsCommonMessage*>::iterator it;
        for (it = msgs.begin(); it != msgs.end(); ++it) {
            SrsCommonMessage* msg = *it;
            srs_freep(msg);
        }
        msgs.clear();
    }
};

// for srs-librtmp, @see https://github.com/simple-rtmp-server/srs/issues/213
#ifdef _WIN32
    int gettimeofday(struct timeval* tv, struct timezone* tz)
    {  
        time_t clock;
        struct tm tm;
        SYSTEMTIME win_time;
    
        GetLocalTime(&win_time);
    
        tm.tm_year = win_time.wYear - 1900;
        tm.tm_mon = win_time.wMonth - 1;
        tm.tm_mday = win_time.wDay;
        tm.tm_hour = win_time.wHour;
        tm.tm_min = win_time.wMinute;
        tm.tm_sec = win_time.wSecond;
        tm.tm_isdst = -1;
    
        clock = mktime(&tm);
    
        tv->tv_sec = (long)clock;
        tv->tv_usec = win_time.wMilliseconds * 1000;
    
        return 0;
    }
    
    int socket_setup()
    {
        WORD wVersionRequested;
        WSADATA wsaData;
        int err;
    
        /* Use the MAKEWORD(lowbyte, highbyte) macro declared in Windef.h */
        wVersionRequested = MAKEWORD(2, 2);
    
        err = WSAStartup(wVersionRequested, &wsaData);
        if (err != 0) {
            /* Tell the user that we could not find a usable */
            /* Winsock DLL.                                  */
            //printf("WSAStartup failed with error: %d\n", err);
            return -1;
        }
        return 0;
    }
    
    int socket_cleanup()
    {
        WSACleanup();
        return 0;
    }
    
    pid_t getpid(void)
    {
        return (pid_t)GetCurrentProcessId();
    }
    
    int usleep(useconds_t usec)
    {
        Sleep((DWORD)(usec / 1000));
        return 0;
    }
    
    ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
    {
        ssize_t nwrite = 0;
        for (int i = 0; i < iovcnt; i++) {
            const struct iovec* current = iov + i;
    
            int nsent = ::send(fd, (char*)current->iov_base, current->iov_len, 0);
            if (nsent < 0) {
                return nsent;
            }
    
            nwrite += nsent;
            if (nsent == 0) {
                return nwrite;
            }
        }
        return nwrite;
    }
    
    ////////////////////////   strlcpy.c (modified) //////////////////////////
    
    /*    $OpenBSD: strlcpy.c,v 1.11 2006/05/05 15:27:38 millert Exp $    */
    
    /*-
     * Copyright (c) 1998 Todd C. Miller <Todd.Miller@courtesan.com>
     *
     * Permission to use, copy, modify, and distribute this software for any
     * purpose with or without fee is hereby granted, provided that the above
     * copyright notice and this permission notice appear in all copies.
     *
     * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
     * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
     * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
     * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
     * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
     * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
     * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
     */
    
    //#include <sys/cdefs.h> // ****
    //#include <cstddef> // ****
    // __FBSDID("$FreeBSD: stable/9/sys/libkern/strlcpy.c 243811 2012-12-03 18:08:44Z delphij $"); // ****
    
    // #include <sys/types.h> // ****
    // #include <sys/libkern.h> // ****
    
    /*
     * Copy src to string dst of size siz.  At most siz-1 characters
     * will be copied.  Always NUL terminates (unless siz == 0).
     * Returns strlen(src); if retval >= siz, truncation occurred.
     */
    
    //#define __restrict // ****
    
    std::size_t strlcpy(char * __restrict dst, const char * __restrict src, size_t siz)
    {
        char *d = dst;
        const char *s = src;
        size_t n = siz;
    
        /* Copy as many bytes as will fit */
        if (n != 0) {
            while (--n != 0) {
                if ((*d++ = *s++) == '\0')
                    break;
            }
        }
    
        /* Not enough room in dst, add NUL and traverse rest of src */
        if (n == 0) {
            if (siz != 0)
                *d = '\0';        /* NUL-terminate dst */
            while (*s++)
                ;
        }
    
        return(s - src - 1);    /* count does not include NUL */
    }
    
    // http://www.cplusplus.com/forum/general/141779/////////////////////////   inet_ntop.c (modified) //////////////////////////
    /*
     * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
     * Copyright (c) 1996-1999 by Internet Software Consortium.
     *
     * Permission to use, copy, modify, and distribute this software for any
     * purpose with or without fee is hereby granted, provided that the above
     * copyright notice and this permission notice appear in all copies.
     *
     * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
     * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
     * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
     * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
     * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
     * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
     * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
     */
    
    // #if defined(LIBC_SCCS) && !defined(lint) // ****
    //static const char rcsid[] = "$Id: inet_ntop.c,v 1.3.18.2 2005/11/03 23:02:22 marka Exp $";
    // #endif /* LIBC_SCCS and not lint */ // ****
    // #include <sys/cdefs.h> // ****
    // __FBSDID("$FreeBSD: stable/9/sys/libkern/inet_ntop.c 213103 2010-09-24 15:01:45Z attilio $"); // ****
    
    //#define _WIN32_WINNT _WIN32_WINNT_WIN8 // ****
    //#include <Ws2tcpip.h> // ****
    #pragma comment(lib, "Ws2_32.lib") // ****
    //#include <cstdio> // ****
    
    // #include <sys/param.h> // ****
    // #include <sys/socket.h> // ****
    // #include <sys/systm.h> // ****
    
    // #include <netinet/in.h> // ****
    
    /*%
     * WARNING: Don't even consider trying to compile this on a system where
     * sizeof(int) < 4.  sizeof(int) > 4 is fine; all the world's not a VAX.
     */
    
    static char *inet_ntop4(const u_char *src, char *dst, socklen_t size);
    static char *inet_ntop6(const u_char *src, char *dst, socklen_t size);
    
    /* char *
     * inet_ntop(af, src, dst, size)
     *    convert a network format address to presentation format.
     * return:
     *    pointer to presentation format address (`dst'), or NULL (see errno).
     * author:
     *    Paul Vixie, 1996.
     */
    const char* inet_ntop(int af, const void *src, char *dst, socklen_t size)
    {
        switch (af) {
        case AF_INET:
            return (inet_ntop4( (unsigned char*)src, (char*)dst, size)); // ****
    #ifdef AF_INET6
        #error "IPv6 not supported"
        //case AF_INET6:
        //    return (char*)(inet_ntop6( (unsigned char*)src, (char*)dst, size)); // ****
    #endif
        default:
            // return (NULL); // ****
            return 0 ; // ****
        }
        /* NOTREACHED */
    }
    
    /* const char *
     * inet_ntop4(src, dst, size)
     *    format an IPv4 address
     * return:
     *    `dst' (as a const)
     * notes:
     *    (1) uses no statics
     *    (2) takes a u_char* not an in_addr as input
     * author:
     *    Paul Vixie, 1996.
     */
    static char * inet_ntop4(const u_char *src, char *dst, socklen_t size)
    {
        static const char fmt[128] = "%u.%u.%u.%u";
        char tmp[sizeof "255.255.255.255"];
        int l;
    
        l = snprintf(tmp, sizeof(tmp), fmt, src[0], src[1], src[2], src[3]); // ****
        if (l <= 0 || (socklen_t) l >= size) {
            return (NULL);
        }
        strlcpy(dst, tmp, size);
        return (dst);
    }
    
    /* const char *
     * inet_ntop6(src, dst, size)
     *    convert IPv6 binary address into presentation (printable) format
     * author:
     *    Paul Vixie, 1996.
     */
    static char * inet_ntop6(const u_char *src, char *dst, socklen_t size)
    {
        /*
         * Note that int32_t and int16_t need only be "at least" large enough
         * to contain a value of the specified size.  On some systems, like
         * Crays, there is no such thing as an integer variable with 16 bits.
         * Keep this in mind if you think this function should have been coded
         * to use pointer overlays.  All the world's not a VAX.
         */
        char tmp[sizeof "ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255"], *tp;
        struct { int base, len; } best, cur;
    #define NS_IN6ADDRSZ 16
    #define NS_INT16SZ 2
        u_int words[NS_IN6ADDRSZ / NS_INT16SZ];
        int i;
    
        /*
         * Preprocess:
         *    Copy the input (bytewise) array into a wordwise array.
         *    Find the longest run of 0x00's in src[] for :: shorthanding.
         */
        memset(words, '\0', sizeof words);
        for (i = 0; i < NS_IN6ADDRSZ; i++)
            words[i / 2] |= (src[i] << ((1 - (i % 2)) << 3));
        best.base = -1;
        best.len = 0;
        cur.base = -1;
        cur.len = 0;
        for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++) {
            if (words[i] == 0) {
                if (cur.base == -1)
                    cur.base = i, cur.len = 1;
                else
                    cur.len++;
            } else {
                if (cur.base != -1) {
                    if (best.base == -1 || cur.len > best.len)
                        best = cur;
                    cur.base = -1;
                }
            }
        }
        if (cur.base != -1) {
            if (best.base == -1 || cur.len > best.len)
                best = cur;
        }
        if (best.base != -1 && best.len < 2)
            best.base = -1;
    
        /*
         * Format the result.
         */
        tp = tmp;
        for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++) {
            /* Are we inside the best run of 0x00's? */
            if (best.base != -1 && i >= best.base &&
                i < (best.base + best.len)) {
                if (i == best.base)
                    *tp++ = ':';
                continue;
            }
            /* Are we following an initial run of 0x00s or any real hex? */
            if (i != 0)
                *tp++ = ':';
            /* Is this address an encapsulated IPv4? */
            if (i == 6 && best.base == 0 && (best.len == 6 ||
                (best.len == 7 && words[7] != 0x0001) ||
                (best.len == 5 && words[5] == 0xffff))) {
                if (!inet_ntop4(src+12, tp, sizeof tmp - (tp - tmp)))
                    return (NULL);
                tp += strlen(tp);
                break;
            }
            tp += std::sprintf(tp, "%x", words[i]); // ****
        }
        /* Was it a trailing run of 0x00's? */
        if (best.base != -1 && (best.base + best.len) == 
            (NS_IN6ADDRSZ / NS_INT16SZ))
            *tp++ = ':';
        *tp++ = '\0';
    
        /*
         * Check for overflow, copy, and we're done.
         */
        if ((socklen_t)(tp - tmp) > size) {
            return (NULL);
        }
        strcpy(dst, tmp);
        return (dst);
    }
#endif

int srs_librtmp_context_parse_uri(Context* context) 
{
    int ret = ERROR_SUCCESS;
    
    // parse uri
    size_t pos = string::npos;
    string uri = context->url;
    // tcUrl, stream
    if ((pos = uri.rfind("/")) != string::npos) {
        context->stream = uri.substr(pos + 1);
        context->tcUrl = uri = uri.substr(0, pos);
    }
    
    std::string schema;
    srs_discovery_tc_url(context->tcUrl, 
        schema, context->host, context->vhost, context->app, context->port,
        context->param);
    
    return ret;
}

int srs_librtmp_context_resolve_host(Context* context) 
{
    int ret = ERROR_SUCCESS;
    
    // create socket
    srs_freep(context->skt);
    context->skt = new SimpleSocketStream();
    
    if ((ret = context->skt->create_socket()) != ERROR_SUCCESS) {
        return ret;
    }
    
    // connect to server:port
    context->ip = srs_dns_resolve(context->host);
    if (context->ip.empty()) {
        return -1;
    }
    
    return ret;
}

int srs_librtmp_context_connect(Context* context) 
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(context->skt);
    
    std::string ip = context->ip;
    int port = ::atoi(context->port.c_str());
    
    if ((ret = context->skt->connect(ip.c_str(), port)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

#ifdef __cplusplus
extern "C"{
#endif

int srs_version_major()
{
    return VERSION_MAJOR;
}

int srs_version_minor()
{
    return VERSION_MINOR;
}

int srs_version_revision()
{
    return VERSION_REVISION;
}

srs_rtmp_t srs_rtmp_create(const char* url)
{
    Context* context = new Context();
    context->url = url;
    return context;
}

srs_rtmp_t srs_rtmp_create2(const char* url)
{
    Context* context = new Context();
    
    // use url as tcUrl.
    context->url = url;
    // auto append stream.
    context->url += "/livestream";
    
    return context;
}

void srs_rtmp_destroy(srs_rtmp_t rtmp)
{
    if (!rtmp) {
        return;
    }
    
    Context* context = (Context*)rtmp;
    
    srs_freep(context);
}

int srs_rtmp_handshake(srs_rtmp_t rtmp)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = srs_rtmp_dns_resolve(rtmp)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = srs_rtmp_connect_server(rtmp)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = srs_rtmp_do_simple_handshake(rtmp)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int srs_rtmp_dns_resolve(srs_rtmp_t rtmp)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    // parse uri
    if ((ret = srs_librtmp_context_parse_uri(context)) != ERROR_SUCCESS) {
        return ret;
    }
    // resolve host
    if ((ret = srs_librtmp_context_resolve_host(context)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int srs_rtmp_connect_server(srs_rtmp_t rtmp)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    if ((ret = srs_librtmp_context_connect(context)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int srs_rtmp_do_complex_handshake(srs_rtmp_t rtmp)
{
#ifndef SRS_AUTO_SSL
    // complex handshake requires ssl
    return ERROR_RTMP_HS_SSL_REQUIRE;
#endif

    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    srs_assert(context->skt != NULL);
    
    // simple handshake
    srs_freep(context->rtmp);
    context->rtmp = new SrsRtmpClient(context->skt);
    
    if ((ret = context->rtmp->complex_handshake()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int srs_rtmp_set_connect_args(srs_rtmp_t rtmp, 
    const char* tcUrl, const char* swfUrl, const char* pageUrl, srs_amf0_t args
) {
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    srs_freep(context->req);
    context->req = new SrsRequest();
    
    if (args) {
        context->req->args = (SrsAmf0Object*)args;
    }
    if (tcUrl) {
        context->req->tcUrl = tcUrl;
    }
    if (swfUrl) {
        context->req->swfUrl = swfUrl;
    }
    if (pageUrl) {
        context->req->pageUrl = pageUrl;
    }
    
    return ret;
}

int srs_rtmp_do_simple_handshake(srs_rtmp_t rtmp)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    srs_assert(context->skt != NULL);
    
    // simple handshake
    srs_freep(context->rtmp);
    context->rtmp = new SrsRtmpClient(context->skt);
    
    if ((ret = context->rtmp->simple_handshake()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int srs_rtmp_connect_app(srs_rtmp_t rtmp)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    string tcUrl = srs_generate_tc_url(
        context->ip, context->vhost, context->app, context->port,
        context->param
    );
    
    if ((ret = context->rtmp->connect_app(
        context->app, tcUrl, context->req, true)) != ERROR_SUCCESS) 
    {
        return ret;
    }
    
    return ret;
}

int srs_rtmp_connect_app2(srs_rtmp_t rtmp,
    char srs_server_ip[128],char srs_server[128], 
    char srs_primary[128], char srs_authors[128], 
    char srs_version[32], int* srs_id, int* srs_pid
) {
    srs_server_ip[0] = 0;
    srs_server[0] = 0;
    srs_primary[0] = 0;
    srs_authors[0] = 0;
    srs_version[0] = 0;
    *srs_id = 0;
    *srs_pid = 0;

    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    string tcUrl = srs_generate_tc_url(
        context->ip, context->vhost, context->app, context->port,
        context->param
    );
    
    std::string sip, sserver, sprimary, sauthors, sversion;
    
    if ((ret = context->rtmp->connect_app2(context->app, tcUrl, NULL, true,
        sip, sserver, sprimary, sauthors, sversion, *srs_id, *srs_pid)) != ERROR_SUCCESS) {
        return ret;
    }
    
    snprintf(srs_server_ip, 128, "%s", sip.c_str());
    snprintf(srs_server, 128, "%s", sserver.c_str());
    snprintf(srs_primary, 128, "%s", sprimary.c_str());
    snprintf(srs_authors, 128, "%s", sauthors.c_str());
    snprintf(srs_version, 32, "%s", sversion.c_str());
    
    return ret;
}

int srs_rtmp_play_stream(srs_rtmp_t rtmp)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    if ((ret = context->rtmp->create_stream(context->stream_id)) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = context->rtmp->play(context->stream, context->stream_id)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int srs_rtmp_publish_stream(srs_rtmp_t rtmp)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    if ((ret = context->rtmp->fmle_publish(context->stream, context->stream_id)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int srs_rtmp_bandwidth_check(srs_rtmp_t rtmp, 
    int64_t* start_time, int64_t* end_time, 
    int* play_kbps, int* publish_kbps,
    int* play_bytes, int* publish_bytes,
    int* play_duration, int* publish_duration
) {
    *start_time = 0;
    *end_time = 0;
    *play_kbps = 0;
    *publish_kbps = 0;
    *play_bytes = 0;
    *publish_bytes = 0;
    *play_duration = 0;
    *publish_duration = 0;
    
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    SrsBandwidthClient client;

    if ((ret = client.initialize(context->rtmp)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = client.bandwidth_check(
        start_time, end_time, play_kbps, publish_kbps,
        play_bytes, publish_bytes, play_duration, publish_duration)) != ERROR_SUCCESS
    ) {
        return ret;
    }
    
    return ret;
}


int srs_rtmp_on_aggregate(Context* context, SrsCommonMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    SrsStream aggregate_stream;
    SrsStream* stream = &aggregate_stream;
    if ((ret = stream->initialize(msg->payload, msg->size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // the aggregate message always use abs time.
    int delta = -1;
    
    while (!stream->empty()) {
        if (!stream->require(1)) {
            ret = ERROR_RTMP_AGGREGATE;
            srs_error("invalid aggregate message type. ret=%d", ret);
            return ret;
        }
        int8_t type = stream->read_1bytes();
        
        if (!stream->require(3)) {
            ret = ERROR_RTMP_AGGREGATE;
            srs_error("invalid aggregate message size. ret=%d", ret);
            return ret;
        }
        int32_t data_size = stream->read_3bytes();
        
        if (data_size < 0) {
            ret = ERROR_RTMP_AGGREGATE;
            srs_error("invalid aggregate message size(negative). ret=%d", ret);
            return ret;
        }
        
        if (!stream->require(3)) {
            ret = ERROR_RTMP_AGGREGATE;
            srs_error("invalid aggregate message time. ret=%d", ret);
            return ret;
        }
        int32_t timestamp = stream->read_3bytes();
        
        if (!stream->require(1)) {
            ret = ERROR_RTMP_AGGREGATE;
            srs_error("invalid aggregate message time(high). ret=%d", ret);
            return ret;
        }
        int32_t time_h = stream->read_1bytes();
        
        timestamp |= time_h<<24;
        timestamp &= 0x7FFFFFFF;
        
        // adjust abs timestamp in aggregate msg.
        if (delta < 0) {
            delta = (int)msg->header.timestamp - (int)timestamp;
        }
        timestamp += delta;
        
        if (!stream->require(3)) {
            ret = ERROR_RTMP_AGGREGATE;
            srs_error("invalid aggregate message stream_id. ret=%d", ret);
            return ret;
        }
        int32_t stream_id = stream->read_3bytes();
        
        if (data_size > 0 && !stream->require(data_size)) {
            ret = ERROR_RTMP_AGGREGATE;
            srs_error("invalid aggregate message data. ret=%d", ret);
            return ret;
        }
        
        // to common message.
        SrsCommonMessage o;
        
        o.header.message_type = type;
        o.header.payload_length = data_size;
        o.header.timestamp_delta = timestamp;
        o.header.timestamp = timestamp;
        o.header.stream_id = stream_id;
        o.header.perfer_cid = msg->header.perfer_cid;

        if (data_size > 0) {
            o.size = data_size;
            o.payload = new char[o.size];
            stream->read_bytes(o.payload, o.size);
        }
        
        if (!stream->require(4)) {
            ret = ERROR_RTMP_AGGREGATE;
            srs_error("invalid aggregate message previous tag size. ret=%d", ret);
            return ret;
        }
        stream->read_4bytes();

        // process parsed message
        SrsCommonMessage* parsed_msg = new SrsCommonMessage();
        parsed_msg->header = o.header;
        parsed_msg->payload = o.payload;
        parsed_msg->size = o.size;
        o.payload = NULL;
        context->msgs.push_back(parsed_msg);
    }
    
    return ret;
}

int srs_rtmp_go_packet(Context* context, SrsCommonMessage* msg, 
    char* type, u_int32_t* timestamp, char** data, int* size,
    bool* got_msg
) {
    int ret = ERROR_SUCCESS;

    // generally we got a message.
    *got_msg = true;
    
    if (msg->header.is_audio()) {
        *type = SRS_RTMP_TYPE_AUDIO;
        *timestamp = (u_int32_t)msg->header.timestamp;
        *data = (char*)msg->payload;
        *size = (int)msg->size;
        // detach bytes from packet.
        msg->payload = NULL;
    } else if (msg->header.is_video()) {
        *type = SRS_RTMP_TYPE_VIDEO;
        *timestamp = (u_int32_t)msg->header.timestamp;
        *data = (char*)msg->payload;
        *size = (int)msg->size;
        // detach bytes from packet.
        msg->payload = NULL;
    } else if (msg->header.is_amf0_data() || msg->header.is_amf3_data()) {
        *type = SRS_RTMP_TYPE_SCRIPT;
        *data = (char*)msg->payload;
        *size = (int)msg->size;
        // detach bytes from packet.
        msg->payload = NULL;
    } else if (msg->header.is_aggregate()) {
        if ((ret = srs_rtmp_on_aggregate(context, msg)) != ERROR_SUCCESS) {
            return ret;
        }
        *got_msg = false;
    } else {
        *type = msg->header.message_type;
        *data = (char*)msg->payload;
        *size = (int)msg->size;
        // detach bytes from packet.
        msg->payload = NULL;
    }
    
    return ret;
}

int srs_rtmp_read_packet(srs_rtmp_t rtmp, char* type, u_int32_t* timestamp, char** data, int* size)
{
    *type = 0;
    *timestamp = 0;
    *data = NULL;
    *size = 0;
    
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    for (;;) {
        SrsCommonMessage* msg = NULL;
        
        // read from cache first.
        if (!context->msgs.empty()) {
            std::vector<SrsCommonMessage*>::iterator it = context->msgs.begin();
            msg = *it;
            context->msgs.erase(it);
        }
        
        // read from protocol sdk.
        if (!msg && (ret = context->rtmp->recv_message(&msg)) != ERROR_SUCCESS) {
            return ret;
        }
        
        // no msg, try again.
        if (!msg) {
            continue;
        }
        
        SrsAutoFree(SrsCommonMessage, msg);
        
        // process the got packet, if nothing, try again.
        bool got_msg;
        if ((ret = srs_rtmp_go_packet(context, msg, type, timestamp, data, size, &got_msg)) != ERROR_SUCCESS) {
            return ret;
        }
        
        // got expected message.
        if (got_msg) {
            break;
        }
    }
    
    return ret;
}

int srs_rtmp_write_packet(srs_rtmp_t rtmp, char type, u_int32_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    SrsSharedPtrMessage* msg = NULL;

    if ((ret = srs_rtmp_create_msg(type, timestamp, data, size, context->stream_id, &msg)) != ERROR_SUCCESS) {
        return ret;
    }

    srs_assert(msg);

    // send out encoded msg.
    if ((ret = context->rtmp->send_and_free_message(msg, context->stream_id)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

srs_bool srs_rtmp_is_onMetaData(char type, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    if (type != SRS_RTMP_TYPE_SCRIPT) {
        return false;
    }
    
    SrsStream stream;
    if ((ret = stream.initialize(data, size)) != ERROR_SUCCESS) {
        return false;
    }
    
    std::string name;
    if ((ret = srs_amf0_read_string(&stream, name)) != ERROR_SUCCESS) {
        return false;
    }
    
    if (name == SRS_CONSTS_RTMP_ON_METADATA) {
        return true;
    }
    
    if (name == SRS_CONSTS_RTMP_SET_DATAFRAME) {
        return true;
    }
    
    return false;
}

/**
* directly write a audio frame.
*/
int srs_write_audio_raw_frame(Context* context,
    char* frame, int frame_size, SrsRawAacStreamCodec* codec, u_int32_t timestamp
) {
    int ret = ERROR_SUCCESS;

    char* data = NULL;
    int size = 0;
    if ((ret = context->aac_raw.mux_aac2flv(frame, frame_size, codec, timestamp, &data, &size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return srs_rtmp_write_packet(context, SRS_RTMP_TYPE_AUDIO, timestamp, data, size);
}

/**
* write aac frame in adts.
*/
int srs_write_aac_adts_frame(Context* context, 
    SrsRawAacStreamCodec* codec, char* frame, int frame_size, u_int32_t timestamp
) {
    int ret = ERROR_SUCCESS;
    
    // send out aac sequence header if not sent.
    if (context->aac_specific_config.empty()) {
        std::string sh;
        if ((ret = context->aac_raw.mux_sequence_header(codec, sh)) != ERROR_SUCCESS) {
            return ret;
        }
        context->aac_specific_config = sh;

        codec->aac_packet_type = 0;

        if ((ret = srs_write_audio_raw_frame(context, (char*)sh.data(), (int)sh.length(), codec, timestamp)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    codec->aac_packet_type = 1;
    return srs_write_audio_raw_frame(context, frame, frame_size, codec, timestamp);
}

/**
* write aac frames in adts.
*/
int srs_write_aac_adts_frames(Context* context,
    char sound_format, char sound_rate, char sound_size, char sound_type,
    char* frames, int frames_size, u_int32_t timestamp
) {
    int ret = ERROR_SUCCESS;
    
    SrsStream* stream = &context->aac_raw_stream;
    if ((ret = stream->initialize(frames, frames_size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    while (!stream->empty()) {
        char* frame = NULL;
        int frame_size = 0;
        SrsRawAacStreamCodec codec;
        if ((ret = context->aac_raw.adts_demux(stream, &frame, &frame_size, codec)) != ERROR_SUCCESS) {
            return ret;
        }

        // override by user specified.
        codec.sound_format = sound_format;
        codec.sound_rate = sound_rate;
        codec.sound_size = sound_size;
        codec.sound_type = sound_type;

        if ((ret = srs_write_aac_adts_frame(context, &codec, frame, frame_size, timestamp)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

/**
* write audio raw frame to SRS.
*/
int srs_audio_write_raw_frame(srs_rtmp_t rtmp, 
    char sound_format, char sound_rate, char sound_size, char sound_type,
    char* frame, int frame_size, u_int32_t timestamp
) {
    int ret = ERROR_SUCCESS;
    
    Context* context = (Context*)rtmp;
    srs_assert(context);
    
    if (sound_format == SrsCodecAudioAAC) {
        // for aac, the frame must be ADTS format.
        if (!srs_aac_is_adts(frame, frame_size)) {
            return ERROR_AAC_REQUIRED_ADTS;
        }
        
        // for aac, demux the ADTS to RTMP format.
        return srs_write_aac_adts_frames(context, 
            sound_format, sound_rate, sound_size, sound_type, 
            frame, frame_size, timestamp);
    } else {
        // use codec info for aac.
        SrsRawAacStreamCodec codec;
        codec.sound_format = sound_format;
        codec.sound_rate = sound_rate;
        codec.sound_size = sound_size;
        codec.sound_type = sound_type;
        codec.aac_packet_type = 0;

        // for other data, directly write frame.
        return srs_write_audio_raw_frame(context, frame, frame_size, &codec, timestamp);
    }
    
    
    return ret;
}

/**
* whether aac raw data is in adts format,
* which bytes sequence matches '1111 1111 1111'B, that is 0xFFF.
*/
srs_bool srs_aac_is_adts(char* aac_raw_data, int ac_raw_size)
{
    SrsStream stream;
    if (stream.initialize(aac_raw_data, ac_raw_size) != ERROR_SUCCESS) {
        return false;
    }
    
    return srs_aac_startswith_adts(&stream);
}

/**
* parse the adts header to get the frame size.
*/
int srs_aac_adts_frame_size(char* aac_raw_data, int ac_raw_size)
{
    int size = -1;
    
    if (!srs_aac_is_adts(aac_raw_data, ac_raw_size)) {
        return size;
    }
    
    // adts always 7bytes.
    if (ac_raw_size <= 7) {
        return size;
    }
    
    // last 2bits
    int16_t ch3 = aac_raw_data[3];
    // whole 8bits
    int16_t ch4 = aac_raw_data[4];
    // first 3bits
    int16_t ch5 = aac_raw_data[5];
    
    size = ((ch3 << 11) & 0x1800) | ((ch4 << 3) & 0x07f8) | ((ch5 >> 5) & 0x0007);
    
    return size;
}
    
/**
* write h264 IPB-frame.
*/
int srs_write_h264_ipb_frame(Context* context, 
    char* frame, int frame_size, u_int32_t dts, u_int32_t pts
) {
    int ret = ERROR_SUCCESS;
    
    // when sps or pps not sent, ignore the packet.
    // @see https://github.com/simple-rtmp-server/srs/issues/203
    if (!context->h264_sps_pps_sent) {
        return ERROR_H264_DROP_BEFORE_SPS_PPS;
    }
    
    // 5bits, 7.3.1 NAL unit syntax,
    // H.264-AVC-ISO_IEC_14496-10.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(frame[0] & 0x1f);
    
    // for IDR frame, the frame is keyframe.
    SrsCodecVideoAVCFrame frame_type = SrsCodecVideoAVCFrameInterFrame;
    if (nal_unit_type == SrsAvcNaluTypeIDR) {
        frame_type = SrsCodecVideoAVCFrameKeyFrame;
    }
    
    std::string ibp;
    if ((ret = context->avc_raw.mux_ipb_frame(frame, frame_size, ibp)) != ERROR_SUCCESS) {
        return ret;
    }
    
    int8_t avc_packet_type = SrsCodecVideoAVCTypeNALU;
    char* flv = NULL;
    int nb_flv = 0;
    if ((ret = context->avc_raw.mux_avc2flv(ibp, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // the timestamp in rtmp message header is dts.
    u_int32_t timestamp = dts;
    return srs_rtmp_write_packet(context, SRS_RTMP_TYPE_VIDEO, timestamp, flv, nb_flv);
}

/**
* write the h264 sps/pps in context over RTMP.
*/
int srs_write_h264_sps_pps(Context* context, u_int32_t dts, u_int32_t pts)
{
    int ret = ERROR_SUCCESS;
    
    // only send when both sps and pps changed.
    if (!context->h264_sps_changed || !context->h264_pps_changed) {
        return ret;
    }
    
    // h264 raw to h264 packet.
    std::string sh;
    if ((ret = context->avc_raw.mux_sequence_header(context->h264_sps, context->h264_pps, dts, pts, sh)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // h264 packet to flv packet.
    int8_t frame_type = SrsCodecVideoAVCFrameKeyFrame;
    int8_t avc_packet_type = SrsCodecVideoAVCTypeSequenceHeader;
    char* flv = NULL;
    int nb_flv = 0;
    if ((ret = context->avc_raw.mux_avc2flv(sh, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // reset sps and pps.
    context->h264_sps_changed = false;
    context->h264_pps_changed = false;
    context->h264_sps_pps_sent = true;
    
    // the timestamp in rtmp message header is dts.
    u_int32_t timestamp = dts;
    return srs_rtmp_write_packet(context, SRS_RTMP_TYPE_VIDEO, timestamp, flv, nb_flv);
}

/**
* write h264 raw frame, maybe sps/pps/IPB-frame.
*/
int srs_write_h264_raw_frame(Context* context, 
    char* frame, int frame_size, u_int32_t dts, u_int32_t pts
) {
    int ret = ERROR_SUCCESS;

    // for sps
    if (context->avc_raw.is_sps(frame, frame_size)) {
        std::string sps;
        if ((ret = context->avc_raw.sps_demux(frame, frame_size, sps)) != ERROR_SUCCESS) {
            return ret;
        }
        
        if (context->h264_sps == sps) {
            return ERROR_H264_DUPLICATED_SPS;
        }
        context->h264_sps_changed = true;
        context->h264_sps = sps;
        
        return srs_write_h264_sps_pps(context, dts, pts);
    }

    // for pps
    if (context->avc_raw.is_pps(frame, frame_size)) {
        std::string pps;
        if ((ret = context->avc_raw.pps_demux(frame, frame_size, pps)) != ERROR_SUCCESS) {
            return ret;
        }
        
        if (context->h264_pps == pps) {
            return ERROR_H264_DUPLICATED_PPS;
        }
        context->h264_pps_changed = true;
        context->h264_pps = pps;
        
        return srs_write_h264_sps_pps(context, dts, pts);
    }

    // ibp frame.
    return srs_write_h264_ipb_frame(context, frame, frame_size, dts, pts);
}

/**
* write h264 multiple frames, in annexb format.
*/
int srs_h264_write_raw_frames(srs_rtmp_t rtmp, 
    char* frames, int frames_size, u_int32_t dts, u_int32_t pts
) {
    int ret = ERROR_SUCCESS;
    
    srs_assert(frames != NULL);
    srs_assert(frames_size > 0);
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    if ((ret = context->h264_raw_stream.initialize(frames, frames_size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // use the last error
    // @see https://github.com/simple-rtmp-server/srs/issues/203
    // @see https://github.com/simple-rtmp-server/srs/issues/204
    int error_code_return = ret;
    
    // send each frame.
    while (!context->h264_raw_stream.empty()) {
        char* frame = NULL;
        int frame_size = 0;
        if ((ret = context->avc_raw.annexb_demux(&context->h264_raw_stream, &frame, &frame_size)) != ERROR_SUCCESS) {
            return ret;
        }
    
        // ignore invalid frame,
        // atleast 1bytes for SPS to decode the type
        if (frame_size <= 0) {
            continue;
        }

        // it may be return error, but we must process all packets.
        if ((ret = srs_write_h264_raw_frame(context, frame, frame_size, dts, pts)) != ERROR_SUCCESS) {
            error_code_return = ret;
            
            // ignore known error, process all packets.
            if (srs_h264_is_dvbsp_error(ret)
                || srs_h264_is_duplicated_sps_error(ret)
                || srs_h264_is_duplicated_pps_error(ret)
            ) {
                continue;
            }
            
            return ret;
        }
    }
    
    return error_code_return;
}

srs_bool srs_h264_is_dvbsp_error(int error_code)
{
    return error_code == ERROR_H264_DROP_BEFORE_SPS_PPS;
}

srs_bool srs_h264_is_duplicated_sps_error(int error_code)
{
    return error_code == ERROR_H264_DUPLICATED_SPS;
}

srs_bool srs_h264_is_duplicated_pps_error(int error_code)
{
    return error_code == ERROR_H264_DUPLICATED_PPS;
}

srs_bool srs_h264_startswith_annexb(char* h264_raw_data, int h264_raw_size, int* pnb_start_code)
{
    SrsStream stream;
    if (stream.initialize(h264_raw_data, h264_raw_size) != ERROR_SUCCESS) {
        return false;
    }
    
    return srs_avc_startswith_annexb(&stream, pnb_start_code);
}

struct FlvContext
{
    SrsFileReader reader;
    SrsFileWriter writer;
    SrsFlvEncoder enc;
    SrsFlvDecoder dec;
};

srs_flv_t srs_flv_open_read(const char* file)
{
    int ret = ERROR_SUCCESS;
    
    FlvContext* flv = new FlvContext();
    
    if ((ret = flv->reader.open(file)) != ERROR_SUCCESS) {
        srs_freep(flv);
        return NULL;
    }
    
    if ((ret = flv->dec.initialize(&flv->reader)) != ERROR_SUCCESS) {
        srs_freep(flv);
        return NULL;
    }
    
    return flv;
}

srs_flv_t srs_flv_open_write(const char* file)
{
    int ret = ERROR_SUCCESS;
    
    FlvContext* flv = new FlvContext();
    
    if ((ret = flv->writer.open(file)) != ERROR_SUCCESS) {
        srs_freep(flv);
        return NULL;
    }
    
    if ((ret = flv->enc.initialize(&flv->writer)) != ERROR_SUCCESS) {
        srs_freep(flv);
        return NULL;
    }
    
    return flv;
}

void srs_flv_close(srs_flv_t flv)
{
    FlvContext* context = (FlvContext*)flv;
    srs_freep(context);
}

int srs_flv_read_header(srs_flv_t flv, char header[9])
{
    int ret = ERROR_SUCCESS;
    
    FlvContext* context = (FlvContext*)flv;

    if (!context->reader.is_open()) {
        return ERROR_SYSTEM_IO_INVALID;
    }
    
    if ((ret = context->dec.read_header(header)) != ERROR_SUCCESS) {
        return ret;
    }
    
    char ts[4]; // tag size
    if ((ret = context->dec.read_previous_tag_size(ts)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int srs_flv_read_tag_header(srs_flv_t flv, char* ptype, int32_t* pdata_size, u_int32_t* ptime)
{
    int ret = ERROR_SUCCESS;
    
    FlvContext* context = (FlvContext*)flv;

    if (!context->reader.is_open()) {
        return ERROR_SYSTEM_IO_INVALID;
    }
    
    if ((ret = context->dec.read_tag_header(ptype, pdata_size, ptime)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int srs_flv_read_tag_data(srs_flv_t flv, char* data, int32_t size)
{
    int ret = ERROR_SUCCESS;
    
    FlvContext* context = (FlvContext*)flv;

    if (!context->reader.is_open()) {
        return ERROR_SYSTEM_IO_INVALID;
    }
    
    if ((ret = context->dec.read_tag_data(data, size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    char ts[4]; // tag size
    if ((ret = context->dec.read_previous_tag_size(ts)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int srs_flv_write_header(srs_flv_t flv, char header[9])
{
    int ret = ERROR_SUCCESS;
    
    FlvContext* context = (FlvContext*)flv;

    if (!context->writer.is_open()) {
        return ERROR_SYSTEM_IO_INVALID;
    }
    
    if ((ret = context->enc.write_header(header)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int srs_flv_write_tag(srs_flv_t flv, char type, int32_t time, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    FlvContext* context = (FlvContext*)flv;

    if (!context->writer.is_open()) {
        return ERROR_SYSTEM_IO_INVALID;
    }
    
    if (type == SRS_RTMP_TYPE_AUDIO) {
        return context->enc.write_audio(time, data, size);
    } else if (type == SRS_RTMP_TYPE_VIDEO) {
        return context->enc.write_video(time, data, size);
    } else {
        return context->enc.write_metadata(type, data, size);
    }

    return ret;
}

int srs_flv_size_tag(int data_size)
{
    return SrsFlvEncoder::size_tag(data_size);
}

int64_t srs_flv_tellg(srs_flv_t flv)
{
    FlvContext* context = (FlvContext*)flv;
    return context->reader.tellg();
}

void srs_flv_lseek(srs_flv_t flv, int64_t offset)
{
    FlvContext* context = (FlvContext*)flv;
    context->reader.lseek(offset);
}

srs_bool srs_flv_is_eof(int error_code)
{
    return error_code == ERROR_SYSTEM_FILE_EOF;
}

srs_bool srs_flv_is_sequence_header(char* data, int32_t size)
{
    return SrsFlvCodec::video_is_sequence_header(data, (int)size);
}

srs_bool srs_flv_is_keyframe(char* data, int32_t size)
{
    return SrsFlvCodec::video_is_keyframe(data, (int)size);
}

srs_amf0_t srs_amf0_parse(char* data, int size, int* nparsed)
{
    int ret = ERROR_SUCCESS;
    
    srs_amf0_t amf0 = NULL;
    
    SrsStream stream;
    if ((ret = stream.initialize(data, size)) != ERROR_SUCCESS) {
        return amf0;
    }
    
    SrsAmf0Any* any = NULL;
    if ((ret = SrsAmf0Any::discovery(&stream, &any)) != ERROR_SUCCESS) {
        return amf0;
    }
    
    stream.skip(-1 * stream.pos());
    if ((ret = any->read(&stream)) != ERROR_SUCCESS) {
        srs_freep(any);
        return amf0;
    }
    
    if (nparsed) {
        *nparsed = stream.pos();
    }
    amf0 = (srs_amf0_t)any;
    
    return amf0;
}

srs_amf0_t srs_amf0_create_string(const char* value)
{
    return SrsAmf0Any::str(value);
}

srs_amf0_t srs_amf0_create_number(srs_amf0_number value)
{
    return SrsAmf0Any::number(value);
}

srs_amf0_t srs_amf0_create_ecma_array()
{
    return SrsAmf0Any::ecma_array();
}

srs_amf0_t srs_amf0_create_strict_array()
{
    return SrsAmf0Any::strict_array();
}

srs_amf0_t srs_amf0_create_object()
{
    return SrsAmf0Any::object();
}

srs_amf0_t srs_amf0_ecma_array_to_object(srs_amf0_t ecma_arr)
{
    srs_assert(srs_amf0_is_ecma_array(ecma_arr));

    SrsAmf0EcmaArray* arr = (SrsAmf0EcmaArray*)ecma_arr;
    SrsAmf0Object* obj = SrsAmf0Any::object();
    
    for (int i = 0; i < arr->count(); i++) {
        std::string key = arr->key_at(i);
        SrsAmf0Any* value = arr->value_at(i);
        obj->set(key, value->copy());
    }
    
    return obj;
}

void srs_amf0_free(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_freep(any);
}

int srs_amf0_size(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->total_size();
}

int srs_amf0_serialize(srs_amf0_t amf0, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    
    SrsStream stream;
    if ((ret = stream.initialize(data, size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = any->write(&stream)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

srs_bool srs_amf0_is_string(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_string();
}

srs_bool srs_amf0_is_boolean(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_boolean();
}

srs_bool srs_amf0_is_number(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_number();
}

srs_bool srs_amf0_is_null(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_null();
}

srs_bool srs_amf0_is_object(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_object();
}

srs_bool srs_amf0_is_ecma_array(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_ecma_array();
}

srs_bool srs_amf0_is_strict_array(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_strict_array();
}

const char* srs_amf0_to_string(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->to_str_raw();
}

srs_bool srs_amf0_to_boolean(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->to_boolean();
}

srs_amf0_number srs_amf0_to_number(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->to_number();
}

void srs_amf0_set_number(srs_amf0_t amf0, srs_amf0_number value)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    any->set_number(value);
}

int srs_amf0_object_property_count(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_object());

    SrsAmf0Object* obj = (SrsAmf0Object*)amf0;
    return obj->count();
}

const char* srs_amf0_object_property_name_at(srs_amf0_t amf0, int index)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_object());

    SrsAmf0Object* obj = (SrsAmf0Object*)amf0;
    return obj->key_raw_at(index);
}

srs_amf0_t srs_amf0_object_property_value_at(srs_amf0_t amf0, int index)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_object());

    SrsAmf0Object* obj = (SrsAmf0Object*)amf0;
    return (srs_amf0_t)obj->value_at(index);
}

srs_amf0_t srs_amf0_object_property(srs_amf0_t amf0, const char* name)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_object());

    SrsAmf0Object* obj = (SrsAmf0Object*)amf0;
    return (srs_amf0_t)obj->get_property(name);
}

void srs_amf0_object_property_set(srs_amf0_t amf0, const char* name, srs_amf0_t value)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_object());

    SrsAmf0Object* obj = (SrsAmf0Object*)amf0;
    any = (SrsAmf0Any*)value;
    obj->set(name, any);
}

void srs_amf0_object_clear(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_object());

    SrsAmf0Object* obj = (SrsAmf0Object*)amf0;
    obj->clear();
}

int srs_amf0_ecma_array_property_count(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_ecma_array());

    SrsAmf0EcmaArray * obj = (SrsAmf0EcmaArray*)amf0;
    return obj->count();
}

const char* srs_amf0_ecma_array_property_name_at(srs_amf0_t amf0, int index)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_ecma_array());

    SrsAmf0EcmaArray* obj = (SrsAmf0EcmaArray*)amf0;
    return obj->key_raw_at(index);
}

srs_amf0_t srs_amf0_ecma_array_property_value_at(srs_amf0_t amf0, int index)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_ecma_array());

    SrsAmf0EcmaArray* obj = (SrsAmf0EcmaArray*)amf0;
    return (srs_amf0_t)obj->value_at(index);
}

srs_amf0_t srs_amf0_ecma_array_property(srs_amf0_t amf0, const char* name)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_ecma_array());

    SrsAmf0EcmaArray* obj = (SrsAmf0EcmaArray*)amf0;
    return (srs_amf0_t)obj->get_property(name);
}

void srs_amf0_ecma_array_property_set(srs_amf0_t amf0, const char* name, srs_amf0_t value)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_ecma_array());

    SrsAmf0EcmaArray* obj = (SrsAmf0EcmaArray*)amf0;
    any = (SrsAmf0Any*)value;
    obj->set(name, any);
}

int srs_amf0_strict_array_property_count(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_strict_array());

    SrsAmf0StrictArray * obj = (SrsAmf0StrictArray*)amf0;
    return obj->count();
}

srs_amf0_t srs_amf0_strict_array_property_at(srs_amf0_t amf0, int index)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_strict_array());

    SrsAmf0StrictArray* obj = (SrsAmf0StrictArray*)amf0;
    return (srs_amf0_t)obj->at(index);
}

void srs_amf0_strict_array_append(srs_amf0_t amf0, srs_amf0_t value)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_strict_array());

    SrsAmf0StrictArray* obj = (SrsAmf0StrictArray*)amf0;
    any = (SrsAmf0Any*)value;
    obj->append(any);
}

int64_t srs_utils_time_ms()
{
    return srs_update_system_time_ms();
}

int64_t srs_utils_send_bytes(srs_rtmp_t rtmp)
{
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    return context->rtmp->get_send_bytes();
}

int64_t srs_utils_recv_bytes(srs_rtmp_t rtmp)
{
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    return context->rtmp->get_recv_bytes();
}

int srs_utils_parse_timestamp(
    u_int32_t time, char type, char* data, int size,
    u_int32_t* ppts
) {
    int ret = ERROR_SUCCESS;
    
    if (type != SRS_RTMP_TYPE_VIDEO) {
        *ppts = time;
        return ret;
    }

    if (!SrsFlvCodec::video_is_h264(data, size)) {
        return ERROR_FLV_INVALID_VIDEO_TAG;
    }

    if (SrsFlvCodec::video_is_sequence_header(data, size)) {
        *ppts = time;
        return ret;
    }
    
    // 1bytes, frame type and codec id.
    // 1bytes, avc packet type.
    // 3bytes, cts, composition time,
    //      pts = dts + cts, or 
    //      cts = pts - dts.
    if (size < 5) {
        return ERROR_FLV_INVALID_VIDEO_TAG;
    }
    
    u_int32_t cts = 0;
    char* p = data + 2;
    char* pp = (char*)&cts;
    pp[2] = *p++;
    pp[1] = *p++;
    pp[0] = *p++;

    *ppts = time + cts;
    
    return ret;
}
    
srs_bool srs_utils_flv_tag_is_ok(char type)
{
    return type == SRS_RTMP_TYPE_AUDIO || type == SRS_RTMP_TYPE_VIDEO || type == SRS_RTMP_TYPE_SCRIPT;
}

srs_bool srs_utils_flv_tag_is_audio(char type)
{
    return type == SRS_RTMP_TYPE_AUDIO;
}
    
srs_bool srs_utils_flv_tag_is_video(char type)
{
    return type == SRS_RTMP_TYPE_VIDEO;
}

srs_bool srs_utils_flv_tag_is_av(char type)
{
    return type == SRS_RTMP_TYPE_AUDIO || type == SRS_RTMP_TYPE_VIDEO;
}

char srs_utils_flv_video_codec_id(char* data, int size)
{
    if (size < 1) {
        return 0;
    }

    char codec_id = data[0];
    codec_id = codec_id & 0x0F;
    
    return codec_id;
}

char srs_utils_flv_video_avc_packet_type(char* data, int size)
{
    if (size < 2) {
        return -1;
    }
    
    if (!SrsFlvCodec::video_is_h264(data, size)) {
        return -1;
    }
    
    u_int8_t avc_packet_type = data[1];
    
    if (avc_packet_type > 2) {
        return -1;
    }
    
    return avc_packet_type;
}

char srs_utils_flv_video_frame_type(char* data, int size)
{
    if (size < 1) {
        return -1;
    }
    
    if (!SrsFlvCodec::video_is_h264(data, size)) {
        return -1;
    }
    
    u_int8_t frame_type = data[0];
    frame_type = (frame_type >> 4) & 0x0f;
    if (frame_type < 1 || frame_type > 5) {
        return -1;
    }
    
    return frame_type;
}

char srs_utils_flv_audio_sound_format(char* data, int size)
{
    if (size < 1) {
        return -1;
    }
    
    u_int8_t sound_format = data[0];
    sound_format = (sound_format >> 4) & 0x0f;
    if (sound_format > 15 || sound_format == 12 || sound_format == 13) {
        return -1;
    }
    
    return sound_format;
}

char srs_utils_flv_audio_sound_rate(char* data, int size)
{
    if (size < 1) {
        return -1;
    }
    
    u_int8_t sound_rate = data[0];
    sound_rate = (sound_rate >> 2) & 0x03;
    if (sound_rate > 3) {
        return -1;
    }
    
    return sound_rate;
}

char srs_utils_flv_audio_sound_size(char* data, int size)
{
    if (size < 1) {
        return -1;
    }
    
    u_int8_t sound_size = data[0];
    sound_size = (sound_size >> 1) & 0x01;
    if (sound_size > 1) {
        return -1;
    }
    
    return sound_size;
}

char srs_utils_flv_audio_sound_type(char* data, int size)
{
    if (size < 1) {
        return -1;
    }
    
    u_int8_t sound_type = data[0];
    sound_type = sound_type & 0x01;
    if (sound_type > 1) {
        return -1;
    }
    
    return sound_type;
}

char srs_utils_flv_audio_aac_packet_type(char* data, int size)
{
    if (size < 2) {
        return -1;
    }
    
    if (srs_utils_flv_audio_sound_format(data, size) != 10) {
        return -1;
    }
    
    u_int8_t aac_packet_type = data[1];
    if (aac_packet_type > 1) {
        return -1;
    }
    
    return aac_packet_type;
}

char* srs_human_amf0_print(srs_amf0_t amf0, char** pdata, int* psize)
{
    if (!amf0) {
        return NULL;
    }
    
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    
    return any->human_print(pdata, psize);
}

const char* srs_human_flv_tag_type2string(char type)
{
    static const char* audio = "Audio";
    static const char* video = "Video";
    static const char* data = "Data";
    static const char* unknown = "Unknown";
    
    switch (type) {
        case SRS_RTMP_TYPE_AUDIO: return audio;
        case SRS_RTMP_TYPE_VIDEO: return video;
        case SRS_RTMP_TYPE_SCRIPT: return data;
        default: return unknown;
    }
    
    return unknown;
}

const char* srs_human_flv_video_codec_id2string(char codec_id)
{
    static const char* h263 = "H.263";
    static const char* screen = "Screen";
    static const char* vp6 = "VP6";
    static const char* vp6_alpha = "VP6Alpha";
    static const char* screen2 = "Screen2";
    static const char* h264 = "H.264";
    static const char* unknown = "Unknown";
    
    switch (codec_id) {
        case 2: return h263;
        case 3: return screen;
        case 4: return vp6;
        case 5: return vp6_alpha;
        case 6: return screen2;
        case 7: return h264;
        default: return unknown;
    }
    
    return unknown;
}

const char* srs_human_flv_video_avc_packet_type2string(char avc_packet_type)
{
    static const char* sps_pps = "SH";
    static const char* nalu = "Nalu";
    static const char* sps_pps_end = "SpsPpsEnd";
    static const char* unknown = "Unknown";
    
    switch (avc_packet_type) {
        case 0: return sps_pps;
        case 1: return nalu;
        case 2: return sps_pps_end;
        default: return unknown;
    }
    
    return unknown;
}

const char* srs_human_flv_video_frame_type2string(char frame_type)
{
    static const char* keyframe = "I";
    static const char* interframe = "P/B";
    static const char* disposable_interframe = "DI";
    static const char* generated_keyframe = "GI";
    static const char* video_infoframe = "VI";
    static const char* unknown = "Unknown";
    
    switch (frame_type) {
        case 1: return keyframe;
        case 2: return interframe;
        case 3: return disposable_interframe;
        case 4: return generated_keyframe;
        case 5: return video_infoframe;
        default: return unknown;
    }
    
    return unknown;
}

const char* srs_human_flv_audio_sound_format2string(char sound_format)
{
    static const char* linear_pcm = "LinearPCM";
    static const char* ad_pcm = "ADPCM";
    static const char* mp3 = "MP3";
    static const char* linear_pcm_le = "LinearPCMLe";
    static const char* nellymoser_16khz = "NellymoserKHz16";
    static const char* nellymoser_8khz = "NellymoserKHz8";
    static const char* nellymoser = "Nellymoser";
    static const char* g711_a_pcm = "G711APCM";
    static const char* g711_mu_pcm = "G711MuPCM";
    static const char* reserved = "Reserved";
    static const char* aac = "AAC";
    static const char* speex = "Speex";
    static const char* mp3_8khz = "MP3KHz8";
    static const char* device_specific = "DeviceSpecific";
    static const char* unknown = "Unknown";
    
    switch (sound_format) {
        case 0: return linear_pcm;
        case 1: return ad_pcm;
        case 2: return mp3;
        case 3: return linear_pcm_le;
        case 4: return nellymoser_16khz;
        case 5: return nellymoser_8khz;
        case 6: return nellymoser;
        case 7: return g711_a_pcm;
        case 8: return g711_mu_pcm;
        case 9: return reserved;
        case 10: return aac;
        case 11: return speex;
        case 14: return mp3_8khz;
        case 15: return device_specific;
        default: return unknown;
    }
    
    return unknown;
}

const char* srs_human_flv_audio_sound_rate2string(char sound_rate)
{
    static const char* khz_5_5 = "5.5KHz";
    static const char* khz_11 = "11KHz";
    static const char* khz_22 = "22KHz";
    static const char* khz_44 = "44KHz";
    static const char* unknown = "Unknown";
    
    switch (sound_rate) {
        case 0: return khz_5_5;
        case 1: return khz_11;
        case 2: return khz_22;
        case 3: return khz_44;
        default: return unknown;
    }
    
    return unknown;
}

const char* srs_human_flv_audio_sound_size2string(char sound_size)
{
    static const char* bit_8 = "8bit";
    static const char* bit_16 = "16bit";
    static const char* unknown = "Unknown";
    
    switch (sound_size) {
        case 0: return bit_8;
        case 1: return bit_16;
        default: return unknown;
    }
    
    return unknown;
}

const char* srs_human_flv_audio_sound_type2string(char sound_type)
{
    static const char* mono = "Mono";
    static const char* stereo = "Stereo";
    static const char* unknown = "Unknown";
    
    switch (sound_type) {
        case 0: return mono;
        case 1: return stereo;
        default: return unknown;
    }
    
    return unknown;
}

const char* srs_human_flv_audio_aac_packet_type2string(char aac_packet_type)
{
    static const char* sps_pps = "SH";
    static const char* raw = "Raw";
    static const char* unknown = "Unknown";
    
    switch (aac_packet_type) {
        case 0: return sps_pps;
        case 1: return raw;
        default: return unknown;
    }
    
    return unknown;
}
    
int srs_human_print_rtmp_packet(char type, u_int32_t timestamp, char* data, int size)
{
    return srs_human_print_rtmp_packet2(type, timestamp, data, size, 0);
}

int srs_human_print_rtmp_packet2(char type, u_int32_t timestamp, char* data, int size, u_int32_t pre_timestamp)
{
    return srs_human_print_rtmp_packet3(type, timestamp, data, size, pre_timestamp, 0);
}
    
int srs_human_print_rtmp_packet3(char type, u_int32_t timestamp, char* data, int size, u_int32_t pre_timestamp, int64_t pre_now)
{
    return srs_human_print_rtmp_packet4(type, timestamp, data, size, pre_timestamp, pre_now, 0, 0);
}
    
int srs_human_print_rtmp_packet4(char type, u_int32_t timestamp, char* data, int size, u_int32_t pre_timestamp, int64_t pre_now, int64_t starttime, int64_t nb_packets)
{
    int ret = ERROR_SUCCESS;
    
    // packets interval in milliseconds.
    double pi = 0;
    if (pre_now > starttime) {
        pi = (pre_now - starttime) / (double)nb_packets;
    }
    
    // global fps(video and audio mixed fps).
    double gfps = 0;
    if (pi > 0) {
        gfps = 1000 / pi;
    }
    
    int diff = 0;
    if (pre_timestamp > 0) {
        diff = (int)timestamp - (int)pre_timestamp;
    }
    
    int ndiff = 0;
    if (pre_now > 0) {
        ndiff = (int)(srs_utils_time_ms() - pre_now);
    }
    
    u_int32_t pts;
    if (srs_utils_parse_timestamp(timestamp, type, data, size, &pts) != 0) {
        srs_human_trace("Rtmp packet id=%"PRId64"/%.1f/%.1f, type=%s, dts=%d, diff=%d, ndiff=%d, size=%d, DecodeError",
            nb_packets, pi, gfps, srs_human_flv_tag_type2string(type), timestamp, diff, ndiff, size
        );
        return ret;
    }
    
    if (type == SRS_RTMP_TYPE_VIDEO) {
        srs_human_trace("Video packet id=%"PRId64"/%.1f/%.1f, type=%s, dts=%d, pts=%d, diff=%d, ndiff=%d, size=%d, %s(%s,%s)",
            nb_packets, pi, gfps, srs_human_flv_tag_type2string(type), timestamp, pts, diff, ndiff, size,
            srs_human_flv_video_codec_id2string(srs_utils_flv_video_codec_id(data, size)),
            srs_human_flv_video_avc_packet_type2string(srs_utils_flv_video_avc_packet_type(data, size)),
            srs_human_flv_video_frame_type2string(srs_utils_flv_video_frame_type(data, size))
        );
    } else if (type == SRS_RTMP_TYPE_AUDIO) {
        srs_human_trace("Audio packet id=%"PRId64"/%.1f/%.1f, type=%s, dts=%d, pts=%d, diff=%d, ndiff=%d, size=%d, %s(%s,%s,%s,%s)",
            nb_packets, pi, gfps, srs_human_flv_tag_type2string(type), timestamp, pts, diff, ndiff, size,
            srs_human_flv_audio_sound_format2string(srs_utils_flv_audio_sound_format(data, size)),
            srs_human_flv_audio_sound_rate2string(srs_utils_flv_audio_sound_rate(data, size)),
            srs_human_flv_audio_sound_size2string(srs_utils_flv_audio_sound_size(data, size)),
            srs_human_flv_audio_sound_type2string(srs_utils_flv_audio_sound_type(data, size)),
            srs_human_flv_audio_aac_packet_type2string(srs_utils_flv_audio_aac_packet_type(data, size))
        );
    } else if (type == SRS_RTMP_TYPE_SCRIPT) {
        srs_human_verbose("Data packet id=%"PRId64"/%.1f/%.1f, type=%s, time=%d, diff=%d, ndiff=%d, size=%d",
            nb_packets, pi, gfps, srs_human_flv_tag_type2string(type), timestamp, diff, ndiff, size);
        int nparsed = 0;
        while (nparsed < size) {
            int nb_parsed_this = 0;
            srs_amf0_t amf0 = srs_amf0_parse(data + nparsed, size - nparsed, &nb_parsed_this);
            if (amf0 == NULL) {
                break;
            }
    
            nparsed += nb_parsed_this;
            
            char* amf0_str = NULL;
            srs_human_raw("%s", srs_human_amf0_print(amf0, &amf0_str, NULL));
            srs_freep(amf0_str);
        }
    } else {
        srs_human_trace("Rtmp packet id=%"PRId64"/%.1f/%.1f, type=%#x, dts=%d, pts=%d, diff=%d, ndiff=%d, size=%d",
            nb_packets, pi, gfps, type, timestamp, pts, diff, ndiff, size);
    }
    
    return ret;
}

const char* srs_human_format_time()
{
    struct timeval tv;
    static char buf[23];
    
    memset(buf, 0, sizeof(buf));
    
    // clock time
    if (gettimeofday(&tv, NULL) == -1) {
        return buf;
    }
    
    // to calendar time
    struct tm* tm;
    if ((tm = localtime((const time_t*)&tv.tv_sec)) == NULL) {
        return buf;
    }
    
    snprintf(buf, sizeof(buf), 
        "%d-%02d-%02d %02d:%02d:%02d.%03d", 
        1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday, 
        tm->tm_hour, tm->tm_min, tm->tm_sec, 
        (int)(tv.tv_usec / 1000));
        
    // for srs-librtmp, @see https://github.com/simple-rtmp-server/srs/issues/213
    buf[sizeof(buf) - 1] = 0;
    
    return buf;
}


#ifdef SRS_HIJACK_IO
srs_hijack_io_t srs_hijack_io_get(srs_rtmp_t rtmp)
{
    if (!rtmp) {
        return NULL;
    }
    
    Context* context = (Context*)rtmp;
    if (!context->skt) {
        return NULL;
    }
    
    return context->skt->hijack_io();
}
#endif

#ifdef __cplusplus
}
#endif

