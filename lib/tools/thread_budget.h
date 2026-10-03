/******************************************************************************
 * thread_budget.h
 *
 * The extra threads node ordering may run (--threads): the initial separator
 * tries and the subtrees of the dissection take threads from it and give them
 * back, so together they never run more than the count asked for. The thread
 * that sets the budget is one of the count.
 *****************************************************************************/

#ifndef THREAD_BUDGET_H
#define THREAD_BUDGET_H

#include <atomic>

class thread_budget {
public:
        static void set(int total) {
                available().store(total > 1 ? total - 1 : 0);
        }
        static bool take() {
                int a = available().load();
                while (a > 0) {
                        if (available().compare_exchange_weak(a, a - 1)) return true;
                }
                return false;
        }
        static void give() {
                available().fetch_add(1);
        }

private:
        static std::atomic<int> & available() {
                static std::atomic<int> count(0);
                return count;
        }
};

#endif
