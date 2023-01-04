/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

// Implementation file for srt_compat.h
/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/

// Prevents from misconfiguration through preprocessor.

#include "platform_sys.h"

#include <srt_compat.h>

#include <string.h>
#include <stdio.h>
#include <errno.h>
#if defined(__unix__) && !defined(BSD) && !defined(SUNOS)
#include <features.h>
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif


static const char* SysStrError_Fallback(int errnum, char* buf, size_t buflen)
{
#if defined(_MSC_VER) && _MSC_VER < 1900
    _snprintf(buf, buflen - 1, "ERROR CODE %d", errnum);
    buf[buflen - 1] = '\0';
#else
    snprintf(buf, buflen, "ERROR CODE %d", errnum);
#endif
    return buf;
}

// This function is a portable and thread-safe version of `strerror`.
// It requires a user-supplied buffer to store the message. The returned
// value is always equal to the given buffer pointer. If the system
// error message is longer than the given buflen, it will be trimmed.
// When the error code is incorrect for the given error message function,
// a fallback message will be returned, either as returned by the underlying
// function, or crafted by this function as a response to error in an
// underlying function. 
extern const char * SysStrError(int errnum, char * buf, size_t buflen)
{
    if (buf == NULL || buflen < 4) // Required to put ??? into it as a fallback
    {
        errno = EFAULT;
        return buf;
    }

    buf[0] = '\0';

#if defined(_WIN32)
    const char* lpMsgBuf;

    // Note: Intentionally the "fixed char size" types are used despite using
    // character size dependent FormatMessage (instead of FormatMessageA) so that
    // your compilation fails when you use wide characters.
    // The problem is that when TCHAR != char, then the buffer written this way
    // would have to be converted to ASCII, not just copied by strncpy.
    FormatMessage(0
            | FORMAT_MESSAGE_ALLOCATE_BUFFER
            | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, // no lpSource
            errnum, // dwMessageId (as controlled by FORMAT_MESSAGE_FROM_SYSTEM)
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            // This below parameter normally should contain a pointer to an allocated buffer,
            // and this way it's LPTSTR. But when FORMAT_MESSAGE_ALLOCATE_BUFFER, then it is
            // expected to be a the value of LPTSTR* type, converted to LPTSTR, that designates
            // a pointer to a variable of type LPTSTR, to which the newly allocated buffer is
            // assigned. This buffer should be freed afterwards using LocalFree().
            (LPSTR)&lpMsgBuf,
            0, NULL);

    if (lpMsgBuf)
    {
        strncpy(buf, lpMsgBuf, buflen-1);
        buf[buflen-1] = 0;
        LocalFree((HLOCAL)lpMsgBuf);
    }
    else
    {
        SysStrError_Fallback(errnum, buf, buflen);
    }

    return buf;

#elif (!defined(__GNU_LIBRARY__) && !defined(__GLIBC__) )  \
    || (( (_POSIX_C_SOURCE >= 200112L) || (_XOPEN_SOURCE >= 600)) && ! _GNU_SOURCE )
    // POSIX/XSI-compliant version.
    // Overall general POSIX version: returns status.
    // 0 for success, otherwise it's:
    // - possibly -1 and the error code is in ::errno
    // - possibly the error code itself
    // The details of the errror are not interesting; simply
    // craft a fallback message in this case.
    if (strerror_r(errnum, buf, buflen) != 0)
    {
        return SysStrError_Fallback(errnum, buf, buflen);
    }
    return buf;
#else
    // GLIBC is non-standard under these conditions.
    // GNU version: returns the pointer to the message.
    // This is either equal to the local buffer (buf)
    // or some system-wide (constant) storage. To maintain
    // stability of the API, this overall function shall
    // always return the local buffer and the message in
    // this buffer - so these cases should be distinguished
    // and the internal storage copied to the buffer.

    char * gnu_buffer = strerror_r(errnum, buf, buflen);
    if (!gnu_buffer)
    {
        // This should never happen, so just a paranoid check
        return SysStrError_Fallback(errnum, buf, buflen);
    }

    // If they are the same, the message is already copied
    // (and it's usually a "fallback message" for an error case).
    if (gnu_buffer != buf)
    {
        strncpy(buf, gnu_buffer, buflen-1);
        buf[buflen-1] = 0; // guarantee what strncpy doesn't
    }

    return buf;
#endif
}
