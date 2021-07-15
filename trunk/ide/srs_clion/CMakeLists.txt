cmake_minimum_required(VERSION 2.8.12)
project(srs CXX)

###########################################################
# Start to configure SRS with jobs of number of CPUs.
include(ProcessorCount)
ProcessorCount(JOBS)

set(DEPS_LIBS ${PROJECT_SOURCE_DIR}/../../objs/st/libst.a
        ${PROJECT_SOURCE_DIR}/../../objs/openssl/lib/libssl.a
        ${PROJECT_SOURCE_DIR}/../../objs/openssl/lib/libcrypto.a
        ${PROJECT_SOURCE_DIR}/../../objs/srtp2/lib/libsrtp2.a
        ${PROJECT_SOURCE_DIR}/../../objs/opus/lib/libopus.a
        ${PROJECT_SOURCE_DIR}/../../objs/ffmpeg/lib/libavutil.a
        ${PROJECT_SOURCE_DIR}/../../objs/ffmpeg/lib/libavcodec.a
        ${PROJECT_SOURCE_DIR}/../../objs/ffmpeg/lib/libswresample.a)
foreach(DEPS_LIB ${DEPS_LIBS})
    IF (NOT EXISTS ${DEPS_LIB})
        MESSAGE("${DEPS_LIB} not found")
        IF (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
            EXEC_PROGRAM("cd ../../.. && ./configure --osx --jobs=${JOBS}")
        ELSE ()
            EXEC_PROGRAM("cd ../../.. && ./configure --jobs=${JOBS}")
        ENDIF ()
    ELSE ()
        MESSAGE("${DEPS_LIB} is ok")
    ENDIF ()
endforeach()

###########################################################
# Setup SRS project
INCLUDE_DIRECTORIES(../../objs
        ../../objs/st
        ../../objs/hp
        ../../objs/openssl/include
        ../../src/core
        ../../src/kernel
        ../../src/protocol
        ../../src/app
        ../../src/service
        ../../objs/srtp2/include
        ../../objs/opus/include
        ../../objs/ffmpeg/include)

set(SOURCE_FILES ../../src/main/srs_main_server.cpp)
AUX_SOURCE_DIRECTORY(../../src/core SOURCE_FILES)
AUX_SOURCE_DIRECTORY(../../src/kernel SOURCE_FILES)
AUX_SOURCE_DIRECTORY(../../src/protocol SOURCE_FILES)
AUX_SOURCE_DIRECTORY(../../src/app SOURCE_FILES)

ADD_DEFINITIONS("-g -O0")

ADD_EXECUTABLE(srs ${SOURCE_FILES})
TARGET_LINK_LIBRARIES(srs dl)
TARGET_LINK_LIBRARIES(srs ${DEPS_LIBS})
TARGET_LINK_LIBRARIES(srs -ldl)

MESSAGE(STATUS "@see https://github.com/ossrs/srs/wiki/v4_CN_IDE")

