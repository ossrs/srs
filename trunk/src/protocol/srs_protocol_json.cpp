/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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

#include <srs_core.hpp>

/* vim: set et ts=3 sw=3 sts=3 ft=c:
 *
 * Copyright (C) 2012, 2013, 2014 James McLaughlin et al.  All rights reserved.
 * https://github.com/udp/json-parser
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _JSON_H
#define _JSON_H

#ifndef json_char
#define json_char char
#endif

#ifndef json_int_t
#ifndef _MSC_VER
#include <inttypes.h>
#define json_int_t int64_t
#else
#define json_int_t __int64
#endif
#endif

#include <stdlib.h>

#ifdef __cplusplus

#include <string.h>

extern "C"
{
    
#endif
    
    typedef struct
    {
        unsigned long max_memory;
        int settings;
        
        /* Custom allocator support (leave null to use malloc/free)
         */
        
        void * (* mem_alloc) (size_t, int zero, void * user_data);
        void (* mem_free) (void *, void * user_data);
        
        void * user_data;  /* will be passed to mem_alloc and mem_free */
        
        size_t value_extra;  /* how much extra space to allocate for values? */
        
    } json_settings;
    
#define json_enable_comments  0x01
    
    typedef enum
    {
        json_none,
        json_object,
        json_array,
        json_integer,
        json_double,
        json_string,
        json_boolean,
        json_null
        
    } json_type;
    
    extern const struct _json_value json_value_none;
    
    typedef struct _json_object_entry
    {
        json_char * name;
        unsigned int name_length;
        
        struct _json_value * value;
        
    } json_object_entry;
    
    typedef struct _json_value
    {
        struct _json_value * parent;
        
        json_type type;
        
        union
        {
            int boolean;
            json_int_t integer;
            double dbl;
            
            struct
            {
                unsigned int length;
                json_char * ptr; /* null terminated */
                
            } string;
            
            struct
            {
                unsigned int length;
                
                json_object_entry * values;
                
#if defined(__cplusplus) && __cplusplus >= 201103L
                decltype(values) begin () const
                {  return values;
                }
                decltype(values) end () const
                {  return values + length;
                }
#endif
                
            } object;
            
            struct
            {
                unsigned int length;
                struct _json_value ** values;
                
#if defined(__cplusplus) && __cplusplus >= 201103L
                decltype(values) begin () const
                {  return values;
                }
                decltype(values) end () const
                {  return values + length;
                }
#endif
                
            } array;
            
        } u;
        
        union
        {
            struct _json_value * next_alloc;
            void * object_mem;
            
        } _reserved;
        
#ifdef JSON_TRACK_SOURCE
        
        /* Location of the value in the source JSON
         */
        unsigned int line, col;
        
#endif
        
        
        /* Some C++ operator sugar */
        
#ifdef __cplusplus
        
    public:
        
        inline _json_value ()
        {  memset (this, 0, sizeof (_json_value));
        }
        
        inline const struct _json_value &operator [] (int index) const
        {
            if (type != json_array || index < 0
                || ((unsigned int) index) >= u.array.length)
            {
                return json_value_none;
            }
            
            return *u.array.values [index];
        }
        
        inline const struct _json_value &operator [] (const char * index) const
        {
            if (type != json_object)
                return json_value_none;
            
            for (unsigned int i = 0; i < u.object.length; ++ i)
                if (!strcmp (u.object.values [i].name, index))
                    return *u.object.values [i].value;
            
            return json_value_none;
        }
        
        inline operator const char * () const
        {
            switch (type)
            {
                case json_string:
                    return u.string.ptr;
                    
                default:
                    return "";
            };
        }
        
        inline operator json_int_t () const
        {
            switch (type)
            {
                case json_integer:
                    return u.integer;
                    
                case json_double:
                    return (json_int_t) u.dbl;
                    
                default:
                    return 0;
            };
        }
        
        inline operator bool () const
        {
            if (type != json_boolean)
                return false;
            
            return u.boolean != 0;
        }
        
        inline operator double () const
        {
            switch (type)
            {
                case json_integer:
                    return (double) u.integer;
                    
                case json_double:
                    return u.dbl;
                    
                default:
                    return 0;
            };
        }
        
#endif
        
    } json_value;
    
    json_value * json_parse (const json_char * json,
                             size_t length);
    
#define json_error_max 128
    json_value * json_parse_ex (json_settings * settings,
                                const json_char * json,
                                size_t length,
                                char * error);
    
    void json_value_free (json_value *);
    
    
    /* Not usually necessary, unless you used a custom mem_alloc and now want to
     * use a custom mem_free.
     */
    void json_value_free_ex (json_settings * settings,
                             json_value *);
    
    
#ifdef __cplusplus
} /* extern "C" */
#endif

