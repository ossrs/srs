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

// Abstract interface for encoding objects to binary format.
//
// ISrsEncoder defines the contract for classes that can serialize/encode objects
// into binary data within an SrsBuffer. This interface is commonly implemented by
// protocol handlers, message formatters, and data serializers throughout SRS.
//
// Key responsibilities:
// - Calculate the exact number of bytes needed for encoding
// - Serialize object data into a provided buffer
// - Handle encoding errors gracefully
//
// Usage pattern:
//   ISrsEncoder* encoder = create_some_encoder();
//   uint64_t required_size = encoder->nb_bytes();
//
//   char buffer_data[required_size];
//   SrsBuffer buffer(buffer_data, required_size);
//
//   srs_error_t err = encoder->encode(&buffer);
//   if (err != srs_success) {
//       // Handle encoding error
//   }
class ISrsEncoder
{
public:
    ISrsEncoder();
    virtual ~ISrsEncoder();

public:
    // Calculate the number of bytes required to encode this object.
    // @return The exact number of bytes needed for successful encoding.
    // @remark This should return a consistent value for the same object state.
    // @remark Implementations must ensure the returned size is accurate to prevent buffer overruns.
    virtual uint64_t nb_bytes() = 0;

    // Encode this object into the provided buffer.
    // @param buf The target buffer to write encoded data into. Must not be NULL.
    // @return srs_success on successful encoding, error code on failure.
    // @remark Caller must ensure buf has at least nb_bytes() space available.
    // @remark The buffer's position will be advanced by the number of bytes written.
    // @remark On error, the buffer state is undefined and should not be used.
    virtual srs_error_t encode(SrsBuffer *buf) = 0;
};

// Abstract interface for decoding/deserializing objects from binary format.
//
// ISrsDecoder defines the contract for classes that can deserialize objects
// from binary data within an SrsBuffer. This interface complements ISrsEncoder
// and is commonly implemented by protocol parsers, message decoders, and data
// deserializers throughout SRS.
//
// Key responsibilities:
// - Deserialize object data from a provided buffer
// - Advance buffer position by the number of bytes consumed
// - Handle decoding errors gracefully
// - Maintain object state consistency during decoding
//
// Usage pattern:
//   ISrsDecoder* decoder = create_some_decoder();
//   SrsBuffer buffer(received_data, data_size);
//
//   srs_error_t err = decoder->decode(&buffer);
//   if (err != srs_success) {
//       // Handle decoding error
//   }
//   // Use decoded object...
class ISrsDecoder
{
public:
    ISrsDecoder();
    virtual ~ISrsDecoder();

public:
    // Decode/deserialize object from the provided buffer.
    // @param buf The source buffer containing binary data to decode. Must not be NULL.
    // @return srs_success on successful decoding, error code on failure.
    // @remark The buffer's position will be advanced by the number of bytes consumed.
    // @remark On error, both the object state and buffer position are undefined.
    // @remark Caller should ensure the buffer contains sufficient data for decoding.
    // @remark Implementation should validate input data and handle malformed content gracefully.
    virtual srs_error_t decode(SrsBuffer *buf) = 0;
};

// Abstract interface for bidirectional encoding/decoding of objects to/from binary format.
//
// ISrsCodec combines ISrsEncoder and ISrsDecoder to provide both serialization
// (encoding) and deserialization (decoding) capabilities. This interface is the
// foundation for protocol handlers, message parsers, and data format converters
// in SRS that need to both read and write binary data.
//
// Key capabilities:
// - Serialize objects to binary format (inherited from ISrsEncoder)
// - Deserialize objects from binary format (inherited from ISrsDecoder)
// - Calculate exact space requirements for encoding
// - Handle encoding/decoding errors gracefully
// - Ensure symmetric encode/decode operations
//
// Common use cases:
// - Protocol message handlers (RTMP, HTTP, WebRTC, etc.)
// - Media format parsers (AMF0, JSON, FLV, etc.)
// - Configuration and metadata serializers
// - Bidirectional data converters and transformers
//
// Complete workflow example:
//   // Encoding workflow
//   ISrsCodec* codec = create_some_codec();
//   uint64_t size = codec->nb_bytes();
//
//   char buffer_data[size];
//   SrsBuffer encode_buffer(buffer_data, size);
//
//   srs_error_t err = codec->encode(&encode_buffer);
//   // Transmit or store encoded data...
//
//   // Decoding workflow
//   ISrsCodec* decoder = create_same_codec_type();
//   SrsBuffer decode_buffer(received_data, data_size);
//
//   err = decoder->decode(&decode_buffer);
//   // Use decoded object (should match original)...
//
// @remark Protocol handlers, AMF0, and JSON parsers should implement this interface.
// @remark Implementations must ensure encode/decode operations are symmetric.
// @remark The same codec type should be able to decode what it encoded.
// TODO: FIXME: protocol, amf0, json should implement this interface.
class ISrsCodec : public ISrsEncoder, public ISrsDecoder
{
public:
    ISrsCodec();
    virtual ~ISrsCodec();
};

