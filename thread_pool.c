#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>


#define THREAD_NUM      5
#define TASK_NUM        10
#define COUNT_NUM       20 

#define CHECK_PTR(ptr) \
    if (!ptr)          \
        return NULL;



/* 任务队列结点 */
typedef struct t_task
{
    void * (*task_callback) (void *arg);
    void *arg;
    struct t_task *next;
} t_task;

typedef struct thread_pool
{
    pthread_t *threadId;                /* 动态数组 */
    pthread_mutex_t mutex_lock;         /* 互斥锁1: 用于互斥共享资源 */
    pthread_mutex_t mutex_busythrcnt_lock;         /* 互斥锁2: 用来互斥忙碌的线程数量 目的:提升效率 */
    pthread_cond_t mutex_cond_ptoc;     /* 条件变量 */
    pthread_cond_t mutex_cond_ctop;     /* 条件变量 */

    t_task * queue;                     /* 任务队列 */
    int     task_size;                  /* 任务数 */

    /* 最小、最大线程数量 */
    int min_thread_num;
    int max_thread_num;
    /* 忙碌线程数量 */
    int busy_thread_num;
    /* 当前线程数量 */
    int current_thread_num;

    /* 管理者线程 */
    pthread_t manager_thread;
    
    int shutdown;                   /* 使能关闭开关 */
} thread_pool;

/* 状态码 */
typedef enum STAT_CODE
{
    MALLOC_ERROR,
    THREAD_CREATE_ERROR,
    THREAD_MUTEX_LOCK_ERROR,
} STAT_CODE;

/* 函数前置声明 */
int threadPool_Init(thread_pool **pool, int threadNums,int min_thread_num,int max_thread_num);
/* 线程池的资源回收 */
int threadPool_ResourceFree(thread_pool *pool);
/* 线程池的内存释放 */
int threadPool_Free(thread_pool *pool);


/* 我是消费者 */
void * thread_func(void *arg)
{
    /* 处理任务 */
    thread_pool *tpool = (thread_pool *)arg;
    
    t_task *task = (t_task *)malloc(sizeof(t_task) * 1);
    memset(task, 0, sizeof(t_task));

    while (1)
    {
        pthread_mutex_lock(&(tpool->mutex_lock));
        while (tpool->task_size == 0 && !tpool->shutdown)
        {
            pthread_cond_wait(&(tpool->mutex_cond_ptoc), &(tpool->mutex_lock));
        }

        /* */
        if (tpool->shutdown)
        {
            printf("Workers'thread ID 0x%x is exiting\n", (unsigned int)pthread_self());
            pthread_mutex_unlock(&(tpool->mutex_lock));
            pthread_exit(NULL);
        }

        task = tpool->queue->next;      /* 这就是我的任务 */
        tpool->queue->next = task->next;
        tpool->task_size--;
        printf("pthread_self get it id:%ld\n", pthread_self());
        pthread_mutex_unlock(&(tpool->mutex_lock));
        pthread_cond_broadcast(&(tpool->mutex_cond_ctop));

        pthread_mutex_lock(&(tpool->mutex_busythrcnt_lock));
        tpool->busy_thread_num++;
        pthread_mutex_unlock(&(tpool->mutex_busythrcnt_lock));

        /* 执行回调函数 */
        task->task_callback(task->arg);

        pthread_mutex_lock(&(tpool->mutex_busythrcnt_lock));
        tpool->busy_thread_num--;
        pthread_mutex_unlock(&(tpool->mutex_busythrcnt_lock));
    }
    pthread_exit(NULL);
}

int checkThreadVaild(int *threadNums)
{
    int ret = 0;
    if (*threadNums > 10 || *threadNums <= 0)
    {
        *threadNums = THREAD_NUM;
    }
    return ret;
}

/* 管理者线程 */
void * manager_func(void *arg)
{
    thread_pool * t_pool = (thread_pool*)arg;

    while(1)
    {
        sleep(5);
        pthread_mutex_lock(&t_pool->mutex_lock);
        
        int busy_thread_num = t_pool->busy_thread_num;
        int task_num = t_pool->task_size;
        int current_thread_num = t_pool->current_thread_num;
        int max_thread_num = t_pool->max_thread_num;
        int min_thread_num = t_pool->min_thread_num;

        /* 增线程 */
        if(busy_thread_num * 2 < task_num  && current_thread_num < max_thread_num)
        {
            int add_thread_num  = (max_thread_num - current_thread_num) / 2;
            for(int idx = 0 ; idx < add_thread_num ; idx++)
            {
                pthread_create(t_pool->threadId,NULL,thread_func,NULL);

                t_pool->current_thread_num++;
            }

        }

        /* 减线程 */
        // if(busy_thread_num > task_num * 2 && current_thread_num > min_thread_num)
        // {
        //     int dl_thread_num = 
        // }
    }
    
}




/* 
 * @brief: 初始化线程池
 * @param1:
 * @param2:   
 */
