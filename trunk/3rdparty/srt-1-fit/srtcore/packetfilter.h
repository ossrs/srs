/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2019 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#ifndef INC__PACKETFILTER_H
#define INC__PACKETFILTER_H

#include <cstdlib>
#include <map>
#include <string>

#include "packet.h"
#include "queue.h"
#include "utilities.h"
#include "packetfilter_api.h"

class PacketFilter
{
    friend class SrtPacketFilterBase;

public:

    typedef std::vector< std::pair<int32_t, int32_t> > loss_seqs_t;

    typedef SrtPacketFilterBase* filter_create_t(const SrtFilterInitializer& init, std::vector<SrtPacket>&, const std::string& config);

private:
    friend bool ParseFilterConfig(std::string s, SrtFilterConfig& out);
    class Factory
    {
    public:
        virtual SrtPacketFilterBase* Create(const SrtFilterInitializer& init, std::vector<SrtPacket>& provided, const std::string& confstr) = 0;

        // Characteristic data
        virtual size_t ExtraSize() = 0;

        virtual ~Factory();
    };

    template <class Target>
    class Creator: public Factory
    {
        virtual SrtPacketFilterBase* Create(const SrtFilterInitializer& init,
                std::vector<SrtPacket>& provided,
                const std::string& confstr) ATR_OVERRIDE
        { return new Target(init, provided, confstr); }

        // Import the extra size data
        virtual size_t ExtraSize() ATR_OVERRIDE { return Target::EXTRA_SIZE; }

    public:
        Creator() {}
        virtual ~Creator() {}
    };


    // We need a private wrapper for the auto-pointer, can't use
    // std::unique_ptr here due to no C++11.
    struct ManagedPtr
    {
        Factory* f;
        mutable bool owns;

        // Accept whatever
        ManagedPtr(Factory* ff): f(ff), owns(true) {}
        ManagedPtr(): f(NULL), owns(false) {}
        ~ManagedPtr()
        {
            if (owns)
                delete f;
        }

        void copy_internal(const ManagedPtr& other)
        {
            other.owns = false;
            f = other.f;
            owns = true;
        }

        ManagedPtr(const ManagedPtr& other)
        {
            copy_internal(other);
        }

        void operator=(const ManagedPtr& other)
        {
            if (owns)
                delete f;
            copy_internal(other);
        }

        Factory* operator->() { return f; }
        Factory* get() { return f; }
    };

    // The list of builtin names that are reserved.
    static std::set<std::string> builtin_filters;

    // Temporarily changed to linear searching, until this is exposed
    // for a user-defined filter.
    typedef std::map<std::string, ManagedPtr> filters_map_t;
    static filters_map_t filters;

    // This is a filter container.
    SrtPacketFilterBase* m_filter;
    void Check()
    {
#if ENABLE_DEBUG
        if (!m_filter)
            abort();
#endif
        // Don't do any check for now.
    }

public:

    static void globalInit();

    static bool IsBuiltin(const std::string&);

    template <class NewFilter>
    static bool add(const std::string& name)
    {
        if (IsBuiltin(name))
            return false;

        filters[name] = new Creator<NewFilter>;
        return true;
    }

    static Factory* find(const std::string& type)
    {
        filters_map_t::iterator i = filters.find(type);
        if (i == filters.end())
            return NULL; // No matter what to return - this is "undefined behavior" to be prevented
        return i->second.get();
    }

    // Filter is optional, so this check should be done always
    // manually.
    bool installed() const { return m_filter; }
    operator bool() const { return installed(); }

    SrtPacketFilterBase* operator->() { Check(); return m_filter; }

    // In the beginning it's initialized as first, builtin default.
    // Still, it will be created only when requested.
    PacketFilter(): m_filter(), m_parent(), m_sndctlpkt(0), m_unitq() {}

    // Copy constructor - important when listener-spawning
    // Things being done:
    // 1. The filter is individual, so don't copy it. Set NULL.
    // 2. This will be configued anyway basing on possibly a new rule set.
    PacketFilter(const PacketFilter& source SRT_ATR_UNUSED): m_filter(), m_sndctlpkt(0), m_unitq() {}

    // This function will be called by the parent CUDT
    // in appropriate time. It should select appropriate
    // filter basing on the value in selector, then
    // pin oneself in into CUDT for receiving event signals.
    bool configure(CUDT* parent, CUnitQueue* uq, const std::string& confstr);

    static bool correctConfig(const SrtFilterConfig& c);

    // Will delete the pinned in filter object.
    // This must be defined in *.cpp file due to virtual
    // destruction.
    ~PacketFilter();

    // Simple wrappers
    void feedSource(ref_t<CPacket> r_packet);
    SRT_ARQLevel arqLevel();
    bool packControlPacket(ref_t<CPacket> r_packet, int32_t seq, int kflg);
    void receive(CUnit* unit, ref_t< std::vector<CUnit*> > r_incoming, ref_t<loss_seqs_t> r_loss_seqs);

protected:
    void InsertRebuilt(std::vector<CUnit*>& incoming, CUnitQueue* uq);

    CUDT* m_parent;

    // Sender part
    SrtPacket m_sndctlpkt;

    // Receiver part
    CUnitQueue* m_unitq;
    std::vector<SrtPacket> m_provided;
};


inline void PacketFilter::feedSource(ref_t<CPacket> r_packet) { SRT_ASSERT(m_filter); return m_filter->feedSource(*r_packet); }
inline SRT_ARQLevel PacketFilter::arqLevel() { SRT_ASSERT(m_filter); return m_filter->arqLevel(); }

#endif
