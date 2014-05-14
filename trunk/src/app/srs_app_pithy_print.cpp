/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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

#include <srs_app_pithy_print.hpp>

#include <stdlib.h>
#include <map>

#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_app_reload.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>

#define SRS_STAGE_DEFAULT_INTERVAL_MS 1200

SrsStageInfo::SrsStageInfo(int _stage_id)
{
    stage_id = _stage_id;
    nb_clients = 0;
    _age = printed_age = 0;
    
    update_print_time();
    
    _srs_config->subscribe(this);
}

SrsStageInfo::~SrsStageInfo()
{
    _srs_config->unsubscribe(this);
}

void SrsStageInfo::update_print_time()
{
    switch (stage_id) {
        case SRS_STAGE_PLAY_USER: {
            pithy_print_time_ms = _srs_config->get_pithy_print_play();
            break;
        }
        case SRS_STAGE_PUBLISH_USER: {
            pithy_print_time_ms = _srs_config->get_pithy_print_publish();
            break;
        }
        case SRS_STAGE_FORWARDER: {
            pithy_print_time_ms = _srs_config->get_pithy_print_forwarder();
            break;
        }
        case SRS_STAGE_ENCODER: {
            pithy_print_time_ms = _srs_config->get_pithy_print_encoder();
            break;
        }
        case SRS_STAGE_INGESTER: {
            pithy_print_time_ms = _srs_config->get_pithy_print_ingester();
            break;
        }
        case SRS_STAGE_EDGE: {
            pithy_print_time_ms = _srs_config->get_pithy_print_edge();
            break;
        }
        case SRS_STAGE_HLS: {
            pithy_print_time_ms = _srs_config->get_pithy_print_hls();
            break;
        }
        default: {
            pithy_print_time_ms = SRS_STAGE_DEFAULT_INTERVAL_MS;
            break;
        }
    }
}

void SrsStageInfo::elapse(int64_t diff)
{
    _age += diff;
}

bool SrsStageInfo::can_print()
{
    int64_t can_print_age = nb_clients * pithy_print_time_ms;
    
    bool can_print = _age >= can_print_age;
    if (can_print) {
        _age = 0;
    }
    
    return can_print;
}

int SrsStageInfo::on_reload_pithy_print()
{
    update_print_time();
    return ERROR_SUCCESS;
}
    
static std::map<int, SrsStageInfo*> _srs_stages;

SrsPithyPrint::SrsPithyPrint(int _stage_id)
{
    stage_id = _stage_id;
    client_id = enter_stage();
    previous_tick = srs_get_system_time_ms();
    _age = 0;
}

SrsPithyPrint::~SrsPithyPrint()
{
    leave_stage();
}

int SrsPithyPrint::enter_stage()
{
    SrsStageInfo* stage = NULL;
    
    std::map<int, SrsStageInfo*>::iterator it = _srs_stages.find(stage_id);
    if (it == _srs_stages.end()) {
        stage = new SrsStageInfo(stage_id);
        _srs_stages[stage_id] = stage;
    } else {
        stage = it->second;
    }
    
    srs_assert(stage != NULL);
    client_id = stage->nb_clients++;

    srs_verbose("enter stage, stage_id=%d, client_id=%d, nb_clients=%d, time_ms=%d",
        stage->stage_id, client_id, stage->nb_clients, stage->pithy_print_time_ms);
    
    return client_id;
}

void SrsPithyPrint::leave_stage()
{
    SrsStageInfo* stage = _srs_stages[stage_id];
    srs_assert(stage != NULL);
    
    stage->nb_clients--;

    srs_verbose("leave stage, stage_id=%d, client_id=%d, nb_clients=%d, time_ms=%d",
        stage->stage_id, client_id, stage->nb_clients, stage->pithy_print_time_ms);
}

void SrsPithyPrint::elapse()
{
    SrsStageInfo* stage = _srs_stages[stage_id];
    srs_assert(stage != NULL);
    
    int64_t diff = srs_get_system_time_ms() - previous_tick;
    diff = srs_max(0, diff);
    
    stage->elapse(diff);
    _age += diff;
    previous_tick = srs_get_system_time_ms();
}

bool SrsPithyPrint::can_print()
{
    SrsStageInfo* stage = _srs_stages[stage_id];
    srs_assert(stage != NULL);
    
    return stage->can_print();
}

int64_t SrsPithyPrint::age()
{
    return _age;
}

