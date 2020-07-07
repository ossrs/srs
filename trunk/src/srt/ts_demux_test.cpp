/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Runner365
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "ts_demux.hpp"
#include <string>
#include <memory>

#define TS_MAX 188

class media_data_get : public ts_media_data_callback_I {
public:
    media_data_get() {};
    virtual ~media_data_get() {};

public:
    virtual void on_data_callback(SRT_DATA_MSG_PTR data_ptr, unsigned int media_type
            , uint64_t dts, uint64_t pts) {
        printf("media type:%d, data len:%d, key_path:%s, dts:%lu(%lu), pts:%lu(%lu)\r\n", 
            media_type, data_ptr->data_len(), data_ptr->get_path().c_str(), dts, dts/90, pts, pts/90);
        FILE* file_p;
        char filename[80];
        
        sprintf(filename, "%u.media", media_type);
        file_p = fopen(filename, "ab+");
        if (file_p) {
            fwrite(data_ptr->get_data(), data_ptr->data_len(), 1, file_p);
            fclose(file_p);
        }
        return;
    }
};

int main(int argn, char** argv) {
    unsigned char data[TS_MAX];
    ts_demux demux_obj;
    auto callback_ptr = std::make_shared<media_data_get>();
    FILE* file_p;
    if (argn < 2) {
        printf("please input ts name.\r\n");
        return 0;
    }

    const char* file_name = argv[1];
    printf("input ts name:%s.\r\n", file_name);

    file_p = fopen(file_name, "r");
    fseek(file_p, 0L, SEEK_END); /* 定位到文件末尾 */
    size_t flen = ftell(file_p); /* 得到文件大小 */
    fseek(file_p, 0L, SEEK_SET); /* 定位到文件开头 */

    do {
        fread(data, TS_MAX, 1, file_p);
        auto input_ptr = std::make_shared<SRT_DATA_MSG>((unsigned char*)data, (unsigned int)TS_MAX, std::string("live/shiwei"));
        demux_obj.decode(input_ptr, callback_ptr);
        flen -= TS_MAX;
    } while(flen > 0);
    return 1;
}