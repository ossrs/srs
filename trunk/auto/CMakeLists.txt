cmake_minimum_required (VERSION 2.6)
project(srs_librtmp)

option(BUILD_WITH_TEST "build with demo " OFF)
option(BUILD_WITH_STATIC "build with static" ON)
option(BUILD_WITH_SHARD "build with shard" OFF)

#configure_file(${CMAKE_CURRENT_LIST_DIR}srs_auto_headers.hpp.in )
set(SRC_ROOT ${CMAKE_CURRENT_LIST_DIR}/src)
include_directories(${CMAKE_CURRENT_LIST_DIR}/objs)

include_directories(${SRC_ROOT}/core)
include_directories(${SRC_ROOT}/kernel)
include_directories(${SRC_ROOT}/libs)
include_directories(${SRC_ROOT}/protocol)

file(GLOB OBJS_HEADERS "${CMAKE_CURRENT_LIST_DIR}/objs/*.h*" )

file(GLOB CORE_HEADERS "${SRC_ROOT}/core/*.h*" )
file(GLOB CORE_SRCS "${SRC_ROOT}/core/*.c*")

file(GLOB KERNEL_HEADERS "${SRC_ROOT}/kernel/*.h*" )
file(GLOB KERNEL_SRCS "${SRC_ROOT}/kernel/*.c*")

file(GLOB LIBS_HEADERS "${SRC_ROOT}/libs/*.h*" )
file(GLOB LIBS_SRCS "${SRC_ROOT}/libs/*.c*")

file(GLOB PROTOCOL_HEADERS "${SRC_ROOT}/protocol/*.h*" )
file(GLOB PROTOCOL_SRCS "${SRC_ROOT}/protocol/*.c*")

if(BUILD_WITH_STATIC)
    add_library(srs_librtmp STATIC ${OBJS_HEADERS}
    ${CORE_HEADERS} ${CORE_SRCS}
    ${KERNEL_HEADERS} ${KERNEL_SRCS}
    ${LIBS_HEADERS} ${LIBS_SRCS}
    ${PROTOCOL_HEADERS} ${PROTOCOL_SRCS})
    install(TARGETS srs_librtmp
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib )
endif()

if(BUILD_WITH_SHARD)
    set(LIB_NAME srs_librtmp)
    if(BUILD_WITH_STATIC)
        set(LIB_NAME srs_librtmp_shared)
    endif()

    add_library(${LIB_NAME} SHARED ${ROOT_HEADERS} ${ROOT_SRCS}
    ${CORE_HEADERS} ${CORE_SRCS}
    ${KERNEL_HEADERS} ${KERNEL_SRCS}
    ${LIBS_HEADERS} ${LIBS_SRCS}
    ${PROTOCOL_HEADERS} ${PROTOCOL_SRCS})

    install(TARGETS ${LIB_NAME}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib )
endif()

if(BUILD_WITH_TEST)

if(UNIX)
	set(EXTRA_LIBS ${EXTRA_LIBS} stdc++ z pthread dl)
elseif(MSVC)
	 set(EXTRA_LIBS ${EXTRA_LIBS} ws2_32 winmm vfw32 wininet Userenv)
endif()

add_executable(srs_aac_raw_publish ${CMAKE_CURRENT_LIST_DIR}/research/librtmp/srs_aac_raw_publish.c)
TARGET_LINK_LIBRARIES(srs_aac_raw_publish srs_librtmp ${EXTRA_LIBS})

add_executable(srs_bandwidth_check ${CMAKE_CURRENT_LIST_DIR}/research/librtmp/srs_bandwidth_check.c)
TARGET_LINK_LIBRARIES(srs_bandwidth_check srs_librtmp ${EXTRA_LIBS})

add_executable(srs_detect_rtmp ${CMAKE_CURRENT_LIST_DIR}/research/librtmp/srs_detect_rtmp.c)
TARGET_LINK_LIBRARIES(srs_detect_rtmp srs_librtmp ${EXTRA_LIBS})

add_executable(srs_flv_injecter ${CMAKE_CURRENT_LIST_DIR}/research/librtmp/srs_flv_injecter.c)
TARGET_LINK_LIBRARIES(srs_flv_injecter srs_librtmp ${EXTRA_LIBS})

add_executable(srs_flv_parser ${CMAKE_CURRENT_LIST_DIR}/research/librtmp/srs_flv_parser.c)
TARGET_LINK_LIBRARIES(srs_flv_parser srs_librtmp ${EXTRA_LIBS})

add_executable(srs_h264_raw_publish ${CMAKE_CURRENT_LIST_DIR}/research/librtmp/srs_h264_raw_publish.c)
TARGET_LINK_LIBRARIES(srs_h264_raw_publish srs_librtmp ${EXTRA_LIBS})

add_executable(srs_ingest_flv ${CMAKE_CURRENT_LIST_DIR}/research/librtmp/srs_ingest_flv.c)
TARGET_LINK_LIBRARIES(srs_ingest_flv srs_librtmp ${EXTRA_LIBS})

add_executable(srs_ingest_rtmp ${CMAKE_CURRENT_LIST_DIR}/research/librtmp/srs_ingest_rtmp.c)
TARGET_LINK_LIBRARIES(srs_ingest_rtmp srs_librtmp ${EXTRA_LIBS})

add_executable(srs_play ${CMAKE_CURRENT_LIST_DIR}/research/librtmp/srs_play.c)
TARGET_LINK_LIBRARIES(srs_play srs_librtmp ${EXTRA_LIBS})

add_executable(srs_publish ${CMAKE_CURRENT_LIST_DIR}/research/librtmp/srs_publish.c)
TARGET_LINK_LIBRARIES(srs_publish srs_librtmp ${EXTRA_LIBS})

add_executable(srs_rtmp_dump ${CMAKE_CURRENT_LIST_DIR}/research/librtmp/srs_rtmp_dump.c)
TARGET_LINK_LIBRARIES(srs_rtmp_dump srs_librtmp ${EXTRA_LIBS})
endif()

install(FILES ${ROOT_HEADERS}  ${CORE_HEADERS} ${KERNEL_HEADERS} ${LIBS_HEADERS}  ${PROTOCOL_HEADERS} DESTINATION include )

install(TARGETS ${PG_LIB_NAME}
	RUNTIME DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib )