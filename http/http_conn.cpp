#include "http_conn.h"
#include "../log/log.h"
#include <mysql/mysql.h>
#include <map>
#include <fstream>

//定义client事件触发方式
#define connfdET //边缘触发非阻塞   要使用while处理sockfd读写
// #define connfdLT //水平触发阻塞

//类静态数据成员初始化 当在类的外部定义静态成员 不能重复使用static关键字 static关键字只能用于类内
int http_conn::m_epollfd = -1;
int http_conn::m_usercount = 0;

// 网站的根目录
const char* doc_root = "/work/webserver/root";

// 定义HTTP响应的一些状态信息  表明server对client请求结果的返回结果,是响应报文状态行与响应报文响应体的一部分（key）
const char* ok_200_title = "OK";        //响应报文状态行
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";//响应报文响应体
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";


//开一个全局map表 将数据库kaximoduodb表中的用户名和密码放入map initmysql_result函数将结果填入该数组
map<string, string> map_users;

//针对map表的锁 不允许两个client同时insert记录到表
locker m_lock;

http_conn::http_conn() {

}

http_conn::~http_conn() {

}

void http_conn::initmysql_result(connection_pool *connPool) //当该函数声明周期结束 里面创建的类connectionRAII mysqlcon也会被回收，即调用其析构函数，其内部sql连接会自动被归还 但是任务类共享的mysql连接会被保留
{
    MYSQL *mysql = NULL; //不要用任务类的成员mysql 在这里定一个局部变量
    LOG_ERROR("%s:%d", "tmd2:mysql是",mysql==NULL);
    //从连接池链表中pop出一个数据库连接，赋给mysql，并将数据库连接池connpool返回
    connectionRAII mysqlcon(&mysql, connPool);
    LOG_ERROR("%s:%d", "tmd3:mysql是",mysql==NULL);
    //在user表中检索username，passwd数据，浏览器端输入 事实上 user表也只有这两个列属性
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))//如果查询成功执行，则返回0。
                                                               //如果发生错误，则返回一个非零值，并设置errno以指示错误类型。
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

        //下面是结果集结构体定义
        // typedef struct st_mysql_res {
        // my_ulonglong  row_count; //结果集中的行数
        // MYSQL_FIELD	*fields;    //指向列信息结构体数组的首地址
        // MYSQL_DATA	*data;
        // MYSQL_ROWS	*data_cursor;
        // unsigned long *lengths;		/* column lengths of current row */
        // MYSQL		*handle;		/* for unbuffered reads */
        // const struct st_mysql_methods *methods;
        // MYSQL_ROW	row;			/* If unbuffered read */
        // MYSQL_ROW	current_row;		/* buffer to current row */
        // MEM_ROOT	field_alloc;
        // unsigned int	field_count, current_field;
        // my_bool	eof;			/* Used by mysql_fetch_row */
        // /* mysql_stmt_close() had to cancel this result */
        // my_bool       unbuffered_fetch_cancelled;  
        // void *extension;
        // } MYSQL_RES;
    //从表中检索完整的结果集 
    MYSQL_RES *result = mysql_store_result(mysql);
    /*
    如果查询成功执行并且结果集不为空，则返回一个指向结果集的指针。
    如果查询失败或结果集为空，则返回NULL。
    */
    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);          //没什么用
        //下面是列信息结构体
        // typedef struct st_mysql_field {
        // char name; / Name of column 列名称*/ 
        // char org_name; / Original column name, if an alias 如果该列是别名，则为原始列名。 */
        // char table; / Table of column if column was a field 列所属的表名。*/
        // char org_table; / Org table name, if table was an alias */
        // char db; / Database for table */
        // char catalog; / Catalog for table */
        // char def; / Default value (set by mysql_list_fields) /
        // unsigned long length; / Width of column (create length) /
        // unsigned long max_length; / Max width for selected set /
        // unsigned int name_length;
        // unsigned int org_name_length;
        // unsigned int table_length;
        // unsigned int org_table_length;
        // unsigned int db_length;
        // unsigned int catalog_length;
        // unsigned int def_length;
        // unsigned int flags; / Div flags /
        // unsigned int decimals; / Number of decimals in field /
        // unsigned int charsetnr; / Character set /
        // enum enum_field_types type; / Type of field. See mysql_com.h for types */
        // void *extension;
        // } MYSQL_FIELD;
    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);       //没什么用

    //从结果集中获取下一行,每一行相当于一条记录，将对应的用户名和密码，存入map中 MYSQL_ROW是一个char**
    /*
    如果结果集中还有行可用，则返回一个指向MYSQL_ROW结构的指针，该结构包含当前行的数据。
    如果结果集中没有更多的行可用，则返回NULL。
    */
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1( row[0] );//temp1是用户名 为一个string串
        string temp2( row[1] );//temp2是密码，与上同理
        map_users[temp1] = temp2;//做映射
        //printf("用户名是:%s,密码是:%s\n",temp1,temp2);
        //std::cout<<"yonghuming:"<<temp1<<"  mima: "<<temp2<<std::endl;
    }
}

