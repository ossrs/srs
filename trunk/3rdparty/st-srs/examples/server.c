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
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>
#include <pwd.h>
#include "st.h"


/******************************************************************
 * Server configuration parameters
 */

/* Log files */
#define PID_FILE    "pid"
#define ERRORS_FILE "errors"
#define ACCESS_FILE "access"

/* Default server port */
#define SERV_PORT_DEFAULT 8000

/* Socket listen queue size */
#define LISTENQ_SIZE_DEFAULT 256

/* Max number of listening sockets ("hardware virtual servers") */
#define MAX_BIND_ADDRS 16

/* Max number of "spare" threads per process per socket */
#define MAX_WAIT_THREADS_DEFAULT 8

/* Number of file descriptors needed to handle one client session */
#define FD_PER_THREAD 2

/* Access log buffer flushing interval (in seconds) */
#define ACCLOG_FLUSH_INTERVAL 30

/* Request read timeout (in seconds) */
#define REQUEST_TIMEOUT 30


/******************************************************************
 * Global data
 */

struct socket_info {
  st_netfd_t nfd;               /* Listening socket                     */
  char *addr;                   /* Bind address                         */
  unsigned int port;            /* Port                                 */
  int wait_threads;             /* Number of threads waiting to accept  */
  int busy_threads;             /* Number of threads processing request */
  int rqst_count;               /* Total number of processed requests   */
} srv_socket[MAX_BIND_ADDRS];   /* Array of listening sockets           */

static int sk_count = 0;        /* Number of listening sockets          */

static int vp_count = 0;        /* Number of server processes (VPs)     */
static pid_t *vp_pids;          /* Array of VP pids                     */

static int my_index = -1;       /* Current process index */
static pid_t my_pid = -1;       /* Current process pid   */

static st_netfd_t sig_pipe[2];  /* Signal pipe           */

/*
 * Configuration flags/parameters
 */
static int interactive_mode = 0;
static int serialize_accept = 0;
static int log_access       = 0;
static char *logdir     = NULL;
static char *username   = NULL;
static int listenq_size = LISTENQ_SIZE_DEFAULT;
static int errfd        = STDERR_FILENO;

/*
 * Thread throttling parameters (all numbers are per listening socket).
 * Zero values mean use default.
 */
static int max_threads = 0;       /* Max number of threads         */
static int max_wait_threads = 0;  /* Max number of "spare" threads */
static int min_wait_threads = 2;  /* Min number of "spare" threads */


/******************************************************************
 * Useful macros
 */

#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffff
#endif

#define SEC2USEC(s) ((s)*1000000LL)

#define WAIT_THREADS(i)  (srv_socket[i].wait_threads)
#define BUSY_THREADS(i)  (srv_socket[i].busy_threads)
#define TOTAL_THREADS(i) (WAIT_THREADS(i) + BUSY_THREADS(i))
#define RQST_COUNT(i)    (srv_socket[i].rqst_count)


/******************************************************************
 * Forward declarations
 */

static void usage(const char *progname);
static void parse_arguments(int argc, char *argv[]);
static void start_daemon(void);
static void set_thread_throttling(void);
static void create_listeners(void);
static void change_user(void);
static void open_log_files(void);
static void start_processes(void);
static void wdog_sighandler(int signo);
static void child_sighandler(int signo);
static void install_sighandlers(void);
static void start_threads(void);
static void *process_signals(void *arg);
static void *flush_acclog_buffer(void *arg);
static void *handle_connections(void *arg);
static void dump_server_info(void);

static void Signal(int sig, void (*handler)(int));
static int cpu_count(void);

extern void handle_session(long srv_socket_index, st_netfd_t cli_nfd);
extern void load_configs(void);
extern void logbuf_open(void);
extern void logbuf_flush(void);
extern void logbuf_close(void);

/* Error reporting functions defined in the error.c file */
extern void err_sys_report(int fd, const char *fmt, ...);
extern void err_sys_quit(int fd, const char *fmt, ...);
extern void err_sys_dump(int fd, const char *fmt, ...);
extern void err_report(int fd, const char *fmt, ...);
extern void err_quit(int fd, const char *fmt, ...);