// Byte-level buffer for reading and writing binary data with automatic position tracking.
//
// SrsBuffer provides a convenient interface for reading and writing basic data types
// (integers, strings, byte arrays) to/from a binary buffer. It maintains an internal
// position pointer that automatically advances as data is read or written, making it
// ideal for parsing and constructing binary protocols and file formats.
//
// Key features:
// - Automatic position tracking with read/write operations
// - Support for both big-endian (network byte order) and little-endian data
// - Type-safe reading/writing of 1, 2, 3, 4, and 8-byte integers
// - String and raw byte array operations
// - Buffer bounds checking and validation
// - Position manipulation (skip, reset) for flexible parsing
//
// Usage example:
//   char data[1024];
//   SrsBuffer buffer(data, sizeof(data));
//
//   // Writing data
//   buffer.write_4bytes(0x12345678);    // Write 32-bit integer
//   buffer.write_string("hello");       // Write string
//
//   // Reading data (reset position first)
//   buffer.skip(-buffer.pos());         // Reset to beginning
//   int32_t value = buffer.read_4bytes(); // Read 32-bit integer
//   std::string str = buffer.read_string(5); // Read 5-byte string
//
// Memory management:
// - SrsBuffer does NOT take ownership of the provided buffer
// - Caller is responsible for allocating and freeing the buffer memory
// - Buffer must remain valid for the lifetime of the SrsBuffer instance
//
// @remark The buffer never manages the bytes memory, user must manage it.
class SrsBuffer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Current read/write position within the buffer
    char *p_;
    // Pointer to the start of the buffer data (not owned by this class)
    char *bytes_;
    // Total size of the buffer in bytes
    int nb_bytes_;

public:
    // Create a buffer wrapper around existing memory.
    // @param b Pointer to the buffer data. Must not be NULL and must remain valid
    //          for the lifetime of this SrsBuffer instance.
    // @param nn Size of the buffer in bytes. Must be non-negative.
    // @remark User must manage the memory lifecycle of buffer b.
    SrsBuffer(char *b, int nn);

    // Destructor. Does not free the underlying buffer memory.
    virtual ~SrsBuffer();

public:
    // Create a copy of this buffer with the same position and data reference.
    // @return A new SrsBuffer instance pointing to the same data with same position.
    // @remark The returned buffer shares the same underlying data pointer.
    // @remark Caller is responsible for deleting the returned buffer.
    SrsBuffer *copy();

    // Get pointer to the start of the buffer data.
    // @return Pointer to the beginning of the buffer (same as constructor parameter).
    char *data();

    // Get pointer to the current position in the buffer.
    // @return Pointer to current read/write position (data() + pos()).
    char *head();

    // Get the total size of the buffer.
    // @return Total buffer size in bytes.
    int size();

    // Set the total size of the buffer.
    // @param v New size in bytes. Should not exceed the original buffer size.
    // @remark Use with caution - does not reallocate memory.
    void set_size(int v);

    // Get the current position within the buffer.
    // @return Current offset from the start of the buffer in bytes.
    int pos();

    // Get the number of bytes remaining from current position to end of buffer.
    // @return Number of unread/unwritten bytes (size() - pos()).
    int left();

    // Check if the buffer has no remaining bytes.
    // @return true if pos() >= size(), false otherwise.
    bool empty();

    // Check if the buffer has enough remaining bytes for a read/write operation.
    // @param required_size Number of bytes needed for the operation.
    // @return true if left() >= required_size, false otherwise.
    // @remark Always call this before read/write operations to avoid buffer overruns.
    // @remark Asserts that required_size is non-negative.
    bool require(int required_size);

public:
    // Move the current position forward or backward within the buffer.
    // @param size Number of bytes to skip. Positive values move forward,
    //             negative values move backward.
    // @remark Use skip(-pos()) to reset buffer position to the beginning.
    // @remark Asserts that the buffer is initialized (data() is not NULL).
    // @remark Does not perform bounds checking - caller must ensure valid position.
    void skip(int size);

