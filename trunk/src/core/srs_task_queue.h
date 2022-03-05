
#ifndef SRS_TASK_QUEUE_H_
#define SRS_TASK_QUEUE_H_

#include <queue>

template<typename TaskT>
class SrsTaskQueue
{
public:
    SrsTaskQueue() = default;
    ~SrsTaskQueue() = default;
    SrsTaskQueue(SrsTaskQueue&& other) = delete;
    SrsTaskQueue& operator=(SrsTaskQueue&& other) = delete;
    SrsTaskQueue(const SrsTaskQueue&) = delete;
    SrsTaskQueue& operator=(const SrsTaskQueue&) = delete;

    void push(const TaskT& task)
    {
        tasks_.push(task);
    }

    void push(TaskT&& task)
    {
        tasks_.push(task);
    }

    bool pop(TaskT& task)
    {
        if (!tasks_.empty())
        {
            task = tasks_.front();
            tasks_.pop();
            return true;
        }
        else
        {
            return false;
        }
    }

    bool empty() const
    {
        return tasks_.empty();
    }

    size_t size() const
    {
        return tasks_.size();
    }

private:
    std::queue<TaskT> tasks_;
};



#endif  // SRS_TASK_QUEUE_H_
