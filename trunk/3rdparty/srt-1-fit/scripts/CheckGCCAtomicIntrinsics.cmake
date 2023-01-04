#
# SRT - Secure, Reliable, Transport Copyright (c) 2021 Haivision Systems Inc.
#
# This Source Code Form is subject to the terms of the Mozilla Public License,
# v. 2.0. If a copy of the MPL was not distributed with this file, You can
# obtain one at http://mozilla.org/MPL/2.0/.
#

# Check for GCC Atomic Intrinsics and whether libatomic is required.
#
# Sets:
#	HAVE_LIBATOMIC
#	HAVE_LIBATOMIC_COMPILES
#	HAVE_LIBATOMIC_COMPILES_STATIC
#	HAVE_GCCATOMIC_INTRINSICS
#	HAVE_GCCATOMIC_INTRINSICS_REQUIRES_LIBATOMIC
#
# See
#	https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html
#	https://gcc.gnu.org/wiki/Atomic/GCCMM/AtomicSync

include(CheckCSourceCompiles)
include(CheckLibraryExists)

function(CheckGCCAtomicIntrinsics)

	unset(HAVE_LIBATOMIC CACHE)
	unset(HAVE_LIBATOMIC_COMPILES CACHE)
	unset(HAVE_LIBATOMIC_COMPILES_STATIC CACHE)
	unset(HAVE_GCCATOMIC_INTRINSICS CACHE)
	unset(HAVE_GCCATOMIC_INTRINSICS_REQUIRES_LIBATOMIC CACHE)

	set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE) # CMake 3.6

	unset(CMAKE_REQUIRED_FLAGS)
	unset(CMAKE_REQUIRED_LIBRARIES)
	unset(CMAKE_REQUIRED_LINK_OPTIONS)

	# Check for existance of libatomic and whether this symbol is present.
	check_library_exists(atomic __atomic_fetch_add_8 "" HAVE_LIBATOMIC)

	set(CheckLibAtomicCompiles_CODE
		"
		int main(void)
		{
			const int result = 0;
			return result;
		}
	")

	set(CMAKE_REQUIRED_LIBRARIES "atomic")

	# Check that the compiler can build a simple application and link with
	# libatomic.
	check_c_source_compiles("${CheckLibAtomicCompiles_CODE}"
							HAVE_LIBATOMIC_COMPILES)
	if(NOT HAVE_LIBATOMIC_COMPILES)
		set(HAVE_LIBATOMIC
			0
			CACHE INTERNAL "" FORCE)
	endif()
	if(HAVE_LIBATOMIC AND HAVE_LIBATOMIC_COMPILES)
		# CMAKE_REQUIRED_LINK_OPTIONS was introduced in CMake 3.14.
		if(CMAKE_VERSION VERSION_LESS "3.14")
			set(CMAKE_REQUIRED_LINK_OPTIONS "-static")
		else()
			set(CMAKE_REQUIRED_FLAGS "-static")
	endif()
	# Check that the compiler can build a simple application and statically link
	# with libatomic.
	check_c_source_compiles("${CheckLibAtomicCompiles_CODE}"
							HAVE_LIBATOMIC_COMPILES_STATIC)
	else()
		set(HAVE_LIBATOMIC_COMPILES_STATIC
			0
			CACHE INTERNAL "" FORCE)
	endif()

	unset(CMAKE_REQUIRED_FLAGS)
	unset(CMAKE_REQUIRED_LIBRARIES)
	unset(CMAKE_REQUIRED_LINK_OPTIONS)

	set(CheckGCCAtomicIntrinsics_CODE
		"
		#include<stddef.h>
		#include<stdint.h>
		int main(void)
		{
			ptrdiff_t x = 0;
			intmax_t y = 0;
			__atomic_add_fetch(&x, 1, __ATOMIC_SEQ_CST);
			__atomic_add_fetch(&y, 1, __ATOMIC_SEQ_CST);
			return __atomic_sub_fetch(&x, 1, __ATOMIC_SEQ_CST)
				+ __atomic_sub_fetch(&y, 1, __ATOMIC_SEQ_CST);
		}
	")

	set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE) # CMake 3.6
	check_c_source_compiles("${CheckGCCAtomicIntrinsics_CODE}"
							HAVE_GCCATOMIC_INTRINSICS)

	if(NOT HAVE_GCCATOMIC_INTRINSICS AND HAVE_LIBATOMIC)
		set(CMAKE_REQUIRED_LIBRARIES "atomic")
		check_c_source_compiles("${CheckGCCAtomicIntrinsics_CODE}"
							HAVE_GCCATOMIC_INTRINSICS_REQUIRES_LIBATOMIC)
		if(HAVE_GCCATOMIC_INTRINSICS_REQUIRES_LIBATOMIC)
			set(HAVE_GCCATOMIC_INTRINSICS
				1
				CACHE INTERNAL "" FORCE)
		endif()
	endif()

endfunction(CheckGCCAtomicIntrinsics)
