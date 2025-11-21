# This file is based off of the Platform/Darwin.cmake and Platform/UnixPaths.cmake
# files which are included with CMake 2.8.4
# It has been altered for VISIONOS development

# Options:
#
# VISION_PLATFORM = OS (default) or SIMULATOR or SIMULATOR64
#   This decides if SDKS will be selected from the XROS.platform or XRSimulator.platform folders
#   OS - the default, used to build for Vision Pro physical device, which have an arm arch.
#   SIMULATOR - used to build for the Simulator platforms, which have an x86 arch.
#
# VISIONOS_ARCH = arm64 (default for OS), x86_64 (addiitonal support for SIMULATOR64)
#
# CMAKE_VISIONOS_DEVELOPER_ROOT = automatic(default) or /path/to/platform/Developer folder
#   By default this location is automatcially chosen based on the VISIONOS_PLATFORM value above.
#   If set manually, it will override the default location and force the user of a particular Developer Platform
#
# CMAKE_VISIONOS_SDK_ROOT = automatic(default) or /path/to/platform/Developer/SDKs/SDK folder
#   By default this location is automatcially chosen based on the CMAKE_VISIONOS_DEVELOPER_ROOT value.
#   In this case it will always be the most up-to-date SDK found in the CMAKE_VISIONOS_DEVELOPER_ROOT path.
#   If set manually, this will force the use of a specific SDK version
#

# Standard settings
set (CMAKE_SYSTEM_NAME Darwin)
set (CMAKE_SYSTEM_VERSION 1)
set (UNIX True)
set (APPLE True)
set (VISIONOS True)

# Required as of cmake 2.8.10
set (CMAKE_OSX_DEPLOYMENT_TARGET "" CACHE STRING "Force unset of the deployment target for visionOs" FORCE)

# Determine the cmake host system version so we know where to find the visionOS SDKs
find_program (CMAKE_UNAME uname /bin /usr/bin /usr/local/bin)
if (CMAKE_UNAME)
	execute_process(COMMAND uname -r
			OUTPUT_VARIABLE CMAKE_HOST_SYSTEM_VERSION)
	string (REGEX REPLACE "^([0-9]+)\\.([0-9]+).*$" "\\1" DARWIN_MAJOR_VERSION "${CMAKE_HOST_SYSTEM_VERSION}")
endif (CMAKE_UNAME)


set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_AR ar CACHE FILEPATH "" FORCE)

set (CMAKE_C_OSX_COMPATIBILITY_VERSION_FLAG "-compatibility_version ")
set (CMAKE_C_OSX_CURRENT_VERSION_FLAG "-current_version ")
set (CMAKE_CXX_OSX_COMPATIBILITY_VERSION_FLAG "${CMAKE_C_OSX_COMPATIBILITY_VERSION_FLAG}")
set (CMAKE_CXX_OSX_CURRENT_VERSION_FLAG "${CMAKE_C_OSX_CURRENT_VERSION_FLAG}")

if (CMAKE_BUILD_TYPE STREQUAL "Debug" OR ENABLE_DEBUG)
	set(VISIONOS_DEBUG_OPTIONS "-glldb -gmodules")
else()
	set(VISIONOS_DEBUG_OPTIONS "-fvisibility=hidden -fvisibility-inlines-hidden")
endif()	

set (CMAKE_C_FLAGS_INIT "${VISIONOS_DEBUG_OPTIONS} ${EMBED_OPTIONS}")
set (CMAKE_CXX_FLAGS_INIT "${VISIONOS_DEBUG_OPTIONS} ${EMBED_OPTIONS}")

set (CMAKE_C_LINK_FLAGS "-Wl,-search_paths_first ${EMBED_OPTIONS} ${CMAKE_C_LINK_FLAGS}")
set (CMAKE_CXX_LINK_FLAGS "-Wl,-search_paths_first ${EMBED_OPTIONS} ${CMAKE_CXX_LINK_FLAGS}")