/* vim: set et ts=3 sw=3 sts=3 ft=c:
 *
 * Copyright (C) 2012, 2013, 2014 James McLaughlin et al.  All rights reserved.
 * https://github.com/udp/json-parser
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

//#include "json.h"

#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

const struct _json_value json_value_none;

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

typedef unsigned int json_uchar;

static unsigned char hex_value (json_char c)
{
    if (isdigit(c))
        return c - '0';
    
    switch (c) {
        case 'a': case 'A': return 0x0A;
        case 'b': case 'B': return 0x0B;
        case 'c': case 'C': return 0x0C;
        case 'd': case 'D': return 0x0D;
        case 'e': case 'E': return 0x0E;
        case 'f': case 'F': return 0x0F;
        default: return 0xFF;
    }
}

typedef struct
{
    unsigned long used_memory;
    
    unsigned int uint_max;
    unsigned long ulong_max;
    
    json_settings settings;
    int first_pass;
    
    const json_char * ptr;
    unsigned int cur_line, cur_col;
    
} json_state;

static void * default_alloc (size_t size, int zero, void * user_data)
{
    return zero ? calloc (1, size) : malloc (size);
}

static void default_free (void * ptr, void * user_data)
{
    free (ptr);
}

static void * json_alloc (json_state * state, unsigned long size, int zero)
{
    if ((state->ulong_max - state->used_memory) < size)
        return 0;
    
    if (state->settings.max_memory
        && (state->used_memory += size) > state->settings.max_memory)
    {
        return 0;
    }
    
    return state->settings.mem_alloc (size, zero, state->settings.user_data);
}

static int new_value (json_state * state,
                      json_value ** top, json_value ** root, json_value ** alloc,
                      json_type type)
{
    json_value * value;
    int values_size;
    
    if (!state->first_pass)
    {
        value = *top = *alloc;
        *alloc = (*alloc)->_reserved.next_alloc;
        
        if (!*root)
            *root = value;
        
        switch (value->type)
        {
            case json_array:
                
                if (value->u.array.length == 0)
                    break;
                
                if (! (value->u.array.values = (json_value **) json_alloc
                       (state, value->u.array.length * sizeof (json_value *), 0)) )
                {
                    return 0;
                }
                
                value->u.array.length = 0;
                break;
                
            case json_object:
                
                if (value->u.object.length == 0)
                    break;
                
                values_size = sizeof (*value->u.object.values) * value->u.object.length;
                
                if (! (value->u.object.values = (json_object_entry *) json_alloc
                       (state, values_size + ((unsigned long) value->u.object.values), 0)) )
                {
                    return 0;
                }
                
                value->_reserved.object_mem = (*(char **) &value->u.object.values) + values_size;
                
                value->u.object.length = 0;
                break;
                
            case json_string:
                
                if (! (value->u.string.ptr = (json_char *) json_alloc
                       (state, (value->u.string.length + 1) * sizeof (json_char), 0)) )
                {
                    return 0;
                }
                
                value->u.string.length = 0;
                break;
                
            default:
                break;
        };
        
        return 1;
    }
    
    if (! (value = (json_value *) json_alloc
           (state, sizeof (json_value) + state->settings.value_extra, 1)))
    {
        return 0;
    }
    
    if (!*root)
        *root = value;
    
    value->type = type;
    value->parent = *top;
    
#ifdef JSON_TRACK_SOURCE
    value->line = state->cur_line;
    value->col = state->cur_col;
#endif
    
    if (*alloc)
        (*alloc)->_reserved.next_alloc = value;
    
    *alloc = *top = value;
    
    return 1;
}

#define whitespace \
case '\n': ++ state.cur_line;  state.cur_col = 0; \
case ' ': case '\t': case '\r'

#define string_add(b)  \
do { if (!state.first_pass) string [string_length] = b;  ++ string_length; } while (0);

#define line_and_col \
state.cur_line, state.cur_col

static const long
flag_next             = 1 << 0,
flag_reproc           = 1 << 1,
flag_need_comma       = 1 << 2,
flag_seek_value       = 1 << 3,
flag_escaped          = 1 << 4,
flag_string           = 1 << 5,
flag_need_colon       = 1 << 6,
flag_done             = 1 << 7,
flag_num_negative     = 1 << 8,
flag_num_zero         = 1 << 9,
flag_num_e            = 1 << 10,
flag_num_e_got_sign   = 1 << 11,
flag_num_e_negative   = 1 << 12,
flag_line_comment     = 1 << 13,
flag_block_comment    = 1 << 14;

json_value * json_parse_ex (json_settings * settings,
                            const json_char * json,
                            size_t length,
                            char * error_buf)
{
    json_char error [json_error_max];
    const json_char * end;
    json_value * top, * root, * alloc = 0;
    json_state state = { 0 };
    long flags;
    long num_digits = 0, num_e = 0;
    json_int_t num_fraction = 0;
    
    /* Skip UTF-8 BOM
     */
    if (length >= 3 && ((unsigned char) json [0]) == 0xEF
        && ((unsigned char) json [1]) == 0xBB
        && ((unsigned char) json [2]) == 0xBF)
    {
        json += 3;
        length -= 3;
    }
    
    error[0] = '\0';
    end = (json + length);
    
    memcpy (&state.settings, settings, sizeof (json_settings));
    
    if (!state.settings.mem_alloc)
        state.settings.mem_alloc = default_alloc;
    
    if (!state.settings.mem_free)
        state.settings.mem_free = default_free;
    
    memset (&state.uint_max, 0xFF, sizeof (state.uint_max));
    memset (&state.ulong_max, 0xFF, sizeof (state.ulong_max));
    
    state.uint_max -= 8; /* limit of how much can be added before next check */
    state.ulong_max -= 8;
    
    for (state.first_pass = 1; state.first_pass >= 0; -- state.first_pass)
    {
        json_uchar uchar;
        unsigned char uc_b1, uc_b2, uc_b3, uc_b4;
        json_char * string = 0;
        unsigned int string_length = 0;
        
        top = root = 0;
        flags = flag_seek_value;
        
        state.cur_line = 1;
        
        for (state.ptr = json ;; ++ state.ptr)
        {
            json_char b = (state.ptr == end ? 0 : *state.ptr);
            
            if (flags & flag_string)
            {
                if (!b)
                {  sprintf (error, "Unexpected EOF in string (at %d:%d)", line_and_col);
                    goto e_failed;
                }
                
                if (string_length > state.uint_max)
                    goto e_overflow;
                
                if (flags & flag_escaped)
                {
                    flags &= ~ flag_escaped;
                    
                    switch (b)
                    {
                        case 'b':  string_add ('\b');  break;
                        case 'f':  string_add ('\f');  break;
                        case 'n':  string_add ('\n');  break;
                        case 'r':  string_add ('\r');  break;
                        case 't':  string_add ('\t');  break;
                        case 'u':
                            
                            if (end - state.ptr < 4 ||
                                (uc_b1 = hex_value (*++ state.ptr)) == 0xFF ||
                                (uc_b2 = hex_value (*++ state.ptr)) == 0xFF ||
                                (uc_b3 = hex_value (*++ state.ptr)) == 0xFF ||
                                (uc_b4 = hex_value (*++ state.ptr)) == 0xFF)
                            {
                                sprintf (error, "Invalid character value `%c` (at %d:%d)", b, line_and_col);
                                goto e_failed;
                            }
                            
                            uc_b1 = (uc_b1 << 4) | uc_b2;
                            uc_b2 = (uc_b3 << 4) | uc_b4;
                            uchar = (uc_b1 << 8) | uc_b2;
                            
                            if ((uchar & 0xF800) == 0xD800) {
                                json_uchar uchar2;
                                
                                if (end - state.ptr < 6 || (*++ state.ptr) != '\\' || (*++ state.ptr) != 'u' ||
                                    (uc_b1 = hex_value (*++ state.ptr)) == 0xFF ||
                                    (uc_b2 = hex_value (*++ state.ptr)) == 0xFF ||
                                    (uc_b3 = hex_value (*++ state.ptr)) == 0xFF ||
                                    (uc_b4 = hex_value (*++ state.ptr)) == 0xFF)
                                {
                                    sprintf (error, "Invalid character value `%c` (at %d:%d)", b, line_and_col);
                                    goto e_failed;
                                }
                                
                                uc_b1 = (uc_b1 << 4) | uc_b2;
                                uc_b2 = (uc_b3 << 4) | uc_b4;
                                uchar2 = (uc_b1 << 8) | uc_b2;
                                
                                uchar = 0x010000 | ((uchar & 0x3FF) << 10) | (uchar2 & 0x3FF);
                            }
                            
                            if (sizeof (json_char) >= sizeof (json_uchar) || (uchar <= 0x7F))
                            {
                                string_add ((json_char) uchar);
                                break;
                            }
                            
                            if (uchar <= 0x7FF)
                            {
                                if (state.first_pass)
                                    string_length += 2;
                                else
                                {  string [string_length ++] = 0xC0 | (uchar >> 6);
                                    string [string_length ++] = 0x80 | (uchar & 0x3F);
                                }
                                
                                break;
                            }
                            
                            if (uchar <= 0xFFFF) {
                                if (state.first_pass)
                                    string_length += 3;
                                else
                                {  string [string_length ++] = 0xE0 | (uchar >> 12);
                                    string [string_length ++] = 0x80 | ((uchar >> 6) & 0x3F);
                                    string [string_length ++] = 0x80 | (uchar & 0x3F);
                                }
                                
                                break;
                            }
                            
                            if (state.first_pass)
                                string_length += 4;
                            else
                            {  string [string_length ++] = 0xF0 | (uchar >> 18);
                                string [string_length ++] = 0x80 | ((uchar >> 12) & 0x3F);
                                string [string_length ++] = 0x80 | ((uchar >> 6) & 0x3F);
                                string [string_length ++] = 0x80 | (uchar & 0x3F);
                            }
                            
                            break;
                            
                        default:
                            string_add (b);
                    };
                    
                    continue;
                }
                
                if (b == '\\')
                {
                    flags |= flag_escaped;
                    continue;
                }
                
                if (b == '"')
                {
                    if (!state.first_pass)
                        string [string_length] = 0;
                    
                    flags &= ~ flag_string;
                    string = 0;
                    
                    switch (top->type)
                    {
                        case json_string:
                            
                            top->u.string.length = string_length;
                            flags |= flag_next;
                            
                            break;
                            
                        case json_object:
                            
                            if (state.first_pass)
                                (*(json_char **) &top->u.object.values) += string_length + 1;
                            else
                            {
                                top->u.object.values [top->u.object.length].name
                                = (json_char *) top->_reserved.object_mem;
                                
                                top->u.object.values [top->u.object.length].name_length
                                = string_length;
                                
                                (*(json_char **) &top->_reserved.object_mem) += string_length + 1;
                            }
                            
                            flags |= flag_seek_value | flag_need_colon;
                            continue;
                            
                        default:
                            break;
                    };
                }
                else
                {
                    string_add (b);
                    continue;
                }
            }
            
            if (state.settings.settings & json_enable_comments)
            {
                if (flags & (flag_line_comment | flag_block_comment))
                {
                    if (flags & flag_line_comment)
                    {
                        if (b == '\r' || b == '\n' || !b)
                        {
                            flags &= ~ flag_line_comment;
                            -- state.ptr;  /* so null can be reproc'd */
                        }
                        
                        continue;
                    }
                    
                    if (flags & flag_block_comment)
                    {
                        if (!b)
                        {  sprintf (error, "%d:%d: Unexpected EOF in block comment", line_and_col);
                            goto e_failed;
                        }
                        
                        if (b == '*' && state.ptr < (end - 1) && state.ptr [1] == '/')
                        {
                            flags &= ~ flag_block_comment;
                            ++ state.ptr;  /* skip closing sequence */
                        }
                        
                        continue;
                    }
                }
                else if (b == '/')
                {
                    if (! (flags & (flag_seek_value | flag_done)) && top->type != json_object)
                    {  sprintf (error, "%d:%d: Comment not allowed here", line_and_col);
                        goto e_failed;
                    }
                    
                    if (++ state.ptr == end)
                    {  sprintf (error, "%d:%d: EOF unexpected", line_and_col);
                        goto e_failed;
                    }
                    
                    switch (b = *state.ptr)
                    {
                        case '/':
                            flags |= flag_line_comment;
                            continue;
                            
                        case '*':
                            flags |= flag_block_comment;
                            continue;
                            
                        default:
                            sprintf (error, "%d:%d: Unexpected `%c` in comment opening sequence", line_and_col, b);
                            goto e_failed;
                    };
                }
            }
            
            if (flags & flag_done)
            {
                if (!b)
                    break;
                
                switch (b)
                {
                    whitespace:
                        continue;
                        
                    default:
                        
                        sprintf (error, "%d:%d: Trailing garbage: `%c`",
                                 state.cur_line, state.cur_col, b);
                        
                        goto e_failed;
                };
            }
            
            if (flags & flag_seek_value)
            {
                switch (b)
                {
                    whitespace:
                        continue;
                        
                    case ']':
                        
                        if (top && top->type == json_array)
                            flags = (flags & ~ (flag_need_comma | flag_seek_value)) | flag_next;
                        else
                        {  sprintf (error, "%d:%d: Unexpected ]", line_and_col);
                            goto e_failed;
                        }
                        
                        break;
                        
                    default:
                        
                        if (flags & flag_need_comma)
                        {
                            if (b == ',')
                            {  flags &= ~ flag_need_comma;
                                continue;
                            }
                            else
                            {
                                sprintf (error, "%d:%d: Expected , before %c",
                                         state.cur_line, state.cur_col, b);
                                
                                goto e_failed;
                            }
                        }
                        
                        if (flags & flag_need_colon)
                        {
                            if (b == ':')
                            {  flags &= ~ flag_need_colon;
                                continue;
                            }
                            else
                            {
                                sprintf (error, "%d:%d: Expected : before %c",
                                         state.cur_line, state.cur_col, b);
                                
                                goto e_failed;
                            }
                        }
                        
                        flags &= ~ flag_seek_value;
                        
                        switch (b)
                    {
                        case '{':
                            
                            if (!new_value (&state, &top, &root, &alloc, json_object))
                                goto e_alloc_failure;
                            
                            continue;
                            
                        case '[':
                            
                            if (!new_value (&state, &top, &root, &alloc, json_array))
                                goto e_alloc_failure;
                            
                            flags |= flag_seek_value;
                            continue;
                            
                        case '"':
                            
                            if (!new_value (&state, &top, &root, &alloc, json_string))
                                goto e_alloc_failure;
                            
                            flags |= flag_string;
                            
                            string = top->u.string.ptr;
                            string_length = 0;
                            
                            continue;
                            
                        case 't':
                            
                            if ((end - state.ptr) < 3 || *(++ state.ptr) != 'r' ||
                                *(++ state.ptr) != 'u' || *(++ state.ptr) != 'e')
                            {
                                goto e_unknown_value;
                            }
                            
                            if (!new_value (&state, &top, &root, &alloc, json_boolean))
                                goto e_alloc_failure;
                            
                            top->u.boolean = 1;
                            
                            flags |= flag_next;
                            break;
                            
                        case 'f':
                            
                            if ((end - state.ptr) < 4 || *(++ state.ptr) != 'a' ||
                                *(++ state.ptr) != 'l' || *(++ state.ptr) != 's' ||
                                *(++ state.ptr) != 'e')
                            {
                                goto e_unknown_value;
                            }
                            
                            if (!new_value (&state, &top, &root, &alloc, json_boolean))
                                goto e_alloc_failure;
                            
                            flags |= flag_next;
                            break;
                            
                        case 'n':
                            
                            if ((end - state.ptr) < 3 || *(++ state.ptr) != 'u' ||
                                *(++ state.ptr) != 'l' || *(++ state.ptr) != 'l')
                            {
                                goto e_unknown_value;
                            }
                            
                            if (!new_value (&state, &top, &root, &alloc, json_null))
                                goto e_alloc_failure;
                            
                            flags |= flag_next;
                            break;
                            
                        default:
                            
                            if (isdigit (b) || b == '-')
                            {
                                if (!new_value (&state, &top, &root, &alloc, json_integer))
                                    goto e_alloc_failure;
                                
                                if (!state.first_pass)
                                {
                                    while (isdigit (b) || b == '+' || b == '-'
                                           || b == 'e' || b == 'E' || b == '.')
                                    {
                                        if ( (++ state.ptr) == end)
                                        {
                                            b = 0;
                                            break;
                                        }
                                        
                                        b = *state.ptr;
                                    }
                                    
                                    flags |= flag_next | flag_reproc;
                                    break;
                                }
                                
                                flags &= ~ (flag_num_negative | flag_num_e |
                                            flag_num_e_got_sign | flag_num_e_negative |
                                            flag_num_zero);
                                
                                num_digits = 0;
                                num_fraction = 0;
                                num_e = 0;
                                
                                if (b != '-')
                                {
                                    flags |= flag_reproc;
                                    break;
                                }
                                
                                flags |= flag_num_negative;
                                continue;
                            }
                            else
                            {  sprintf (error, "%d:%d: Unexpected %c when seeking value", line_and_col, b);
                                goto e_failed;
                            }
                    };
                };
            }
            else
            {
                switch (top->type)
                {
                    case json_object:
                        
                        switch (b)
                    {
                        whitespace:
                            continue;
                            
                        case '"':
                            
                            if (flags & flag_need_comma)
                            {  sprintf (error, "%d:%d: Expected , before \"", line_and_col);
                                goto e_failed;
                            }
                            
                            flags |= flag_string;
                            
                            string = (json_char *) top->_reserved.object_mem;
                            string_length = 0;
                            
                            break;
                            
                        case '}':
                            
                            flags = (flags & ~ flag_need_comma) | flag_next;
                            break;
                            
                        case ',':
                            
                            if (flags & flag_need_comma)
                            {
                                flags &= ~ flag_need_comma;
                                break;
                            }
                            
                        default:
                            sprintf (error, "%d:%d: Unexpected `%c` in object", line_and_col, b);
                            goto e_failed;
                    };
                        
                        break;
                        
                    case json_integer:
                    case json_double:
                        
                        if (isdigit (b))
                        {
                            ++ num_digits;
                            
                            if (top->type == json_integer || flags & flag_num_e)
                            {
                                if (! (flags & flag_num_e))
                                {
                                    if (flags & flag_num_zero)
                                    {  sprintf (error, "%d:%d: Unexpected `0` before `%c`", line_and_col, b);
                                        goto e_failed;
                                    }
                                    
                                    if (num_digits == 1 && b == '0')
                                        flags |= flag_num_zero;
                                }
                                else
                                {
                                    flags |= flag_num_e_got_sign;
                                    num_e = (num_e * 10) + (b - '0');
                                    continue;
                                }
                                
                                top->u.integer = (top->u.integer * 10) + (b - '0');
                                continue;
                            }
                            
                            num_fraction = (num_fraction * 10) + (b - '0');
                            continue;
                        }
                        
                        if (b == '+' || b == '-')
                        {
                            if ( (flags & flag_num_e) && !(flags & flag_num_e_got_sign))
                            {
                                flags |= flag_num_e_got_sign;
                                
                                if (b == '-')
                                    flags |= flag_num_e_negative;
                                
                                continue;
                            }
                        }
                        else if (b == '.' && top->type == json_integer)
                        {
                            if (!num_digits)
                            {  sprintf (error, "%d:%d: Expected digit before `.`", line_and_col);
                                goto e_failed;
                            }
                            
                            top->type = json_double;
                            top->u.dbl = (double) top->u.integer;
                            
                            num_digits = 0;
                            continue;
                        }
                        
                        if (! (flags & flag_num_e))
                        {
                            if (top->type == json_double)
                            {
                                if (!num_digits)
                                {  sprintf (error, "%d:%d: Expected digit after `.`", line_and_col);
                                    goto e_failed;
                                }
                                
                                top->u.dbl += ((double) num_fraction) / (pow (10.0, (double) num_digits));
                            }
                            
                            if (b == 'e' || b == 'E')
                            {
                                flags |= flag_num_e;
                                
                                if (top->type == json_integer)
                                {
                                    top->type = json_double;
                                    top->u.dbl = (double) top->u.integer;
                                }
                                
                                num_digits = 0;
                                flags &= ~ flag_num_zero;
                                
                                continue;
                            }
                        }
                        else
                        {
                            if (!num_digits)
                            {  sprintf (error, "%d:%d: Expected digit after `e`", line_and_col);
                                goto e_failed;
                            }
                            
                            top->u.dbl *= pow (10.0, (double)
                                               (flags & flag_num_e_negative ? - num_e : num_e));
                        }
                        
                        if (flags & flag_num_negative)
                        {
                            if (top->type == json_integer)
                                top->u.integer = - top->u.integer;
                            else
                                top->u.dbl = - top->u.dbl;
                        }
                        
                        flags |= flag_next | flag_reproc;
                        break;
                        
                    default:
                        break;
                };
            }
            
            if (flags & flag_reproc)
            {
                flags &= ~ flag_reproc;
                -- state.ptr;
            }
            
            if (flags & flag_next)
            {
                flags = (flags & ~ flag_next) | flag_need_comma;
                
                if (!top->parent)
                {
                    /* root value done */
                    
                    flags |= flag_done;
                    continue;
                }
                
                if (top->parent->type == json_array)
                    flags |= flag_seek_value;
                
                if (!state.first_pass)
                {
                    json_value * parent = top->parent;
                    
                    switch (parent->type)
                    {
                        case json_object:
                            
                            parent->u.object.values
                            [parent->u.object.length].value = top;
                            
                            break;
                            
                        case json_array:
                            
                            parent->u.array.values
                            [parent->u.array.length] = top;
                            
                            break;
                            
                        default:
                            break;
                    };
                }
                
                if ( (++ top->parent->u.array.length) > state.uint_max)
                    goto e_overflow;
                
                top = top->parent;
                
                continue;
            }
        }
        
        alloc = root;
    }
    
    return root;
    
