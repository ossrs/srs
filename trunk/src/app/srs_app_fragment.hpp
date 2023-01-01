//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_FRAGMENT_HPP
#define SRS_APP_FRAGMENT_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

// Represent a fragment, such as HLS segment, DVR segment or DASH segment.
// It's a media file, for example FLV or MP4, with duration.
class SrsFragment
{
private:
    // The duration in srs_utime_t.
    srs_utime_t dur;
    // The full file path of fragment.
    std::string filepath;
    // The start DTS in srs_utime_t of segment.
    srs_utime_t start_dts;
    // Whether current segement contains sequence header.
    bool sequence_header;
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

// The fragment window manage a series of fragment.
class SrsFragmentWindow
{
private:
    std::vector<SrsFragment*> fragments;
    // The expired fragments, need to be free in future.
    std::vector<SrsFragment*> expired_fragments;
public:
    SrsFragmentWindow();
    virtual ~SrsFragmentWindow();
public:
    // Dispose all fragments, delete the files.
    virtual void dispose();
    // Append a new fragment, which is ready to delivery to client.
    virtual void append(SrsFragment* fragment);
    // Shrink the window, push the expired fragment to a queue.
    virtual void shrink(srs_utime_t window);
    // Clear the expired fragments.
    virtual void clear_expired(bool delete_files);
    // Get the max duration in srs_utime_t of all fragments.
    virtual srs_utime_t max_duration();
public:
    virtual bool empty();
    virtual SrsFragment* first();
    virtual int size();
    virtual SrsFragment* at(int index);
};

#endif

