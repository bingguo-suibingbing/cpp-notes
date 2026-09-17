// =============================================================================
//  m3_smartptr.cpp —— 智能指针：一半打平，一半 Boost 仍有独门武器
//
//  结论速览：
//    * shared_ptr / unique_ptr / weak_ptr / enable_shared_from_this：两边打平，
//      新代码直接用 std:: 版本（标准库版本还有 C++20 atomic<shared_ptr>、
//      std::make_shared 的保证等）。
//    * intrusive_ptr：STL 没有对应物。引用计数放在对象内部，
//      省掉独立控制块（一次堆分配 + 一个指针大小的额外内存），
//      和已有 COM/自研引用计数体系对接非常自然。
//    * local_shared_ptr：STL 没有。非原子引用计数，单线程下明显更快。
// =============================================================================
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/smart_ptr/local_shared_ptr.hpp>
#include <boost/smart_ptr/make_local_shared.hpp>

#include "demo_api.h"
#include "demo_common.h"

using namespace demo;

// -----------------------------------------------------------------------------
// intrusive_ptr 要求的两个 ADL 钩子函数（必须和类型在同一个命名空间）
// -----------------------------------------------------------------------------
namespace demo {

struct Session {
    std::string name;
    // 引用计数就在对象自己身上 —— 没有独立控制块
    std::atomic<long> refcount{0};
    explicit Session(std::string n) : name(std::move(n)) {}
};

void intrusive_ptr_add_ref(Session* p) { p->refcount.fetch_add(1, std::memory_order_relaxed); }
void intrusive_ptr_release(Session* p) {
    if (p->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) delete p;
}

// local_shared_ptr 也要求同样的钩子（用普通 int 计数，不带原子操作）
struct LocalSession {
    std::string name;
    long refcount = 0;
    explicit LocalSession(std::string n) : name(std::move(n)) {}
};

void intrusive_ptr_add_ref(LocalSession* p) { ++p->refcount; }
void intrusive_ptr_release(LocalSession* p) { if (--p->refcount == 0) delete p; }

}  // namespace demo

void demo_m3_smart_ptr() {
    title("M3. 智能指针：shared_ptr 打平，intrusive_ptr 是 STL 的空白");

    // -------------------------------------------------------------------
    item("1) 通用部分：Boost 是标准库的前身，现在直接用 std:: 就行");
    {
        auto sp = std::make_shared<std::string>("stl");
        auto bp = boost::make_shared<std::string>("boost");
        side("STL  ", "std::make_shared   -> " + *sp);
        side("Boost", "boost::make_shared  -> " + *bp);
        line("");
        line("  std::unique_ptr / weak_ptr / enable_shared_from_this 都是从 Boost 搬过来的，");
        line("  所以这一层谁更好没有悬念：用标准库，少一个第三方依赖。");
    }

    // -------------------------------------------------------------------
    item("2) 独立控制块 vs 内嵌引用计数（intrusive_ptr 的真正价值）");
    {
        side("STL  ", "sizeof(std::shared_ptr<Session>)  = " +
                          std::to_string(sizeof(std::shared_ptr<Session>)) +
                          " 字节（对象指针 + 控制块指针 = 2 个指针）");
        side("Boost", "sizeof(boost::intrusive_ptr<Session>) = " +
                          std::to_string(sizeof(boost::intrusive_ptr<Session>)) +
                          " 字节（只有对象指针）");
        line("");
        side("STL  ", "shared_ptr 还要额外 new 一个控制块（原子计数 + 弱计数 + 删除器）：");
        line("          [Session 对象] + [控制块]  = 两次堆分配，缓存局部性差");
        side("Boost", "intrusive_ptr 的引用计数就在 Session 里：");
        line("          [Session{ name, atomic refcount }] = 一次堆分配");

        boost::intrusive_ptr<Session> a(new Session("A"));
        boost::intrusive_ptr<Session> b = a;   // 计数 -> 2，不碰堆
        side("Boost", "拷贝后 refcount = " + std::to_string(a->refcount.load()));
        b.reset();
        side("Boost", "reset 后 refcount = " + std::to_string(a->refcount.load()));
        line("");
        line("  什么时候真的需要它？");
        line("   - 对象已经有自己的引用计数（COM 的 AddRef/Release、老SDK、自研对象池）");
        line("   - 需要让对象生命周期完全由对象自身决定，避免 shared_ptr 控制块脱管");
        line("   - 海量小对象场景，想省掉控制块的分配和 16 字节开销");
        line("  代价：必须自己保证 add_ref/release 正确（写错就是内存泄漏，没有 RAII 兜底）。");
    }

    // -------------------------------------------------------------------
    item("3) local_shared_ptr：非原子引用计数（单线程专用加速）");
    {
        // 注意这不是线程安全的，只在"确定单线程"时用
        boost::local_shared_ptr<LocalSession> lp(new LocalSession("L"));
        side("Boost", "boost::local_shared_ptr 引用计数用普通 int，不加锁不插内存屏障");
        side("Boost", "对应 std::shared_ptr 一定是 atomic 计数 —— 单线程下纯属白付代价");
        line("");

        // 性能对比：反复拷贝/析构
        constexpr int N = 2000000;
        volatile long long sink = 0;

        auto sp = std::make_shared<int>(7);
        double t_std = time_ms([&] {
            for (int i = 0; i < N; ++i) {
                auto copy = sp;          // 原子 inc/dec
                sink += *copy;
            }
        });

        auto lsp = boost::make_local_shared<int>(7);
        double t_local = time_ms([&] {
            for (int i = 0; i < N; ++i) {
                auto copy = lsp;         // 非原子 inc/dec
                sink += *copy;
            }
        });

        char rel[160];
        side("STL  ", "std::shared_ptr       拷贝 " + std::to_string(N) + " 次: " + ms(t_std));
        side("Boost", "boost::local_shared_ptr 拷贝 " + std::to_string(N) + " 次: " + ms(t_local));
        std::snprintf(rel, sizeof rel, "  ==> local_shared_ptr 快 %.2f 倍（单线程场景）", t_std / t_local);
        line(rel);
        line("  危险提示：local_shared_ptr 跨线程共享会直接数据竞争，属于用错就炸的优化。");
        (void)sink;
    }

    // -------------------------------------------------------------------
    item("4) shared_array / aliasing（老代码里还能见到）");
    {
        // Boost 支持数组形式（C++17 起 std::shared_ptr<T[]> 也有了）
        boost::shared_array<int> arr(new int[4]{1, 2, 3, 4});
        side("Boost", "boost::shared_array<int> 自动 delete[]，C++17 前 STL 没这个");
        side("STL  ", "C++17 起用 std::shared_ptr<int[]>(new int[4]{1,2,3,4})，现在不需要 Boost 了");

        // aliasing constructor：两个库都有
        struct S { int a; double b; };
        auto owner = std::make_shared<S>();
        std::shared_ptr<double> alias(owner, &owner->b);   // 不增加堆分配
        side("STL  ", "aliasing 构造 std::shared_ptr<double>(owner, &owner->b) 也能做，打平");
        (void)alias;
        (void)arr;
    }

    // -------------------------------------------------------------------
    line();
    line("小结: 通用智能指针 —— 打平，用标准的；");
    line("      intrusive_ptr / local_shared_ptr —— Boost 独有，特定场景有真实收益。");
}
