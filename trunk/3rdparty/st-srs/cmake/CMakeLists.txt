# CMake minimum version should be called first
cmake_minimum_required(VERSION 3.10)

# Name of the project.
# Language "C" is required for find_package(Threads).
if (CMAKE_VERSION VERSION_LESS 3.0)
    project(st CXX C ASM)
elseif (CMAKE_HOST_WIN32)
    # Native Windows uses MASM (ASM_MASM) below, not the GNU assembler (ASM).
    cmake_policy(SET CMP0048 NEW)
    project(st VERSION 4.0.0 LANGUAGES CXX C)
else()
    cmake_policy(SET CMP0048 NEW)
    project(st VERSION 4.0.0 LANGUAGES CXX C ASM)
endif()

# For utest required C++11.
set (CMAKE_CXX_STANDARD 11)

###########################################################
# Native Windows x64 with MSVC, the WIN64 platform of ../Makefile, which stays the main build. This gives a Visual
# Studio project of the utest for debugging, for example:
#       cmake -S cmake -B build -G "Visual Studio 17 2022" -A x64
# No bash, and the source tree is not written: st.h is copied to the build directory, so the obj junction made by
# make is left alone.
if (CMAKE_HOST_WIN32 AND NOT MSVC)
    message(FATAL_ERROR "Only MSVC is supported on Windows, for example the Visual Studio generator")
endif()
if (MSVC)
    get_filename_component(ST_DIR ${PROJECT_SOURCE_DIR}/.. ABSOLUTE)
    message("ST home is ${ST_DIR}")

    # NOMINMAX keeps windows.h, included by winsock2.h, from breaking std::min and std::max.
    ADD_DEFINITIONS("-DWIN64 -DDEBUG -DNOMINMAX")

    configure_file(${ST_DIR}/public.h ${PROJECT_BINARY_DIR}/st/st.h COPYONLY)
    INCLUDE_DIRECTORIES(${PROJECT_BINARY_DIR}/st ${ST_DIR}/utest)

    foreach (FILE event.c io.c key.c sched.c stk.c sync.c common.c)
        list(APPEND SOURCE_FILES ${ST_DIR}/${FILE})
    endforeach ()
    # The context switch is MASM assembly.
    enable_language(ASM_MASM)
    list(APPEND SOURCE_FILES ${ST_DIR}/md_win64.asm)

    # Google Test with the same DLL runtime (/MD) as the utest.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    if (NOT TARGET gtest)
        ADD_SUBDIRECTORY(${ST_DIR}/utest/gtest-fit gtest-fit)
    endif ()
    INCLUDE_DIRECTORIES(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})

    set(ST_UTEST_SOURCE_FILES ${SOURCE_FILES})
    AUX_SOURCE_DIRECTORY(${ST_DIR}/utest ST_UTEST_SOURCE_FILES)

    ADD_EXECUTABLE(st_utest ${ST_UTEST_SOURCE_FILES})
    TARGET_LINK_LIBRARIES(st_utest gtest gtest_main ws2_32)
    set_property(DIRECTORY ${PROJECT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT st_utest)

    # The tools ported to MSVC so far.
    foreach (TOOL helloworld porting verify)
        set(ST_TOOL_SOURCE_FILES ${SOURCE_FILES})
        AUX_SOURCE_DIRECTORY(${ST_DIR}/tools/${TOOL} ST_TOOL_SOURCE_FILES)
        ADD_EXECUTABLE(st_${TOOL} ${ST_TOOL_SOURCE_FILES})
        TARGET_LINK_LIBRARIES(st_${TOOL} ws2_32)
    endforeach ()

    MESSAGE(STATUS "@see https://github.com/ossrs/state-threads#usage")
    return()
endif()

###########################################################
execute_process(
        COMMAND bash -c "cd ${PROJECT_SOURCE_DIR}/../ && pwd"
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
    ADD_DEFINITIONS("-DDARWIN -DMD_HAVE_KQUEUE -DMD_HAVE_SELECT -DDEBUG")
ELSE ()
    ADD_DEFINITIONS("-DLINUX -DMD_HAVE_EPOLL -DMD_HAVE_SELECT -DDEBUG")
ENDIF ()

execute_process(COMMAND bash -c "cd ${ST_DIR} && mkdir -p obj && cp public.h obj/st.h")

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

ADD_DEFINITIONS("-g -O0 -fsanitize=address -fno-omit-frame-pointer")

###########################################################
# Setup ST utest project
if(NOT TARGET gtest)
    ADD_SUBDIRECTORY(${ST_DIR}/utest/gtest-fit gtest-fit)
endif()
INCLUDE_DIRECTORIES(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})

set(ST_UTEST_SOURCE_FILES ${SOURCE_FILES})
AUX_SOURCE_DIRECTORY(${ST_DIR}/utest ST_UTEST_SOURCE_FILES)

ADD_EXECUTABLE(st_utest ${ST_UTEST_SOURCE_FILES})
TARGET_LINK_LIBRARIES(st_utest gtest gtest_main)
TARGET_LINK_LIBRARIES(st_utest dl)
TARGET_LINK_LIBRARIES(st_utest ${DEPS_LIBS})
TARGET_LINK_LIBRARIES(st_utest -ldl -pthread)
TARGET_LINK_LIBRARIES(st_utest -fsanitize=address -fno-omit-frame-pointer)

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

