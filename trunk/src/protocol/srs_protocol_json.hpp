//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_JSON_HPP
#define SRS_PROTOCOL_JSON_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

// @see: https://github.com/udp/json-parser

class SrsAmf0Any;
class SrsJsonArray;
class SrsJsonObject;

class SrsJsonAny
{
public:
    char marker_;
// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    SrsJsonAny();

public:
    virtual ~SrsJsonAny();

public:
    virtual bool is_string();
    virtual bool is_boolean();
    virtual bool is_integer();
    virtual bool is_number();
    virtual bool is_object();
    virtual bool is_array();
    virtual bool is_null();

public:
    virtual std::string to_str();
    virtual bool to_boolean();
    virtual int64_t to_integer();
    virtual double to_number();
    virtual SrsJsonObject *to_object();
    virtual SrsJsonArray *to_array();

public:
    virtual std::string dumps();
    virtual SrsAmf0Any *to_amf0();

public:
    static SrsJsonAny *str(const char *value = NULL);
    static SrsJsonAny *str(const char *value, int length);
    static SrsJsonAny *boolean(bool value = false);
    static SrsJsonAny *integer(int64_t value = 0);
    static SrsJsonAny *number(double value = 0.0);
    static SrsJsonAny *null();
    static SrsJsonObject *object();
    static SrsJsonArray *array();

public:
    // Returns NULL on error.
    static SrsJsonAny *loads(std::string str);
};

class SrsJsonObject : public SrsJsonAny
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    typedef std::pair<std::string, SrsJsonAny *> SrsJsonObjectPropertyType;
    std::vector<SrsJsonObjectPropertyType> properties_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    friend class SrsJsonAny;
    SrsJsonObject();

public:
    virtual ~SrsJsonObject();

public:
    virtual int count();
    virtual std::string key_at(int index);
    virtual SrsJsonAny *value_at(int index);

public:
    virtual std::string dumps();
    virtual SrsAmf0Any *to_amf0();

public:
    virtual SrsJsonObject *set(std::string key, SrsJsonAny *value);
    virtual SrsJsonAny *get_property(std::string name);
    virtual SrsJsonAny *ensure_property_string(std::string name);
    virtual SrsJsonAny *ensure_property_integer(std::string name);
    virtual SrsJsonAny *ensure_property_number(std::string name);
    virtual SrsJsonAny *ensure_property_boolean(std::string name);
    virtual SrsJsonAny *ensure_property_object(std::string name);
    virtual SrsJsonAny *ensure_property_array(std::string name);
};

class SrsJsonArray : public SrsJsonAny
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<SrsJsonAny *> properties_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    friend class SrsJsonAny;
    SrsJsonArray();

public:
    virtual ~SrsJsonArray();

public:
    virtual int count();
    virtual SrsJsonAny *at(int index);
    virtual SrsJsonArray *add(SrsJsonAny *value);
    virtual SrsJsonArray *append(SrsJsonAny *value);

public:
    virtual std::string dumps();
    virtual SrsAmf0Any *to_amf0();
};

#endif