e_unknown_value:
    
    sprintf (error, "%d:%d: Unknown value", line_and_col);
    goto e_failed;
    
e_alloc_failure:
    
    strcpy (error, "Memory allocation failure");
    goto e_failed;
    
e_overflow:
    
    sprintf (error, "%d:%d: Too long (caught overflow)", line_and_col);
    goto e_failed;
    
e_failed:
    
    if (error_buf)
    {
        if (*error)
            strcpy (error_buf, error);
        else
            strcpy (error_buf, "Unknown error");
    }
    
    if (state.first_pass)
        alloc = root;
    
    while (alloc)
    {
        top = alloc->_reserved.next_alloc;
        state.settings.mem_free (alloc, state.settings.user_data);
        alloc = top;
    }
    
    if (!state.first_pass)
        json_value_free_ex (&state.settings, root);
    
    return 0;
}

json_value * json_parse (const json_char * json, size_t length)
{
    json_settings settings = { 0 };
    return json_parse_ex (&settings, json, length, 0);
}

void json_value_free_ex (json_settings * settings, json_value * value)
{
    json_value * cur_value;
    
    if (!value)
        return;
    
    value->parent = 0;
    
    while (value)
    {
        switch (value->type)
        {
            case json_array:
                
                if (!value->u.array.length)
                {
                    settings->mem_free (value->u.array.values, settings->user_data);
                    break;
                }
                
                value = value->u.array.values [-- value->u.array.length];
                continue;
                
            case json_object:
                
                if (!value->u.object.length)
                {
                    settings->mem_free (value->u.object.values, settings->user_data);
                    break;
                }
                
                value = value->u.object.values [-- value->u.object.length].value;
                continue;
                
            case json_string:
                
                settings->mem_free (value->u.string.ptr, settings->user_data);
                break;
                
            default:
                break;
        };
        
        cur_value = value;
        value = value->parent;
        settings->mem_free (cur_value, settings->user_data);
    }
}

