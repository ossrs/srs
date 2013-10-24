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

#include <srs_core_complex_handshake.hpp>

#include <time.h>
#include <stdlib.h>

#include <srs_core_error.hpp>
#include <srs_core_log.hpp>

SrsComplexHandshake::SrsComplexHandshake()
{
}

SrsComplexHandshake::~SrsComplexHandshake()
{
}

/**
* 764bytes key结构
* 	random-data: (offset)bytes
* 	key-data: 128bytes
* 	random-data: (764-offset-128-4)bytes
* 	offset: 4bytes
*/
struct key_block
{
	// (offset)bytes
	char* random0;
	int random0_size;
	
	// 128bytes
	char key[128];
	
	// (764-offset-128-4)bytes
	char* random1;
	int random1_size;
	
	// 4bytes
	int32_t offset;
};
void srs_key_block_init(key_block* key)
{
}
void srs_key_block_free(key_block* key)
{
}

/**
* 764bytes digest结构
* 	offset: 4bytes
* 	random-data: (offset)bytes
* 	digest-data: 32bytes
* 	random-data: (764-4-offset-32)bytes
*/
struct digest_block
{
	// 4bytes
	int32_t offset;
	
	// (offset)bytes
	char* random0;
	int random0_size;
	
	// 32bytes
	char digest[32];
	
	// (764-4-offset-32)bytes
	char* random1;
	int random1_size;
};
void srs_digest_block_init(digest_block* digest)
{
}
void srs_digest_block_free(digest_block* digest)
{
}

/**
* c1s1 schema0
* 	time: 4bytes
* 	version: 4bytes
* 	key: 764bytes
* 	digest: 764bytes
* c1s1 schema1
* 	time: 4bytes
* 	version: 4bytes
* 	digest: 764bytes
* 	key: 764bytes
*/
struct c1s1
{
	enum schema_type {
		schema0 = 0, 
		schema1 = 1
	};
	union block {
		key_block key; 
		digest_block digest; 
	};
	
	// 4bytes
	int32_t time;
	// 4bytes
	int32_t version;
	// 764bytes
	// if schema0, use key
	// if schema1, use digest
	block block0;
	// 764bytes
	// if schema0, use digest
	// if schema1, use key
	block block1;
	
	// the logic schema
	schema_type schema;
	
	c1s1()
	{
		time = ::time(NULL);
		version = 0x00;
		
		schema = c1s1::schema0;
		srs_key_block_init(&block0.key);
		srs_digest_block_init(&block1.digest);
	}
	virtual ~c1s1()
	{
		if (schema == c1s1::schema0) {
			srs_key_block_free(&block0.key);
			srs_digest_block_free(&block1.digest);
		} else {
			srs_digest_block_free(&block0.digest);
			srs_key_block_free(&block1.key);
		}
	}
};

int SrsComplexHandshake::handshake(SrsSocket& skt, char* _c1)
{
	int ret = ERROR_SUCCESS;
	
	static bool _random_initialized = false;
	if (!_random_initialized) {
		srand(0);
		_random_initialized = true;
		srs_trace("srand initialized the random.");
	}
	
	c1s1 c1;
	
	return ret;
}

