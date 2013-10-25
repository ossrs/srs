/*
The MIT License (MIT)

Copyright (c) 2013 winlin

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

#ifndef SRS_CORE_CODEC_HPP
#define SRS_CORE_CODEC_HPP

/*
#include <srs_core_codec.hpp>
*/

#include <srs_core.hpp>

/**
* Annex E. The FLV File Format
* @doc update the README.cmd
*/
class SrsCodec
{
public:
	SrsCodec();
	virtual ~SrsCodec();
public:
	virtual bool video_is_sequence_header(int8_t* data, int size);
	virtual bool audio_is_sequence_header(int8_t* data, int size);
private:
	virtual bool video_is_h264(int8_t* data, int size);
	virtual bool audio_is_aac(int8_t* data, int size);
};

#endif