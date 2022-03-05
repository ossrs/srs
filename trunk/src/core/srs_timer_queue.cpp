#include <srs_timer_queue.h>
#include <assert.h>
#include <algorithm>
#include <mutex>


uint64_t get_tick_count()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}


void SrsTimerQueue::set_timer(
    uint32_t id,
    uint32_t elapse,
    const std::function<void(uint32_t)>& on_timer)
{
    erase(id);
    timers_.insert(Timer{ id, elapse, get_tick_count(), on_timer });
}

void SrsTimerQueue::kill_timer(uint32_t id)
{
    erase(id);
}

void SrsTimerQueue::erase(uint32_t id)
{
    for (auto iter = timers_.begin(); iter != timers_.end(); iter++)
    {
        if (iter->id == id)
        {
            timers_.erase(iter);
            break;
        }
    }
}

bool SrsTimerQueue::empty()const
{
    return timers_.empty();
}

bool SrsTimerQueue::peek(uint32_t& id, std::function<void(uint32_t)>& on_timer)
{
    if (timers_.empty()) return false;
    Timer first_timer = *timers_.begin();
    uint64_t end_time = first_timer.start + first_timer.interval;
    uint64_t current = get_tick_count();
    if (current >= end_time)
    {
        id = first_timer.id;
        on_timer = first_timer.on_timer;
        timers_.erase(timers_.begin());
        first_timer.start += first_timer.interval;
        timers_.insert(first_timer);
        return true;
    }
    else
    {
        return false;
    }
}

uint64_t SrsTimerQueue::get_wait_time()
{
    const uint64_t kWaitInfinite = 60 * 60 * 1000;  // 1 hour
    if (timers_.empty()) return kWaitInfinite;
    Timer first_timer = *timers_.begin();
    uint64_t end_time = first_timer.start + first_timer.interval;
    uint64_t current = get_tick_count();
    return current <= end_time ? end_time - current : 0;
}

bool SrsTimerQueue::Timer::operator<(const Timer& right) const
{
    auto end_time  = start  + interval;
    auto right_end_time = right.start + right.interval;
    return (
        (end_time < right_end_time) ||
        (end_time == right_end_time && id < right.id));
}
