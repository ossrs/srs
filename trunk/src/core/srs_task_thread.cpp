#include <srs_task_thread.h>

SrsTaskThread::SrsTaskThread()
{
}

SrsTaskThread::~SrsTaskThread()
{
}

void SrsTaskThread::begin(int thread_number)
{
    for (int i = 0; i < thread_number; i++)
    {
        threads_.emplace_back(&SrsTaskThread::thread_proc, this);
    }
}

void SrsTaskThread::end()
{
    queue_.notify_exit();
    for (auto& thread : threads_)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
}

void SrsTaskThread::post_task(const std::function<void()>& task)
{
    queue_.post_task(std::function<void()>(task));
}

void SrsTaskThread::post_task(std::function<void()>&& task)
{
    queue_.post_task(task);
}

size_t SrsTaskThread::task_count() const
{
    return queue_.task_count();
}

void SrsTaskThread::set_timer(
    uint32_t id,
    uint32_t elapse,
    const std::function<void(uint32_t)>& on_timer)
{
    queue_.set_timer(id, elapse, on_timer);
}

void SrsTaskThread::kill_timer(uint32_t id)
{
    queue_.kill_timer(id);
}

void SrsTaskThread::thread_proc()
{
    queue_.do_while();
}

