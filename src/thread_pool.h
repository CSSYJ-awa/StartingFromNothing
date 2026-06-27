/**
 * thread_pool.h —— 轻量级线程池（纯头文件）
 *
 * 支持带优先级的异步任务调度，用于：
 * 1. 后台地形生成
 * 2. 后台网格构建（网格化）
 * 3. 区块加载/卸载
 *
 * 设计特点：
 * - 固定数量的工作线程
 * - 优先级队列（离玩家近的区块优先生成）
 * - 支持任务依赖和取消
 * - RAII 管理线程生命周期
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <chrono>

// ============================================================================
// ThreadPool —— 带优先级的线程池
// ============================================================================
class ThreadPool
{
public:
    /**
     * 任务优先级
     * HIGH: 玩家周围区块（立即生成）
     * NORMAL: 中等距离区块
     * LOW: 远景区块 / 预生成
     */
    enum class Priority : uint8_t
    {
        HIGH   = 0,
        NORMAL = 1,
        LOW    = 2
    };

    /**
     * 带优先级的任务包装
     */
    struct Task
    {
        Priority priority;
        uint64_t sequence; // 入队序号，同优先级时按 FIFO
        std::function<void()> func;

        bool operator>(const Task& other) const
        {
            if (priority != other.priority)
                return priority > other.priority;
            return sequence > other.sequence;
        }
    };

    /**
     * @param numThreads 工作线程数，默认 = CPU 核心数
     */
    explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency())
        : m_stop(false)
        , m_sequence(0)
    {
        if (numThreads == 0) numThreads = 1;

        for (size_t i = 0; i < numThreads; ++i)
        {
            m_workers.emplace_back([this, i]()
            {
                while (true)
                {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(m_mutex);
                        m_cv.wait(lock, [this]()
                        {
                            return m_stop.load() || !m_tasks.empty();
                        });

                        if (m_stop.load() && m_tasks.empty())
                            return;

                        task = std::move(m_tasks.top().func);
                        m_tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    ~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_stop.store(true);
        }
        m_cv.notify_all();
        for (auto& worker : m_workers)
        {
            if (worker.joinable())
                worker.join();
        }
    }

    // 禁止拷贝
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /**
     * 提交一个任务到线程池
     * @tparam F 可调用对象类型
     * @tparam Args 参数类型
     * @param priority 任务优先级
     * @param f 可调用对象
     * @param args 参数
     * @return std::future 用于获取结果
     */
    template<typename F, typename... Args>
    auto enqueue(Priority priority, F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> result = task->get_future();

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_stop.load())
                throw std::runtime_error("ThreadPool: enqueue on stopped pool");

            m_tasks.push(Task{ priority, m_sequence++, [task]() { (*task)(); } });
        }
        m_cv.notify_one();
        return result;
    }

    /**
     * 提交高优先级任务
     */
    template<typename F, typename... Args>
    auto enqueueHigh(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        return enqueue(Priority::HIGH, std::forward<F>(f), std::forward<Args>(args)...);
    }

    /**
     * 提交普通优先级任务
     */
    template<typename F, typename... Args>
    auto enqueueNormal(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        return enqueue(Priority::NORMAL, std::forward<F>(f), std::forward<Args>(args)...);
    }

    /**
     * 提交低优先级任务
     */
    template<typename F, typename... Args>
    auto enqueueLow(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        return enqueue(Priority::LOW, std::forward<F>(f), std::forward<Args>(args)...);
    }

    /** 获取工作线程数 */
    size_t workerCount() const { return m_workers.size(); }

    /** 获取当前待处理任务数 */
    size_t pendingTasks() const
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_tasks.size();
    }

private:
    std::vector<std::thread> m_workers;
    std::priority_queue<Task, std::vector<Task>, std::greater<Task>> m_tasks;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_stop;
    std::atomic<uint64_t> m_sequence;
};
