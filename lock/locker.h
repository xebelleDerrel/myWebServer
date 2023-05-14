#ifndef LOCKER_H
#define LOCKER_H
#include <exception>
#include <pthread.h>
#include <semaphore.h>

// 信号量类
class sem 
{
public:
    // 初始化信号量(无初始值)
    sem() 
    {
        if (sem_init(&m_sem, 0, 0) != 0) {
            throw std::exception();
        } 
    }
    // 初始化信号量（有初始值）
    sem(int num) 
    {
        if (sem_init(&m_sem, 0, num) != 0) 
        {
            throw std::exception();
        }
    }
    // 销毁信号量
    ~sem()
    {
        sem_destroy(&m_sem);
    }
    //
    bool wait() 
    {
        return sem_wait(&m_sem);
    }
    //
    bool post()
    {
        return sem_post(&m_sem);
    }
private:
    sem_t m_sem;
};
// 互斥量类
class locker
{
public:
    // 初始化互斥量
    locker()
    {
        if (pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
    }
    // 销毁互斥量
    ~locker() 
    {
        pthread_mutex_destroy(&m_mutex);
    }
    // 互斥量加锁
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex);
    }
    // 互斥量解锁
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex);
    }
    // 获取锁
    pthread_mutex_t *get() 
    {
        return &m_mutex;
    }
private:
    pthread_mutex_t m_mutex;
};
// 条件变量类
class cond
{
public:
    // 初始化条件变量
    cond()
    {
        if (pthread_cond_init(&m_cond, NULL) != 0) {
            throw std::exception();
        }
    }
    // 销毁条件变量
    ~cond() 
    {
        pthread_cond_destroy(&m_cond);
    }
    // 
    bool wait(pthread_mutex_t *m_mutex) 
    {
        int ret = 0;
        // pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, m_mutex);
        // pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    //
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        int ret = 0;
        // pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        // pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    //
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }
    //
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    // static pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};

#endif