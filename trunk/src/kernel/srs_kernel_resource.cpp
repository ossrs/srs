//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_kernel_resource.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_factory.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_log.hpp>

#include <algorithm>
using namespace std;

SrsPps *_srs_pps_ids = NULL;
SrsPps *_srs_pps_fids = NULL;
SrsPps *_srs_pps_fids_level0 = NULL;
SrsPps *_srs_pps_dispose = NULL;

SrsResourceManager *_srs_conn_manager = NULL;

ISrsDisposingHandler::ISrsDisposingHandler()
{
}

ISrsDisposingHandler::~ISrsDisposingHandler()
{
}

ISrsResource::ISrsResource()
{
}

ISrsResource::~ISrsResource()
{
}

std::string ISrsResource::desc()
{
    return "Resource";
}

ISrsResourceManager::ISrsResourceManager()
{
}

ISrsResourceManager::~ISrsResourceManager()
{
}

SrsResourceManager::SrsResourceManager(const std::string &label, bool verbose)
{
    verbose_ = verbose;
    label_ = label;
    cond_ = _srs_kernel_factory->create_cond();
    trd_ = NULL;
    p_disposing_ = NULL;
    removing_ = false;

    nn_level0_cache_ = 100000;
    conns_level0_cache_ = new SrsResourceFastIdItem[nn_level0_cache_];
}

SrsResourceManager::~SrsResourceManager()
{
    if (trd_) {
        cond_->signal();
        trd_->stop();

        srs_freep(trd_);
    }
    srs_freep(cond_);

    clear();

    // Free all objects not in zombies.
    std::vector<ISrsResource *>::iterator it;
    for (it = conns_.begin(); it != conns_.end(); ++it) {
        ISrsResource *resource = *it;
        srs_freep(resource);
    }

    srs_freepa(conns_level0_cache_);
}

srs_error_t SrsResourceManager::start()
{
    srs_error_t err = srs_success;

    cid_ = _srs_context->generate_id();
    trd_ = _srs_kernel_factory->create_coroutine("manager", this, cid_);

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "conn manager");
    }

    return err;
}

bool SrsResourceManager::empty()
{
    return conns_.empty();
}

size_t SrsResourceManager::size()
{
    return conns_.size();
}

srs_error_t SrsResourceManager::cycle()
{
    srs_error_t err = srs_success;

    srs_trace("%s: connection manager run, conns=%d", label_.c_str(), (int)conns_.size());

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "conn manager");
        }

        // Clear all zombies, because we may switch context and lost signal
        // when we clear zombie connection.
        while (!zombies_.empty()) {
            clear();
        }

        cond_->wait();
    }

    return err;
}

void SrsResourceManager::add(ISrsResource *conn, bool *exists)
{
    if (std::find(conns_.begin(), conns_.end(), conn) == conns_.end()) {
        conns_.push_back(conn);
    } else {
        if (exists) {
            *exists = true;
        }
    }
}

void SrsResourceManager::add_with_id(const std::string &id, ISrsResource *conn)
{
    add(conn);
    conns_id_[id] = conn;
}

void SrsResourceManager::add_with_fast_id(uint64_t id, ISrsResource *conn)
{
    bool exists = false;
    add(conn, &exists);
    conns_fast_id_[id] = conn;

    if (exists) {
        return;
    }

    // For new resource, build the level-0 cache for fast-id.
    SrsResourceFastIdItem *item = &conns_level0_cache_[(id | id >> 32) % nn_level0_cache_];

    // Ignore if exits item.
    if (item->fast_id_ && item->fast_id_ == id) {
        return;
    }

    // Fresh one, create the item.
    if (!item->fast_id_) {
        item->fast_id_ = id;
        item->impl_ = conn;
        item->nn_collisions_ = 1;
        item->available_ = true;
    }

    // Collision, increase the collisions.
    if (item->fast_id_ != id) {
        item->nn_collisions_++;
        item->available_ = false;
    }
}

void SrsResourceManager::add_with_name(const std::string &name, ISrsResource *conn)
{
    add(conn);
    conns_name_[name] = conn;
}

ISrsResource *SrsResourceManager::at(int index)
{
    return (index < (int)conns_.size()) ? conns_.at(index) : NULL;
}

