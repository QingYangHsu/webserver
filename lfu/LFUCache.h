#ifndef LFUCACHE_H
#define LFU_CACHE_H

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
#include <iostream>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/uio.h>
#include <string.h>
#include <unordered_map>
#include "../locker.h"



// LFUCache.h
#define LFU_CAPACITY 10

using namespace std;

// 链表的节点 一级链表和二级链表都使用该模板Node
template<typename T>
class Node {
public:
    T value;
    Node* pre;
    Node* next;
};

// ⽂件名->⽂件内容的映射
// 因为文件内容我们使用内存映射的方式 所以就是一个string串
struct Key {
    string key_, value_;
};

//定义二级链表节点
typedef Node<Key>* key_node;

class KeyList {
public:
    //初始化一条链表 并且设置该链表的频率
    void init(int freq);

    //析构并回收一整条链表
    void destory();
    
    void add(key_node& node);
    
    void del(key_node& node);
    
    bool isEmpty();
    
    int freq;          //命中次数

    key_node tail_;

    key_node Dummyhead_;
};

//定义一级链表节点
typedef Node<KeyList>* freq_node;

/*
典型的双重链表+hash表实现
⾸先是⼀个⼤链表(一级链表)，链表的每个节点都是⼀个⼩链表(二级链表),二级链表附带⼀个值表示频度
⼩链表存的是同⼀频度下的key value节点。
hash表存key到⼤链表节点的映射(key,freq_node)和
key到⼩链表节点的映射(key,key_node).
*/

// LFU由多个二级链表组成
class LFUCache {
public:
    freq_node Dummyhead_; // 一级链表的伪头节点，⾥⾯每个节点都是二级链表的头节点
    
    size_t capacity_;
    locker mutex_lock;
    unordered_map<string, key_node> kmap_; // key到keynode(二级节点)的映射
    unordered_map<string, freq_node> fmap_; // key到freqnode(一级节点)的映射
    
    void addFreq(key_node& nowk, freq_node& nowf);
    void del(freq_node& node);
    void init();

    static LFUCache& getCache() {           //单例模式 懒汉
        static LFUCache cache(LFU_CAPACITY);
        return cache;
    }          
    ~LFUCache();
    bool get(string& key, string& value); // 通过key返回value并进⾏LFU操作
    void set(string& key, string& value); // 更新LFU缓存
    size_t getCapacity() const { 
        return capacity_; 
    }
private:
    LFUCache(int capicity);     //私有构造函数
};



#endif