/*
 * General server example: accept a client connection and do something.
 * This program just outputs a short HTML page, but can be easily adapted
 * to do other things.
 *
 * This server creates a constant number of processes ("virtual processors"
 * or VPs) and replaces them when they die. Each virtual processor manages
 * its own independent set of state threads (STs), the number of which varies
 * with load against the server. Each state thread listens to exactly one
 * listening socket. The initial process becomes the watchdog, waiting for
 * children (VPs) to die or for a signal requesting termination or restart.
 * Upon receiving a restart signal (SIGHUP), all VPs close and then reopen
 * log files and reload configuration. All currently active connections remain
 * active. It is assumed that new configuration affects only request
 * processing and not the general server parameters such as number of VPs,
 * thread limits, bind addresses, etc. Those are specified as command line
 * arguments, so the server has to be stopped and then started again in order
 * to change them.
 *
 * Each state thread loops processing connections from a single listening
 * socket. Only one ST runs on a VP at a time, and VPs do not share memory,
 * so no mutual exclusion locking is necessary on any data, and the entire
 * server is free to use all the static variables and non-reentrant library
 * functions it wants, greatly simplifying programming and debugging and
 * increasing performance (for example, it is safe to ++ and -- all global
 * counters or call inet_ntoa(3) without any mutexes). The current thread on
 * each VP maintains equilibrium on that VP, starting a new thread or
 * terminating itself if the number of spare threads exceeds the lower or
 * upper limit.
 *
 * All I/O operations on sockets must use the State Thread library's I/O
 * functions because only those functions prevent blocking of the entire VP
 * process and perform state thread scheduling.
 */
int main(int argc, char *argv[])
{
  /* Parse command-line options */
  parse_arguments(argc, argv);

  /* Allocate array of server pids */
  if ((vp_pids = calloc(vp_count, sizeof(pid_t))) == NULL)
    err_sys_quit(errfd, "ERROR: calloc failed");

  /* Start the daemon */
  if (!interactive_mode)
    start_daemon();

  /* Initialize the ST library */
  if (st_init() < 0)
    err_sys_quit(errfd, "ERROR: initialization failed: st_init");

  /* Set thread throttling parameters */
  set_thread_throttling();

  /* Create listening sockets */
  create_listeners();

  /* Change the user */
  if (username)
    change_user();

  /* Open log files */
  open_log_files();

  /* Start server processes (VPs) */
  start_processes();

  /* Turn time caching on */
  st_timecache_set(1);

  /* Install signal handlers */
  install_sighandlers();

  /* Load configuration from config files */
  load_configs();

  /* Start all threads */
  start_threads();

  /* Become a signal processing thread */
  process_signals(NULL);

  /* NOTREACHED */
  return 1;
}


/******************************************************************/

static void usage(const char *progname)
{
  fprintf(stderr, "Usage: %s -l <log_directory> [<options>]\n\n"
	  "Possible options:\n\n"
	  "\t-b <host>:<port>        Bind to specified address. Multiple"
	  " addresses\n"
	  "\t                        are permitted.\n"
	  "\t-p <num_processes>      Create specified number of processes.\n"
	  "\t-t <min_thr>:<max_thr>  Specify thread limits per listening"
	  " socket\n"
	  "\t                        across all processes.\n"
	  "\t-u <user>               Change server's user id to specified"
	  " value.\n"
	  "\t-q <backlog>            Set max length of pending connections"
	  " queue.\n"
	  "\t-a                      Enable access logging.\n"
	  "\t-i                      Run in interactive mode.\n"
	  "\t-S                      Serialize all accept() calls.\n"
	  "\t-h                      Print this message.\n",
	  progname);
  exit(1);
}


/******************************************************************/