ISrsResource *SrsResourceManager::find_by_id(std::string id)
{
    ++_srs_pps_ids->sugar_;
    map<string, ISrsResource *>::iterator it = conns_id_.find(id);
    return (it != conns_id_.end()) ? it->second : NULL;
}

ISrsResource *SrsResourceManager::find_by_fast_id(uint64_t id)
{
    SrsResourceFastIdItem *item = &conns_level0_cache_[(id | id >> 32) % nn_level0_cache_];
    if (item->available_ && item->fast_id_ == id) {
        ++_srs_pps_fids_level0->sugar_;
        return item->impl_;
    }

    ++_srs_pps_fids->sugar_;
    map<uint64_t, ISrsResource *>::iterator it = conns_fast_id_.find(id);
    return (it != conns_fast_id_.end()) ? it->second : NULL;
}

ISrsResource *SrsResourceManager::find_by_name(std::string name)
{
    ++_srs_pps_ids->sugar_;
    map<string, ISrsResource *>::iterator it = conns_name_.find(name);
    return (it != conns_name_.end()) ? it->second : NULL;
}

void SrsResourceManager::subscribe(ISrsDisposingHandler *h)
{
    if (std::find(handlers_.begin(), handlers_.end(), h) == handlers_.end()) {
        handlers_.push_back(h);
    }

    // Restore the handler from unsubscribing handlers.
    vector<ISrsDisposingHandler *>::iterator it;
    if ((it = std::find(unsubs_.begin(), unsubs_.end(), h)) != unsubs_.end()) {
        it = unsubs_.erase(it);
    }
}

void SrsResourceManager::unsubscribe(ISrsDisposingHandler *h)
{
    vector<ISrsDisposingHandler *>::iterator it = find(handlers_.begin(), handlers_.end(), h);
    if (it != handlers_.end()) {
        it = handlers_.erase(it);
    }

    // Put it to the unsubscribing handlers.
    if (std::find(unsubs_.begin(), unsubs_.end(), h) == unsubs_.end()) {
        unsubs_.push_back(h);
    }
}

void SrsResourceManager::remove(ISrsResource *c)
{
    SrsContextRestore(_srs_context->get_id());

    removing_ = true;
    do_remove(c);
    removing_ = false;
}

void SrsResourceManager::do_remove(ISrsResource *c)
{
    bool in_zombie = false;
    bool in_disposing = false;
    check_remove(c, in_zombie, in_disposing);
    bool ignored = in_zombie || in_disposing;

    if (verbose_) {
        _srs_context->set_id(c->get_id());
        srs_trace("%s: before dispose resource(%s)(%p), conns=%d, zombies=%d, ign=%d, inz=%d, ind=%d",
                  label_.c_str(), c->desc().c_str(), c, (int)conns_.size(), (int)zombies_.size(), ignored,
                  in_zombie, in_disposing);
    }
    if (ignored) {
        return;
    }

    // Push to zombies, we will free it in another coroutine.
    zombies_.push_back(c);

    // We should copy all handlers, because it may change during callback.
    vector<ISrsDisposingHandler *> handlers = handlers_;

    // Notify other handlers to handle the before-dispose event.
    for (int i = 0; i < (int)handlers.size(); i++) {
        ISrsDisposingHandler *h = handlers.at(i);

        // Ignore if handler is unsubscribing.
        if (!unsubs_.empty() && std::find(unsubs_.begin(), unsubs_.end(), h) != unsubs_.end()) {
            srs_warn2(TAG_RESOURCE_UNSUB, "%s: ignore before-dispose resource(%s)(%p) for %p, conns=%d",
                      label_.c_str(), c->desc().c_str(), c, h, (int)conns_.size());
            continue;
        }

        h->on_before_dispose(c);
    }

    // Notify the coroutine to free it.
    cond_->signal();
}

void SrsResourceManager::check_remove(ISrsResource *c, bool &in_zombie, bool &in_disposing)
{
    // Only notify when not removed(in zombies_).
    vector<ISrsResource *>::iterator it = std::find(zombies_.begin(), zombies_.end(), c);
    if (it != zombies_.end()) {
        in_zombie = true;
    }

    // Also ignore when we are disposing it.
    if (p_disposing_) {
        it = std::find(p_disposing_->begin(), p_disposing_->end(), c);
        if (it != p_disposing_->end()) {
            in_disposing = true;
        }
    }
}

