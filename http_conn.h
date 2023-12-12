#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

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
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include "locker.h"
#include <sys/uio.h>
#include <string.h>
#include "lst_timer.h"
#include "./CGImysql/sql_connection_pool.h"


class util_timer;   // 前向声明 因为client_data成员定义要用到

//任务类 
class http_conn {
public:
    /*
    enum 是 C++ 中的一个关键字，用于定义枚举类型（Enumeration Type）。
    枚举类型是一种用户自定义的数据类型，用于定义一组具名的常量值。
    */

    //这一部分定义了一些状态
    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    
    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
        这正好对应了请求报文的三个部分
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    
    // 从状态机的三种可能状态，即请求部分三个部分每一个部分都对应很多行，定义行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };
    

    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };



public:
    //读缓冲区大小
    static const int READ_BUFFER_SIZE = 2048;
    //写缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024; 

    //所有的socket上的事件都被注册到同一个epoll例程，因此设置为静态成员
    static int m_epollfd;

    //用来统计用户数量
    static int m_usercount;

    // 文件名的最大长度
    static const int FILENAME_LEN = 200;        

    void initmysql_result(connection_pool *connPool);

//    MYSQL *mysql; //我觉得这个数据成员无意义

    http_conn();
    ~http_conn();
    
    //处理客户端请求
    void process();


    //初始化新接收的连接
    void init(int sockfd, struct sockaddr_in &addr) ;

    //关闭连接
    void close_conn();

    //非阻塞的读
    bool read();
     
    //非阻塞的写
    bool write();

    //解析http请求报文
    HTTP_CODE process_read() ;

    bool process_write(HTTP_CODE ret);

    //解析请求行
    HTTP_CODE parse_request_line(char *text) ;

    //解析请求头部
    HTTP_CODE parse_headers(char *text) ;

    //解析请求体
    HTTP_CODE parse_content(char *text) ;


    //解析具体某一行
    LINE_STATUS parse_line() ;

public:
    int getsock() {
        return m_sockfd;
    }


private:
    //该http连接的socket
    int m_sockfd;
    //读缓冲
    char readbuff[READ_BUFFER_SIZE];
    //写缓冲
    char writebuff[WRITE_BUFFER_SIZE];
    //标记读缓冲区已经读入的客户端数据的最后一个字节的下一个位置 因为我们不可能一次性将客户端数据全部读完
    int m_read_idx;


    //客户端地址
    struct sockaddr_in m_address;

    //当前正在分析的字符在读缓冲区的位置
    int m_checked_index;

    //当前正在解析行在readbuff中的起始位置
    int m_start_line;

    //主状态机当前所处的状态
    CHECK_STATE m_check_state;

    //初始化连接以外的信息，上面的有参init负责初始化连接的信息
    void init() ;

    HTTP_CODE do_request() ;

    //释放掉在do_request中申请的内存空间
    void unmap();

    //获取某一行的头指针
    char *getline() {
        return readbuff + m_start_line;
    }

    //请求目标文件的文件名
    char * m_url;

    //协议版本 只支持http1.1
    char * m_version;

    //请求方法
    METHOD m_method;
    //主机名
    char *host;

    //HTTP请求是否要保持连接 即http报文头部字段 connection方法 若为close，则该值为false 若为keep_alive,则该值为true
    bool m_linger;
    
    // HTTP请求报文的请求体总长度
    int m_content_length;
    
    //主机名，即客户端想要访问的服务器端域名
    char * m_host;

    // 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root是网站根目录
    char m_real_file[ FILENAME_LEN ];

    // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息 由系统自定义的默认构造函数来初始化
    struct stat m_file_stat; 

    // 客户请求的目标文件在do_reauest中被mmap到内存中的起始位置
    char* m_file_address;                   

    char *m_string; //存储请求体数据 主要是用户登录的用户密码

    // 这一组函数被process_write调用以填充HTTP应答。
 
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_content_type();
    //添加响应状态行
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();

    // 写缓冲区中待发送的字节数    
    int m_write_idx;            


    // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。 
    // 我们有两块分散的内存 一块是内存映射的内存(其中是客户请求资源文件的内容，作为http响应报文的响应体) 一块是writebuff(其中是http响应报文的状态行.头部字段) 
    // 所以我们定义大小为2的数组
    struct iovec m_iv[2];                   
    int m_iv_count;

    int bytes_to_send;              // 将要发送的数据的字节数 由主线程调用write使用
    int bytes_have_send;            // write中已经发送的字节数                
    int cgi;        //是否启用的POST 如果是1 启用post 如果是0 不启用post
public:
    util_timer* timer;          // 定时器
};







#endif