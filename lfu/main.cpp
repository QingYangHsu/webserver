#include "LFUCache.h"
#include <iostream>
#include <vector>
#include <random>
#include "../memorypool/memorypool.h"

using namespace std;

void test1() {
    //初始化内存池，因为lfucache中的new行为空间申请都走的内存池
    init_MemoryPool();

    LFUCache& Cache =  LFUCache::getCache();
    cout << "the capacity of LFU is : " << Cache.getCapacity() << endl;
    default_random_engine e;
    e.seed(time(NULL));
    uniform_int_distribution<unsigned> u(0, 25);
    vector<pair<string, string>> seed(26);
    for(int i = 0; i < 26; ++i) {
        seed[i] = make_pair('a' + i, 'A' + i);
    }

    for(int i = 0; i < 100; ++i) {
        string val;
        auto temp = seed[u(e)];
        cout << "----------round " << i << "----------" << endl;
        cout << "i want key: " << temp.first << endl;
        if(!Cache.get(temp.first, val)) {
            cout << "cache miss ";
            Cache.set(temp.first, temp.second);
            cout << "insert val..." << endl;
        }
        else {
            cout << "cache hit and value is :" << val << endl;
        }
    }
    std::cout<<"exit successfully"<<std::endl;
}

int main() {
    test1();
    return 0;
}



//g++ *.cpp ../memorypool/memorypool.cpp -o test -std=c++11