//设置文件描述符非阻塞
void setnonblocking(int fd) {
    int old_flag = fcntl(fd, F_GETFL);
    old_flag |= O_NONBLOCK;
    fcntl(fd,F_SETFL, old_flag);
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;

#ifdef connfdET //这里对client使用边缘触发 主要是server接收到client写或者读事件 处理读写时 要用while 因为如果并没有一次性处理完client的字节 后面该事件不会重复触发 所以要用while确保数据处理完全
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef connfdLT //这里对client使用条件触发 主要是server接收到client写或者读事件 处理读写时 用if 因为即使并没有一次性处理完client的字节 后面该事件仍然会重复触发
    event.events = EPOLLIN | EPOLLRDHUP;
#endif

#ifdef listenfdET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef listenfdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif

    if (one_shot)
        event.events |= EPOLLONESHOT;
    /*
    EPOLLONESHOT事件是一种在epoll机制中用于防止同一事件被触发多次的事件。
    当一个socket上的事件被触发时，比如数据可读或者可写，
    操作系统会对这个socket的文件描述符注册的所有监听该事件的操作进行触发。
    然而，在某些情况下，一个socket上的事件可能在处理过程中再次被触发，
    导致两个线程同时操作一个socket，这可能引发问题。

    EPOLLONESHOT事件可以解决这个问题。对于注册了EPOLLONESHOT事件的文件描述符，
    操作系统最多触发其上注册的一个可读、可写或者异常事件，且只触发一次，
    除非使用epoll_ctl函数重置该文件描述符上EPOLLONESHOT事件。
    这样，在一个线程正在使用socket时，其他线程无法操作该socket。
    */
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}


//当本次对监控的fd未处理完全时，修改文件描述符fd 重置epoll上的oneshot事件 ，以确保下一次可读时，epollin能被触发
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd =fd;
    event.events = ev | EPOLLONESHOT |EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
    //EPOLL_CTL_MOD是epoll系统调用中的一种动作，它用于修改已经注册的fd的监听事件。
}

//初始化连接
void http_conn::init(int sockfd, struct sockaddr_in &addr) {
    m_sockfd = sockfd;
    m_address = addr;

    //设置sockfd的端口复用 这样客户端主动关闭后不用经历time_wait
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    //cout<<"debug3"<<endl;
    //将客户fd添加到epoll文件例程中 并且设置one-shot
    addfd(m_epollfd, m_sockfd, true);

    //总用户数+1
    m_usercount++;

    init();

    //为什么这两个init要分开写呢，因为假如一个用户是重新初始化，其没有必要做上面建立连接的操作 总用户数也没有必要变化
}

