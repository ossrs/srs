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
/**
gcc srs_flv_injecter.c ../../objs/lib/srs_librtmp.a -g -O0 -lstdc++ -o srs_flv_injecter
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "../../objs/include/srs_librtmp.h"

#define ERROR_INJECTED 10000

int process(const char* in_flv_file, const char* out_flv_file, srs_flv_t* pic, srs_flv_t* poc);
int build_keyframes(srs_flv_t ic, srs_amf0_t *pname, srs_amf0_t* pdata, srs_amf0_t* pfilepositions, int64_t* pmetadata_end_offset);
int do_inject_flv(srs_flv_t ic, srs_flv_t oc, srs_amf0_t amf0_name, srs_amf0_t amf0_data, srs_amf0_t filepositions, int64_t metadata_end_offset);

int main(int argc, char** argv)
{
    int ret = 0;
    
    // user options.
    char* in_flv_file;
    char* out_flv_file;
    // flv handler
    srs_flv_t ic = NULL;
    srs_flv_t oc = NULL;
    
    // temp variables.
    int tmp_file_size = 0;
    char* tmp_file;

    printf("inject flv file keyframes to metadata.\n");
    printf("srs(ossrs) client librtmp library.\n");
    printf("version: %d.%d.%d\n", srs_version_major(), srs_version_minor(), srs_version_revision());
    
    if (argc <= 2) {
        printf("inject flv file keyframes to metadata\n"
            "Usage: %s in_flv_file out_flv_file\n"
            "   in_flv_file         input flv file to inject.\n"
            "   out_flv_file        the inject output file, can be in_flv_file.\n"
            "For example:\n"
            "   %s doc/source.200kbps.768x320.flv injected.flv\n"
            "   %s ../../doc/source.200kbps.768x320.flv injected.flv\n",
            argv[0], argv[0], argv[0]);
        exit(-1);
    }
    
    in_flv_file = argv[1];
    out_flv_file = argv[2];
    
    tmp_file_size = strlen(out_flv_file) + strlen(".tmp") + 1;
    tmp_file = (char*)malloc(tmp_file_size);
    snprintf(tmp_file, tmp_file_size, "%s.tmp", out_flv_file);
    
    srs_human_trace("input:  %s", in_flv_file);
    srs_human_trace("output:  %s", out_flv_file);
    srs_human_trace("tmp_file:  %s", tmp_file);

    ret = process(in_flv_file, tmp_file, &ic, &oc);
    
    srs_flv_close(ic);
    srs_flv_close(oc);
    
    if (ret != 0) {
        unlink(tmp_file);
        if (ret == ERROR_INJECTED) {
            ret = 0;
            srs_human_trace("file already injected.");
        } else {
            srs_human_trace("error, remove tmp file.");
        }
    } else {
        rename(tmp_file, out_flv_file);
        srs_human_trace("completed, rename to %s", out_flv_file);
    }
    
    free(tmp_file);
    
    return ret;
}

int process(const char* in_flv_file, const char* out_flv_file, srs_flv_t* pic, srs_flv_t* poc)
{
    int ret = 0;
    
    srs_flv_t ic;
    srs_flv_t oc;
    
    // to adjust metadata.
    // the ic metadata end offset, the next tag start offset.
    // all oc metadata must adjust according to:
    //      adjust = new_metadata_end_offset - metadata_end_offset
    int64_t metadata_end_offset = 0;
    
    // metadata
    srs_amf0_t amf0_name = NULL;
    srs_amf0_t amf0_data = NULL;
    srs_amf0_t filepositions = NULL;
    
    if ((ic = srs_flv_open_read(in_flv_file)) == NULL) {
        ret = 2;
        srs_human_trace("open input flv file failed. ret=%d", ret);
        return ret;
    }
    *pic = ic;
    
    if ((oc = srs_flv_open_write(out_flv_file)) == NULL) {
        ret = 2;
        srs_human_trace("open output flv file failed. ret=%d", ret);
        return ret;
    }
    *poc = oc;
    
    /**
    * we use two roundtrip to avoid the paddings of metadata,
    * to support large keyframes videos without padding fields.
    */
    // build keyframes offset to metadata.
    if ((ret = build_keyframes(ic, &amf0_name, &amf0_data, &filepositions, &metadata_end_offset)) != 0) {
        return ret;
    }
    
    // inject the metadata to oc.
    if ((ret = do_inject_flv(ic, oc, amf0_name, amf0_data, filepositions, metadata_end_offset)) != 0) {
        return ret;
    }
    
    // TODO: FIXME: mem leak when error.
    srs_amf0_free(amf0_name);
    srs_amf0_free(amf0_data);
    
    return ret;
}

