#ifndef PRACTICE_THREAD_POOL_H
#define PRACTICE_THREAD_POOL_H

#include <mutex>
#include <queue>
#include <functional>
#include <future>
#include <thread>
#include <utility>
#include <vector>

template <typename T>
class SafeQueue
{
private:
    //任务队列
    std::queue<T> m_queue;

    //确保任意时刻任务队列只有一个操作在执行，比如在读大小的同时有线程进行了增删操作，就出问题了
    std::mutex m_mutex;
public:
    bool empty(){
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

    int size(){
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

    //队列添加任务
    void enqueue(T &t){
        std::unique_lock<std::mutex> lock(m_mutex);
        m_queue.emplace(t);
    }

    //队列取出任务
    bool dequeue(T &t){
        std::unique_lock<std::mutex> lock(m_mutex);

        if(m_queue.empty()){
            return false;
        }
        t = std::move(m_queue.front());
        m_queue.pop();
        return true;
    }
};

class ThreadPool
{
private:
    bool m_shutdown;//用于判断线程池是否关闭

    SafeQueue<std::function<void()>> m_queue;//任务队列

    std::vector<std::thread> m_threads;//工作线程队列

    std::mutex m_conditional_mutex;//线程池加锁，用以确保不会有两个线程取到同一个任务

    std::condition_variable m_conditional_lock;//条件变量，用来阻塞或唤醒线程

    //内置线程工作类
    class ThreadWorker
    {
    private:
        int m_id;//线程id
        ThreadPool *m_pool;
    public:
        ThreadWorker(ThreadPool *pool, const int id) : m_pool(pool), m_id(id){

        }

        //重载(),使得调用对象和调用函数一样
        void operator()(){
            std::function<void()> func;//定义基础函数对象

            bool dequeued;//是否取出队列中元素

            while(!m_pool -> m_shutdown){
                std::unique_lock<std::mutex> lock(m_pool ->m_conditional_mutex);

                //如果任务队列为空，阻塞当前线程
                if(m_pool -> m_queue.empty()){
                    m_pool -> m_conditional_lock.wait(lock);
                }

                //取出任务队列中的元素
                dequeued = m_pool->m_queue.dequeue(func);

                //取出成功，执行func
                if (dequeued){
                    func();
                }
            }
        }

    };
public:
    //线程池构造函数
    ThreadPool(const int n_threads = 4)
            : m_threads(std::vector<std::thread>(n_threads)), m_shutdown(false){

    }


    //初始化线程池
    void init(){
        for(int i = 0; i < m_threads.size(); ++i){
            m_threads.at(i) = std::thread(ThreadWorker(this, i));
        }
    }

    void shutdown()
    {
        m_shutdown = true;
        m_conditional_lock.notify_all(); // 通知，唤醒所有工作线程

        for (int i = 0; i < m_threads.size(); ++i)
        {
            if (m_threads.at(i).joinable()) // 判断线程是否在等待
            {
                m_threads.at(i).join(); // 将线程加入到等待队列
            }
        }
    }

    // Submit a function to be executed asynchronously by the pool
    template <typename F, typename... Args>
    auto submit(F &&f, Args &&...args) -> std::future<decltype(f(args...))>
    {
        // Create a function with bounded parameter ready to execute
        std::function<decltype(f(args...))()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...); // 连接函数和参数定义，特殊函数类型，避免左右值错误

        // Encapsulate it into a shared pointer in order to be able to copy construct
        auto task_ptr = std::make_shared<std::packaged_task<decltype(f(args...))()>>(func);

        // Warp packaged task into void function
        std::function<void()> warpper_func = [task_ptr]()
        {
            (*task_ptr)();
        };

        // 队列通用安全封包函数，并压入安全队列
        m_queue.enqueue(warpper_func);

        // 唤醒一个等待中的线程
        m_conditional_lock.notify_one();

        // 返回先前注册的任务指针
        return task_ptr->get_future();
    }
};

#endif //PRACTICE_THREAD_POOL_H
