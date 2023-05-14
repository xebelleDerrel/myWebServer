#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdio>
#include <unistd.h>
#include <errno.h>
#include <cstdlib>
#include <cassert>
// #include <cassert>
#include <sys/epoll.h>

#include "./lock/locker.h"
#include "./http/http_conn.h"
#include "./threadpool/threadpool.h"


#define MAX_FD 65536            // 最大文件描述符
#define MAX_EVENT_NUMBER 10000      // 最大事件数

#define listenfdLT              // 水平触发阻塞

// 这三个函数在http_conn.cpp中定义，改变链接属性
extern void addfd(int epollfd, int fd, bool one_shot);
extern void removefd(int epollfd, int fd);
extern int setnonblocking(int fd);

// 设置定时器相关参数


static int epollfd = 0;


int main(int argc, char *argv[])
{
    if (argc <= 1)
    {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }

    int port = atoi(argv[2]);
    
    // 创建线程池

    threadpool<http_conn> *pool;
    try
    {
        pool = new threadpool<http_conn>;
    }
    catch(...)
    {
        return 1;
    }
    printf("创建线程池成功！！！\n");
    http_conn *users = new http_conn[MAX_FD];
    assert(users);


    // 创建服务端socket
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    // 设置服务器端口重用
    int flag = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    // 绑定端口
    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    
    // 监听端口
    ret = listen(listenfd, 5);
    assert(ret >= 0);

    // 创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(5);
    assert(epollfd != -1);
    
    // 不能将监听端口listenfd设置为EPOLLONESHOT否则会丢失客户连接
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    bool stop_server = false;


    while (!stop_server) {
        // 阻塞方式调用epoll_wait
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        
        // 若为EINTER代表阻塞过程被信号中断
        if (number < 0 && errno != EINTR) 
        {
            // 打印错误日志
            break;
        }
        for (int i = 0; i < number; ++i)  
        {
            int sockfd = events[i].data.fd;
            
            // 处理新到的客户连接
            if (sockfd == listenfd) 
            {
                // #ifdef DEBUG
                    printf("新的客户端连接 %d!\n", sockfd);
                // #endif
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
#ifdef listenfdLT
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength);
                if (connfd < 0) 
                {
                    // 打印错误日志
                    continue;
                }
                if (http_conn::m_user_count >= MAX_FD)
                {
                    // 超过最大任务处理量
                    // 打印错误日志
                    continue;
                }
                users[connfd].init(connfd, client_address);
#endif

#ifdef listenfdET
    /*
        对于监听的sockfd，最好使用水平触发模式，
        边缘触发模式会导致高并发情况下，有的客户端会连接不上。
        如果非要使用边缘触发，网上有的方案是用while来循环accept()
    */
#endif
            }

            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 对端出现错误
                // EPOLLRDHUP   ： 表示读关闭，并不是所有内核版本都支持
                // EPOLLHUP     ： EPOLLHUP 表示读写都关闭，表示对应的文件描述符被挂断；
                // EPOLLERR     ： 表示对应的文件描述符发生错误；

                /*
                    定时器相关的处理
                */
            }

            // 处理信号
            

            // 处理客户端上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                printf("客户端发来消息:\n");
                // 将收到的数据读入http对象的缓冲区
                if (users[sockfd].read_once())
                {
                    // 打印处理数据的日志
                    // users[sockfd].print_read_buf();
                    pool->append(&users[sockfd]);
                } 
                else
                {

                }
            }
            // 处理客户端上的读事件
            else if (events[i].events & EPOLLOUT)
            {

            }

        } 
    }

    close(epollfd);
    close(listenfd);

    delete [] users;
    delete pool;
    return 0;
}