void json_value_free (json_value * value)
{
    json_settings settings = { 0 };
    settings.mem_free = default_free;
    json_value_free_ex (&settings, value);
}

#endif

/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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

#include <srs_protocol_json.hpp>

#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_kernel_utility.hpp>

/* json encode
 cout<< SRS_JOBJECT_START
         << SRS_JFIELD_STR("name", "srs") << SRS_JFIELD_CONT
         << SRS_JFIELD_ORG("version", 100) << SRS_JFIELD_CONT
         << SRS_JFIELD_NAME("features") << SRS_JOBJECT_START
             << SRS_JFIELD_STR("rtmp", "released") << SRS_JFIELD_CONT
             << SRS_JFIELD_STR("hls", "released") << SRS_JFIELD_CONT
             << SRS_JFIELD_STR("dash", "plan")
         << SRS_JOBJECT_END << SRS_JFIELD_CONT
         << SRS_JFIELD_STR("author", "srs team")
     << SRS_JOBJECT_END
 it's:
 cont<< "{"
         << "name:" << "srs" << ","
         << "version:" << 100 << ","
         << "features:" << "{"
             << "rtmp:" << "released" << ","
             << "hls:" << "released" << ","
             << "dash:" << "plan"
         << "}" << ","
         << "author:" << "srs team"
     << "}"
 that is:
 """
 {
     "name": "srs",
     "version": 100,
     "features": {
         "rtmp": "released",
         "hls": "released",
         "dash": "plan"
     },
     "author": "srs team"
 }
 """
 */
