//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_fragment.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>

#include <sstream>
#include <unistd.h>
using namespace std;

ISrsFragment::ISrsFragment()
{
}

ISrsFragment::~ISrsFragment()
{
}

SrsFragment::SrsFragment()
{
    dur_ = 0;
    start_dts_ = -1;
    sequence_header_ = false;
    number_ = 0;
}

SrsFragment::~SrsFragment()
{
}

void SrsFragment::append(int64_t dts)
{
    // The max positive ms is 0x7fffffffffffffff/1000.
    static const int64_t maxMS = 0x20c49ba5e353f7LL;

    // We reset negative or overflow dts to zero.
    if (dts > maxMS || dts < 0) {
        dts = 0;
    }

    srs_utime_t dts_in_tbn = dts * SRS_UTIME_MILLISECONDS;

    if (start_dts_ == -1) {
        start_dts_ = dts_in_tbn;
    }

    // TODO: FIXME: Use cumulus dts.
    start_dts_ = srs_min(start_dts_, dts_in_tbn);
    dur_ = dts_in_tbn - start_dts_;
}

srs_utime_t SrsFragment::get_start_dts()
{
    return start_dts_;
}

srs_utime_t SrsFragment::duration()
{
    return dur_;
}

bool SrsFragment::is_sequence_header()
{
    return sequence_header_;
}

void SrsFragment::set_sequence_header(bool v)
{
    sequence_header_ = v;
}

string SrsFragment::fullpath()
{
    return filepath_;
}

void SrsFragment::set_path(string v)
{
    filepath_ = v;
}

srs_error_t SrsFragment::unlink_file()
{
    srs_error_t err = srs_success;

    SrsPath path;
    if ((err = path.unlink(filepath_)) != srs_success) {
        return srs_error_wrap(err, "unlink %s", filepath_.c_str());
    }

    return err;
}

srs_error_t SrsFragment::create_dir()
{
    srs_error_t err = srs_success;

    SrsPath path;
    std::string segment_dir = path.filepath_dir(filepath_);

    if ((err = path.mkdir_all(segment_dir)) != srs_success) {
        return srs_error_wrap(err, "create %s", segment_dir.c_str());
    }

    srs_info("Create dir %s ok", segment_dir.c_str());

    return err;
}

string SrsFragment::tmppath()
{
    return filepath_ + ".tmp";
}

srs_error_t SrsFragment::unlink_tmpfile()
{
    srs_error_t err = srs_success;

    string filepath = tmppath();
    SrsPath path;
    if ((err = path.unlink(filepath)) != srs_success) {
        return srs_error_wrap(err, "unlink tmp file %s", filepath.c_str());
    }

    return err;
}

srs_error_t SrsFragment::rename()
{
    srs_error_t err = srs_success;

    string full_path = fullpath();
    string tmp_file = tmppath();
    int tempdur = srsu2msi(duration());
    if (true) {
        std::stringstream ss;
        ss << tempdur;
        full_path = srs_strings_replace(full_path, "[duration]", ss.str());
    }

    int r0 = ::rename(tmp_file.c_str(), full_path.c_str());
    if (r0 < 0) {
        return srs_error_new(ERROR_SYSTEM_FRAGMENT_RENAME, "rename %s to %s", tmp_file.c_str(), full_path.c_str());
    }

    filepath_ = full_path;
    return err;
}

void SrsFragment::set_number(uint64_t n)
{
    number_ = n;
}

uint64_t SrsFragment::number()
{
    return number_;
}

ISrsFragmentWindow::ISrsFragmentWindow()
{
}

ISrsFragmentWindow::~ISrsFragmentWindow()
{
}

SrsFragmentWindow::SrsFragmentWindow()
{
}

SrsFragmentWindow::~SrsFragmentWindow()
{
    vector<ISrsFragment *>::iterator it;

    for (it = fragments_.begin(); it != fragments_.end(); ++it) {
        ISrsFragment *fragment = *it;
        srs_freep(fragment);
    }
    fragments_.clear();

    for (it = expired_fragments_.begin(); it != expired_fragments_.end(); ++it) {
        ISrsFragment *fragment = *it;
        srs_freep(fragment);
    }
    expired_fragments_.clear();
}

void SrsFragmentWindow::dispose()
{
    srs_error_t err = srs_success;

    std::vector<ISrsFragment *>::iterator it;

    for (it = fragments_.begin(); it != fragments_.end(); ++it) {
        ISrsFragment *fragment = *it;
        if ((err = fragment->unlink_file()) != srs_success) {
            srs_warn("Unlink ts failed %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
        srs_freep(fragment);
    }
    fragments_.clear();

    for (it = expired_fragments_.begin(); it != expired_fragments_.end(); ++it) {
        ISrsFragment *fragment = *it;
        if ((err = fragment->unlink_file()) != srs_success) {
            srs_warn("Unlink ts failed %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
        srs_freep(fragment);
    }
    expired_fragments_.clear();
}

void SrsFragmentWindow::append(ISrsFragment *fragment)
{
    fragments_.push_back(fragment);
}

void SrsFragmentWindow::shrink(srs_utime_t window)
{
    srs_utime_t duration = 0;

    int remove_index = -1;

    for (int i = (int)fragments_.size() - 1; i >= 0; i--) {
        ISrsFragment *fragment = fragments_[i];
        duration += fragment->duration();

        if (duration > window) {
            remove_index = i;
            break;
        }
    }

    for (int i = 0; i < remove_index && !fragments_.empty(); i++) {
        ISrsFragment *fragment = *fragments_.begin();
        fragments_.erase(fragments_.begin());
        expired_fragments_.push_back(fragment);
    }
}

void SrsFragmentWindow::clear_expired(bool delete_files)
{
    srs_error_t err = srs_success;

    std::vector<ISrsFragment *>::iterator it;

    for (it = expired_fragments_.begin(); it != expired_fragments_.end(); ++it) {
        ISrsFragment *fragment = *it;
        if (delete_files && (err = fragment->unlink_file()) != srs_success) {
            srs_warn("Unlink ts failed, %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
        srs_freep(fragment);
    }

    expired_fragments_.clear();
}

srs_utime_t SrsFragmentWindow::max_duration()
{
    srs_utime_t v = 0;

    std::vector<ISrsFragment *>::iterator it;

    for (it = fragments_.begin(); it != fragments_.end(); ++it) {
        ISrsFragment *fragment = *it;
        v = srs_max(v, fragment->duration());
    }

    return v;
}

bool SrsFragmentWindow::empty()
{
    return fragments_.empty();
}

ISrsFragment *SrsFragmentWindow::first()
{
    return fragments_.at(0);
}

int SrsFragmentWindow::size()
{
    return (int)fragments_.size();
}

ISrsFragment *SrsFragmentWindow::at(int index)
{
    return fragments_.at(index);
}
