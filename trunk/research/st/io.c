/* 
 * The contents of this file are subject to the Mozilla Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/MPL/
 * 
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 * 
 * The Original Code is the Netscape Portable Runtime library.
 * 
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation.  Portions created by Netscape are 
 * Copyright (C) 1994-2000 Netscape Communications Corporation.  All
 * Rights Reserved.
 * 
 * Contributor(s):  Silicon Graphics, Inc.
 * 
 * Portions created by SGI are Copyright (C) 2000-2001 Silicon
 * Graphics, Inc.  All Rights Reserved.
 * 
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License Version 2 or later (the
 * "GPL"), in which case the provisions of the GPL are applicable 
 * instead of those above.  If you wish to allow use of your 
 * version of this file only under the terms of the GPL and not to
 * allow others to use your version of this file under the MPL,
 * indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by
 * the GPL.  If you do not delete the provisions above, a recipient
 * may use your version of this file under either the MPL or the
 * GPL.
 */

/*
 * This file is derived directly from Netscape Communications Corporation,
 * and consists of extensive modifications made during the year(s) 1999-2000.
 */

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include "common.h"


#if EAGAIN != EWOULDBLOCK
#define _IO_NOT_READY_ERROR  ((errno == EAGAIN) || (errno == EWOULDBLOCK))
#else
#define _IO_NOT_READY_ERROR  (errno == EAGAIN)
#endif

#define _LOCAL_MAXIOV  16

/* File descriptor object free list */
static _st_netfd_t *_st_netfd_freelist = NULL;
/* Maximum number of file descriptors that the process can open */
static int _st_osfd_limit = -1;

static void _st_netfd_free_aux_data(_st_netfd_t *fd);

int _st_io_init(void)
{
  struct sigaction sigact;
  struct rlimit rlim;
  int fdlim;

  /* Ignore SIGPIPE */
  sigact.sa_handler = SIG_IGN;
  sigemptyset(&sigact.sa_mask);
  sigact.sa_flags = 0;
  if (sigaction(SIGPIPE, &sigact, NULL) < 0)
    return -1;

  /* Set maximum number of open file descriptors */
  if (getrlimit(RLIMIT_NOFILE, &rlim) < 0)
    return -1;

  fdlim = (*_st_eventsys->fd_getlimit)();
  if (fdlim > 0 && rlim.rlim_max > (rlim_t) fdlim) {
    rlim.rlim_max = fdlim;
  }
  rlim.rlim_cur = rlim.rlim_max;
  if (setrlimit(RLIMIT_NOFILE, &rlim) < 0)
    return -1;
  _st_osfd_limit = (int) rlim.rlim_max;

  return 0;
}


int st_getfdlimit(void)
{
  return _st_osfd_limit;
}


void st_netfd_free(_st_netfd_t *fd)
{
  if (!fd->inuse)
    return;

  fd->inuse = 0;
  if (fd->aux_data)
    _st_netfd_free_aux_data(fd);
  if (fd->private_data && fd->destructor)
    (*(fd->destructor))(fd->private_data);
  fd->private_data = NULL;
  fd->destructor = NULL;
  fd->next = _st_netfd_freelist;
  _st_netfd_freelist = fd;
}


static _st_netfd_t *_st_netfd_new(int osfd, int nonblock, int is_socket)
{
  _st_netfd_t *fd;
  int flags = 1;

  if ((*_st_eventsys->fd_new)(osfd) < 0)
    return NULL;

  if (_st_netfd_freelist) {
    fd = _st_netfd_freelist;
    _st_netfd_freelist = _st_netfd_freelist->next;
  } else {
    fd = calloc(1, sizeof(_st_netfd_t));
    if (!fd)
      return NULL;
  }

  fd->osfd = osfd;
  fd->inuse = 1;
  fd->next = NULL;

  if (nonblock) {
    /* Use just one system call */
    if (is_socket && ioctl(osfd, FIONBIO, &flags) != -1)
      return fd;
    /* Do it the Posix way */
    if ((flags = fcntl(osfd, F_GETFL, 0)) < 0 ||
    fcntl(osfd, F_SETFL, flags | O_NONBLOCK) < 0) {
      st_netfd_free(fd);
      return NULL;
    }
  }

  return fd;
}


_st_netfd_t *st_netfd_open(int osfd)
{
  return _st_netfd_new(osfd, 1, 0);
}


