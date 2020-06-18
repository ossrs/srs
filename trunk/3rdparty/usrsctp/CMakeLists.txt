#
# Copyright (C) 2015-2015 Oleg Alexeenkov
# Copyright (C) 2015-2019 Felix Weinrank
#
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the project nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

project(usrsctplib C)
cmake_minimum_required(VERSION 3.0)

# Debug build type as default
if (NOT CMAKE_BUILD_TYPE)
	message(STATUS "No build type selected, using DEBUG")
	set(CMAKE_BUILD_TYPE "DEBUG")
endif ()

include(CheckStructHasMember)
include(CheckIncludeFile)
include(CheckIncludeFiles)
include(CheckCCompilerFlag)

#################################################
# CHECK OPTIONS
#################################################

option(sctp_invariants "Add runtime checks" 0)
if (sctp_invariants)
	add_definitions(-DINVARIANTS)
endif ()

option(sctp_debug "Provide debug information" 1)
if (sctp_debug)
	add_definitions(-DSCTP_DEBUG)
endif ()

option(sctp_inet "Support IPv4" 1)
if (sctp_inet)
	add_definitions(-DINET)
endif ()

option(sctp_inet6 "Support IPv6" 1)
if (sctp_inet6)
	add_definitions(-DINET6)
endif ()

option(sctp_werror "Treat warning as error" 1)

option(sctp_link_programs_static "Link example programs static" 0)

option(sctp_build_programs "Build example programs" 1)

option(sctp_sanitizer_address "Compile with address sanitizer" 0)

option(sctp_sanitizer_memory "Compile with memory sanitizer" 0)

option(sctp_build_fuzzer "Compile in clang fuzzing mode" 0)

if (sctp_sanitizer_address AND sctp_sanitizer_memory)
	message(FATAL_ERROR "Can not compile with both sanitizer options")
endif ()

if (sctp_link_programs_static OR WIN32)
	set(programs_link_library "usrsctp-static")
else ()
	set(programs_link_library "usrsctp")
endif ()


#################################################
# CHECK FOR TYPES AND FUNCTIONS
#################################################

check_include_files("sys/queue.h" have_sys_queue_h)
if (have_sys_queue_h)
	add_definitions(-DHAVE_SYS_QUEUE_H)
endif ()

check_include_files("sys/socket.h;linux/if_addr.h" have_linux_if_addr_h)
if (have_linux_if_addr_h)
	add_definitions(-DHAVE_LINUX_IF_ADDR_H)
endif ()

check_include_files("sys/socket.h;linux/rtnetlink.h" have_linux_rtnetlink_h)
if (have_linux_rtnetlink_h)
	add_definitions(-DHAVE_LINUX_RTNETLINK_H)
endif ()

check_include_files("sys/types.h;netinet/in.h;netinet/ip.h;netinet/ip_icmp.h" have_netinet_ip_icmp_h)
if (have_netinet_ip_icmp_h)
	add_definitions(-DHAVE_NETINET_IP_ICMP_H)
endif ()

check_include_files("stdatomic.h" have_stdatomic_h)
if (have_stdatomic_h)
	add_definitions(-DHAVE_STDATOMIC_H)
endif ()


#################################################
# CHECK STRUCT MEMBERS
#################################################

