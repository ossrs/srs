/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/

#ifndef INC__THREADNAME_H
#define INC__THREADNAME_H

#ifdef __linux__

#include <sys/prctl.h>

class ThreadName
{
    char old_name[128];
    char new_name[128];
    bool good;

public:
    static const size_t BUFSIZE = 128;

    static bool get(char* namebuf)
    {
        return prctl(PR_GET_NAME, (unsigned long)namebuf, 0, 0) != -1;
    }

    static bool set(const char* name)
    {
        return prctl(PR_SET_NAME, (unsigned long)name, 0, 0) != -1;
    }


    ThreadName(const char* name)
    {
        if ( (good = get(old_name)) )
        {
            snprintf(new_name, 127, "%s", name);
            new_name[127] = 0;
            prctl(PR_SET_NAME, (unsigned long)new_name, 0, 0);
        }
    }

    ~ThreadName()
    {
        if ( good )
            prctl(PR_SET_NAME, (unsigned long)old_name, 0, 0);
    }
};

#else

// Fake class, which does nothing. You can also take a look how
// this works in other systems that are not supported here and add
// the support. This is a fallback for systems that do not support
// thread names.

class ThreadName
{
public:

    static bool get(char*) { return false; }
    static bool set(const char*) { return false; }

    ThreadName(const char*)
    {
    }

    ~ThreadName() // just to make it "non-trivially-destructible" for compatibility with normal version
    {
    }

};



#endif
#endif
