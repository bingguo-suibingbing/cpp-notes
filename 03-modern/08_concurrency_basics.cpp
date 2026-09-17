// ============================================================================
//  08_concurrency_basics.cpp
// ----------------------------------------------------------------------------
//  演示主题：
//    1. std::thread：join vs detach 的后果，为什么示例代码必须 join
//    2. C++20 std::jthread：析构自动 join + stop_token 协作式取消
//    3. std::mutex / lock_guard / unique_lock / scoped_lock（多锁防死锁）
//    4. std::atomic：为什么 i++ 不是原子的、fetch_add、memory_order 的实用解释
//    5. std::condition_variable：完整可运行的生产者-消费者队列
//    6. std::async / std::future / std::promise
//    7. 数据竞争 = 未定义行为（用一个「原子 vs 非原子」的对照实验说明）
//    8. 并发三条实用建议：能不用共享状态就不用、必须共享就用消息传递或锁、
//       锁的粒度要小
//
//  关键结论：
//    - 所有线程必须 join（或 jthread 自动 join）；detach + main 先退出 = 程序崩溃或结果丢失。
//    - 保护共享数据的是「同一个互斥量 + 所有访问路径都加锁」，不是「加了锁」。
//    - i++ 是「读-改-写」三步，多线程下必然丢更新；原子操作把它合成一步。
//    - 数据竞争是 UB：不是「偶尔算错」，而是编译器可以按「无竞争」假设做任何优化。
//    - 条件变量必须配谓词（while 循环）防止虚假唤醒；notify 越界调用也安全。
//    - 锁的粒度决定并发度：临界区里不要做 I/O、不要 sleep、不要调用未知回调。
// ============================================================================

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <future>
#include <iostream>
#include <mutex>
#include <numeric>
#include <optional>
#include <queue>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

std::mutex g_cout_mutex;  // 保护 std::cout，避免多线程输出交错

// 线程安全的打印：每个完整行只被一个线程写
void print_line(const std::string& text) {
    const std::lock_guard<std::mutex> lock(g_cout_mutex);
    std::cout << text << "\n";
}

// ---------------------------------------------------------------- 生产者消费者队列
// 有界阻塞队列：完整实现了「条件变量 + 谓词 + 关闭语义」
template <typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(std::size_t capacity) : capacity_(capacity) {}

    // 返回 false 表示队列已关闭且不会再放入数据
    bool push(T value) {
        std::unique_lock<std::mutex> lock(mutex_);
        // 谓词必须检查 closed_，否则关闭时生产者会永久等待
        not_full_.wait(lock, [this] { return queue_.size() < capacity_ || closed_; });
        if (closed_) {
            return false;
        }
        queue_.push(std::move(value));
        lock.unlock();          // 先解锁再通知，减少被唤醒线程立刻阻塞的概率
        not_empty_.notify_one();  // 有一个消费者可以干活了
        return true;
    }

    // 返回 nullopt 表示队列已关闭且已排空
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] { return !queue_.empty() || closed_; });
        if (queue_.empty()) {
            return std::nullopt;  // 关闭且排空，消费者应该退出
        }
        T value = std::move(queue_.front());
        queue_.pop();
        lock.unlock();
        not_full_.notify_one();  // 有一个生产者可以继续投递
        return value;
    }

    void close() {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        // 关闭是「状态变化」，可能唤醒所有等待者，所以必须 notify_all
        not_empty_.notify_all();
        not_full_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::queue<T> queue_;
    std::size_t capacity_;
    bool closed_{false};
};

// ---------------------------------------------------------------- 死锁演示
// 用 scoped_lock 一次性锁住多把锁，避免 AB-BA 死锁
struct Account {
    std::string owner;
    int balance;
    std::mutex mutex;
};

// 错误示范：分别加锁 -> 两个线程以相反顺序访问就会死锁（这里不实际调用）
void transfer_unsafe(Account& from, Account& to, int amount) {
    std::lock_guard<std::mutex> lock_from(from.mutex);
    std::lock_guard<std::mutex> lock_to(to.mutex);  // 若另一线程反向加锁 -> 死锁
    from.balance -= amount;
    to.balance += amount;
}

