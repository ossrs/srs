
 /*
  WARNING: Generated from ../scripts/generate-error-types.tcl

  DO NOT MODIFY.

  Copyright applies as per the generator script.
 */

#include <cstddef>


namespace srt
{
// MJ_SUCCESS 'Success'

const char* strerror_msgs_success [] = {
    "Success", // MN_NONE = 0
    ""
};

// MJ_SETUP 'Connection setup failure'

const char* strerror_msgs_setup [] = {
    "Connection setup failure", // MN_NONE = 0
    "Connection setup failure: connection timed out", // MN_TIMEOUT = 1
    "Connection setup failure: connection rejected", // MN_REJECTED = 2
    "Connection setup failure: unable to create/configure SRT socket", // MN_NORES = 3
    "Connection setup failure: aborted for security reasons", // MN_SECURITY = 4
    "Connection setup failure: socket closed during operation", // MN_CLOSED = 5
    ""
};

// MJ_CONNECTION ''

const char* strerror_msgs_connection [] = {
    "", // MN_NONE = 0
    "Connection was broken", // MN_CONNLOST = 1
    "Connection does not exist", // MN_NOCONN = 2
    ""
};

// MJ_SYSTEMRES 'System resource failure'

const char* strerror_msgs_systemres [] = {
    "System resource failure", // MN_NONE = 0
    "System resource failure: unable to create new threads", // MN_THREAD = 1
    "System resource failure: unable to allocate buffers", // MN_MEMORY = 2
    "System resource failure: unable to allocate a system object", // MN_OBJECT = 3
    ""
};

// MJ_FILESYSTEM 'File system failure'

const char* strerror_msgs_filesystem [] = {
    "File system failure", // MN_NONE = 0
    "File system failure: cannot seek read position", // MN_SEEKGFAIL = 1
    "File system failure: failure in read", // MN_READFAIL = 2
    "File system failure: cannot seek write position", // MN_SEEKPFAIL = 3
    "File system failure: failure in write", // MN_WRITEFAIL = 4
    ""
};

// MJ_NOTSUP 'Operation not supported'

const char* strerror_msgs_notsup [] = {
    "Operation not supported", // MN_NONE = 0
    "Operation not supported: Cannot do this operation on a BOUND socket", // MN_ISBOUND = 1
    "Operation not supported: Cannot do this operation on a CONNECTED socket", // MN_ISCONNECTED = 2
    "Operation not supported: Bad parameters", // MN_INVAL = 3
    "Operation not supported: Invalid socket ID", // MN_SIDINVAL = 4
    "Operation not supported: Cannot do this operation on an UNBOUND socket", // MN_ISUNBOUND = 5
    "Operation not supported: Socket is not in listening state", // MN_NOLISTEN = 6
    "Operation not supported: Listen/accept is not supported in rendezous connection setup", // MN_ISRENDEZVOUS = 7
    "Operation not supported: Cannot call connect on UNBOUND socket in rendezvous connection setup", // MN_ISRENDUNBOUND = 8
    "Operation not supported: Incorrect use of Message API (sendmsg/recvmsg).", // MN_INVALMSGAPI = 9
    "Operation not supported: Incorrect use of Buffer API (send/recv) or File API (sendfile/recvfile).", // MN_INVALBUFFERAPI = 10
    "Operation not supported: Another socket is already listening on the same port", // MN_BUSY = 11
    "Operation not supported: Message is too large to send (it must be less than the SRT send buffer size)", // MN_XSIZE = 12
    "Operation not supported: Invalid epoll ID", // MN_EIDINVAL = 13
    "Operation not supported: All sockets removed from epoll, waiting would deadlock", // MN_EEMPTY = 14
    "Operation not supported: Another socket is bound to that port and is not reusable for requested settings", // MN_BUSYPORT = 15
    ""
};

// MJ_AGAIN 'Non-blocking call failure'

const char* strerror_msgs_again [] = {
    "Non-blocking call failure", // MN_NONE = 0
    "Non-blocking call failure: no buffer available for sending", // MN_WRAVAIL = 1
    "Non-blocking call failure: no data available for reading", // MN_RDAVAIL = 2
    "Non-blocking call failure: transmission timed out", // MN_XMTIMEOUT = 3
    "Non-blocking call failure: early congestion notification", // MN_CONGESTION = 4
    ""
};

// MJ_PEERERROR 'The peer side has signaled an error'

const char* strerror_msgs_peererror [] = {
    "The peer side has signaled an error", // MN_NONE = 0
    ""
};


const char** strerror_array_major [] = {
    strerror_msgs_success, // MJ_SUCCESS = 0
    strerror_msgs_setup, // MJ_SETUP = 1
    strerror_msgs_connection, // MJ_CONNECTION = 2
    strerror_msgs_systemres, // MJ_SYSTEMRES = 3
    strerror_msgs_filesystem, // MJ_FILESYSTEM = 4
    strerror_msgs_notsup, // MJ_NOTSUP = 5
    strerror_msgs_again, // MJ_AGAIN = 6
    strerror_msgs_peererror, // MJ_PEERERROR = 7
    NULL
};

#define SRT_ARRAY_SIZE(ARR) sizeof(ARR) / sizeof(ARR[0])

const size_t strerror_array_sizes [] = {
    SRT_ARRAY_SIZE(strerror_msgs_success) - 1,
    SRT_ARRAY_SIZE(strerror_msgs_setup) - 1,
    SRT_ARRAY_SIZE(strerror_msgs_connection) - 1,
    SRT_ARRAY_SIZE(strerror_msgs_systemres) - 1,
    SRT_ARRAY_SIZE(strerror_msgs_filesystem) - 1,
    SRT_ARRAY_SIZE(strerror_msgs_notsup) - 1,
    SRT_ARRAY_SIZE(strerror_msgs_again) - 1,
    SRT_ARRAY_SIZE(strerror_msgs_peererror) - 1,
    0
};


const char* strerror_get_message(size_t major, size_t minor)
{
    static const char* const undefined = "UNDEFINED ERROR";

    // Extract the major array
    if (major >= sizeof(strerror_array_major)/sizeof(const char**))
    {
        return undefined;
    }

    const char** array = strerror_array_major[major];
    const size_t size = strerror_array_sizes[major];

    if (minor >= size)
    {
        return undefined;
    }

    return array[minor];
}


} // namespace srt
