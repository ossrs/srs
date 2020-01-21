#include "srt_data.hpp"
#include <string.h>

SRT_DATA_MSG::SRT_DATA_MSG(unsigned int len, const std::string& path):_len(len)
    ,_key_path(path) {
    _data_p = new unsigned char[len];
    memset(_data_p, 0, len);
}

SRT_DATA_MSG::SRT_DATA_MSG(unsigned char* data_p, unsigned int len, const std::string& path):_len(len)
    ,_key_path(path)
{
    _data_p = new unsigned char[len];
    memcpy(_data_p, data_p, len);
}

SRT_DATA_MSG::~SRT_DATA_MSG() {
    delete _data_p;
}

std::string SRT_DATA_MSG::get_path() {
    return _key_path;
}

unsigned int SRT_DATA_MSG::data_len() {
    return _len;
}

unsigned char* SRT_DATA_MSG::get_data() {
    return _data_p;
}
