#ifndef MEMORYPOOL_H
#define MEMORYPOOL_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/uio.h>
#include <string.h>
#include "../locker.h"
#include <utility>
#include <iostream>

//使用内存池的代码一定要做init_memorypool()初始化操作
//块大小定义
#define BlockSize 4096

struct Slot {
   Slot* next;
};

class MemoryPool {
public:
   MemoryPool();

   ~MemoryPool();

   void init(int size);    //槽大小

   // 分配或收回⼀个元素的内存空间
   Slot* allocate();

   void deAllocate(Slot* p);

private:
   int slotSize_;       // 每个槽所占字节数

   Slot* currentBolck_; // 内存块链表的头指针，其指向第一个可用的块，每当当前块空间用完，就从堆内存重新申请一个新的块，头插到块链表首部

   Slot* currentSlot_; // 当前块可使用slot链表的头指针

   Slot* lastSlot_; // 当前块可使用slot链表的尾指针

   Slot* freeSlot_; // 元素构造后释放掉的slot链表头指针，这条slot链表横跨块与块之间

   locker mutex_freeSlot_;          //空闲链表的锁

   locker mutex_other_;             //其余成员的锁

   size_t padPointer(char* p, size_t align); // 计算对⻬所需空间

   Slot* allocateBlock(); // 从堆申请内存块放进内存池

   Slot* nofree_solve();  //如果allocate时，free链表为空的处理办法
};

//函数声明
void init_MemoryPool();

void* use_Memory(size_t size);

void free_Memory(size_t size, void* p);

//模板函数的声明和实现通常建议放在同一个头文件中

//自定义版本new
template<typename T, typename... Args>
T* newElement(Args&&... args) {
   //std::cout<<"qxutag1"<<std::endl;
   T* p;
   
   if( ( p = reinterpret_cast<T *>( use_Memory(sizeof(T)) ) ) != nullptr ) {
      // new(p) T1(value);
      // placement new:在指针p所指向的内存空间创建⼀个T1类型的对象，类似于realloc
      // 把已有的空间当成⼀个缓冲区来使⽤，减少了分配空间所耗费的时间
      // 因为直接⽤new操作符分配内存的话，在堆中查找⾜够⼤的剩余空间速度是⽐较慢的
      //std::cout<<"qxutag2"<<std::endl;
      new(p) T(std::forward<Args>(args)...); // 完美转发
   }
   return p;
}

// 调⽤p的析构函数，然后将其总内存池中释放
template<typename T>
void deleteElement(T* p) {
   // printf("deleteElement...\n");
   if(p) {
      p->~T();
   }
   free_Memory(sizeof(T), reinterpret_cast<void *>(p));
   // printf("deleteElement success\n");
}

#endif