set (CMAKE_PLATFORM_HAS_INSTALLNAME 1)
set (CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS "-dynamiclib")
set (CMAKE_SHARED_MODULE_CREATE_C_FLAGS "-bundle")
set (CMAKE_SHARED_MODULE_LOADER_C_FLAG "-Wl,-bundle_loader,")
set (CMAKE_SHARED_MODULE_LOADER_CXX_FLAG "-Wl,-bundle_loader,")
set (CMAKE_FIND_LIBRARY_SUFFIXES ".dylib" ".so" ".a")

# Specify install_name_tool and pkg-config since it outside of SDK path and therefore can't be found by CMake
if (NOT DEFINED CMAKE_INSTALL_NAME_TOOL)
	find_program(CMAKE_INSTALL_NAME_TOOL install_name_tool)
endif (NOT DEFINED CMAKE_INSTALL_NAME_TOOL)

if (NOT DEFINED PKG_CONFIG_EXECUTABLE)
	find_program(PKG_CONFIG_EXECUTABLE NAMES pkg-config)
	if (DEFINED PKG_CONFIG_EXECUTABLE)
		execute_process(COMMAND pkg-config --version OUTPUT_VARIABLE PKG_CONFIG_VERSION_STRING)
	endif(DEFINED PKG_CONFIG_EXECUTABLE)
endif(NOT DEFINED PKG_CONFIG_EXECUTABLE) 


# fffio Specify path to install shared library on device
set (CMAKE_INSTALL_NAME_DIR  "@executable_path/Frameworks")
set (CMAKE_BUILD_WITH_INSTALL_NAME_DIR TRUE)

# Setup visionOS platform unless specified manually with VISIONOS_PLATFORM
if (NOT DEFINED VISIONOS_PLATFORM)
	set (VISIONOS_PLATFORM "OS")
endif (NOT DEFINED VISIONOS_PLATFORM)
set (VISIONOS_PLATFORM ${VISIONOS_PLATFORM} CACHE STRING "Type of visionOS Platform")

# Check the platform selection and setup for developer root
if (${VISIONOS_PLATFORM} STREQUAL OS)
	set (VISIONOS_PLATFORM_LOCATION "XROS.platform")

	# This causes the installers to properly locate the output libraries
	set (CMAKE_XCODE_EFFECTIVE_PLATFORMS "-xros")
elseif (${VISIONOS_PLATFORM} STREQUAL SIMULATOR)
    set (SIMULATOR true)
	set (VISIONOS_PLATFORM_LOCATION "XRSimulator.platform")

	# This causes the installers to properly locate the output libraries
	set (CMAKE_XCODE_EFFECTIVE_PLATFORMS "-xrsimulator")
elseif (${VISIONOS_PLATFORM} STREQUAL SIMULATOR64)
    set (SIMULATOR true)
	set (VISIONOS_PLATFORM_LOCATION "XRSimulator.platform")

	# This causes the installers to properly locate the output libraries
	set (CMAKE_XCODE_EFFECTIVE_PLATFORMS "-xrsimulator")
else (${VISIONOS_PLATFORM} STREQUAL OS)
	message (FATAL_ERROR "Unsupported VISIONOS_PLATFORM value selected. Please choose OS or SIMULATOR")
endif (${VISIONOS_PLATFORM} STREQUAL OS)

# Setup visionOS developer location unless specified manually with CMAKE_VISIONOS_DEVELOPER_ROOT
if (NOT DEFINED CMAKE_VISIONOS_DEVELOPER_ROOT)
	execute_process(COMMAND /usr/bin/xcode-select -print-path
			OUTPUT_VARIABLE CMAKE_XCODE_DEVELOPER_DIR)
	string(STRIP "${CMAKE_XCODE_DEVELOPER_DIR}" CMAKE_XCODE_DEVELOPER_DIR) # FIXED: remove new line character, otherwise it complain no visionOS SDK's found in default search path
	set (CMAKE_VISIONOS_DEVELOPER_ROOT "${CMAKE_XCODE_DEVELOPER_DIR}/Platforms/${VISIONOS_PLATFORM_LOCATION}/Developer")
