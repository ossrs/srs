/*
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "st.h"

#define IOBUFSIZE (16*1024)

#define IOV_LEN   256
#define IOV_COUNT (IOBUFSIZE / IOV_LEN)

#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffff
#endif

static char *prog;                     /* Program name   */
static struct sockaddr_in rmt_addr;    /* Remote address */

static unsigned long testing;
#define TESTING_VERBOSE		0x1
#define TESTING_READV		0x2
#define	TESTING_READ_RESID	0x4
#define TESTING_WRITEV		0x8
#define TESTING_WRITE_RESID	0x10

static void read_address(const char *str, struct sockaddr_in *sin);
static void start_daemon(void);
static int  cpu_count(void);
static void set_concurrency(int nproc);
static void *handle_request(void *arg);
static void print_sys_error(const char *msg);


/*
 * This program acts as a generic gateway. It listens for connections
 * to a local address ('-l' option). Upon accepting a client connection,
 * it connects to the specified remote address ('-r' option) and then
 * just pumps the data through without any modification.
 */
int main(int argc, char *argv[])
{
  extern char *optarg;
  int opt, sock, n;
  int laddr, raddr, num_procs, alt_ev, one_process;
  int serialize_accept = 0;
  struct sockaddr_in lcl_addr, cli_addr;
  st_netfd_t cli_nfd, srv_nfd;

  prog = argv[0];
  num_procs = laddr = raddr = alt_ev = one_process = 0;

  /* Parse arguments */
  while((opt = getopt(argc, argv, "l:r:p:Saht:X")) != EOF) {
    switch (opt) {
    case 'a':
      alt_ev = 1;
      break;
    case 'l':
      read_address(optarg, &lcl_addr);
      laddr = 1;
      break;
    case 'r':
      read_address(optarg, &rmt_addr);
      if (rmt_addr.sin_addr.s_addr == INADDR_ANY) {
	fprintf(stderr, "%s: invalid remote address: %s\n", prog, optarg);
	exit(1);
      }
      raddr = 1;
      break;
    case 'p':
      num_procs = atoi(optarg);
      if (num_procs < 1) {
	fprintf(stderr, "%s: invalid number of processes: %s\n", prog, optarg);
	exit(1);
      }
      break;
    case 'S':
      /*
       * Serialization decision is tricky on some platforms. For example,
       * Solaris 2.6 and above has kernel sockets implementation, so supposedly
       * there is no need for serialization. The ST library may be compiled
       * on one OS version, but used on another, so the need for serialization
       * should be determined at run time by the application. Since it's just
       * an example, the serialization decision is left up to user.
       * Only on platforms where the serialization is never needed on any OS
       * version st_netfd_serialize_accept() is a no-op.
       */
      serialize_accept = 1;
      break;
    case 't':
      testing = strtoul(optarg, NULL, 0);
      break;
    case 'X':
      one_process = 1;
      break;
    case 'h':
    case '?':
      fprintf(stderr, "Usage: %s [options] -l <[host]:port> -r <host:port>\n",
       prog);
      fprintf(stderr, "options are:\n");
      fprintf(stderr, "  -p <num_processes>	number of parallel processes\n");
      fprintf(stderr, "  -S			serialize accepts\n");
      fprintf(stderr, "  -a			use alternate event system\n");
#ifdef DEBUG
      fprintf(stderr, "  -t mask		testing/debugging mode\n");
      fprintf(stderr, "  -X			one process, don't daemonize\n");
#endif
      exit(1);
    }
  }
  if (!laddr) {
    fprintf(stderr, "%s: local address required\n", prog);
    exit(1);
  }
  if (!raddr) {
    fprintf(stderr, "%s: remote address required\n", prog);
    exit(1);
  }
  if (num_procs == 0)
    num_procs = cpu_count();

  fprintf(stderr, "%s: starting proxy daemon on %s:%d\n", prog,
	  inet_ntoa(lcl_addr.sin_addr), ntohs(lcl_addr.sin_port));

  /* Start the daemon */
  if (one_process)
    num_procs = 1;
  else
    start_daemon();

  if (alt_ev)
    st_set_eventsys(ST_EVENTSYS_ALT);

  /* Initialize the ST library */
  if (st_init() < 0) {
    print_sys_error("st_init");
    exit(1);
  }

  /* Create and bind listening socket */
  if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    print_sys_error("socket");
    exit(1);
  }
  n = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&n, sizeof(n)) < 0) {
    print_sys_error("setsockopt");
    exit(1);
  }
  if (bind(sock, (struct sockaddr *)&lcl_addr, sizeof(lcl_addr)) < 0) {
    print_sys_error("bind");
    exit(1);
  }
  listen(sock, 128);
  if ((srv_nfd = st_netfd_open_socket(sock)) == NULL) {
    print_sys_error("st_netfd_open");
    exit(1);
  }
  /* See the comment regarding serialization decision above */
  if (num_procs > 1 && serialize_accept && st_netfd_serialize_accept(srv_nfd)
      < 0) {
    print_sys_error("st_netfd_serialize_accept");
    exit(1);
  }

  /* Start server processes */
  if (!one_process)
    set_concurrency(num_procs);

  for ( ; ; ) {
    n = sizeof(cli_addr);
    cli_nfd = st_accept(srv_nfd, (struct sockaddr *)&cli_addr, &n,
     ST_UTIME_NO_TIMEOUT);
    if (cli_nfd == NULL) {
      print_sys_error("st_accept");
      exit(1);
    }
    if (st_thread_create(handle_request, cli_nfd, 0, 0) == NULL) {
      print_sys_error("st_thread_create");
      exit(1);
    }
  }

  /* NOTREACHED */
  return 1;
}


