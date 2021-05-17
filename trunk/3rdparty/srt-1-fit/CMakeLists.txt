#
# SRT - Secure, Reliable, Transport
# Copyright (c) 2018 Haivision Systems Inc.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

cmake_minimum_required (VERSION 2.8.12 FATAL_ERROR)
# XXX This can be potentially done in future, but there still exist
# some dependent project using cmake 2.8 - this can't be done this way.
#cmake_minimum_required (VERSION 3.0.2 FATAL_ERROR)
#project(SRT VERSION "1.4.1")
project(SRT C CXX)

set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/scripts")
include(haiUtil)
include(FindPkgConfig)
# XXX See 'if (MINGW)' condition below, may need fixing.
include(FindThreads)
include(CheckFunctionExists)

# Platform shortcuts
string(TOLOWER ${CMAKE_SYSTEM_NAME} SYSNAME_LC)
set_if(DARWIN      ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
set_if(LINUX       ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
set_if(BSD         ${SYSNAME_LC} MATCHES "bsd$")
set_if(MICROSOFT   WIN32 AND (NOT MINGW AND NOT CYGWIN))
set_if(GNU         ${CMAKE_SYSTEM_NAME} MATCHES "GNU")
set_if(SYMLINKABLE LINUX OR DARWIN OR BSD OR CYGWIN OR GNU)

# Not sure what to do in case of compiling by MSVC.
# This will make installdir in C:\Program Files\SRT then
# inside "bin" and "lib64" directories. At least this maintains
# the current status. Shall this be not desired, override values
# of CMAKE_INSTALL_BINDIR, CMAKE_INSTALL_LIBDIR and CMAKE_INSTALL_INCLUDEDIR.
if (NOT DEFINED CMAKE_INSTALL_LIBDIR)
	include(GNUInstallDirs)
endif()

set (SRT_VERSION 1.4.1)
set_version_variables(SRT_VERSION ${SRT_VERSION})

# The CMAKE_BUILD_TYPE seems not to be always set, weird.
if (NOT DEFINED ENABLE_DEBUG)

	if (CMAKE_BUILD_TYPE STREQUAL "Debug")
		set (ENABLE_DEBUG ON)
	else()
		set (ENABLE_DEBUG OFF)
	endif()
endif()

# Set CMAKE_BUILD_TYPE properly, now that you know
# that ENABLE_DEBUG is set as it should.

if (ENABLE_DEBUG EQUAL 2)
	set (CMAKE_BUILD_TYPE "RelWithDebInfo")
elseif (ENABLE_DEBUG) # 1, ON, YES, TRUE, Y, or any other non-zero number
	set (CMAKE_BUILD_TYPE "Debug")
else()
	set (CMAKE_BUILD_TYPE "Release")
endif()

message(STATUS "BUILD TYPE: ${CMAKE_BUILD_TYPE}")

getVarsWith(ENFORCE_ enforcers)
foreach(ef ${enforcers})
	set (val ${${ef}})
	if (NOT val STREQUAL "")
		set(val =${val})
	endif()
	string(LENGTH ENFORCE_ pflen)
	string(LENGTH ${ef} eflen)
	math(EXPR alen ${eflen}-${pflen})
	string(SUBSTRING ${ef} ${pflen} ${alen} ef)
	message(STATUS "FORCED PP VARIABLE: ${ef}${val}")
	add_definitions(-D${ef}${val})
endforeach()

# option defaults
# XXX CHANGE: Logging is enabled now by default,
# use ENABLE_LOGGING=NO in cmake or
# --disable-logging in configure.
set(ENABLE_HEAVY_LOGGING_DEFAULT OFF)

# Always turn logging on if the build type is debug
if (ENABLE_DEBUG)
	set(ENABLE_HEAVY_LOGGING_DEFAULT ON)
endif()


# options
option(CYGWIN_USE_POSIX "Should the POSIX API be used for cygwin. Ignored if the system isn't cygwin." OFF)
option(ENABLE_CXX11 "Should the c++11 parts (srt-live-transmit) be enabled" ON)
option(ENABLE_APPS "Should the Support Applications be Built?" ON)
option(ENABLE_PROFILE "Should instrument the code for profiling. Ignored for non-GNU compiler." $ENV{HAI_BUILD_PROFILE})
option(ENABLE_LOGGING "Should logging be enabled" ON)
option(ENABLE_HEAVY_LOGGING "Should heavy debug logging be enabled" ${ENABLE_HEAVY_LOGGING_DEFAULT})
option(ENABLE_HAICRYPT_LOGGING "Should logging in haicrypt be enabled" 0)
option(ENABLE_SHARED "Should libsrt be built as a shared library" ON)
option(ENABLE_STATIC "Should libsrt be built as a static library" ON)
option(ENABLE_RELATIVE_LIBPATH "Should application contain relative library paths, like ../lib" OFF)
option(ENABLE_SUFLIP "Should suflip tool be built" OFF)
option(ENABLE_GETNAMEINFO "In-logs sockaddr-to-string should do rev-dns" OFF)
option(ENABLE_UNITTESTS "Enable unit tests" OFF)
option(ENABLE_ENCRYPTION "Enable encryption in SRT" ON)
option(ENABLE_CXX_DEPS "Extra library dependencies in srt.pc for the CXX libraries useful with C language" ON)
option(USE_STATIC_LIBSTDCXX "Should use static rather than shared libstdc++" OFF)
option(ENABLE_INET_PTON "Set to OFF to prevent usage of inet_pton when building against modern SDKs while still requiring compatibility with older Windows versions, such as Windows XP, Windows Server 2003 etc." ON)
option(ENABLE_CODE_COVERAGE "Enable code coverage reporting" OFF)
option(ENABLE_MONOTONIC_CLOCK "Enforced clock_gettime with monotonic clock on GC CV /temporary fix for #729/" OFF)
option(USE_OPENSSL_PC "Use pkg-config to find OpenSSL libraries" ON)
option(USE_BUSY_WAITING "Enable more accurate sending times at a cost of potentially higher CPU load" OFF)
option(USE_GNUSTL "Get c++ library/headers from the gnustl.pc" OFF)

set(TARGET_srt "srt" CACHE STRING "The name for the SRT library")

if (NOT ENABLE_LOGGING)
	set (ENABLE_HEAVY_LOGGING OFF)
	message(STATUS "LOGGING: DISABLED")
else()
	if (ENABLE_HEAVY_LOGGING)
		message(STATUS "LOGGING: HEAVY")
	else()
		message(STATUS "LOGGING: ENABLED")
	endif()
endif()

if (USE_BUSY_WAITING)
	message(STATUS "USE_BUSY_WAITING: ON")
	list(APPEND SRT_EXTRA_CFLAGS "-DUSE_BUSY_WAITING=1")
else()
	message(STATUS "USE_BUSY_WAITING: OFF (default)")
endif()

if ( CYGWIN AND NOT CYGWIN_USE_POSIX )
	set(WIN32 1)
	set(CMAKE_LEGACY_CYGWIN_WIN32 1)
	add_definitions(-DWIN32=1 -DCYGWIN=1)
	message(STATUS "HAVE CYGWIN. Setting backward compat CMAKE_LEGACY_CYGWIN_WIN32 and -DWIN32")
endif()

if (NOT USE_ENCLIB)
	if (USE_GNUTLS)
		message("NOTE: USE_GNUTLS is deprecated. Use -DUSE_ENCLIB=gnutls instead.")
		set (USE_ENCLIB gnutls)
	else()
		set (USE_ENCLIB openssl)
	endif()

endif()

set(USE_ENCLIB "${USE_ENCLIB}" CACHE STRING "The crypto library that SRT uses")
set_property(CACHE USE_ENCLIB PROPERTY STRINGS "openssl" "gnutls" "mbedtls")

# Make sure DLLs and executabes go to the same path regardles of subdirectory
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Handle WITH_COMPILER_PREFIX and WITH_COMPILER_TYPE options
if (DEFINED WITH_COMPILER_PREFIX)
	message(STATUS "Handling compiler with WITH_COMPILER_PREFIX=${WITH_COMPILER_PREFIX}")
	# Check also type. Default is gcc.
	if (NOT DEFINED WITH_COMPILER_TYPE)
		set (WITH_COMPILER_TYPE gcc)
	endif()

	if (${WITH_COMPILER_TYPE} STREQUAL gcc)
		set (CMAKE_C_COMPILER ${WITH_COMPILER_PREFIX}gcc)
		set (CMAKE_CXX_COMPILER ${WITH_COMPILER_PREFIX}g++)
	elseif (${WITH_COMPILER_TYPE} STREQUAL cc)
		set (CMAKE_C_COMPILER ${WITH_COMPILER_PREFIX}cc)
		set (CMAKE_CXX_COMPILER ${WITH_COMPILER_PREFIX}c++)
	else()
		# Use blindly <command> for C compiler and <command>++ for C++.
		# At least this matches clang.
		set (CMAKE_C_COMPILER ${WITH_COMPILER_PREFIX}${WITH_COMPILER_TYPE})
		set (CMAKE_CXX_COMPILER ${WITH_COMPILER_PREFIX}${WITH_COMPILER_TYPE}++)
	endif()

	message(STATUS "Compiler type: ${WITH_COMPILER_TYPE}. C: ${CMAKE_C_COMPILER}; C++: ${CMAKE_CXX_COMPILER}")
else()
	message(STATUS "No WITH_COMPILER_PREFIX - using C++ compiler ${CMAKE_CXX_COMPILER}")
endif()


if (DEFINED WITH_SRT_TARGET)
	set (TARGET_haisrt ${WITH_SRT_TARGET})
endif()

# When you use crosscompiling, you have to take care that PKG_CONFIG_PATH
# and CMAKE_PREFIX_PATH are set properly.

# symbol exists in win32, but function does not.
if(WIN32)
	if(ENABLE_INET_PTON)
		set(CMAKE_REQUIRED_LIBRARIES ws2_32)
		check_function_exists(inet_pton HAVE_INET_PTON)
		add_definitions(-D_WIN32_WINNT=0x0600)
	else()
		add_definitions(-D_WIN32_WINNT=0x0501)
	endif()
else()
	check_function_exists(inet_pton HAVE_INET_PTON)
endif()
if (DEFINED HAVE_INET_PTON)
	add_definitions(-DHAVE_INET_PTON=1)
endif()

if (ENABLE_MONOTONIC_CLOCK)
	add_definitions(-DENABLE_MONOTONIC_CLOCK=1)
endif()

if (ENABLE_ENCRYPTION)
	if ("${USE_ENCLIB}" STREQUAL "gnutls")
		set (SSL_REQUIRED_MODULES "gnutls nettle")
		if (WIN32)
			if (MINGW)
				set (SSL_REQUIRED_MODULES "${SSL_REQUIRED_MODULES} zlib")
			endif()
		endif()

		pkg_check_modules (SSL REQUIRED ${SSL_REQUIRED_MODULES})

		add_definitions(
			-DUSE_GNUTLS=1
		)

		link_directories(
			${SSL_LIBRARY_DIRS}
		)
	else() # Common for mbedtls and openssl
		if ("${USE_ENCLIB}" STREQUAL "mbedtls")
			add_definitions(-DUSE_MBEDTLS=1)
			set (SSL_REQUIRED_MODULES "mbedtls mbedcrypto")
		else()
			add_definitions(-DUSE_OPENSSL=1)
			set (SSL_REQUIRED_MODULES "openssl libcrypto")
		endif()
		# Try using pkg-config method first if enabled,
		# fall back to find_package method otherwise
		if (USE_OPENSSL_PC)
			pkg_check_modules(SSL ${SSL_REQUIRED_MODULES})
		endif()
		if (SSL_FOUND)
			# We have some cases when pkg-config is improperly configured
			# When it doesn't ship the -L and -I options, and the CMAKE_PREFIX_PATH
			# is set (also through `configure`), then we have this problem. If so,
			# set forcefully the -I and -L contents to prefix/include and
			# prefix/lib.
			if ("${SSL_LIBRARY_DIRS}" STREQUAL "")
			if (NOT "${CMAKE_PREFIX_PATH}" STREQUAL "")
				message(STATUS "WARNING: pkg-config has incorrect prefix - enforcing target path prefix: ${CMAKE_PREFIX_PATH}")
				set (SSL_LIBRARY_DIRS ${CMAKE_PREFIX_PATH}/${CMAKE_INSTALL_LIBDIR})
				set (SSL_INCLUDE_DIRS ${CMAKE_PREFIX_PATH}/include)
			endif()
			endif()

			link_directories(
				${SSL_LIBRARY_DIRS}
			)
			message(STATUS "SSL via pkg-config: -L ${SSL_LIBRARY_DIRS} -I ${SSL_INCLUDE_DIRS} -l;${SSL_LIBRARIES}")
		else()
			if ("${USE_ENCLIB}" STREQUAL "mbedtls")
				if ("${SSL_LIBRARY_DIRS}" STREQUAL "")
					set(MBEDTLS_PREFIX "${CMAKE_PREFIX_PATH}" CACHE PATH "The path of mbedtls")
					find_package(MbedTLS REQUIRED)
					set (SSL_INCLUDE_DIRS ${MBEDTLS_INCLUDE_DIR})
					set (SSL_LIBRARIES  ${MBEDTLS_LIBRARIES})
				endif()
				if ("${SSL_LIBRARIES}" STREQUAL "")
					set (SSL_LIBRARIES  mbedtls mbedcrypto)
				endif()
				message(STATUS "SSL enforced mbedtls: -I ${SSL_INCLUDE_DIRS} -l;${SSL_LIBRARIES}")
			else()
				find_package(OpenSSL REQUIRED)
				set (SSL_INCLUDE_DIRS ${OPENSSL_INCLUDE_DIR})
				set (SSL_LIBRARIES ${OPENSSL_LIBRARIES})
				message(STATUS "SSL via find_package(OpenSSL): -I ${SSL_INCLUDE_DIRS} -l;${SSL_LIBRARIES}")
			endif()
		endif()
	endif()

	add_definitions(-DSRT_ENABLE_ENCRYPTION)
	message(STATUS "ENCRYPTION: ENABLED, using: ${SSL_REQUIRED_MODULES}")
	message (STATUS "SSL libraries: ${SSL_LIBRARIES}")
else()
	message(STATUS "ENCRYPTION: DISABLED")
endif()

if ( USE_GNUSTL )
	pkg_check_modules (GNUSTL REQUIRED gnustl)
	link_directories(${GNUSTL_LIBRARY_DIRS})
	include_directories(${GNUSTL_INCLUDE_DIRS})
	set (SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE} ${GNUSTL_LIBRARIES} ${GNUSTL_LDFLAGS})
endif()

# Detect if the compiler is GNU compatable for flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Intel|Clang|AppleClang")
	message(STATUS "COMPILER: ${CMAKE_CXX_COMPILER_ID} (${CMAKE_CXX_COMPILER}) - GNU compat")
	set(HAVE_COMPILER_GNU_COMPAT 1)
else()
	message(STATUS "COMPILER: ${CMAKE_CXX_COMPILER_ID} (${CMAKE_CXX_COMPILER}) - NOT GNU compat")
	set(HAVE_COMPILER_GNU_COMPAT 0)
endif()

if (DISABLE_CXX11)
	set (ENABLE_CXX11 0)
elseif( DEFINED ENABLE_CXX11 )
else()
	set (ENABLE_CXX11 1)
endif()

if (NOT ENABLE_CXX11)
	message(WARNING "Parts that require C++11 support will be disabled (srt-live-transmit)")
endif()

# add extra warning flags for gccish compilers
if (HAVE_COMPILER_GNU_COMPAT)
	set (SRT_GCC_WARN "-Wall -Wextra")
else()
	# cpp debugging on Windows :D
	#set (SRT_GCC_WARN "/showIncludes")
endif()

if (USE_STATIC_LIBSTDCXX)
	if (HAVE_COMPILER_GNU_COMPAT)
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++")
	else()
		message(FATAL_ERROR "On non-GNU-compat compiler it's not known how to use static C++ standard library.")
	endif()
endif()

# We need clock_gettime, but on some systems this is only provided
# by librt. Check if librt is required.
if (ENABLE_MONOTONIC_CLOCK AND LINUX)
	# "requires" - exits on FATAL_ERROR when clock_gettime not available
	test_requires_clock_gettime(NEED_CLOCK_GETTIME)
	set (WITH_EXTRALIBS "${WITH_EXTRALIBS} ${NEED_CLOCK_GETTIME}")
endif()


# This options is necessary on some systems; on a cross-ARM compiler it
# has been detected, for example, that -lrt is necessary for some applications
# because clock_gettime is needed by some functions and it is alternatively
# provided by libc, but only in newer versions. This options is rarely necessary,
# but may help in several corner cases in unusual platforms.
if (WITH_EXTRALIBS)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${WITH_EXTRALIBS}")
endif()

# CMake has only discovered in 3.3 version that some set-finder is
# necessary. Using variables for shortcut to a clumsy check syntax.

set (srt_libspec_shared ${ENABLE_SHARED})
set (srt_libspec_static ${ENABLE_STATIC})

set (haicrypt_libspec VIRTUAL)

set (srtpack_libspec_common)
if (srt_libspec_shared)
	list(APPEND srtpack_libspec_common ${TARGET_srt}_shared)

endif()
if (srt_libspec_static)
	list(APPEND srtpack_libspec_common ${TARGET_srt}_static)
endif()

set (SRT_SRC_HAICRYPT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/haicrypt)
set (SRT_SRC_SRTCORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/srtcore)
set (SRT_SRC_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/common)
set (SRT_SRC_TOOLS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tools)
set (SRT_SRC_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test)

if(WIN32)
	message(STATUS "DETECTED SYSTEM: WINDOWS;  WIN32=1; PTW32_STATIC_LIB=1")
	add_definitions(-DWIN32=1 -DPTW32_STATIC_LIB=1)
elseif(DARWIN)
	message(STATUS "DETECTED SYSTEM: DARWIN;  OSX=1")
	add_definitions(-DOSX=1)
elseif(BSD)
	message(STATUS "DETECTED SYSTEM: BSD;  BSD=1")
	add_definitions(-DBSD=1)
elseif(LINUX)
	add_definitions(-DLINUX=1)
	message(STATUS "DETECTED SYSTEM: LINUX;  LINUX=1" )
elseif(CYGWIN)
	add_definitions(-DCYGWIN=1)
	message(STATUS "DETECTED SYSTEM: CYGWIN (posix mode); CYGWIN=1")
elseif(GNU)
	add_definitions(-DGNU=1)
	message(STATUS "DETECTED SYSTEM: GNU;  GNU=1" )
else()
	message(FATAL_ERROR "Unsupported system: ${CMAKE_SYSTEM_NAME}")
endif()

add_definitions(
	-D_GNU_SOURCE
	-DHAI_PATCH=1
	-DHAI_ENABLE_SRT=1
	-DSRT_VERSION="${SRT_VERSION}"
)

# This is obligatory include directory for all targets. This is only
# for private headers. Installable headers should be exclusively used DIRECTLY.
include_directories(${SRT_SRC_COMMON_DIR} ${SRT_SRC_SRTCORE_DIR} ${SRT_SRC_HAICRYPT_DIR})

if (ENABLE_LOGGING)
	list(APPEND SRT_EXTRA_CFLAGS "-DENABLE_LOGGING=1")
	if (ENABLE_HEAVY_LOGGING)
		list(APPEND SRT_EXTRA_CFLAGS "-DENABLE_HEAVY_LOGGING=1")
	endif()
	if (ENABLE_HAICRYPT_LOGGING)
		if (ENABLE_HAICRYPT_LOGGING STREQUAL 2) # Allow value 2 for INSECURE DEBUG logging
			message(WARNING " *** ENABLED INSECURE HAICRYPT LOGGING - USE FOR TESTING ONLY!!! ***")
			list(APPEND SRT_EXTRA_CFLAGS "-DENABLE_HAICRYPT_LOGGING=2")
		else()
			list(APPEND SRT_EXTRA_CFLAGS "-DENABLE_HAICRYPT_LOGGING=1")
		endif()
	endif()
endif()

if (ENABLE_GETNAMEINFO)
	list(APPEND SRT_EXTRA_CFLAGS "-DENABLE_GETNAMEINFO=1")
endif()

if (ENABLE_THREAD_CHECK)
	add_definitions(
		-DSRT_ENABLE_THREADCHECK=1
		-DFUGU_PLATFORM=1
		-I${WITH_THREAD_CHECK_INCLUDEDIR}
	)
endif()

if (ENABLE_PROFILE)
	if (HAVE_COMPILER_GNU_COMPAT)
		# They are actually cflags, not definitions, but CMake is stupid enough.
		add_definitions(-g -pg)
		link_libraries(-g -pg)
	else()
		message(FATAL_ERROR "Profiling option is not supported on this platform")
	endif()
endif()

if (ENABLE_CODE_COVERAGE)
	if (HAVE_COMPILER_GNU_COMPAT)
		add_definitions(-g -O0 --coverage)
		link_libraries(--coverage)
	else()
		message(FATAL_ERROR "Code coverage option is not supported on this platform")
	endif()
endif()

if (PTHREAD_LIBRARY AND PTHREAD_INCLUDE_DIR)
	message(STATUS "Pthread library: ${PTHREAD_LIBRARY}")
	message(STATUS "Pthread include dir: ${PTHREAD_INCLUDE_DIR}")
elseif (MICROSOFT)
	find_package(pthreads QUIET)

	if (NOT PTHREAD_INCLUDE_DIR OR NOT PTHREAD_LIBRARY)
		#search package folders with GLOB to add as extra hint for headers
		file(GLOB PTHREAD_PACKAGE_INCLUDE_HINT ./packages/cinegy.pthreads-win*/sources)
		if (PTHREAD_PACKAGE_INCLUDE_HINT)
			message(STATUS "PTHREAD_PACKAGE_INCLUDE_HINT value: ${PTHREAD_PACKAGE_INCLUDE_HINT}")
		endif()

		# find pthread header
		find_path(PTHREAD_INCLUDE_DIR pthread.h HINTS C:/pthread-win32/include ${PTHREAD_PACKAGE_INCLUDE_HINT})

		if (PTHREAD_INCLUDE_DIR)
			message(STATUS "Pthread include dir: ${PTHREAD_INCLUDE_DIR}")
		else()
			message(FATAL_ERROR "Failed to find pthread.h. Specify PTHREAD_INCLUDE_DIR.")
		endif()

		#search package folders with GLOB to add as extra hint for libs
		file(GLOB PTHREAD_PACKAGE_LIB_HINT ./packages/cinegy.pthreads-win*/runtimes/win-*/native/release)
		if (PTHREAD_PACKAGE_LIB_HINT)
			message(STATUS "PTHREAD_PACKAGE_LIB_HINT value: ${PTHREAD_PACKAGE_LIB_HINT}")
		endif()

		#find pthread library
		set(PTHREAD_LIB_SUFFIX "")
		if (ENABLE_DEBUG)
			set(PTHREAD_LIB_SUFFIX "d")
		endif ()

		set(PTHREAD_COMPILER_FLAG "")
		if (MICROSOFT)
			set(PTHREAD_COMPILER_FLAG "V")
		elseif (MINGW)
			set(PTHREAD_COMPILER_FLAG "G")
		endif ()

		foreach(EXHAND C CE SE)
			foreach(COMPAT 1 2)
				list(APPEND PTHREAD_W32_LIBRARY "pthread${PTHREAD_COMPILER_FLAG}${EXHAND}${PTHREAD_LIB_SUFFIX}${COMPAT}")
			endforeach()
		endforeach()

		find_library(PTHREAD_LIBRARY NAMES ${PTHREAD_W32_LIBRARY} pthread pthread_dll pthread_lib HINTS C:/pthread-win32/lib C:/pthread-win64/lib ${PTHREAD_PACKAGE_LIB_HINT})
		if (PTHREAD_LIBRARY)
			message(STATUS "Pthread library: ${PTHREAD_LIBRARY}")
		else()
			message(FATAL_ERROR "Failed to find pthread library. Specify PTHREAD_LIBRARY.")
		endif()
	endif()
else ()
	find_package(Threads REQUIRED)
	set(PTHREAD_LIBRARY ${CMAKE_THREAD_LIBS_INIT})
endif()

# This is required in some projects that add some other sources
# to the SRT library to be compiled together (aka "virtual library").
if (DEFINED SRT_EXTRA_LIB_INC)
	include(${SRT_EXTRA_LIB_INC}.cmake)
	# Expected to provide variables:
	# - SOURCES_srt_extra
	# - EXTRA_stransmit
endif()

# ---------------------------------------------------------------------------

# ---
# Target: haicrypt.
# Completing sources and installable headers. Flag settings will follow.
# ---
if (ENABLE_ENCRYPTION)
	set (HAICRYPT_FILELIST_MAF "filelist-${USE_ENCLIB}.maf")

	MafReadDir(haicrypt ${HAICRYPT_FILELIST_MAF}
		SOURCES SOURCES_haicrypt
		PUBLIC_HEADERS HEADERS_haicrypt
		PROTECTED_HEADERS HEADERS_haicrypt
	)
endif()

if (WIN32)
	MafReadDir(common filelist_win32.maf
		SOURCES SOURCES_common
		PUBLIC_HEADERS HEADERS_srt_win32
		PROTECTED_HEADERS HEADERS_srt_win32
	)
	message(STATUS "WINDOWS detected: adding compat sources: ${SOURCES_common}")
endif()


# Make the OBJECT library for haicrypt and srt. Then they'll be bound into
# real libraries later, either one common, or separate.

# This is needed for Xcode to properly handle CMake OBJECT Libraries
# From docs (https://cmake.org/cmake/help/latest/command/add_library.html#object-libraries):
#
# ... Some native build systems (such as Xcode) may not like targets that have only object files,
# so consider adding at least one real source file to any target that references $<TARGET_OBJECTS:objlib>.
set(OBJECT_LIB_SUPPORT "${PROJECT_SOURCE_DIR}/cmake_object_lib_support.c")

add_library(haicrypt_virtual OBJECT ${SOURCES_haicrypt} ${SOURCES_common})

# NOTE: The "virtual library" is a library specification that cmake
# doesn't support (the library of OBJECT type is something in kind of that,
# but not fully supported - for example it doesn't support transitive flags,
# so this can't be used desired way). It's a private-only dependency type,
# where the project isn't compiled into any library file at all - instead, all
# of its source files are incorporated directly to the source list of the
# project that depends on it. In cmake this must be handled manually.


# ---
# Target: srt. DEFINITION ONLY. Haicrypt flag settings follow.
# ---
MafReadDir(srtcore filelist.maf
	SOURCES SOURCES_srt
	PUBLIC_HEADERS HEADERS_srt
	PROTECTED_HEADERS HEADERS_srt
	PRIVATE_HEADERS HEADERS_srt_private
)

# Auto generated version file and add it to the HEADERS_srt list.
if(DEFINED ENV{APPVEYOR_BUILD_NUMBER})
	set(SRT_VERSION_BUILD ON)
	set(APPVEYOR_BUILD_NUMBER_STRING $ENV{APPVEYOR_BUILD_NUMBER})
	message(STATUS "AppVeyor build environment detected: Adding build number to version header")
endif()

configure_file("srtcore/version.h.in" "version.h" @ONLY)

list(INSERT HEADERS_srt 0 "${CMAKE_CURRENT_BINARY_DIR}/version.h")
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

add_library(srt_virtual OBJECT ${SOURCES_srt} ${SOURCES_srt_extra} ${HEADERS_srt})

if (ENABLE_SHARED)
	# Set this to sources as well, as it won't be automatically handled
	foreach (tar srt_virtual haicrypt_virtual)
		set_target_properties(${tar} PROPERTIES POSITION_INDEPENDENT_CODE 1)
	endforeach()

	#add resource files to shared library, to set DLL metadata on Windows DLLs
	if (MICROSOFT)
		MafReadDir(srtcore filelist.maf
			SOURCES_WIN32_SHARED SOURCES_srt_shared_win32
		)
		
		message(STATUS "WINDOWS detected: Adding sources to SRT shared project: ${SOURCES_srt_shared_win32}")
	endif()
endif()

# Manual handling of dependency on virtual library
# By setting the target, all settings applied to the haicrypt target
# will now apply to the dependent library.
#list(APPEND SOURCES_srt ${SOURCES_haicrypt})
set (VIRTUAL_srt $<TARGET_OBJECTS:srt_virtual> $<TARGET_OBJECTS:haicrypt_virtual>)

if (srt_libspec_shared)
	add_library(${TARGET_srt}_shared SHARED ${OBJECT_LIB_SUPPORT} ${VIRTUAL_srt} ${SOURCES_srt_shared_win32} ${HEADERS_srt_shared_win32})
	# shared libraries need PIC
	set (CMAKE_POSITION_INDEPENDENT_CODE ON)
	set_property(TARGET ${TARGET_srt}_shared PROPERTY OUTPUT_NAME ${TARGET_srt})
	set_target_properties (${TARGET_srt}_shared PROPERTIES VERSION ${SRT_VERSION} SOVERSION ${SRT_VERSION_MAJOR})
	list (APPEND INSTALL_TARGETS ${TARGET_srt}_shared)
	if (ENABLE_ENCRYPTION)
		target_link_libraries(${TARGET_srt}_shared PRIVATE ${SSL_LIBRARIES})
	endif()
	if (MICROSOFT)
		target_link_libraries(${TARGET_srt}_shared PRIVATE ws2_32.lib)
		set_target_properties(${TARGET_srt}_shared PROPERTIES LINK_FLAGS "/DELAYLOAD:libeay32.dll")
	elseif (MINGW)
		target_link_libraries(${TARGET_srt}_shared PRIVATE wsock32.lib ws2_32.lib)
	elseif (APPLE)
		set_property(TARGET ${TARGET_srt}_shared PROPERTY MACOSX_RPATH ON)
	endif()
	if (USE_GNUSTL)
		target_link_libraries(${TARGET_srt}_shared PRIVATE ${GNUSTL_LIBRARIES} ${GNUSTL_LDFLAGS})
	endif()
endif()

if (srt_libspec_static)
	add_library(${TARGET_srt}_static STATIC ${OBJECT_LIB_SUPPORT} ${VIRTUAL_srt})

	# For Windows, leave the name to be "srt_static.lib".
	# Windows generates two different library files:
	# - a usual static library for static linkage
	# - a shared library exposer, which allows pre-resolution and later dynamic
	#   linkage when running the executable
	# Both having unfortunately the same names created by MSVC compiler.
	# It's not the case of Cygwin/MINGW - they are named there libsrt.a and libsrt.dll.a
	if (MICROSOFT)
		# Keep _static suffix. By unknown reason, the name must still be set explicitly.
		set_property(TARGET ${TARGET_srt}_static PROPERTY OUTPUT_NAME ${TARGET_srt}_static)
	else()
		set_property(TARGET ${TARGET_srt}_static PROPERTY OUTPUT_NAME ${TARGET_srt})
	endif()

	list (APPEND INSTALL_TARGETS ${TARGET_srt}_static)
	if (ENABLE_ENCRYPTION)
		target_link_libraries(${TARGET_srt}_static PRIVATE ${SSL_LIBRARIES})
	endif()
	if (MICROSOFT)
		target_link_libraries(${TARGET_srt}_static PRIVATE ws2_32.lib)
	elseif (MINGW)
		target_link_libraries(${TARGET_srt}_static PRIVATE wsock32 ws2_32)
	endif()
	if (USE_GNUSTL)
		target_link_libraries(${TARGET_srt}_static PRIVATE ${GNUSTL_LIBRARIES} ${GNUSTL_LDFLAGS})
	endif()
endif()



# ---
# And back to target: haicrypt. Both targets must be defined
# prior to setting flags, and after defining the list of sources
# can no longer be extended.
#
# For haicrypt.spec = VIRTUAL, these settings apply to srt.
# Otherwise they apply to haicrypt.
# ---


target_include_directories(haicrypt_virtual PRIVATE  ${SSL_INCLUDE_DIRS})

if (MICROSOFT)
	set (SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE} ws2_32.lib)
