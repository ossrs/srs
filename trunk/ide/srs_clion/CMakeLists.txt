cmake_minimum_required(VERSION 2.6.4)
project(srs CXX)

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    MESSAGE("[Linux] srs_libs not found")
    IF(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../../objs/st/libst.a)
        EXEC_PROGRAM("cd ${CMAKE_CURRENT_SOURCE_DIR}/../../ && ./configure")
    ENDIF(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../../objs/st/libst.a)
elseif (CMAKE_SYSTEM_NAME MATCHES "Darwin")
    MESSAGE("[OSX] srs_libs not found")
    IF(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../../objs/st/libst.a)
        EXEC_PROGRAM("cd ${CMAKE_CURRENT_SOURCE_DIR}/../../ && ./configure --osx")
    ENDIF(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../../objs/st/libst.a)
else()
	message(FATAL_ERROR "unknown CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")
endif()   


ADD_CUSTOM_TARGET(build_srs ALL
        COMMAND make
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../
        COMMENT "build_srs makefile target")

MESSAGE(STATUS "only for jetbrains IDE, @see https://github.com/ossrs/srs/wiki/v1_CN_IDE#jetbrains")
MESSAGE(STATUS "use ./configure && make, @see https://github.com/ossrs/srs#usage")

