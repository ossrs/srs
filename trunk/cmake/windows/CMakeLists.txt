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
