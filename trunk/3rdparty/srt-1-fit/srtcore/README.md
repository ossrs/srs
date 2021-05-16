SRT Core
========

These files are contents of the SRT library. Beside files that are used exclusively
and internally by the library, this directory also contains:

 - common files: usually header files, which can be used also by other projects,
even if they don't link against SRT

 - public and protected header files - header files for the library, which will
be picked up from here

Which header files are public, protected and private, it's defined in the manifest
file together with all source files that the SRT library comprises of: `filelist.maf`.


Common files
============

This directory holds the files that may be used separately by both SRT library
itself and the internal applications.

Source files are added to SRT library, so apps don't have to use them. However
these source files might be used by some internal applications that do not
link against SRT library.

Header files contained here might be required by internal applications no
matter if they link against SRT or not. They are here because simultaneously
they are used also by the SRT library.


Utilities
=========

1. threadname.h

This is a utility that is useful for debugging and it allows a thread to be given
a name. This name is used in the logging messages, as well as you can see it also
inside the debugger.

This is currently supported only on Linux; some more portable and more reliable
way is needed.

2. utilities.h

A set of various reusable components, all defined as C++ classes or C++ inline
functions. 

3. `netinet_any.h`

This defines a `sockaddr_any` type, which simplifies dealing with the BSD socket API
using `sockaddr`, `sockaddr_in` and `sockaddr_in6` structures.


Compat and portability
======================

1. `srt_compat.h`

This part contains some portability problem resolutions, including:
 - `strerror` in a version that is both portable and thread safe
 - `localtime` in a version that is both portable and thread safe

2. win directory

This contains various header files that are used on Windows platform only.
They provide various facilities available OOTB on POSIX systems.

3. `platform_sys.h`

This is a file that is responsible to include whatever system include
files must be included for whatever system API must be provided for
the needs of SRT library. This is a part of public headers.


