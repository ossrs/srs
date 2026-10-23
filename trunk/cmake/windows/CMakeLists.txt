# The native Windows x64 build with MSVC. Linux and macOS build with ./configure.
#
# The third-party libraries come from vcpkg in manifest mode, with the manifest trunk/vcpkg.json. The
# vcpkg of Visual Studio, which vcvars64 points VCPKG_ROOT at, is used unless a toolchain is given.
cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
  file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}" SRS_VCPKG_ROOT)
  set(CMAKE_TOOLCHAIN_FILE "${SRS_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "")
endif()
set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "")
# Static libraries with the static release CRT (/MT), which is also what ST's win64 build uses, since
# it gives cl no runtime option. SRS builds with it too, or the link mixes two CRTs.
set(VCPKG_TARGET_TRIPLET x64-windows-static-release CACHE STRING "")
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)

project(srs LANGUAGES C CXX)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Only MSVC x64 is supported; on Linux and macOS use ./configure")
endif()

find_package(OpenSSL 3 REQUIRED)
find_package(libSRTP CONFIG REQUIRED)
# The vcpkg libsrt port installs no CMake package, only a pkg-config file.
find_path(SRT_INCLUDE_DIR srt/srt.h REQUIRED)
find_library(SRT_LIBRARY NAMES srt_static srt REQUIRED)
add_library(srt_lib INTERFACE)
target_include_directories(srt_lib INTERFACE "${SRT_INCLUDE_DIR}")
target_link_libraries(srt_lib INTERFACE "${SRT_LIBRARY}" OpenSSL::Crypto ws2_32)
find_package(Opus CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

# ST, which srs-windows-runner.sh builds with make win64-debug into objs/windows/st before it
# configures this build.
set(SRS_ST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../objs/windows/st" CACHE PATH "")
add_library(st STATIC IMPORTED)
set_target_properties(st PROPERTIES
  IMPORTED_LOCATION "${SRS_ST_DIR}/libst.a"
  INTERFACE_INCLUDE_DIRECTORIES "${SRS_ST_DIR}"
  INTERFACE_LINK_LIBRARIES ws2_32)

# FFmpeg-fit, which srs-windows-runner.sh builds under MSYS2 into objs/windows/ffmpeg after this build
# is configured, since its libopus comes from the vcpkg install here. Not an imported target, which
# would need the include directory to exist now.
set(SRS_FFMPEG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../objs/windows/ffmpeg" CACHE PATH "")
add_library(ffmpeg INTERFACE)
target_include_directories(ffmpeg INTERFACE "${SRS_FFMPEG_DIR}/include")
target_link_libraries(ffmpeg INTERFACE
  "${SRS_FFMPEG_DIR}/lib/libavcodec.a"
  "${SRS_FFMPEG_DIR}/lib/libswresample.a"
  "${SRS_FFMPEG_DIR}/lib/libavutil.a"
  Opus::opus
  bcrypt)

add_subdirectory(deps-check)

# srs.exe, from the sources ./configure builds for the same options.
option(SRS_RTSP "RTSP server" OFF)
option(SRS_GB28181 "GB28181 over TCP" OFF)
option(SRS_FFMPEG_FIT "FFmpeg-fit for AAC and Opus in WebRTC" ON)
option(SRS_SIMULATOR "RTC simulator API" OFF)
option(SRS_UTEST "Build for the unit tests" OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/../srs-modules.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/srs-auto-headers.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${SRS_TRUNK_DIR}/auto/module_files.txt" "${SRS_TRUNK_DIR}/AUTHORS.md")
srs_write_auto_headers("${CMAKE_BINARY_DIR}/srs_auto_headers.hpp")
srs_server_sources(SRS_SOURCES)
# For srs-windows-runner.sh, which counts the files that compile per module.
string(REPLACE ";" "\n" text "${SRS_SOURCES}")
file(WRITE "${CMAKE_BINARY_DIR}/srs-sources.txt" "${text}\n")

# The modules but main build once, into srs_modules, which srs.exe and srs_utest.exe both link.
srs_module_files(SERVER SRS_MAIN_SOURCES)
set(SRS_MODULE_SOURCES ${SRS_SOURCES})
list(REMOVE_ITEM SRS_MODULE_SOURCES ${SRS_MAIN_SOURCES})
list(TRANSFORM SRS_MODULE_SOURCES PREPEND "${SRS_TRUNK_DIR}/")
list(TRANSFORM SRS_MAIN_SOURCES PREPEND "${SRS_TRUNK_DIR}/")
add_library(srs_modules OBJECT ${SRS_MODULE_SOURCES})
target_include_directories(srs_modules PUBLIC
  "${CMAKE_BINARY_DIR}"
  "${SRS_TRUNK_DIR}/src/core"
  "${SRS_TRUNK_DIR}/src/kernel"
  "${SRS_TRUNK_DIR}/src/protocol"
  "${SRS_TRUNK_DIR}/src/app")
# NOMINMAX keeps windows.h from defining min and max over std::min and std::max. The sources call
# the POSIX names MSVC has, such as getpid and unlink, on purpose. The sources hold UTF-8 text,
# which cl reads in the system code page without /utf-8. SRS calls OpenSSL 1.x APIs that OpenSSL 3
# deprecates but still has; the GCC pragmas that silence them on Unix do nothing in cl.
target_compile_definitions(srs_modules PUBLIC
  WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS
  _WINSOCK_DEPRECATED_NO_WARNINGS OPENSSL_API_COMPAT=0x10000000L)
target_compile_options(srs_modules PUBLIC /W3 /utf-8)
# ST's context switch does not keep the shadow stack that /CETCOMPAT turns on.
target_link_options(srs_modules PUBLIC /CETCOMPAT:NO)
target_link_libraries(srs_modules PUBLIC st OpenSSL::SSL OpenSSL::Crypto libSRTP::srtp2 srt_lib ws2_32 iphlpapi)
if(SRS_FFMPEG_FIT)
  target_link_libraries(srs_modules PUBLIC ffmpeg)
endif()

# The manifest sets the UTF-8 code page, so file names in UTF-8 work as on Unix.
add_executable(srs ${SRS_MAIN_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/srs.manifest")
target_include_directories(srs PRIVATE "${SRS_TRUNK_DIR}/src/main")
target_link_libraries(srs PRIVATE srs_modules)
set(SRS_TARGETS srs_modules srs)

# srs_utest.exe, with SRS_UTEST on, as ./configure builds it with --utest=on. Its main is in
# srs_utest.cpp, so it links gtest without gtest_main.
if(SRS_UTEST)
  srs_utest_sources(SRS_UTEST_SOURCES)
  list(REMOVE_ITEM SRS_UTEST_SOURCES ${SRS_SOURCES})
  # For srs-windows-runner.sh, which counts the utest files that compile.
  string(REPLACE ";" "\n" text "${SRS_UTEST_SOURCES}")
  file(WRITE "${CMAKE_BINARY_DIR}/utest-sources.txt" "${text}\n")
  list(TRANSFORM SRS_UTEST_SOURCES PREPEND "${SRS_TRUNK_DIR}/")
  add_executable(srs_utest ${SRS_UTEST_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/srs.manifest")
  target_include_directories(srs_utest PRIVATE "${SRS_TRUNK_DIR}/src/utest")
  target_link_libraries(srs_utest PRIVATE srs_modules GTest::gtest)
  list(APPEND SRS_TARGETS srs_utest)
endif()

set_target_properties(${SRS_TARGETS} PROPERTIES
  CXX_STANDARD 14
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The Visual Studio solution, which srs-windows-runner.sh vs generates: F5 starts srs.exe, and both
# programs run in trunk, where the paths of the config files lead.
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT srs)
set_target_properties(srs PROPERTIES
  VS_DEBUGGER_WORKING_DIRECTORY "${SRS_TRUNK_DIR}"
  VS_DEBUGGER_COMMAND_ARGUMENTS "-c conf/console.conf")
if(SRS_UTEST)
  set_target_properties(srs_utest PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${SRS_TRUNK_DIR}")
endif()
# The project ids come from the target names, not from the build directory as CMake makes them by
# default, so .vs/srs.slnx, where Visual Studio writes them, is the same on every machine.
if(CMAKE_GENERATOR MATCHES "^Visual Studio")
  get_property(targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  get_property(checks DIRECTORY deps-check PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target ALL_BUILD ZERO_CHECK ${targets} ${checks})
    string(UUID guid NAMESPACE 6ba7b811-9dad-11d1-80b4-00c04fd430c8 NAME "ossrs/srs/${target}" TYPE MD5)
    set(${target}_GUID_CMAKE "${guid}" CACHE INTERNAL "")
  endforeach()
endif()
