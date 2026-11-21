//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_CORE_AUTO_FREE_HPP
#define SRS_CORE_AUTO_FREE_HPP

#include <srs_core.hpp>

// See https://github.com/ossrs/srs/issues/4551
template <class T>
class SrsUniquePtr
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    T *ptr_;
    void (*deleter_)(T *);

public:
    SrsUniquePtr(T *ptr = NULL, void (*deleter)(T *) = NULL)
    {
        ptr_ = ptr;
        deleter_ = deleter;
    }
    virtual ~SrsUniquePtr()
    {
        if (!deleter_) {
            delete ptr_;
        } else {
            deleter_(ptr_);
        }
    }

public:
    T *get()
    {
        return ptr_;
    }
    T *operator->()
    {
        return ptr_;
    }

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsUniquePtr(const SrsUniquePtr<T> &);
    SrsUniquePtr<T> &operator=(const SrsUniquePtr<T> &);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    T &
    operator*();
    operator bool() const;
#if __cplusplus >= 201103L // C++11
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsUniquePtr(SrsUniquePtr<T> &&);
    SrsUniquePtr<T> &operator=(SrsUniquePtr<T> &&);
#endif
};

// See https://github.com/ossrs/srs/issues/4551
template <class T>
class SrsUniquePtr<T[]>
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    T *ptr_;

public:
    SrsUniquePtr(T *ptr = NULL)
    {
        ptr_ = ptr;
    }
    virtual ~SrsUniquePtr()
    {
        delete[] ptr_;
    }

public:
    T *get()
    {
        return ptr_;
    }
    T &operator[](std::size_t index)
    {
        return ptr_[index];
    }
    const T &operator[](std::size_t index) const
    {
        return ptr_[index];
    }

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsUniquePtr(const SrsUniquePtr<T> &);
    SrsUniquePtr<T> &operator=(const SrsUniquePtr<T> &);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    T &
    operator*();
    operator bool() const;
#if __cplusplus >= 201103L // C++11
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsUniquePtr(SrsUniquePtr<T> &&);
    SrsUniquePtr<T> &operator=(SrsUniquePtr<T> &&);
#endif
};

// Unlike std::shared_ptr, it has no weak ptr, no shared from this, no inheritance and no
// comparing, see https://github.com/ossrs/srs/issues/4551
template <class T>
class SrsSharedPtr
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    T *ptr_;
    uint32_t *ref_count_;

public:
    SrsSharedPtr(T *ptr = NULL)
    {
        ptr_ = ptr;
        ref_count_ = ptr ? new uint32_t(1) : NULL;
    }
    SrsSharedPtr(const SrsSharedPtr<T> &cp)
    {
        copy(cp);
    }
    virtual ~SrsSharedPtr()
    {
        reset();
    }

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    void reset()
    {
        if (!ref_count_)
            return;

        (*ref_count_)--;
        if (*ref_count_ == 0) {
            delete ptr_;
            delete ref_count_;
        }

        ptr_ = NULL;
        ref_count_ = NULL;
    }
    void copy(const SrsSharedPtr<T> &cp)
    {
        ptr_ = cp.ptr_;
        ref_count_ = cp.ref_count_;
        if (ref_count_)
            (*ref_count_)++;
    }
    void move(SrsSharedPtr<T> &cp)
    {
        ptr_ = cp.ptr_;
        ref_count_ = cp.ref_count_;
        cp.ptr_ = NULL;
        cp.ref_count_ = NULL;
    }

public:
    T *get()
    {
        return ptr_;
    }
    T *operator->()
    {
        return ptr_;
    }
    SrsSharedPtr<T> &operator=(const SrsSharedPtr<T> &cp)
    {
        if (this != &cp) {
            reset();
            copy(cp);
        }
        return *this;
    }

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    T &
    operator*()
    {
        return *ptr_;
    }
    operator bool() const
    {
        return ptr_ != NULL;
    }
#if __cplusplus >= 201103L // C++11
public:
    SrsSharedPtr(SrsSharedPtr<T> &&cp)
    {
        move(cp);
    };
    SrsSharedPtr<T> &operator=(SrsSharedPtr<T> &&cp)
    {
        if (this != &cp) {
            reset();
            move(cp);
        }
        return *this;
    };
#endif
};

#endif
