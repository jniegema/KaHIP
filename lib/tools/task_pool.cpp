#include "task_pool.h"
#include <utility>

thread_local task_pool *task_pool::m_current = nullptr;

task_pool::task_pool(int threads) : m_previous(m_current), m_concurrency(threads), m_stopping(false) {
        if (threads <= 0) return;
        try {
                for (int i = 1; i < threads; ++i) m_workers.emplace_back(&task_pool::worker, this);
        } catch (...) {
                stop();
                throw;
        }
        m_current = this;
}

task_pool::~task_pool() {
        if (m_concurrency > 0) {
                stop();
                m_current = m_previous;
        }
}

void task_pool::stop() {
        {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_stopping = true;
        }
        m_ready.notify_all();
        for (auto &worker : m_workers) worker.join();
}

int task_pool::concurrency() {
        return m_current ? m_current->m_concurrency : 1;
}

void task_pool::enqueue(std::function<void()> task) {
        {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_tasks.push_back(std::move(task));
        }
        m_ready.notify_one();
}

bool task_pool::help() {
        std::function<void()> task;
        {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_tasks.empty()) return false;
                task = std::move(m_tasks.back());
                m_tasks.pop_back();
        }
        task();
        return true;
}

void task_pool::worker() {
        m_current = this;
        for (;;) {
                std::function<void()> task;
                {
                        std::unique_lock<std::mutex> lock(m_mutex);
                        m_ready.wait(lock, [&] { return m_stopping || !m_tasks.empty(); });
                        if (m_tasks.empty()) return;
                        task = std::move(m_tasks.back());
                        m_tasks.pop_back();
                }
                task();
        }
}

task_pool::group::group() : m_pool(m_current), m_pending(0) {}

task_pool::group::~group() {
        drain();
}

void task_pool::group::run(std::function<void()> task) {
        ++m_pending;
        try {
                auto guarded = [this, task] {
                        try {
                                task();
                        } catch (...) {
                                std::lock_guard<std::mutex> lock(m_mutex);
                                if (!m_error) m_error = std::current_exception();
                        }
                        {
                                std::lock_guard<std::mutex> lock(m_mutex);
                                --m_pending;
                                m_finished.notify_all();
                        }
                };
                if (m_pool) m_pool->enqueue(guarded);
                else guarded();
        } catch (...) {
                --m_pending;
                throw;
        }
}

void task_pool::group::drain() {
        while (m_pending.load() != 0) {
                if (m_pool && m_pool->help()) continue;
                std::unique_lock<std::mutex> lock(m_mutex);
                m_finished.wait(lock, [&] { return m_pending.load() == 0; });
        }
        // The last worker must finish touching the condition variable before
        // the group can be destroyed, including when the fast path sees zero.
        std::lock_guard<std::mutex> lock(m_mutex);
}

void task_pool::group::wait() {
        drain();
        if (m_error) std::rethrow_exception(m_error);
}
