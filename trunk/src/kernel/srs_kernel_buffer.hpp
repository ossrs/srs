//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_BUFFER_HPP
#define SRS_KERNEL_BUFFER_HPP

#include <srs_core.hpp>

#include <string>
#include <sys/types.h>

class SrsBuffer;

// Encoder.
class ISrsEncoder
{
public:
    ISrsEncoder();
    virtual ~ISrsEncoder();

public:
    /**
     * get the number of bytes to code to.
     */
    virtual uint64_t nb_bytes() = 0;
    /**
     * encode object to bytes in SrsBuffer.
     */
    virtual srs_error_t encode(SrsBuffer *buf) = 0;
};

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
class ISrsCodec : public ISrsEncoder
{
public:
    ISrsCodec();
    virtual ~ISrsCodec();

public:
    // Get the number of bytes to code to.
    virtual uint64_t nb_bytes() = 0;
    // Encode object to bytes in SrsBuffer.
    virtual srs_error_t encode(SrsBuffer *buf) = 0;

public:
    // Decode object from bytes in SrsBuffer.
    virtual srs_error_t decode(SrsBuffer *buf) = 0;
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
    char *p;
    // the bytes data for buffer to read or write.
    char *bytes;
    // the total number of bytes.
    int nb_bytes;

public:
    // Create buffer with data b and size nn.
    // @remark User must free the data b.
    SrsBuffer(char *b, int nn);
    ~SrsBuffer();

public:
    // Copy the object, keep position of buffer.
    SrsBuffer *copy();
    // Get the data and head of buffer.
    //      current-bytes = head() = data() + pos()
    char *data();
    char *head();
    // Get the total size of buffer.
    //      left-bytes = size() - pos()
    int size();
    void set_size(int v);
    // Get the current buffer position.
    int pos();
    // Left bytes in buffer, total size() minus the current pos().
    int left();
    // Whether buffer is empty.
    bool empty();
    // Whether buffer is able to supply required size of bytes.
    // @remark User should check buffer by require then do read/write.
    // @remark Assert the required_size is not negative.
    bool require(int required_size);

public:
    // Skip some size.
    // @param size can be any value. positive to forward; negative to backward.
    // @remark to skip(pos()) to reset buffer.
    // @remark assert initialized, the data() not NULL.
    void skip(int size);

public:
    // Read 1bytes char from buffer.
    int8_t read_1bytes();
    // Read 2bytes int from buffer.
    int16_t read_2bytes();
    int16_t read_le2bytes();
    // Read 3bytes int from buffer.
    int32_t read_3bytes();
    int32_t read_le3bytes();
    // Read 4bytes int from buffer.
    int32_t read_4bytes();
    int32_t read_le4bytes();
    // Read 8bytes int from buffer.
    int64_t read_8bytes();
    int64_t read_le8bytes();
    // Read string from buffer, length specifies by param len.
    std::string read_string(int len);
    // Read bytes from buffer, length specifies by param len.
    void read_bytes(char *data, int size);

public:
    // Write 1bytes char to buffer.
    void write_1bytes(int8_t value);
    // Write 2bytes int to buffer.
    void write_2bytes(int16_t value);
    void write_le2bytes(int16_t value);
    // Write 4bytes int to buffer.
    void write_4bytes(int32_t value);
    void write_le4bytes(int32_t value);
    // Write 3bytes int to buffer.
    void write_3bytes(int32_t value);
    void write_le3bytes(int32_t value);
    // Write 8bytes int to buffer.
    void write_8bytes(int64_t value);
    void write_le8bytes(int64_t value);
    // Write string to buffer
    void write_string(std::string value);
    // Write bytes to buffer
    void write_bytes(char *data, int size);
};

/**
 * the bit buffer, base on SrsBuffer,
 * for exmaple, the h.264 avc buffer is bit buffer.
 */
class SrsBitBuffer
{
private:
    int8_t cb;
    uint8_t cb_left;
    SrsBuffer *stream;

public:
    SrsBitBuffer(SrsBuffer *b);
    ~SrsBitBuffer();

public:
    bool empty();
    int8_t read_bit();
    bool require_bits(int n);
    int left_bits();
    void skip_bits(int n);
    int32_t read_bits(int n);
    int8_t read_8bits();
    int16_t read_16bits();
    int32_t read_32bits();
    srs_error_t read_bits_ue(uint32_t &v);
    srs_error_t read_bits_se(int32_t &v);
};

// Memory block for shared payload data.
// This class encapsulates a memory buffer with size information,
// designed to be used with SrsSharedPtr for efficient memory sharing.
class SrsMemoryBlock
{
private:
    // The current message parsed size,
    //       size <= allocated buffer size
    // For the payload maybe sent in multiple chunks.
    int size_;
    // The payload of message, the SrsMemoryBlock manages the memory lifecycle.
    // @remark, not all message payload can be decoded to packet. for example,
    //       video/audio packet use raw bytes, no video/audio packet.
    char *payload_;

public:
    SrsMemoryBlock();
    virtual ~SrsMemoryBlock();

public:
    char *payload() { return payload_; }
    int size() { return size_; }

public:
    // Create memory block with specified size.
    // @param size, the size of memory to allocate. Must be non-negative.
    virtual void create(int size);
    // Create memory block from existing buffer.
    // @param data, the buffer to copy from.
    // @param size, the size of buffer. Must be non-negative.
    // @remark, this method will copy the data.
    virtual void create(char *data, int size);
    // Attach existing buffer to memory block.
    // @param data, the buffer to attach, memory block takes ownership.
    // @param size, the size of buffer. Must be non-negative.
    // @remark, this method takes ownership of data, caller should not free it.
    virtual void attach(char *data, int size);
};

#endif
