#
# Copyright (C) 2015-2015 Oleg Alexeenkov
# Copyright (C) 2015-2020 Felix Weinrank
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
# INCLUDE MODULES
#################################################

include(CheckIncludeFile)


#################################################
# CHECK INCLUDES
#################################################

include_directories(${CMAKE_SOURCE_DIR}/usrsctplib)


#################################################
# OS DEPENDENT
#################################################

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
	add_definitions(-D_GNU_SOURCE)
endif ()

if (CMAKE_SYSTEM_NAME MATCHES "Darwin" OR CMAKE_SYSTEM_NAME MATCHES "iOS")
	add_definitions(-D__APPLE_USE_RFC_2292)
endif ()

if (MSYS OR MINGW)
	message(STATUS "MSYS / MINGW")
	add_definitions(-D__USE_MINGW_ANSI_STDIO)
	# 0x0601 = Windows 7 API
	add_definitions(-DWINVER=0x0601)
	add_definitions(-D_WIN32_WINNT=0x0601)

	if (CMAKE_C_COMPILER_ID MATCHES "GNU")
		message(STATUS "MSYS / MINGW + GCC")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-format")
	endif ()
endif ()


#################################################
# MISC
#################################################

find_package(Threads)


#################################################
# PROGRAMS
#################################################

list(APPEND check_programs
	chargen_server_upcall.c
	client.c
	client_upcall.c
	daytime_server.c
	daytime_server_upcall.c
	discard_server.c
	discard_server_upcall.c
	echo_server.c
	echo_server_upcall.c
	ekr_client.c
	ekr_loop.c
	ekr_loop_upcall.c
	ekr_peer.c
	ekr_server.c
	http_client.c
	http_client_upcall.c
	rtcweb.c
	st_client.c
	test_libmgmt.c
	test_timer.c
	tsctp.c
	tsctp_upcall.c
)

foreach (source_file ${check_programs})
	get_filename_component(source_file_we ${source_file} NAME_WE)
	add_executable(
		${source_file_we}
		${source_file}
		programs_helper.c
	)

	target_link_libraries(
		${source_file_we}
		"usrsctp"
		${CMAKE_THREAD_LIBS_INIT}
	)

	if (APPLE)
		set_target_properties(
			${source_file_we}
			PROPERTIES
			XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
			com.github.sctplab.usrsctp.${source_file_we}
		)
	endif()
endforeach ()
