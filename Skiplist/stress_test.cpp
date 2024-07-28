//压测
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <pthread.h>
#include <time.h>
#include "skiplist.h"

#define NUM_THREADS 1 //最大线程数
#define TEST_COUNT 100000//测试次数             工作线程把这些次数分掉

auto &sl = SkipList<int, std::string>::getinst();  

void *insertElement(void* threadid) {//线程主函数 用来测量跳表插入性能
    long tid; 
    tid = (long)threadid;
    std::cout << tid << std::endl;  //tid标识了这是第几个线程
    int tmp = TEST_COUNT/NUM_THREADS; //所有线程分摊测试总数
	for (int i=tid*tmp, count=0; count<tmp; i++) {
        count++;
		sl.insert_element(rand() % TEST_COUNT, "a"); 
	}
    pthread_exit(NULL);
}

void *getElement(void* threadid) {//这个函数也可作为线程主函数，用来测量跳表查询性能
    long tid; 
    tid = (long)threadid;
    std::cout << tid << std::endl;  
    int tmp = TEST_COUNT/NUM_THREADS; 
    std::string str = "";
	for (int count=0; count<tmp; ) {
        count++;
		sl.search_element(rand() % TEST_COUNT, str); 
	}
    pthread_exit(NULL);
}

int main() {
    srand (time(NULL));  //使用当前时间time(NULL)初始化随机数生成器  
    {

        pthread_t threads[NUM_THREADS];
        int rc;
        int i;

        auto start = std::chrono::high_resolution_clock::now();             //使用C++的chrono库来获取当前时间,作为开始值

        for( i = 0; i < NUM_THREADS; i++ ) {
            std::cout << "main() : creating thread, " << i << std::endl;
            rc = pthread_create(&threads[i], NULL, insertElement, (void *)i);       //成功返回0 失败返回errno

            if (rc) {
                std::cout << "Error:unable to create thread," << rc << std::endl;
                exit(-1);
            }
        }

        void *ret;
        for( i = 0; i < NUM_THREADS; i++ ) {
            if (pthread_join(threads[i], &ret) !=0 )  {//回收子线程资源，ret存储线程主函数返回值 该函数成功返回0 失败返回errno
                perror("pthread_create() error"); 
                exit(3);
            }
        }
        auto finish = std::chrono::high_resolution_clock::now(); ////使用C++的chrono库来获取当前时间，作为结束值
        std::chrono::duration<double> elapsed = finish - start;
        std::cout << "insert elapsed:" << elapsed.count() << std::endl;//通过该值可以评估跳表插入性能
    }
    sl.display_list();

    {
        pthread_t threads[NUM_THREADS];
        int rc;
        int i;
        auto start = std::chrono::high_resolution_clock::now();

        for( i = 0; i < NUM_THREADS; i++ ) {
            std::cout << "main() : creating thread, " << i << std::endl;
            rc = pthread_create(&threads[i], NULL, getElement, (void *)i);

            if (rc) {
                std::cout << "Error:unable to create thread," << rc << std::endl;
                exit(-1);
            }
        }

        void *ret;
        for( i = 0; i < NUM_THREADS; i++ ) {
            if (pthread_join(threads[i], &ret) !=0 )  {
                perror("pthread_create() error"); 
                exit(3);
            }
        }

        auto finish = std::chrono::high_resolution_clock::now(); 
        std::chrono::duration<double> elapsed = finish - start;
        std::cout << "get elapsed:" << elapsed.count() << std::endl;
    }

	pthread_exit(NULL);//主线程退出
    return 0;

}

//g++ stress_test.cpp ../memorypool/memorypool.cpp -o stress  --std=c++11 -lpthread