#define SRS_JOBJECT_START "{"
#define SRS_JFIELD_NAME(k) "\"" << k << "\":"
#define SRS_JFIELD_OBJ(k) SRS_JFIELD_NAME(k) << SRS_JOBJECT_START
#define SRS_JFIELD_STR(k, v) SRS_JFIELD_NAME(k) << "\"" << v << "\""
#define SRS_JFIELD_ORG(k, v) SRS_JFIELD_NAME(k) << std::dec << v
#define SRS_JFIELD_BOOL(k, v) SRS_JFIELD_ORG(k, (v? "true":"false"))
#define SRS_JFIELD_NULL(k) SRS_JFIELD_NAME(k) << "null"
#define SRS_JFIELD_ERROR(ret) "\"" << "code" << "\":" << ret
#define SRS_JFIELD_CONT ","
#define SRS_JOBJECT_END "}"
#define SRS_JARRAY_START "["
#define SRS_JARRAY_END "]"

// Json marker
#define SRS_JSON_Boolean                   0x01
#define SRS_JSON_String                    0x02
#define SRS_JSON_Object                    0x03
#define SRS_JSON_Integer                   0x04
#define SRS_JSON_Number                    0x05
#define SRS_JSON_Null                      0x06
#define SRS_JSON_Array                     0x07

