#include <stdio.h>
#include <iostream>
#include <pthread.h>
//懒汉模式：类实例不用的时候不去初始化，只有使用的时候才初始化
// class single{
// private:
//     //私有静态指针变量指向唯一实例
//     static single *p;

//     //静态互斥量锁，是由于静态函数只能访问静态成员
//     static pthread_mutex_t lock;

//     //私有化构造函数
//     single(){
//         pthread_mutex_init(&lock, NULL);//初始化锁
//     }
//     ~single(){}
//     public:
//     //公有静态方法获取实例
//     static single* getinstance();
// };

// pthread_mutex_t single::lock;// 在类体外初始化静态变量 
// single* single::p = NULL;//类静态变量初始化
// single* single::getinstance(){
//     if (NULL == p){
//         pthread_mutex_lock(&lock);
//         if (NULL == p){
//             p = new single;
//         }
//         pthread_mutex_unlock(&lock);
//     }
//     return p;
// }

// /*
// 为什么要用双检测，只检测一次不行吗？

// 在该类未实例化时，如果有两个线程同时调用该类，两个线程都在26行第一个if判空，第一个线程获取27行的锁，第二个线程等待，
// 第一个线程在28行判空，成功创建实例，然后解锁，第二个线程获取锁，进入第二个if发现p不为空，直接解锁
// 获取第一个线程创建的实例

// 创建的这个实例分配在堆内存上，其一个数据成员p指针指向堆内存的位置
// */


// //c++11 之后。 编译器保证类内部静态变量的线程安全性，即如果两个进程同时调用single类的getinstance函数，编译器会自动进行线程异步
// class single{
//  private:
//      single(){}
//      ~single(){}
 
//  public:
//      static single* getinstance();
 
//  };

// single* single::getinstance(){
//     static single obj;//这里的obj是全局变量，并且只初始化一次
//     return &obj;
// }

// //c++11之前写法

// class single {
// private:
//     single() {
//         pthread_mutex_init(&lock,NULL);
//     }
//     ~single() {
//         pthread_mutex_destroy(&lock);
//     }
//     static pthread_mutex_t lock;

// public:
// static single* getinstance() {
//     pthread_mutex_lock(&lock);
//     static single instance;
//     pthread_mutex_unlock(&lock);
//     return &instance;
    
// }
// };
// pthread_mutex_t single::lock;




//饿汉模式
class single{
private:
    static single* p;
    single(){}
    ~single(){}

public:
    static single* getinstance();

};
single* single::p = new single();

single* single::getinstance(){
return p;
}

//测试方法
int main(){

single *p1 = single::getinstance();
single *p2 = single::getinstance();

if (p1 == p2)
    std::cout << "same" << std::endl;

system("pause");
return 0;
}


//饿汉模式虽好，但其存在隐藏的问题，在于非静态对象（函数外的static对象）在不同编译单元中的初始化顺序是未定义的。如果在初始化完成之前调用 getInstance() 方法会返回一个未定义的实例。
//但是一般不会有这种问题，因为类内static变量指针p是在类外初始化的，换言之，是在编译过程中初始化的

//单例模式通常将构造函数设置为私有（private），以防止其他类实例化该单例类。
//将构造函数设置为私有，可以防止其他类通过 new 操作符创建单例类的实例。这样做可以确保单例类只能通过提供的公共接口（如 GetInstance）获取实例，从而控制实例的数量。