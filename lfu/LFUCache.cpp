#include "LFUCache.h"
#include "../memorypool/memorypool.h"

void KeyList::init(int freq) {
    this->freq = freq;
    Dummyhead_ = newElement<Node<Key>>();
    tail_ = Dummyhead_;
    Dummyhead_->next = nullptr;
}

// 删除整个链表
void KeyList::destory() {
    while(Dummyhead_ != nullptr) {
        key_node pre = Dummyhead_;
        Dummyhead_ = Dummyhead_->next;
        deleteElement(pre);
    }
}

// 将节点添加到链表头部

void KeyList::add(key_node& node) {
    if(Dummyhead_->next == nullptr) {
        tail_ = node;
    }
    else {
        Dummyhead_->next->pre = node;
    }
    node->next = Dummyhead_->next;
    node->pre = Dummyhead_;
    Dummyhead_->next = node;
    assert(!isEmpty());
}


// 删除⼩链表中的节点
void KeyList::del(key_node& node) {
    node->pre->next = node->next;
    if(node->next == nullptr) {
        tail_ = node->pre;
    }
    else {
        node->next->pre = node->pre;
    }
}


bool KeyList::isEmpty() {
    return Dummyhead_ == tail_;
}

LFUCache::LFUCache(int capacity) : capacity_(capacity),mutex_lock() {
    init();
}

LFUCache::~LFUCache() {
    std::cout<<"lfucache析构函数调用"<<std::endl;
    while(Dummyhead_) {
        freq_node pre = Dummyhead_;
        Dummyhead_ = Dummyhead_->next;
        //pre->getValue()是keylist类型
        pre->value.destory();      //删除整个二级链表
        deleteElement(pre);              //析构该节点，并释放这个节点空间
    }
}

void LFUCache::init() {
    // FIXME:缓存的容量动态变化

    // Dummyhead_ = new Node<KeyList>();
    Dummyhead_ = newElement<Node<KeyList>>();
    
    //伪节点对应链表频率设置为0
    Dummyhead_->value.init(0);
    Dummyhead_->next = nullptr;
}


// 更新节点频度：
// 如果不存在下⼀个频度的链表，则增加⼀个
// 然后将当前节点放到下⼀个频度的链表的头位置
void LFUCache::addFreq(key_node& nowk, freq_node& nowf) {
    freq_node nxt;
    if(nowf->next== nullptr || nowf->next->value.freq != nowf->value.freq + 1) {
        // 新建⼀个下⼀频度的⼤链表,加到nowf后⾯
        nxt = newElement<Node<KeyList>>();
        nxt->value.init(nowf->value.freq + 1);
        if(nowf->next != nullptr) {
            nowf->next->pre = nxt;
        }
        nxt->next = nowf->next;
        nowf->next = nxt;
        nxt->pre = nowf;
    }
    else {
        nxt = nowf->next;
    }

    fmap_[nowk->value.key_] = nxt;

    // 将其从上⼀频度的⼩链表删除
    // 然后加到下⼀频度的⼩链表中
    if(nowf != Dummyhead_) {
        nowf->value.del(nowk);
    }
    nxt->value.add(nowk);
    assert(!nxt->value.isEmpty());

    // 如果旧频度的⼩链表在删除完已经空了
    if(nowf != Dummyhead_ && nowf->value.isEmpty()) {
        del(nowf);
    }
}

//成功为true 失败为false
bool LFUCache::get(string& key, string& val) {
    if(!capacity_) return false;
    mutex_lock.lock();
    if(fmap_.find(key) != fmap_.end()) {
        // 缓存命中
        key_node nowk = kmap_[key];
        freq_node nowf = fmap_[key];
        val = nowk->value.value_;
        addFreq(nowk, nowf);
        mutex_lock.unlock();
        return true;
    }
    // 未命中
    mutex_lock.unlock();
    return false;
}

//一定成功
void LFUCache::set(string& key, string& val) {
    if(!capacity_) return;
    mutex_lock.lock();
    // 缓存满了
    // 从频度最⼩的⼩链表中的节点中删除最后⼀个节点（⼩链表中的删除符合LRU）
    if(kmap_.size() == capacity_) {
        freq_node head_list = Dummyhead_->next;
        key_node last = head_list->value.tail_;
        head_list->value.del(last);
        kmap_.erase(last->value.key_);
        fmap_.erase(last->value.key_);
        deleteElement(last);

        // 如果频度最⼩的链表已经没有节点，就删除
        if(head_list->value.isEmpty()) {
            del(head_list);
        }
    }

    // 使⽤内存池
    key_node nowk = newElement<Node<Key>>();

    nowk->value.key_ = key;
    nowk->value.value_ = val;
    addFreq(nowk, Dummyhead_);
    kmap_[key] = nowk;
    fmap_[key] = Dummyhead_->next;
    mutex_lock.unlock();
}

//一级链表删除一个节点
void LFUCache::del(freq_node& node) {
    node->pre->next = node->next;
    if(node->next != nullptr) {
        node->next->pre = node->pre;
    }
    node->value.destory();
    deleteElement(node);
 
}