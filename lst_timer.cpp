#include "lst_timer.h"
#include "log/log.h"


sort_timer_lst::~sort_timer_lst() {
    util_timer* tmp = head;
    while( tmp ) {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

void sort_timer_lst::add_timer( util_timer* timer ) {
    if( !timer ) {
        return;
    }
    count++;
    if( !head && !tail) {//即当前链表为空
        LOG_DEBUG("%s","当前链表为空");
        head = tail = timer;
        return; 
    }
    /* 如果目标定时器的超时时间小于当前链表中所有定时器的超时时间，则把该定时器插入链表头部,作为链表新的头节点，
        否则就需要调用私有重载函数 add_timer(),把它插入链表中合适的位置，以保证链表的升序特性 */
    if( timer->expire < head->expire ) {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);
}

void sort_timer_lst::adjust_timer(util_timer* timer) {//timer指向链表中的一个节点
        if( !timer )  {
            return;
        }
        util_timer* tmp = timer->next;
        // 如果被调整的目标定时器处在链表的尾部，或者该定时器新的超时时间值仍然小于其下一个定时器的超时时间则不用调整
        if( !tmp || ( timer->expire < tmp->expire ) ) {
            return;
        }
        // 如果目标定时器是链表的头节点，则将该定时器从链表中取出并重新插入链表
        if( timer == head ) {
            head = head->next;
            head->prev = NULL;
            timer->next = NULL;//timer作为旧头结点和新头结点断开
            add_timer( timer, head );
        } else {
            // 如果目标定时器不是链表的头节点，则将该定时器从链表中取出，然后插入其原来所在位置后的部分链表中
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer( timer, timer->next );
        }
}
void sort_timer_lst::del_timer(util_timer* timer) {
        if( !timer ) {
            return;
        }
        count--;
        // 链表中只有一个定时器，即目标定时器
        if( ( timer == head ) && ( timer == tail ) ) {
            LOG_DEBUG("%s","当前链表只有一个节点");
            delete timer;
            head = NULL;
            tail = NULL;
            return;
        }
        /* 如果链表中至少有两个定时器，且目标定时器是链表的头节点，
         则将链表的头节点重置为原头节点的下一个节点，然后删除目标定时器。 */
        if( timer == head ) {
            head = head->next;
            head->prev = NULL;
            delete timer;
            return;
        }
        /* 如果链表中至少有两个定时器，且目标定时器是链表的尾节点，
        则将链表的尾节点重置为原尾节点的前一个节点，然后删除目标定时器。*/
        if( timer == tail ) {
            tail = tail->prev;
            tail->next = NULL;
            delete timer;
            return;
        }
        // 如果目标定时器位于链表的中间，则把它前后的定时器串联起来，然后删除目标定时器
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
}

void sort_timer_lst::tick() {
    if( !head ) {
        return;
    }
    LOG_DEBUG("%s","timer tick");
    time_t cur = time( NULL );  // 获取当前系统时间
    util_timer* tmp = head;
    // 从头节点开始依次处理每个定时器，直到遇到一个尚未到期的定时器
    while( tmp ) {
        /* 因为每个定时器都使用绝对时间作为超时值，所以可以把定时器的超时值和系统当前时间，
        比较以判断定时器是否到期*/
        if( cur < tmp->expire ) {//若系统时间小于该定时器设置的超时时间 说明还没超时，则后面因为是升序，也必然不可能超时
            break;
        }

        // 执行删除，删除该客户的注册事件并且关闭该客户连接=
        tmp->user_data->close_conn();
        // 执行完定时器中的定时任务之后，就将它从链表中删除，并重置链表头节点
        head = tmp->next;
        if( head ) {
            head->prev = NULL;
        }
        delete tmp;//删除旧结点
        count--;
        tmp = head;//下一轮从新头结点开始遍历
    }
}

void sort_timer_lst::add_timer(util_timer* timer, util_timer* lst_head)  {//head是起始点
        util_timer* prev = lst_head;
        util_timer* tmp = prev->next;
        /* 遍历 list_head 节点之后的部分链表，直到找到一个超时时间大于目标定时器的超时时间节点
        并将目标定时器插入该节点之前 */
        while(tmp) {
            if( timer->expire < tmp->expire ) {//找到了 在prev与tmp之前插入timer
                prev->next = timer;
                timer->next = tmp;
                tmp->prev = timer;
                timer->prev = prev;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        /* 如果遍历完 lst_head 节点之后的部分链表，仍未找到超时时间大于目标定时器的超时时间的节点，
        	此时prev指向旧链表尾部，tmp指向NULL
           则将目标定时器插入链表尾部，并把它设置为链表新的尾节点。*/
        if( !tmp ) {
            prev->next = timer;
            timer->prev = prev;
            timer->next = NULL;//封尾
            tail = timer;
        }
}