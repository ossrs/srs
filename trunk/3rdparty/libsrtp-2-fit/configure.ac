dnl Process this file with autoconf to produce a configure script.
AC_INIT([libsrtp2], [2.3.0], [https://github.com/cisco/libsrtp/issues])

dnl Must come before AC_PROG_CC
EMPTY_CFLAGS="no"
if test "x$CFLAGS" = "x"; then
   dnl Default value for CFLAGS if not specified.
   EMPTY_CFLAGS="yes"
fi

dnl Checks for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_CXX
AC_ARG_VAR(
  [EXTRA_CFLAGS],
  [C compiler flags appended to the regular C compiler flags instead of overriding them])
AM_PROG_AR
AC_PROG_RANLIB
AC_PROG_INSTALL
AC_PROG_SED

dnl Check the byte order
AC_C_BIGENDIAN

AC_CANONICAL_HOST

dnl check host_cpu type, set defines appropriately
case $host_cpu in
  i*86 | x86_64 )
    AC_DEFINE([CPU_CISC], [1], [Define if building for a CISC machine (e.g. Intel).])
    AC_DEFINE([HAVE_X86], [1], [Define to use X86 inlined assembly code])
    ;;
  * )
    AC_DEFINE([CPU_RISC], [1], [Define if building for a RISC machine (assume slow byte access).])
    ;;
esac

dnl Check if we are on a Windows platform.
case $host_os in
  *cygwin*|*mingw* )
    EXE=.exe
    ;;
  * )
    EXE=""
    ;;
esac
AC_SUBST([EXE])   # define executable suffix; this is needed for `make clean'

dnl Checks for supported compiler flags.
supported_cflags=""
if test "$EMPTY_CFLAGS" = "no"; then
  supported_cflags="$CFLAGS"
fi

dnl For accurate detection, we need warnings as errors.
dnl I.e. Clang will issue a warning about unsupported flags.
dnl For the compilation to fail, those warnings needs to be upgraded to errors.
dnl This will be removed again once the tests are complete (see below).
WERROR=""
for w in -Werror -errwarn; do
  if test "x$WERROR" = "x"; then
    AC_MSG_CHECKING([whether ${CC-c} accepts $w])
    save_cflags="$CFLAGS"
    AS_IF([test "x$CFLAGS" = "x"], [CFLAGS="$w"], [CFLAGS="$CFLAGS $w"])
    AC_COMPILE_IFELSE([AC_LANG_SOURCE([int main(void) { return 0; }])],
      [WERROR="$w"
       AC_MSG_RESULT([yes])],
      [CFLAGS="$save_cflags"
       AC_MSG_RESULT([no])])
  fi
done

dnl Note that -fPIC is not explicitly added to LDFLAGS.
dnl Since the compiler is used as the link driver, CFLAGS will be part of the
dnl link line as well and the linker will get the flag from there.
dnl Adding it to LDFLAGS explicitly would duplicate the flag on the link line,
dnl but otherwise do no harm.
AC_MSG_CHECKING([whether ${CC-c} accepts -fPIC])
save_cflags="$CFLAGS"
AS_IF([test "x$CFLAGS" = "x"], [CFLAGS="-fPIC"], [CFLAGS="$CFLAGS -fPIC"])
AC_COMPILE_IFELSE([AC_LANG_SOURCE([int main(void) { return 0; }])],
  [AS_IF([test "x$supported_cflags" = "x"], [supported_cflags="-fPIC"], [supported_cflags="$supported_cflags -fPIC"])
   AC_MSG_RESULT([yes])],
  [CFLAGS="$save_cflags"
   AC_MSG_RESULT([no])])