//上个函数的重载，初始化连接以外的信息，上面的有参init负责初始化连接的信息
void http_conn::init() {
    //主状态机初始化状态为解析请求首行
    m_check_state = CHECK_STATE_REQUESTLINE;
    //初始化当前正在分析的字符在读缓冲区的位置 就是首位 
    m_checked_index = 0;
    //当前正在解析行在readbuff中的起始位置 默认从第零行首位置在第0位
    m_start_line = 0;
    //标记读缓冲区已经读入的客户端数据的最后一个字节的下一个位置
    m_read_idx = 0;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    // 将读缓冲区全部清空
    bzero(readbuff, READ_BUFFER_SIZE);
    
    //HTTP请求是否要保持连接,默认为close
    m_linger = false;

    //HTTP请求的请求体总长度初始值置0
    m_content_length = 0;
    // 服务器端域名初始值 置为空
    m_host = 0;

    m_write_idx = 0;

    //这两个指标是http写响应报文用到的
    bytes_to_send = 0;
    bytes_have_send = 0;

    cgi=0;//如果是0 不启用post 默认不启用 即不做用户交互，密码登录验证功能
    //mysql=NULL;
    bzero(readbuff, READ_BUFFER_SIZE);
    bzero(writebuff, READ_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}

//从epoll中移除监听的文件描述符
int removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
    //将该文件描述符关掉
    printf("close fd:%d\n",fd);
    close(fd);
}

//关闭连接
void http_conn::close_conn() {
    if(m_sockfd != -1) {
        removefd(m_epollfd, m_sockfd);
        //-1表示无效
        m_sockfd = -1;
        m_usercount--;
    }
}

//非阻塞的读 因为我们将套接字设置的是非阻塞 成功为true 失败为false 
//这个函数是主线程调的 主线程将读取到的数据保存到任务类中的读缓冲readbuff，同时设置m_read_index，表示读取到的数据大小
bool http_conn::read() {
    //读入数据超过缓冲区容量
    if(m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }
    //读取到的字节
    int bytes_read = 0;
#ifdef connfdLT//水平 因为若本次事件没有处理完 后面还会重复触发 m_read_idx记录了上一次读取到的截止位置
    bytes_read = recv(m_sockfd, readbuff + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0 );//该函数非阻塞
    if (bytes_read <= 0)
    {
        return false;
    }
    m_read_idx += bytes_read;
    return true;
#endif
#ifdef connfdET//边缘 因为事件只会触发一次
    while(true) {
        bytes_read = recv(m_sockfd, readbuff + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0 );//该函数非阻塞
        if(bytes_read == -1) {
            //eagain表示现在无可用的数据 以后再试一次 ewouldblock：表示发送套接字发送缓冲区已满或者接收套接字缓冲区已空
            if(( errno = EAGAIN) || (errno == EWOULDBLOCK)) {
                //因为套接字都是非阻塞模式 所有有可能没有数据
                break;
            }
            else return false;            
        }
        else if(bytes_read == 0) {
            //收到对方eof
            return false;
        }
        m_read_idx += bytes_read;
    }

    printf("读取到了数据：%s\n", readbuff);
    return true;
#endif
}
     
//由线程池中的八个线程的线程主函数worker中的run调用 是处理http请求的入口函数
//这个地方是主线程读完请求报文后处理业务逻辑，主要分为逻辑读和逻辑写
void http_conn::process() {

    //解析http请求 HTTP_CODE是服务器处理HTTP请求的可能结果，报文解析的结果
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST) {
        //客户端请求报文不完整 需要重新注册客户端的监听事件：读事件 等待下一次处理
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return ;
    }
    //正常情况下 read_ret为FILE_REQUEST

    // printf("parse request, create response\n");


    //生成响应 将process_read()读取http请求报文的结果作为参数传入写http响应报文写函数
    bool write_ret = process_write( read_ret );
    if ( !write_ret ) {     //写失败
        close_conn();
    }
    //因为事件都是one_shot事件，要重新注册客户端的监听事件：写事件
    modfd( m_epollfd, m_sockfd, EPOLLOUT);


}

