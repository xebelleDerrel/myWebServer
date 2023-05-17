#ifndef LIST_TIMER_H
#define LIST_TIMER_H
#include <time.h>
#include <netinet/in.h>
// 连接资源结构体成员需要用到定时器类
// 需要前向声明
class util_timer;

// 连接资源
struct client_data
{
  // 客户端socket地址
  sockaddr_in address;

  // socket文件描述符
  int sockfd;

  // 定时器
  util_timer *timer;
};

// 定时器类
class util_timer
{
public:
  util_timer() : prev(NULL), next(NULL) {}

public:
  // 超时时间
  time_t expire;
  // 回调函数，函数指针
  void (*cb_func)(client_data *);
  // 连接资源
  client_data *user_data;
  // 前向定时器
  util_timer *prev;
  // 后继定时器
  util_timer *next;
};

// 定时器容器类
class sort_timer_list
{
public:
  sort_timer_list() : head(NULL), tail (NULL) {}
  // 常规销毁链表
  ~sort_timer_list()
  {
    util_timer *tmp = head;
    while (tmp)
    {
      head = tmp->next;
      delete tmp;
      tmp = head;
    }
  }

    // 添加定时器
    void add_timer(util_timer *timer)
    {
      if (!timer)
      {
        return;
      }
      if (!head)
      {
        head = tail = timer;
        return;
      }
      // 如果新的定时器的超时时间小于当前头部节点
      // 直接将当前定时器节点作为头部节点
      if (timer->expire < head->expire)
      {
        timer->next = head;
        head->prev  = timer;
        head = timer;
      }
      // 否则调用私有成员，调整内部节点
      add_timer(timer, head);
    }

    // 调整定时器，任务发生变化时，调整定时器在链表中的位置
    void adjust_timer(util_timer *timer)
    {
      if (!timer)
      {
        return;
      }
      util_timer *tmp = timer->next;

      // 被调整的定时器在链表尾部
      // 定时器超时值仍然小于下一个定时器超时值，不调整
      if (!tmp || (timer->expire < tmp->next->expire))
      {
        return;
      }

      // 被调整的定时器是表头节点，将定时器取出，重新插入
      if (timer == head)
      {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
      }

      // 被调整的定时器在内部，将定时器取出，重新插入
      else 
      {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
      }
    }
  // 定时任务处理函数
  void tick()
  {
    if (!head)
    {
      return;
    }

    // 获取当前时间
    time_t cur = time(NULL);
    util_timer *tmp = head;

    // 遍历定时器链表
    int cnt = 0;
    while (tmp)
    {
      // 链表为升序链表
      // 当时间小于定时器超时时间，后面的定时器也没到期
      if (cur < tmp->expire)
      {
        break;
      }
      // 当定时器到期，则调用回调函数，执行定时事件
      tmp->cb_func(tmp->user_data);

      // 将处理后的定时器从链表容器中删除，并重置头结点
      head = tmp->next;
      if (head)
      {
        head->prev = NULL;
      }
      delete tmp;
      tmp = head;
      cnt++;
    }
    
  }
  
    // 删除定时器
    void del_timer(util_timer *timer)
    {
      if (!timer)
      {
        return;
      }

      // 链表中只有一个定时器，需要删除该定时器
      if ((timer == head) && (timer == tail))
      {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
      }

      // 被删除的定时器为头节点
      if (timer == head)
      {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
      }

      // 被删除的定时器为尾节点
      if (timer == tail)
      {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
      }

      // 被删除的定时器在链表内部
      timer->prev->next = timer->next;
      timer->next->prev = timer->prev;
      delete timer;
    }

    // 打印链表
    void print_timer()
    {
      if (!head) 
        return;
      util_timer *cur_timer = head;
      int cnt = 1;
      while (cur_timer) 
      {
        printf("\t第%d个定时器将于< %s > 到期\n", cnt++, asctime(localtime(&cur_timer->expire)));
        cur_timer = cur_timer->next;
      }
    }
private:
  // 私有成员，被公有成员add_timer和adjust_time调用
  // 主要用于调整链表内部节点
  void add_timer(util_timer *timer, util_timer *lst_head)
  {
    util_timer *prev = lst_head;
    util_timer *tmp  = prev->next;
    
    // 遍历当前结点之后的链表，按照超时时间找到目标定时器对应的位置，常规双向链表插入操作
    while (tmp)
    {
      if (timer->expire < tmp->expire)
      {
        prev->next  = timer;
        timer->next = tmp;
        tmp->prev   = timer;
        timer->prev = prev;
        break;
      }
      prev = tmp;
      tmp  = tmp->next;
    }

    // 遍历完发现，目标定时器需要放到结尾处
    if (!tmp)
    {
      prev->next = timer;
      timer->prev = prev;
      timer->next = NULL;
      tail = timer;
    }
  }



private:
  // 头尾节点
  util_timer *head;
  util_timer *tail;
};



#endif