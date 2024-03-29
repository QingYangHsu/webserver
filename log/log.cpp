#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

Log::Log()//构造函数
{
    m_count = 0;
    m_is_async = false;//默认不使用同步 使用同步即意味着日志string不是写入缓冲队列，等消费者来取，而是直接写入文件，这样做cpu等待时间会很长
}

Log::~Log()//析构函数
{
    if (m_fp != NULL)
    {
        fclose(m_fp);//关闭打开的文件指针
    }
}
//异步需要设置阻塞队列的长度，同步不需要设置（关键） 因为异步是将日志暂存到阻塞队列中，之后会有进程从中提取日志写入日志文件
bool Log::init(const char *file_name, int log_buf_size, int split_lines, int max_queue_size)
{
    //如果设置了max_queue_size,则设置为异步 因为异步的关键就是暂存
    if (max_queue_size >= 1)
    {
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        //flush_log_thread为线程主函数 这里只创建了一个子线程，该子线程主要工作就是清缓冲，而且是不停地清缓冲直到缓冲为空，因此只需要一个消费者线程
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_log_buf_size = log_buf_size;//一条日志最大字节数
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;//一个日志文件最大条数

    time_t t = time(NULL);// 会获取当前的时间（以从1970年1月1日开始的秒数来表示）
    struct tm *sys_tm = localtime(&t);// 将上述得到的时间转换为本地时间 struct tm结构体描述了日期和时间的各个部分，如年、月、日、时、分、秒等
    struct tm my_tm = *sys_tm;

 
    const char *p = strrchr(file_name, '/');//用于在一个字符串中查找特定字符或子字符串最后一次出现的位置。如果未找到，返回NULL
    char log_full_name[256] = {0};//日志文件全名

    if (p == NULL)//走这个分支
    {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);//年 月 日 文件名 比如2023_11_23_serverlog
    }
    else//一般来说,这个分支的目的在于将日志文件保存到一个特定路径 而不是相对路径
    {
        strcpy(log_name, p + 1);//log_name为log文件名
        strncpy(dir_name, file_name, p - file_name + 1);//dir_name为路径名
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);//做拼接：路径名 年 月 日 文件名 
    }

    m_today = my_tm.tm_mday;

    m_fp = fopen(log_full_name, "a");//打开一个已有的文本文件，以便在末尾追加数据。如果文件不存在，则会创建一个新文件。
    if (m_fp == NULL)
    {
        return false;
    }

    return true;
}

void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};//本地buff 存日志等级
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }
    //写入一个log，对m_count++, m_split_lines最大行数
    m_mutex.lock();//m_count m_today m_fp是临界资源 要加锁
    m_count++;//日志行数++

    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) //everyday log init的时候对m_today用my_tm.tm_mday做了初始化
    {//这个判断表示文件该更新了 如果现在写日志的日期与做初始化的日期不一样或者当前文件已满
        
        char new_log[256] = {0};//新文件名
        fflush(m_fp);//将旧文件内容刷入磁盘
        fclose(m_fp);//关闭旧文件
        char tail[16] = {0};
       
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);//年月日
        //分别对两种情况做处理
        if (m_today != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);//路径名 年月日 日志文件名
            m_today = my_tm.tm_mday;//更改日期
            m_count = 0;//日志条数清零
        }
        else
        {
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);//路径名 年月日 日志文件名 新条数
        }
        m_fp = fopen(new_log, "a");//打开新日志 更新文件指针
    }
    
    m_mutex.unlock();//解锁

    va_list valst;
    va_start(valst, format);

    string log_str;//实际完整的一条日志内容
    m_mutex.lock();//上锁 因为m_buf是临界资源

    //写入的具体时间内容格式
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);//年月日时分秒微妙 日志等级
    //返回实际写入字节数
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);//str是目标字符串，size是目标字符串的最大长度，format是格式化字符串，args是可变参数列表。
    m_buf[n + m] = '\n';//换行符
    m_buf[n + m + 1] = '\0';//结束符
    log_str = m_buf;

    m_mutex.unlock();
    //接下来做协议选择
    if (m_is_async && !m_log_queue->full())//异步 并且缓冲区不为空
    {
        m_log_queue->push(log_str);
    }
    else//同步
    {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);//直接写入文件m_fp
        m_mutex.unlock();
    }

    va_end(valst);
}

void Log::flush(void)
{
    m_mutex.lock();
    //强制刷新写入的日志文件
    fflush(m_fp);
    m_mutex.unlock();
}
