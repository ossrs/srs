cmake_minimum_required(VERSION 2.8.12)
project(srs CXX)

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
    EXEC_PROGRAM("cd ${SRS_DIR} && ./configure --osx --jobs=${JOBS}")
ELSE ()
    EXEC_PROGRAM("cd ${SRS_DIR} && ./configure --jobs=${JOBS}")
ENDIF ()

set(DEPS_LIBS ${SRS_DIR}/objs/st/libst.a
        ${SRS_DIR}/objs/openssl/lib/libssl.a
        ${SRS_DIR}/objs/openssl/lib/libcrypto.a
        ${SRS_DIR}/objs/srtp2/lib/libsrtp2.a
        ${SRS_DIR}/objs/ffmpeg/lib/libavcodec.a
        ${SRS_DIR}/objs/ffmpeg/lib/libavutil.a
        ${SRS_DIR}/objs/opus/lib/libopus.a
        ${SRS_DIR}/objs/ffmpeg/lib/libswresample.a)
foreach(DEPS_LIB ${DEPS_LIBS})
    IF (NOT EXISTS ${DEPS_LIB})
        MESSAGE(FATAL_ERROR "${DEPS_LIB} not found")
    ELSE ()
        MESSAGE("${DEPS_LIB} is ok")
    ENDIF ()
endforeach()

###########################################################
# Setup SRS project
INCLUDE_DIRECTORIES(${SRS_DIR}/objs
        ${SRS_DIR}/objs/st
        ${SRS_DIR}/objs/openssl/include
        ${SRS_DIR}/objs/srtp2/include
        ${SRS_DIR}/objs/opus/include
        ${SRS_DIR}/objs/ffmpeg/include
        ${SRS_DIR}/src/core
        ${SRS_DIR}/src/kernel
        ${SRS_DIR}/src/protocol
        ${SRS_DIR}/src/app
        ${SRS_DIR}/src/service)

set(SOURCE_FILES ${SRS_DIR}/src/main/srs_main_server.cpp)
AUX_SOURCE_DIRECTORY(${SRS_DIR}/src/core SOURCE_FILES)
AUX_SOURCE_DIRECTORY(${SRS_DIR}/src/kernel SOURCE_FILES)
AUX_SOURCE_DIRECTORY(${SRS_DIR}/src/protocol SOURCE_FILES)
AUX_SOURCE_DIRECTORY(${SRS_DIR}/src/app SOURCE_FILES)

ADD_DEFINITIONS("-g -O0")

ADD_EXECUTABLE(srs ${SOURCE_FILES})
TARGET_LINK_LIBRARIES(srs dl)
TARGET_LINK_LIBRARIES(srs ${DEPS_LIBS})
TARGET_LINK_LIBRARIES(srs -ldl -pthread)

MESSAGE(STATUS "@see https://github.com/ossrs/srs/wiki/v4_CN_IDE")