class SrsJsonString : public SrsJsonAny
{
public:
    std::string value;
    
    SrsJsonString(const char* v)
    {
        marker = SRS_JSON_String;
        if (v) {
            value = v;
        }
    }
    SrsJsonString(const char* v, int s)
    {
        marker = SRS_JSON_String;
        if (v) {
            value.append(v, s);
        }
    }
    virtual ~SrsJsonString()
    {
    }
};

class SrsJsonBoolean : public SrsJsonAny
{
public:
    bool value;
    
    SrsJsonBoolean(bool v)
    {
        marker = SRS_JSON_Boolean;
        value = v;
    }
    virtual ~SrsJsonBoolean()
    {
    }
};

class SrsJsonInteger : public SrsJsonAny
{
public:
    int64_t value;
    
    SrsJsonInteger(int64_t v)
    {
        marker = SRS_JSON_Integer;
        value = v;
    }
    virtual ~SrsJsonInteger()
    {
    }
};

class SrsJsonNumber : public SrsJsonAny
{
public:
    double value;
    
    SrsJsonNumber(double v)
    {
        marker = SRS_JSON_Number;
        value = v;
    }
    virtual ~SrsJsonNumber()
    {
    }
};

class SrsJsonNull : public SrsJsonAny
{
public:
    SrsJsonNull() {
        marker = SRS_JSON_Null;
    }
    virtual ~SrsJsonNull() {
    }
};

SrsJsonAny::SrsJsonAny()
{
    marker = 0;
}

SrsJsonAny::~SrsJsonAny()
{
}

bool SrsJsonAny::is_string()
{
    return marker == SRS_JSON_String;
}

bool SrsJsonAny::is_boolean()
{
    return marker == SRS_JSON_Boolean;
}

bool SrsJsonAny::is_number()
{
    return marker == SRS_JSON_Number;
}

bool SrsJsonAny::is_integer()
{
    return marker == SRS_JSON_Integer;
}

bool SrsJsonAny::is_object()
{
    return marker == SRS_JSON_Object;
}

bool SrsJsonAny::is_array()
{
    return marker == SRS_JSON_Array;
}

bool SrsJsonAny::is_null()
{
    return marker == SRS_JSON_Null;
}

string SrsJsonAny::to_str()
{
    SrsJsonString* p = dynamic_cast<SrsJsonString*>(this);
    srs_assert(p != NULL);
    return p->value;
}