endif (NOT DEFINED CMAKE_VISIONOS_DEVELOPER_ROOT)
set (CMAKE_VISIONOS_DEVELOPER_ROOT ${CMAKE_VISIONOS_DEVELOPER_ROOT} CACHE PATH "Location of visionOS Platform")

# Find and use the most recent visionOS sdk unless specified manually with CMAKE_VISIONOS_SDK_ROOT
if (NOT DEFINED CMAKE_VISIONOS_SDK_ROOT)
	file (GLOB _CMAKE_VISIONOS_SDKS "${CMAKE_VISIONOS_DEVELOPER_ROOT}/SDKs/*")
	if (_CMAKE_VISIONOS_SDKS) 
		list (SORT _CMAKE_VISIONOS_SDKS)
		list (REVERSE _CMAKE_VISIONOS_SDKS)
		list (GET _CMAKE_VISIONOS_SDKS 0 CMAKE_VISIONOS_SDK_ROOT)
	else (_CMAKE_VISIONOS_SDKS)
		message (FATAL_ERROR "No visionOS SDK's found in default search path ${CMAKE_VISIONOS_DEVELOPER_ROOT}. Manually set CMAKE_VISIONOS_SDK_ROOT or install the visionOS SDK.")
	endif (_CMAKE_VISIONOS_SDKS)
	message (STATUS "Toolchain using default visionOS SDK: ${CMAKE_VISIONOS_SDK_ROOT}")
endif (NOT DEFINED CMAKE_VISIONOS_SDK_ROOT)
set (CMAKE_VISIONOS_SDK_ROOT ${CMAKE_VISIONOS_SDK_ROOT} CACHE PATH "Location of the selected visionOS SDK")

# Set the sysroot default to the most recent SDK
set (CMAKE_OSX_SYSROOT ${CMAKE_VISIONOS_SDK_ROOT} CACHE PATH "Sysroot used for visionOS support")

# set the architecture for visionOS 
if (NOT DEFINED VISIONOS_ARCH)
	if (${VISIONOS_PLATFORM} STREQUAL OS)
		set (VISIONOS_ARCH arm64)
	elseif (${VISIONOS_PLATFORM} STREQUAL SIMULATOR)
		set (VISIONOS_ARCH arm64)
	elseif (${VISIONOS_PLATFORM} STREQUAL SIMULATOR64)
		set (VISIONOS_ARCH x86_64)
	endif (${VISIONOS_PLATFORM} STREQUAL OS)
endif(NOT DEFINED VISIONOS_ARCH)
set (CMAKE_OSX_ARCHITECTURES ${VISIONOS_ARCH} CACHE STRING "Build architecture for visionOS")

# Set the find root to the visionOS developer roots and to user defined paths
set (CMAKE_FIND_ROOT_PATH ${CMAKE_VISIONOS_DEVELOPER_ROOT} ${CMAKE_VISIONOS_SDK_ROOT} ${CMAKE_PREFIX_PATH} CACHE STRING "visionOS find search path root")

# default to searching for frameworks first
set (CMAKE_FIND_FRAMEWORK FIRST)

# set up the default search directories for frameworks
set (CMAKE_SYSTEM_FRAMEWORK_PATH
	${CMAKE_VISIONOS_SDK_ROOT}/System/Library/Frameworks
	${CMAKE_VISIONOS_SDK_ROOT}/System/Library/PrivateFrameworks
	${CMAKE_VISIONOS_SDK_ROOT}/Developer/Library/Frameworks
)

# only search the visionOS sdks, not the remainder of the host filesystem (except for programs, so that we can still find Python if needed)
set (CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BOTH)
set (CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set (CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