if test "$EMPTY_CFLAGS" = "yes"; then
  for f in -Wall -pedantic -Wstrict-prototypes; do
    AC_MSG_CHECKING([whether ${CC-c} accepts $f])
    save_cflags="$CFLAGS"
    AS_IF([test "x$CFLAGS" = "x"], [CFLAGS="$f"], [CFLAGS="$CFLAGS $f"])
    AC_COMPILE_IFELSE([AC_LANG_SOURCE([int main(void) { return 0; }])],
      [AS_IF([test "x$supported_cflags" = "x"], [supported_cflags="$f"], [supported_cflags="$supported_cflags $f"])
       AC_MSG_RESULT([yes])],
      [CFLAGS="$save_cflags"
       AC_MSG_RESULT([no])])
  done

  OOPT=""
  for f in -O4 -O3; do
    if test "x$OOPT" = "x"; then
      AC_MSG_CHECKING([whether ${CC-c} accepts $f])
      save_cflags="$CFLAGS"
      AS_IF([test "x$CFLAGS" = "x"], [CFLAGS="$f"], [CFLAGS="$CFLAGS $f"])
      AC_COMPILE_IFELSE([AC_LANG_SOURCE([int main(void) { return 0; }])],
        [AS_IF([test "x$supported_cflags" = "x"], [supported_cflags="$f"], [supported_cflags="$supported_cflags $f"])
         OOPT="$f"
         AC_MSG_RESULT([yes])],
        [CFLAGS="$save_cflags"
         AC_MSG_RESULT([no])])
    fi
  done

  for f in -fexpensive-optimizations -funroll-loops; do
    AC_MSG_CHECKING([whether ${CC-c} accepts $f])
    save_cflags="$CFLAGS"
    AS_IF([test "x$CFLAGS" = "x"], [CFLAGS="$f"], [CFLAGS="$CFLAGS $f"])
    AC_COMPILE_IFELSE([AC_LANG_SOURCE([int main(void) { return 0; }])],
      [AS_IF([test "x$supported_cflags" = "x"], [supported_cflags="$f"], [supported_cflags="$supported_cflags $f"])
       AC_MSG_RESULT([yes])],
      [CFLAGS="$save_cflags"
       AC_MSG_RESULT([no])])
  done
fi

dnl When turning off warnigns, we're expecting unrecognized command line option errors if they're not
dnl supported. However, the -Wno-<warning> form isn't consulted unless a warning is triggered.
dnl At least that's the case for GCC. So to check which warnings we can turn off, we need to check
dnl if they can be turned on, thereby forcing GCC to take the argument into account right away.
for f in -Wno-language-extension-token; do
  AC_MSG_CHECKING([whether ${CC-c} accepts $f])
  save_cflags="$CFLAGS"
  testf=$(echo "$f" | $SED 's|-Wno-\(.*\)|-W\1|g')
  AS_IF([test "x$CFLAGS" = "x"], [CFLAGS="$testf"], [CFLAGS="$CFLAGS $testf"])
  AC_COMPILE_IFELSE([AC_LANG_SOURCE([int main(void) { return 0; }])],
    [AS_IF([test "x$supported_cflags" = "x"], [supported_cflags="$f"], [supported_cflags="$supported_cflags $f"])
     AC_MSG_RESULT([yes])],
    [CFLAGS="$save_cflags"
     AC_MSG_RESULT([no])])
done

dnl Remowing -Werror again
CFLAGS="$supported_cflags"

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS(
    [unistd.h byteswap.h stdint.h sys/uio.h inttypes.h sys/types.h machine/types.h sys/int_types.h],
    [], [], [AC_INCLUDES_DEFAULT])

dnl socket() and friends
AC_CHECK_HEADERS([sys/socket.h netinet/in.h arpa/inet.h], [], [], [AC_INCLUDES_DEFAULT])
AC_CHECK_HEADERS(
    [windows.h],
    [AC_CHECK_HEADERS([winsock2.h], [], [], [AC_INCLUDES_DEFAULT])],
    [], [AC_INCLUDES_DEFAULT])

AC_CHECK_TYPES([int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, uint64_t])
AC_CHECK_SIZEOF([unsigned long])
AC_CHECK_SIZEOF([unsigned long long])

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T

dnl Checks for library functions.
AC_CHECK_FUNCS([socket inet_aton usleep sigaction])

dnl Find socket function if not found yet.
if test "x$ac_cv_func_socket" = "xno"; then
  AC_CHECK_LIB([socket], [socket])
  AC_MSG_CHECKING([for socket in -lwsock32])
  SAVELIBS="$LIBS"
  LIBS="$LIBS -lwsock32"
  AC_LINK_IFELSE(
    [AC_LANG_SOURCE([
#include <winsock2.h>
int main(void)
{
    int fd = socket(0, 0, 0);
    if (fd < 0)
      return -1;
    else
      return 0;
}
    ])],
    [ac_cv_func_socket=yes
     AC_MSG_RESULT([yes])],
    [LIBS="$SAVELIBS"
     AC_MSG_RESULT([no])])
