#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;

// 获取当前文件名  
#define GET_CURRENT_FILE_NAME() (__builtin_strrchr(__FILE__, '/') ? __builtin_strrchr(__FILE__, '/') + 1 : __FILE__)  
  
// 获取当前行号  
#define GET_CURRENT_LINE_NUMBER() (__builtin_strrchr(__func__, ')') ? atoi(__builtin_strrchr(__func__, ')') - 2 : -1)  

//单例模式 懒汉模式 类实例不用的时候不去初始化，只有使用的时候才初始化 并且使用c++11之后标准
class Log
{
public:
    //C++11以后,使用局部变量懒汉不用加锁
    static Log *get_instance()
    {
        static Log instance;//只有第一次调用会定义(分配空间)并初始化(调用构造函数)，之后在这里都会跳过定义以及初始化步骤
        return &instance;
    }

    static void *flush_log_thread(void *args)//线程主函数；该线程主要从缓冲队列取出一个字符串写入到文件中 其作用相当于消费者 线程声明在init里面
    {
        Log::get_instance()->async_write_log();
    }

    //可选择的参数有日志文件、日志缓冲区大小(即一条日志最大字节数)、最大行数以及阻塞队列最大容量
    bool init(const char *file_name, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    void write_log(int level, const char *format, ...);//写日志信息 异步方式下：将日志添加到缓冲区 其作用相当于生产者，同步方式下直接将日志写入到文件 第一个参数是日志等级
                                                                //同步方式下：日志信息直接写到日志文件 而不进入缓冲队列
    void flush(void);

private:
    Log();
    virtual ~Log();//将析构函数定义为虚函数主要是为了支持多态性。在C++中，如果基类的析构函数被声明为虚函数，那么当通过基类指针删除派生类对象时，就会调用派生类的析构函数，从而正确地释放派生类对象占用的资源。
    void *async_write_log()//异步写日志方法 其主要由线程主函数调用
    {
        string single_log;
        //从阻塞队列中取出一个日志并保存到string，写入文件 一直循环 直到将缓冲数组清空
        while (m_log_queue->pop(single_log))
        {
            m_mutex.lock();//因为日志文件属于共享资源 所以上锁 因为如果同时有两个写进程将自己的日志string串写进日志文件，日志文件存储的信息就会错乱，成乱码
            fputs(single_log.c_str(), m_fp);//c_str()函数被用来将std::string对象转换为C风格的字符串
            m_mutex.unlock();//解锁
        }
    }

private:
    char dir_name[128]; //路径名
    char log_name[128]; //log文件名 仅仅是单文件名 不加路径
    int m_split_lines;  //日志最大行数
    int m_log_buf_size; //日志缓冲区大小
    long long m_count;  //日志行数记录
    int m_today;        //因为按天分类,记录当前时间是那一天
    FILE *m_fp;         //打开log的文件指针
    char *m_buf;        //本地buf 暂存一条日志信息(一条日志具体内容写到这里) 其指向缓冲区大小为log_buf_size
    block_queue<string> *m_log_queue; //阻塞队列 最重要的数据成员 用循环数组实现 是写日志进程与读日志进程共享的缓冲队列 其只在异步日志模式下其作用，因此在init函数里面初始化，如果init里面判断了不开异步日志，就不会初始化阻塞队列
    bool m_is_async;                  //是否同步标志位 默认不开启
    locker m_mutex;                   //日志文件锁 同时只有一个写进程能将自己的日志string串写进日志文件 或者是该类所有数据成员的锁
};

//写日志四种调用
#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, ##__VA_ARGS__)

#endif
