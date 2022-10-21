# Name of the project.
# Language "C" is required for find_package(Threads).
if (CMAKE_VERSION VERSION_LESS 3.0)
    project(st CXX C ASM)
else()
    cmake_policy(SET CMP0048 NEW)
    project(st VERSION 4.0.0 LANGUAGES CXX C ASM)
endif()
cmake_minimum_required(VERSION 2.8.12)

# For utest required C++11.
set (CMAKE_CXX_STANDARD 11)

###########################################################
execute_process(
        COMMAND bash -c "cd ${PROJECT_SOURCE_DIR}/../../ && pwd"
        OUTPUT_VARIABLE ST_DIR
)
string(STRIP ${ST_DIR} ST_DIR)
message("ST home is ${ST_DIR}")

###########################################################
# Start to configure ST with jobs of number of CPUs.
include(ProcessorCount)
ProcessorCount(JOBS)

# We should always configure ST for switching between branches.
IF (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    ADD_DEFINITIONS("-arch x86_64 -DDARWIN -DMD_HAVE_KQUEUE -DMD_HAVE_SELECT -DDEBUG")
ELSE ()
    ADD_DEFINITIONS("-DLINUX -DMD_HAVE_EPOLL -DMD_HAVE_SELECT -DDEBUG")
ENDIF ()

EXEC_PROGRAM("cd ${ST_DIR} && mkdir -p obj && cp public.h obj/st.h")

###########################################################
# For whole project.
INCLUDE_DIRECTORIES(${ST_DIR}/obj ${ST_DIR}/utest ${ST_DIR}/utest/gtest/include)

# Common used sources for SRS and utest.
list(APPEND SOURCE_FILES ${ST_DIR}/event.c)
list(APPEND SOURCE_FILES ${ST_DIR}/io.c)
list(APPEND SOURCE_FILES ${ST_DIR}/key.c)
list(APPEND SOURCE_FILES ${ST_DIR}/sched.c)
list(APPEND SOURCE_FILES ${ST_DIR}/stk.c)
list(APPEND SOURCE_FILES ${ST_DIR}/sync.c)
IF (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    list(APPEND SOURCE_FILES ${ST_DIR}/md_darwin.S)
ELSE ()
    list(APPEND SOURCE_FILES ${ST_DIR}/md_linux.S)
    list(APPEND SOURCE_FILES ${ST_DIR}/md_linux2.S)
ENDIF ()

ADD_DEFINITIONS("-g -O0")

###########################################################
# Setup ST utest project
ADD_SUBDIRECTORY(${ST_DIR}/utest/gtest-fit gtest-fit)
INCLUDE_DIRECTORIES(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})

set(ST_UTEST_SOURCE_FILES ${SOURCE_FILES})
AUX_SOURCE_DIRECTORY(${ST_DIR}/utest ST_UTEST_SOURCE_FILES)

ADD_EXECUTABLE(st_utest ${ST_UTEST_SOURCE_FILES})
TARGET_LINK_LIBRARIES(st_utest gtest gtest_main)
TARGET_LINK_LIBRARIES(st_utest dl)
TARGET_LINK_LIBRARIES(st_utest ${DEPS_LIBS})
TARGET_LINK_LIBRARIES(st_utest -ldl -pthread)

###########################################################
# Setup tools/backtrace project
set(ST_BACKTRACE_SOURCE_FILES ${SOURCE_FILES})
AUX_SOURCE_DIRECTORY(${ST_DIR}/tools/backtrace ST_BACKTRACE_SOURCE_FILES)

ADD_EXECUTABLE(st_backtrace ${ST_BACKTRACE_SOURCE_FILES})
TARGET_LINK_LIBRARIES(st_backtrace ${DEPS_LIBS})
TARGET_LINK_LIBRARIES(st_backtrace -ldl)

###########################################################
# Setup tools/helloworld project
set(ST_HELLOWORLD_SOURCE_FILES ${SOURCE_FILES})
AUX_SOURCE_DIRECTORY(${ST_DIR}/tools/helloworld ST_HELLOWORLD_SOURCE_FILES)

ADD_EXECUTABLE(st_helloworld ${ST_HELLOWORLD_SOURCE_FILES})
TARGET_LINK_LIBRARIES(st_helloworld ${DEPS_LIBS})

###########################################################
# Setup tools/jmpbuf project
set(ST_JMPBUF_SOURCE_FILES ${SOURCE_FILES})
AUX_SOURCE_DIRECTORY(${ST_DIR}/tools/jmpbuf ST_JMPBUF_SOURCE_FILES)

ADD_EXECUTABLE(st_jmpbuf ${ST_JMPBUF_SOURCE_FILES})
TARGET_LINK_LIBRARIES(st_jmpbuf ${DEPS_LIBS})

###########################################################
# Setup tools/pcs project
set(ST_PCS_SOURCE_FILES ${SOURCE_FILES})
AUX_SOURCE_DIRECTORY(${ST_DIR}/tools/pcs ST_PCS_SOURCE_FILES)

ADD_EXECUTABLE(st_pcs ${ST_PCS_SOURCE_FILES})
TARGET_LINK_LIBRARIES(st_pcs ${DEPS_LIBS})

###########################################################
# Setup tools/porting project
set(ST_PORTING_SOURCE_FILES ${SOURCE_FILES})
AUX_SOURCE_DIRECTORY(${ST_DIR}/tools/porting ST_PORTING_SOURCE_FILES)

ADD_EXECUTABLE(st_porting ${ST_PORTING_SOURCE_FILES})
TARGET_LINK_LIBRARIES(st_porting ${DEPS_LIBS})

###########################################################
# Setup tools/stack project
set(ST_STACK_SOURCE_FILES ${SOURCE_FILES})
AUX_SOURCE_DIRECTORY(${ST_DIR}/tools/stack ST_STACK_SOURCE_FILES)

ADD_EXECUTABLE(st_stack ${ST_STACK_SOURCE_FILES})
TARGET_LINK_LIBRARIES(st_stack ${DEPS_LIBS})

###########################################################
# Setup tools/verify project
set(ST_VERIFY_SOURCE_FILES ${SOURCE_FILES})
AUX_SOURCE_DIRECTORY(${ST_DIR}/tools/verify ST_VERIFY_SOURCE_FILES)

ADD_EXECUTABLE(st_verify ${ST_VERIFY_SOURCE_FILES})
TARGET_LINK_LIBRARIES(st_verify ${DEPS_LIBS})

###########################################################
# Done
MESSAGE(STATUS "@see https://github.com/ossrs/state-threads#usage")