public:
    // Read a single byte from the buffer and advance position.
    // @return The byte value as a signed 8-bit integer.
    // @remark Caller must ensure require(1) before calling.
    int8_t read_1bytes();

    // Read a 2-byte integer from the buffer in big-endian (network) byte order.
    // @return The 16-bit integer value.
    // @remark Caller must ensure require(2) before calling.
    int16_t read_2bytes();

    // Read a 2-byte integer from the buffer in little-endian byte order.
    // @return The 16-bit integer value.
    // @remark Caller must ensure require(2) before calling.
    int16_t read_le2bytes();

    // Read a 3-byte integer from the buffer in big-endian byte order.
    // @return The 24-bit value as a 32-bit integer (high byte is 0).
    // @remark Caller must ensure require(3) before calling.
    int32_t read_3bytes();

    // Read a 3-byte integer from the buffer in little-endian byte order.
    // @return The 24-bit value as a 32-bit integer (high byte is 0).
    // @remark Caller must ensure require(3) before calling.
    int32_t read_le3bytes();

    // Read a 4-byte integer from the buffer in big-endian (network) byte order.
    // @return The 32-bit integer value.
    // @remark Caller must ensure require(4) before calling.
    int32_t read_4bytes();

    // Read a 4-byte integer from the buffer in little-endian byte order.
    // @return The 32-bit integer value.
    // @remark Caller must ensure require(4) before calling.
    int32_t read_le4bytes();

    // Read an 8-byte integer from the buffer in big-endian (network) byte order.
    // @return The 64-bit integer value.
    // @remark Caller must ensure require(8) before calling.
    int64_t read_8bytes();

    // Read an 8-byte integer from the buffer in little-endian byte order.
    // @return The 64-bit integer value.
    // @remark Caller must ensure require(8) before calling.
    int64_t read_le8bytes();

    // Read a string of specified length from the buffer.
    // @param len Number of bytes to read as string data.
    // @return String containing the read bytes.
    // @remark Caller must ensure require(len) before calling.
    // @remark Does not assume null-termination - reads exactly len bytes.
    std::string read_string(int len);

    // Read raw bytes from the buffer into a provided array.
    // @param data Destination buffer to copy bytes into. Must not be NULL.
    // @param size Number of bytes to read and copy.
    // @remark Caller must ensure require(size) before calling.
    // @remark Caller must ensure data buffer has at least size bytes available.
    void read_bytes(char *data, int size);

public:
    // Write a single byte to the buffer and advance position.
    // @param value The byte value to write.
    // @remark Caller must ensure require(1) before calling.
    void write_1bytes(int8_t value);

    // Write a 2-byte integer to the buffer in big-endian (network) byte order.
    // @param value The 16-bit integer value to write.
    // @remark Caller must ensure require(2) before calling.
    void write_2bytes(int16_t value);

    // Write a 2-byte integer to the buffer in little-endian byte order.
    // @param value The 16-bit integer value to write.
    // @remark Caller must ensure require(2) before calling.
    void write_le2bytes(int16_t value);

    // Write a 4-byte integer to the buffer in big-endian (network) byte order.
    // @param value The 32-bit integer value to write.
    // @remark Caller must ensure require(4) before calling.
    void write_4bytes(int32_t value);

    // Write a 4-byte integer to the buffer in little-endian byte order.
    // @param value The 32-bit integer value to write.
    // @remark Caller must ensure require(4) before calling.
    void write_le4bytes(int32_t value);

    // Write a 3-byte integer to the buffer in big-endian byte order.
    // @param value The 24-bit value to write (high byte ignored).
    // @remark Caller must ensure require(3) before calling.
    void write_3bytes(int32_t value);

    // Write a 3-byte integer to the buffer in little-endian byte order.
    // @param value The 24-bit value to write (high byte ignored).
    // @remark Caller must ensure require(3) before calling.
    void write_le3bytes(int32_t value);

    // Write an 8-byte integer to the buffer in big-endian (network) byte order.
    // @param value The 64-bit integer value to write.
    // @remark Caller must ensure require(8) before calling.
    void write_8bytes(int64_t value);

    // Write an 8-byte integer to the buffer in little-endian byte order.
    // @param value The 64-bit integer value to write.
    // @remark Caller must ensure require(8) before calling.
    void write_le8bytes(int64_t value);

    // Write a string to the buffer as raw bytes.
    // @param value The string to write. All bytes of the string are written.
    // @remark Caller must ensure require(value.length()) before calling.
    // @remark Does not write null terminator - only the string content.
    void write_string(std::string value);

    // Write raw bytes from an array to the buffer.
    // @param data Source buffer containing bytes to write. Must not be NULL.
    // @param size Number of bytes to write from the source buffer.
    // @remark Caller must ensure require(size) before calling.
    // @remark Caller must ensure data buffer contains at least size bytes.
    void write_bytes(char *data, int size);
};

