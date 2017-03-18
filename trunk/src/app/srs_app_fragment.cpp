/*
The MIT License (MIT)

Copyright (c) 2013-2017 SRS(ossrs)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <srs_app_fragment.hpp>

#include <srs_kernel_utility.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>

#include <unistd.h>
using namespace std;

SrsFragment::SrsFragment()
{
    dur = 0;
    start_dts = -1;
    sequence_header = false;
}

SrsFragment::~SrsFragment()
{
}

void SrsFragment::append(int64_t dts)
{
    if (start_dts == -1) {
        start_dts = dts;
    }
    
    // TODO: FIXME: Use cumulus dts.
    start_dts = srs_min(start_dts, dts);
    dur = dts - start_dts;
}

int64_t SrsFragment::duration()
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

int SrsFragment::unlink_file()
{
    int ret = ERROR_SUCCESS;
    
    if (::unlink(filepath.c_str()) < 0) {
        ret = ERROR_SYSTEM_FRAGMENT_UNLINK;
        srs_error("Unlink fragment failed, file=%s, ret=%d.", filepath.c_str(), ret);
        return ret;
    }
    
    return ret;
}

string SrsFragment::tmppath()
{
    return filepath + ".tmp";
}

int SrsFragment::unlink_tmpfile()
{
    int ret = ERROR_SUCCESS;
    
    string filepath = tmppath();
    if (::unlink(filepath.c_str()) < 0) {
        ret = ERROR_SYSTEM_FRAGMENT_UNLINK;
        srs_error("Unlink temporary fragment failed, file=%s, ret=%d.", filepath.c_str(), ret);
        return ret;
    }
    
    return ret;
}

int SrsFragment::rename()
{
    int ret = ERROR_SUCCESS;
    
    string full_path = fullpath();
    string tmp_file = tmppath();
    
    if (::rename(tmp_file.c_str(), full_path.c_str()) < 0) {
        ret = ERROR_SYSTEM_FRAGMENT_RENAME;
        srs_error("rename ts file failed, %s => %s. ret=%d", tmp_file.c_str(), full_path.c_str(), ret);
        return ret;
    }
    
    return ret;
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
    int ret = ERROR_SUCCESS;
    
    std::vector<SrsFragment*>::iterator it;
    
    for (it = fragments.begin(); it != fragments.end(); ++it) {
        SrsFragment* fragment = *it;
        if ((ret = fragment->unlink_file()) != ERROR_SUCCESS) {
            srs_warn("Unlink ts failed, file=%s, ret=%d", fragment->fullpath().c_str(), ret);
        }
        srs_freep(fragment);
    }
    fragments.clear();
    
    for (it = expired_fragments.begin(); it != expired_fragments.end(); ++it) {
        SrsFragment* fragment = *it;
        if ((ret = fragment->unlink_file()) != ERROR_SUCCESS) {
            srs_warn("Unlink ts failed, file=%s, ret=%d", fragment->fullpath().c_str(), ret);
        }
        srs_freep(fragment);
    }
    expired_fragments.clear();
}

void SrsFragmentWindow::append(SrsFragment* fragment)
{
    fragments.push_back(fragment);
}

void SrsFragmentWindow::shrink(int64_t window)
{
    int64_t duration = 0;
    
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
    int ret = ERROR_SUCCESS;
    
    std::vector<SrsFragment*>::iterator it;
    
    for (it = expired_fragments.begin(); it != expired_fragments.end(); ++it) {
        SrsFragment* fragment = *it;
        if (delete_files && (ret = fragment->unlink_file()) != ERROR_SUCCESS) {
            srs_warn("Unlink ts failed, file=%s, ret=%d", fragment->fullpath().c_str(), ret);
        }
        srs_freep(fragment);
    }
    
    expired_fragments.clear();
}

int64_t SrsFragmentWindow::max_duration()
{
    int64_t v = 0;
    
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

