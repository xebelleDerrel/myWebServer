#ifndef HTTP_CONN_H
#define HTTP_CONN_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>


#include <sys/stat.h>
#include <cstring>

#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <stdarg.h> // VA_LIST
#include <errno.h>
#include <sys/uio.h>


#define PRINTDEBUG

class http_conn
{
public:
    // 设置读取文件的名称m_real_file大小
    static const int FILENAME_LEN = 200;
    // 设置读缓冲区m_read_buf大小
    static const int READ_BUFFER_SIZE = 2048;
    // 设置写缓冲区m_write_buf大小
    static const int WRITE_BUFFER_SIZE = 1024;
    // 报文请求的方法，本项目只用到GET和POST
    enum METHOD 
    {
        GET = 0, 
        POST, 
        HEAD, 
        PUT, 
        DELETE, 
        TRACE, 
        OPTIONS, 
        CONNEXT, 
        PATH};
    // 主状态机的状态
    enum CHECK_STATE 
    {
        CHECK_STATE_REQUESTLINE = 0, 
        CHECK_STATE_HEADER, 
        CHECK_STATE_CONTENT 
    };
    // 从状态机的状态
    /*
        LINE_OK : 解析到完整的一行
        LINE_BAD ： 解析行出现语法错误
        LINE_OPEN ： 解析行不完整   
    
    */
    enum LINE_STATUS 
    {
        LINE_OK = 0, 
        LINE_BAD, 
        LINE_OPEN
    };
    // 报文解析的结果
    /*
        NO_REQUEST : 请求不完整，需要继续读取请求报文数据                    
        GET_REQUEST : 获得了完整的HTTP请求                
        BAD_REQUEST : HTTP请求报文有语法错误
        NO_RESOURCE : 
        FORBIDDEN_REQUEST, 文件不可读
        FILE_REQUEST, 表示请求文件存在，且可以访问
        INTERNAL_ERROR : 服务器内部错误，该结果在主状态机逻辑switch的default下，一般不会触发
        CLOSED_CONNECTION
    
    */
    enum HTTP_CODE 
    {
        NO_REQUEST,                         
        GET_REQUEST,                    
        BAD_REQUEST, 
        NO_RESOURCE, 
        FORBIDDEN_REQUEST, 
        FILE_REQUEST, 
        INTERNAL_ERROR, 
        CLOSED_CONNECTION
    };
public:
    http_conn() {}
    ~http_conn() {}

public:
    // 初始化套接字地址，函数内部会调用私有方法init
    void init(int sockfd, const sockaddr_in &addr);
    // 关闭http连接
    void close_conn(bool real_clost = true);
    
    void process();
    // 读取浏览器端发来的全部数据
    bool read_once();
    // 响应报文写入函数
    bool write();
    // 获取客户端套接字地址
    sockaddr_in *get_address() { return &m_address; }
    // 

    //

private:
    void init();
    // 从m_read_buf读取，并处理请求报文
    HTTP_CODE process_read();
    // 向m_write_buf写入请求报文数据
    bool process_write(HTTP_CODE ret);
    // 主状态机解析报文中的请求行数据
    HTTP_CODE parse_request_line(char *text);
    // 主状态机解析报文中的请求头数据
    HTTP_CODE parse_headers(char *text);
    // 主状态机请求解析报文中的请求内容
    HTTP_CODE parse_content(char *text);
    // 生成响应报文
    HTTP_CODE do_request();

    // m_start_line是已经解析的字符数
    // get_line用于将指针向后偏移，指向未处理的字符
    char *get_line() { return m_read_buf + m_start_line; }

    // 从状态机读取一行，分析是请求报文的哪一部分
    LINE_STATUS parse_line();

    // 取消映射
    void unmap();
    // 根据响应报文格式，生成对应8个部分，以下函数均由do_request调用
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();




public:
    static int m_epollfd;                   // epoll句柄
    static int m_user_count;                // 未知

private:
    int m_sockfd;                           // http请求对应的客户端socket   
    sockaddr_in m_address;                  // 客户端socket地址

    char m_read_buf[READ_BUFFER_SIZE];      // 存储读取的请求报文数据
    int m_read_idx;                         // 缓冲区中m_read_buf中数据的最后一个字节的下一个位置
    int m_checked_idx;                      // m_read_buf读取的位置
    int m_start_line;                       //  m_read_buf中已经解析的字符个数

    char m_write_buf[WRITE_BUFFER_SIZE];    // 存储发出的响应报文数据
    int m_write_idx;                        // 指示buffer中的长度

    CHECK_STATE m_check_state;              // 主状态机的状态
    METHOD  m_method;                       // 请求方法
    
    // 以下是解析请求报文中对应的6个变量
    // 存储读取文件的名称
    char m_real_file[FILENAME_LEN];
    char *m_url;
    char *m_version;
    char *m_host;
    int m_content_length;
    bool m_linger;                          // 是否为长连接

    char *m_file_address;                   // 读取服务器上的文件地址
    struct stat m_file_stat;                // ?
    struct iovec m_iv[2];                   // m_iv[0]指向响应报文缓冲区，m_iv[1]指向打开的文件，io向量机制iovec，配合readv/writev
    int m_iv_count;                         // ?
    int cgi;                                // 是否启用POST
    char *m_string;                         // 存储请求头数据（用户名+密码）
    int bytes_to_send;                      // 剩余发送字节数
    int bytes_have_send;                    // 已发送字节数

#ifdef PRINTDEBUG
public:
    void print_read_buf   () { printf("%s\n", m_read_buf); };
    void print_resolve_buf();
    int get_sockfd     () { return m_sockfd; }
#endif

};





#endif