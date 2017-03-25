/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 SRS(ossrs)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_KERNEL_BUFFER_HPP
#define SRS_KERNEL_BUFFER_HPP

#include <srs_core.hpp>

#include <sys/types.h>
#include <string>

class SrsBuffer;

/**
 * the srs codec, to code and decode object with bytes:
 *      code: to encode/serialize object to bytes in buffer,
 *      decode: to decode/deserialize object from bytes in buffer.
 * we use SrsBuffer as bytes helper utility,
 * for example, to code:
 *      ISrsCodec* obj = ...
 *      char* bytes = new char[obj->size()];
 *
 *      SrsBuffer* buf = new SrsBuffer();
 *      buf->initialize(bytes, obj->size())
 *
 *      obj->encode(buf);
 * for example, to decode:
 *      int nb_bytes = ...
 *      char* bytes = ...
 *
 *      SrsBuffer* buf = new Srsbuffer();
 *      buf->initialize(bytes, nb_bytes);
 *
 *      ISrsCodec* obj = ...
 *      obj->decode(buf);
 * @remark protocol or amf0 or json should implements this interface.
 */
// TODO: FIXME: protocol, amf0, json should implements it.
class ISrsCodec
{
public:
    ISrsCodec();
    virtual ~ISrsCodec();
public:
    /**
     * get the number of bytes to code to.
     */
    // TODO: FIXME: change to uint64_t.
    virtual int nb_bytes() = 0;
    /**
     * encode object to bytes in SrsBuffer.
     */
    virtual int encode(SrsBuffer* buf) = 0;
public:
    /**
     * decode object from bytes in SrsBuffer.
     */
    virtual int decode(SrsBuffer* buf) = 0;
};

/**
 * bytes utility, used to:
 * convert basic types to bytes,
 * build basic types from bytes.
 * @remark the buffer never mange the bytes, user must manage it.
 */
class SrsBuffer
{
private:
    // current position at bytes.
    char* p;
    // the bytes data for stream to read or write.
    char* bytes;
    // the total number of bytes.
    int nb_bytes;
public:
    SrsBuffer();
    SrsBuffer(char* b, int nb_b);
    virtual ~SrsBuffer();
private:
    virtual void set_value(char* b, int nb_b);
public:
    /**
     * initialize the stream from bytes.
     * @b, the bytes to convert from/to basic types.
     * @nb, the size of bytes, total number of bytes for stream.
     * @remark, stream never free the bytes, user must free it.
     * @remark, return error when bytes NULL.
     * @remark, return error when size is not positive.
     */
    virtual int initialize(char* b, int nb);
    // get the status of stream
public:
    /**
     * get data of stream, set by initialize.
     * current bytes = data() + pos()
     */
    virtual char* data();
    /**
     * the total stream size, set by initialize.
     * left bytes = size() - pos().
     */
    virtual int size();
    /**
     * tell the current pos.
     */
    virtual int pos();
    /**
     * whether stream is empty.
     * if empty, user should never read or write.
     */
    virtual bool empty();
    /**
     * whether required size is ok.
     * @return true if stream can read/write specified required_size bytes.
     * @remark assert required_size positive.
     */
    virtual bool require(int required_size);
    // to change stream.
public:
    /**
     * to skip some size.
     * @param size can be any value. positive to forward; nagetive to backward.
     * @remark to skip(pos()) to reset stream.
     * @remark assert initialized, the data() not NULL.
     */
    virtual void skip(int size);
public:
    /**
     * get 1bytes char from stream.
     */
    virtual int8_t read_1bytes();
    /**
     * get 2bytes int from stream.
     */
    virtual int16_t read_2bytes();
    /**
     * get 3bytes int from stream.
     */
    virtual int32_t read_3bytes();
    /**
     * get 4bytes int from stream.
     */
    virtual int32_t read_4bytes();
    /**
     * get 8bytes int from stream.
     */
    virtual int64_t read_8bytes();
    /**
     * get string from stream, length specifies by param len.
     */
    virtual std::string read_string(int len);
    /**
     * get bytes from stream, length specifies by param len.
     */
    virtual void read_bytes(char* data, int size);
public:
    /**
     * write 1bytes char to stream.
     */
    virtual void write_1bytes(int8_t value);
    /**
     * write 2bytes int to stream.
     */
    virtual void write_2bytes(int16_t value);
    /**
     * write 4bytes int to stream.
     */
    virtual void write_4bytes(int32_t value);
    /**
     * write 3bytes int to stream.
     */
    virtual void write_3bytes(int32_t value);
    /**
     * write 8bytes int to stream.
     */
    virtual void write_8bytes(int64_t value);
    /**
     * write string to stream
     */
    virtual void write_string(std::string value);
    /**
     * write bytes to stream
     */
    virtual void write_bytes(char* data, int size);
};

/**
 * the bit stream, base on SrsBuffer,
 * for exmaple, the h.264 avc stream is bit stream.
 */
class SrsBitBuffer
{
private:
    int8_t cb;
    uint8_t cb_left;
    SrsBuffer* stream;
public:
    SrsBitBuffer();
    virtual ~SrsBitBuffer();
public:
    virtual int initialize(SrsBuffer* s);
    virtual bool empty();
    virtual int8_t read_bit();
};

#endif