bool SrsJsonAny::to_boolean()
{
    SrsJsonBoolean* p = dynamic_cast<SrsJsonBoolean*>(this);
    srs_assert(p != NULL);
    return p->value;
}

int64_t SrsJsonAny::to_integer()
{
    SrsJsonInteger* p = dynamic_cast<SrsJsonInteger*>(this);
    srs_assert(p != NULL);
    return p->value;
}

double SrsJsonAny::to_number()
{
    SrsJsonNumber* p = dynamic_cast<SrsJsonNumber*>(this);
    srs_assert(p != NULL);
    return p->value;
}

SrsJsonObject* SrsJsonAny::to_object()
{
    SrsJsonObject* p = dynamic_cast<SrsJsonObject*>(this);
    srs_assert(p != NULL);
    return p;
}

SrsJsonArray* SrsJsonAny::to_array()
{
    SrsJsonArray* p = dynamic_cast<SrsJsonArray*>(this);
    srs_assert(p != NULL);
    return p;
}

string SrsJsonAny::dumps()
{
    switch (marker) {
        case SRS_JSON_String: {
            return "\"" + to_str() + "\"";
        }
        case SRS_JSON_Boolean: {
            return to_boolean()? "true":"false";
        }
        case SRS_JSON_Integer: {
            return srs_int2str(to_integer());
        }
        case SRS_JSON_Number: {
            // len(max int64_t) is 20, plus one "+-."
            char tmp[22];
            snprintf(tmp, 22, "%.6f", to_number());
            return tmp;
        }
        case SRS_JSON_Null: {
            return "null";
        }
        case SRS_JSON_Object: {
            SrsJsonObject* obj = to_object();
            return obj->dumps();
        }
        case SRS_JSON_Array: {
            SrsJsonArray* arr = to_array();
            return arr->dumps();
        }
        default: {
            break;
        }
    }
    
    return "null";
}

SrsAmf0Any* SrsJsonAny::to_amf0()
{
    switch (marker) {
        case SRS_JSON_String: {
            return SrsAmf0Any::str(to_str().c_str());
        }
        case SRS_JSON_Boolean: {
            return SrsAmf0Any::boolean(to_boolean());
        }
        case SRS_JSON_Integer: {
            return SrsAmf0Any::number(to_integer());
        }
        case SRS_JSON_Number: {
            return SrsAmf0Any::number(to_number());
        }
        case SRS_JSON_Null: {
            return SrsAmf0Any::null();
        }
        case SRS_JSON_Object: {
            // json object must override this method.
            srs_assert(false);
        }
        case SRS_JSON_Array: {
            // json array must override this method.
            srs_assert(false);
        }
        default: {
            break;
        }
    }
    
    return SrsAmf0Any::null();
}

SrsJsonAny* SrsJsonAny::str(const char* value)
{
    return new SrsJsonString(value);
}

SrsJsonAny* SrsJsonAny::str(const char* value, int length)
{
    return new SrsJsonString(value, length);
}

SrsJsonAny* SrsJsonAny::boolean(bool value)
{
    return new SrsJsonBoolean(value);
}

SrsJsonAny* SrsJsonAny::integer(int64_t value)
{
    return new SrsJsonInteger(value);
}

SrsJsonAny* SrsJsonAny::number(double value)
{
    return new SrsJsonNumber(value);
}

SrsJsonAny* SrsJsonAny::null()
{
    return new SrsJsonNull();
}

SrsJsonObject* SrsJsonAny::object()
{
    return new SrsJsonObject();
}

SrsJsonArray* SrsJsonAny::array()
{
    return new SrsJsonArray();
}

SrsJsonAny* srs_json_parse_tree(json_value* node)
{
    if (!node) {
        return NULL;
    }
    
    switch (node->type) {
        case json_null:
            return SrsJsonAny::null();
        case json_string:
            return SrsJsonAny::str(node->u.string.ptr, node->u.string.length);
        case json_integer:
            return SrsJsonAny::integer(node->u.integer);
        case json_double:
            return SrsJsonAny::number(node->u.dbl);
        case json_boolean:
            return SrsJsonAny::boolean(node->u.boolean != 0);
        case json_object: {
            SrsJsonObject* obj = SrsJsonAny::object();
            for (int i = 0; i < node->u.object.length; i++) {
                json_object_entry& entry = node->u.object.values[i];
                SrsJsonAny* value = srs_json_parse_tree(entry.value);
                
                if (!value) {
                    srs_freep(obj);
                    return NULL;
                }
                
                obj->set(string(entry.name, entry.name_length), value);
            }
            return obj;
        }
        case json_array: {
            SrsJsonArray* arr = SrsJsonAny::array();
            for (int i = 0; i < node->u.array.length; i++) {
                json_value* p = node->u.array.values[i];
                SrsJsonAny* value = srs_json_parse_tree(p);
                
                if (!value) {
                    srs_freep(arr);
                    return NULL;
                }
                
                arr->add(value);
            }
            return arr;
        }
        default:
            return NULL;
    }
    
    return NULL;
}

SrsJsonAny* SrsJsonAny::loads(const string& str)
{
    if (str.empty()) {
        return NULL;
    }
    
    json_value* j = json_parse(str.data(), str.length());
    SrsJsonAny* sj = srs_json_parse_tree(j);
    json_value_free(j);
    
    return sj;
}

SrsJsonObject::SrsJsonObject()
{
    marker = SRS_JSON_Object;
}

SrsJsonObject::~SrsJsonObject()
{
    std::vector<SrsJsonObjectPropertyType>::iterator it;
    for (it = properties.begin(); it != properties.end(); ++it) {
        SrsJsonObjectPropertyType item = *it;
        SrsJsonAny* obj = item.second;
        srs_freep(obj);
    }
    properties.clear();
}