fi

AC_MSG_CHECKING([whether to enable debug logging in all modules])
AC_ARG_ENABLE([debug-logging],
  [AS_HELP_STRING([--enable-debug-logging], [Enable debug logging in all modules])],
  [], enable_debug_logging=no)
if test "$enable_debug_logging" = "yes"; then
   AC_DEFINE([ENABLE_DEBUG_LOGGING], [1], [Define to enabled debug logging for all mudules.])
fi
AC_MSG_RESULT([$enable_debug_logging])

PKG_PROG_PKG_CONFIG
AS_IF([test "x$PKG_CONFIG" != "x"], [PKG_CONFIG="$PKG_CONFIG --static"])

AC_MSG_CHECKING([whether to leverage OpenSSL crypto])
AC_ARG_ENABLE([openssl],
  [AS_HELP_STRING([--enable-openssl], [compile in OpenSSL crypto engine])],
  [], [enable_openssl=no])
AC_MSG_RESULT([$enable_openssl])

AC_MSG_CHECKING([whether to leverage NSS crypto])
AC_ARG_ENABLE([nss],
  [AS_HELP_STRING([--enable-nss], [compile in NSS crypto engine])],
  [], [enable_nss=no])
AC_MSG_RESULT([$enable_nss])

if test "$enable_openssl" = "yes"; then
   AC_MSG_CHECKING([for user specified OpenSSL directory])
   AC_ARG_WITH([openssl-dir],
      [AS_HELP_STRING([--with-openssl-dir], [Location of OpenSSL installation])],
      [if test "x$PKG_CONFIG" != "x" && test -f $with_openssl_dir/lib/pkgconfig/libcrypto.pc; then
         if test "x$PKG_CONFIG_PATH" = "x"; then
           export PKG_CONFIG_PATH="$with_openssl_dir/lib/pkgconfig"
         else
           export PKG_CONFIG_PATH="$with_openssl_dir/lib/pkgconfig:$PKG_CONFIG_PATH"
         fi
         AC_MSG_RESULT([$with_openssl_dir])
       elif test -d $with_openssl_dir/lib; then
         CFLAGS="$CFLAGS -I$with_openssl_dir/include"
         if test "x$LDFLAGS" = "x"; then
           LDFLAGS="-L$with_openssl_dir/lib"
         else
           LDFLAGS="$LDFLAGS -L$with_openssl_dir/lib"
         fi
         AC_MSG_RESULT([$with_openssl_dir])
       else
         AC_MSG_RESULT([invalid])
         AC_MSG_FAILURE([Invalid OpenSSL location: $with_openssl_dir])
       fi],
      [AC_MSG_RESULT([no])])

   if test "x$PKG_CONFIG" != "x"; then
     PKG_CHECK_MODULES([crypto], [libcrypto >= 1.0.1],
       [CFLAGS="$CFLAGS $crypto_CFLAGS"
        LIBS="$crypto_LIBS $LIBS"])
   else
     AC_CHECK_LIB([dl], [dlopen], [], [AC_MSG_WARN([can't find libdl])])
     AC_CHECK_LIB([z], [inflate], [], [AC_MSG_WARN([can't find libz])])
   fi

   AC_SEARCH_LIBS([EVP_EncryptInit], [crypto],
     [], [AC_MSG_FAILURE([can't find openssl >= 1.0.1 crypto lib])])
   AC_SEARCH_LIBS([EVP_aes_128_ctr], [crypto],
     [], [AC_MSG_FAILURE([can't find openssl >= 1.0.1 crypto lib])])
   AC_SEARCH_LIBS([EVP_aes_128_gcm], [crypto],
     [], [AC_MSG_FAILURE([can't find openssl >= 1.0.1 crypto lib])])

   AC_DEFINE([GCM], [1], [Define this to use AES-GCM.])
   AC_DEFINE([OPENSSL], [1], [Define this to use OpenSSL crypto.])
   AES_ICM_OBJS="crypto/cipher/aes_icm_ossl.o crypto/cipher/aes_gcm_ossl.o"
   HMAC_OBJS=crypto/hash/hmac_ossl.o
   AC_SUBST([USE_EXTERNAL_CRYPTO], [1])

   AC_MSG_CHECKING([if OPENSSL_cleanse is broken])
   AC_RUN_IFELSE([AC_LANG_PROGRAM([
     #include <stdio.h>
     #include <openssl/crypto.h>
   ], [
     #define BUFFER_SIZE (16)
     char buffer[[BUFFER_SIZE]];
     int i;
     for (i = 0; i < BUFFER_SIZE; i++) {
       buffer[[i]] = i & 0xff;
     }
     OPENSSL_cleanse(buffer, BUFFER_SIZE);
     for (i = 0; i < BUFFER_SIZE; i++) {
       if (buffer[[i]]) {
         printf("Buffer contents not zero at position %d (is %d)\n", i,
             buffer[[i]]);
         return 1;
       }
     }
   ])], [openssl_cleanse_broken=no], [
     openssl_cleanse_broken=yes
     AC_DEFINE([OPENSSL_CLEANSE_BROKEN], [1], [Define this if OPENSSL_cleanse is broken.])
   ])
   AC_MSG_RESULT([$openssl_cleanse_broken])

   AC_MSG_CHECKING([whether to leverage OpenSSL KDF algorithm])
   AC_ARG_ENABLE([openssl-kdf],
      [AS_HELP_STRING([--enable-openssl-kdf], [Use OpenSSL KDF algorithm])],
      [], [enable_openssl_kdf=no])
   AC_MSG_RESULT([$enable_openssl_kdf])
   if test "$enable_openssl_kdf" = "yes"; then
     AC_SEARCH_LIBS([kdf_srtp], [crypto],
       [], [AC_MSG_FAILURE([can't find openssl KDF lib])])
     AC_DEFINE([OPENSSL_KDF], [1], [Define this to use OpenSSL KDF for SRTP.])
   fi
elif test "$enable_nss" = "yes"; then
   AC_MSG_CHECKING([for user specified NSS directory])
   AC_ARG_WITH([nss-dir],
      [AS_HELP_STRING([--with-nss-dir], [Location of NSS installation])],
      [if test "x$PKG_CONFIG" != "x" && test -f $with_nss_dir/lib/pkgconfig/nss.pc; then
         if test "x$PKG_CONFIG_PATH" = "x"; then
           export PKG_CONFIG_PATH="$with_nss_dir/lib/pkgconfig"
         else
           export PKG_CONFIG_PATH="$with_nss_dir/lib/pkgconfig:$PKG_CONFIG_PATH"
         fi
         AC_MSG_RESULT([$with_nss_dir])
       elif test -d $with_nss_dir/lib; then
         CFLAGS="$CFLAGS -I$with_nss_dir/include"
         CFLAGS="$CFLAGS -I$with_nss_dir/../public/nss"
         if test "x$LDFLAGS" = "x"; then
           LDFLAGS="-L$with_nss_dir/lib"
         else
           LDFLAGS="$LDFLAGS -L$with_nss_dir/lib"
         fi
         nss_skip_pkg_config=yes
         AC_MSG_RESULT([$with_nss_dir])
       else
         AC_MSG_RESULT([invalid])
         AC_MSG_FAILURE([Invalid NSS location: $with_nss_dir])
       fi
       AC_SUBST([CRYPTO_LIBDIR], [$with_nss_dir/lib])],
      [AC_MSG_RESULT([no])])

   if test "x$PKG_CONFIG" != "x" && test "$nss_skip_pkg_config" != "yes"; then
     PKG_CHECK_MODULES([nss], [nss],
       [CFLAGS="$CFLAGS $nss_CFLAGS"
         LIBS="$nss_LIBS $LIBS"])
   else
     AC_CHECK_HEADERS(
       [nss.h],
       [], [AC_MSG_FAILURE([can't find useable NSS headers])],
       [AC_INCLUDES_DEFAULT])
     AC_CHECK_LIB(
       [nspr4], [PR_GetError],
       [], [AC_MSG_WARN([can't find libnspr4])])
     AC_CHECK_LIB(
       [nss3], [NSS_NoDB_Init],
       [], [AC_MSG_FAILURE([can't find useable libnss3])])
   fi

   AC_DEFINE([GCM], [1], [Define this to use AES-GCM.])
   AC_DEFINE([NSS], [1], [Define this to use NSS crypto.])
   AES_ICM_OBJS="crypto/cipher/aes_icm_nss.o crypto/cipher/aes_gcm_nss.o"

   # TODO(RLB): Use NSS for HMAC
   HMAC_OBJS="crypto/hash/hmac.o crypto/hash/sha1.o"

   # TODO(RLB): Use NSS for KDF

   AC_SUBST([USE_EXTERNAL_CRYPTO], [1])
else
   AES_ICM_OBJS="crypto/cipher/aes_icm.o crypto/cipher/aes.o"
   HMAC_OBJS="crypto/hash/hmac.o crypto/hash/sha1.o"
fi
AC_SUBST([AES_ICM_OBJS])
AC_SUBST([HMAC_OBJS])

dnl Checking for PCAP

PCAP_LIB=""
AC_ARG_ENABLE([pcap], AS_HELP_STRING([--disable-pcap], [Build without `pcap' library (-lpcap)]))
AS_IF([test "x$enable_pcap" != "xno"], [
  AC_CHECK_LIB([pcap], [pcap_create],
    [PCAP_LIB="-lpcap"
     AC_DEFINE([HAVE_PCAP], [1], [Define to 1 if you have the `pcap' library (-lpcap)])
     AC_SUBST([HAVE_PCAP], [1])])

  AC_CHECK_LIB([wpcap], [pcap_create],
    [PCAP_LIB="-lwpcap"
     AC_DEFINE([HAVE_PCAP], [1], [Define to 1 if you have the `winpcap' library (-lwpcap)])
     AC_SUBST([HAVE_PCAP], [1])])
])
AC_SUBST([PCAP_LIB])
   
AC_MSG_CHECKING([whether to redirect logging to stdout])
AC_ARG_ENABLE([log-stdout],
  [AS_HELP_STRING([--enable-log-stdout], [redirecting logging to stdout])],
  [], [enable_log_stdout=no])
if test "$enable_log_stdout" = "yes"; then
   AC_DEFINE([ERR_REPORTING_STDOUT], [1], [Define to redirect logging to stdout.])
fi
AC_MSG_RESULT([$enable_log_stdout])

AC_MSG_CHECKING([wheather to use a file for logging])
AC_ARG_WITH([log-file],
  [AS_HELP_STRING([--with-log-file], [Use file for logging])],
  [AS_CASE([x$with_log_file],
     [x], [valid_with_log_file="no"],
     [xyes], [valid_with_log_file="no"],
     [valid_with_error_file="yes"])
   AS_IF([test "$valid_with_log_file" = "no"],
     [AC_MSG_RESULT([invalid])
      AC_MSG_FAILURE([Invalid value for --with-log-file: "$with_log_file"])],
     [AC_DEFINE_UNQUOTED([ERR_REPORTING_FILE], ["$with_log_file"], [Logging statments will be writen to this file.])
      AC_MSG_RESULT([using log file: "$with_log_file"])])],
  [AC_MSG_RESULT([no])])

AS_IF(
  [test "$enable_log_stdout" = "yes" && test "x$with_log_file" != "x"],
  [AC_MSG_FAILURE([Can only use one of --enable-log-stdout and --with-log-file; they are mutually exclusive])])

dnl Appending EXTRA_CFLAGS, if given
AC_MSG_CHECKING([for extra C compiler flags])
AS_IF([test "x$EXTRA_CFLAGS" != "x"],
   [AS_IF([test "x$CFLAGS" = "x"],
      [CFLAGS="$EXTRA_CFLAGS"], [CFLAGS="$CFLAGS $EXTRA_CFLAGS"])
    AC_MSG_RESULT([$EXTRA_CFLAGS])],
   [AC_MSG_RESULT(no)])

AC_CONFIG_HEADER([crypto/include/config.h:config_in.h])

AC_CONFIG_FILES([Makefile crypto/Makefile doc/Makefile fuzzer/Makefile libsrtp2.pc])
AC_OUTPUT

# This is needed when building outside the source dir.
AS_MKDIR_P([crypto/cipher])
AS_MKDIR_P([crypto/hash])
AS_MKDIR_P([crypto/kernel])
AS_MKDIR_P([crypto/math])
AS_MKDIR_P([crypto/replay])
AS_MKDIR_P([crypto/test])
AS_MKDIR_P([doc])
AS_MKDIR_P([srtp])
AS_MKDIR_P([test])
