#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../locker.h"

using namespace std;
//数据库连接池
class connection_pool
{
public:
	MYSQL *GetConnection();				 //线程获取数据库连接
	bool ReleaseConnection(MYSQL *conn); //释放连接
	int GetFreeConn();					 //获取当前空闲连接数
	void DestroyPool();					 //销毁所有连接

	//单例模式 懒汉模式 获取一个数据库连接池类的实例
	static connection_pool *GetInstance();
	//构造初始化
	void init(string url, string User, string PassWord, string DataBaseName, int Port, unsigned int MaxConn); 
	
	connection_pool();
	~connection_pool();

private:
	unsigned int MaxConn;  //最大连接数 这个值和信号量最大值保持相同 信号量最大值表示同时可以有多少个进程访问临界资源
	unsigned int CurConn;  //当前已使用的连接数
	unsigned int FreeConn; //当前空闲的连接数

private:
	locker lock;//使用的锁 因为数据库连接池中的连接属于临界资源 相当于线程池中的线程数组 
	list<MYSQL *> connList; //连接池核心容器为链表 也可以用数组实现 相当于一个缓冲队列 用来储存八个连接
	sem reserve;//信号量 表示空闲的连接数 回想一下线程池中，信号量可以用于post与wait，调度各个producer与costomer的行为 在这里仍然同理

private:
	string url;			 //主机地址
	string Port;		 //数据库端口号
	string User;		 //登陆数据库用户名
	string PassWord;	 //登陆数据库密码
	string DatabaseName; //使用数据库名
};

//将数据库连接(client)的获取与释放通过RAII机制封装，避免手动释放。
//实际上就是对数据库生产者和消费者的调用函数多封装了一层，没什么大不了的
class connectionRAII{

public:
	connectionRAII(MYSQL **con, connection_pool *connPool);//这里要用二级指针 即深拷贝 第一个参数是一个传出参数 获取一个已经连接成功的client 第二个参数是sql连接池
	~connectionRAII();//
	
private:
	MYSQL *conRAII;//一个数据库连接指针
	connection_pool *poolRAII;//一个数据库连接池指针
};

#endif


/*
RAII（Resource Acquisition Is Initialization），也称为资源获取即初始化，
是C++语言中的一种管理资源、避免泄漏的机制。它利用了C++中对象构造和析构的特性，
通过在对象构造时获取资源，在对象生命期内控制对资源的访问，
最后在对象析构时释放资源，以达到安全管理资源、避免资源泄漏的目的。
*/