//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_FRAGMENT_HPP
#define SRS_APP_FRAGMENT_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

class SrsFormat;

class ISrsFragment
{
public:
    ISrsFragment();
    virtual ~ISrsFragment();

public:
    virtual void set_path(std::string v) = 0;
    virtual std::string tmppath() = 0;
    virtual srs_error_t rename() = 0;
    virtual void append(int64_t dts) = 0;
    virtual srs_error_t create_dir() = 0;
    virtual void set_number(uint64_t n) = 0;
    virtual uint64_t number() = 0;
    virtual srs_utime_t duration() = 0;
    virtual srs_error_t unlink_tmpfile() = 0;
    virtual srs_utime_t get_start_dts() = 0;
    virtual srs_error_t unlink_file() = 0;
};

// A media file with a duration, such as an HLS, DVR or DASH segment.
class SrsFragment : public ISrsFragment
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_utime_t dur_;
    std::string filepath_;
    srs_utime_t start_dts_;
    bool sequence_header_;
    // Used in the DASH MPD.
    uint64_t number_;

public:
    SrsFragment();
    virtual ~SrsFragment();

public:
    // The dts is in ms.
    virtual void append(int64_t dts);
    virtual srs_utime_t get_start_dts();
    virtual srs_utime_t duration();
    virtual bool is_sequence_header();
    virtual void set_sequence_header(bool v);
    virtual std::string fullpath();
    virtual void set_path(std::string v);
    virtual srs_error_t unlink_file();
    virtual srs_error_t create_dir();

public:
    virtual std::string tmppath();
    virtual srs_error_t unlink_tmpfile();
    virtual srs_error_t rename();

public:
    virtual void set_number(uint64_t n);
    virtual uint64_t number();
};

class ISrsFragmentWindow
{
public:
    ISrsFragmentWindow();
    virtual ~ISrsFragmentWindow();

public:
    virtual void dispose() = 0;
    virtual void append(ISrsFragment *fragment) = 0;
    virtual void shrink(srs_utime_t window) = 0;
    virtual void clear_expired(bool delete_files) = 0;
    virtual srs_utime_t max_duration() = 0;

public:
    virtual bool empty() = 0;
    virtual ISrsFragment *first() = 0;
    virtual int size() = 0;
    virtual ISrsFragment *at(int index) = 0;
};

class SrsFragmentWindow : public ISrsFragmentWindow
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<ISrsFragment *> fragments_;
    std::vector<ISrsFragment *> expired_fragments_;

public:
    SrsFragmentWindow();
    virtual ~SrsFragmentWindow();

public:
    virtual void dispose();
    virtual void append(ISrsFragment *fragment);
    virtual void shrink(srs_utime_t window);
    virtual void clear_expired(bool delete_files);
    virtual srs_utime_t max_duration();

public:
    virtual bool empty();
    virtual ISrsFragment *first();
    virtual int size();
    virtual ISrsFragment *at(int index);
};

#endif