static void parse_arguments(int argc, char *argv[])
{
  extern char *optarg;
  int opt;
  char *c;

  while ((opt = getopt(argc, argv, "b:p:l:t:u:q:aiSh")) != EOF) {
    switch (opt) {
    case 'b':
      if (sk_count >= MAX_BIND_ADDRS)
	err_quit(errfd, "ERROR: max number of bind addresses (%d) exceeded",
		 MAX_BIND_ADDRS);
      if ((c = strdup(optarg)) == NULL)
	err_sys_quit(errfd, "ERROR: strdup");
      srv_socket[sk_count++].addr = c;
      break;
    case 'p':
      vp_count = atoi(optarg);
      if (vp_count < 1)
	err_quit(errfd, "ERROR: invalid number of processes: %s", optarg);
      break;
    case 'l':
      logdir = optarg;
      break;
    case 't':
      max_wait_threads = (int) strtol(optarg, &c, 10);
      if (*c++ == ':')
	max_threads = atoi(c);
      if (max_wait_threads < 0 || max_threads < 0)
	err_quit(errfd, "ERROR: invalid number of threads: %s", optarg);
      break;
    case 'u':
      username = optarg;
      break;
    case 'q':
      listenq_size = atoi(optarg);
      if (listenq_size < 1)
	err_quit(errfd, "ERROR: invalid listen queue size: %s", optarg);
      break;
    case 'a':
      log_access = 1;
      break;
    case 'i':
      interactive_mode = 1;
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
    case 'h':
    case '?':
      usage(argv[0]);
    }
  }

  if (logdir == NULL && !interactive_mode) {
    err_report(errfd, "ERROR: logging directory is required\n");
    usage(argv[0]);
  }

  if (getuid() == 0 && username == NULL)
    err_report(errfd, "WARNING: running as super-user!");

  if (vp_count == 0 && (vp_count = cpu_count()) < 1)
    vp_count = 1;

  if (sk_count == 0) {
    sk_count = 1;
    srv_socket[0].addr = "0.0.0.0";
  }
}


/******************************************************************/

static void start_daemon(void)
{
  pid_t pid;

  /* Start forking */
  if ((pid = fork()) < 0)
    err_sys_quit(errfd, "ERROR: fork");
  if (pid > 0)
    exit(0);                  /* parent */

  /* First child process */
  setsid();                   /* become session leader */

  if ((pid = fork()) < 0)
    err_sys_quit(errfd, "ERROR: fork");
  if (pid > 0)                /* first child */
    exit(0);

  umask(022);

  if (chdir(logdir) < 0)
    err_sys_quit(errfd, "ERROR: can't change directory to %s: chdir", logdir);
}


/******************************************************************
 * For simplicity, the minimal size of thread pool is considered
 * as a maximum number of spare threads (max_wait_threads) that
 * will be created upon server startup. The pool size can grow up
 * to the max_threads value. Note that this is a per listening
 * socket limit. It is also possible to limit the total number of
 * threads for all sockets rather than impose a per socket limit.
 */

static void set_thread_throttling(void)
{
  /*
   * Calculate total values across all processes.
   * All numbers are per listening socket.
   */
  if (max_wait_threads == 0)
    max_wait_threads = MAX_WAIT_THREADS_DEFAULT * vp_count;
  /* Assuming that each client session needs FD_PER_THREAD file descriptors */
  if (max_threads == 0)
    max_threads = (st_getfdlimit() * vp_count) / FD_PER_THREAD / sk_count;
  if (max_wait_threads > max_threads)
    max_wait_threads = max_threads;

  /*
   * Now calculate per-process values.
   */
  if (max_wait_threads % vp_count)
    max_wait_threads = max_wait_threads / vp_count + 1;
  else
    max_wait_threads = max_wait_threads / vp_count;
  if (max_threads % vp_count)
    max_threads = max_threads / vp_count + 1;
  else
    max_threads = max_threads / vp_count;

  if (min_wait_threads > max_wait_threads)
    min_wait_threads = max_wait_threads;
}


/******************************************************************/

