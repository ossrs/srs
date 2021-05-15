// DO NOT DELETE
// This file is needed for Xcode to properly handle CMake OBJECT Libraries
// From docs (https://cmake.org/cmake/help/latest/command/add_library.html#object-libraries):
//
// ... Some native build systems (such as Xcode) may not like targets that have only object files,
// so consider adding at least one real source file to any target that references $<TARGET_OBJECTS:objlib>.

// Just a dummy symbol to avoid compiler warnings
int srt_object_lib_dummy = 0;
