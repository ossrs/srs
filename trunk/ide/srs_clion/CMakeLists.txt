# Name of the project.
# Language "C" is required for find_package(Threads).
if (CMAKE_VERSION VERSION_LESS 3.0)
    project(srs CXX C)
else()
    cmake_policy(SET CMP0048 NEW)
    project(srs VERSION 4.0.0 LANGUAGES CXX C)
endif()
cmake_minimum_required(VERSION 2.8.12)

# For utest required C++11.
set (CMAKE_CXX_STANDARD 11)

###########################################################
execute_process(
        COMMAND bash -c "cd ${PROJECT_SOURCE_DIR}/../../ && pwd"
        OUTPUT_VARIABLE SRS_DIR
)
string(STRIP ${SRS_DIR} SRS_DIR)
message("SRS home is ${SRS_DIR}")

###########################################################
# Start to configure SRS with jobs of number of CPUs.
include(ProcessorCount)
ProcessorCount(JOBS)

# We should always configure SRS for switching between branches.
IF (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    EXEC_PROGRAM("cd ${SRS_DIR} && ./configure --osx --srt=on --utest=on --jobs=${JOBS}")
ELSE ()
    EXEC_PROGRAM("cd ${SRS_DIR} && ./configure --srt=on --utest=on --jobs=${JOBS}")
ENDIF ()

set(DEPS_LIBS ${SRS_DIR}/objs/st/libst.a
        ${SRS_DIR}/objs/openssl/lib/libssl.a
        ${SRS_DIR}/objs/openssl/lib/libcrypto.a
        ${SRS_DIR}/objs/srtp2/lib/libsrtp2.a
        ${SRS_DIR}/objs/ffmpeg/lib/libavcodec.a
        ${SRS_DIR}/objs/ffmpeg/lib/libavutil.a
        ${SRS_DIR}/objs/opus/lib/libopus.a
        ${SRS_DIR}/objs/ffmpeg/lib/libswresample.a
        ${SRS_DIR}/objs/srt/lib/libsrt.a)
foreach(DEPS_LIB ${DEPS_LIBS})
    IF (NOT EXISTS ${DEPS_LIB})
        MESSAGE(FATAL_ERROR "${DEPS_LIB} not found")
    ELSE ()
        MESSAGE("${DEPS_LIB} is ok")
    ENDIF ()
endforeach()

###########################################################
# For whole project.
INCLUDE_DIRECTORIES(${SRS_DIR}/objs
        ${SRS_DIR}/objs/st
        ${SRS_DIR}/objs/openssl/include
        ${SRS_DIR}/objs/srtp2/include
        ${SRS_DIR}/objs/opus/include
        ${SRS_DIR}/objs/ffmpeg/include
        ${SRS_DIR}/objs/srt/include
        ${SRS_DIR}/src/core
        ${SRS_DIR}/src/kernel
        ${SRS_DIR}/src/protocol
        ${SRS_DIR}/src/app
        ${SRS_DIR}/src/service
        ${SRS_DIR}/src/srt)

# Common used sources for SRS and utest.
AUX_SOURCE_DIRECTORY(${SRS_DIR}/src/core SOURCE_FILES)
AUX_SOURCE_DIRECTORY(${SRS_DIR}/src/kernel SOURCE_FILES)
AUX_SOURCE_DIRECTORY(${SRS_DIR}/src/protocol SOURCE_FILES)
AUX_SOURCE_DIRECTORY(${SRS_DIR}/src/app SOURCE_FILES)
AUX_SOURCE_DIRECTORY(${SRS_DIR}/src/srt SOURCE_FILES)

# Remove the duplicated test main for srt.
# TODO: FIMXE: Remove the file directly, use utest or main or research.
list(REMOVE_ITEM SOURCE_FILES ${SRS_DIR}/src/srt/ts_demux_test.cpp)

ADD_DEFINITIONS("-g -O0")

###########################################################
# Setup SRS project

set(SRS_SOURCE_FILES ${SOURCE_FILES})
list(APPEND SRS_SOURCE_FILES ${SRS_DIR}/src/main/srs_main_server.cpp)

ADD_EXECUTABLE(srs ${SRS_SOURCE_FILES})
TARGET_LINK_LIBRARIES(srs dl)
TARGET_LINK_LIBRARIES(srs ${DEPS_LIBS})
TARGET_LINK_LIBRARIES(srs -ldl -pthread)

###########################################################
# For utest.
# See https://google.github.io/googletest/quickstart-cmake.html
# See https://stackoverflow.com/a/21479008/17679565
ADD_SUBDIRECTORY(${SRS_DIR}/3rdparty/gtest-fit gtest-fit)
INCLUDE_DIRECTORIES(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})

INCLUDE_DIRECTORIES(${SRS_DIR}/src/utest)
set(UTEST_SOURCE_FILES ${SOURCE_FILES})
AUX_SOURCE_DIRECTORY(${SRS_DIR}/src/utest UTEST_SOURCE_FILES)

ADD_EXECUTABLE(utest ${UTEST_SOURCE_FILES})
TARGET_LINK_LIBRARIES(utest gtest gtest_main)
TARGET_LINK_LIBRARIES(utest dl)
TARGET_LINK_LIBRARIES(utest ${DEPS_LIBS})
TARGET_LINK_LIBRARIES(utest -ldl -pthread)

###########################################################
# Done
MESSAGE(STATUS "@see https://github.com/ossrs/srs/wiki/v4_CN_IDE")

