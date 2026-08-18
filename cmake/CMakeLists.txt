cmake_minimum_required(VERSION 3.10)
project(srs-all)

add_subdirectory(../trunk/cmake srs-build)
add_subdirectory(../trunk/3rdparty/st-srs/cmake st-build)