//主状态机：解析http请求报文
HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;//记录当前行的读取状态 初始默认设置为读取到一个完整行
    HTTP_CODE ret =NO_REQUEST;//记录http请求的处理结果作为返回值 
    
    //行指针
    char *text = 0;

    //m_check_state在init中默认设置为检查请求行
    while( ((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) 
    || (line_status = parse_line()) == LINE_OK) {
        //如果主状态机是解析请求体，说明请求行以及请求头已经检查完了，同时另一个条件为上一行数据是完整的 
        //或者是解析到了一行完整的数据则解析该行的数据 否则若parse_line()返回的是LINE_OPEN,或者line_bad
        //则会跳过整个while循环，直接返回NO_REQUEST,在process_read()上级调用process()中，会重新注册事件，以后继续读入客户端数据，再重新做逻辑处理

        //获取一行数据，text保存 因为在parse_line()函数中已经将一行的行结束符"\r\n"都替换为'\0'
        text = getline();
        //m_startline更新为下一行的起始位置 m_checked_index已经在parse_line中更新，指向readbuff中还未分析的新行
        m_start_line = m_checked_index;
        printf("解析行为:got 1 http line:%s \n", text);

        switch(m_check_state) {
            case CHECK_STATE_REQUESTLINE:           //主状态机状态为解析请求行 
            {
                ret =parse_request_line(text);      //  在函数内部完成主状态机状态转移
                if(ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                //如果是其他情况直接break掉 继续获取下一行数据
                break;

            }
            case CHECK_STATE_HEADER:
            {
                ret =parse_headers(text);
                if(ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                else if(ret == GET_REQUEST) {
                    //解析具体的请求的信息 若请求体长度为0，说明我们已经得到了一个完整的HTTP请求，即GET_REQUEST，因为get报文没有请求体消息 只有post报文有请求体消息
                    return do_request();
                }
                break;      //跳出这个switch 继续读取请求头部下一行数据
                //2023.12.12：这里是一个问题 为什么这个break会引发错误
            }
            case CHECK_STATE_CONTENT:
            {
                ret =parse_content(text);
                if(ret == GET_REQUEST) {
                    return do_request();
                }
                //改变从状态机状态 即如果失败的话将行状态设置为数据不完整
                line_status = LINE_OPEN;
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }
    //若整个while循环跳过 就会走到这一步 
    return NO_REQUEST;
}

// 根据process_read处理HTTP请求的结果，写http响应报文，返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret)
    {
        case INTERNAL_ERROR: {//如果是内部错误
            //添加http响应报文状态行
            add_status_line( 500, error_500_title );
            //添加http响应报文头部字段
            add_headers( strlen( error_500_form ) );
            //添加http响应体
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        }
        case BAD_REQUEST: {//语法错误 比如用户请求一个文件夹
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        }
        case NO_RESOURCE: {//服务器没有该请求资源
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST: {//访问权限不够
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        }
        case FILE_REQUEST: {//一般情况下：获取文件成功 在process_read中的do_request已经将申请的资源文件通过内存映射映射到一个地址m_file_address
            add_status_line(200, ok_200_title );
            if(m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                m_iv[ 0 ].iov_base = writebuff;//响应报文状态行以及头部字段以及响应体
                m_iv[ 0 ].iov_len = m_write_idx;
                m_iv[ 1 ].iov_base = m_file_address;//内存映射地址，这片区域内容作为实际返回的文件内容 跟在响应体后面
                m_iv[ 1 ].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                //主线程将要发送的字节数
                bytes_to_send = m_write_idx + m_file_stat.st_size;

                return true;
            }
            else//如果文件大小为0，则发送一个简单的HTML成功页面。无论何种情况，如果无法添加内容，则返回false。
            {
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string))
                    return false;
            }            
        }
        default:
            return false;
    }
    m_iv[ 0 ].iov_base = writebuff;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

// 写HTTP响应 由主线程调用，当主线程监听到一个可以写的事件时，就将任务类中准备好的数据写出到客户端
//函数返回值 false表示写入完成 可以关闭连接 true表示写入尚未完成 需要保持连接等待写一次epoll事件
bool http_conn::write()
{
    int temp = 0;
    
    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束,重新注册读事件
        modfd( m_epollfd, m_sockfd, EPOLLIN ); 
        //重新初始化连接以外的信息，而不必初始化用户相关信息，因为先前已经初始化过了
        init();
        return true;
    }

    while(1) {
        // writev表示分散写 即如果有多块内存的数据 这多块内存不是连续的 可以一起写 如果请求成功，就有两块，如果不成功，只有一块 返回值为成功写入的总字节数，如果返回值为-1，则表示写入出现错误。
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if ( temp <= -1 ) {
            //当errno的值为EAGAIN时，表示出现了（资源暂时不可用）的错误。该错误通常在非阻塞操作中出现，表明当前操作无法立即完成，但稍后可能会变得可用。
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                modfd( m_epollfd, m_sockfd, EPOLLOUT );
                return true;
            }
            unmap();            //释放内存映射空间
            return false;
        }

        bytes_have_send += temp;//已经发送的字节加
        bytes_to_send -= temp;//将要发送的字节减

        if (bytes_have_send >= m_iv[0].iov_len)//表示已经发送了完整的响应状态行和响应头部响应体，该发送即文件内容部分
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = writebuff + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if (bytes_to_send <= 0)//所有数据发送完毕
        {
            // 没有数据要发送了
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);//再次为该客户端注册可读事件 等待其下一次http请求申请

            if (m_linger)//是否要保持连接 因为http是无状态协议 可以保持http连接状态
            {
                init();//初始化连接以外的信息
                return true;
            }
            else
            {
                return false;   //如果向主线程返回false，主线程会在定时器链表中将对应定时器删除,并关闭与该客户连接
            }
        }

    }

    
}

