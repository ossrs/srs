//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_UTEST_KERNEL_HPP
#define SRS_UTEST_KERNEL_HPP

/*
#include <srs_utest_kernel.hpp>
*/
#include <srs_utest.hpp>

#include <string>
#include <vector>

#include <srs_kernel_file.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_protocol_stream.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_ps.hpp>
#include <srs_kernel_stream.hpp>

class MockSrsFile
{
public:
    SrsBuffer* _buf;
    SrsSimpleStream _data;
public:
    MockSrsFile();
    virtual ~MockSrsFile();
public:
    virtual srs_error_t open(std::string file);
    virtual void close();
public:
    virtual srs_error_t write(void* data, size_t count, ssize_t* pnwrite);
    virtual srs_error_t read(void* data, size_t count, ssize_t* pnread);
    virtual srs_error_t lseek(off_t offset, int whence, off_t* seeked);
};

class MockFileRemover
{
private:
    std::string path_;
public:
    MockFileRemover(std::string p);
    virtual ~MockFileRemover();
};

class MockSrsFileWriter : public SrsFileWriter
{
public:
    MockSrsFile* uf;
    srs_error_t err;
    // Error if exceed this offset.
    int error_offset;
    // Whether opened.
    bool opened;
public:
    MockSrsFileWriter();
    virtual ~MockSrsFileWriter();
public:
    virtual srs_error_t open(std::string file);
    virtual void close();
public:
    virtual bool is_open();
    virtual void seek2(int64_t offset);
    virtual int64_t tellg();
    virtual int64_t filesize();
    virtual char* data();
    virtual string str();
public:
    virtual srs_error_t write(void* buf, size_t count, ssize_t* pnwrite);
    virtual srs_error_t lseek(off_t offset, int whence, off_t* seeked);
// for mock
public:
    void mock_reset_offset();
};

class MockSrsFileReader : public SrsFileReader
{
public:
    MockSrsFile* uf;
    bool opened;
    // Could seek.
    bool seekable;
public:
    MockSrsFileReader();
    MockSrsFileReader(const char* data, int nb_data);
    virtual ~MockSrsFileReader();
public:
    virtual srs_error_t open(std::string file);
    virtual void close();
public:
    virtual bool is_open();
    virtual int64_t tellg();
    virtual void skip(int64_t size);
    virtual int64_t seek2(int64_t offset);
    virtual int64_t filesize();
public:
    virtual srs_error_t read(void* buf, size_t count, ssize_t* pnread);
    virtual srs_error_t lseek(off_t offset, int whence, off_t* seeked);
// for mock
public:
    // append data to current offset, modify the offset and size.
    void mock_append_data(const char* _data, int _size);
    void mock_reset_offset();
};

class MockBufferReader: public ISrsReader
{
private:
    std::string str;
public:
    MockBufferReader(const char* data);
    virtual ~MockBufferReader();
public:
    virtual srs_error_t read(void* buf, size_t size, ssize_t* nread);
};

class MockSrsCodec : public ISrsCodec
{
public:
    MockSrsCodec();
    virtual ~MockSrsCodec();
public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer* buf);
    virtual srs_error_t decode(SrsBuffer* buf);
};

class MockTsHandler : public ISrsTsHandler
{
public:
    SrsTsMessage* msg;
public:
    MockTsHandler();
    virtual ~MockTsHandler();
public:
    virtual srs_error_t on_ts_message(SrsTsMessage* m);
};

class MockPsHandler : public ISrsPsMessageHandler
{
public:
    std::vector<SrsTsMessage*> msgs_;
public:
    MockPsHandler();
    virtual ~MockPsHandler();
public:
    virtual srs_error_t on_ts_message(SrsTsMessage* m);
    virtual void on_recover_mode(int nn_recover);
    virtual void on_recover_done(srs_utime_t duration);
    MockPsHandler* clear();
};

#endif