static void read_address(const char *str, struct sockaddr_in *sin)
{
  char host[128], *p;
  struct hostent *hp;
  unsigned short port;

  strcpy(host, str);
  if ((p = strchr(host, ':')) == NULL) {
    fprintf(stderr, "%s: invalid address: %s\n", prog, host);
    exit(1);
  }
  *p++ = '\0';
  port = (unsigned short) atoi(p);
  if (port < 1) {
    fprintf(stderr, "%s: invalid port: %s\n", prog, p);
    exit(1);
  }

  memset(sin, 0, sizeof(struct sockaddr_in));
  sin->sin_family = AF_INET;
  sin->sin_port = htons(port);
  if (host[0] == '\0') {
    sin->sin_addr.s_addr = INADDR_ANY;
    return;
  }
  sin->sin_addr.s_addr = inet_addr(host);
  if (sin->sin_addr.s_addr == INADDR_NONE) {
    /* not dotted-decimal */
    if ((hp = gethostbyname(host)) == NULL) {
      fprintf(stderr, "%s: can't resolve address: %s\n", prog, host);
      exit(1);
    }
    memcpy(&sin->sin_addr, hp->h_addr, hp->h_length);
  }
}

#ifdef DEBUG
static void show_iov(const struct iovec *iov, int niov)
{
  int i;
  size_t total;

  printf("iov %p has %d entries:\n", iov, niov);
  total = 0;
  for (i = 0; i < niov; i++) {
    printf("iov[%3d] iov_base=%p iov_len=0x%lx(%lu)\n",
     i, iov[i].iov_base, (unsigned long) iov[i].iov_len,
     (unsigned long) iov[i].iov_len);
    total += iov[i].iov_len;
  }
  printf("total 0x%lx(%ld)\n", (unsigned long) total, (unsigned long) total);
}

/*
 * This version is tricked out to test all the
 * st_(read|write)v?(_resid)? variants.  Use the non-DEBUG version for
 * anything serious.  st_(read|write) are all this function really
 * needs.
 */
