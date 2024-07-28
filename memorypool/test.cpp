#include "memorypool.h"
#include <memory>
#include <iostream>

using namespace std;

template<typename data>
class Person {
public:
    int id_;
    std::string name_;
    data key;
public:
    Person(int id, std::string name, data da) : id_(id), name_(name), key(da) {
        printf("构造函数调⽤\n");
    }
    ~Person() {
        printf("析构函数调⽤\n");
    }
};

void test01() {
    printf("creating a person\n");
    //cout<<"tag1"<<endl;
    //初始化shared_ptr时可自制定删除行为
    init_MemoryPool();
    //init_MemoryPool<Person>();
    shared_ptr<Person<int>> p1(newElement<Person<int>>(11, "Lawson",1224), deleteElement<Person<int>>);
    printf("sizeof(name_) = %d\n", sizeof(p1->name_));
    printf("sizeof(Person) = %d\n", sizeof(Person<int>));
}


int main() {
    test01();
    //cout<<"tag2"<<endl;
    return 0;
}