static void create_listeners(void)
{
  int i, n, sock;
  char *c;
  struct sockaddr_in serv_addr;
  struct hostent *hp;
  unsigned short port;

  for (i = 0; i < sk_count; i++) {
    port = 0;
    if ((c = strchr(srv_socket[i].addr, ':')) != NULL) {
      *c++ = '\0';
      port = (unsigned short) atoi(c);
    }
    if (srv_socket[i].addr[0] == '\0')
      srv_socket[i].addr = "0.0.0.0";
    if (port == 0)
      port = SERV_PORT_DEFAULT;

    /* Create server socket */
    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0)
      err_sys_quit(errfd, "ERROR: can't create socket: socket");
    n = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&n, sizeof(n)) < 0)
      err_sys_quit(errfd, "ERROR: can't set SO_REUSEADDR: setsockopt");
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = inet_addr(srv_socket[i].addr);
    if (serv_addr.sin_addr.s_addr == INADDR_NONE) {
      /* not dotted-decimal */
      if ((hp = gethostbyname(srv_socket[i].addr)) == NULL)
	err_quit(errfd, "ERROR: can't resolve address: %s",
		 srv_socket[i].addr);
      memcpy(&serv_addr.sin_addr, hp->h_addr, hp->h_length);
    }
    srv_socket[i].port = port;

    /* Do bind and listen */
    if (bind(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
      err_sys_quit(errfd, "ERROR: can't bind to address %s, port %hu",
		   srv_socket[i].addr, port);
    if (listen(sock, listenq_size) < 0)
      err_sys_quit(errfd, "ERROR: listen");

    /* Create file descriptor object from OS socket */
    if ((srv_socket[i].nfd = st_netfd_open_socket(sock)) == NULL)
      err_sys_quit(errfd, "ERROR: st_netfd_open_socket");
    /*
     * On some platforms (e.g. IRIX, Linux) accept() serialization is never
     * needed for any OS version.  In that case st_netfd_serialize_accept()
     * is just a no-op. Also see the comment above.
     */
    if (serialize_accept && st_netfd_serialize_accept(srv_socket[i].nfd) < 0)
      err_sys_quit(errfd, "ERROR: st_netfd_serialize_accept");
  }
}


/******************************************************************/

static void change_user(void)
{
  struct passwd *pw;

  if ((pw = getpwnam(username)) == NULL)
    err_quit(errfd, "ERROR: can't find user '%s': getpwnam failed", username);

  if (setgid(pw->pw_gid) < 0)
    err_sys_quit(errfd, "ERROR: can't change group id: setgid");
  if (setuid(pw->pw_uid) < 0)
    err_sys_quit(errfd, "ERROR: can't change user id: setuid");

  err_report(errfd, "INFO: changed process user id to '%s'", username);
}


/******************************************************************/

static void open_log_files(void)
{
  int fd;
  char str[32];

  if (interactive_mode)
    return;

  /* Open access log */
  if (log_access)
    logbuf_open();

  /* Open and write pid to pid file */
  if ((fd = open(PID_FILE, O_CREAT | O_WRONLY | O_TRUNC, 0644)) < 0)
    err_sys_quit(errfd, "ERROR: can't open pid file: open");
  sprintf(str, "%d\n", (int)getpid());
  if (write(fd, str, strlen(str)) != strlen(str))
    err_sys_quit(errfd, "ERROR: can't write to pid file: write");
  close(fd);

  /* Open error log file */
  if ((fd = open(ERRORS_FILE, O_CREAT | O_WRONLY | O_APPEND, 0644)) < 0)
    err_sys_quit(errfd, "ERROR: can't open error log file: open");
  errfd = fd;

  err_report(errfd, "INFO: starting the server...");
}


/******************************************************************/