int parse_metadata(char* data, int size, srs_amf0_t* pname, srs_amf0_t* pdata)
{
    int ret = 0;
    
    int nparsed = 0;
    *pname = srs_amf0_parse(data, size, &nparsed);
    
    if (*pname == NULL || nparsed >= size) {
        srs_human_trace("invalid amf0 name data.");
        return -1;
    }
    
    *pdata = srs_amf0_parse(data + nparsed, size - nparsed, &nparsed);
    if (*pdata == NULL || nparsed > size) {
        srs_human_trace("invalid amf0 value data");
        return -1;
    }
    
    return ret;
}

int build_keyframes(srs_flv_t ic, srs_amf0_t *pname, srs_amf0_t* pdata, srs_amf0_t* pfilepositions, int64_t* pmetadata_end_offset)
{
    int ret = 0;
    
    // flv header
    char header[13];
    
    // packet data
    char type;
    u_int32_t timestamp = 0;
    char* data = NULL;
    int32_t size;
    int64_t offset = 0;
    
    // metadata
    srs_amf0_t amf0_name = NULL;
    srs_amf0_t amf0_data = NULL;
    
    srs_amf0_t keyframes = NULL;
    srs_amf0_t filepositions = NULL;
    srs_amf0_t times = NULL;
    
    // reset to generate metadata
    srs_flv_lseek(ic, 0);
    
    if ((ret = srs_flv_read_header(ic, header)) != 0) {
        return ret;
    }
    
    srs_human_trace("build keyframe infos from flv");
    for (;;) {
        offset = srs_flv_tellg(ic);
        
        // tag header
        if ((ret = srs_flv_read_tag_header(ic, &type, &size, &timestamp)) != 0) {
            if (srs_flv_is_eof(ret)) {
                srs_human_trace("parse completed.");
                return 0;
            }
            srs_human_trace("flv get packet failed. ret=%d", ret);
            return ret;
        }
        
        if (size <= 0) {
            srs_human_trace("invalid size=%d", size);
            return ret;
        }
        
        // TODO: FIXME: mem leak when error.
        data = (char*)malloc(size);
        if ((ret = srs_flv_read_tag_data(ic, data, size)) != 0) {
            return ret;
        }
        
        // data tag
        if (type == SRS_RTMP_TYPE_VIDEO) {
            if (!srs_flv_is_sequence_header(data, size) && srs_flv_is_keyframe(data, size)) {
                srs_amf0_strict_array_append(filepositions, srs_amf0_create_number(offset));
                srs_amf0_strict_array_append(times, srs_amf0_create_number(((double)timestamp)/ 1000));
            }
        } else if (type == SRS_RTMP_TYPE_SCRIPT) {
            *pmetadata_end_offset = srs_flv_tellg(ic);
            if ((ret = parse_metadata(data, size, &amf0_name, &amf0_data)) != 0) {
                return ret;
            }
            
            *pname = amf0_name;
            *pdata = amf0_data;
            
            if (srs_amf0_is_object(amf0_data)) {
                keyframes = srs_amf0_object_property(amf0_data, "keyframes");
                if (keyframes == NULL) {
                    keyframes = srs_amf0_create_object();
                    srs_amf0_object_property_set(amf0_data, "keyframes", keyframes);
                }
                // always clear the old keyframes.
                srs_amf0_object_clear(keyframes);
                
                *pfilepositions = filepositions = srs_amf0_create_strict_array();
                srs_amf0_object_property_set(keyframes, "filepositions", filepositions);
                
                times = srs_amf0_create_strict_array();
                srs_amf0_object_property_set(keyframes, "times", times);
            } else if (srs_amf0_is_ecma_array(amf0_data)) {
                keyframes = srs_amf0_ecma_array_property(amf0_data, "keyframes");
                if (keyframes == NULL) {
                    keyframes = srs_amf0_create_object();
                    srs_amf0_ecma_array_property_set(amf0_data, "keyframes", keyframes);
                }
                // always clear the old keyframes.
                srs_amf0_object_clear(keyframes);
                
                *pfilepositions = filepositions = srs_amf0_create_strict_array();
                srs_amf0_object_property_set(keyframes, "filepositions", filepositions);
                
                times = srs_amf0_create_strict_array();
                srs_amf0_object_property_set(keyframes, "times", times);
            }
        }
        
        free(data);
    }
    
    return ret;
}

