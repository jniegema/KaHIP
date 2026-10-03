#ifndef KAHIP_TASK_POOL_H
#define KAHIP_TASK_POOL_H

#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// A pool lasts for one ordering. Waiting callers help with queued work so
// nested separator searches cannot exhaust all workers and deadlock.
class task_pool {
public:
        explicit task_pool(int threads);
        ~task_pool();
        task_pool(const task_pool &) = delete;
        task_pool &operator=(const task_pool &) = delete;
        task_pool(task_pool &&) = delete;
        task_pool &operator=(task_pool &&) = delete;

        static int concurrency();

        class group {
        public:
                group();
                ~group();
                group(const group &) = delete;
                group &operator=(const group &) = delete;
                group(group &&) = delete;
                group &operator=(group &&) = delete;

                void run(std::function<void()> task);
                void wait();

        private:
                void drain();
                task_pool *m_pool;
                std::atomic<unsigned> m_pending;
                std::mutex m_mutex;
                std::condition_variable m_finished;
                std::exception_ptr m_error;
        };

private:
        void enqueue(std::function<void()> task);
        bool help();
        void worker();
        void stop();

        static thread_local task_pool *m_current;
        task_pool *m_previous;
        int m_concurrency;
        bool m_stopping;
        std::mutex m_mutex;
        std::condition_variable m_ready;
        std::deque<std::function<void()>> m_tasks;
        std::vector<std::thread> m_workers;
};

#endif
