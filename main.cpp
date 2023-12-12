#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <assert.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"
#include "lst_timer.h"
#include "./log/log.h"
#include "./CGImysql/sql_connection_pool.h"
//表示定时时间为5秒 每隔5秒就会产生一个定时信号
#define TIMESLOT 5 


//最大可以有65535个客户 客户数组长度开到65535
#define maxfd 65535

//监听的最大事件数量
#define max_event_number 10000

// #define SYNLOG  //同步写日志
 #define ASYNLOG //异步写日志

#define listenfdET //server端采取边缘触发非阻塞 这里设置主要是在server处理新client连接的时候 边缘要用while循环
// #define listenfdLT //server端采取水平触发阻塞 这里设置主要是在server处理新client连接的时候 水平只处理一次即可，用if

//管道 pipefd[0]是读端 pipefd[1]是写端
static int pipefd[2];
//链表类
static sort_timer_lst timer_lst;

//添加信号捕捉 第一个参数是信号 第二个参数是该信号的处理函数的函数指针
void addsig(int sig, void(handler)(int)) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    //设置临时阻塞信号集 将所有信号阻塞
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);

}
/*
当在一个源文件中使用 extern 关键字声明一个函数时，
它表示该函数是在其他源文件中定义的函数。
这样，在当前文件中就可以调用该函数而无需重新定义它。
*/
//添加文件描述符到epoll例程中
extern void addfd(int epollfd, int fd, bool one_shot);


//从epoll例程中删除文件描述符
extern int removefd(int epollfd, int fd);


//从epoll例程中修改文件描述符
extern void modfd(int epollfd, int fd, int ev);

//设置套接字为非阻塞
extern void setnonblocking(int fd);

//信号处理函数
void sig_handler( int sig )//信号处理逻辑：将信号值写入到管道写端
{
    int save_errno = errno;
    int msg = sig;
    send( pipefd[1], ( char* )&msg, 1, 0 );//将信号值写入到管道里面 
    /*
	通常情况下，信号的值是一个整数，通常占用多个字节。
	但是在这个特定的例子中，只发送一个字节的原因可能是为了简化和快速地将信号值写入到管道中。
	由于信号值的范围通常不会超过一个字节的表示范围，因此只发送一个字节已经足够。
	*/
    errno = save_errno;
}


void timer_handler()
{
    // 定时处理任务，实际上就是调用tick()函数 tick函数将链表中超时的结点删掉
    timer_lst.tick();
    // 因为一次 alarm 调用只会引起一次SIGALARM 信号，所以我们要重新定时，以不断触发 SIGALARM信号。即一个接力的过程
    alarm(TIMESLOT);
    LOG_DEBUG("%s","定时器接力");
    Log::get_instance()->flush();
}

