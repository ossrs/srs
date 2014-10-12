/*
 * Copyright (c) 1985, 1988, 1993
 *    The Regents of the University of California.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Portions created by SGI are Copyright (C) 2000 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met: 
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Silicon Graphics, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "stx.h"

#define MAXPACKET 1024

#if !defined(NETDB_INTERNAL) && defined(h_NETDB_INTERNAL)
#define NETDB_INTERNAL h_NETDB_INTERNAL
#endif

/* New in Solaris 7 */
#if !defined(_getshort) && defined(ns_get16)
#define _getshort(cp) ns_get16(cp)
#define _getlong(cp)  ns_get32(cp)
#endif

typedef union {
    HEADER hdr;
    u_char buf[MAXPACKET];
} querybuf_t;

int _stx_dns_ttl;


static int parse_answer(querybuf_t *ans, int len, struct in_addr *addrs,
			int *num_addrs)
{
    char buf[MAXPACKET];
    HEADER *ahp;
    u_char *cp, *eoa;
    int type, n, i;

    ahp = &ans->hdr;
    eoa = ans->buf + len;
    cp = ans->buf + sizeof(HEADER);
    h_errno = TRY_AGAIN;
    _stx_dns_ttl = -1;
    i = 0;

    while (ahp->qdcount > 0) {
	ahp->qdcount--;
	cp += dn_skipname(cp, eoa) + QFIXEDSZ;
    }
    while (ahp->ancount > 0 && cp < eoa && i < *num_addrs) {
	ahp->ancount--;
	if ((n = dn_expand(ans->buf, eoa, cp, buf, sizeof(buf))) < 0)
	    return -1;
	cp += n;
	if (cp + 4 + 4 + 2 >= eoa)
	    return -1;
	type = _getshort(cp);
	cp += 4;
	if (type == T_A)
	    _stx_dns_ttl = _getlong(cp);
	cp += 4;
	n = _getshort(cp);
	cp += 2;
	if (type == T_A) {
	    if (n > sizeof(*addrs) || cp + n > eoa)
		return -1;
	    memcpy(&addrs[i++], cp, n);
	}
	cp += n;
    }

    *num_addrs = i;
    return 0;
}


static int query_domain(st_netfd_t nfd, const char *name,
			struct in_addr *addrs, int *num_addrs,
			st_utime_t timeout)
{
    querybuf_t qbuf;
    u_char *buf = qbuf.buf;
    HEADER *hp = &qbuf.hdr;
    int blen = sizeof(qbuf);
    int i, len, id;

    for (i = 0; i < _res.nscount; i++) {
	len = res_mkquery(QUERY, name, C_IN, T_A, NULL, 0, NULL, buf, blen);
	if (len <= 0) {
	    h_errno = NO_RECOVERY;
	    return -1;
	}
	id = hp->id;

	if (st_sendto(nfd, buf, len, (struct sockaddr *)&(_res.nsaddr_list[i]),
		      sizeof(struct sockaddr), timeout) != len) {
	    h_errno = NETDB_INTERNAL;
	    /* EINTR means interrupt by other thread, NOT by a caught signal */
	    if (errno == EINTR)
		return -1;
	    continue;
	}

	/* Wait for reply */
	do {
	    len = st_recvfrom(nfd, buf, blen, NULL, NULL, timeout);
	    if (len <= 0)
		break;
	} while (id != hp->id);

	if (len < HFIXEDSZ) {
	    h_errno = NETDB_INTERNAL;
	    if (len >= 0)
		errno = EMSGSIZE;
	    else if (errno == EINTR)  /* see the comment above */
		return -1;
	    continue;
	}

	hp->ancount = ntohs(hp->ancount);
	hp->qdcount = ntohs(hp->qdcount);
	if ((hp->rcode != NOERROR) || (hp->ancount == 0)) {
	    switch (hp->rcode) {
	    case NXDOMAIN:
		h_errno = HOST_NOT_FOUND;
		break;
	    case SERVFAIL:
		h_errno = TRY_AGAIN;
		break;
	    case NOERROR:
		h_errno = NO_DATA;
		break;
	    case FORMERR:
	    case NOTIMP:
	    case REFUSED:
	    default:
		h_errno = NO_RECOVERY;
	    }
	    continue;
	}

	if (parse_answer(&qbuf, len, addrs, num_addrs) == 0)
	    return 0;
    }

    return -1;
}


#define CLOSE_AND_RETURN(ret) \
  {                           \
    n = errno;                \
    st_netfd_close(nfd);      \
    errno = n;                \
    return (ret);             \
  }


int _stx_dns_getaddrlist(const char *host, struct in_addr *addrs,
                         int *num_addrs, st_utime_t timeout)
{
    char name[MAXDNAME], **domain;
    const char *cp;
    int s, n, maxlen, dots;
    int trailing_dot, tried_as_is;
    st_netfd_t nfd;

    if ((_res.options & RES_INIT) == 0 && res_init() == -1) {
	h_errno = NETDB_INTERNAL;
	return -1;
    }
    if (_res.options & RES_USEVC) {
	h_errno = NETDB_INTERNAL;
	errno = ENOSYS;
	return -1;
    }
    if (!host || *host == '\0') {
	h_errno = HOST_NOT_FOUND;
	return -1;
    }

    /* Create UDP socket */
    if ((s = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
	h_errno = NETDB_INTERNAL;
	return -1;
    }
    if ((nfd = st_netfd_open_socket(s)) == NULL) {
	h_errno = NETDB_INTERNAL;
	n = errno;
	close(s);
	errno = n;
	return -1;
    }

    maxlen = sizeof(name) - 1;
    n = 0;
    dots = 0;
    trailing_dot = 0;
    tried_as_is = 0;

    for (cp = host; *cp && n < maxlen; cp++) {
	dots += (*cp == '.');
	name[n++] = *cp;
    }
    if (name[n - 1] == '.')
	trailing_dot = 1;

    /*
     * If there are dots in the name already, let's just give it a try
     * 'as is'.  The threshold can be set with the "ndots" option.
     */
    if (dots >= _res.ndots) {
	if (query_domain(nfd, host, addrs, num_addrs, timeout) == 0)
	    CLOSE_AND_RETURN(0);
	if (h_errno == NETDB_INTERNAL && errno == EINTR)
	    CLOSE_AND_RETURN(-1);
	tried_as_is = 1;
    }

    /*
     * We do at least one level of search if
     *     - there is no dot and RES_DEFNAME is set, or
     *     - there is at least one dot, there is no trailing dot,
     *       and RES_DNSRCH is set.
     */
    if ((!dots && (_res.options & RES_DEFNAMES)) ||
	(dots && !trailing_dot && (_res.options & RES_DNSRCH))) {
	name[n++] = '.';
	for (domain = _res.dnsrch; *domain; domain++) {
	    strncpy(name + n, *domain, maxlen - n);
	    if (query_domain(nfd, name, addrs, num_addrs, timeout) == 0)
		CLOSE_AND_RETURN(0);
	    if (h_errno == NETDB_INTERNAL && errno == EINTR)
		CLOSE_AND_RETURN(-1);
	    if (!(_res.options & RES_DNSRCH))
		break;
	}
    }

    /*
     * If we have not already tried the name "as is", do that now.
     * note that we do this regardless of how many dots were in the
     * name or whether it ends with a dot.
     */
    if (!tried_as_is) {
	if (query_domain(nfd, host, addrs, num_addrs, timeout) == 0)
	    CLOSE_AND_RETURN(0);
    }

    CLOSE_AND_RETURN(-1);
}

