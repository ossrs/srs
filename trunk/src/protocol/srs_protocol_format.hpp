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

#ifndef SRS_PROTOCOL_FORMAT_HPP
#define SRS_PROTOCOL_FORMAT_HPP

/*
#include <srs_protocol_format.hpp>
*/

#include <srs_core.hpp>

class SrsFrame;
class SrsOnMetaDataPacket;
class SrsSharedPtrMessage;

/**
 * A codec format, including one or many stream, each stream identified by a frame.
 * For example, a typical RTMP stream format, consits of a video and audio frame.
 * Maybe some RTMP stream only has a audio stream, for instance, redio application.
 */
class SrsFormat
{
public:
    SrsFrame* audio;
    SrsFrame* video;
public:
    SrsFormat();
    virtual ~SrsFormat();
public:
    // Initialize the format.
    virtual int initialize();
    // Initialize the format from metadata, optional.
    virtual int on_metadata(SrsOnMetaDataPacket* meta);
    // When got a parsed audio packet.
    virtual int on_audio(SrsSharedPtrMessage* shared_audio);
    // When got a parsed video packet.
    virtual int on_video(SrsSharedPtrMessage* shared_video, bool is_sequence_header);
};

#endif