static void start_processes(void)
{
  int i, status;
  pid_t pid;
  sigset_t mask, omask;

  if (interactive_mode) {
    my_index = 0;
    my_pid = getpid();
    return;
  }

  for (i = 0; i < vp_count; i++) {
    if ((pid = fork()) < 0) {
      err_sys_report(errfd, "ERROR: can't create process: fork");
      if (i == 0)
	exit(1);
      err_report(errfd, "WARN: started only %d processes out of %d", i,
		 vp_count);
      vp_count = i;
      break;
    }
    if (pid == 0) {
      my_index = i;
      my_pid = getpid();
      /* Child returns to continue in main() */
      return;
    }
    vp_pids[i] = pid;
  }

  /*
   * Parent process becomes a "watchdog" and never returns to main().
   */

  /* Install signal handlers */
  Signal(SIGTERM, wdog_sighandler);  /* terminate */
  Signal(SIGHUP,  wdog_sighandler);  /* restart   */
  Signal(SIGUSR1, wdog_sighandler);  /* dump info */

  /* Now go to sleep waiting for a child termination or a signal */
  for ( ; ; ) {
    if ((pid = wait(&status)) < 0) {
      if (errno == EINTR)
	continue;
      err_sys_quit(errfd, "ERROR: watchdog: wait");
    }
    /* Find index of the exited child */
    for (i = 0; i < vp_count; i++) {
      if (vp_pids[i] == pid)
	break;
    }

    /* Block signals while printing and forking */
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGHUP);
    sigaddset(&mask, SIGUSR1);
    sigprocmask(SIG_BLOCK, &mask, &omask);

    if (WIFEXITED(status))
      err_report(errfd, "WARN: watchdog: process %d (pid %d) exited"
		 " with status %d", i, pid, WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
      err_report(errfd, "WARN: watchdog: process %d (pid %d) terminated"
		 " by signal %d", i, pid, WTERMSIG(status));
    else if (WIFSTOPPED(status))
      err_report(errfd, "WARN: watchdog: process %d (pid %d) stopped"
		 " by signal %d", i, pid, WSTOPSIG(status));
    else
      err_report(errfd, "WARN: watchdog: process %d (pid %d) terminated:"
		 " unknown termination reason", i, pid);

    /* Fork another VP */
    if ((pid = fork()) < 0) {
      err_sys_report(errfd, "ERROR: watchdog: can't create process: fork");
    } else if (pid == 0) {
      my_index = i;
      my_pid = getpid();
      /* Child returns to continue in main() */
      return;
    }
    vp_pids[i] = pid;

    /* Restore the signal mask */
    sigprocmask(SIG_SETMASK, &omask, NULL);
  }
}


/******************************************************************/

static void wdog_sighandler(int signo)
{
  int i, err;

  /* Save errno */
  err = errno;
  /* Forward the signal to all children */
  for (i = 0; i < vp_count; i++) {
    if (vp_pids[i] > 0)
      kill(vp_pids[i], signo);
  }
  /*
   * It is safe to do pretty much everything here because process is
   * sleeping in wait() which is async-safe.
   */
  switch (signo) {
  case SIGHUP:
    err_report(errfd, "INFO: watchdog: caught SIGHUP");
    /* Reopen log files - needed for log rotation */
    if (log_access) {
      logbuf_close();
      logbuf_open();
    }
    close(errfd);
    if ((errfd = open(ERRORS_FILE, O_CREAT | O_WRONLY | O_APPEND, 0644)) < 0)
      err_sys_quit(STDERR_FILENO, "ERROR: watchdog: open");
    break;
  case SIGTERM:
    /* Non-graceful termination */
    err_report(errfd, "INFO: watchdog: caught SIGTERM, terminating");
    unlink(PID_FILE);
    exit(0);
  case SIGUSR1:
    err_report(errfd, "INFO: watchdog: caught SIGUSR1");
    break;
  default:
    err_report(errfd, "INFO: watchdog: caught signal %d", signo);
  }
  /* Restore errno */
  errno = err;
}


/******************************************************************/

static void install_sighandlers(void)
{
  sigset_t mask;
  int p[2];

  /* Create signal pipe */
  if (pipe(p) < 0)
    err_sys_quit(errfd, "ERROR: process %d (pid %d): can't create"
		 " signal pipe: pipe", my_index, my_pid);
  if ((sig_pipe[0] = st_netfd_open(p[0])) == NULL ||
      (sig_pipe[1] = st_netfd_open(p[1])) == NULL)
    err_sys_quit(errfd, "ERROR: process %d (pid %d): can't create"
		 " signal pipe: st_netfd_open", my_index, my_pid);

  /* Install signal handlers */
  Signal(SIGTERM, child_sighandler);  /* terminate */
  Signal(SIGHUP,  child_sighandler);  /* restart   */
  Signal(SIGUSR1, child_sighandler);  /* dump info */

  /* Unblock signals */
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGHUP);
  sigaddset(&mask, SIGUSR1);
  sigprocmask(SIG_UNBLOCK, &mask, NULL);
}


/******************************************************************/

