#include <srs_task_thread.h>
#include <iostream>


void on_timer(uint32_t id)
{
    std::cout<< "定时任务被执行了!!!"<<std::endl;
}

int main ()
{
    SrsTaskThread srs_thread;

    srs_thread.begin(1); //线程名设置可以添加，开启线程

    srs_thread.set_timer(1, 500, std::bind(&on_timer, std::placeholders::_1));
    std::string task_data = "打印日志";
    auto func = [](const std::string data)
    {
        std::cout << "执行任务：" << data.c_str() << std::endl;
    };

    srs_thread.post_task(std::bind(func, task_data));

    srs_thread.end();
    

}