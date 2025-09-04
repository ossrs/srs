//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_http_stack_llhttpadapter.hpp>

// To avoid type mismatch, these functions are used to bridge the APIs.
// Bridge functions for internal callbacks that call the public API implementations
// These bridge the internal callback signature to the public API signature

extern int llhttp__on_message_begin(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_message_begin(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_message_begin((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_protocol(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_protocol(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_protocol((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_url(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_url(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_url((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_status(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_status(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_status((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_method(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_method(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_method((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_version(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_version(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_version((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_header_field(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_header_field(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_header_field((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_header_value(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_header_value(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_header_value((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_chunk_extension_name(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_chunk_extension_name(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_chunk_extension_name((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_chunk_extension_value(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_chunk_extension_value(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_chunk_extension_value((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_headers_complete(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_headers_complete(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_headers_complete((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_body(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_body(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_body((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_message_complete(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_message_complete(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_message_complete((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_url_complete(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_url_complete(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_url_complete((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_status_complete(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_status_complete(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_status_complete((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_method_complete(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_method_complete(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_method_complete((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_version_complete(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_version_complete(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_version_complete((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_header_field_complete(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_header_field_complete(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_header_field_complete((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_header_value_complete(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_header_value_complete(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_header_value_complete((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_chunk_header(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_chunk_header(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_chunk_header((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_chunk_complete(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_chunk_complete(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_chunk_complete((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_chunk_extension_name_complete(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_chunk_extension_name_complete(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_chunk_extension_name_complete((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_chunk_extension_value_complete(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_chunk_extension_value_complete(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_chunk_extension_value_complete((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_reset(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_reset(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_reset((llhttp_t *)s, (const char *)p, (const char *)endp);
}

extern int llhttp__on_protocol_complete(llhttp_t *s, const char *p, const char *endp);

int llhttp__on_protocol_complete(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return llhttp__on_protocol_complete((llhttp_t *)s, (const char *)p, (const char *)endp);
}

int llhttp__before_headers_complete(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return 0;
}

int llhttp__after_headers_complete(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return 0;
}

int llhttp__after_message_complete(llhttp__internal_t *s, const unsigned char *p, const unsigned char *endp)
{
    return 0;
}