// 正确示范：std::scoped_lock 用「避免死锁的算法」同时锁住两把锁
void transfer_safe(Account& from, Account& to, int amount) {
    const std::scoped_lock lock(from.mutex, to.mutex);  // C++17，等价于 std::lock + lock_guard
    from.balance -= amount;
    to.balance += amount;
}

// ---------------------------------------------------------------- 性能实测
template <typename Fn>
double time_ms(Fn&& fn, int repeats) {
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < repeats; ++i) {
        fn();
    }
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

}  // namespace

int main() {
    std::cout << "==== 1. std::thread：join vs detach ====\n";
    {
        // join：调用方阻塞等线程结束，这是示例代码必须用的方式
        std::thread worker([] {
            print_line("  [worker] 开始工作");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            print_line("  [worker] 工作完成");
        });
        std::cout << "  joinable() = " << (worker.joinable() ? "true" : "false") << "\n";
        worker.join();
        std::cout << "  join 之后 joinable() = " << (worker.joinable() ? "true" : "false")
                  << "\n";

        // std::thread 析构时若仍 joinable，直接 std::terminate，这是刻意的设计
        std::cout << "  规则：std::thread 析构前必须 join 或 detach，否则进程直接终止\n";

        // detach 的后果演示：这里刻意让被 detach 的线程活得比 main 长一点点
        std::cout << "  detach 后果演示（这里用 sleep 保证观察得到，真实代码不该这样）:\n";
        std::atomic<bool> detached_done{false};
        std::thread detached([&detached_done] {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            detached_done = true;
            std::cout << "  [detached] 我还在跑，但没人能 join 我\n";
        });
        detached.detach();
        std::cout << "  detach 后 joinable() = " << (detached.joinable() ? "true" : "false")
                  << "，主线程无法再等待它\n";
        // 用一次「等待标志」代替 join，只是为了演示；正规做法就是 jthread
        while (!detached_done.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::cout << "  detached 线程已完成。若 main 此时先退出，它可能在访问已销毁对象 -> UB\n";
    }

    std::cout << "\n==== 2. std::jthread：自动 join + stop_token ====\n";
    {
        {
            // 离开作用域时自动 join，不需要手写 join，异常路径也安全
            std::jthread worker([](std::stop_token token) {
                int ticks = 0;
                while (!token.stop_requested()) {  // 协作式取消：线程自己检查
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    ++ticks;
                }
                std::cout << "  [jthread] 收到停止请求，已跑 " << ticks << " 次\n";
            });
            std::cout << "  jthread 创建后 joinable() = " << (worker.joinable() ? "true" : "false")
                      << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            worker.request_stop();  // 请求停止，线程在下一个检查点退出
            std::cout << "  已调用 request_stop()，等待 jthread 析构自动 join\n";
        }
        std::cout << "  jthread 已析构（自动 join 完成），程序继续安全执行\n";
        std::cout << "  对比：同样的代码用 std::thread + detach 就是不确定性崩溃的源头\n";
    }

    std::cout << "\n==== 3. 互斥量与锁的选择 ====\n";
    {
        int counter = 0;
        std::mutex counter_mutex;

        // lock_guard：最简单的 RAII 锁，不能手动解锁、不能条件等待
        {
            const std::lock_guard<std::mutex> lock(counter_mutex);
            counter += 1;
        }
        std::cout << "  lock_guard 保护后 counter = " << counter << "\n";

        // unique_lock：更灵活（可解锁/重锁/交给条件变量），代价是稍大稍慢
        {
            std::unique_lock<std::mutex> lock(counter_mutex);
            counter += 1;
            lock.unlock();            // 可以提前释放
            counter += 1;             // 故意在无锁状态下改：单线程这里安全，多线程就是竞争
            lock.lock();              // 可以再锁回来
            counter += 1;
        }
        std::cout << "  unique_lock 后 counter = " << counter
                  << "（注意中间那段无锁修改在多线程下是错的）\n";

        // 多锁：scoped_lock 一次性锁住，内部使用避免死锁的算法
        Account alice{"alice", 1000, {}};
        Account bob{"bob", 1000, {}};
        transfer_safe(alice, bob, 250);   // 同一线程演示，语义与多线程一致
        transfer_safe(bob, alice, 100);
        std::cout << "  scoped_lock 转账后 alice = " << alice.balance
                  << ", bob = " << bob.balance << "（总和守恒 = "
                  << alice.balance + bob.balance << "）\n";
        (void)&transfer_unsafe;  // 只演示签名，不调用：调用顺序不当就会死锁
        std::cout << "  选型：单锁用 lock_guard；要条件变量/手动解锁用 unique_lock；\n";
        std::cout << "        多把锁一律用 scoped_lock，绝不手写 lock/unlock 序列\n";
    }

    std::cout << "\n==== 4. std::atomic 与 memory_order ====\n";
    {
        constexpr int kThreads = 4;
        constexpr int kPerThread = 50000;

        // 非原子：数据竞争 -> UB，结果几乎一定小于理论值
        {
            int plain = 0;
            std::vector<std::thread> threads;
            threads.reserve(kThreads);
            for (int t = 0; t < kThreads; ++t) {
                threads.emplace_back([&plain] {
                    for (int i = 0; i < kPerThread; ++i) {
                        ++plain;  // 读-改-写三步，非原子
                    }
                });
            }
            for (auto& thread : threads) {
                thread.join();
            }
            std::cout << "  int 自增（无保护）      : " << plain << " / 期望 "
                      << kThreads * kPerThread << "，丢失更新\n";
        }

        // 原子：结果严格正确
        {
            std::atomic<int> atomic_counter{0};
            std::vector<std::thread> threads;
            threads.reserve(kThreads);
            for (int t = 0; t < kThreads; ++t) {
                threads.emplace_back([&atomic_counter] {
                    for (int i = 0; i < kPerThread; ++i) {
                        atomic_counter.fetch_add(1, std::memory_order_relaxed);
                    }
                });
            }
            for (auto& thread : threads) {
                thread.join();
            }
            std::cout << "  atomic 自增             : " << atomic_counter.load() << " / 期望 "
                      << kThreads * kPerThread << "，严格正确\n";
        }

        std::atomic<int> value{10};
        std::cout << "  fetch_add 返回旧值：fetch_add(5) = " << value.fetch_add(5)
                  << "，之后 value = " << value.load() << "\n";
        std::cout << "  compare_exchange_weak：CAS，是实现无锁结构的基本砖块\n";
        int expected = 15;
        const bool cas_ok = value.compare_exchange_strong(expected, 99);
        std::cout << "    compare_exchange_strong(期望 15 -> 99) = " << (cas_ok ? "true" : "false")
                  << "，value = " << value.load() << "\n";
        expected = 15;  // 现在值已经是 99，CAS 会失败并把实际值写回 expected
        const bool cas_fail = value.compare_exchange_strong(expected, 0);
        std::cout << "    再试一次（期望 15）= " << (cas_fail ? "true" : "false")
                  << "，expected 被更新为 " << expected << "（失败时会回写当前值）\n";

        std::cout << "  memory_order 实用解释：\n";
        std::cout << "    relaxed ：只保证这个变量自身原子，不保证与其他变量的顺序。\n";
        std::cout << "              适合纯计数（不需要与别的内存同步）。\n";
        std::cout << "    release ：本线程之前的写，在 store 之前对别人可见（发布）。\n";
        std::cout << "    acquire ：读到 release 的值后，能看到对方在 release 之前的所有写（订阅）。\n";
        std::cout << "    seq_cst ：默认值，全局单一顺序，最贵但最不容易写错。\n";
        std::cout << "  建议：先用默认 seq_cst 保证正确，profile 确认是瓶颈后再降级\n";
    }

    std::cout << "\n==== 5. 生产者-消费者队列（条件变量完整示例） ====\n";
    {
        BlockingQueue<std::string> queue(4);  // 故意把容量设小，让生产者真的会等待
        std::atomic<int> consumed{0};
        std::mutex result_mutex;
        std::vector<std::string> collected;

        constexpr int kItems = 12;

        std::thread producer([&queue] {
            for (int i = 0; i < kItems; ++i) {
                const std::string item = "任务-" + std::to_string(i);
                if (!queue.push(item)) {
                    break;
                }
            }
            queue.close();  // 生产完毕必须关闭，否则消费者永远等下去
            print_line("  [producer] 已投递全部任务并关闭队列");
        });

        std::vector<std::thread> consumers;
        consumers.reserve(2);
        for (int c = 0; c < 2; ++c) {
            consumers.emplace_back([&queue, &consumed, &result_mutex, &collected, c] {
                while (const auto item = queue.pop()) {
                    {
                        const std::lock_guard<std::mutex> lock(result_mutex);
                        collected.push_back(*item);
                    }
                    ++consumed;
                    (void)c;  // 消费者编号在本示例里只用于说明，不需要打印
                }
            });
        }

        producer.join();
        for (auto& consumer : consumers) {
            consumer.join();
        }

        std::cout << "  消费总数 = " << consumed.load() << " / 期望 " << kItems << "\n";
        std::sort(collected.begin(), collected.end());
        std::cout << "  收到的任务：";
        for (const auto& item : collected) {
            std::cout << item << ' ';
        }
        std::cout << "\n  要点：wait 用谓词防虚假唤醒；close 后要 notify_all；"
                     "两个条件变量分别管「非空」和「未满」\n";
    }

    std::cout << "\n==== 6. std::async / std::future / std::promise ====\n";
    {
        // async：把任务丢给运行时，返回 future
        auto slow_task = [](int x) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            return x * x;
        };
        std::future<int> f1 = std::async(std::launch::async, slow_task, 7);
        std::future<int> f2 = std::async(std::launch::async, slow_task, 9);
        // 两个任务并行执行，get 会阻塞到结果就绪
        std::cout << "  async 结果 = " << f1.get() << " 和 " << f2.get()
                  << "（两个任务并行，总耗时接近一个任务的耗时）\n";
        std::cout << "  f1.valid() = " << (f1.valid() ? "true" : "false")
                  << "（get 之后 future 失效，不能重复 get）\n";

        std::cout << "  launch::deferred 是陷阱：不指定策略时可能推迟到 get 才执行，"
                     "看起来像并行其实是串行\n";

        // promise / future：手工传递一个值（可跨线程、可作为一次性事件）
        std::promise<std::string> promise;
        std::future<std::string> future = promise.get_future();
        std::thread setter([&promise] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            promise.set_value("来自另一个线程的结果");
        });
        std::cout << "  promise/future 收到: " << future.get() << "\n";
        setter.join();

        // 异常也能通过 future 传回来
        std::promise<int> error_promise;
        std::future<int> error_future = error_promise.get_future();
        std::thread thrower([&error_promise] {
            try {
                throw std::runtime_error("子线程内部失败");
            } catch (...) {
                error_promise.set_exception(std::current_exception());
            }
        });
        try {
            (void)error_future.get();  // 在调用线程重新抛出
        } catch (const std::exception& e) {
            std::cout << "  异常通过 future 跨线程传播: " << e.what() << "\n";
        }
        thrower.join();
    }

    std::cout << "\n==== 7. 性能实测：原子 vs 互斥量 vs 无保护 ====\n";
    {
        constexpr int kThreads = 4;
        constexpr int kPerThread = 100000;

        auto run_plain = [] {
            int plain = 0;
            std::vector<std::thread> threads;
            threads.reserve(kThreads);
            for (int t = 0; t < kThreads; ++t) {
                threads.emplace_back([&plain] {
                    for (int i = 0; i < kPerThread; ++i) {
                        ++plain;
                    }
                });
            }
            for (auto& thread : threads) {
                thread.join();
            }
            return plain;
        };

        auto run_atomic = [] {
            std::atomic<int> value{0};
            std::vector<std::thread> threads;
            threads.reserve(kThreads);
            for (int t = 0; t < kThreads; ++t) {
                threads.emplace_back([&value] {
                    for (int i = 0; i < kPerThread; ++i) {
                        value.fetch_add(1, std::memory_order_relaxed);
                    }
                });
            }
            for (auto& thread : threads) {
                thread.join();
            }
            return value.load();
        };

        auto run_mutex = [] {
            int value = 0;
            std::mutex mutex;
            std::vector<std::thread> threads;
            threads.reserve(kThreads);
            for (int t = 0; t < kThreads; ++t) {
                threads.emplace_back([&value, &mutex] {
                    for (int i = 0; i < kPerThread; ++i) {
                        const std::lock_guard<std::mutex> lock(mutex);
                        ++value;
                    }
                });
            }
            for (auto& thread : threads) {
                thread.join();
            }
            return value;
        };

        constexpr int kRepeats = 3;
        const int plain_result = run_plain();
        const int atomic_result = run_atomic();
        const int mutex_result = run_mutex();

        const double t_plain = time_ms(run_plain, kRepeats);
        const double t_atomic = time_ms(run_atomic, kRepeats);
        const double t_mutex = time_ms(run_mutex, kRepeats);

        std::cout << "  " << kThreads << " 线程 x " << kPerThread << " 次自增，重复 "
                  << kRepeats << " 轮\n";
        std::cout << "  无保护 int   : " << t_plain << " ms（结果 " << plain_result
                  << "，错误 -> 这是数据竞争）\n";
        std::cout << "  atomic       : " << t_atomic << " ms（结果 " << atomic_result
                  << "，正确）\n";
        std::cout << "  mutex 保护   : " << t_mutex << " ms（结果 " << mutex_result
                  << "，正确但最慢）\n";
        std::cout << "  结论：\n";
        std::cout << "    - 无保护最快但结果是错的，快没有意义（UB 还可能让整个程序行为异常）\n";
        std::cout << "    - 原子操作通常比互斥量快，但争抢激烈时也会因缓存行冲突变慢\n";
        std::cout << "    - 互斥量适合保护「多步不变量」，原子适合保护「单个变量」\n";
        std::cout << "    - 数字随核数 / 机器 / 配置变化很大，趋势稳定，仅供参考\n";
    }

    std::cout << "\n==== 8. 数据竞争是未定义行为，不是「偶尔算错」 ====\n";
    std::cout << "  上面「无保护 int」的实验里，结果几乎总是小于期望值，但这不是全部危害：\n";
    std::cout << "  按 C++ 标准，数据竞争就是 UB，编译器可以假设它不存在，于是：\n";
    std::cout << "    - 把循环优化成「只写一次」（因为假设没有别的线程能看到中间状态）\n";
    std::cout << "    - 把变量长期留在寄存器里，别的线程永远看不到更新\n";
    std::cout << "    - 重排指令顺序，让「先写标志再写数据」被别人观察到反过来\n";
    std::cout << "  检测手段：\n";
    std::cout << "    - ThreadSanitizer（clang / gcc 的 -fsanitize=thread）能直接报出竞争点\n";
    std::cout << "    - MSVC 可用 /analyze 与并发可视化工具；更实用的是代码评审 + 明确的所有权约定\n";
    std::cout << "    - 压力测试只能提高发现概率，不能证明没有竞争\n";

    std::cout << "\n==== 9. 并发三条实用建议 ====\n";
    std::cout << "  1) 能不用共享状态就不用：数据拷贝一份给每个线程，或者让每个线程只碰自己的分片，\n";
    std::cout << "     最后再合并（map-reduce 的思路）。无共享 = 无锁 = 无竞争。\n";
    std::cout << "  2) 必须共享就用消息传递或锁：优先「队列 + 单向数据流」（本例的生产者消费者），\n";
    std::cout << "     其次才是共享内存 + 互斥量；共享可变状态越多，正确性越难保证。\n";
    std::cout << "  3) 锁的粒度要小：临界区里只做内存操作，不要做 I/O、不要 sleep、\n";
    std::cout << "     不要调用可能回调外部代码的函数（回调里再抢同一把锁就是自死锁）。\n";
    std::cout << "  补充：优先用 std::jthread / std::scoped_lock / std::atomic 这些「不容易写错」的工具；\n";
    std::cout << "        自己手写 lock/unlock 的地方，就是 bug 会藏身的地方\n";

    std::cout << "\n==== 小结 ====\n";
    std::cout << "  线程必须 join（jthread 自动 join）；取消用 stop_token 协作完成\n";
    std::cout << "  共享可变状态 -> 互斥量；单变量 -> atomic；跨线程传值 -> future/promise/队列\n";
    std::cout << "  数据竞争是 UB，先保证正确，再谈性能\n";
    return 0;
}
