//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_KERNEL_STREAM_HPP
#define SRS_KERNEL_STREAM_HPP

#include <srs_core.hpp>

#include <vector>

/**
 * the simple buffer use vector to append bytes,
 * it's for hls and http, and need to be refined in future.
 */
class SrsSimpleStream
{
private:
    std::vector<char> data;
public:
    SrsSimpleStream();
    virtual ~SrsSimpleStream();
public:
    /**
     * get the length of buffer. empty if zero.
     * @remark assert length() is not negative.
     */
    virtual int length();
    /**
     * get the buffer bytes.
     * @return the bytes, NULL if empty.
     */
    virtual char* bytes();
    /**
     * erase size of bytes from begin.
     * @param size to erase size of bytes.
     *       clear if size greater than or equals to length()
     * @remark ignore size is not positive.
     */
    virtual void erase(int size);
    /**
     * append specified bytes to buffer.
     * @param size the size of bytes
     * @remark assert size is positive.
     */
    virtual void append(const char* bytes, int size);
    virtual void append(SrsSimpleStream* src);
};

#endif
