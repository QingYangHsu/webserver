#ifndef LOKER_H
#define LOKER_H
#include <pthread.h>
#include <exception>
#include <semaphore.h>

//线程同步机制封装类
//互斥锁类

//管理临界区资源 条件变量类



//锁类
class locker {
public:
    // 默认构造函数
    locker() {
        if(pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
    }

    // 析构函数
    ~locker() {
        pthread_mutex_destroy(&m_mutex);
    }

    // 上锁
    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    // unlock
    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    // get private num:mutex
    pthread_mutex_t* get() {
        return &m_mutex;
    }


private:
    // 一个互斥量
    pthread_mutex_t m_mutex; 
};


//条件变量类
class cond {
public:
    cond() {
        if(pthread_cond_init(&m_cond, NULL) != 0) {
            throw std::exception();
        }
    }
    ~cond() {
        pthread_cond_destroy(&m_cond);
    }

    // 阻塞，调用了该函数，线程会阻塞，并且释放锁
    bool wait(pthread_mutex_t *mutex) {
        return pthread_cond_wait(&m_cond, mutex) == 0;
    }

    // 阻塞多长时间，调用该函数，线程会阻塞并占用锁，直到指定的时间结束并且释放锁
    bool timewait(pthread_mutex_t *mutex, struct timespec t) {
        return pthread_cond_timedwait(&m_cond, mutex, &t) == 0;
    }

    // 唤醒一个或者多个等待的线程
    bool signal(pthread_mutex_t *mutex) {
        return pthread_cond_signal(&m_cond) == 0;
    }

    // 唤醒所有等待的线程
    bool broadcast() {
       return pthread_cond_broadcast(&m_cond) == 0;
    }


private:
    pthread_cond_t m_cond;


};


//信号量类
class sem {
public:
    sem() {
        //信号量初值定为0 表示请求队列刚开始零个物品  
        if(sem_init(&m_sem, 0, 0) != 0) {
            throw std::exception();
        }
    }
    sem(int num) {
        if(sem_init(&m_sem, 0, num) != 0) {
            throw std::exception();
        }
    }
    ~sem() {
        sem_destroy(&m_sem);
    }

    //lock
    int wait() {
        return sem_wait(&m_sem);
    }


    //unlock
    int post() {
        return sem_post(&m_sem);
    }

private:
    sem_t m_sem;



};


#endif