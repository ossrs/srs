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

#include <srs_core_amf0.hpp>

#include <srs_core_log.hpp>
#include <srs_core_stream.hpp>

// AMF0 marker
#define RTMP_AMF0_Number 					0x00
#define RTMP_AMF0_Boolean 					0x01
#define RTMP_AMF0_String 					0x02
#define RTMP_AMF0_Object 					0x03
#define RTMP_AMF0_MovieClip 				0x04 // reserved, not supported
#define RTMP_AMF0_Null 						0x05
#define RTMP_AMF0_Undefined 				0x06
#define RTMP_AMF0_Reference 				0x07
#define RTMP_AMF0_EcmaArray 				0x08
#define RTMP_AMF0_ObjectEnd 				0x09
#define RTMP_AMF0_StrictArray 				0x0A
#define RTMP_AMF0_Date 						0x0B
#define RTMP_AMF0_LongString 				0x0C
#define RTMP_AMF0_UnSupported 				0x0D
#define RTMP_AMF0_RecordSet 				0x0E // reserved, not supported
#define RTMP_AMF0_XmlDocument 				0x0F
#define RTMP_AMF0_TypedObject 				0x10
// AVM+ object is the AMF3 object.
#define RTMP_AMF0_AVMplusObject 			0x11
// origin array whos data takes the same form as LengthValueBytes
#define RTMP_AMF0_OriginStrictArray 		0x20

std::string srs_amf0_read_string(SrsStream* stream)
{
	std::string str;
	
	// marker
	if (!stream->require(1)) {
		srs_warn("amf0 read string marker failed");
		return str;
	}
	
	char marker = stream->read_char();
	if (marker != RTMP_AMF0_String) {
		srs_warn("amf0 check string marker failed. marker=%#x, required=%#x", marker, RTMP_AMF0_String);
		return str;
	}
	srs_verbose("amf0 read string marker success");
	
	// len
	if (!stream->require(2)) {
		srs_warn("amf0 read string length failed");
		return str;
	}
	int16_t len = stream->read_2bytes();
	srs_verbose("amf0 read string length success. len=%d", len);
	
	// data
	if (!stream->require(len)) {
		srs_warn("amf0 read string data failed");
		return str;
	}
	str = stream->read_string(len);
	
	// support utf8-1 only
	// 1.3.1 Strings and UTF-8
	// UTF8-1 = %x00-7F
	for (int i = 0; i < len; i++) {
		char ch = *(str.data() + i);
		if ((ch & 0x80) != 0) {
			srs_warn("only support utf8-1, 0x00-0x7F, actual is %#x", (int)ch);
			return "";
		}
	}
	srs_verbose("amf0 read string data success. str=%s", str.c_str());
	
	return str;
}

double srs_amf0_read_number(SrsStream* stream)
{
	return 0;
}

