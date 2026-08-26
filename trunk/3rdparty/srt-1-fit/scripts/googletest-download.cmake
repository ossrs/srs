# code copied from https://crascit.com/2015/07/25/cmake-gtest/
cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(GOOGLETEST_DOWNLOAD)

include(ExternalProject)

ExternalProject_Add(
	googletest
	SOURCE_DIR "@GOOGLETEST_DOWNLOAD_ROOT@/googletest-src"
	BINARY_DIR "@GOOGLETEST_DOWNLOAD_ROOT@/googletest-build"
	GIT_REPOSITORY
	https://github.com/google/googletest.git
	GIT_TAG release-1.12.1
	CONFIGURE_COMMAND ""
	BUILD_COMMAND ""
	INSTALL_COMMAND ""
	TEST_COMMAND ""
)