elseif (MINGW)
	set (SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE} -lwsock32 -lws2_32)
endif()

# ---
# So, back to target: srt. Setting the rest of the settings for srt target.
# ---

# Applying this to public includes is not transitive enough.
# On Windows, apps require this as well, so it's safer to
# spread this to all targets.
if (PTHREAD_INCLUDE_DIR)
	include_directories(${PTHREAD_INCLUDE_DIR})
endif()

# Link libraries must be applied directly to the derivatives
# as virtual libraries (OBJECT-type) cannot have linkage declarations
# transitive or not.

foreach(tar ${srtpack_libspec_common})
	message(STATUS "ADDING TRANSITIVE LINK DEP to:${tar} : ${PTHREAD_LIBRARY} ${dep}")
	target_link_libraries (${tar} PUBLIC ${PTHREAD_LIBRARY} ${dep})
endforeach()


set (SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE} ${PTHREAD_LIBRARY})

target_compile_definitions(srt_virtual PRIVATE -DSRT_EXPORTS )
target_compile_definitions(haicrypt_virtual PUBLIC -DHAICRYPT_DYNAMIC)
if (ENABLE_SHARED)
	target_compile_definitions(srt_virtual PUBLIC -DSRT_DYNAMIC) 
	target_compile_definitions(haicrypt_virtual PRIVATE -DHAICRYPT_EXPORTS)
