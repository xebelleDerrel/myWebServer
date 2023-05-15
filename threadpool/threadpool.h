#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"


#define THREADDEBUG

template <typename T>
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，m_requests是请求队列中最多允许的、等待处理的请求数量*/
    threadpool(int thread_number = 8, int max_request = 10000);
    ~threadpool();
    // 向任务队列中添加任务
    bool append(T *request); 

private:
    /*工作线程运行的函数，它不断从工作队列中去除任务并执行之*/
    static void *worker(void *arg);
    void run();
private:
    int m_thread_number;    // 线程池中的线程数
    int m_max_requests;     // 请求队列中允许的最大请求数
    pthread_t * m_threads;  // 描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue;  // 请求队列
    locker m_queuelocker;       // 保护请求队列的互斥锁
    sem m_queuestat;            // 是否有任务需要处理
    bool m_stop;                // 是否结束线程
};
template <typename T>
threadpool<T>::threadpool(int thread_number, int max_requests) : m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(NULL)
{
    if (thread_number <= 0 || max_requests <= 0) 
    {
        throw std::exception();
    }
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads) 
    {
        throw std::exception();
    }
    for (int i = 0; i < thread_number; i++) {

        
        // 创建线程
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) 
        {
            
            delete [] m_threads;
            throw std::exception();
        }
        // 线程分离
        if (pthread_detach(m_threads[i])) {
            delete [] m_threads;
            throw std::exception();
        }
        #ifdef THREADDEBUG
            printf("create the number of %d thread\n", i + 1);
        #endif
    }
}

// 销毁线程池
template <typename T>
threadpool<T>::~threadpool()
{
    delete [] m_threads;
    m_stop = true;
}

// 向工作队列中添加任务
template <typename T>
bool threadpool<T>::append(T *request) 
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        // 达到任务请求上限
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

// 线程池工作函数
template <typename T>
void *threadpool<T>::worker(void *arg) 
{
    threadpool<T> *pool = (threadpool<T>*)arg;
    pool->run();
    return pool;
}

// 线程池的工作逻辑,不断从工作队列里面取任务执行
template <typename T>
void threadpool<T>::run()
{
    while (!m_stop)
    {
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        
        T *request = m_workqueue.front();
        printf("取到一个任务，其sockfd为%d\n", request->get_sockfd()); 
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request) 
        {
            continue;
            
        }

        printf("开始处理任务\n");
        request->process();
    }
}


#endif