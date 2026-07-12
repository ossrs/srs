#
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

#project(usrsctp-fuzzer C)

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

if (CMAKE_SYSTEM_NAME MATCHES "Darwin")
	add_definitions(-D__APPLE_USE_RFC_2292)
endif ()

#################################################
# COMPILER FLAGS
#################################################

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-gnu-zero-variadic-macro-arguments -Wno-unused-variable")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=fuzzer")


#################################################
# PROGRAMS
#################################################

# FUZZING_STAGE LEVELS
# 0 - MULTI - based on input
# 1 - COOKIE_WAIT
# 2 - COOKIE_ECHO
# 3 - ESTABLISHED
# 4 - DATA SENT
# 5 - DATA RECEIVED

add_executable(fuzzer_listen fuzzer_listen.c ../programs/programs_helper.c)
target_link_libraries(fuzzer_listen usrsctp)

add_executable(fuzzer_listen_verbose fuzzer_listen.c ../programs/programs_helper.c)
target_compile_definitions(fuzzer_listen_verbose PRIVATE FUZZ_VERBOSE)
target_link_libraries(fuzzer_listen_verbose usrsctp)

add_executable(fuzzer_fragment fuzzer_fragment.c ../programs/programs_helper.c)
target_link_libraries(fuzzer_fragment usrsctp)

add_executable(fuzzer_connect_multi fuzzer_connect.c ../programs/programs_helper.c)
target_compile_definitions(fuzzer_connect_multi PRIVATE FUZZING_STAGE=0)
target_link_libraries(fuzzer_connect_multi usrsctp)

add_executable(fuzzer_connect_multi_verbose fuzzer_connect.c ../programs/programs_helper.c)
target_compile_definitions(fuzzer_connect_multi_verbose PRIVATE FUZZING_STAGE=0 FUZZ_VERBOSE)
target_link_libraries(fuzzer_connect_multi_verbose usrsctp)

# add_executable(fuzzer_connect_cookie_wait fuzzer_connect.c)
# target_compile_definitions(fuzzer_connect_cookie_wait PRIVATE FUZZING_STAGE=1)
# target_link_libraries(fuzzer_connect_cookie_wait usrsctp)

# add_executable(fuzzer_connect_cookie_echoed fuzzer_connect.c)
# target_compile_definitions(fuzzer_connect_cookie_echoed PRIVATE FUZZING_STAGE=2)
# target_link_libraries(fuzzer_connect_cookie_echoed usrsctp)

# add_executable(fuzzer_connect_established fuzzer_connect.c)
# target_compile_definitions(fuzzer_connect_established PRIVATE FUZZING_STAGE=3)
# target_link_libraries(fuzzer_connect_established usrsctp)

# add_executable(fuzzer_connect_data_sent fuzzer_connect.c)
# target_compile_definitions(fuzzer_connect_data_sent PRIVATE FUZZING_STAGE=4)
# target_link_libraries(fuzzer_connect_data_sent usrsctp)

# add_executable(fuzzer_connect_data_received fuzzer_connect.c)
# target_compile_definitions(fuzzer_connect_data_received PRIVATE FUZZING_STAGE=5)
# target_link_libraries(fuzzer_connect_data_received usrsctp)
