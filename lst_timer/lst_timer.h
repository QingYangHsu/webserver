#ifndef LST_TIMER
#define LST_TIMER

#include <stdio.h>
#include <time.h>
#include <arpa/inet.h>
#include "../http/http_conn.h"

#define BUFFER_SIZE 64 //读缓冲大小

class http_conn; //客户类的前向声明 定时器类中有该类的指针

// 定时器类 函数都是类内定义 默认内联
class util_timer {
public:
    util_timer() : next(NULL){}

public:
   time_t expire;   // 任务超时时间，这里使用绝对时间
   http_conn* user_data; //用户信息
   util_timer* next;    // 指向后一个定时器
};

// 定时器链表，它是一个按照超时时间升序、双向链表，且带有头节点和尾节点。
class sort_timer_lst {
public:
    int count;
    sort_timer_lst() : head( NULL ), count(0) {}
    
    // 链表被销毁时，删除其中所有的定时器，即释放链表上所有节点的空间
    ~sort_timer_lst() ;
    
    // 将目标定时器timer添加到链表中
    void add_timer( util_timer* timer ) ;
    
    /* 当某个定时任务发生变化时，调整对应的定时器在链表中的位置。
    即我们接收某用户数据或者向某用户发送数据，超时时间会延后
    这个函数只考虑被调整的定时器的
    超时时间延长的情况，即该定时器需要往链表的尾部移动。*/
    void adjust_timer(util_timer* timer) ;
    // 将目标定时器 timer 从链表中删除 timer指向待删除结点
    void del_timer( util_timer* timer ) ;

    /* SIGALARM 信号每次被触发就在其信号处理函数中执行一次 tick() 函数，以删除链表上到期节点，并且关闭与该客户的连接*/
    void tick() ;
    util_timer* gethead() { return head;}


private:
    /* 一个重载的辅助函数，它被公有的 add_timer 函数和 adjust_timer 函数调用
    此时timer超时时间大于lst_head指向定时器超时时间
    该函数表示将目标定时器 timer 添加到节点 lst_head 之后的合适位置中 */
    void add_timer(util_timer* timer, util_timer* lst_head) ;

private:
    util_timer* head;   // 头结点
};

#endif