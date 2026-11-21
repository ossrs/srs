//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_AMF0_HPP
#define SRS_PROTOCOL_AMF0_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

class SrsBuffer;
class SrsAmf0Object;
class SrsAmf0EcmaArray;
class SrsAmf0StrictArray;
class SrsJsonAny;

namespace srs_internal
{
class SrsUnSortedHashtable;
class SrsAmf0ObjectEOF;
class SrsAmf0Date;
} // namespace srs_internal

/**
 * 2.1 Types Overview
 * value-type = number-type | boolean-type | string-type | object-type
 *         | null-marker | undefined-marker | reference-type | ecma-array-type
 *         | strict-array-type | date-type | long-string-type | xml-document-type
 *         | typed-object-type
 */
class SrsAmf0Any
{
public:
    char marker_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    friend class SrsAmf0Object;
    friend class SrsAmf0EcmaArray;
    friend class SrsAmf0StrictArray;
    // How deep this value nests in its containers, 1 at the top level.
    int depth_;

public:
    SrsAmf0Any();
    virtual ~SrsAmf0Any();

public:
    virtual bool is_string();
    virtual bool is_boolean();
    virtual bool is_number();
    virtual bool is_null();
    virtual bool is_undefined();
    virtual bool is_object();
    virtual bool is_object_eof();
    virtual bool is_ecma_array();
    virtual bool is_strict_array();
    virtual bool is_date();
    virtual bool is_complex_object();

public:
    virtual std::string to_str();
    virtual const char *to_str_raw();
    virtual bool to_boolean();
    virtual double to_number();
    virtual int64_t to_date();
    virtual int16_t to_date_time_zone();
    virtual SrsAmf0Object *to_object();
    virtual SrsAmf0EcmaArray *to_ecma_array();
    virtual SrsAmf0StrictArray *to_strict_array();

public:
    virtual void set_number(double value);

public:
    // The size includes the marker.
    virtual int total_size() = 0;
    virtual srs_error_t read(SrsBuffer *stream) = 0;
    virtual srs_error_t write(SrsBuffer *stream) = 0;
    virtual SrsAmf0Any *copy() = 0;
    // The caller frees the returned data.
    virtual char *human_print(char **pdata, int *psize);
    virtual SrsJsonAny *to_json();

public:
    static SrsAmf0Any *str(const char *value = NULL);
    static SrsAmf0Any *boolean(bool value = false);
    static SrsAmf0Any *number(double value = 0.0);
    static SrsAmf0Any *date(int64_t value = 0);
    static SrsAmf0Any *null();
    static SrsAmf0Any *undefined();
    static SrsAmf0Object *object();
    static SrsAmf0Any *object_eof();
    static SrsAmf0EcmaArray *ecma_array();
    static SrsAmf0StrictArray *strict_array();

public:
    // Creates the instance by the marker without reading it, so the caller then calls read().
    static srs_error_t discovery(SrsBuffer *stream, SrsAmf0Any **ppvalue);
};

/**
 * 2.5 Object Type
 * anonymous-object-type = object-marker *(object-property)
 * object-property = (UTF-8 value-type) | (UTF-8-empty object-end-marker)
 */
class SrsAmf0Object : public SrsAmf0Any
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_internal::SrsUnSortedHashtable *properties_;
    srs_internal::SrsAmf0ObjectEOF *eof_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    friend class SrsAmf0Any;
    SrsAmf0Object();

public:
    virtual ~SrsAmf0Object();

public:
    virtual int total_size();
    virtual srs_error_t read(SrsBuffer *stream);
    virtual srs_error_t write(SrsBuffer *stream);
    virtual SrsAmf0Any *copy();
    virtual SrsJsonAny *to_json();

public:
    virtual void clear();
    virtual int count();
    virtual std::string key_at(int index);
    virtual const char *key_raw_at(int index);
    virtual SrsAmf0Any *value_at(int index);

public:
    // The object takes ownership of the value.
    virtual void set(std::string key, SrsAmf0Any *value);
    // The object owns the returned value of the getters.
    virtual SrsAmf0Any *get_property(std::string name);
    virtual SrsAmf0Any *ensure_property_string(std::string name);
    virtual SrsAmf0Any *ensure_property_number(std::string name);
    virtual void remove(std::string name);
};

/**
 * 2.10 ECMA Array Type
 * ecma-array-type = associative-count *(object-property)
 * associative-count = U32
 * object-property = (UTF-8 value-type) | (UTF-8-empty object-end-marker)
 */
class SrsAmf0EcmaArray : public SrsAmf0Any
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_internal::SrsUnSortedHashtable *properties_;
    srs_internal::SrsAmf0ObjectEOF *eof_;
    int32_t count_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    friend class SrsAmf0Any;
    SrsAmf0EcmaArray();

