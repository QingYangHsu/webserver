#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <pthread.h>
#include <list>
#include "locker.h"
#include <exception>
#include <cstdio>


//线程池类 定义成模板类是为了代码复用，模板参数T是任务类
template<typename T>
class threadpool {
public:
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    bool append(T *reauest);//当有一个任务到来，需要向请求队列添加任务


private:
    //在c++中，线程主函数必须是static
    //在c++中，静态函数成员 可以直接访问类的静态成员变量，但不能访问非静态成员变量，也不能使用this指针
    static void* worker(void *arg);
    
    //线程池启动起来
    void run();



private:

    //线程数量
    int m_thread_number;

    //线程池数组 动态创建
    pthread_t * m_threads;

    //请求队列中年最多允许的，等待处理的请求数量 即请求队列最大容量
    int m_max_requests;

    //请求队列
    std::list< T*> m_workqueue;

    //互斥锁(用于各个线程共享的请求队列)
    locker m_queuelocker;

    //信号量 用来判断是否有任务需要处理(回想一下生产者消费者模型 使用信号量来避免死锁)
    sem m_queuestat;

    //是否结束线程 默认为false 不结束线程
    bool m_stop;

};



//线程池类构造函数
template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests) :
    m_thread_number(thread_number), m_max_requests(max_requests), 
    m_stop(false), m_threads(NULL) {
        if(thread_number <=0 || max_requests <= 0) {
            throw std::exception();
        }

        m_threads = new pthread_t[m_thread_number];
        if(!m_thread_number) {
            throw std::exception();
        }

        //创建m_thread_number个线程，并设置线程脱离，因为我们不可能让主线程去回收子线程资源 所以设置线程脱离，让子线程结束自己回收资源
        //在C++中，对指针变量执行"++"操作符会导致指针向前移动一个元素的大小。具体移动的字节数取决于指针所指向的类型。
        for(int i = 0; i<m_thread_number; i++) {
            printf("create the %dth thread\n", i);
            // 将this指针作为参数传入静态线程主函数，这样线程主函数中就能使用类的非静态成员
            if(pthread_create(m_threads+i, NULL, worker, this) != 0) { //worker是线程主函数
                delete[] m_threads;
                throw std::exception();
            }
            if( pthread_detach(m_threads[i]) ) {
                delete[] m_threads;
                throw std::exception();
            } 

        }
    }

template<typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
    m_stop = true;

}

//因为请求队列为所有线程所共享，所以为同步资源 所以更新请求队列要注意线程同步
template<typename T>
bool threadpool<T>::append(T *request) {
    //lock
    m_queuelocker.lock();
    //当前请求队列的数据容量大于最大请求数
    if(m_workqueue.size() > m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    //unlock
    m_queuelocker.unlock();
    //信号量加一 提醒各线程请求队列中数目加了一个 休眠的八个线程会苏醒一个来进行逻辑处理
    m_queuestat.post();
    return true;
}

//线程主函数
template<typename T>
void* threadpool<T>::worker(void *arg) {
    threadpool *pool = (threadpool *)arg;
    pool->run();
    //这个返回值没什么用
    return pool;
}


template<typename T>
void threadpool<T>::run() {
    //直到m_stop为false 才让线程停止
    while(!m_stop) {
        //类似于生产者消费者代码，如果请求队列不为空，则不会阻塞，并将信号量减一 如果为空，则一直在这里阻塞
        m_queuestat.wait(); //m_queuestat是信号量 所以线程刚一启动会在这里阻塞 因为信号量初值为0
        m_queuelocker.lock();
        //判断请求队列是否为空 
        if(m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        //如果没获取到，继续continue
        if(!request) {
            continue;
        }
        //子线程从链式请求队列首部取得一个任务之后，调用任务类的执行程序进行逻辑处理
        request->process();
    }
}

//append 与 run就相当于生产者消费者程序中的生产者线程主函数producer
//与消费者线程主函数customer,两者通过条件变量协调线程同步

#endif