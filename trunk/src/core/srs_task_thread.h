
#ifndef SRS_TASK_THREAD_H_
#define SRS_TASK_THREAD_H_

#include <thread>
#include <srs_task_queue.h>
#include <srs_thread_queue.h>

class SrsTaskThread
{
private:

public:
    SrsTaskThread();
    ~SrsTaskThread();
    SrsTaskThread(SrsTaskThread&&) = delete;
    SrsTaskThread& operator=(SrsTaskThread&&) = delete;
    SrsTaskThread(const SrsTaskThread&) = delete;
    SrsTaskThread& operator=(const SrsTaskThread&) = delete;

    void begin(int thread_number = 1);
    void end();

    void post_task(const std::function<void()>& task);
    void post_task(std::function<void()>&& task);
    size_t task_count() const;
    void set_timer(uint32_t id, uint32_t elapse /* milliseconds */,
        const std::function<void(uint32_t)>& on_timer);
    void kill_timer(uint32_t id);

private:
    void thread_proc();

private:
    std::vector<std::thread> threads_;
    SrsThreadQueue<std::function<void()>, SrsTaskQueue> queue_;
};


#endif  // SRS_TASK_THREAD_H_