public:
    virtual ~SrsAmf0EcmaArray();

public:
    virtual int total_size();
    virtual srs_error_t read(SrsBuffer *stream);
    virtual srs_error_t write(SrsBuffer *stream);
    virtual SrsAmf0Any *copy();
    virtual SrsJsonAny *to_json();

public:
    virtual void clear();
    virtual int count();
    virtual std::string key_at(int index);
    virtual const char *key_raw_at(int index);
    virtual SrsAmf0Any *value_at(int index);

public:
    // The array takes ownership of the value.
    virtual void set(std::string key, SrsAmf0Any *value);
    // The array owns the returned value of the getters.
    virtual SrsAmf0Any *get_property(std::string name);
    virtual SrsAmf0Any *ensure_property_string(std::string name);
    virtual SrsAmf0Any *ensure_property_number(std::string name);
};

/**
 * 2.12 Strict Array Type
 * array-count = U32
 * strict-array-type = array-count *(value-type)
 */
class SrsAmf0StrictArray : public SrsAmf0Any
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<SrsAmf0Any *> properties_;
    int32_t count_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    friend class SrsAmf0Any;
    SrsAmf0StrictArray();

public:
    virtual ~SrsAmf0StrictArray();

public:
    virtual int total_size();
    virtual srs_error_t read(SrsBuffer *stream);
    virtual srs_error_t write(SrsBuffer *stream);
    virtual SrsAmf0Any *copy();
    virtual SrsJsonAny *to_json();

public:
    virtual void clear();
    virtual int count();
    virtual SrsAmf0Any *at(int index);

public:
    // The array takes ownership of the element.
    virtual void append(SrsAmf0Any *any);
};

class SrsAmf0Size
{
public:
    static int utf8(std::string value);
    static int str(std::string value);
    static int number();
    static int date();
    static int null();
    static int undefined();
    static int boolean();
    static int object(SrsAmf0Object *obj);
    static int object_eof();
    static int ecma_array(SrsAmf0EcmaArray *arr);
    static int strict_array(SrsAmf0StrictArray *arr);
    static int any(SrsAmf0Any *o);
};

// The caller frees *ppvalue.
extern srs_error_t srs_amf0_read_any(SrsBuffer *stream, SrsAmf0Any **ppvalue);

/**
 * 2.4 String Type
 * string-type = string-marker UTF-8
 */
extern srs_error_t srs_amf0_read_string(SrsBuffer *stream, std::string &value);
extern srs_error_t srs_amf0_write_string(SrsBuffer *stream, std::string value);

/**
 * 2.3 Boolean Type
 * boolean-type = boolean-marker U8
 *         0 is false, <> 0 is true
 */
extern srs_error_t srs_amf0_read_boolean(SrsBuffer *stream, bool &value);
extern srs_error_t srs_amf0_write_boolean(SrsBuffer *stream, bool value);

/**
 * 2.2 Number Type
 * number-type = number-marker DOUBLE
 */
extern srs_error_t srs_amf0_read_number(SrsBuffer *stream, double &value);
extern srs_error_t srs_amf0_write_number(SrsBuffer *stream, double value);

/**
 * 2.7 null Type
 * null-type = null-marker
 */
extern srs_error_t srs_amf0_read_null(SrsBuffer *stream);
extern srs_error_t srs_amf0_write_null(SrsBuffer *stream);

/**
 * 2.8 undefined Type
 * undefined-type = undefined-marker
 */
extern srs_error_t srs_amf0_read_undefined(SrsBuffer *stream);
extern srs_error_t srs_amf0_write_undefined(SrsBuffer *stream);