// 往写缓冲中写入待发送的数据 相当于一个util函数，各种add函数底层都调用该函数
bool http_conn::add_response( const char* format, ... ) {//第一个参数是固定参数 剩余是可变参数
    //此时写缓冲已满
    if( m_write_idx >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    va_list arg_list;//va_list用于处理可变参数列表,它是一个类型，用于存储和访问函数中传递的可变数量的参数。
    va_start( arg_list, format );//va_start宏用于初始化va_list类型的变量，使其指向可变参数列表的起始位置。其中第二个参数是最后一个固定参数
    int len = vsnprintf( writebuff + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ) {//写入的数据大于写缓冲区剩余空位
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );//va_end宏用于结束对可变参数列表的访问。
    return true;
}

//添加响应报文状态行 参数为错误码以及错误描述信息
bool http_conn::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

//添加响应报文头部字段 我们只写三个响应头部 当然成熟的服务器程序可以写更多头部字段 注意每个头部字段后面都要加"\r\n"
bool http_conn::add_headers(int content_len) {  //参数：请求体长度
    //添加目标文档长度字段
    add_content_length(content_len);
    //添加目标文档MIME类型
    add_content_type();
    //添加连接状态
    add_linger();
    //所有头部字段结束要单独添加一行空白行 空白行只有"\r\n"
    add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}
bool http_conn::add_content_type() {//text是主文档类型 html是子文档类型
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

//添加空行
bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

//添加http响应报文响应体
bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}



//解析http请求报文请求行，获得请求方法，目标url，http版本    text为readbuff中读取到的一行
HTTP_CODE http_conn::parse_request_line(char* text) {
    //  一个请求行的示例为:“GET /index.html HTTP/1.1”
    m_url = strpbrk(text, " \t"); // 判断第二个参数中的字符哪个在text中最先出现 注意第二个参数字符串中有空字符
    if (! m_url) { 
        return BAD_REQUEST;
    }
    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';    // 置位空字符，字符串结束符
    char* method = text;
    if ( strcasecmp(method, "GET") == 0 ) { // 忽略大小写比较
        m_method = GET;
    } 
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;//标志位置1 启用post
    }
    else {
        return BAD_REQUEST;
    }

    // m_url指向 "/index.html HTTP/1.1"串首位
    m_version = strpbrk( m_url, " \t" );//m_version指向html后的字符串的空字符 即" HTTP/1.1""
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';//先将m_version首位置0，再移动一位，现m_version指向HTTP/1.1
    if (strcasecmp( m_version, "HTTP/1.1") != 0 ) {
        return BAD_REQUEST;
    }
    /**
     * 一般情况：/index.html
     * 特殊情况：http://192.168.110.129:10000/index.html
    */
    if (strncasecmp(m_url, "http://", 7) == 0 ) {//判断m_url前7个字符是否是http://   
        m_url += 7;//m_url指向"192.168.110.129:10000/index.html"
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        m_url = strchr( m_url, '/' );//m_url指向"/index.html"
    }
    if ( !m_url || m_url[0] != '/' ) {//若找不到或者定位失误
        return BAD_REQUEST;
    }

    //当url只为/时，将judge.html粘到/屁股后面 即url指向/judge.html   对应用户在浏览器输入http://192.168.88.100:9190/。一定会发送一个get报文服务器会返回judge.html
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");
    //现在m_url指向“/judge.html”
    m_check_state = CHECK_STATE_HEADER;     //请求行就一行，请求行解析完后，将主状态机改为解析请求头部
    return NO_REQUEST;
}

