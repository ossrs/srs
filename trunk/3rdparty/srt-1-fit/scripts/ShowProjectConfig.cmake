#
# SRT - Secure, Reliable, Transport Copyright (c) 2021 Haivision Systems Inc.
#
# This Source Code Form is subject to the terms of the Mozilla Public License,
# v. 2.0. If a copy of the MPL was not distributed with this file, You can
# obtain one at http://mozilla.org/MPL/2.0/.
#

function(ShowProjectConfig)

   set(__ssl_configuration)
   if (SSL_FOUND OR SSL_LIBRARIES)
      set(__ssl_configuration
      "    SSL Configuration:
      SSL_FOUND=${SSL_FOUND}
      SSL_INCLUDE_DIRS=${SSL_INCLUDE_DIRS}
      SSL_LIBRARIES=${SSL_LIBRARIES}
      SSL_VERSION=${SSL_VERSION}\n")
   endif()

   set(static_property_link_libraries)
   if (srt_libspec_static)
      get_target_property(
         static_property_link_libraries
         ${TARGET_srt}_static
         LINK_LIBRARIES)
   endif()
   set(shared_property_link_libraries)
   if (srt_libspec_shared)
      get_target_property(
         shared_property_link_libraries
         ${TARGET_srt}_shared
         LINK_LIBRARIES)
   endif()

   # See https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html#id13
   set(__more_tc1_config)
   if (CMAKE_CROSSCOMPILING)
      set(__more_tc1_config
         "    CMAKE_SYSROOT: ${CMAKE_SYSROOT}\n")
   endif()

   # See https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html#id13
   set(__more_tc2_config)
   if (APPLE)
      set(__more_tc2_config
    "    CMAKE_INSTALL_NAME_TOOL: ${CMAKE_INSTALL_NAME_TOOL}
    CMAKE_OSX_SYSROOT: ${CMAKE_OSX_SYSROOT}
    CMAKE_OSX_ARCHITECTURES: ${CMAKE_OSX_ARCHITECTURES}
    CMAKE_OSX_DEPLOYMENT_TARGET: ${CMAKE_OSX_DEPLOYMENT_TARGET}
    CMAKE_OSX_SYSROOT: ${CMAKE_OSX_SYSROOT}\n")
   elseif (ANDROID)
      set(__more_tc2_config
    "    CMAKE_ANDROID_NDK: ${CMAKE_ANDROID_NDK}
    CMAKE_ANDROID_STANDALONE_TOOLCHAIN: ${CMAKE_ANDROID_STANDALONE_TOOLCHAIN}
    CMAKE_ANDROID_API: ${CMAKE_ANDROID_API}
    CMAKE_ANDROID_ARCH_ABI: ${CMAKE_ANDROID_ARCH_ABI}
    CMAKE_ANDROID_NDK_TOOLCHAIN_VERSION: ${CMAKE_ANDROID_NDK_TOOLCHAIN_VERSION}
    CMAKE_ANDROID_STL_TYPE: ${CMAKE_ANDROID_STL_TYPE}\n")
   endif()

   message(STATUS
      "\n"
      "========================================================================\n"
      "= Project Configuration:\n"
      "========================================================================\n"
      "  SRT Version:\n"
      "    SRT_VERSION: ${SRT_VERSION}\n"
      "    SRT_VERSION_BUILD: ${SRT_VERSION_BUILD}\n"
      "  CMake Configuration:\n"
      "    CMAKE_VERSION: ${CMAKE_VERSION}\n"
      "    CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}\n"
      "    CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}\n"
      "  Target Configuration:\n"
      "    CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}\n"
      "    CMAKE_SYSTEM_VERSION: ${CMAKE_SYSTEM_VERSION}\n"
      "    CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}\n"
      "    CMAKE_SIZEOF_VOID_P: ${CMAKE_SIZEOF_VOID_P}\n"
      "    DARWIN: ${DARWIN}\n"
      "    LINUX: ${LINUX}\n"
      "    BSD: ${BSD}\n"
      "    MICROSOFT: ${MICROSOFT}\n"
      "    GNU: ${GNU}\n"
      "    ANDROID: ${ANDROID}\n"
      "    SUNOS: ${SUNOS}\n"
      "    POSIX: ${POSIX}\n"
      "    SYMLINKABLE: ${SYMLINKABLE}\n"
      "    APPLE: ${APPLE}\n"
      "    UNIX: ${UNIX}\n"
      "    WIN32: ${WIN32}\n"
      "    MINGW: ${MINGW}\n"
      "    CYGWIN: ${CYGWIN}\n"
      "    CYGWIN_USE_POSIX: ${CYGWIN_USE_POSIX}\n"
      "  Toolchain Configuration:\n"
      "    CMAKE_TOOLCHAIN_FILE: ${CMAKE_TOOLCHAIN_FILE}\n"
      "    CMAKE_CROSSCOMPILING: ${CMAKE_CROSSCOMPILING}\n"
      "${__more_tc1_config}"
      "    CMAKE_C_COMPILER_ID: ${CMAKE_C_COMPILER_ID}\n"
      "    CMAKE_C_COMPILER_VERSION: ${CMAKE_C_COMPILER_VERSION}\n"
      "    CMAKE_C_COMPILER: ${CMAKE_C_COMPILER}\n"
      "    CMAKE_C_FLAGS: '${CMAKE_C_FLAGS}'\n"
      "    CMAKE_C_COMPILE_FEATURES: ${CMAKE_C_COMPILE_FEATURES}\n"
      "    CMAKE_C_STANDARD: ${CMAKE_CXX_STANDARD}\n"
      "    CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}\n"
      "    CMAKE_CXX_COMPILER_VERSION: ${CMAKE_CXX_COMPILER_VERSION}\n"
      "    CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}\n"
      "    CMAKE_CXX_FLAGS: '${CMAKE_CXX_FLAGS}'\n"
      "    CMAKE_CXX_COMPILE_FEATURES: ${CMAKE_CXX_COMPILE_FEATURES}\n"
      "    CMAKE_CXX_STANDARD: ${CMAKE_CXX_STANDARD}\n"
      "    CMAKE_LINKER: ${CMAKE_LINKER}\n"
      #"    CMAKE_EXE_LINKER_FLAGS: ${CMAKE_EXE_LINKER_FLAGS}\n"
      #"    CMAKE_EXE_LINKER_FLAGS_INIT: ${CMAKE_EXE_LINKER_FLAGS_INIT}\n"
      #"    CMAKE_MODULE_LINKER_FLAGS: ${CMAKE_MODULE_LINKER_FLAGS}\n"
      #"    CMAKE_MODULE_LINKER_FLAGS_INIT: ${CMAKE_MODULE_LINKER_FLAGS_INIT}\n"
      #"    CMAKE_SHARED_LINKER_FLAGS: ${CMAKE_SHARED_LINKER_FLAGS}\n"
      #"    CMAKE_SHARED_LINKER_FLAGS_INIT: ${CMAKE_SHARED_LINKER_FLAGS_INIT}\n"
      #"    CMAKE_STATIC_LINKER_FLAGS: ${CMAKE_STATIC_LINKER_FLAGS}\n"
      #"    CMAKE_STATIC_LINKER_FLAGS_INIT: ${CMAKE_STATIC_LINKER_FLAGS_INIT}\n"
      "    CMAKE_NM: ${CMAKE_NM}\n"
      "    CMAKE_AR: ${CMAKE_AR}\n"
      "    CMAKE_RANLIB: ${CMAKE_RANLIB}\n"
      "${__more_tc2_config}"
      "    HAVE_COMPILER_GNU_COMPAT: ${HAVE_COMPILER_GNU_COMPAT}\n"
      "    CMAKE_THREAD_LIBS: ${CMAKE_THREAD_LIBS}\n"
      "    CMAKE_THREAD_LIBS_INIT: ${CMAKE_THREAD_LIBS_INIT}\n"
      "    ENABLE_THREAD_CHECK: ${ENABLE_THREAD_CHECK}\n"
      "    USE_CXX_STD_APP: ${USE_CXX_STD_APP}\n"
      "    USE_CXX_STD_LIB: ${USE_CXX_STD_LIB}\n"
      "    STDCXX: ${STDCXX}\n"
      "    USE_CXX_STD: ${USE_CXX_STD}\n"
      "    HAVE_CLOCK_GETTIME_IN: ${HAVE_CLOCK_GETTIME_IN}\n"
      "    HAVE_CLOCK_GETTIME_LIBRT: ${HAVE_CLOCK_GETTIME_LIBRT}\n"
      "    HAVE_PTHREAD_GETNAME_NP_IN_PTHREAD_NP_H: ${HAVE_PTHREAD_GETNAME_NP_IN_PTHREAD_NP_H}\n"
      "    HAVE_PTHREAD_SETNAME_NP_IN_PTHREAD_NP_H: ${HAVE_PTHREAD_SETNAME_NP_IN_PTHREAD_NP_H}\n"
      "    HAVE_PTHREAD_GETNAME_NP: ${HAVE_PTHREAD_GETNAME_NP}\n"
      "    HAVE_PTHREAD_SETNAME_NP: ${HAVE_PTHREAD_SETNAME_NP}\n"
      "    HAVE_LIBATOMIC: ${HAVE_LIBATOMIC}\n"
      "    HAVE_LIBATOMIC_COMPILES: ${HAVE_LIBATOMIC_COMPILES}\n"
      "    HAVE_LIBATOMIC_COMPILES_STATIC: ${HAVE_LIBATOMIC_COMPILES_STATIC}\n"
      "    HAVE_GCCATOMIC_INTRINSICS: ${HAVE_GCCATOMIC_INTRINSICS}\n"
      "    HAVE_GCCATOMIC_INTRINSICS_REQUIRES_LIBATOMIC: ${HAVE_GCCATOMIC_INTRINSICS_REQUIRES_LIBATOMIC}\n"
      "    HAVE_CXX_ATOMIC: ${HAVE_CXX_ATOMIC}\n"
      "    HAVE_CXX_ATOMIC_STATIC: ${HAVE_CXX_ATOMIC_STATIC}\n"
      "  Project Configuration:\n"
      "    ENABLE_DEBUG: ${ENABLE_DEBUG}\n"
      "    ENABLE_CXX11: ${ENABLE_CXX11}\n"
      "    ENABLE_APPS: ${ENABLE_APPS}\n"
      "    ENABLE_EXAMPLES: ${ENABLE_EXAMPLES}\n"
      "    ENABLE_BONDING: ${ENABLE_BONDING}\n"
      "    ENABLE_TESTING: ${ENABLE_TESTING}\n"
      "    ENABLE_PROFILE: ${ENABLE_PROFILE}\n"
      "    ENABLE_LOGGING: ${ENABLE_LOGGING}\n"
      "    ENABLE_HEAVY_LOGGING: ${ENABLE_HEAVY_LOGGING}\n"
      "    ENABLE_HAICRYPT_LOGGING: ${ENABLE_HAICRYPT_LOGGING}\n"
      "    ENABLE_SHARED: ${ENABLE_SHARED}\n"
      "    ENABLE_STATIC: ${ENABLE_STATIC}\n"
      "    ENABLE_RELATIVE_LIBPATH: ${ENABLE_RELATIVE_LIBPATH}\n"
      "    ENABLE_GETNAMEINFO: ${ENABLE_GETNAMEINFO}\n"
      "    ENABLE_UNITTESTS: ${ENABLE_UNITTESTS}\n"
      "    ENABLE_ENCRYPTION: ${ENABLE_ENCRYPTION}\n"
      "    ENABLE_CXX_DEPS: ${ENABLE_CXX_DEPS}\n"
      "    USE_STATIC_LIBSTDCXX: ${USE_STATIC_LIBSTDCXX}\n"
      "    ENABLE_INET_PTON: ${ENABLE_INET_PTON}\n"
      "    ENABLE_CODE_COVERAGE: ${ENABLE_CODE_COVERAGE}\n"
      "    ENABLE_MONOTONIC_CLOCK: ${ENABLE_MONOTONIC_CLOCK}\n"
      "    ENABLE_STDCXX_SYNC: ${ENABLE_STDCXX_SYNC}\n"
      "    USE_OPENSSL_PC: ${USE_OPENSSL_PC}\n"
      "    OPENSSL_USE_STATIC_LIBS: ${OPENSSL_USE_STATIC_LIBS}\n"
      "    USE_BUSY_WAITING: ${USE_BUSY_WAITING}\n"
      "    USE_GNUSTL: ${USE_GNUSTL}\n"
      "    ENABLE_SOCK_CLOEXEC: ${ENABLE_SOCK_CLOEXEC}\n"
      "    ENABLE_SHOW_PROJECT_CONFIG: ${ENABLE_SHOW_PROJECT_CONFIG}\n"
      "    ENABLE_CLANG_TSA: ${ENABLE_CLANG_TSA}\n"
      "    ATOMIC_USE_SRT_SYNC_MUTEX: ${ATOMIC_USE_SRT_SYNC_MUTEX}\n"
      "  Constructed Configuration:\n"
      "    DISABLE_CXX11: ${DISABLE_CXX11}\n"
      "    HAVE_INET_PTON: ${HAVE_INET_PTON}\n"
      "    PTHREAD_LIBRARY: ${PTHREAD_LIBRARY}\n"
      "    USE_ENCLIB: ${USE_ENCLIB}\n"
      "${__ssl_configuration}"
      "    TARGET_srt: ${TARGET_srt}\n"
      "    srt_libspec_static: ${srt_libspec_static}\n"
      "    srt_libspec_shared: ${srt_libspec_shared}\n"
      "    SRT_LIBS_PRIVATE: ${SRT_LIBS_PRIVATE}\n"
      "    Target Link Libraries:\n"
      "      Static: ${static_property_link_libraries}\n"
      "      Shared: ${shared_property_link_libraries}\n"
      "========================================================================\n"
   )

endfunction(ShowProjectConfig)
