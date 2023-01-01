//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_fragment.hpp>

#include <srs_kernel_utility.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>

#include <unistd.h>
#include <sstream>
using namespace std;

SrsFragment::SrsFragment()
{
    dur = 0;
    start_dts = -1;
    sequence_header = false;
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

    if (start_dts == -1) {
        start_dts = dts_in_tbn;
    }
    
    // TODO: FIXME: Use cumulus dts.
    start_dts = srs_min(start_dts, dts_in_tbn);
    dur = dts_in_tbn - start_dts;
}

srs_utime_t SrsFragment::get_start_dts()
{
    return start_dts;
}

srs_utime_t SrsFragment::duration()
{
    return dur;
}

bool SrsFragment::is_sequence_header()
{
    return sequence_header;
}

void SrsFragment::set_sequence_header(bool v)
{
    sequence_header = v;
}

string SrsFragment::fullpath()
{
    return filepath;
}

void SrsFragment::set_path(string v)
{
    filepath = v;
}

srs_error_t SrsFragment::unlink_file()
{
    srs_error_t err = srs_success;
    
    if (::unlink(filepath.c_str()) < 0) {
        return srs_error_new(ERROR_SYSTEM_FRAGMENT_UNLINK, "unlink %s", filepath.c_str());
    }
    
    return err;
}

srs_error_t SrsFragment::create_dir()
{
    srs_error_t err = srs_success;
    
    std::string segment_dir = srs_path_dirname(filepath);
    
    if ((err = srs_create_dir_recursively(segment_dir)) != srs_success) {
        return srs_error_wrap(err, "create %s", segment_dir.c_str());
    }
    
    srs_info("Create dir %s ok", segment_dir.c_str());
    
    return err;
}

string SrsFragment::tmppath()
{
    return filepath + ".tmp";
}

srs_error_t SrsFragment::unlink_tmpfile()
{
    srs_error_t err = srs_success;
    
    string filepath = tmppath();
    if (::unlink(filepath.c_str()) < 0) {
        return srs_error_new(ERROR_SYSTEM_FRAGMENT_UNLINK, "unlink tmp file %s", filepath.c_str());
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
	   full_path = srs_string_replace(full_path, "[duration]", ss.str());
    }

    int r0 = ::rename(tmp_file.c_str(), full_path.c_str());
    if (r0 < 0) {
        return srs_error_new(ERROR_SYSTEM_FRAGMENT_RENAME, "rename %s to %s", tmp_file.c_str(), full_path.c_str());
    }

    filepath = full_path;
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

SrsFragmentWindow::SrsFragmentWindow()
{
}

SrsFragmentWindow::~SrsFragmentWindow()
{
    vector<SrsFragment*>::iterator it;
    
    for (it = fragments.begin(); it != fragments.end(); ++it) {
        SrsFragment* fragment = *it;
        srs_freep(fragment);
    }
    fragments.clear();
    
    for (it = expired_fragments.begin(); it != expired_fragments.end(); ++it) {
        SrsFragment* fragment = *it;
        srs_freep(fragment);
    }
    expired_fragments.clear();
}

void SrsFragmentWindow::dispose()
{
    srs_error_t err = srs_success;
    
    std::vector<SrsFragment*>::iterator it;
    
    for (it = fragments.begin(); it != fragments.end(); ++it) {
        SrsFragment* fragment = *it;
        if ((err = fragment->unlink_file()) != srs_success) {
            srs_warn("Unlink ts failed %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
        srs_freep(fragment);
    }
    fragments.clear();
    
    for (it = expired_fragments.begin(); it != expired_fragments.end(); ++it) {
        SrsFragment* fragment = *it;
        if ((err = fragment->unlink_file()) != srs_success) {
            srs_warn("Unlink ts failed %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
        srs_freep(fragment);
    }
    expired_fragments.clear();
}

void SrsFragmentWindow::append(SrsFragment* fragment)
{
    fragments.push_back(fragment);
}

void SrsFragmentWindow::shrink(srs_utime_t window)
{
    srs_utime_t duration = 0;
    
    int remove_index = -1;
    
    for (int i = (int)fragments.size() - 1; i >= 0; i--) {
        SrsFragment* fragment = fragments[i];
        duration += fragment->duration();
        
        if (duration > window) {
            remove_index = i;
            break;
        }
    }
    
    for (int i = 0; i < remove_index && !fragments.empty(); i++) {
        SrsFragment* fragment = *fragments.begin();
        fragments.erase(fragments.begin());
        expired_fragments.push_back(fragment);
    }
}

void SrsFragmentWindow::clear_expired(bool delete_files)
{
    srs_error_t err = srs_success;
    
    std::vector<SrsFragment*>::iterator it;
    
    for (it = expired_fragments.begin(); it != expired_fragments.end(); ++it) {
        SrsFragment* fragment = *it;
        if (delete_files && (err = fragment->unlink_file()) != srs_success) {
            srs_warn("Unlink ts failed, %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
        srs_freep(fragment);
    }
    
    expired_fragments.clear();
}

srs_utime_t SrsFragmentWindow::max_duration()
{
    srs_utime_t v = 0;
    
    std::vector<SrsFragment*>::iterator it;
    
    for (it = fragments.begin(); it != fragments.end(); ++it) {
        SrsFragment* fragment = *it;
        v = srs_max(v, fragment->duration());
    }
    
    return v;
}

bool SrsFragmentWindow::empty()
{
    return fragments.empty();
}

SrsFragment* SrsFragmentWindow::first()
{
    return fragments.at(0);
}

int SrsFragmentWindow::size()
{
    return (int)fragments.size();
}

SrsFragment* SrsFragmentWindow::at(int index)
{
    return fragments.at(index);
}

