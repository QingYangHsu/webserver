#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;
//连接池构造函数
connection_pool::connection_pool()
{	//当前已经使用的连接数 当前空闲的连接数都初始化为0
	this->CurConn = 0;
	this->FreeConn = 0;
}
//懒汉模式
connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

//构造初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, unsigned int MaxConn)
{
	this->url = url; //主机地址
	this->Port = Port; //数据库端口号
	this->User = User; //登陆数据库用户名
	this->PassWord = PassWord;
	this->DatabaseName = DBName;//使用数据库名

	lock.lock();//上锁 因为connlist是临界资源
	for (int i = 0; i < MaxConn; i++)//maxconn是最大连接数
	{
		MYSQL *con = NULL;
		con = mysql_init(con);//初始化一个mysql结构体对象 相当于一个还未与sql server连的client

		if (con == NULL)//mysql结构体对象初始化失败
		{
			cout << "Error:" << mysql_error(con);
			exit(1);
		}
		/*
		mysql_real_connect()函数原型如下：
		MYSQL *mysql_real_connect(MYSQL *mysql, const char *host, const char *user,const char *passwd, const char *db, unsigned int port,const char *unix_socket, unsigned long client_flag);
		参数说明：
		mysql：一个已经初始化的MYSQL对象指针，通过mysql_init()函数获得。
		host：连接的主机地址，可以是IP地址或主机名。即数据库服务器地址 在本例中服务器就跑在本机 所以可以直接用localhost
		user：数据库的用户名。
		passwd：数据库的密码。
		db：要连接的数据库名。
		port：连接的端口号，默认为0，表示使用默认端口。
		unix_socket：UNIX套接字路径，用于本地连接。
		client_flag：客户端连接标志，可以使用特定的选项设置，如CLIENT_FOUND_ROWS、CLIENT_MULTI_STATEMENTS等。
		返回值：
		返回一个MYSQL结构体对象指针，如果连接成功，则指针指向连接的MYSQL对象；如果连接失败，则返回NULL。
		下面的.c_str()是string类的类函数，将string对象转换为char*
		*/
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);//与server连接
		//此时con指向的mysql结构体已经和mysql的server连接起来
		if (con == NULL)//con与server连接失败
		{
			cout << "Error: " << mysql_error(con);
			exit(1);
		}
		connList.push_back(con);//将已经与server连接好的client插入链表
		++FreeConn;//空闲连接数++
	}

	reserve = sem(FreeConn);//用总共建立的空闲连接总数 初始化信号量

	this->MaxConn = FreeConn;
	
	lock.unlock();//解锁 链表已经初始化完毕
}


//当有请求时，从数据库连接池(即链表)中返回一个可用连接(一个已经与server连接的client)，调用线程更新使用和空闲连接数
//消费者customer
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;

	if (0 == connList.size())//线程池连接链表为空
		return NULL;

	reserve.wait();//若reserve大于0，信号量值reserve减一 否则调用线程就阻塞在这里
	
	lock.lock();//上锁 因为连接链表是临界资源

	con = connList.front();
	connList.pop_front();

	--FreeConn;//空闲数减一
	++CurConn;//已经连接数加一

	lock.unlock();//解锁
	return con;
}

//调用线程释放当前使用的连接 返回值 成功为true 失败为false
//producer
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con)//要释放的连接为空
		return false;

	lock.lock();

	connList.push_back(con);
	++FreeConn;
	--CurConn;

	lock.unlock();

	reserve.post();//信号量值加一 同时唤醒一个正在沉睡的调用GetConnection的线程
	return true;
}

//销毁数据库连接池 因为链表每一个节点都分配了空间 这个函数纯纯是给析构函数调用的
void connection_pool::DestroyPool()
{
	lock.lock();//上锁
	if (connList.size() > 0)
	{
		list<MYSQL *>::iterator it;//迭代器
		for (it = connList.begin(); it != connList.end(); ++it)//遍历整条链表
		{
			MYSQL *con = *it;//it解引用之后是mysql*类型
			mysql_close(con);//关闭该mysql客户与mysql server的连接 
		}
		CurConn = 0;
		FreeConn = 0;
		connList.clear();//清空链表 并且回收每一个节点指向结构体所分配的空间

		lock.unlock();//解锁
	}

	lock.unlock();
}

//获取当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->FreeConn;
}

//RAII机制销毁连接池 析构函数
connection_pool::~connection_pool()
{
	DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){//调用线程从连接池pop出一个可用连接
	*SQL = connPool->GetConnection();//从连接池链表中pop出一个可用连接，深拷贝给sql 
	
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII(){//调用线程用完该client，将该client放回连接池
	poolRAII->ReleaseConnection(conRAII);
}