// 解析HTTP请求的一个请求头部信息
HTTP_CODE http_conn::parse_headers(char* text) {   //text为请求头部的完整一行 当然，请求头部可能包含很多行，所以要多次调用该函数
    // 遇到空行，表示其余头部字段在之前已经解析完毕
    if( text[0] == '\0' ) {         //相当于终止条件
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 主状态机转移到CHECK_STATE_CONTENT状态
        //  这个值在请求头部中会有个字段表示 我们读取即可
        if ( m_content_length != 0 ) {      //即实际读取结果和预期结果不一致，主线程还要继续读取客户数据
            m_check_state = CHECK_STATE_CONTENT;//主状态机状态转移 转移成解析请求体
            return NO_REQUEST;                  //返回http请求结果 即继续读取客户数据 读取客户请求的请求体部分
        }
        // 若请求体长度为0，说明我们已经得到了一个完整的HTTP请求，即GET_REQUEST
        return GET_REQUEST;
    } 
    else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) {// 判断前是11个字符是不是connection：
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;//text现在指向connection: 冒号后面的空格
        text += strspn( text, " \t" );//找到text中第一个不在" \t"中出现的下标，即跳过空格
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {//若connection方法是keep-alive
            m_linger = true;
        }
    } 
    else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {// 判断前15个字符是不是Content-Length，即请求体长度
        // 处理Content-Length头部字段
        text += 15;//text现在指向Content-Length: 冒号后面的空格
        text += strspn( text, " \t" );//找到text中第一个不在" \t"中出现的下标，即跳过空格
        m_content_length = atol(text);//字符转整数
    } 
    else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        // 处理Host头部字段
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    } 
    else {//我们在这里只解析三种请求头部 Host connection content_length,成熟的服务器还可以解析其他的请求头部
        printf( "oop! unknow header %s\n", text );
    }
    return NO_REQUEST;//当解析上面三种头部时，意味着头部尚未解析完全，因此http处理结果为继续解析
}

//解析http请求报文请求体 只是判断它是否被完整的读入了
HTTP_CODE http_conn::parse_content( char* text ) {
    //m_checked_index此时指向请求头部下一位 
    if ( m_read_idx >= ( m_content_length + m_checked_index ) )//请求体已被完整读入到text中
    {
        text[ m_content_length ] = '\0';//将text字符串第m_content_length位置为字符串结束符
        //POST请求中最后为输入的用户名和密码 保存到任务类m_string
        m_string = text;
        std::cout<<"m_string是"<<m_string<<std::endl;
        return GET_REQUEST;
    }
    return NO_REQUEST;//请求体尚未被完整读入到text中
}