static void child_sighandler(int signo)
{
  int err, fd;

  err = errno;
  fd = st_netfd_fileno(sig_pipe[1]);

  /* write() is async-safe */
  if (write(fd, &signo, sizeof(int)) != sizeof(int))
    err_sys_quit(errfd, "ERROR: process %d (pid %d): child's signal"
		 " handler: write", my_index, my_pid);
  errno = err;
}


/******************************************************************
 * The "main" function of the signal processing thread.
 */

/* ARGSUSED */
static void *process_signals(void *arg)
{
  int signo;

  for ( ; ; ) {
    /* Read the next signal from the signal pipe */
    if (st_read(sig_pipe[0], &signo, sizeof(int),
     ST_UTIME_NO_TIMEOUT) != sizeof(int))
      err_sys_quit(errfd, "ERROR: process %d (pid %d): signal processor:"
		   " st_read", my_index, my_pid);

    switch (signo) {
    case SIGHUP:
      err_report(errfd, "INFO: process %d (pid %d): caught SIGHUP,"
		 " reloading configuration", my_index, my_pid);
      if (interactive_mode) {
	load_configs();
	break;
      }
      /* Reopen log files - needed for log rotation */
      if (log_access) {
	logbuf_flush();
	logbuf_close();
	logbuf_open();
      }
      close(errfd);
      if ((errfd = open(ERRORS_FILE, O_CREAT | O_WRONLY | O_APPEND, 0644)) < 0)
	err_sys_quit(STDERR_FILENO, "ERROR: process %d (pid %d): signal"
		     " processor: open", my_index, my_pid);
      /* Reload configuration */
      load_configs();
      break;
    case SIGTERM:
      /*
       * Terminate ungracefully since it is generally not known how long
       * it will take to gracefully complete all client sessions.
       */
      err_report(errfd, "INFO: process %d (pid %d): caught SIGTERM,"
		 " terminating", my_index, my_pid);
      if (log_access)
	logbuf_flush();
      exit(0);
    case SIGUSR1:
      err_report(errfd, "INFO: process %d (pid %d): caught SIGUSR1",
		 my_index, my_pid);
      /* Print server info to stderr */
      dump_server_info();
      break;
    default:
      err_report(errfd, "INFO: process %d (pid %d): caught signal %d",
		 my_index, my_pid, signo);
    }
  }

  /* NOTREACHED */
  return NULL;
}


/******************************************************************
 * The "main" function of the access log flushing thread.
 */

/* ARGSUSED */
static void *flush_acclog_buffer(void *arg)
{
  for ( ; ; ) {
    st_sleep(ACCLOG_FLUSH_INTERVAL);
    logbuf_flush();
  }

  /* NOTREACHED */
  return NULL;
}


/******************************************************************/

static void start_threads(void)
{
  long i, n;

  /* Create access log flushing thread */
  if (log_access && st_thread_create(flush_acclog_buffer, NULL, 0, 0) == NULL)
    err_sys_quit(errfd, "ERROR: process %d (pid %d): can't create"
		 " log flushing thread", my_index, my_pid);

  /* Create connections handling threads */
  for (i = 0; i < sk_count; i++) {
    err_report(errfd, "INFO: process %d (pid %d): starting %d threads"
	       " on %s:%u", my_index, my_pid, max_wait_threads,
	       srv_socket[i].addr, srv_socket[i].port);
    WAIT_THREADS(i) = 0;
    BUSY_THREADS(i) = 0;
    RQST_COUNT(i) = 0;
    for (n = 0; n < max_wait_threads; n++) {
      if (st_thread_create(handle_connections, (void *)i, 0, 0) != NULL)
	WAIT_THREADS(i)++;
      else
	err_sys_report(errfd, "ERROR: process %d (pid %d): can't create"
		       " thread", my_index, my_pid);
    }
    if (WAIT_THREADS(i) == 0)
      exit(1);
  }
}


/******************************************************************/

