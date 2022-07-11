#include "http_conn.h"

using namespace std;

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

//测试用
const char* ok_200_form = "恭喜你访问成功！" ;

// 网站的根目录
const char* doc_root = "/home/sun/experiment/webserver/resources";
int http_conn::epollfd = -1;
int http_conn::user_num = 0;

http_conn::http_conn(){
};

http_conn::~http_conn(){
}

void addfd(int epollfd,int fd, bool oneshot);
void removefd(int epollfd,int fd);
void modfd(int epollfd,int fd,int ev);
void http_conn::init(int connfd,struct sockaddr_in address){ //初始化user数组中http_conn对象
    m_sockfd = connfd;
    m_address = address;
    //设置端口复用，这步好像没意义
    int reuse = 1;
    setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    addfd(epollfd,connfd,true); //给通信的socket注册epoll事件
    user_num++;  //总的用户数量+1
    init();
    return;
}

void http_conn::init(){
    m_check_state = CHECK_STATE_REQUESTLINE;    // 初始状态为检查请求行
    m_linger = false;       // 默认不保持链接  Connection : keep-alive保持连接

    m_method = GET;         // 默认请求方式为GET
    m_url = 0;              
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, READ_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}

void http_conn::closefd(){  //关闭客户端连接，取消epoll事件，置m_socket为-1，代表关闭了该连接
    if (m_sockfd != -1){
        removefd(epollfd,m_sockfd); //remove的同时已经close掉了当前的m_sockfd
        m_sockfd = -1;  
        user_num --;
    }
    return;
}

bool http_conn::mread(){
    if (m_read_idx >= READ_BUFFER_SIZE){
        return false;
    }
    int byte_read = 0;
    while(true){
        byte_read = recv(m_sockfd,m_read_buf+m_read_idx,
                    READ_BUFFER_SIZE-m_checked_idx,0);
        if (byte_read == -1){
            if (errno == EAGAIN || errno == EWOULDBLOCK){//一直读，直到无数据
                break;   //无数据
            }
            return false; //读出错
        }
        else if (byte_read == 0){
            return false;
        }
        m_read_idx += byte_read;
    }
    //cout << m_read_buf << endl;
    return true;
}

//将http_conn写缓冲区的http响应报文写入TCP写缓冲区中
bool http_conn::mwrite(){
    int temp = 0;
    int byte_have_send  = 0;
    int byte_to_send = m_write_idx;
    if (byte_to_send == 0){ //已将所有数据写入TCP缓冲区中
        modfd(epollfd,m_sockfd,EPOLLIN);
        init();
        return true;
    }
    while(1){
        //分散写
        temp = writev(m_sockfd,m_iv,m_iv_count);
        if (temp <= -1){
            if (errno == EAGAIN){ // TCP写缓冲区已满,等待下一次EPOLLOUT事件
                modfd(epollfd,m_sockfd,EPOLLOUT);
                return true; //保持当前http_conn的连接
            }
            //出错
            unmap();    //取消内存映射
            return false;
        }
        byte_have_send += temp;
        byte_to_send -= temp;
        if (byte_to_send <= byte_have_send){
            //发送成功,一次数据交互结束
            unmap();
            if (m_linger){
                init();
                modfd(epollfd,m_sockfd,EPOLLIN);
                return true;
            }
            else{
                modfd(epollfd,m_sockfd,EPOLLIN); //可能没必要
                return false; //只要return false 主函数会帮忙关闭连接的
            }
        }
    }
    return false;
}

void setnoblocking(int fd){
    int old_option = fcntl(fd,F_GETFL);
    int new_option = old_option | O_NONBLOCK ;
    fcntl(fd,F_SETFL,new_option);  //设置文件描述符非阻塞
    return;
}
//添加要监听的文件描述符到epollfd中
void addfd(int epollfd,int fd, bool oneshot){ 
    epoll_event event; //用于封装待监听的文件描述符和待监听的事件
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLHUP;
    if (oneshot){ //同一个文件描述符上的事件无论如何都只会触发一次,为了防止一次没读完而反复触发，提高效率,2.防止线程冲突
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    //因为设置了只会触发一次,因此必须循环读取数据，因此文件描述符必须非阻塞
    setnoblocking(fd);
}

void removefd(int epollfd,int fd){
    epoll_event event;
    event.data.fd = fd;
    epoll_ctl(epollfd,fd,EPOLL_CTL_DEL,0);
    close(fd);
    return;
}

// 修改文件描述符，重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );
}

