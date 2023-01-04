#!/usr/bin/tclsh
#*
#* SRT - Secure, Reliable, Transport
#* Copyright (c) 2020 Haivision Systems Inc.
#* 
#* This Source Code Form is subject to the terms of the Mozilla Public
#* License, v. 2.0. If a copy of the MPL was not distributed with this
#* file, You can obtain one at http://mozilla.org/MPL/2.0/.
#* 
#*/
#
#*****************************************************************************
#written by
#  Haivision Systems Inc.
#*****************************************************************************

set code_major {
    UNKNOWN    -1
    SUCCESS     0
    SETUP       1
    CONNECTION  2
    SYSTEMRES   3
    FILESYSTEM  4
    NOTSUP      5
    AGAIN       6
    PEERERROR   7
}

set code_minor {
    NONE             0
    TIMEOUT          1
    REJECTED         2
    NORES            3
    SECURITY         4
    CLOSED           5

    
    CONNLOST         1
    NOCONN           2
    
    THREAD           1
    MEMORY           2
    OBJECT           3
    
    SEEKGFAIL        1
    READFAIL         2
    SEEKPFAIL        3
    WRITEFAIL        4
    
    ISBOUND          1
    ISCONNECTED      2
    INVAL            3
    SIDINVAL         4
    ISUNBOUND        5
    NOLISTEN         6
    ISRENDEZVOUS     7
    ISRENDUNBOUND    8
    INVALMSGAPI      9
    INVALBUFFERAPI  10
    BUSY            11
    XSIZE           12
    EIDINVAL        13
    EEMPTY          14
    BUSYPORT        15
    
    WRAVAIL          1
    RDAVAIL          2
    XMTIMEOUT        3
    CONGESTION       4
}


set errortypes {

    SUCCESS "Success" {
        NONE      ""
    }

    SETUP "Connection setup failure" {
        NONE      ""
        TIMEOUT   "connection timed out"
        REJECTED  "connection rejected"
        NORES     "unable to create/configure SRT socket"
        SECURITY  "aborted for security reasons"
        CLOSED    "socket closed during operation"
    }

    CONNECTION "" {
        NONE     ""
        CONNLOST "Connection was broken"
        NOCONN   "Connection does not exist"
    }

    SYSTEMRES "System resource failure" {
        NONE   ""
        THREAD "unable to create new threads"
        MEMORY "unable to allocate buffers"
        OBJECT "unable to allocate a system object"
    }

    FILESYSTEM "File system failure" {
        NONE      ""
        SEEKGFAIL "cannot seek read position"
        READFAIL  "failure in read"
        SEEKPFAIL "cannot seek write position"
        WRITEFAIL "failure in write"
    }

    NOTSUP "Operation not supported" {
        NONE           ""
        ISBOUND        "Cannot do this operation on a BOUND socket"
        ISCONNECTED    "Cannot do this operation on a CONNECTED socket"
        INVAL          "Bad parameters"
        SIDINVAL       "Invalid socket ID"
        ISUNBOUND      "Cannot do this operation on an UNBOUND socket"
        NOLISTEN       "Socket is not in listening state"
        ISRENDEZVOUS   "Listen/accept is not supported in rendezous connection setup"
        ISRENDUNBOUND  "Cannot call connect on UNBOUND socket in rendezvous connection setup"
        INVALMSGAPI    "Incorrect use of Message API (sendmsg/recvmsg)."
        INVALBUFFERAPI "Incorrect use of Buffer API (send/recv) or File API (sendfile/recvfile)."
        BUSY           "Another socket is already listening on the same port"
        XSIZE          "Message is too large to send (it must be less than the SRT send buffer size)"
        EIDINVAL       "Invalid epoll ID"
        EEMPTY         "All sockets removed from epoll, waiting would deadlock"
        BUSYPORT       "Another socket is bound to that port and is not reusable for requested settings"
    }

    AGAIN "Non-blocking call failure" {
        NONE       ""
        WRAVAIL    "no buffer available for sending"
        RDAVAIL    "no data available for reading"
        XMTIMEOUT  "transmission timed out"
        CONGESTION "early congestion notification"        
    }

    PEERERROR "The peer side has signaled an error" {
        NONE ""
    }

}

set main_array_item {
const char** strerror_array_major [] = {
$minor_array_list
};
}

set major_size_item {
const size_t strerror_array_sizes [] = {
$minor_array_sizes
};
}

set minor_array_item {
const char* strerror_msgs_$majorlc [] = {
$minor_message_items
};
}

set strerror_function {
const char* strerror_get_message(size_t major, size_t minor)
{
    static const char* const undefined = "UNDEFINED ERROR";

    // Extract the major array
    if (major >= sizeof(strerror_array_major)/sizeof(const char**))
    {
        return undefined;
    }

    const char** array = strerror_array_major[major];
    size_t size = strerror_array_sizes[major];

    if (minor >= size)
    {
        return undefined;
    }

    return array[minor];
}

}

set globalheader {
 /*
  WARNING: Generated from ../scripts/generate-error-types.tcl

  DO NOT MODIFY.

  Copyright applies as per the generator script.
 */

#include <cstddef>

}

proc Generate:imp {} {

    puts $::globalheader

    puts "namespace srt\n\{"

    # Generate major array
    set majitem 0
    set minor_array_sizes ""
    foreach {mt mm cont} $::errortypes {

        puts "// MJ_$mt '$mm'"

        # Generate minor array
        set majorlc [string tolower $mt]
        set minor_message_items ""
        set minitem 0
        foreach {mnt mnm} $cont {
            if {$mm == ""} {
                set msg $mnm
            } elseif {$mnm == ""} {
                set msg $mm
            } else {
                set msg "$mm: $mnm"
            }
            append minor_message_items "    \"$msg\", // MN_$mnt = $minitem\n"
            incr minitem
        }
        append minor_message_items "    \"\""
        puts [subst -nobackslashes -nocommands $::minor_array_item]

        append minor_array_list "    strerror_msgs_$majorlc, // MJ_$mt = $majitem\n"
        #append minor_array_sizes "    [expr {$minitem}],\n"
        append minor_array_sizes "    SRT_ARRAY_SIZE(strerror_msgs_$majorlc) - 1,\n"
        incr majitem
    }
    append minor_array_list "    NULL"
    append minor_array_sizes "    0"

    puts [subst -nobackslashes -nocommands $::main_array_item]
    puts {#define SRT_ARRAY_SIZE(ARR) sizeof(ARR) / sizeof(ARR[0])}
    puts [subst -nobackslashes -nocommands $::major_size_item]

    puts $::strerror_function

    puts "\} // namespace srt"
}


set defmode imp
if {[lindex $argv 0] != ""} {
    set defmode [lindex $argv 0]
}

Generate:$defmode
