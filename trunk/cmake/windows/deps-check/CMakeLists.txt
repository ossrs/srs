# One small program per third-party library, to show it builds, links and runs with MSVC.
# srs-windows-runner.sh builds the srs-deps-check target and runs each deps-check-<name>.exe.
add_custom_target(srs-deps-check)

function(srs_deps_check name)
  add_executable(deps-check-${name} ${name}.cpp)
  target_link_libraries(deps-check-${name} PRIVATE ${ARGN})
  set_target_properties(deps-check-${name} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/deps-check"
    EXCLUDE_FROM_ALL ON)
  add_dependencies(srs-deps-check deps-check-${name})
endfunction()

srs_deps_check(openssl OpenSSL::SSL OpenSSL::Crypto)
srs_deps_check(srtp libSRTP::srtp2)
srs_deps_check(srt srt_lib)
srs_deps_check(ffmpeg ffmpeg)
srs_deps_check(st st)
srs_deps_check(gtest GTest::gtest_main)