int do_inject_flv(srs_flv_t ic, srs_flv_t oc, srs_amf0_t amf0_name, srs_amf0_t amf0_data, srs_amf0_t filepositions, int64_t metadata_end_offset)
{
    int ret = 0;
    
    // flv header
    char header[13];
    // packet data
    char type;
    u_int32_t timestamp = 0;
    char* data = NULL;
    int32_t size;
    
    // metadata
    srs_amf0_t fileposition = NULL;
    int amf0_name_size = 0;
    int i;
    
    // the metadata end offset, the next tag start offset.
    int64_t new_metadata_end_offset = 0;
    int offset_adjust = 0;
    
    // reset to write injected file
    srs_flv_lseek(ic, 0);
    
    if ((ret = srs_flv_read_header(ic, header)) != 0) {
        return ret;
    }
    
    if ((ret = srs_flv_write_header(oc, header)) != 0) {
        return ret;
    }
    
    // write metadata
    if (amf0_name != NULL && amf0_data != NULL) {
        amf0_name_size = srs_amf0_size(amf0_name);
        size = amf0_name_size + srs_amf0_size(amf0_data);

        // adjust all offset of keyframes.
        new_metadata_end_offset = srs_flv_tellg(oc) + srs_flv_size_tag(size);
        // the adjust is new offset sub the old offset of metadata end.
        offset_adjust = new_metadata_end_offset - metadata_end_offset;
        for (i = 0; i < srs_amf0_strict_array_property_count(filepositions); i++) {
            fileposition = srs_amf0_strict_array_property_at(filepositions, i);
            srs_amf0_set_number(fileposition, srs_amf0_to_number(fileposition) + offset_adjust);
        }
        
        data = (char*)malloc(size);
        memset(data, 0, size);
        if ((ret = srs_amf0_serialize(amf0_name, data, amf0_name_size)) != 0) {
            return ret;
        }
        if ((ret = srs_amf0_serialize(amf0_data, data + amf0_name_size, size - amf0_name_size)) != 0) {
            return ret;
        }
        if ((ret = srs_flv_write_tag(oc, SRS_RTMP_TYPE_SCRIPT, 0, data, size)) != 0) {
            return ret;
        }
        free(data);
    }
    
    srs_human_trace("build keyframe infos from flv");
    for (;;) {
        // tag header
        if ((ret = srs_flv_read_tag_header(ic, &type, &size, &timestamp)) != 0) {
            if (srs_flv_is_eof(ret)) {
                srs_human_trace("parse completed.");
                return 0;
            }
            srs_human_trace("flv get packet failed. ret=%d", ret);
            return ret;
        }
        
        if (size <= 0) {
            srs_human_trace("invalid size=%d", size);
            break;
        }
        
        // TODO: FIXME: mem leak when error.
        data = (char*)malloc(size);
        if ((ret = srs_flv_read_tag_data(ic, data, size)) != 0) {
            return ret;
        }
        
        // data tag
        if (type == SRS_RTMP_TYPE_SCRIPT) {
            continue;
        }
        
        // copy
        if ((ret = srs_flv_write_tag(oc, type, timestamp, data, size)) != 0) {
            return ret;
        }
        
        free(data);
    }
    
    return ret;
}