int main(int argc, char *argv[]) {

#ifdef ASYNLOG
    Log::get_instance()->init("ServerLog", 2000, 800000, 8); //异步日志模型 缓冲队列最大容量开到8
#endif

#ifdef SYNLOG
    Log::get_instance()->init("ServerLog", 2000, 800000, 0); //同步日志模型 缓冲队列最大容量开到0 因为同步方式写日志直接将日志写到磁盘文件之中
#endif

    if(argc <= 1) {
        printf("按照如下格式运行：%s port_number\n", basename(argv[0]));
        exit(-1);
    }

    //获取端口号
    int port = atoi(argv[1]);

    //"SIGPIPE" 是在 UNIX 或类 UNIX 系统中的一种信号，
    //表示管道破裂（Broken Pipe）。当一个进程尝试向已经关闭写端的管道（或者套接字）写入数据时，
    //操作系统会向该进程发送 SIGPIPE 信号，以通知它管道已经断开。
    //下面是对sigpipe信号进行处理 将 SIGPIPE 信号的处理方式设置为忽略
    addsig(SIGPIPE, SIG_IGN);

    //创建数据库连接池 通过连接池类的GetInstance()函数建立一个类单例 
    connection_pool *connPool = connection_pool::GetInstance();
    //主机地址为本机 用户名为root 密码991224 数据库为kaximoduodb 端口号3306 最大八个连接 对这些连接进行初始化 localhost可以代表本机地址 即数据库链表现在有八个已经与server连接的client在待命 
    connPool->init("localhost", "root", "991224", "kaximoduodb", 3306, 8);//最大连接数是8

    //初始化线程池
    threadpool<http_conn> *pool = NULL;
    try {
        pool = new threadpool<http_conn>;
    }
    catch(...) {
        return 1;
    }

    //创建一个数组用于保存所有的客户端信息 users是该http_conn数组首地址 将用户名与密码放在一个全局map表中
    http_conn *users = new http_conn[maxfd];

    //初始化数据库读取表 在这之前肯定数据库池要先初始化好 池子中已经创立一定数量的连接，这些在connPool->init()中实现
    //然后这个动作通过SELECT username,passwd FROM user这一个sql语句将原来保存在数据库中的连接的用户名与密码存放到全局数组map中，相当于读取历史登录记录
    users->initmysql_result(connPool);//connPool是数据库连接池 
    
    int serverfd = socket(PF_INET, SOCK_STREAM, 0);
    
    //设置端口复用 （即允许服务端在主动关闭后不用经历time_wait立即绑定相同端口）
    int reuse = 1;
    setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in serveraddr;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons( atoi(argv[1]) );
    bind(serverfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr) );
    
    //这个5表示等待队列与已连接队列客户数量最多为5
    listen(serverfd, 5);

    //创建epoll对象 事件数组 添加
    epoll_event events[max_event_number];

    int epollfd = epoll_create(5);

    //将监听的文件描述符添加到epoll对象中 这里写一个函数实现 默认不对服务端套接字注册one_shot事件 因为理解one_shot的含义 不可能同时又两个线程同时处理server套接字
    addfd(epollfd, serverfd, false);

    http_conn::m_epollfd = epollfd;

    //主线程不断地循环检测有没有哪些事件发生

    // printf("111\n");

    // 创建管道 管道有读端和写端 pipefd[0]表示读 pipefd[1]表示写
    bool ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert( ret != -1 );
    setnonblocking( pipefd[1]);//管道写端设置为非阻塞
    addfd( epollfd, pipefd[0],false );//监听管道读端

    // 设置信号处理函数
    addsig( SIGALRM , sig_handler);
    addsig( SIGTERM , sig_handler);
    bool stop_server = false;

    bool timeout = false;
    alarm(TIMESLOT);  //设置一个5s的定时器 5s到后 自动捕捉该alarm信号，触发sig_handler，将该alarm信号写入管道写端，serverfd监视到管道写端写事件后，将该信号读出，timeout设置为true，最后执行timerhandler，主要是删除链表中过时节点，以及设置下一个alarm定时器
    while(!stop_server) {
        //返回值代表监测到几个事件
        //最后一个参数为-1，表示默认阻塞 成功时返回发生事件的文件描述数 失败返回-1 最后一个参数为-1 该函数默认阻塞
        int num = epoll_wait(epollfd, events, max_event_number, -1);
        /*
        "EINTR" 是一个在计算机编程中常见的缩写，代表 "Interrupted System Call"（中断的系统调用）。
        在类Unix系统（如Linux）中，当一个进程正在进行系统调用（例如读取文件、写入数据等）时，
        如果收到了一个信号（如SIGINT），系统调用可能会被中断。当系统调用被中断时，操作系统会返回一个特定的错误码，即 "EINTR"。
        这种情况下，程序通常可以选择重新发起被中断的系统调用，或者采取其他适当的操作来处理信号。
        处理 "EINTR" 错误码是编写可靠的、正确处理信号的程序的重要部分之一。
        */
        if(num < 0 && errno != EINTR) {
            printf("%s\n","epoll error");
            break;
        }

        //循环遍历事件数组
        for(int i = 0; i < num; i++) {
            int sockfd = events[i].data.fd;
            if(sockfd == serverfd) {
                printf("%d\n",1);
                struct sockaddr_in clientaddr;
                socklen_t client_addrlen = sizeof(clientaddr);
#ifdef listenfdLT //服务端水平触发 这里一定用if 因为一次没有将事件处理完 下次事件仍然会重新注册 
                int clientfd = accept(serverfd, (struct sockaddr*)&clientaddr, &client_addrlen);

                if(http_conn::m_usercount >= maxfd) {
                    //目前的连接数已经满了 即现在服务器连接的用户特别多
                    //给这个客户端写一个信息 “服务器正忙 需要等待”
                    printf("%s\n","busy");
                    close(clientfd);
                    continue;//跳过本轮事件对该client的处理
                }

                //将新的客户数据初始化 并放在数组中 直接以客户端文件描述符大小作为索引放入数组
                users[clientfd].init(clientfd, clientaddr);

                // 创建定时器，设置超时时间，然后绑定定时器与用户数据，最后将定时器添加到链表timer_lst中
                util_timer* timer = new util_timer;//这里会调用定时器类构造函数
                timer->user_data = &users[clientfd];
                time_t cur = time( NULL );
                timer->expire = cur + 3 * TIMESLOT;//过15秒没有数据收发 即认为该客户连接超时
                users[clientfd].timer = timer;
                timer_lst.add_timer( timer );//将该定时器插入链表
                LOG_DEBUG("shuliang:%d",timer_lst.count);
                Log::get_instance()->flush();
#endif
#ifdef listenfdET //边缘模式一定要使用一个while循环 因为如果本次处理并没有完全处理完client事件，下次不会重复触发该事件，因此一定要用while确保事件处理完全
                while (1)
                {
                    int clientfd = accept(serverfd, (struct sockaddr*)&clientaddr, &client_addrlen);
                    if (clientfd < 0)//上一轮while循环已经将client的事情处理完全，正常在这里break
                    {
                        LOG_ERROR("%s:errno is:%d", "accept error", errno);
                        break;
                    }
                    if (http_conn::m_usercount >= maxfd)
                    {
                        printf("%s\n","busy");
                        close(clientfd);
                        break;//注意 这里用break 而不是continue
                    }
                    users[clientfd].init(clientfd, clientaddr);
                    // 创建定时器，设置超时时间，然后绑定定时器与用户数据，最后将定时器添加到链表timer_lst中
                    util_timer* timer = new util_timer;//这里会调用定时器类构造函数
                    timer->user_data = &users[clientfd];
                    time_t cur = time( NULL );
                    timer->expire = cur + 3 * TIMESLOT;//过15秒没有数据收发 即认为该客户连接超时
                    users[clientfd].timer = timer;
                    timer_lst.add_timer( timer );//将该定时器插入链表
                    LOG_DEBUG("shuliang:%d",timer_lst.count);
                    Log::get_instance()->flush();
                }
                continue;//新连接处理完成 下面的else if不用再判断了
#endif
            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {//当对方连接断开，会有一个EPOLLRDHUP事件  EPOLLHUP：对端挂断了套接字 EPOLLERR：发生错误
                printf("%d\n",2);
                //对方异常断开或者错误等事件
                util_timer* timer = users[sockfd].timer;
                if( timer )
                {
                    timer_lst.del_timer( timer );//从链表删除该定时器
                }
                users[sockfd].close_conn();
            }
            else if((sockfd != pipefd[0]) && events[i].events & EPOLLIN) {
                printf("%d\n",3);
                //按照proactor模式，我们要在主线程中一次性把所有的数据读出来
                util_timer* timer = users[sockfd].timer;
                if(users[sockfd].read()) {
                    // 如果某个客户端上有数据可读，则我们要调整该连接对应的定时器，以延迟该连接被关闭的时间。
                    if( timer ) {
                        time_t cur = time( NULL );
                        timer->expire = cur + 3 * TIMESLOT;//将超时时间改为当前时间再加15秒
                        LOG_DEBUG("%s","adjust timer once");
                        timer_lst.adjust_timer( timer );
                    }
                    //一次性把所有数据都读完 交给工作线程去处理业务逻辑 users+sockfd表示users指针偏移到sockfd位置 每次偏移步长为http_conn大小
                    pool->append(users + sockfd);
                }
                else {//读取失败
                    if( timer ) {
                    timer_lst.del_timer( timer );//从链表删除该定时器
                    }
                    users[sockfd].close_conn();
                }
            }
            else if(events[i].events & EPOLLOUT) {
                printf("%d\n",4);
                //一次性写完所有数据
                LOG_DEBUG("shuliang%d\n",timer_lst.count);
                Log::get_instance()->flush();
                if(!users[sockfd].write() ) {//函数返回值 false表示不保持连接 true表示需要保持连接等待下一次事件
                    util_timer* timer = users[sockfd].timer;
                    if( timer )
                    {   
                        printf("%d\n",8);
                        LOG_DEBUG("shuliang%d\n",timer_lst.count);
                        timer_lst.del_timer( timer );//从链表删除该定时器
                        LOG_DEBUG("shuliang%d\n",timer_lst.count);
                        LOG_DEBUG("链表首尾%d,%d\n",timer_lst.gethead()==NULL,timer_lst.gettail()==NULL);
                        Log::get_instance()->flush();
                    }
                    //关闭连接
                    LOG_DEBUG("%s","关闭客户端连接\n");
                    users[sockfd].close_conn();
                }
            }
            else if( ( sockfd == pipefd[0] ) && ( events[i].events & EPOLLIN ) ) {//如果管道读端套接字有事件 并且发生可以读入事件
                printf("%d\n",5);
                // 处理信号
                int sig;
                char signals[1024];//接收缓冲区
                ret = recv( pipefd[0], signals, sizeof( signals ), 0 );//ret是接收字节数
                if( ret == -1 ) {
                    continue;
                } 
				else if( ret == 0 ) {
                    continue;
                } 
				else  {
                    for( int i = 0; i < ret; ++i ) {//遍历每一个字节
                        switch( signals[i] )  {
                            case SIGALRM://定时时间(5s)到
                            {
                                // 用timeout变量标记有定时任务需要处理，但不立即处理定时任务
                                // 这是因为定时任务的优先级不是很高，我们优先处理其他更重要的任务。
                                timeout = true;
                                break;//跳出遍历每一个字节的循环
                            }
                            case SIGTERM://表示收到了进程终止信号
                            {
                                stop_server = true;
                            }
                        }
                    }
                }
            }
        }
        // 最后处理定时事件，因为I/O事件有更高的优先级。当然，这样做将导致定时任务不能精准的按照预定的时间执行。
        if( timeout ) {
            printf("%d\n",6);
            timer_handler();
            timeout = false;
        }
    }





    close(epollfd);
    close(serverfd);
    close( pipefd[1] );
    close( pipefd[0] );
    delete[] users;
    delete pool;



    return 0;
}