static int pass(st_netfd_t in, st_netfd_t out)
{
  char buf[IOBUFSIZE];
  struct iovec iov[IOV_COUNT];
  int ioviter, nw, nr;

  if (testing & TESTING_READV) {
    for (ioviter = 0; ioviter < IOV_COUNT; ioviter++) {
      iov[ioviter].iov_base = &buf[ioviter * IOV_LEN];
      iov[ioviter].iov_len = IOV_LEN;
    }
    if (testing & TESTING_VERBOSE) {
      printf("readv(%p)...\n", in);
      show_iov(iov, IOV_COUNT);
    }
    if (testing & TESTING_READ_RESID) {
      struct iovec *riov = iov;
      int riov_cnt = IOV_COUNT;
      if (st_readv_resid(in, &riov, &riov_cnt, ST_UTIME_NO_TIMEOUT) == 0) {
	if (testing & TESTING_VERBOSE) {
	  printf("resid\n");
	  show_iov(riov, riov_cnt);
	  printf("full\n");
	  show_iov(iov, IOV_COUNT);
	}
	nr = 0;
	for (ioviter = 0; ioviter < IOV_COUNT; ioviter++)
	  nr += iov[ioviter].iov_len;
	nr = IOBUFSIZE - nr;
      } else
	nr = -1;
    } else
      nr = (int) st_readv(in, iov, IOV_COUNT, ST_UTIME_NO_TIMEOUT);
  } else {
    if (testing & TESTING_READ_RESID) {
      size_t resid = IOBUFSIZE;
      if (st_read_resid(in, buf, &resid, ST_UTIME_NO_TIMEOUT) == 0)
	nr = IOBUFSIZE - resid;
      else
	nr = -1;
    } else
      nr = (int) st_read(in, buf, IOBUFSIZE, ST_UTIME_NO_TIMEOUT);
  }
  if (testing & TESTING_VERBOSE)
    printf("got 0x%x(%d) E=%d\n", nr, nr, errno);

  if (nr <= 0)
    return 0;

  if (testing & TESTING_WRITEV) {
    for (nw = 0, ioviter = 0; nw < nr;
     nw += iov[ioviter].iov_len, ioviter++) {
      iov[ioviter].iov_base = &buf[nw];
      iov[ioviter].iov_len = nr - nw;
      if (iov[ioviter].iov_len > IOV_LEN)
	iov[ioviter].iov_len = IOV_LEN;
    }
    if (testing & TESTING_VERBOSE) {
      printf("writev(%p)...\n", out);
      show_iov(iov, ioviter);
    }
    if (testing & TESTING_WRITE_RESID) {
      struct iovec *riov = iov;
      int riov_cnt = ioviter;
      if (st_writev_resid(out, &riov, &riov_cnt, ST_UTIME_NO_TIMEOUT) == 0) {
	if (testing & TESTING_VERBOSE) {
	  printf("resid\n");
	  show_iov(riov, riov_cnt);
	  printf("full\n");
	  show_iov(iov, ioviter);
	}
	nw = 0;
	while (--ioviter >= 0)
	  nw += iov[ioviter].iov_len;
	nw = nr - nw;
      } else
	nw = -1;
    } else
      nw = st_writev(out, iov, ioviter, ST_UTIME_NO_TIMEOUT);
  } else {
    if (testing & TESTING_WRITE_RESID) {
      size_t resid = nr;
      if (st_write_resid(out, buf, &resid, ST_UTIME_NO_TIMEOUT) == 0)
	nw = nr - resid;
      else
	nw = -1;
    } else
      nw = st_write(out, buf, nr, ST_UTIME_NO_TIMEOUT);
  }
  if (testing & TESTING_VERBOSE)
    printf("put 0x%x(%d) E=%d\n", nw, nw, errno);

  if (nw != nr)
    return 0;

  return 1;
}
#else /* DEBUG */
/*
 * This version is the simple one suitable for serious use.
 */
