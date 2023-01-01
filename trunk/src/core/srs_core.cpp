//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_core.hpp>

_SrsContextId::_SrsContextId()
{
}

_SrsContextId::_SrsContextId(const _SrsContextId& cp)
{
    v_ = cp.v_;
}

_SrsContextId& _SrsContextId::operator=(const _SrsContextId& cp)
{
    v_ = cp.v_;
    return *this;
}

_SrsContextId::~_SrsContextId()
{
}

const char* _SrsContextId::c_str() const
{
    return v_.c_str();
}

bool _SrsContextId::empty() const
{
    return v_.empty();
}

int _SrsContextId::compare(const _SrsContextId& to) const
{
    return v_.compare(to.v_);
}

_SrsContextId& _SrsContextId::set_value(const std::string& v)
{
    v_ = v;
    return *this;
}