endif()

if (srt_libspec_shared)
if (MICROSOFT)
	target_link_libraries(${TARGET_srt}_shared PUBLIC Ws2_32.lib)
endif()
endif()

# Cygwin installs the *.dll libraries in bin directory and uses PATH.

set (INSTALL_SHARED_DIR ${CMAKE_INSTALL_LIBDIR})
if (CYGWIN)
	set (INSTALL_SHARED_DIR ${CMAKE_INSTALL_BINDIR})
endif()

message(STATUS "INSTALL DIRS: bin=${CMAKE_INSTALL_BINDIR} lib=${CMAKE_INSTALL_LIBDIR} shlib=${INSTALL_SHARED_DIR} include=${CMAKE_INSTALL_INCLUDEDIR}")

install(TARGETS ${INSTALL_TARGETS}
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
		ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
		LIBRARY DESTINATION ${INSTALL_SHARED_DIR}
)
install(FILES ${HEADERS_srt} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/srt)
if (WIN32)
	install(FILES ${HEADERS_srt_win32} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/srt/win)
endif()

# ---
# That's all for target definition
# ---

join_arguments(SRT_EXTRA_CFLAGS ${SRT_EXTRA_CFLAGS})

#message(STATUS "Target srt: LIBSPEC: ${srtpack_libspec_common} SOURCES: {${SOURCES_srt}}  HEADERS: {${HEADERS_srt}}")

