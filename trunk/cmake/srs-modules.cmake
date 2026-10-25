# The source files of the SRS modules, from auto/module_files.sh, which ./configure sources too, so
# all builds compile the same files. The files of SRS_<MODULE>_<FEATURE>_FILES are listed only when
# the CMake option SRS_<FEATURE> is on, as configure lists them only for SRS_<FEATURE>=YES.
if(NOT DEFINED SRS_TRUNK_DIR)
  get_filename_component(SRS_TRUNK_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

# Sets SRS_<FEATURE> for each feature of auto/module_files.sh, on when the srs_auto_headers.hpp that
# ./configure wrote defines it, so the files are the ones configure chose.
function(srs_module_options_from_header header)
  if(NOT EXISTS "${header}")
    message(FATAL_ERROR "No ${header}, run ./configure first")
  endif()
  file(STRINGS "${header}" defines REGEX "^#define SRS_[A-Z0-9_]+$")
  file(STRINGS "${SRS_TRUNK_DIR}/auto/module_files.sh" names REGEX "^SRS_[A-Z]+_[A-Z0-9_]+_FILES=")
  foreach(name IN LISTS names)
    string(REGEX REPLACE "^SRS_[A-Z]+_([A-Z0-9_]+)_FILES=.*$" "\\1" feature "${name}")
    list(FIND defines "#define SRS_${feature}" i)
    if(i LESS 0)
      set(SRS_${feature} OFF PARENT_SCOPE)
    else()
      set(SRS_${feature} ON PARENT_SCOPE)
    endif()
  endforeach()
endfunction()

# Sets out to the files of module, as paths relative to the trunk, such as src/core/srs_core.cpp:
# SRS_<MODULE>_FILES, then each SRS_<MODULE>_<FEATURE>_FILES whose feature is on, in the file's order.
function(srs_module_files module out)
  set(dirs CORE src/core KERNEL src/kernel PROTOCOL src/protocol APP src/app SERVER src/main UTEST src/utest)
  list(FIND dirs ${module} i)
  if(i LESS 0)
    message(FATAL_ERROR "No SRS module ${module}")
  endif()
  math(EXPR i "${i} + 1")
  list(GET dirs ${i} dir)

  # Each list is NAME=(file file ...) at the start of a line, and may go on over more lines.
  file(READ "${SRS_TRUNK_DIR}/auto/module_files.sh" text)
  string(REGEX MATCHALL "(^|\n)SRS_${module}_[A-Z0-9_]*FILES=\\([^)]*\\)" lists "${text}")
  set(files)
  foreach(list IN LISTS lists)
    string(REGEX MATCH "SRS_${module}_(([A-Z0-9_]+)_)?FILES=\\(([^)]*)\\)" _ "${list}")
    set(feature "${CMAKE_MATCH_2}")
    string(STRIP "${CMAKE_MATCH_3}" names)
    string(REGEX REPLACE "[ \t\r\n]+" ";" names "${names}")
    if(feature)
      if(NOT DEFINED SRS_${feature})
        message(FATAL_ERROR "No option SRS_${feature} for SRS_${module}_${feature}_FILES in auto/module_files.sh")
      endif()
      if(NOT SRS_${feature})
        continue()
      endif()
    endif()
    foreach(name IN LISTS names)
      if(NOT name MATCHES "^[A-Za-z0-9_]+$")
        message(FATAL_ERROR "Bad file ${name} in auto/module_files.sh")
      endif()
      list(APPEND files "${dir}/${name}.cpp")
    endforeach()
  endforeach()
  set(${out} "${files}" PARENT_SCOPE)
endfunction()

# Sets out to the files of srs.exe, in the order ./configure links them.
function(srs_server_sources out)
  set(all)
  foreach(module CORE KERNEL PROTOCOL APP SERVER)
    srs_module_files(${module} files)
    list(APPEND all ${files})
  endforeach()
  set(${out} "${all}" PARENT_SCOPE)
endfunction()

# Sets out to the files of srs_utest.exe: the srs modules but main, which the utest replaces with its
# own, then the utest files, as ./configure links srs_utest.
function(srs_utest_sources out)
  set(all)
  foreach(module CORE KERNEL PROTOCOL APP UTEST)
    srs_module_files(${module} files)
    list(APPEND all ${files})
  endforeach()
  set(${out} "${all}" PARENT_SCOPE)
endfunction()