// Bit-level buffer reader for parsing binary data at bit granularity.
//
// SrsBitBuffer provides bit-level access to binary data, commonly used for parsing
// video codec bitstreams like H.264/AVC, H.265/HEVC, and other formats that require
// bit-precise parsing. It wraps an SrsBuffer and maintains internal state to track
// partial byte consumption.
//
// Key features:
// - Bit-level reading with automatic byte boundary handling
// - Optimized fast paths for byte-aligned operations (8, 16, 32-bit reads)
// - Support for Exp-Golomb encoding used in H.264/H.265 standards
// - Efficient buffering with minimal memory overhead
//
// Usage example:
//   char data[] = {0x12, 0x34, 0x56, 0x78};
//   SrsBuffer buffer(data, 4);
//   SrsBitBuffer bb(&buffer);
//
//   int bit = bb.read_bit();           // Read single bit
//   int nibble = bb.read_bits(4);      // Read 4 bits
//   int byte_val = bb.read_8bits();    // Read 8 bits (optimized)
//
//   uint32_t ue_val;
//   bb.read_bits_ue(ue_val);           // Read unsigned Exp-Golomb
//
// @remark The underlying SrsBuffer must remain valid for the lifetime of SrsBitBuffer.
// @remark This class does not take ownership of the SrsBuffer pointer.
class SrsBitBuffer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Current byte being processed (cached from stream)
    int8_t cb_;
    // Number of unread bits remaining in current byte (0-8)
    uint8_t cb_left_;
    // Underlying byte buffer (not owned by this class)
    SrsBuffer *stream_;

public:
    // Construct a bit buffer from an existing byte buffer.
    // @param b The underlying SrsBuffer to read from. Must not be NULL and must
    //          remain valid for the lifetime of this SrsBitBuffer instance.
    SrsBitBuffer(SrsBuffer *b);

    // Destructor. Does not free the underlying SrsBuffer.
    virtual ~SrsBitBuffer();

public:
    // Check if the bit buffer is empty (no more bits to read).
    // @return true if no more bits are available, false otherwise.
    bool empty();

    // Read a single bit from the buffer.
    // @return The bit value (0 or 1).
    // @remark Caller must ensure !empty() before calling.
    int8_t read_bit();

    // Check if the specified number of bits are available for reading.
    // @param n Number of bits to check for availability. Must be non-negative.
    // @return true if n bits are available, false otherwise.
    bool require_bits(int n);

    // Get the number of bits remaining in the buffer.
    // @return Total number of unread bits available.
    int left_bits();

    // Skip the specified number of bits without reading their values.
    // @param n Number of bits to skip. Must be <= left_bits().
    void skip_bits(int n);

    // Read the specified number of bits as an integer value.
    // @param n Number of bits to read (1-32). Must be <= left_bits().
    // @return The bits interpreted as an unsigned integer, MSB first.
    int32_t read_bits(int n);

    // Read 8 bits as a byte value.
    // Uses optimized fast path when byte-aligned, falls back to read_bits(8) otherwise.
    // @return The 8-bit value.
    // @remark Caller must ensure at least 8 bits are available.
    int8_t read_8bits();

    // Read 16 bits as a short value.
    // Uses optimized fast path when byte-aligned, falls back to read_bits(16) otherwise.
    // @return The 16-bit value in network byte order.
    // @remark Caller must ensure at least 16 bits are available.
    int16_t read_16bits();

    // Read 32 bits as an integer value.
    // Uses optimized fast path when byte-aligned, falls back to read_bits(32) otherwise.
    // @return The 32-bit value in network byte order.
    // @remark Caller must ensure at least 32 bits are available.
    int32_t read_32bits();

    // Read an unsigned Exp-Golomb encoded value.
    //
    // Implements the ue(v) parsing algorithm from ITU-T H.265 specification:
    // - Count leading zero bits (leadingZeroBits)
    // - Read 1 bit (must be 1)
    // - Read leadingZeroBits more bits
    // - Calculate: codeNum = (2^leadingZeroBits) - 1 + read_bits(leadingZeroBits)
    //
    // Common in H.264/H.265 for encoding non-negative integers with variable length.
    // Smaller values use fewer bits.
    //
    // @param v Output parameter to store the decoded unsigned value.
    // @return srs_success on success, error code on failure (empty buffer, overflow).
    srs_error_t read_bits_ue(uint32_t &v);

    // Read a signed Exp-Golomb encoded value.
    //
    // Implements the se(v) parsing algorithm from ITU-T H.265 specification:
    // - First reads ue(v) to get unsigned code
    // - Maps to signed value: if (code & 1) result = (code + 1) / 2; else result = -(code / 2)
    //
    // This encoding alternates between positive and negative values:
    // se(0)=0, se(1)=1, se(-1)=-1, se(2)=2, se(-2)=-2, etc.
    //
    // @param v Output parameter to store the decoded signed value.
    // @return srs_success on success, error code on failure (propagated from read_bits_ue).
    srs_error_t read_bits_se(int32_t &v);
};

