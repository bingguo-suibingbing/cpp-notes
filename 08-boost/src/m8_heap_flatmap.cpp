// =============================================================================
//  m8_heap_flatmap.cpp —— 堆与"更底层"的容器
//
//  结论速览：
//    * boost::heap 提供 4 种可合并堆（binomial / fibonacci / pairing / skew），
//      STL 只有 std::priority_queue（基于 vector 的二叉堆，不能合并、不能改键）。
//    * boost::container 提供 flat_map/flat_set/stable_vector/small_vector 等
//      "STL 没有但很有用"的容器，而且 API 尽量兼容 STL。
//    * 这些是 Boost 的"增量价值"：不是替代 STL，而是补充 STL。
// =============================================================================
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <map>
#include <queue>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/container/flat_map.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/heap/binomial_heap.hpp>
#include <boost/heap/fibonacci_heap.hpp>
#include <boost/heap/pairing_heap.hpp>

#include "demo_api.h"
#include "demo_common.h"

using namespace demo;

void demo_m8_heap_flatmap() {
    title("M8. 堆与容器补充：STL 没有的增量能力");

    // ===================================================================
    item("1) boost::heap 的可合并堆 vs std::priority_queue");
    {
        // std::priority_queue：底层是 vector 的二叉堆，没有 merge()
        std::priority_queue<int> spq;

        // boost::binomial_heap：支持 merge / 迭代器 / 修改键值
        boost::heap::binomial_heap<int> bh;

        std::mt19937 rng(20260314);
        std::uniform_int_distribution<int> dist(0, 1000000);

        constexpr int N = 200000;
        std::vector<int> data(N);
        for (auto& v : data) v = dist(rng);

        double t_std_push = time_ms([&] {
            for (int v : data) spq.push(v);
        });
        double t_boost_push = time_ms([&] {
            for (int v : data) bh.push(v);
        });

        side("STL  ", "std::priority_queue push " + std::to_string(N) + " 次: " + ms(t_std_push));
        side("Boost", "binomial_heap       push " + std::to_string(N) + " 次: " + ms(t_boost_push));

        volatile long long sink = 0;
        double t_std_pop = time_ms([&] {
            while (!spq.empty()) { sink += spq.top(); spq.pop(); }
        });
        double t_boost_pop = time_ms([&] {
            while (!bh.empty()) { sink += bh.top(); bh.pop(); }
        });
        side("STL  ", "std::priority_queue 全弹出: " + ms(t_std_pop));
        side("Boost", "binomial_heap       全弹出: " + ms(t_boost_pop));
        line("");
        line("  注意：单纯 push/pop，std::priority_queue 通常更快（连续内存 + 简单下沉）。");
        line("  boost::heap 的价值不在速度，而在 STL 根本没有的能力 —— 见下一条。");
        (void)sink;
    }

    // ===================================================================
    item("2) merge：把两个堆 O(log n) 合并（std::priority_queue 做不到）");
    {
        boost::heap::pairing_heap<int> h1, h2;
        for (int v : {1, 5, 9, 13})  h1.push(v);
        for (int v : {2, 6, 10, 14}) h2.push(v);

        side("Boost", "合并前 h1.top() = " + std::to_string(h1.top()) +
                          "，h2.top() = " + std::to_string(h2.top()));
        h1.merge(h2);   // <-- 关键：O(log n)，不搬元素
        side("Boost", "merge 后 h1.top() = " + std::to_string(h1.top()) +
                          "，h1.size() = " + std::to_string(h1.size()) +
                          "，h2 已被搬空 size=" + std::to_string(h2.size()));
        side("STL  ", "std::priority_queue 连 .begin()/.end() 都没有，merge 只能：");
        side("STL  ", "  ① 把两个堆全部弹出到 vector（O(n log n) 排序）再重建 —— 慢且费内存；");
        side("STL  ", "  ② 或者自己手写可合并堆。");
        line("");
        line("  典型用途：多路归并、Dijkstra/A* 的优先队列、任务调度合并队列、");
        line("            k 个有序流的合并（日志合并、外部排序）。");
    }

    // ===================================================================
    item("3) 修改堆中已有元素的键值（优先级队列的改键）");
    {
        boost::heap::fibonacci_heap<int> fh;
        std::vector<boost::heap::fibonacci_heap<int>::handle_type> handles;
        for (int v : {10, 20, 30, 40}) handles.push_back(fh.push(v));

        side("Boost", "初始 top = " + std::to_string(fh.top()));
        // 把 10 提升成 99 —— 直接改句柄，不用弹出再压入
        fh.update(handles[0], 99);
        side("Boost", "update(handle, 99) 后 top = " + std::to_string(fh.top()));
        side("Boost", "fibonacci_heap 还支持 increase/decrease（Dijkstra 里改距离必需）");
        side("STL  ", "std::priority_queue 没有句柄/迭代器概念，改键只能 push 一个新值");
        side("STL  ", "  然后靠懒删除（弹出时判断是否过期）绕过 —— 内存会膨胀。");
    }

    // ===================================================================
    item("4) flat_map vs std::map：小数据量下差距明显");
    {
        constexpr int M = 2000;   // 小表场景（配置表、枚举映射常见规模）

        std::mt19937 rng(7);
        std::uniform_int_distribution<int> dist(0, 100000);
        std::vector<int> keys(M);
        for (auto& k : keys) k = dist(rng);

        boost::container::flat_map<int, int> fm;
        std::map<int, int>                    sm;
        std::unordered_map<int, int>          um;

        double t_fm = time_ms([&] {
            for (int k : keys) fm[k] = k;
            for (int k : keys) (void)fm.find(k);
        });
        double t_sm = time_ms([&] {
            for (int k : keys) sm[k] = k;
            for (int k : keys) (void)sm.find(k);
        });
        double t_um = time_ms([&] {
            for (int k : keys) um[k] = k;
            for (int k : keys) (void)um.find(k);
        });

        side("Boost", "flat_map         " + std::to_string(M) + " 次插入+查找: " + ms(t_fm));
        side("STL  ", "std::map         " + std::to_string(M) + " 次插入+查找: " + ms(t_sm));
        side("STL  ", "std::unordered_map " + std::to_string(M) + " 次插入+查找: " + ms(t_um));
        char buf[160];
        std::snprintf(buf, sizeof buf, "  ==> flat_map 相对 std::map 快 %.1f 倍", t_sm / t_fm);
        line(buf);
        line("  原理：flat_map 是排序的 vector，查找虽为 O(log n) 但全程连续内存、");
        line("        缓存命中率高，常数因子比红黑树小得多。");
        line("  代价：插入/删除 O(n)（搬元素），且插入会让迭代器全部失效。");
    }

    // ===================================================================
    item("5) small_vector：栈上小缓冲，避免小数组的堆分配");
    {
        boost::container::small_vector<int, 8> sv;   // 前 8 个元素放栈上
        side("Boost", "small_vector<int, 8> sizeof = " + std::to_string(sizeof(sv)) +
                          " 字节（内含 8 个 int 的栈缓冲 = 32 字节）");
        for (int i = 0; i < 6; ++i) sv.push_back(i);
        side("Boost", "存 6 个元素：完全没有堆分配（size=" + std::to_string(sv.size()) + "）");
        for (int i = 6; i < 20; ++i) sv.push_back(i);
        side("Boost", "超过 8 个后自动转堆分配（size=" + std::to_string(sv.size()) +
                          "，capacity=" + std::to_string(sv.capacity()) + "）");
        side("STL  ", "std::vector 无论多小都一定堆分配；小对象高频创建时这是明显开销。");
        line("  用途：函数返回的小集合、每帧构造的临时列表、解析器里的 token 缓冲。");
    }

    // ===================================================================
    line();
    line("小结(Boost 赢在): 可合并堆 / 改键堆 / flat_map / small_vector —— ");
    line("  这些都不是替代 STL，而是 STL 没覆盖到的数据结构空白。");
}