int threadPool_Init(thread_pool **pool, int threadNums,int min_thread_num,int max_thread_num)
{
    int ret = 0;
    /* 校验合法性 */
    checkThreadVaild(&threadNums);

    thread_pool * t_pool = (thread_pool *)malloc(sizeof(thread_pool) * 1);
    if (!t_pool)
    {
        /* 状态码 */
        return MALLOC_ERROR;
    }
    memset(t_pool, 0, sizeof(thread_pool));
    /* 数组的方式存储线程池里面的线程Id号 */
    t_pool->threadId = (pthread_t *)malloc(sizeof(pthread_t) * threadNums);
    memset(t_pool->threadId, 0, sizeof(pthread_t) * threadNums);

    /* 管理者线程 */
    ret = pthread_create(&(t_pool->manager_thread), NULL, manager_func, t_pool);
    if (ret == -1)
    {
        return THREAD_CREATE_ERROR;
    }

    /* 循环创建工作线程 */
    for (int idx = 0; idx < threadNums; idx++)
    {
        ret = pthread_create(&(t_pool->threadId[idx]), NULL, thread_func, t_pool);
        if (ret == -1)
        {
            return THREAD_CREATE_ERROR;
        }

    }
    /* todo... */
    pthread_mutex_init(&(t_pool->mutex_lock), NULL);
    pthread_cond_init(&(t_pool->mutex_cond_ptoc),NULL);
    pthread_cond_init(&(t_pool->mutex_cond_ctop),NULL);
    
    
    

    /* 使用单向链表实现任务队列 */
    t_pool->queue = (t_task *)malloc(sizeof(t_task) * 1);
    if (t_pool->queue == NULL)
    {
        printf("malloc is error\n");
        /* TODO... */
    }

#if 0
    t_pool->queue->task_callback = NULL;
    t_pool->queue->arg = NULL;
    t_pool->queue->next = NULL;
#else
    memset(t_pool->queue, 0, sizeof(t_task));
#endif
    t_pool->task_size = 0;
    t_pool->busy_thread_num = 0;
    t_pool->max_thread_num = max_thread_num;
    t_pool->min_thread_num = min_thread_num;
    t_pool->current_thread_num = threadNums;
    /* 使能 让线程自己回收 */
    t_pool->shutdown = 0;       
    
    /* 指针赋值 */
    *pool = t_pool;
    return ret;
}

/* 生产者 */
int addTask(thread_pool *tpool, void *(*func)(void *), void *arg)
{
    /* 判断参数的合法性 */
    // CHECK_PTR(tpool);

    int ret = 0;    
    pthread_mutex_lock(&(tpool->mutex_lock));
    while(tpool->task_size == TASK_NUM)
    {
        pthread_cond_wait(&(tpool->mutex_cond_ctop),&(tpool->mutex_lock));
    }

    t_task *taskNode = (t_task *)malloc(sizeof(t_task) * 1);
    taskNode->task_callback = func;
    taskNode->arg = arg;
    taskNode->next = NULL;

    /* 将任务节点尾插到任务队列中 */
    t_task *travelTask = tpool->queue;
    while (travelTask->next != NULL)
    {
        travelTask = travelTask->next;
    }
    travelTask->next = taskNode;
    /* 任务数量加一 */
    tpool->task_size++;
    pthread_mutex_unlock(&(tpool->mutex_lock));
    pthread_cond_broadcast(&(tpool->mutex_cond_ptoc));

    return ret;
}


/* 销毁线程池 */
int threadPool_Destory(thread_pool *pool)
{
    int ret = 0;
    ret = threadPool_ResourceFree(pool);
    ret = threadPool_Free(pool);
    return ret;
}

/* 线程池的资源回收 */
int threadPool_ResourceFree(thread_pool *pool)
{
    int ret = 0;
    pool->shutdown = 1;

    /* 先销毁管理者线程 */
    pthread_join(pool->manager_thread, NULL);


    for (int idx = 0; idx < pool->current_thread_num; idx++)
    {
        pthread_cond_broadcast(&(pool->mutex_cond_ptoc));
    }

    for (int idx = 0; idx < pool->current_thread_num; idx++)
    {
        pthread_join(pool->threadId[idx], NULL);
    }

    return ret;
}

/* */
int threadPool_Free(thread_pool *pool)
{   
    int ret = 0;

    /* 释放线程 */
    if (pool->threadId)
    {
        free(pool->threadId);
        pool->threadId = NULL;
    }

    /* 释放队列 */
    while(pool->queue->next != NULL)
    {
        t_task *freeNode = pool->queue->next;
        pool->queue->next = freeNode->next;
        free(freeNode);
    }
    free(pool->queue);

    /* 释放锁资源 和 条件变量 */
    pthread_mutex_destroy(&(pool->mutex_lock));
    pthread_cond_destroy(&(pool->mutex_cond_ptoc));
    pthread_cond_destroy(&(pool->mutex_cond_ptoc));

    /* 释放线程池 */
    if (pool)
    {
        free(pool);
        pool = NULL;
    }
    return ret;
}


void * printf_func(void *arg)
{
    printf("hello\n");

    return NULL;
}

int main()
{   
    thread_pool *t_pool = (thread_pool *)malloc(sizeof(thread_pool) * 1);
    memset(t_pool, 0, sizeof(t_pool));

    threadPool_Init(&t_pool, THREAD_NUM,5,20);
    for (int idx = 0; idx < COUNT_NUM; idx++)
    {
        addTask(t_pool, printf_func, NULL);
    }
    threadPool_Destory(t_pool);


    return 0;
}