/*主体流程：
最开始线程池会被初始化，即66行new一个线程池初始化类，自动调用构造函数，
在构造函数中完成了八个线程的创建，设置了线程分离，以及设置了线程主函数worker；
一旦线程被创建就自动调用线程主函数worker，
在worker中调用类的run函数，让线程池启动（也就是说默认情况下这个run会被启动八次）；
初始情况下请求队列为空，信号量m_cond为初值0，因此线程主函数会阻塞在m_queuestat.wait()处，因为信号量小于等于0,信号量的wait
函数会让调用线程休眠

接着主线程继续向下执行
当主线程已经完成与了epoll例程创建 serverfd创建 客户端数组http_conn[]的创建；以及任务类静态数据成员的指定（100行）
然后创建管道，pipefd[0]为管道读端 pipefd[1]为管道写端 并且将管道写端设置为非阻塞
并且监听管道读端，为边缘触发，并且设置非阻塞。
同时注册SIGALRM和SIGTERM两个信号以及对应处理函数sig_handler，sig_handler处理函数逻辑是向管道写端写入信号值，
只写入一个字节
121行有新客户请求连接 会将新客户连接并注册到epoll例程，并更新客户数组，并且创建一个定时器类，
用users[clientsock]，超时时间等更新定时器类，并将这个定时器类插入链表（使用链表的插入函数成员），

143行当客户有数据来的时候，主线程会通过users[sockfd].read()一次性把数据读完；read函数负责将数据保存到任务类自带的读缓冲readbuff之中，并更新readindex
读完之后更新该客户对应定时器的超时时间，即延长15秒，读取失败会将该定时器从链表删去并且关闭连接并且注销事件，然后会将任务追加到各个线程共享的请求队列之中，
通过pool->append(users + sockfd)语句实现；
其中users是http_conn *类型的变量，也可以理解为数组名，该数组保存所有的客户端信息（或者是所有的工作任务），
users+sockfd表示users指针向后移动sockfd个http_conn个结构体的位置，其类型为http_conn*,

当任务被追加到请求队列之中，m_queuestat.post()使条件变量m_cond++，线程池中阻塞的的八个子线程中一个就会被唤醒，接着线程主函数
worker中的run函数中m_queuestat.wait()
条件变量--，向下执行，run函数不断地循环从链式请求队列头部取得一个任务
每取到一个任务，就去调用任务类的process；
执行process就来做业务处理，在process中先调用process_read()解析http请求报文，并将解析结果中的关键字段更新任务类数据成员m_method m_version m_url，
以及将client请求的资源文件进行内存映射，映射地址保存到m_file_address
其中任务类的m_check_state主状态机控制http解析的整体流程，接着在process中调用process_write()写http响应报文，通过process_read解析的结果，
得到不同的http状态码以及状态信息，写不同的响应行响应头部内容，如果是200ok，则使用分散发的方式，填写任务类成员m_iv结构体数组成员内容，
并更新bytes_to_send，表示将要发送的内容的字节数，并且重新注册写出事件epollout

在主线程中160行当下一次发生有数据可写的事件epollout后，
主线程就调用任务类的write(),将数据写出去，并根据返回结果决定是否关闭连接，该返回结果集任务类中m_linger中的值，
即是否保持与该客户的连接，由于任务类默认不包吃连接，所以将该定时器从链表删去，并且关闭客户端注销事件

如果是管道读端有读事件，说明5秒的定时时间已到，信号处理函数已经向管道写端写了信号然后被管道读端接收到了，
我们将管道读端的数据保存到signals缓冲区，然后逐字节遍历，当找到定时器信号即停止遍历，做上标记timeout，
延后处理，因为信号处理的优先级在主线程工作逻辑中较低，主线程的主要工作时完成与客户端的io与连接

直到最后，我们执行定时时间，即调用timer_handler,在handler里面实际上就是调用链表的tick()函数,tick函数将链表中超时的结点删掉,然后接力定时器信号，
相当于击鼓传花，因为alarm(timeslot)每次只能产生一个信号

编译指令：
g++ *.cpp -lpthread

浏览器输入：192.168.88.100/9190:index.html
常见bug：
只能创建八个线程 不能连接客户
解决：将虚拟机防火墙关掉 systemctl stop firewalld

变更webserver文件夹名，记得在http_conn.cpp中改根目录名字

实践证明：当浏览器连接到本地运行的服务器获取index.html资源，会发送两次html请求报文



压力测试：
使用webbench
./webbench -c 1000 -t 5 http://192.168.88.100:9190/index.html -t表示只访问5秒

*/