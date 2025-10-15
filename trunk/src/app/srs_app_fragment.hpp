//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_FRAGMENT_HPP
#define SRS_APP_FRAGMENT_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

// Forward declarations
class SrsFormat;

// The fragment interface.
class ISrsFragment
{
public:
    ISrsFragment();
    virtual ~ISrsFragment();

public:
    // Set the full path of fragment.
    virtual void set_path(std::string v) = 0;
    // Get the temporary path for file.
    virtual std::string tmppath() = 0;
    // Rename the temp file to final file.
    virtual srs_error_t rename() = 0;
    // Append a frame with dts into fragment.
    virtual void append(int64_t dts) = 0;
    // Create the dir for file recursively.
    virtual srs_error_t create_dir() = 0;
    // Set the number of this fragment.
    virtual void set_number(uint64_t n) = 0;
    // Get the number of this fragment.
    virtual uint64_t number() = 0;
    // Get the duration of fragment in srs_utime_t.
    virtual srs_utime_t duration() = 0;
    // Unlink the temporary file.
    virtual srs_error_t unlink_tmpfile() = 0;
    // Get the start dts of fragment.
    virtual srs_utime_t get_start_dts() = 0;
    // Unlink the fragment, to delete the file.
    virtual srs_error_t unlink_file() = 0;
};

// Represent a fragment, such as HLS segment, DVR segment or DASH segment.
// It's a media file, for example FLV or MP4, with duration.
class SrsFragment : public ISrsFragment
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The duration in srs_utime_t.
    srs_utime_t dur_;
    // The full file path of fragment.
    std::string filepath_;
    // The start DTS in srs_utime_t of segment.
    srs_utime_t start_dts_;
    // Whether current segement contains sequence header.
    bool sequence_header_;
    // The number of this segment, use in dash mpd.
    uint64_t number_;

public:
    SrsFragment();
    virtual ~SrsFragment();

public:
    // Append a frame with dts into fragment.
    // @dts The dts of frame in ms.
    virtual void append(int64_t dts);
    // Get the start dts of fragment.
    virtual srs_utime_t get_start_dts();
    // Get the duration of fragment in srs_utime_t.
    virtual srs_utime_t duration();
    // Whether the fragment contains any sequence header.
    virtual bool is_sequence_header();
    // Set whether contains sequence header.
    virtual void set_sequence_header(bool v);
    // Get the full path of fragment.
    virtual std::string fullpath();
    // Set the full path of fragment.
    virtual void set_path(std::string v);
    // Unlink the fragment, to delete the file.
    // @remark Ignore any error.
    virtual srs_error_t unlink_file();
    // Create the dir for file recursively.
    virtual srs_error_t create_dir();

public:
    // Get the temporary path for file.
    virtual std::string tmppath();
    // Unlink the temporary file.
    virtual srs_error_t unlink_tmpfile();
    // Rename the temp file to final file.
    virtual srs_error_t rename();

public:
    // Get or set the number of this fragment.
    virtual void set_number(uint64_t n);
    virtual uint64_t number();
};

// The fragment window interface.
class ISrsFragmentWindow
{
public:
    ISrsFragmentWindow();
    virtual ~ISrsFragmentWindow();

public:
    // Dispose all fragments, delete the files.
    virtual void dispose() = 0;
    // Append a new fragment, which is ready to delivery to client.
    virtual void append(ISrsFragment *fragment) = 0;
    // Shrink the window, push the expired fragment to a queue.
    virtual void shrink(srs_utime_t window) = 0;
    // Clear the expired fragments.
    virtual void clear_expired(bool delete_files) = 0;
    // Get the max duration in srs_utime_t of all fragments.
    virtual srs_utime_t max_duration() = 0;

public:
    virtual bool empty() = 0;
    virtual ISrsFragment *first() = 0;
    virtual int size() = 0;
    virtual ISrsFragment *at(int index) = 0;
};

// The fragment window manage a series of fragment.
class SrsFragmentWindow : public ISrsFragmentWindow
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<ISrsFragment *> fragments_;
    // The expired fragments, need to be free in future.
    std::vector<ISrsFragment *> expired_fragments_;

public:
    SrsFragmentWindow();
    virtual ~SrsFragmentWindow();

public:
    // Dispose all fragments, delete the files.
    virtual void dispose();
    // Append a new fragment, which is ready to delivery to client.
    virtual void append(ISrsFragment *fragment);
    // Shrink the window, push the expired fragment to a queue.
    virtual void shrink(srs_utime_t window);
    // Clear the expired fragments.
    virtual void clear_expired(bool delete_files);
    // Get the max duration in srs_utime_t of all fragments.
    virtual srs_utime_t max_duration();

public:
    virtual bool empty();
    virtual ISrsFragment *first();
    virtual int size();
    virtual ISrsFragment *at(int index);
};

#endif