//解析具体某一行
LINE_STATUS http_conn::parse_line() {
    char temp;

    //逐字符遍历主线程读入的readbuff数组，直到遇到行结束符，表示读到了完整的一行 
    for(; m_checked_index < m_read_idx; ++m_checked_index) {
        temp = readbuff[m_checked_index];
        if(temp == '\r') {
            if((m_checked_index+1) == m_read_idx) {
                return LINE_OPEN;//行数据尚不完整
            }
            else if(readbuff[m_checked_index+1] == '\n') {
                //将原来的每一行末尾的两字符'\r' '\n'改成字符串结束符'\0'
                readbuff[m_checked_index++] = '\0';
                readbuff[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp == '\n') {
            if( ( m_checked_index > 1) && ( readbuff[ m_checked_index - 1 ] == '\r' ) ) {
            //即行的定位不准 将\r当做上一行末尾 \n当做下一行开头
            readbuff[ m_checked_index-1 ] = '\0';
            readbuff[ m_checked_index++ ] = '\0';
            return LINE_OK;
            }
            return LINE_BAD;
        }
        //读取到其他类型字符 继续向后读
    }
    return LINE_OPEN;//行数据尚不完整
}

//做具体的处理 比如将申请的资源在服务器本地找到，写回给客户端 判断依据：\r\n
// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功

HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root);//将doc_root拷贝到m_real_file doc_root是本地保存网站各种资源的根目录  m_real_file为"/work/webServer/root"
    int len = strlen(doc_root);
    //printf("m_url:%s\n", m_url);
    //m_url在解析请求行中得到
    const char *p = strrchr(m_url, '/');//strrchr 是一个 C 语言标准库函数，用于在一个字符串中查找最后一个出现指定字符的位置。

    //处理cgi 当http报文为post cgi设置为1 只可能两种情况：m_url 为"/2CGISQL.cgi"(用户点击登录) m_url为"/3CGISQL.cgi"(用户点击注册)
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {

        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");//m_url_real指向"/"
        strcat(m_url_real, m_url + 2);//m_url_real指向"/CGISQL.cgi"
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);//m_real_file为"/work/webServer/root/CGISQL.cgi"
        //但是在post处理中，m_real_file并没有什么用，因为post的作用是在用户登录和注册行为中将用户名与密码放在http请求体中提供给server，以便进行后续sql操作
        free(m_url_real);//回收m_url_real指向的空间，因为其有效字段已经粘接到m_real_file

        //将用户名和密码从请求体中提取出来
        //user=123&password=123
        char name[100], password[100];
        int i;
        //从5开始时跳过user=，从用户名实际第一个字符开始
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';
        //name为"123\0" m_string[i]为'&'
        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';
        //password为123
        std::cout<<"提取出来的用户名密码为"<<name<<","<<password<<std::endl;
        printf("xqy1\n");
        if (*(p + 1) == '3')            //注册行为
        {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");
            //sql_insert为 INSERT INTO user(username, passwd) VALUES('123', '123')
            printf("xqy2\n");
            if (map_users.find(name) == map_users.end())//users是一个map表，在该map表中查找有无相同用户名
            {

                m_lock.lock();//因为这块上了锁，所以只会有一个线程池的工作线程访问这块临界代码区，从而任务类只保留有一个共享的mysql连接是合理的，因为同时只可能有一个线程调用这个连接
                //printf("显示：%d\n",mysql==NULL);//tmd就是这里的问题
                MYSQL *mysql=connection_pool::GetInstance()->GetConnection();//修补措施：按理来说这句不应该加的 因为任务类的成员mysql本来可以解决问题
                int res = mysql_query(mysql, sql_insert);//这里一定要保证mysql为有效连接，如果执行成功，返回0；否则返回非零值，表示出现了错误。
                map_users.insert(pair<string, string>(name, password));//更新map表，将新用户名字密码插入map表
                connection_pool::GetInstance()->ReleaseConnection(mysql);//手动归还连接，与788行对应
                m_lock.unlock();
                printf("xqy3\n");
                if (!res)//sql插入语句执行成功
                    strcpy(m_url, "/log.html");//针对用户注册行为，成功，将m_url更新为/log.html，用户界面转移到登录界面
                else
                    strcpy(m_url, "/registerError.html");//针对用户注册行为，失败，将m_url更新为/registerError.html，用户界面转移到注册失败界面
            }
            else//该用户之前已经注册过
                strcpy(m_url, "/registerError.html");
        }
        //如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')       //登录行为
        {
            if (map_users.find(name) != map_users.end() && map_users[name] == password)         //该用户存在，并且密码正确
                strcpy(m_url, "/welcome.html");//针对用户登录行为，成功，将m_url更新为/welcome.html，用户界面转移到欢迎界面
            else
                strcpy(m_url, "/logError.html");//针对用户登录行为，失败，将m_url更新为/logError.html，用户界面转移到登录失败界面
        }
    }

    if (*(p + 1) == '0')//当http报文为get cgi设置为0 用户直接输入http://192.168.88.100:9190/0，server返回register.html
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        //m_real_file为/work/webServer/root/register.html 

        free(m_url_real);
    }
    else if (*(p + 1) == '1')//当http报文为get cgi设置为0 用户直接输入http://192.168.88.100:9190/1，server返回log.html
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '5')//当http报文为get cgi设置为0 用户直接输入http://192.168.88.100:9190/5，server返回picture.html
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '6')//当http报文为get cgi设置为0 用户直接输入http://192.168.88.100:9190/6，server返回video.html
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '7')//当http报文为get cgi设置为0 用户直接输入http://192.168.88.100:9190/7，server返回fans.html
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else//发送m_url实际请求的文件 一般在这步发judge.html 即m_url为/，表示客户获取服务器默认资源
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    if (stat(m_real_file, &m_file_stat) < 0)//获取请求资源的状态信息
        return NO_RESOURCE;//服务器没有资源
    if (!(m_file_stat.st_mode & S_IROTH))//S_IROTH表示该文件是否可以被读取
        return FORBIDDEN_REQUEST;//客户对该资源无足够访问权限
    if (S_ISDIR(m_file_stat.st_mode))//该文件是目录
        return BAD_REQUEST;//客户请求语法错误
    int fd = open(m_real_file, O_RDONLY);
    // 创建内存映射 mmap返回值是void* 这个地址要保存下来 因为我们将来发送的时候发这个地址的内容
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);//关闭打开的客户请求文件
    return FILE_REQUEST;//表示已经获取到这个文件，获取成功
}