// 解析一行，判断依据\r\n
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for ( ; m_checked_idx < m_read_idx; ++m_checked_idx ) {
        temp = m_read_buf[ m_checked_idx ];
        if ( temp == '\r' ) {
            if ( ( m_checked_idx + 1 ) == m_read_idx ) {
                return LINE_OPEN;
            } else if ( m_read_buf[ m_checked_idx + 1 ] == '\n' ) {
                m_read_buf[ m_checked_idx++ ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if( temp == '\n' )  {
            if( ( m_checked_idx > 1) && ( m_read_buf[ m_checked_idx - 1 ] == '\r' ) ) {
                m_read_buf[ m_checked_idx-1 ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// 解析HTTP请求行，获得请求方法，目标URL,以及HTTP版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t"); // 判断第二个参数中的字符哪个在text中最先出现
    if (! m_url) { 
        return BAD_REQUEST;
    }
    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';    // 置位空字符，字符串结束符
    char* method = text;
    if ( strcasecmp(method, "GET") == 0 ) { // 忽略大小写比较
        m_method = GET;
    } else {
        return BAD_REQUEST;
    }
    // /index.html HTTP/1.1
    // 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标。
    m_version = strpbrk( m_url, " \t" );
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    if (strcasecmp( m_version, "HTTP/1.1") != 0 ) {
        return BAD_REQUEST;
    }
    /**
     * http://192.168.110.129:10000/index.html
    */
    if (strncasecmp(m_url, "http://", 7) == 0 ) {   
        m_url += 7;
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        m_url = strchr( m_url, '/' );
    }
    if ( !m_url || m_url[0] != '/' ) {
        return BAD_REQUEST;
    }
    m_check_state = CHECK_STATE_HEADER; // 检查状态变成检查头
    return NO_REQUEST;
}

// 解析HTTP请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char* text) {   
    // 遇到空行，表示头部字段解析完毕
    if( text[0] == '\0' ) {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if ( m_content_length != 0 ) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            m_linger = true;
        }
    } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol(text);
    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        // 处理Host头部字段
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    } else {
        //printf( "oop! unknow header %s\n", text );
    }
    return NO_REQUEST;
}

// 我们没有真正解析HTTP请求的消息体，只是判断它是否被完整的读入了
http_conn::HTTP_CODE http_conn::parse_content( char* text ) {
    if ( m_read_idx >= ( m_content_length + m_checked_idx ) )
    {
        text[ m_content_length ] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    // "/home/sun/experiment/webserver/resources"
    // cout << "url:" << m_url << endl;
    strcpy( m_real_file, doc_root );
    int len = strlen( doc_root );
    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 );
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if ( stat( m_real_file, &m_file_stat ) < 0 ) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if ( ! ( m_file_stat.st_mode & S_IROTH ) ) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if ( S_ISDIR( m_file_stat.st_mode ) ) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open( m_real_file, O_RDONLY );
    // 创建内存映射
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close( fd );
    return FILE_REQUEST;
}

void http_conn::unmap(){
    if (m_file_address){
        munmap(m_file_address,m_file_stat.st_size);
        m_file_address = 0;
    }
}

//主状态机，解析HTTP请求
http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    while(((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) 
            ||(line_status = parse_line()) == LINE_OK){//一行读取完毕
        text = get_line() ; //获取一行的数据
        m_start_line = m_checked_idx;
       // printf("got 1 http line:%s\n",text);
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:{
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST){
                return BAD_REQUEST;
            }
            break;
        }

        case CHECK_STATE_HEADER:{
            ret = parse_headers(text);
            if (ret == BAD_REQUEST){
                return BAD_REQUEST;
            }
            else if (ret == GET_REQUEST){
                return do_request();
            }
            break;
        }
        
        case CHECK_STATE_CONTENT:{
            ret = parse_content(text);
            if (ret == GET_REQUEST){
                return do_request();
            }
            line_status = LINE_OPEN;
            break;
        }
        
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
} 

// 往写缓冲中写入待发送的数据,是http_conn对象的写缓冲区
// 利用va_list进行可变参数的传入和解析
bool http_conn::add_response( const char* format, ... ) {
    if( m_write_idx >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ) {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    return true;
}

bool http_conn::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

bool http_conn::add_headers(int content_len) {//需要知道响应体的内容长度
    add_content_length(content_len); 
    add_content_type();
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

bool http_conn::process_write(HTTP_CODE ret){
    switch(ret){
        //下面所有都是往http_conn写缓冲区中写内容
        case INTERNAL_ERROR:{
            add_status_line(500,error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form)){
                return false; //http_conn对象的写缓冲区已满
            }
            break;
        }
        case BAD_REQUEST:{
            add_status_line(400,error_400_title);
            add_headers(strlen(error_400_form));
            if(! add_content(error_400_form)){
                return false;
            }
            break;
        }
        case NO_RESOURCE:{
            add_status_line(404,error_404_title);
            add_headers(strlen(error_404_form));
            if (! add_content(error_404_form)){
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:{
            add_status_line(403,error_403_title);
            add_headers(strlen(error_403_form));
            if (! add_content(error_403_form)){
                return false;
            }
            break;
        }
        case FILE_REQUEST:{
            //cout << "FILE_REQUEST" << endl;
            add_status_line(200,ok_200_title);
            add_headers(m_file_stat.st_size);   //回写真正的客户端所请求的资源
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len  = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            return true;
        }
        default:
            return false;
    }
    m_iv[0].iov_base = m_write_buf; //http_conn写缓冲区内容
    m_iv[0].iov_len = m_write_idx; //http_conn写缓冲区长度
    m_iv_count = 1;
    return true;
}

//工作线程处理的业务逻辑
void http_conn::process(){
    // 解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST){
        modfd(epollfd,m_sockfd,EPOLLIN);
        return;  //重置EPOLLONSESHOT事件后等待客户端继续发送报文
    }
    //生成响应,工作线程将响应报文写入http_conn对象的写缓冲区
    bool write_ret = process_write(read_ret); //根据HTTP解析结果执行不同的响应逻辑，返回不同的响应报文
    if (!write_ret){
        closefd();
        return;
    }
    //工作线程处理一次通信结束,通知主线程可以写入TCP缓冲区了
    //该线程继续休眠
    modfd(epollfd,m_sockfd,EPOLLOUT);
    return;
}