// Memory block for managing shared payload data with automatic lifecycle management.
//
// SrsMemoryBlock encapsulates a dynamically allocated memory buffer with size tracking,
// specifically designed for use with SrsSharedPtr to enable efficient memory sharing
// across multiple components without unnecessary copying. This is commonly used for
// message payloads, media data, and other binary content in SRS.
//
// Key features:
// - Automatic memory lifecycle management (allocation and deallocation)
// - Support for both copying and taking ownership of existing buffers
// - Size tracking for safe buffer operations
// - Designed for shared ownership via SrsSharedPtr
// - Suitable for chunked data that may arrive in multiple parts
//
// Usage patterns:
//
// Creating new memory block:
//   SrsSharedPtr<SrsMemoryBlock> block(new SrsMemoryBlock());
//   block->create(1024);  // Allocate 1KB
//   memcpy(block->payload(), data, data_size);
//
// Copying existing data:
//   SrsSharedPtr<SrsMemoryBlock> block(new SrsMemoryBlock());
//   block->create(source_data, source_size);  // Copies the data
//
// Taking ownership of existing buffer:
//   char* buffer = malloc(size);  // Caller allocates
//   SrsSharedPtr<SrsMemoryBlock> block(new SrsMemoryBlock());
//   block->attach(buffer, size);  // Takes ownership, caller must not free
//
// Shared usage:
//   SrsSharedPtr<SrsMemoryBlock> block1 = original_block;
//   SrsSharedPtr<SrsMemoryBlock> block2 = original_block;
//   // Both share the same underlying memory
//
// @remark Not all payload data can be decoded to structured packets - some data
//         (like video/audio packets) may remain as raw bytes.
// @remark The size may be less than the allocated buffer size for chunked data.
class SrsMemoryBlock
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Current size of valid data in the buffer.
    // This may be less than the allocated buffer size for chunked data
    // that arrives in multiple parts.
    int size_;

    // Pointer to the allocated memory buffer.
    // SrsMemoryBlock owns this memory and is responsible for its lifecycle.
    // The buffer contains the actual payload data.
    char *payload_;

public:
    // Construct an empty memory block.
    // Call create() or attach() to initialize with actual memory.
    SrsMemoryBlock();

    // Destructor automatically frees the managed memory.
    // Safe to call even if no memory was allocated.
    virtual ~SrsMemoryBlock();

public:
    // Get direct access to the payload buffer.
    // @return Pointer to the memory buffer, or NULL if not initialized.
    // @remark Caller should not free this pointer - it's managed by SrsMemoryBlock.
    // @remark The returned pointer is valid until the SrsMemoryBlock is destroyed.
    char *payload() { return payload_; }

    // Get the current size of valid data in the buffer.
    // @return Number of bytes of valid data, may be 0 if not initialized.
    // @remark This may be less than the allocated buffer size for chunked data.
    int size() { return size_; }

public:
    // Allocate a new memory buffer of the specified size.
    // @param size Number of bytes to allocate. Must be non-negative.
    // @remark Any existing buffer is freed before allocating the new one.
    // @remark The allocated memory is uninitialized - caller should fill it as needed.
    // @remark If size is 0, creates a valid but empty memory block.
    virtual void create(int size);

    // Create a memory buffer by copying data from an existing buffer.
    // @param data Source buffer to copy from. Must not be NULL if size > 0.
    // @param size Number of bytes to copy. Must be non-negative.
    // @remark Any existing buffer is freed before creating the new one.
    // @remark This method creates an independent copy - changes to source won't affect this block.
    // @remark If size is 0, creates a valid but empty memory block.
    virtual void create(char *data, int size);

    // Take ownership of an existing buffer without copying.
    // @param data Buffer to take ownership of. Must be allocated with malloc/new[].
    // @param size Size of the buffer in bytes. Must be non-negative.
    // @remark Any existing buffer is freed before attaching the new one.
    // @remark This method takes ownership - caller must NOT free the data pointer.
    // @remark The provided buffer will be freed with delete[] when this object is destroyed.
    // @remark If data is NULL and size is 0, creates a valid but empty memory block.
    virtual void attach(char *data, int size);
};

#endif