void SrsResourceManager::clear()
{
    if (zombies_.empty()) {
        return;
    }

    SrsContextRestore(cid_);
    if (verbose_) {
        srs_trace("%s: clear zombies=%d resources, conns=%d, removing=%d, unsubs=%d",
                  label_.c_str(), (int)zombies_.size(), (int)conns_.size(), removing_, (int)unsubs_.size());
    }

    // Clear all unsubscribing handlers, if not removing any resource.
    if (!removing_ && !unsubs_.empty()) {
        vector<ISrsDisposingHandler *>().swap(unsubs_);
    }

    do_clear();
}

void SrsResourceManager::do_clear()
{
    // To prevent thread switch when delete connection,
    // we copy all connections then free one by one.
    vector<ISrsResource *> copy;
    copy.swap(zombies_);
    p_disposing_ = &copy;

    for (int i = 0; i < (int)copy.size(); i++) {
        ISrsResource *conn = copy.at(i);

        if (verbose_) {
            _srs_context->set_id(conn->get_id());
            srs_trace("%s: disposing #%d resource(%s)(%p), conns=%d, disposing=%d, zombies=%d", label_.c_str(),
                      i, conn->desc().c_str(), conn, (int)conns_.size(), (int)copy.size(), (int)zombies_.size());
        }

        ++_srs_pps_dispose->sugar_;

        dispose(conn);
    }

    // Reset it for it points to a local object.
    // @remark We must set the disposing to NULL to avoid reusing address,
    // because the context might switch.
    p_disposing_ = NULL;

    // We should free the resources when finished all disposing callbacks,
    // which might cause context switch and reuse the freed addresses.
    for (int i = 0; i < (int)copy.size(); i++) {
        ISrsResource *conn = copy.at(i);
        srs_freep(conn);
    }
}

void SrsResourceManager::dispose(ISrsResource *c)
{
    for (map<string, ISrsResource *>::iterator it = conns_name_.begin(); it != conns_name_.end();) {
        if (c != it->second) {
            ++it;
        } else {
            // Use C++98 style: https://stackoverflow.com/a/4636230
            conns_name_.erase(it++);
        }
    }

    for (map<string, ISrsResource *>::iterator it = conns_id_.begin(); it != conns_id_.end();) {
        if (c != it->second) {
            ++it;
        } else {
            // Use C++98 style: https://stackoverflow.com/a/4636230
            conns_id_.erase(it++);
        }
    }

    for (map<uint64_t, ISrsResource *>::iterator it = conns_fast_id_.begin(); it != conns_fast_id_.end();) {
        if (c != it->second) {
            ++it;
        } else {
            // Update the level-0 cache for fast-id.
            uint64_t id = it->first;
            SrsResourceFastIdItem *item = &conns_level0_cache_[(id | id >> 32) % nn_level0_cache_];
            item->nn_collisions_--;
            if (!item->nn_collisions_) {
                item->fast_id_ = 0;
                item->available_ = false;
            }

            // Use C++98 style: https://stackoverflow.com/a/4636230
            conns_fast_id_.erase(it++);
        }
    }

    vector<ISrsResource *>::iterator it = std::find(conns_.begin(), conns_.end(), c);
    if (it != conns_.end()) {
        it = conns_.erase(it);
    }

    // We should copy all handlers, because it may change during callback.
    vector<ISrsDisposingHandler *> handlers = handlers_;

    // Notify other handlers to handle the disposing event.
    for (int i = 0; i < (int)handlers.size(); i++) {
        ISrsDisposingHandler *h = handlers.at(i);

        // Ignore if handler is unsubscribing.
        if (!unsubs_.empty() && std::find(unsubs_.begin(), unsubs_.end(), h) != unsubs_.end()) {
            srs_warn2(TAG_RESOURCE_UNSUB, "%s: ignore disposing resource(%s)(%p) for %p, conns=%d",
                      label_.c_str(), c->desc().c_str(), c, h, (int)conns_.size());
            continue;
        }

        h->on_disposing(c);
    }
}