set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SRT_DEBUG_OPT} ${SRT_EXTRA_CFLAGS} ${SRT_GCC_WARN}")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SRT_DEBUG_OPT} ${SRT_EXTRA_CFLAGS} ${SRT_GCC_WARN}")

# PC file generation.
if (NOT DEFINED INSTALLDIR)
	set (INSTALLDIR ${CMAKE_INSTALL_PREFIX})
	get_filename_component(INSTALLDIR ${INSTALLDIR} ABSOLUTE)
endif()

# Required if linking a C application.
# This may cause trouble when you want to compile your app with static libstdc++;
# if your build requires it, you'd probably remove -lstdc++ from the list
# obtained by `pkg-config --libs`.
if(ENABLE_CXX_DEPS)
	foreach(LIB ${CMAKE_CXX_IMPLICIT_LINK_LIBRARIES})
		if(IS_ABSOLUTE ${LIB} AND EXISTS ${LIB})
			set(SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE} ${LIB})
		else()
			set(SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE} "-l${LIB}")
		endif()
	endforeach()
endif()

join_arguments(SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE})

# haisrt.pc left temporarily for backward compatibility. To be removed in future!
configure_file(scripts/srt.pc.in haisrt.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/haisrt.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
configure_file(scripts/srt.pc.in srt.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/srt.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

# Applications

if (HAVE_COMPILER_GNU_COMPAT AND ENABLE_CXX11)
	message(STATUS "C++ VERSION: Setting C++11 compat flag for gnu compiler")
	set (CFLAGS_CXX_STANDARD "-std=c++11")
else()
	message(STATUS "C++ VERSION: leaving default, not a GNU compiler, assuming C++11 or newer is default.")
	set (CFLAGS_CXX_STANDARD "")
endif()

# If static is available, link apps against static one.
# Otherwise link against shared one.

if (srt_libspec_static)
	set (srt_link_library ${TARGET_srt}_static)
	if (ENABLE_RELATIVE_LIBPATH)
		message(STATUS "ENABLE_RELATIVE_LIBPATH=ON will be ignored due to static linking.")
	endif()
elseif(srt_libspec_shared)
	set (srt_link_library ${TARGET_srt}_shared)
else()
	message(FATAL_ERROR "Either ENABLE_STATIC or ENABLE_SHARED has to be ON!")
endif()

macro(srt_add_program name)
	add_executable(${name} ${ARGN})
	target_include_directories(${name} PRIVATE apps)
	target_include_directories(${name} PRIVATE common)
	install(TARGETS ${name} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endmacro()

macro(srt_make_application name)
	target_compile_options(${name} PRIVATE ${CFLAGS_CXX_STANDARD})

	# This is recommended by cmake, but it doesn't work anyway.
	# What is needed is that this below CMAKE_INSTALL_RPATH (yes, relative)
	# is added as is.
	# set (CMAKE_SKIP_RPATH FALSE)
	# set (CMAKE_SKIP_BUILD_RPATH FALSE)
	# set (CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
	# set (CMAKE_INSTALL_RPATH "../${CMAKE_INSTALL_LIBDIR}")
	# set (CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
	# set (FORCE_RPATH BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH_USE_LINK_PATH TRUE)

	if (LINUX AND ENABLE_RELATIVE_LIBPATH AND NOT srt_libspec_static)
		# This is only needed on Linux, on Windows (including Cygwin) the library file will
		# be placed into the binrary directory anyway.
		# XXX not sure about Mac.
		# See this name used already in install(${TARGET_srt} LIBRARY DESTINATION...).
		set(FORCE_RPATH LINK_FLAGS -Wl,-rpath,.,-rpath,../${CMAKE_INSTALL_LIBDIR} BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH_USE_LINK_PATH TRUE)
	endif()

	# We state that Darwin always uses CLANG compiler, which honors this flag the same way.
	set_target_properties(${name} PROPERTIES COMPILE_FLAGS "${CFLAGS_CXX_STANDARD}" ${FORCE_RPATH})

	target_link_libraries(${name} ${srt_link_library})
	if (USE_GNUSTL)
		target_link_libraries(${name} PRIVATE ${GNUSTL_LIBRARIES} ${GNUSTL_LDFLAGS})
	endif()
	if (srt_libspec_static AND CMAKE_DL_LIBS)
		target_link_libraries(${name} ${CMAKE_DL_LIBS})
	endif()
endmacro()

macro(srt_add_application name) # ARGN=sources...
	srt_add_program(${name} apps/${name}.cpp ${ARGN})
	srt_make_application(${name})
	install(TARGETS ${name} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endmacro()

## FIXME: transmitmedia.cpp does not build on OpenBSD
##    Issue: https://github.com/Haivision/srt/issues/590
if (BSD
   AND ${SYSNAME_LC} MATCHES "^openbsd$")
   set(ENABLE_APPS OFF)
endif()
## The applications currently require c++11.
if (NOT ENABLE_CXX11)
   set(ENABLE_APPS OFF)
endif()

if (ENABLE_APPS)

	# Make a virtual library of all shared app files
	MafReadDir(apps support.maf
		SOURCES SOURCES_support
	)

	# A special trick that makes the shared application sources
	# to be compiled once for all applications. Maybe this virtual
	# library should be changed into a static one and made useful
	# for users.
	add_library(srtsupport_virtual OBJECT ${SOURCES_support})
	target_compile_options(srtsupport_virtual PUBLIC ${CFLAGS_CXX_STANDARD})
	set (VIRTUAL_srtsupport $<TARGET_OBJECTS:srtsupport_virtual>)

	# Applications

	srt_add_application(srt-live-transmit ${VIRTUAL_srtsupport})
	if (DEFINED EXTRA_stransmit)
		set_target_properties(srt-live-transmit PROPERTIES COMPILE_FLAGS "${EXTRA_stransmit}")
	endif()
	srt_add_application(srt-file-transmit ${VIRTUAL_srtsupport})

	if (MINGW)
		# FIXME: with MINGW, it fails to build apps that require C++11
		# https://github.com/Haivision/srt/issues/177
		message(WARNING "On MinGW, some C++11 apps are blocked due to lacking proper C++11 headers for <thread>. FIX IF POSSIBLE.")
	else()
		# srt-multiplex temporarily blocked
		#srt_add_application(srt-multiplex ${VIRTUAL_srtsupport})
		srt_add_application(srt-tunnel ${VIRTUAL_srtsupport})
		target_compile_definitions(srt-tunnel PUBLIC -DSRT_ENABLE_VERBOSE_LOCK)
	endif()

	if (ENABLE_TESTING)

		macro(srt_add_testprogram name)
			# Variables in macros are not local. Clear them forcefully.
			set (SOURCES_app_indir "")
			set (SOURCES_app "")
			# Unlike Silvercat, in cmake you must know the full list
			# of source files at the moment when defining the target
			# and it can't be altered later.
			#
			# For testing applications, every application has its exclusive
			# list of source files in its own Manifest file.
			MafReadDir(testing ${name}.maf SOURCES SOURCES_app)
			srt_add_program(${name} ${SOURCES_app})
		endmacro()

		srt_add_testprogram(utility-test)
		if (NOT WIN32)
			# This program is symlinked under git-cygwin.
			# Avoid misleading syntax error.
			srt_add_testprogram(uriparser-test)
			target_compile_options(uriparser-test PRIVATE -DTEST)
			target_compile_options(uriparser-test PRIVATE ${CFLAGS_CXX_STANDARD})
		endif()
		
		srt_add_testprogram(srt-test-live)
		srt_make_application(srt-test-live)

		srt_add_testprogram(srt-test-file)
		srt_make_application(srt-test-file)

		srt_add_testprogram(srt-test-relay)
		srt_make_application(srt-test-relay)
		target_compile_definitions(srt-test-relay PUBLIC -DSRT_ENABLE_VERBOSE_LOCK)

		srt_add_testprogram(srt-test-multiplex)
		srt_make_application(srt-test-multiplex)
	endif()

endif()

if (ENABLE_EXAMPLES)

	# No examples should need C++11
	macro(srt_add_example mainsrc)
		get_filename_component(name ${mainsrc} NAME_WE)
		srt_add_program(${name} examples/${mainsrc} ${ARGN})
	endmacro()

	srt_add_example(sendfile.cpp)
	srt_make_application(sendfile)

	srt_add_example(recvfile.cpp)
	srt_make_application(recvfile)

	srt_add_example(recvlive.cpp)
	srt_make_application(recvlive)

	srt_add_example(test-c-client.c)
	srt_make_application(test-c-client)

	srt_add_example(test-c-server.c)
	srt_make_application(test-c-server)

	srt_add_example(testcapi-connect.c)
	target_link_libraries(testcapi-connect ${srt_link_library} ${DEPENDS_srt})

endif()


if (ENABLE_UNITTESTS AND ENABLE_CXX11)
	set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

	find_package(GTest 1.8)
	if (NOT GTEST_FOUND)
		message(STATUS "GTEST not found! Fetching from git.")
		include(googletest)
		fetch_googletest(
			${PROJECT_SOURCE_DIR}/scripts
			${PROJECT_BINARY_DIR}/googletest
		)
		set(GTEST_BOTH_LIBRARIES "gtest_main" CACHE STRING "Add gtest_main target")
	endif()

	MafReadDir(test filelist.maf
		SOURCES SOURCES_unittests
	)
	
	message(STATUS "Unit test sources: ${SOURCES_unittests}")

	srt_add_program(test-srt ${SOURCES_unittests})
	srt_make_application(test-srt)
	target_include_directories(test-srt PRIVATE  ${SSL_INCLUDE_DIRS})

	target_link_libraries(
		test-srt
		${GTEST_BOTH_LIBRARIES}
		${srt_link_library}
		${PTHREAD_LIBRARY}
		)

	add_test(
		NAME
			test-srt
		COMMAND
			${CMAKE_BINARY_DIR}/test-srt
	)

	enable_testing()

endif()


install(PROGRAMS scripts/srt-ffplay DESTINATION ${CMAKE_INSTALL_BINDIR})


if (DEFINED SRT_EXTRA_APPS_INC)
	include(${SRT_EXTRA_APPS_INC}.cmake)
	# No extra variables expected. Just use the variables
	# already provided and define additional targets.
endif()

if ( ENABLE_SUFLIP )
	set (SOURCES_suflip
		${CMAKE_CURRENT_SOURCE_DIR}/apps/suflip.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/common/uriparser.cpp
	)
	srt_add_program(suflip ${SOURCES_suflip})
	target_link_libraries(suflip ${srt_link_library})
	install(TARGETS suflip RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif ()
