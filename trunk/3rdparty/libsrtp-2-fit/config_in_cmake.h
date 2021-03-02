/* clang-format off */

/* Define to the full name and version of this package. */
#cmakedefine PACKAGE_VERSION "@PACKAGE_VERSION@"

/* Define to the version of this package. */
#cmakedefine PACKAGE_STRING "@PACKAGE_STRING@"

/* Define to enabled debug logging for all mudules. */
#cmakedefine ENABLE_DEBUG_LOGGING 1

/* Logging statments will be writen to this file. */
#cmakedefine ERR_REPORTING_FILE "@ERR_REPORTING_FILE@"

/* Define to redirect logging to stdout. */
#cmakedefine ERR_REPORTING_STDOUT 1

/* Define this to use OpenSSL crypto. */
#cmakedefine OPENSSL 1

/* Define this to use AES-GCM. */
#cmakedefine GCM 1

/* Define if building for a CISC machine (e.g. Intel). */
#define CPU_CISC 1

/* Define if building for a RISC machine (assume slow byte access). */
/* #undef CPU_RISC */

/* Define to use X86 inlined assembly code */
#cmakedefine HAVE_X86 1

/* Define WORDS_BIGENDIAN to 1 if your processor stores words with the most
 significant byte first (like Motorola and SPARC, unlike Intel). */
#cmakedefine WORDS_BIGENDIAN 1

/* Define to 1 if you have the <arpa/inet.h> header file. */
#cmakedefine HAVE_ARPA_INET_H 1

/* Define to 1 if you have the <byteswap.h> header file. */
#cmakedefine HAVE_BYTESWAP_H 1

/* Define to 1 if you have the <inttypes.h> header file. */
#cmakedefine HAVE_INTTYPES_H 1

/* Define to 1 if you have the <machine/types.h> header file. */
#cmakedefine HAVE_MACHINE_TYPES_H 1

/* Define to 1 if you have the <netinet/in.h> header file. */
#cmakedefine HAVE_NETINET_IN_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#cmakedefine HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#cmakedefine HAVE_STDLIB_H 1

/* Define to 1 if you have the <sys/int_types.h> header file. */
#cmakedefine HAVE_SYS_INT_TYPES_H 1

/* Define to 1 if you have the <sys/socket.h> header file. */
#cmakedefine HAVE_SYS_SOCKET_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#cmakedefine HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#cmakedefine HAVE_UNISTD_H 1

/* Define to 1 if you have the <windows.h> header file. */
#cmakedefine HAVE_WINDOWS_H 1

/* Define to 1 if you have the <winsock2.h> header file. */
#cmakedefine HAVE_WINSOCK2_H 1

/* Define to 1 if you have the `inet_aton' function. */
#cmakedefine HAVE_INET_ATON 1

/* Define to 1 if you have the `sigaction' function. */
#cmakedefine HAVE_SIGACTION 1

/* Define to 1 if you have the `usleep' function. */
#cmakedefine HAVE_USLEEP 1

/* Define to 1 if the system has the type `uint8_t'. */
#cmakedefine HAVE_UINT8_T 1

/* Define to 1 if the system has the type `uint16_t'. */
#cmakedefine HAVE_UINT16_T 1

/* Define to 1 if the system has the type `uint32_t'. */
#cmakedefine HAVE_UINT32_T 1

/* Define to 1 if the system has the type `uint64_t'. */
#cmakedefine HAVE_UINT64_T 1

/* Define to 1 if the system has the type `int32_t'. */
#cmakedefine HAVE_INT32_T 1

/* The size of `unsigned long', as computed by sizeof. */
@SIZEOF_UNSIGNED_LONG_CODE@

/* The size of `unsigned long long', as computed by sizeof. */
@SIZEOF_UNSIGNED_LONG_LONG_CODE@

/* Define inline to what is supported by compiler  */
#cmakedefine HAVE_INLINE 1
#cmakedefine HAVE___INLINE 1
#ifndef HAVE_INLINE
  #ifdef HAVE___INLINE
    #define inline __inline
  #else
    #define inline
  #endif
#endif
