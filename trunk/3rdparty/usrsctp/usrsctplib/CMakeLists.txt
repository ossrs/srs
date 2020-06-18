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


#################################################
# INCLUDE MODULES AND SETTINGS
#################################################

set(VERSION "1.0.0")

set(prefix 					${CMAKE_INSTALL_PREFIX})
set(exec_prefix 			${prefix})
set(libdir 					${exec_prefix}/lib)
set(includedir 				${prefix}/include/usrsctp)
set(CMAKE_REQUIRED_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_MACOSX_RPATH 		1)

include(CheckCCompilerFlag)

add_definitions(-D__Userspace__)
add_definitions(-DSCTP_SIMPLE_ALLOCATOR)
add_definitions(-DSCTP_PROCESS_LEVEL_LOCKS)


#################################################
# OS DEPENDENT
#################################################

check_c_compiler_flag(-Wno-address-of-packed-member has_wno_address_of_packed_member)
if (has_wno_address_of_packed_member)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-address-of-packed-member")
endif ()

check_c_compiler_flag(-Wno-deprecated-declarations has_wno_deprecated_declarations)
if (has_wno_deprecated_declarations)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-deprecated-declarations")
endif ()

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
	add_definitions(-D_GNU_SOURCE)
endif ()

if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
	add_definitions(-D__APPLE_USE_RFC_2292)
endif ()

#################################################
# MISC
#################################################



#################################################
# LIBRARY FILES
#################################################

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

list(APPEND usrsctp_root_headers
	user_atomic.h
	user_environment.h
	user_inpcb.h
	user_ip_icmp.h
	user_ip6_var.h
	user_malloc.h
	user_mbuf.h
	user_queue.h
	user_recv_thread.h
	user_route.h
	user_socketvar.h
	user_uma.h
	usrsctp.h
)

list(APPEND usrsctp_netinet_headers
	netinet/sctp_asconf.h
	netinet/sctp_auth.h
	netinet/sctp_bsd_addr.h
	netinet/sctp_callout.h
	netinet/sctp_constants.h
	netinet/sctp_crc32.h
	netinet/sctp_header.h
	netinet/sctp_indata.h
	netinet/sctp_input.h
	netinet/sctp_lock_userspace.h
	netinet/sctp_os_userspace.h
	netinet/sctp_os.h
	netinet/sctp_output.h
	netinet/sctp_pcb.h
	netinet/sctp_peeloff.h
	netinet/sctp_process_lock.h
	netinet/sctp_sha1.h
	netinet/sctp_structs.h
	netinet/sctp_sysctl.h
	netinet/sctp_timer.h
	netinet/sctp_uio.h
	netinet/sctp_var.h
	netinet/sctputil.h
	netinet/sctp.h
)

list(APPEND usrsctp_netinet6_headers
	netinet6/sctp6_var.h
)

list(APPEND usrsctp_headers
	${usrsctp_root_headers}
	${usrsctp_netinet_headers}
	${usrsctp_netinet6_headers}
)

list(APPEND usrsctp_sources
	netinet/sctp_asconf.c
	netinet/sctp_auth.c
	netinet/sctp_bsd_addr.c
	netinet/sctp_callout.c
	netinet/sctp_cc_functions.c
	netinet/sctp_crc32.c
	netinet/sctp_indata.c
	netinet/sctp_input.c
	netinet/sctp_output.c
	netinet/sctp_pcb.c
	netinet/sctp_peeloff.c
	netinet/sctp_sha1.c
	netinet/sctp_ss_functions.c
	netinet/sctp_sysctl.c
	netinet/sctp_timer.c
	netinet/sctp_userspace.c
	netinet/sctp_usrreq.c
	netinet/sctputil.c
	netinet6/sctp6_usrreq.c
	user_environment.c
	user_mbuf.c
	user_recv_thread.c
	user_socket.c
)

add_library(usrsctp SHARED ${usrsctp_sources} ${usrsctp_headers})
add_library(usrsctp-static STATIC ${usrsctp_sources} ${usrsctp_headers})

target_include_directories(usrsctp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(usrsctp-static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (WIN32)
	message(STATUS "link library: ws2_32")
	target_link_libraries(usrsctp ws2_32 iphlpapi.lib)
	target_link_libraries(usrsctp-static ws2_32 iphlpapi.lib)
endif ()

set_target_properties(usrsctp-static PROPERTIES OUTPUT_NAME "usrsctp")
set_target_properties(usrsctp PROPERTIES IMPORT_SUFFIX "_import.lib")
set_target_properties(usrsctp PROPERTIES SOVERSION 1 VERSION 1.0.0)

if (NOT DEFINED CMAKE_INSTALL_LIBDIR)
	SET(CMAKE_INSTALL_LIBDIR lib)
endif ()


#################################################
# INSTALL LIBRARY AND HEADER
#################################################

install(TARGETS usrsctp usrsctp-static DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES usrsctp.h DESTINATION include)