namespace srs_internal
{
/**
 * 2.4 String Type
 * string-type = string-marker UTF-8
 */
class SrsAmf0String : public SrsAmf0Any
{
public:
    std::string value_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    friend class SrsAmf0Any;
    SrsAmf0String(const char *value);

public:
    virtual ~SrsAmf0String();

public:
    virtual int total_size();
    virtual srs_error_t read(SrsBuffer *stream);
    virtual srs_error_t write(SrsBuffer *stream);
    virtual SrsAmf0Any *copy();
};

/**
 * 2.3 Boolean Type
 * boolean-type = boolean-marker U8
 *         0 is false, <> 0 is true
 */
class SrsAmf0Boolean : public SrsAmf0Any
{
public:
    bool value_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    friend class SrsAmf0Any;
    SrsAmf0Boolean(bool value);

public:
    virtual ~SrsAmf0Boolean();

public:
    virtual int total_size();
    virtual srs_error_t read(SrsBuffer *stream);
    virtual srs_error_t write(SrsBuffer *stream);
    virtual SrsAmf0Any *copy();
};

/**
 * 2.2 Number Type
 * number-type = number-marker DOUBLE
 */
class SrsAmf0Number : public SrsAmf0Any
{
public:
    double value_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    friend class SrsAmf0Any;
    SrsAmf0Number(double value);

public:
    virtual ~SrsAmf0Number();

public:
    virtual int total_size();
    virtual srs_error_t read(SrsBuffer *stream);
    virtual srs_error_t write(SrsBuffer *stream);
    virtual SrsAmf0Any *copy();
};

/**
 * 2.13 Date Type
 * time-zone = S16 ; reserved, not supported should be set to 0x0000
 * date-type = date-marker DOUBLE time-zone
 * @see: https://github.com/ossrs/srs/issues/185
 */
class SrsAmf0Date : public SrsAmf0Any
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int64_t date_value_;
    int16_t time_zone_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    friend class SrsAmf0Any;
    SrsAmf0Date(int64_t value);

public:
    virtual ~SrsAmf0Date();

public:
    virtual int total_size();
    virtual srs_error_t read(SrsBuffer *stream);
    virtual srs_error_t write(SrsBuffer *stream);
    virtual SrsAmf0Any *copy();

public:
    virtual int64_t date();
    virtual int16_t time_zone();
};

/**
 * 2.7 null Type
 * null-type = null-marker
 */
class SrsAmf0Null : public SrsAmf0Any
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    friend class SrsAmf0Any;
    SrsAmf0Null();

public:
    virtual ~SrsAmf0Null();

public:
    virtual int total_size();
    virtual srs_error_t read(SrsBuffer *stream);
    virtual srs_error_t write(SrsBuffer *stream);
    virtual SrsAmf0Any *copy();
};

/**
 * 2.8 undefined Type
 * undefined-type = undefined-marker
 */
class SrsAmf0Undefined : public SrsAmf0Any
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    friend class SrsAmf0Any;
    SrsAmf0Undefined();

public:
    virtual ~SrsAmf0Undefined();

public:
    virtual int total_size();
    virtual srs_error_t read(SrsBuffer *stream);
    virtual srs_error_t write(SrsBuffer *stream);
    virtual SrsAmf0Any *copy();
};

// Keeps the properties in insertion order: FMLE crashes on the connect app response when they
// are sorted by name, as a map would sort them.
class SrsUnSortedHashtable
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    typedef std::pair<std::string, SrsAmf0Any *> SrsAmf0ObjectPropertyType;
    std::vector<SrsAmf0ObjectPropertyType> properties_;

public:
    SrsUnSortedHashtable();
    virtual ~SrsUnSortedHashtable();

public:
    virtual int count();
    virtual void clear();
    virtual std::string key_at(int index);
    virtual const char *key_raw_at(int index);
    virtual SrsAmf0Any *value_at(int index);
    virtual void set(std::string key, SrsAmf0Any *value);

public:
    virtual SrsAmf0Any *get_property(std::string name);
    virtual SrsAmf0Any *ensure_property_string(std::string name);
    virtual SrsAmf0Any *ensure_property_number(std::string name);
    virtual void remove(std::string name);

public:
    virtual void copy(SrsUnSortedHashtable *src);
};

/**
 * 2.11 Object End Type
 * object-end-type = UTF-8-empty object-end-marker
 * 0x00 0x00 0x09
 */
class SrsAmf0ObjectEOF : public SrsAmf0Any
{
public:
    SrsAmf0ObjectEOF();
    virtual ~SrsAmf0ObjectEOF();

public:
    virtual int total_size();
    virtual srs_error_t read(SrsBuffer *stream);
    virtual srs_error_t write(SrsBuffer *stream);
    virtual SrsAmf0Any *copy();
};

/**
 * 1.3.1 Strings and UTF-8
 * UTF-8 = U16 *(UTF8-char)
 * UTF8-char = UTF8-1 | UTF8-2 | UTF8-3 | UTF8-4
 * UTF8-1 = %x00-7F
 */
extern srs_error_t srs_amf0_read_utf8(SrsBuffer *stream, std::string &value);
extern srs_error_t srs_amf0_write_utf8(SrsBuffer *stream, std::string value);

extern bool srs_amf0_is_object_eof(SrsBuffer *stream);
extern srs_error_t srs_amf0_write_object_eof(SrsBuffer *stream, SrsAmf0ObjectEOF *value);

extern srs_error_t srs_amf0_write_any(SrsBuffer *stream, SrsAmf0Any *value);
}; // namespace srs_internal

#endif