set(CMAKE_REQUIRED_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/usrsctplib")

check_include_file(usrsctp.h have_usrsctp_h)
if (NOT have_usrsctp_h)
	message(FATAL_ERROR "usrsctp.h not found")
endif ()

check_struct_has_member("struct sockaddr" "sa_len" "sys/types.h;sys/socket.h" have_sa_len)
if (have_sa_len)
	message(STATUS "have_sa_len")
	add_definitions(-DHAVE_SA_LEN)
endif ()

check_struct_has_member("struct sockaddr_in" "sin_len" "sys/types.h;netinet/in.h" have_sin_len)
if (have_sin_len)
	message(STATUS "have_sin_len")
	add_definitions(-DHAVE_SIN_LEN)
endif ()

check_struct_has_member("struct sockaddr_in6" "sin6_len" "sys/types.h;netinet/in.h" have_sin6_len)
if (have_sin6_len)
	message(STATUS "have_sin6_len")
	add_definitions(-DHAVE_SIN6_LEN)
endif ()

check_struct_has_member("struct sockaddr_conn" "sconn_len" "usrsctp.h" have_sconn_len)
if (have_sconn_len)
	message(STATUS "HAVE_SCONN_LEN")
	add_definitions(-DHAVE_SCONN_LEN)
endif ()


#################################################
# COMPILER SETTINGS
#################################################

# Determine if compiler is Visual Studio compiler or Clang in MSVC compatible mode
if (CMAKE_C_COMPILER_ID MATCHES "MSVC" OR CMAKE_C_SIMULATE_ID MATCHES "MSVC")
  set(C_COMPILER_IS_MSVC_LIKE TRUE)
endif()

# SETTINGS FOR VISUAL STUDIO COMPILER
if (C_COMPILER_IS_MSVC_LIKE)
	if (CMAKE_C_FLAGS MATCHES "/W[0-4]")
		string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
	else ()
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W4")
	endif ()

	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4100") # 'identifier' : unreferenced formal parameter
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4127") # conditional expression is constant
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4200") # nonstandard extension used : zero-sized array in struct/union
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4214") # bit field types other than int
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4706") # assignment within conditional expression
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4245") # 'conversion' : conversion from 'type1' to 'type2', signed/unsigned mismatch
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4389") # 'operator' : signed/unsigned mismatch
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4702") # unreachable code
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4701") # Potentially uninitialized local variable 'name' used

	# ToDo
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /wd4244") # 'conversion' conversion from 'type1' to 'type2', possible loss of data

	if (sctp_werror)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /WX")

		if (CMAKE_C_COMPILER_ID MATCHES "Clang")
			# temporary disable exta clang warnings preventing compilation
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /clang:-Wno-unused-function")
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /clang:-Wno-missing-field-initializers")
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /clang:-Wno-sign-compare")
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /clang:-Wno-format")
		endif()
	endif ()
# SETTINGS FOR UNIX COMPILER
elseif (CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER_ID MATCHES "AppleClang" OR CMAKE_C_COMPILER_ID MATCHES "GNU")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99 -pedantic -Wall -Wextra")

	check_c_compiler_flag(-Wfloat-equal has_wfloat_equal)
	if (has_wfloat_equal)
		set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -Wfloat-equal")
	endif ()

	check_c_compiler_flag(-Wshadow has_wshadow)
	if (has_wshadow)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wshadow")
	endif ()

	check_c_compiler_flag(-Wpointer-arith has_wpointer_aritih)
	if (has_wpointer_aritih)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wpointer-arith")
	endif ()

	check_c_compiler_flag(-Wunreachable-code has_wunreachable_code)
	if (has_wunreachable_code)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wunreachable-code")
	endif ()

	check_c_compiler_flag(-Winit-self has_winit_self)
	if (has_winit_self)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Winit-self")
	endif ()

	check_c_compiler_flag(-Wno-unused-function has_wno_unused_function)
	if (has_wno_unused_function)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-function")
	endif ()

	check_c_compiler_flag(-Wno-unused-parameter has_wno_unused_parameter)
	if (has_wno_unused_parameter)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")
	endif ()

	check_c_compiler_flag(-Wno-unreachable-code has_wno_unreachable_code)
	if (has_wno_unreachable_code)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unreachable-code")
	endif ()

	check_c_compiler_flag(-Wstrict-prototypes has_wstrict_prototypes)
	if (has_wstrict_prototypes)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wstrict-prototypes")
	endif ()

	if (sctp_werror)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")
	endif ()

	if (sctp_sanitizer_address)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address,undefined,signed-integer-overflow -fno-omit-frame-pointer -fno-sanitize-recover=all -fsanitize-address-use-after-scope ")
	endif ()

	if (sctp_sanitizer_memory)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=memory -fno-omit-frame-pointer -fsanitize-memory-track-origins -fPIE")
	endif ()

	if (sctp_build_fuzzer)
		set(CMAKE_BUILD_TYPE "DEBUG")
		add_definitions(-DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O1 -fsanitize=fuzzer-no-link")
	endif ()
endif ()

message(STATUS "Compiler flags (CMAKE_C_FLAGS): ${CMAKE_C_FLAGS}")


#################################################
# INCLUDE SUBDIRS
#################################################

add_subdirectory(usrsctplib)

if (sctp_build_programs)
	add_subdirectory(programs)
endif ()

if (sctp_build_fuzzer)
	add_subdirectory(fuzzer)
endif ()
