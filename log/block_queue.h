/*************************************************************
*循环数组实现的阻塞队列，m_back = (m_back + 1) % m_max_size;  
*线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
**************************************************************/

#ifndef BLOCK_QUEUE_H 
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../locker.h"
using namespace std;

template <class T>//类模板
class block_queue//阻塞队列类 主要是用一个循环数组实现队列queue
{
public:
    block_queue(int max_size = 1000)//构造函数
    {
        if (max_size <= 0)
        {
            exit(-1);
        }

        m_max_size = max_size;
        m_array = new T[max_size];//一开始 T数组已经开好了 只不过里面的字段都是默认值 这片内存开在堆上
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    void clear()//不用管数组中存储的旧值 新值会重写覆盖原来的旧值
    {
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }

    ~block_queue()
    {
        m_mutex.lock();
        if (m_array != NULL)
            delete [] m_array;//删除堆上开辟的空间

        m_mutex.unlock();
    }
    //判断队列是否满了
    bool full() //这里加锁解锁好像无必要
    {
        m_mutex.lock();
        if (m_size >= m_max_size)
        {

            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    //判断队列是否为空
    bool empty() 
    {
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    //返回队首元素 将其保存在value中 返回值true表示返回成功 false表示返回失败
    bool front(T &value) 
    {
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_front];
        m_mutex.unlock();
        return true;
    }
    //返回队尾元素 将其保存在value中 返回值true表示返回成功 false表示返回失败
    bool back(T &value) 
    {
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }

    int size() //这里有无加锁必要？
    {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_size;

        m_mutex.unlock();
        return tmp;
    }

    int max_size()//这里有无加锁必要？
    {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_max_size;

        m_mutex.unlock();
        return tmp;
    }
    //往队列添加元素，需要将所有使用队列的线程先唤醒
    //当有元素push进队列,相当于生产者生产了一个元素
    //若当前没有线程等待条件变量,则唤醒无意义
    bool push(const T &item)//返回值：true表示push成功 false表示push失败
    {

        m_mutex.lock();
        if (m_size >= m_max_size)//如果当前实际容量已经等于最大容量(不可能大于)，则唤醒该条件变量队列上休眠的所有进程(当然主要是想唤醒消费者)，赶紧pop出去一个，挪位置
        {

            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }

        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;

        m_size++;

        m_cond.broadcast();//唤醒该条件变量队列上休眠的所有人
        m_mutex.unlock();
        return true;
    }
    //pop时,如果当前队列没有元素,将会等待条件变量
    bool pop(T &item)
    {

        m_mutex.lock();
        while (m_size <= 0)//如果当前实际容量已经等于0(不可能小于)，则休眠，其实还可以唤醒该条件变量队列上休眠的所有进程(当然主要是想唤醒生产者)，赶紧生产一个
        {                  //这里进程在156休眠被唤醒了之后，发现m_size大于0，就会跳出循环 
            
            if (!m_cond.wait(m_mutex.get()))//该进程主动休眠，并将刚刚获得的锁交还 成功返回true 失败返回false
            {
                m_mutex.unlock();//如果失败 说明进程休眠并且释放锁失败 手动释放锁 进程退出
                return false;
            }
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

    //增加了超时处理 如果此时队列为空，给了一个ms_timeout的缓冲时间，来等待生产者 返回值，成功pop为true 失败为false
    bool pop(T &item, int ms_timeout)//传入参数是毫秒 
    {
        struct timespec t = {0, 0};//这个结构体存的是截止时间
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);
        m_mutex.lock();
        if (m_size <= 0)//如果当前实际容量已经等于0(不可能小于)，则休眠，
        {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if (!m_cond.timewait(m_mutex.get(), t))//在时间t之内休眠并持有锁，如果超过时间唤醒事件未发生则休眠并且释放锁，返回false 如果等待时间内有一个进程(push进程)发出signal信号，就返回true
            {
                m_mutex.unlock();//无必要
                return false;//表示pop失败
            }
        }

        if (m_size <= 0)//再做一次保险判断 因为有可能有两个消费者同时在timewait，但是只有一个生产者生产了一个物品。另一个消费者消费了，m_size又变为0，所以本消费者在这里做一下判断
        {
            m_mutex.unlock();//这里十分有必要
            return false;
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

private:
    locker m_mutex;//天啊 这样写简直是有病 locker类里面最有用的就是互斥锁pthread_mutex_t m_mutex;tmd为啥还要再封装一个类？
    cond m_cond;

    T * m_array;//m_array指向一片连续分配的T类型的空间，即一维T数组的数组名，或者数组首地址
    int m_size;
    int m_max_size;
    int m_front;//指向队首元素的前一位
    int m_back;//指向队尾元素
};

#endif