// 对内存映射区执行munmap操作
void http_conn::unmap() {
    if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = 0;//重置为NULL
    }
}




/*
以下是一个典型的 HTTP/1.1 请求报文的示例：

http
GET /index.html HTTP/1.1  
Host: www.example.com  
User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.3  
Accept-Language: en-US,en;q=0.8  
Accept-Encoding: gzip, deflate, sdch, br  
DNT: 1  
Connection: keep-alive  
Cookie: __cfduid=d1170008f290738a89c314e040000000; _ga=GA1.2.123456789.1512345678; ...  
Cache-Control: max-age=0

这个请求报文包含了以下部分：

请求行 (GET /index.html HTTP/1.1)：这里使用了 GET 方法请求 /index.html 资源，并指定了使用的 HTTP 协议版本为 1.1。
请求头 (Host, User-Agent, Accept, Accept-Language, Accept-Encoding, DNT, Connection, Cookie, Cache-Control 等)：这些是描述请求的各种属性和参数的键值对。每个请求头字段都以冒号分隔键和值，后面跟一个换行符。
请求体：在这个例子中，请求是一个 GET 请求，通常不包含请求体。如果是 POST 或 PUT 请求，请求体中会包含要发送到服务器的数据。



*/



/*
当然，以下是一个典型的 HTTP/1.1 响应报文的示例：

http
HTTP/1.1 200 OK  
Date: Sat, 31 Dec 2023 23:59:59 GMT  
Content-Type: text/html; charset=UTF-8  
Content-Length: 1234  
Connection: keep-alive  
Cache-Control: max-age=3600  
Expires: Sun, 01 Jan 2024 00:59:59 GMT  
Vary: Accept-Encoding  
Last-Modified: Sat, 31 Dec 2023 23:50:00 GMT  
Server: Apache/2.4.52 (Unix)  
ETag: "123456789"  
Accept-Ranges: bytes  
  
<!DOCTYPE html>  
<html lang="en">  
<head>  
    <meta charset="UTF-8">  
    <title>Example Page</title>  
</head>  
<body>  
    <h1>Welcome to the Example Page!</h1>  
    <p>This is an example of an HTTP/1.1 response.</p>  
    <!-- More HTML content here -->  
</body>  
</html>
这个响应报文包含了以下部分：

状态行 (HTTP/1.1 200 OK)：这里指定了 HTTP 协议版本为 1.1，状态码为 200，表示请求成功。状态描述为 "OK"。
响应头 (Date, Content-Type, Content-Length, Connection, Cache-Control, Expires, Vary, Last-Modified, Server, ETag, Accept-Ranges 等)：这些是描述响应的各种属性和参数的键值对。每个响应头字段都以冒号分隔键和值，后面跟一个换行符。
响应体：在状态行和响应头之后，是响应的主体内容。在这个例子中，主体是一个 HTML 文档，它包含了页面的结构和内容。实际的响应体内容会根据请求的资源和服务器返回的数据而有所不同。
*/