_st_netfd_t *st_netfd_open_socket(int osfd)
{
  return _st_netfd_new(osfd, 1, 1);
}


int st_netfd_close(_st_netfd_t *fd)
{
  if ((*_st_eventsys->fd_close)(fd->osfd) < 0)
    return -1;

  st_netfd_free(fd);
  return close(fd->osfd);
}


int st_netfd_fileno(_st_netfd_t *fd)
{
  return (fd->osfd);
}


void st_netfd_setspecific(_st_netfd_t *fd, void *value,
    		  _st_destructor_t destructor)
{
  if (value != fd->private_data) {
    /* Free up previously set non-NULL data value */
    if (fd->private_data && fd->destructor)
      (*(fd->destructor))(fd->private_data);
  }
  fd->private_data = value;
  fd->destructor = destructor;
}


void *st_netfd_getspecific(_st_netfd_t *fd)
{
  return (fd->private_data);
}


/*
 * Wait for I/O on a single descriptor.
 */
int st_netfd_poll(_st_netfd_t *fd, int how, st_utime_t timeout)
{
  struct pollfd pd;
  int n;

  pd.fd = fd->osfd;
  pd.events = (short) how;
  pd.revents = 0;

  if ((n = st_poll(&pd, 1, timeout)) < 0)
    return -1;
  if (n == 0) {
    /* Timed out */
    errno = ETIME;
    return -1;
  }
  if (pd.revents & POLLNVAL) {
    errno = EBADF;
    return -1;
  }

  return 0;
}


#ifdef MD_ALWAYS_UNSERIALIZED_ACCEPT
/* No-op */
int st_netfd_serialize_accept(_st_netfd_t *fd)
{
  fd->aux_data = NULL;
  return 0;
}

/* No-op */
static void _st_netfd_free_aux_data(_st_netfd_t *fd)
{
  fd->aux_data = NULL;
}

_st_netfd_t *st_accept(_st_netfd_t *fd, struct sockaddr *addr, int *addrlen,
    	       st_utime_t timeout)
{
  int osfd, err;
  _st_netfd_t *newfd;

  while ((osfd = accept(fd->osfd, addr, (socklen_t *)addrlen)) < 0) {
    if (errno == EINTR)
      continue;
    if (!_IO_NOT_READY_ERROR)
      return NULL;
    /* Wait until the socket becomes readable */
    if (st_netfd_poll(fd, POLLIN, timeout) < 0)
      return NULL;
  }

  /* On some platforms the new socket created by accept() inherits */
  /* the nonblocking attribute of the listening socket */
#if defined (MD_ACCEPT_NB_INHERITED)
  newfd = _st_netfd_new(osfd, 0, 1);
#elif defined (MD_ACCEPT_NB_NOT_INHERITED)
  newfd = _st_netfd_new(osfd, 1, 1);
#else
#error Unknown OS
#endif

  if (!newfd) {
    err = errno;
    close(osfd);
    errno = err;
  }

  return newfd;
}

#else /* MD_ALWAYS_UNSERIALIZED_ACCEPT */
/*
 * On some platforms accept() calls from different processes
 * on the same listen socket must be serialized.
 * The following code serializes accept()'s without process blocking.
 * A pipe is used as an inter-process semaphore.
 */
int st_netfd_serialize_accept(_st_netfd_t *fd)
{
  _st_netfd_t **p;
  int osfd[2], err;

  if (fd->aux_data) {
    errno = EINVAL;
    return -1;
  }
  if ((p = (_st_netfd_t **)calloc(2, sizeof(_st_netfd_t *))) == NULL)
    return -1;
  if (pipe(osfd) < 0) {
    free(p);
    return -1;
  }
  if ((p[0] = st_netfd_open(osfd[0])) != NULL &&
      (p[1] = st_netfd_open(osfd[1])) != NULL &&
      write(osfd[1], " ", 1) == 1) {
    fd->aux_data = p;
    return 0;
  }
  /* Error */
  err = errno;
  if (p[0])
    st_netfd_free(p[0]);
  if (p[1])
    st_netfd_free(p[1]);
  close(osfd[0]);
  close(osfd[1]);
  free(p);
  errno = err;

  return -1;
}

static void _st_netfd_free_aux_data(_st_netfd_t *fd)
{
  _st_netfd_t **p = (_st_netfd_t **) fd->aux_data;

  st_netfd_close(p[0]);
  st_netfd_close(p[1]);
  free(p);
  fd->aux_data = NULL;
}

