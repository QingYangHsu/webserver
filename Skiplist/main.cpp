#include <iostream>
#include "skiplist.h"
#define FILE_PATH "./store/dumpFile"

int main() {
    
    // 键值中的key用int型，如果用其他类型，需要自定义比较函数
    // 而且如果修改key的类型，同时需要修改skipList.load_file函数
    
    //SkipList<int, std::string> skipList(6);//定义了一个6层高度的跳表 同时定义了元素key & val类型
    auto &slist = SkipList<int, std::string>::getinst();            //我们用引用类型去接返回值
	slist.insert_element(1, "学"); 
	slist.insert_element(3, "算法"); 
	slist.insert_element(7, "认准"); 
	slist.insert_element(8, "微信公众号：代码随想录"); 
	slist.insert_element(9, "学习"); 
	slist.insert_element(19, "算法不迷路"); 
	slist.insert_element(19, "赶快关注吧你会发现详见很晚！"); 

    std::cout << "skipList size:" << slist.size() << std::endl;

    slist.dump_file();//将当前跳表内容存到指定路径文件

    // skipList.load_file();
    std::string tmpstr = "";
    slist.search_element(9, tmpstr);
    slist.search_element(18, tmpstr);


    slist.display_list();

    slist.delete_element(3);
    slist.delete_element(7);

    std::cout << "skipList size:" << slist.size() << std::endl;

    slist.display_list();
    slist.delete_skiplist();
    std::cout<<"main func exit successfully"<<std::endl;
}

//g++ main.cpp ../memorypool/memorypool.cpp -o main -std=c++11 -lpthread