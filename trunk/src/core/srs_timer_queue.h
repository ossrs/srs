
#ifndef SRS_TIMER_QUEUE_H_
#define SRS_TIMER_QUEUE_H_

#include <set>
#include <functional>
#include <stdint.h>

class SrsTimerQueue
{
public:
    SrsTimerQueue() = default;
    ~SrsTimerQueue() = default;
    SrsTimerQueue(SrsTimerQueue&& other) = delete;
    SrsTimerQueue& operator=(SrsTimerQueue&& other) = delete;
    SrsTimerQueue(const SrsTimerQueue&) = delete;
    SrsTimerQueue& operator=(const SrsTimerQueue&) = delete;

    void set_timer(uint32_t id, uint32_t elapse /* milliseconds */,
        const std::function<void(uint32_t)>& on_timer);
    void kill_timer(uint32_t id);
    bool empty() const;

    bool peek(uint32_t& id, std::function<void(uint32_t)>& on_timer);
    uint64_t get_wait_time();

private:
    void erase(uint32_t id);

private:
    struct Timer
    {
        uint32_t id;  ///< 时钟ID
        uint64_t interval;  ///< 间隔, 单位: 毫秒
        uint64_t start;  ///< 开始时间, 单位: 毫秒
        std::function<void(uint32_t)> on_timer;  ///< 回调函数
        bool operator<(const Timer& right) const;
    };

    std::set<Timer> timers_;  ///< timers
};

#endif  // SRS_TIMER_QUEUE_H_
