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
#include "./timer/list_timer.h"

#define MAX_FD 65536            // 最大文件描述符
#define MAX_EVENT_NUMBER 10000      // 最大事件数
#define TIMESLOT 5             //最小超时单位

#define listenfdLT              // 水平触发阻塞

// 这三个函数在http_conn.cpp中定义，改变链接属性
extern void addfd(int epollfd, int fd, bool one_shot);
extern void removefd(int epollfd, int fd);
extern int setnonblocking(int fd);

// 设置定时器相关参数
static int pipefd[2];
static sort_timer_list timer_list;
static int epollfd = 0;
// 信号处理函数
void sig_handler(int sig) 
{
    // 为保证函数的可重入性，保留原来的errno
    // 可重入性表示中断后再次进入该函数，环境变量与之前相同，不会丢失数据
    int save_errno = errno;
    int msg = sig;

    // 将信号值从管道写端写入，传输字符类型，而非整型
    /*
     * 管道是一种字节流，它不会关心写入的数据类型，只会按照字节写入。
     * 在这里，将信号值转换为字符类型是为了将信号值按照字节写入管道中，
     * 方便后续读取时进行解析。
    */
    // 信号值的范围是 1 到 64，因此可以用一个字节的值来表示每个信号。
    send(pipefd[1], (char *)&msg, 1, 0);

    // 将原来的errno赋值为当前的errno
    errno = save_errno;
}

// 设置信号函数
void addsig(int sig, void(handler)(int), bool restart = true)
{
    // 创建sigaction结构体变量
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));

    // 信号处理函数中仅仅发送信号值，不做对应逻辑处理
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    // 将所有信号添加到信号信号集中
    sigfillset(&sa.sa_mask);

    // 执行sigaction函数
    assert(sigaction(sig, &sa, NULL) != -1);
}

// 定时处理任务，重新定时不断触发SIGALARM信号
void timer_handler()
{
    printf("开始定时检查定时器队列是否有超时的定时器！\n");
    // printf("定时器当前状态：\n");
    // timer_list.print_timer();
    timer_list.tick();
    alarm(TIMESLOT);
}


// 定时器回调函数，删除非活动连接再socke上的注册事件，并关闭
void cb_func(client_data *user_data)
{
    // 删除活动在socket上的注册事件
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);

    // 关闭文件描述符
    close(user_data->sockfd);

    // 减少连接数
    http_conn::m_user_count--;
}



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

    // 创建管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);

    // 设置管道写端为非阻塞，为什么写端要非阻塞?
    // 为了避免管道缓冲区满了而阻塞写入，一般情况下，管道写入操作都是非阻塞的
    setnonblocking(pipefd[1]);


    // 设置管道读端为ET非阻塞
    addfd(epollfd, pipefd[0], false);

    // 传递给主循环的信号值，这里只关注SIGALRM和SIGTERM
    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);

    // 循环条件    
    bool stop_server = false;
    
    // 创建连接资源数组
    client_data *users_timer = new client_data[MAX_FD];

    // 超时标志
    bool timeout = false;
    
    // 每隔TIMESLOT时间触发SIGALARM信号
    alarm(TIMESLOT);

    while (!stop_server) {
        // 阻塞方式调用epoll_wait
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        
        // 若为EINTER代表阻塞过程被信号中断
        if (number < 0 && errno != EINTR) 
        {
            // 打印错误日志
            break;
        }
        // 轮询文件描述符
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
                
                // 初始化该连接对应的连接资源
                users_timer[connfd].address = client_address;
                users_timer[connfd].sockfd = connfd;

                // 创建定时器临时变量
                util_timer *timer = new util_timer;
                // 设置定时器对应的连接资源
                timer->user_data = &users_timer[connfd];
                // 设置回调函数
                timer->cb_func = cb_func;

                time_t cur = time(NULL);
                // 设置绝对超时时间
                
                timer->expire = cur + 3 * TIMESLOT;
                printf("客户端#%d定时器初始化，到期时间为%s\n", sockfd, asctime(localtime(&timer->expire)));
                // 创建该连接对应的定时器，初始化为前述临时变量
                users_timer[connfd].timer = timer;
                // 将该定时器添加到链表中
                timer_list.add_timer(timer);
                
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

                // 服务器端关闭连接，移除对应的定时器
                cb_func(&users_timer[sockfd]);

                util_timer *timer = users_timer[sockfd].timer;
                if (timer)
                {
                    timer_list.del_timer(timer); 
                }
            }

            // 处理信号
            else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN))
            {
                int sig;
                char signals[1024];

                // 从管道读端读出信号值，成功返回字节数，失败返回-1
                // 正常情况下，这里的ret返回值总是1，只有14和15两个ASCII码对应的字符
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1) 
                {
                    // handle the error
                    continue;
                }
                else if (ret == 0)
                {
                    // 对端关闭
                    continue;
                }
                else
                {
                    // 处理信号对应的逻辑
                    for (int i = 0; i < ret; ++i)
                    {
                        // 这里面明明是字符
                        switch ((signals[i]))
                        {
                        case SIGALRM:
                        {    
                            timeout = true;
                            break;
                        }
                        case SIGTERM:
                        {
                            stop_server = true;
                            break;
                        }
                        default:
                            break;
                        }
                    }
                }
            }

            // 处理客户端上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                // 创建定时器临时变量，将该连接对应的定时器取出来
                util_timer *timer = users_timer[sockfd].timer;
            
                // 将收到的数据读入http对象的缓冲区
                if (users[sockfd].read_once())
                {
                    // 打印处理数据的日志
                    
                    // 若检测到读事件，将该事件放入请求队列
                    pool->append(&users[sockfd]);

                    // 若有数据传输，则将定时器往后延迟3个单位
                    // 对其在链表上的位置进行调整
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        timer_list.adjust_timer(timer);
                    }
                } 
                else
                {
                    // 服务器端关闭连接，移除对应的定时器
                    cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_list.del_timer(timer);
                    }
                }
            }
            // 处理客户端上的读事件
            else if (events[i].events & EPOLLOUT)
            {
                // printf("处理写事件\n");

                util_timer *timer = users_timer[sockfd].timer;
                if (users[sockfd].write())
                {
                    
                    // 若有新的数据传输，则将定时器往后延迟3个单位
                    // 并对新的定时器在链表上的位置进行调整

                    // printf("写入成功\n");
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        timer_list.adjust_timer(timer);
                    }
                }
                else 
                {
                    // 服务器端关闭连接，移除对应的定时器
                    cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_list.del_timer(timer);
                    }
                }
            }

        }

        // 处理定时器并非必须事件，收到信号并不是立马处理
        // 完成读写事件后，再进行处理
        if (timeout)
        {
            timer_handler();
            timeout = false;
        } 
    }

    close(epollfd);
    close(listenfd);

    delete [] users;
    delete [] users_timer;
    delete pool;
    return 0;
}
