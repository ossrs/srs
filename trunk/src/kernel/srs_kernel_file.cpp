/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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

#include <srs_kernel_file.hpp>

#include <fcntl.h>
#include <unistd.h>
#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>

SrsFileWriter::SrsFileWriter()
{
    fd = -1;
}

SrsFileWriter::~SrsFileWriter()
{
    close();
}

int SrsFileWriter::open(string file)
{
    int ret = ERROR_SUCCESS;
    
    if (fd > 0) {
        ret = ERROR_SYSTEM_FILE_ALREADY_OPENED;
        srs_error("file %s already opened. ret=%d", _file.c_str(), ret);
        return ret;
    }
    
    int flags = O_CREAT|O_WRONLY|O_TRUNC;
    mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH;

    if ((fd = ::open(file.c_str(), flags, mode)) < 0) {
        ret = ERROR_SYSTEM_FILE_OPENE;
        srs_error("open file %s failed. ret=%d", file.c_str(), ret);
        return ret;
    }
    
    _file = file;
    
    return ret;
}

void SrsFileWriter::close()
{
    int ret = ERROR_SUCCESS;
    
    if (fd < 0) {
        return;
    }
    
    if (::close(fd) < 0) {
        ret = ERROR_SYSTEM_FILE_CLOSE;
        srs_error("close file %s failed. ret=%d", _file.c_str(), ret);
        return;
    }
    fd = -1;
    
    return;
}

bool SrsFileWriter::is_open()
{
    return fd > 0;
}

int64_t SrsFileWriter::tellg()
{
    return (int64_t)::lseek(fd, 0, SEEK_CUR);
}

int SrsFileWriter::write(void* buf, size_t count, ssize_t* pnwrite)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nwrite;
    // TODO: FIXME: use st_write.
    if ((nwrite = ::write(fd, buf, count)) < 0) {
        ret = ERROR_SYSTEM_FILE_WRITE;
        srs_error("write to file %s failed. ret=%d", _file.c_str(), ret);
        return ret;
    }
    
    if (pnwrite != NULL) {
        *pnwrite = nwrite;
    }
    
    return ret;
}

SrsFileReader::SrsFileReader()
{
    fd = -1;
}

SrsFileReader::~SrsFileReader()
{
    close();
}

int SrsFileReader::open(string file)
{
    int ret = ERROR_SUCCESS;
    
    if (fd > 0) {
        ret = ERROR_SYSTEM_FILE_ALREADY_OPENED;
        srs_error("file %s already opened. ret=%d", _file.c_str(), ret);
        return ret;
    }

    if ((fd = ::open(file.c_str(), O_RDONLY)) < 0) {
        ret = ERROR_SYSTEM_FILE_OPENE;
        srs_error("open file %s failed. ret=%d", file.c_str(), ret);
        return ret;
    }
    
    _file = file;
    
    return ret;
}

void SrsFileReader::close()
{
    int ret = ERROR_SUCCESS;
    
    if (fd < 0) {
        return;
    }
    
    if (::close(fd) < 0) {
        ret = ERROR_SYSTEM_FILE_CLOSE;
        srs_error("close file %s failed. ret=%d", _file.c_str(), ret);
        return;
    }
    fd = -1;
    
    return;
}

bool SrsFileReader::is_open()
{
    return fd > 0;
}

int64_t SrsFileReader::tellg()
{
    return (int64_t)::lseek(fd, 0, SEEK_CUR);
}

void SrsFileReader::skip(int64_t size)
{
    ::lseek(fd, size, SEEK_CUR);
}

int64_t SrsFileReader::lseek(int64_t offset)
{
    return (int64_t)::lseek(fd, offset, SEEK_SET);
}

int64_t SrsFileReader::filesize()
{
    int64_t cur = tellg();
    int64_t size = (int64_t)::lseek(fd, 0, SEEK_END);
    ::lseek(fd, cur, SEEK_SET);
    return size;
}

int SrsFileReader::read(void* buf, size_t count, ssize_t* pnread)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nread;
    // TODO: FIXME: use st_read.
    if ((nread = ::read(fd, buf, count)) < 0) {
        ret = ERROR_SYSTEM_FILE_READ;
        srs_error("read from file %s failed. ret=%d", _file.c_str(), ret);
        return ret;
    }
    
    if (nread == 0) {
        ret = ERROR_SYSTEM_FILE_EOF;
        return ret;
    }
    
    if (pnread != NULL) {
        *pnread = nread;
    }
    
    return ret;
}
