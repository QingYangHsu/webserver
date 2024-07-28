#include "memorypool.h"



MemoryPool& get_MemoryPool(int id);

MemoryPool::MemoryPool():mutex_freeSlot_(), mutex_other_() {
}


MemoryPool::~MemoryPool() {
   Slot* cur = currentBolck_;
   while(cur) {
      Slot* next = cur->next;
      // free(reinterpret_cast<void *>(cur));
      // 转化为 void 指针，是因为 void 类型不需要调⽤析构函数,只释放空间
      //将这片块空间还给堆区
      operator delete(reinterpret_cast<void*>(cur));
      cur = next;
   }
}

void MemoryPool::init(int size) {
   assert(size > 0);
   //std::cout<<"size = "<<size<<std::endl;
   //if (size == 0) {std::cout<<"bad error"<<std::endl;}
   slotSize_ = size;
   currentBolck_ = NULL;
   currentSlot_ = NULL;
   lastSlot_ = NULL;
   freeSlot_ = NULL;
}

// 计算对⻬所需补的空间
inline size_t MemoryPool::padPointer(char* p, size_t align) {
   //std::cout<<"tag7"<<std::endl;
   size_t result = reinterpret_cast<size_t>(p);
   //std::cout<<"align:"<<align<<std::endl;
   //std::cout<<"tag8"<<std::endl;
   //std::cout<<align<<","<<result<<std::endl;
   return ((align - result) % align);
}

Slot* MemoryPool::allocateBlock() {
   //std::cout<<"tag6"<<std::endl;
   void *addr = operator new(BlockSize);
   //std::cout<<addr<<std::endl;
   char* newBlock = reinterpret_cast<char *>(addr);
   //std::cout<<&newBlock<<":"<<std::endl;
   //std::cout<<newBlock<<std::endl;
   char* body = newBlock + sizeof(Slot*);
   //std::cout<<&body<<std::endl;
   //std::cout<<newBlock + sizeof(Slot*)<<std::endl;
   //std::cout<<"tag6-1"<<std::endl;
   //std::cout<<body<<","<<slotSize_<<std::endl;
   // 计算为了对⻬需要空出多少位置
   //std::cout<<"slotSize_:"<<slotSize_<<std::endl;
   size_t bodyPadding = padPointer(body, static_cast<size_t>(slotSize_));
   //std::cout<<"bodypadding"<<bodyPadding<<std::endl;
   // 注意：线程同步问题
   //std::cout<<"tag1"<<std::endl;
   Slot* useSlot;
   {
      //MutexLockGuard lock(mutex_other_);
      mutex_other_.lock();

      // newBlock接到Block链表的头部
      reinterpret_cast<Slot *>(newBlock)->next = currentBolck_;
      currentBolck_ = reinterpret_cast<Slot *>(newBlock);
      
      // 为该Block开始的地⽅加上bodyPadding个char* 空间
      //更新currentSlot_ lastSlot_指针
      currentSlot_ = reinterpret_cast<Slot *>(body + bodyPadding);
      lastSlot_ = reinterpret_cast<Slot *>(newBlock + BlockSize - slotSize_ + 1);

      useSlot = currentSlot_;

      // slot指针⼀次移动8个字节?
      currentSlot_ += (slotSize_ >> 3);
      mutex_other_.unlock();
   }
   
   return useSlot;
}

Slot* MemoryPool::nofree_solve() {
   if(currentSlot_ >= lastSlot_) {
      //std::cout<<"qxutag6666"<<std::endl;
      return allocateBlock();
   }
   Slot* useSlot;
   {
      //MutexLockGuard lock(mutex_other_);
      mutex_other_.lock();
      useSlot = currentSlot_;
      currentSlot_ += (slotSize_ >> 3); //?
      mutex_other_.unlock();
   }
   return useSlot;
}

Slot* MemoryPool::allocate() {
   //std::cout<<"tag4"<<std::endl;

   if(freeSlot_) {
      {
         //MutexLockGuard lock(mutex_freeSlot_);
         mutex_freeSlot_.lock();
         if(freeSlot_) {
            Slot* result = freeSlot_;
            freeSlot_ = freeSlot_->next;
            mutex_freeSlot_.unlock();
            return result;
         }
         mutex_freeSlot_.unlock();
      }
   }
   //std::cout<<"tag5"<<std::endl;
   return nofree_solve();
}

inline void MemoryPool::deAllocate(Slot* p) {
   if(p) {
      // 将slot加⼊释放队列
      mutex_freeSlot_.lock();
      //MutexLockGuard lock(mutex_freeSlot_);
      p->next = freeSlot_;
      freeSlot_ = p;
      mutex_freeSlot_.unlock();
   }
}

MemoryPool& get_MemoryPool(int id) {
   static MemoryPool memorypool_[64];
   return memorypool_[id];
}

// 数组中分别存放Slot⼤⼩为8，16，...，512字节的内存池
void init_MemoryPool() {
   for(int i = 0; i < 64; i++) {
      get_MemoryPool(i).init((i + 1) << 3);
   }
}

// 申请size大小的空间 
// 超过512字节就直接从堆区new
// 没到512字节，就根据申请大小找到对应的内存池去分配一个固定大小的空间
void* use_Memory(size_t size) {
   //std::cout<<"qxutag1-1"<<std::endl;
   if(!size) {
      //std::cout<<"qxutag1-2"<<std::endl;
      return nullptr;
   }
   if(size > 512) {
      //std::cout<<"qxutag1-3"<<std::endl;
      return operator new(size);
   }

   // 相当于(size / 8)向上取整
   //std::cout<<"qxutag1-4"<<std::endl;
   //std::cout<<"size:"<<size<<std::endl;
   //std::cout<<((size + 7) >> 3) - 1 << std::endl;
   return reinterpret_cast<void *>(get_MemoryPool( ((size + 7) >> 3) - 1 ).allocate());
}

//回收p所在的size大小的空间
void free_Memory(size_t size, void* p) {
   if(!p) return;
   if(size > 512) {
      operator delete (p);
      return;
   }
   get_MemoryPool(((size + 7) >> 3) - 1).deAllocate(reinterpret_cast<Slot *>(p));
}