_st_netfd_t *st_accept(_st_netfd_t *fd, struct sockaddr *addr, int *addrlen,
    	       st_utime_t timeout)
{
  int osfd, err;
  _st_netfd_t *newfd;
  _st_netfd_t **p = (_st_netfd_t **) fd->aux_data;
  ssize_t n;
  char c;

  for ( ; ; ) {
    if (p == NULL) {
      osfd = accept(fd->osfd, addr, (socklen_t *)addrlen);
    } else {
      /* Get the lock */
      n = st_read(p[0], &c, 1, timeout);
      if (n < 0)
    return NULL;
      ST_ASSERT(n == 1);
      /* Got the lock */
      osfd = accept(fd->osfd, addr, (socklen_t *)addrlen);
      /* Unlock */
      err = errno;
      n = st_write(p[1], &c, 1, timeout);
      ST_ASSERT(n == 1);
      errno = err;
    }
    if (osfd >= 0)
      break;
    if (errno == EINTR)
      continue;
    if (!_IO_NOT_READY_ERROR)
      return NULL;
    /* Wait until the socket becomes readable */
    if (st_netfd_poll(fd, POLLIN, timeout) < 0)
      return NULL;
  }

  /* On some platforms the new socket created by accept() inherits */
  /* the nonblocking attribute of the listening socket */
#if defined (MD_ACCEPT_NB_INHERITED)
  newfd = _st_netfd_new(osfd, 0, 1);
#elif defined (MD_ACCEPT_NB_NOT_INHERITED)
  newfd = _st_netfd_new(osfd, 1, 1);
#else
#error Unknown OS
#endif

  if (!newfd) {
    err = errno;
    close(osfd);
    errno = err;
  }

  return newfd;
}
#endif /* MD_ALWAYS_UNSERIALIZED_ACCEPT */


int st_connect(_st_netfd_t *fd, const struct sockaddr *addr, int addrlen,
           st_utime_t timeout)
{
  int n, err = 0;

  while (connect(fd->osfd, addr, addrlen) < 0) {
    if (errno != EINTR) {
      /*
       * On some platforms, if connect() is interrupted (errno == EINTR)
       * after the kernel binds the socket, a subsequent connect()
       * attempt will fail with errno == EADDRINUSE.  Ignore EADDRINUSE
       * iff connect() was previously interrupted.  See Rich Stevens'
       * "UNIX Network Programming," Vol. 1, 2nd edition, p. 413
       * ("Interrupted connect").
       */
      if (errno != EINPROGRESS && (errno != EADDRINUSE || err == 0))
    return -1;
      /* Wait until the socket becomes writable */
      if (st_netfd_poll(fd, POLLOUT, timeout) < 0)
    return -1;
      /* Try to find out whether the connection setup succeeded or failed */
      n = sizeof(int);
      if (getsockopt(fd->osfd, SOL_SOCKET, SO_ERROR, (char *)&err,
    	     (socklen_t *)&n) < 0)
    return -1;
      if (err) {
    errno = err;
    return -1;
      }
      break;
    }
    err = 1;
  }

  return 0;
}


ssize_t st_read(_st_netfd_t *fd, void *buf, size_t nbyte, st_utime_t timeout)
{
  ssize_t n;

  while ((n = read(fd->osfd, buf, nbyte)) < 0) {
    if (errno == EINTR)
      continue;
    if (!_IO_NOT_READY_ERROR)
      return -1;
    /* Wait until the socket becomes readable */
    if (st_netfd_poll(fd, POLLIN, timeout) < 0)
      return -1;
  }

  return n;
}


int st_read_resid(_st_netfd_t *fd, void *buf, size_t *resid,
    	  st_utime_t timeout)
{
  struct iovec iov, *riov;
  int riov_size, rv;

  iov.iov_base = buf;
  iov.iov_len = *resid;
  riov = &iov;
  riov_size = 1;
  rv = st_readv_resid(fd, &riov, &riov_size, timeout);
  *resid = iov.iov_len;
  return rv;
}


ssize_t st_readv(_st_netfd_t *fd, const struct iovec *iov, int iov_size,
    	 st_utime_t timeout)
{
  ssize_t n;

  while ((n = readv(fd->osfd, iov, iov_size)) < 0) {
    if (errno == EINTR)
      continue;
    if (!_IO_NOT_READY_ERROR)
      return -1;
    /* Wait until the socket becomes readable */
    if (st_netfd_poll(fd, POLLIN, timeout) < 0)
      return -1;
  }

  return n;
}