static int pass(st_netfd_t in, st_netfd_t out)
{
  char buf[IOBUFSIZE];
  int nw, nr;

  nr = (int) st_read(in, buf, IOBUFSIZE, ST_UTIME_NO_TIMEOUT);
  if (nr <= 0)
    return 0;

  nw = st_write(out, buf, nr, ST_UTIME_NO_TIMEOUT);
  if (nw != nr)
    return 0;

  return 1;
}
#endif

static void *handle_request(void *arg)
{
  struct pollfd pds[2];
  st_netfd_t cli_nfd, rmt_nfd;
  int sock;

  cli_nfd = (st_netfd_t) arg;
  pds[0].fd = st_netfd_fileno(cli_nfd);
  pds[0].events = POLLIN;

  /* Connect to remote host */
  if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
    print_sys_error("socket");
    goto done;
  }
  if ((rmt_nfd = st_netfd_open_socket(sock)) == NULL) {
    print_sys_error("st_netfd_open_socket");
    close(sock);
    goto done;
  }
  if (st_connect(rmt_nfd, (struct sockaddr *)&rmt_addr,
		 sizeof(rmt_addr), ST_UTIME_NO_TIMEOUT) < 0) {
    print_sys_error("st_connect");
    st_netfd_close(rmt_nfd);
    goto done;
  }
  pds[1].fd = sock;
  pds[1].events = POLLIN;

  /*
   * Now just pump the data through.
   * XXX This should use one thread for each direction for true full-duplex.
   */
  for ( ; ; ) {
    pds[0].revents = 0;
    pds[1].revents = 0;

    if (st_poll(pds, 2, ST_UTIME_NO_TIMEOUT) <= 0) {
      print_sys_error("st_poll");
      break;
    }

    if (pds[0].revents & POLLIN) {
      if (!pass(cli_nfd, rmt_nfd))
	break;
    }

    if (pds[1].revents & POLLIN) {
      if (!pass(rmt_nfd, cli_nfd))
	break;
    }
  }
  st_netfd_close(rmt_nfd);

done:

  st_netfd_close(cli_nfd);

  return NULL;
}

static void start_daemon(void)
{
  pid_t pid;

  /* Start forking */
  if ((pid = fork()) < 0) {
    print_sys_error("fork");
    exit(1);
  }
  if (pid > 0)
    exit(0);                        /* parent */

  /* First child process */
  setsid();                         /* become session leader */

  if ((pid = fork()) < 0) {
    print_sys_error("fork");
    exit(1);
  }
  if (pid > 0)                      /* first child */
    exit(0);

  chdir("/");
  umask(022);
}

/*
 * Create separate processes ("virtual processors"). Since it's just an
 * example, there is no watchdog - the parent just exits leaving children
 * on their own.
 */
static void set_concurrency(int nproc)
{
  pid_t pid;
  int i;

  if (nproc < 1)
    nproc = 1;

  for (i = 0; i < nproc; i++) {
    if ((pid = fork()) < 0) {
      print_sys_error("fork");
      exit(1);
    }
    /* Child returns */
    if (pid == 0)
      return;
  }

  /* Parent just exits */
  exit(0);
}

static int cpu_count(void)
{
  int n;

#if defined (_SC_NPROCESSORS_ONLN)
  n = (int) sysconf(_SC_NPROCESSORS_ONLN);
#elif defined (_SC_NPROC_ONLN)
  n = (int) sysconf(_SC_NPROC_ONLN);
#elif defined (HPUX)
#include <sys/mpctl.h>
  n = mpctl(MPC_GETNUMSPUS, 0, 0);
#else
  n = -1;
  errno = ENOSYS;
#endif

  return n;
}

static void print_sys_error(const char *msg)
{
  fprintf(stderr, "%s: %s: %s\n", prog, msg, strerror(errno));
}