static void *handle_connections(void *arg)
{
  st_netfd_t srv_nfd, cli_nfd;
  struct sockaddr_in from;
  int fromlen;
  long i = (long) arg;

  srv_nfd = srv_socket[i].nfd;
  fromlen = sizeof(from);

  while (WAIT_THREADS(i) <= max_wait_threads) {
    cli_nfd = st_accept(srv_nfd, (struct sockaddr *)&from, &fromlen,
     ST_UTIME_NO_TIMEOUT);
    if (cli_nfd == NULL) {
      err_sys_report(errfd, "ERROR: can't accept connection: st_accept");
      continue;
    }
    /* Save peer address, so we can retrieve it later */
    st_netfd_setspecific(cli_nfd, &from.sin_addr, NULL);

    WAIT_THREADS(i)--;
    BUSY_THREADS(i)++;
    if (WAIT_THREADS(i) < min_wait_threads && TOTAL_THREADS(i) < max_threads) {
      /* Create another spare thread */
      if (st_thread_create(handle_connections, (void *)i, 0, 0) != NULL)
	WAIT_THREADS(i)++;
      else
	err_sys_report(errfd, "ERROR: process %d (pid %d): can't create"
		       " thread", my_index, my_pid);
    }

    handle_session(i, cli_nfd);

    st_netfd_close(cli_nfd);
    WAIT_THREADS(i)++;
    BUSY_THREADS(i)--;
  }

  WAIT_THREADS(i)--;
  return NULL;
}


/******************************************************************/

static void dump_server_info(void)
{
  char *buf;
  int i, len;

  if ((buf = malloc(sk_count * 512)) == NULL) {
    err_sys_report(errfd, "ERROR: malloc failed");
    return;
  }

  len = sprintf(buf, "\n\nProcess #%d (pid %d):\n", my_index, (int)my_pid);
  for (i = 0; i < sk_count; i++) {
    len += sprintf(buf + len, "\nListening Socket #%d:\n"
		   "-------------------------\n"
		   "Address                    %s:%u\n"
		   "Thread limits (min/max)    %d/%d\n"
		   "Waiting threads            %d\n"
		   "Busy threads               %d\n"
		   "Requests served            %d\n",
		   i, srv_socket[i].addr, srv_socket[i].port,
		   max_wait_threads, max_threads,
		   WAIT_THREADS(i), BUSY_THREADS(i), RQST_COUNT(i));
  }

  write(STDERR_FILENO, buf, len);
  free(buf);
}


/******************************************************************
 * Stubs
 */

/*
 * Session handling function stub. Just dumps small HTML page.
 */
void handle_session(long srv_socket_index, st_netfd_t cli_nfd)
{
  static char resp[] = "HTTP/1.0 200 OK\r\nContent-type: text/html\r\n"
                       "Connection: close\r\n\r\n<H2>It worked!</H2>\n";
  char buf[512];
  int n = sizeof(resp) - 1;
  struct in_addr *from = st_netfd_getspecific(cli_nfd);

  if (st_read(cli_nfd, buf, sizeof(buf), SEC2USEC(REQUEST_TIMEOUT)) < 0) {
    err_sys_report(errfd, "WARN: can't read request from %s: st_read",
		   inet_ntoa(*from));
    return;
  }
  if (st_write(cli_nfd, resp, n, ST_UTIME_NO_TIMEOUT) != n) {
    err_sys_report(errfd, "WARN: can't write response to %s: st_write",
		   inet_ntoa(*from));
    return;
  }

  RQST_COUNT(srv_socket_index)++;
}


/*
 * Configuration loading function stub.
 */
void load_configs(void)
{
  err_report(errfd, "INFO: process %d (pid %d): configuration loaded",
	     my_index, my_pid);
}


/*
 * Buffered access logging methods.
 * Note that stdio functions (fopen(3), fprintf(3), fflush(3), etc.) cannot
 * be used if multiple VPs are created since these functions can flush buffer
 * at any point and thus write only partial log record to disk.
 * Also, it is completely safe for all threads of the same VP to write to
 * the same log buffer without any mutex protection (one buffer per VP, of
 * course).
 */
void logbuf_open(void)
{

}


void logbuf_flush(void)
{

}


void logbuf_close(void)
{

}


/******************************************************************
 * Small utility functions
 */

static void Signal(int sig, void (*handler)(int))
{
  struct sigaction sa;

  sa.sa_handler = handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(sig, &sa, NULL);
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

/******************************************************************/