int st_readv_resid(_st_netfd_t *fd, struct iovec **iov, int *iov_size,
    	   st_utime_t timeout)
{
  ssize_t n;

  while (*iov_size > 0) {
    if (*iov_size == 1)
      n = read(fd->osfd, (*iov)->iov_base, (*iov)->iov_len);
    else
      n = readv(fd->osfd, *iov, *iov_size);
    if (n < 0) {
      if (errno == EINTR)
    continue;
      if (!_IO_NOT_READY_ERROR)
    return -1;
    } else if (n == 0)
      break;
    else {
      while ((size_t) n >= (*iov)->iov_len) {
    n -= (*iov)->iov_len;
    (*iov)->iov_base = (char *) (*iov)->iov_base + (*iov)->iov_len;
    (*iov)->iov_len = 0;
    (*iov)++;
    (*iov_size)--;
    if (n == 0)
      break;
      }
      if (*iov_size == 0)
    break;
      (*iov)->iov_base = (char *) (*iov)->iov_base + n;
      (*iov)->iov_len -= n;
    }
    /* Wait until the socket becomes readable */
    if (st_netfd_poll(fd, POLLIN, timeout) < 0)
      return -1;
  }

  return 0;
}


ssize_t st_read_fully(_st_netfd_t *fd, void *buf, size_t nbyte,
    	      st_utime_t timeout)
{
  size_t resid = nbyte;
  return st_read_resid(fd, buf, &resid, timeout) == 0 ?
    (ssize_t) (nbyte - resid) : -1;
}


int st_write_resid(_st_netfd_t *fd, const void *buf, size_t *resid,
    	   st_utime_t timeout)
{
  struct iovec iov, *riov;
  int riov_size, rv;

  iov.iov_base = (void *) buf;        /* we promise not to modify buf */
  iov.iov_len = *resid;
  riov = &iov;
  riov_size = 1;
  rv = st_writev_resid(fd, &riov, &riov_size, timeout);
  *resid = iov.iov_len;
  return rv;
}


ssize_t st_write(_st_netfd_t *fd, const void *buf, size_t nbyte,
    	 st_utime_t timeout)
{
  size_t resid = nbyte;
  return st_write_resid(fd, buf, &resid, timeout) == 0 ?
    (ssize_t) (nbyte - resid) : -1;
}


ssize_t st_writev(_st_netfd_t *fd, const struct iovec *iov, int iov_size,
    	  st_utime_t timeout)
{
  ssize_t n, rv;
  size_t nleft, nbyte;
  int index, iov_cnt;
  struct iovec *tmp_iov;
  struct iovec local_iov[_LOCAL_MAXIOV];

  /* Calculate the total number of bytes to be sent */
  nbyte = 0;
  for (index = 0; index < iov_size; index++)
    nbyte += iov[index].iov_len;

  rv = (ssize_t)nbyte;
  nleft = nbyte;
  tmp_iov = (struct iovec *) iov;    /* we promise not to modify iov */
  iov_cnt = iov_size;

  while (nleft > 0) {
    if (iov_cnt == 1) {
      if (st_write(fd, tmp_iov[0].iov_base, nleft, timeout) != (ssize_t) nleft)
    rv = -1;
      break;
    }
    if ((n = writev(fd->osfd, tmp_iov, iov_cnt)) < 0) {
      if (errno == EINTR)
    continue;
      if (!_IO_NOT_READY_ERROR) {
    rv = -1;
    break;
      }
    } else {
      if ((size_t) n == nleft)
    break;
      nleft -= n;
      /* Find the next unwritten vector */
      n = (ssize_t)(nbyte - nleft);
      for (index = 0; (size_t) n >= iov[index].iov_len; index++)
    n -= iov[index].iov_len;

      if (tmp_iov == iov) {
    /* Must copy iov's around */
    if (iov_size - index <= _LOCAL_MAXIOV) {
      tmp_iov = local_iov;
    } else {
      tmp_iov = calloc(1, (iov_size - index) * sizeof(struct iovec));
      if (tmp_iov == NULL)
        return -1;
    }
      }

      /* Fill in the first partial read */
      tmp_iov[0].iov_base = &(((char *)iov[index].iov_base)[n]);
      tmp_iov[0].iov_len = iov[index].iov_len - n;
      index++;
      /* Copy the remaining vectors */
      for (iov_cnt = 1; index < iov_size; iov_cnt++, index++) {
    tmp_iov[iov_cnt].iov_base = iov[index].iov_base;
    tmp_iov[iov_cnt].iov_len = iov[index].iov_len;
      }
    }
    /* Wait until the socket becomes writable */
    if (st_netfd_poll(fd, POLLOUT, timeout) < 0) {
      rv = -1;
      break;
    }
  }

  if (tmp_iov != iov && tmp_iov != local_iov)
    free(tmp_iov);

  return rv;
}


