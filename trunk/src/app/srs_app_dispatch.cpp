//
// Copyright (c) 2022 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_dispatch.hpp>

#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_st.hpp>

#define PARSER_QUEUE_MEM_SIZE (9000)

SrsDispatch::SrsDispatch()
{
    log_queue_ = new SrsCircleQueue<SrsStWork>();
}

SrsDispatch::~SrsDispatch()
{
    srs_freep(log_queue_);
}

srs_error_t SrsDispatch::init()
{
    srs_error_t err = srs_success;

    if ((err = do_init_log(log_queue_, PARSER_QUEUE_MEM_SIZE)) != srs_success) {
        return srs_error_wrap(err, "init shm failed, size=%d", PARSER_QUEUE_MEM_SIZE);
    }
	
    return err;
}

srs_error_t SrsDispatch::do_init_log(SrsCircleQueue<SrsStWork>* queue, int size)
{
    srs_error_t err = srs_success;

    if (!queue) {
        return srs_error_new(ERROR_QUEUE_INIT, "queue is not created");
    }

    if ((err = queue->init(size)) != srs_success) {
        return srs_error_wrap(err, "init shm error, need create");
    }

    return err;
}

srs_error_t SrsDispatch::dispatch_log(SrsStWork& work)
{
    srs_error_t err = srs_success;

    work.type = SrsWorkTypeLog;
    work.push_time = srs_update_system_time();

    if ((err = log_queue_->push(work)) != srs_success) {
        return srs_error_wrap(err, "queue push failed");
    }

    unsigned int work_num = log_queue_->size(); (void)work_num;
    srs_info("push parse work type=%d, fd=%d success. %d in total", work.type, work.fd, work_num);

    return err;
}

srs_error_t SrsDispatch::fetch_log(SrsStWork& work)
{
    srs_error_t err = srs_success;

    if (log_queue_->size() == 0) {
        return srs_error_new(ERROR_QUEUE_POP, "queue is empty");
    }

    if ((err = log_queue_->pop(work)) != srs_success) {
        return srs_error_wrap(err, "queue pop failed");
    }

    srs_trace("get work, type=%d", work.type);

    return err;
}

bool SrsDispatch::empty()
{
    return log_queue_->size() == 0;
}