int SrsJsonObject::count()
{
    return (int)properties.size();
}

string SrsJsonObject::key_at(int index)
{
    srs_assert(index < count());
    SrsJsonObjectPropertyType& elem = properties[index];
    return elem.first;
}

SrsJsonAny* SrsJsonObject::value_at(int index)
{
    srs_assert(index < count());
    SrsJsonObjectPropertyType& elem = properties[index];
    return elem.second;
}

string SrsJsonObject::dumps()
{
    stringstream ss;
    
    ss << SRS_JOBJECT_START;
    
    for (int i = 0; i < (int)properties.size(); i++) {
        std::string name = this->key_at(i);
        SrsJsonAny* any = this->value_at(i);
        
        ss << SRS_JFIELD_NAME(name) << any->dumps();
        if (i < (int)properties.size() - 1) {
            ss << SRS_JFIELD_CONT;
        }
    }
    
    ss << SRS_JOBJECT_END;
    
    return ss.str();
}

SrsAmf0Any* SrsJsonObject::to_amf0()
{
    SrsAmf0Object* obj = SrsAmf0Any::object();
    
    for (int i = 0; i < (int)properties.size(); i++) {
        std::string name = this->key_at(i);
        SrsJsonAny* any = this->value_at(i);
        
        obj->set(name, any->to_amf0());
    }
    
    return obj;
}

void SrsJsonObject::set(string key, SrsJsonAny* value)
{
    if (!value) {
        srs_warn("add a NULL propertity %s", key.c_str());
        return;
    }
    
    std::vector<SrsJsonObjectPropertyType>::iterator it;
    
    for (it = properties.begin(); it != properties.end(); ++it) {
        SrsJsonObjectPropertyType& elem = *it;
        std::string name = elem.first;
        SrsJsonAny* any = elem.second;
        
        if (key == name) {
            srs_freep(any);
            properties.erase(it);
            break;
        }
    }
    
    properties.push_back(std::make_pair(key, value));
}

SrsJsonAny* SrsJsonObject::get_property(string name)
{
    std::vector<SrsJsonObjectPropertyType>::iterator it;
    
    for (it = properties.begin(); it != properties.end(); ++it) {
        SrsJsonObjectPropertyType& elem = *it;
        std::string key = elem.first;
        SrsJsonAny* any = elem.second;
        if (key == name) {
            return any;
        }
    }
    
    return NULL;
}

SrsJsonAny* SrsJsonObject::ensure_property_string(string name)
{
    SrsJsonAny* prop = get_property(name);
    
    if (!prop) {
        return NULL;
    }
    
    if (!prop->is_string()) {
        return NULL;
    }
    
    return prop;
}

SrsJsonAny* SrsJsonObject::ensure_property_integer(string name)
{
    SrsJsonAny* prop = get_property(name);
    
    if (!prop) {
        return NULL;
    }
    
    if (!prop->is_integer()) {
        return NULL;
    }
    
    return prop;
}

SrsJsonAny* SrsJsonObject::ensure_property_number(string name)
{
    SrsJsonAny* prop = get_property(name);
    
    if (!prop) {
        return NULL;
    }
    
    if (!prop->is_number()) {
        return NULL;
    }
    
    return prop;
}

SrsJsonAny* SrsJsonObject::ensure_property_boolean(string name)
{
    SrsJsonAny* prop = get_property(name);
    
    if (!prop) {
        return NULL;
    }
    
    if (!prop->is_boolean()) {
        return NULL;
    }
    
    return prop;
}

SrsJsonAny* SrsJsonObject::ensure_property_object(string name)
{
    SrsJsonAny* prop = get_property(name);
    
    if (!prop) {
        return NULL;
    }
    
    if (!prop->is_object()) {
        return NULL;
    }
    
    return prop;
}

SrsJsonAny* SrsJsonObject::ensure_property_array(string name)
{
    SrsJsonAny* prop = get_property(name);
    
    if (!prop) {
        return NULL;
    }
    
    if (!prop->is_array()) {
        return NULL;
    }
    
    return prop;
}

SrsJsonArray::SrsJsonArray()
{
    marker = SRS_JSON_Array;
}

SrsJsonArray::~SrsJsonArray()
{
    std::vector<SrsJsonAny*>::iterator it;
    for (it = properties.begin(); it != properties.end(); ++it) {
        SrsJsonAny* item = *it;
        srs_freep(item);
    }
    properties.clear();
}

int SrsJsonArray::count()
{
    return (int)properties.size();
}

SrsJsonAny* SrsJsonArray::at(int index)
{
    srs_assert(index < count());
    SrsJsonAny* elem = properties[index];
    return elem;
}

void SrsJsonArray::add(SrsJsonAny* value)
{
    properties.push_back(value);
}

void SrsJsonArray::append(SrsJsonAny* value)
{
    add(value);
}

string SrsJsonArray::dumps()
{
    stringstream ss;
    
    ss << SRS_JARRAY_START;
    
    for (int i = 0; i < (int)properties.size(); i++) {
        SrsJsonAny* any = properties[i];
        
        ss << any->dumps();
        
        if (i < (int)properties.size() - 1) {
            ss << SRS_JFIELD_CONT;
        }
    }
    
    ss << SRS_JARRAY_END;
    
    return ss.str();
}

SrsAmf0Any* SrsJsonArray::to_amf0()
{
    SrsAmf0StrictArray* arr = SrsAmf0Any::strict_array();
    
    for (int i = 0; i < (int)properties.size(); i++) {
        SrsJsonAny* any = properties[i];
        
        arr->append(any->to_amf0());
    }
    
    return arr;
}

////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////