int st_writev_resid(_st_netfd_t *fd, struct iovec **iov, int *iov_size,
    	    st_utime_t timeout)
{
  ssize_t n;

  while (*iov_size > 0) {
    if (*iov_size == 1)
      n = write(fd->osfd, (*iov)->iov_base, (*iov)->iov_len);
    else
      n = writev(fd->osfd, *iov, *iov_size);
    if (n < 0) {
      if (errno == EINTR)
    continue;
      if (!_IO_NOT_READY_ERROR)
    return -1;
    } else {
      while ((size_t) n >= (*iov)->iov_len) {
    n -= (*iov)->iov_len;
    (*iov)->iov_base = (char *) (*iov)->iov_base + (*iov)->iov_len;
    (*iov)->iov_len = 0;
    (*iov)++;
    (*iov_size)--;
    if (n == 0)
      break;
      }
      if (*iov_size == 0)
    break;
      (*iov)->iov_base = (char *) (*iov)->iov_base + n;
      (*iov)->iov_len -= n;
    }
    /* Wait until the socket becomes writable */
    if (st_netfd_poll(fd, POLLOUT, timeout) < 0)
      return -1;
  }

  return 0;
}


/*
 * Simple I/O functions for UDP.
 */
int st_recvfrom(_st_netfd_t *fd, void *buf, int len, struct sockaddr *from,
    	int *fromlen, st_utime_t timeout)
{
  int n;

  while ((n = recvfrom(fd->osfd, buf, len, 0, from, (socklen_t *)fromlen))
     < 0) {
    if (errno == EINTR)
      continue;
    if (!_IO_NOT_READY_ERROR)
      return -1;
    /* Wait until the socket becomes readable */
    if (st_netfd_poll(fd, POLLIN, timeout) < 0)
      return -1;
  }

  return n;
}


int st_sendto(_st_netfd_t *fd, const void *msg, int len,
          const struct sockaddr *to, int tolen, st_utime_t timeout)
{
  int n;

  while ((n = sendto(fd->osfd, msg, len, 0, to, tolen)) < 0) {
    if (errno == EINTR)
      continue;
    if (!_IO_NOT_READY_ERROR)
      return -1;
    /* Wait until the socket becomes writable */
    if (st_netfd_poll(fd, POLLOUT, timeout) < 0)
      return -1;
  }

  return n;
}


int st_recvmsg(_st_netfd_t *fd, struct msghdr *msg, int flags,
           st_utime_t timeout)
{
  int n;

  while ((n = recvmsg(fd->osfd, msg, flags)) < 0) {
    if (errno == EINTR)
      continue;
    if (!_IO_NOT_READY_ERROR)
      return -1;
    /* Wait until the socket becomes readable */
    if (st_netfd_poll(fd, POLLIN, timeout) < 0)
      return -1;
  }

  return n;
}


int st_sendmsg(_st_netfd_t *fd, const struct msghdr *msg, int flags,
           st_utime_t timeout)
{
  int n;

  while ((n = sendmsg(fd->osfd, msg, flags)) < 0) {
    if (errno == EINTR)
      continue;
    if (!_IO_NOT_READY_ERROR)
      return -1;
    /* Wait until the socket becomes writable */
    if (st_netfd_poll(fd, POLLOUT, timeout) < 0)
      return -1;
  }

  return n;
}



/*
 * To open FIFOs or other special files.
 */
_st_netfd_t *st_open(const char *path, int oflags, mode_t mode)
{
  int osfd, err;
  _st_netfd_t *newfd;

  while ((osfd = open(path, oflags | O_NONBLOCK, mode)) < 0) {
    if (errno != EINTR)
      return NULL;
  }

  newfd = _st_netfd_new(osfd, 0, 0);
  if (!newfd) {
    err = errno;
    close(osfd);
    errno = err;
  }

  return newfd;
}

