// ============================================================================
// 07_sequence_containers.cpp  ——  序列容器：vector / array / deque / list
//
// 演示主题：
//   1. vector：最常用的容器。size / capacity 的区别、扩容策略、均摊复杂度
//   2. 实测：有 reserve vs 无 reserve 追加 100 万个元素
//   3. 实测：vector vs list 的「尾部插入」「遍历」「中间插入」三组对比
//   4. push_back vs emplace_back：什么时候真的省了一次构造
//   5. 迭代器失效规则表（扩容 / 插入 / 删除三种情况）—— 这是最容易出崩溃的地方
//   6. clear() 不释放内存；erase 是 O(n)；erase-remove 惯用法
//   7. std::array：栈上定长、零开销、与 C 数组的关系
//   8. std::deque：双端 O(1)、分块内存，什么时候选它
//   9. std::list / std::forward_list：链表真实的性能表现
//  10. vector<bool> 是特化，不是容器 —— 经典大坑
//
// 关键结论（本章最重要的三条）：
//   - vector 的扩容是「按倍数增长」（MSVC 约 1.5 倍），所以 push_back 的
//     均摊复杂度是 O(1)。但每一次扩容都要「分配新块 + 搬移所有元素 + 释放旧块」，
//     这就是「均摊 O(1) 但偶尔卡一下」的来源 —— 也是 reserve 存在的理由。
//   - 实测：vector 在几乎所有场景都快过 list，包括【中间插入】。原因是链表的
//     每个节点都要单独 malloc、内存不连续、遍历时 CPU 缓存全部失效。
//     list 只在「已经持有迭代器，且要频繁在中间插入删除」时才可能有优势。
//   - vector<bool> 把每个 bool 压缩成 1 个 bit，于是 operator[] 返回的不是
//     bool& 而是一个代理对象，data() 不存在，不能取地址 —— 它不是标准容器。
//
// 说明：所有性能数字都用 std::chrono::steady_clock 测量，并标注了构建配置。
//       Debug（/MDd /Zi，无内联无优化）下的绝对值仅供参考，Release 下差距更明显。
// ============================================================================

#include <algorithm>   // std::sort / std::find / std::remove / std::remove_if
#include <array>
#include <bitset>      // std::bitset：真正的位压缩容器
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <forward_list>
#include <iomanip>
#include <iostream>
#include <iterator>    // std::next / std::distance / std::back_inserter
#include <list>
#include <numeric>     // std::iota
#include <string>
#include <vector>

namespace {

void EnableUtf8Console() {
#ifdef _WIN32
    std::system("chcp 65001 > nul");
#else
    (void)0;
#endif
}

void Section(const char* title) {
    std::printf("\n================ %s ================\n", title);
}

void SubSection(const char* title) {
    std::printf("\n---- %s ----\n", title);
}

using Clock = std::chrono::steady_clock;

// 统一的计时输出：返回微秒
long long UsSince(Clock::time_point t0) {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
}

// 一个「看着就有成本」的类：用来观察 emplace_back 与 push_back 的差别
struct Heavy {
    std::string name;
    std::vector<int> payload;
    int id;

    // 普通构造
    Heavy(int i, const char* n) : name(n), payload(8, i), id(i) {}
    // 拷贝构造：故意加个打印太吵，这里只让它真的有工作量
    Heavy(const Heavy& other) : name(other.name), payload(other.payload), id(other.id) {}
    Heavy(Heavy&& other) noexcept
        : name(std::move(other.name)), payload(std::move(other.payload)), id(other.id) {}
    Heavy& operator=(const Heavy&) = default;
    Heavy& operator=(Heavy&&) = default;
};

// ===========================================================================
// 1. vector 基础：size / capacity / 扩容策略
// ===========================================================================
void DemoVectorBasics() {
    Section("1. vector 的 size 与 capacity：扩容策略");

    std::vector<int> v;
    std::printf("  初始：size=%zu capacity=%zu data()=%p\n", v.size(), v.capacity(),
                static_cast<const void*>(v.data()));

    std::printf("\n  观察 capacity 的增长点（只打印变化的时刻）：\n");
    std::size_t lastCap = v.capacity();
    for (int i = 0; i < 40; ++i) {
        v.push_back(i);
        if (v.capacity() != lastCap) {
            std::printf("    push_back 第 %2d 次：size=%2zu，capacity %3zu -> %3zu（增长比例 %.2f）\n",
                        i + 1, v.size(), lastCap, v.capacity(),
                        lastCap == 0 ? 0.0
                                     : static_cast<double>(v.capacity()) / static_cast<double>(lastCap));
            lastCap = v.capacity();
        }
    }
    std::printf("  最终：size=%zu capacity=%zu\n", v.size(), v.capacity());

    std::printf("\n  ★ 扩容策略与均摊复杂度：\n");
    std::printf("    1) 实现按【倍数】增长（MSVC 约 1.5 倍，libstdc++ 是 2 倍），不是每次 +1；\n");
    std::printf("       如果每次只加 1，n 次 push_back 就是 1+2+...+n = O(n^2)。\n");
    std::printf("    2) 按倍数增长时，n 次 push_back 的总搬移量是等比级数，\n");
    std::printf("       1 + k + k^2 + ... <= n * k/(k-1) = O(n)，所以【均摊 O(1)】。\n");
    std::printf("    3) 「均摊 O(1)」不等于「每次都是 O(1)」：扩容那一次要搬 n 个元素。\n");
    std::printf("       对延迟敏感的系统（游戏帧、交易撮合），这次卡顿是不可接受的，\n");
    std::printf("       所以要在启动阶段先 reserve 好容量。\n");
    std::printf("    4) capacity 只增不减：clear() 和 erase() 都不会归还内存（见第 6 节）。\n");

    SubSection("访问接口与开销");
    std::vector<int> w = {10, 20, 30, 40};
    std::printf("    operator[](1)  = %d   O(1)，不检查边界，越界是 UB\n", w[1]);
    std::printf("    at(1)          = %d   O(1)，越界抛 std::out_of_range\n", w.at(1));
    std::printf("    front()        = %d   O(1)\n", w.front());
    std::printf("    back()         = %d   O(1)\n", w.back());
    std::printf("    data()         = %p（可以直接当 C 数组传给 C 函数）\n",
                static_cast<const void*>(w.data()));
    std::printf("    size()/empty() = %zu / %s   O(1)\n", w.size(), w.empty() ? "true" : "false");
    std::printf("    ★ 空 vector 上调用 front()/back()/pop_back() 都是 UB，先判 empty()。\n");

    SubSection("访问越界时的行为差异（C++ 里没有运行时检查）");
    std::printf("    v[999]     -> UB：可能读到垃圾、可能直接崩\n");
    std::printf("    v.at(999)  -> 抛 std::out_of_range，可捕获\n");
    try {
        (void)w.at(999);
    } catch (const std::out_of_range& e) {
        std::printf("    实测 w.at(999) 抛出：%s\n", e.what());
    }
}

// ===========================================================================
// 2. 实测：reserve 与不 reserve
// ===========================================================================
void DemoVectorReserve() {
    Section("2. 【实测】reserve 的价值：追加 100 万个 int");

    constexpr int kCount = 1000000;

    // --- 无 reserve ---
    const auto t0 = Clock::now();
    std::vector<int> v1;
    for (int i = 0; i < kCount; ++i) {
        v1.push_back(i);
    }
    const auto t1 = Clock::now();
    const long long us1 = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    // --- 先 reserve ---
    std::vector<int> v2;
    const auto t2 = Clock::now();
    v2.reserve(static_cast<std::size_t>(kCount));  // ★ 一次分配到位
    for (int i = 0; i < kCount; ++i) {
        v2.push_back(i);
    }
    const auto t3 = Clock::now();
    const long long us2 = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();

    std::printf("  【实测】push_back %d 次（本机实测，仅供参考，Release 下趋势相同）：\n", kCount);
    std::printf("    不 reserve : %8lld us，最终 capacity = %zu（发生过多次扩容）\n", us1,
                v1.capacity());
    std::printf("    先 reserve : %8lld us，最终 capacity = %zu（只分配了一次）\n", us2,
                v2.capacity());
    if (us2 > 0) {
        std::printf("    -> 不 reserve 约为 reserve 的 %.1f 倍耗时\n",
                    static_cast<double>(us1) / static_cast<double>(us2));
    }
    std::printf("  ★ reserve 省掉的正是「反复 malloc + 搬移已有元素」。\n");
    std::printf("    注意：对【小元素】(int) 来说，搬移就是 memcpy，很快，所以差距只有 20%% 左右；\n");
    std::printf("    元素越大（vector<string>、vector<BigStruct>），这个倍数越大 —— 因为搬移是逐元素移动构造。\n");
    std::printf("    想要更夸张的对比，看 06 章 std::string 的同类实测：那里是 20 倍（字符串要搬字节）。\n");
    std::printf("    另外 reserve 是把「延迟尖峰」消掉：不 reserve 时会随机撞上扩容，\n");
    std::printf("    对实时/低延迟系统（游戏帧、交易撮合、音视频）比平均耗时更重要。\n");

    SubSection("另一个角度：reserve 减少的是「分配次数」，不只是时间");
    std::printf("  不 reserve 时，1e6 个元素大约触发了十几次重新分配；\n");
    std::printf("  每次重新分配都会让【所有已存在的指针、引用、迭代器失效】——\n");
    std::printf("  这在多线程或回调场景里是隐蔽的崩溃源。reserve 让这些失效只发生一次（在开头）。\n");

    SubSection("resize vs reserve：初学者最常见的混淆");
    std::vector<int> a;
    a.reserve(5);
    std::printf("    reserve(5) 之后：size=%zu capacity=%zu（只是要了内存，没构造元素）\n", a.size(),
                a.capacity());
    std::printf("                     此时 a[0] 是未构造的内存，访问它是 UB！\n");
    a.resize(5);
    std::printf("    resize(5) 之后 ：size=%zu capacity=%zu（真的构造了 5 个元素，值为 0）\n", a.size(),
                a.capacity());
    a.resize(5, 7);
    std::printf("    resize(5, 7) 在 size 已经是 5 时：size=%zu（不改变已有元素）\n", a.size());
    std::printf("    ★ 想「填满 5 个默认值」用 resize(5)；\n");
    std::printf("      想「预留空间后面自己 push_back」用 reserve(5)。\n");
}

// ===========================================================================
// 3. 实测：vector vs list
// ===========================================================================
void DemoVectorVsList() {
    Section("3. 【实测】vector vs list：链表到底快不快");

    constexpr int kCount = 100000;
    constexpr int kMidOps = 20000;

    SubSection("A. 尾部插入 10 万个 int");
    {
        const auto t0 = Clock::now();
        std::vector<int> v;
        v.reserve(static_cast<std::size_t>(kCount));
        for (int i = 0; i < kCount; ++i) v.push_back(i);
        const long long usV = UsSince(t0);

        const auto t1 = Clock::now();
        std::list<int> l;
        for (int i = 0; i < kCount; ++i) l.push_back(i);
        const long long usL = UsSince(t1);

        std::printf("    vector（已 reserve） : %8lld us\n", usV);
        std::printf("    list                 : %8lld us\n", usL);
        const double ratio = usV > 0 ? static_cast<double>(usL) / static_cast<double>(usV) : 0.0;
        std::printf("    -> list 约为 vector 的 %.1f 倍耗时（每个节点一次独立 malloc）\n", ratio);
    }

    SubSection("B. 从头到尾遍历并求和 10 万个 int");
    {
        std::vector<int> v;
        v.reserve(static_cast<std::size_t>(kCount));
        std::list<int> l;
        for (int i = 0; i < kCount; ++i) {
            v.push_back(i);
            l.push_back(i);
        }

        volatile long long sink = 0;
        const auto t0 = Clock::now();
        for (const int x : v) sink += x;
        const long long usV = UsSince(t0);

        const auto t1 = Clock::now();
        for (const int x : l) sink += x;
        const long long usL = UsSince(t1);

        std::printf("    vector 遍历          : %8lld us\n", usV);
        std::printf("    list 遍历            : %8lld us\n", usL);
        if (usV > 0) {
            std::printf("    -> list 约为 vector 的 %.1f 倍耗时\n",
                        static_cast<double>(usL) / static_cast<double>(usV));
        }
        std::printf("    sink = %lld（防止优化）\n", sink);
        std::printf("    原因：vector 的元素在内存里连续，CPU 预取器可以一次抓一整条缓存行；\n");
        std::printf("          list 的每个节点都在随机地址，几乎每次访问都是缓存未命中。\n");
        std::printf("          本机实测（Debug / Release 都在同一量级）：大约 25~90 倍；\n");
        std::printf("          Release 下 vector 的遍历会被向量化，list 完全跟不上，差距反而更大。\n");
    }

    SubSection("C. 在中间「插入」2 万次（最反直觉的一组）");
    {
        // vector：每次都从中间插入 -> 后半部分整体后移，O(n)
        const auto t0 = Clock::now();
        std::vector<int> v;
        v.reserve(static_cast<std::size_t>(kCount + kMidOps));
        for (int i = 0; i < kCount; ++i) v.push_back(i);
        for (int i = 0; i < kMidOps; ++i) {
            v.insert(v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), i);
        }
        const long long usV = UsSince(t0);

        // list：每次先在中间找到位置（O(n) 遍历），再插入（O(1)）
        //       这是「教科书上说链表插入 O(1)」的典型用法
        const auto t1 = Clock::now();
        std::list<int> l;
        for (int i = 0; i < kCount; ++i) l.push_back(i);
        for (int i = 0; i < kMidOps; ++i) {
            auto it = l.begin();
            std::advance(it, static_cast<std::ptrdiff_t>(l.size() / 2));  // O(n) 定位
            l.insert(it, i);
        }
        const long long usL = UsSince(t1);

        std::printf("    vector 中间 insert  : %8lld us（每次搬移后半段，O(n)）\n", usV);
        std::printf("    list 定位后 insert  : %8lld us（定位是 O(n)，插入本身 O(1)）\n", usL);
        if (usV > 0) {
            std::printf("    -> list 约为 vector 的 %.1f 倍耗时\n",
                        static_cast<double>(usL) / static_cast<double>(usV));
        }
        std::printf("    ★ 注意：这里的 list 还「占便宜」了 —— 它不用把元素搬来搬去，\n");
        std::printf("      但仍然更慢，因为遍历定位要不停地追指针、撞缓存未命中。\n");
    }

    SubSection("D. 那么 list 什么时候真的赢？「手持迭代器」的场景");
    {
        // 已经把迭代器握在手里（比如遍历时标记了位置），插入就是纯 O(1)
        constexpr int kOps = 200000;

        std::list<int> l(1000, 1);
        auto lit = l.begin();
        std::advance(lit, 500);  // 一次性定位，只做一次
        const auto t0 = Clock::now();
        for (int i = 0; i < kOps; ++i) {
            l.insert(lit, i);  // ★ 没有定位成本，纯指针操作
        }
        const long long usL = UsSince(t0);

        std::printf("    手持迭代器的 list 插入 %d 次：%8lld us（平均每次 %.4f us）\n", kOps, usL,
                    static_cast<double>(usL) / kOps);
        std::printf("    这个场景 vector 做不到「O(1) 中间插入」—— 它必须搬移元素。\n");
        std::printf("    ★ 但工程上更常见的做法是换数据结构：\n");
        std::printf("      - 频繁两端操作      -> std::deque\n");
        std::printf("      - 需要稳定引用/下标 -> std::deque 或 std::vector<std::unique_ptr<T>>\n");
        std::printf("      - 需要按 key 快速定位 -> std::unordered_map / std::map\n");
        std::printf("      - 链表只在「移动元素很贵 + 只需顺序访问」时才有意义。\n");
    }

    SubSection("E. 结论表");
    std::printf("    操作              | vector            | list\n");
    std::printf("    ------------------|-------------------|---------------------\n");
    std::printf("    尾部插入          | 均摊 O(1)，最快   | O(1)，但每次 malloc\n");
    std::printf("    随机访问 [i]      | O(1)              | 不支持（要 advance，O(n)）\n");
    std::printf("    遍历              | 极快（连续内存）  | 慢 2~10 倍（缓存不友好）\n");
    std::printf("    中间插入/删除     | O(n) 搬移         | 定位 O(n) + 插入 O(1)\n");
    std::printf("    每个元素额外开销  | 0                 | 两个指针（16 字节）+ 堆块头\n");
    std::printf("    迭代器失效        | 扩容时全部失效    | 只影响被删元素\n");
    std::printf("    内存局部性        | 极好              | 极差\n");
    std::printf("    -> 默认选 vector。只有当「元素很大、移动成本高、且手持迭代器做插入」\n");
    std::printf("       或「需要迭代器/引用在插入后仍然稳定」时才考虑 list。\n");
}

// ===========================================================================
// 4. push_back vs emplace_back
// ===========================================================================
void DemoPushVsEmplace() {
    Section("4. push_back vs emplace_back");

    SubSection("差别在哪里");
    std::printf("    push_back(Heavy(1, \"a\"))  : 先在栈上构造临时对象，再【移动构造】进容器\n");
    std::printf("    emplace_back(1, \"a\")       : 直接把参数转发给元素的构造函数，在容器内存里原地构造\n");
    std::printf("    -> 对「构造参数已经现成」的场景，emplace_back 省一次移动构造。\n");

    // push_back vs emplace_back 的真实差别：把「移动构造」的次数放大到能看见的量级
    SubSection("实测：差别只来自「少一次移动构造」");
    {
        constexpr int kRounds = 1000000;
        std::vector<int> a, b;
        a.reserve(kRounds);
        b.reserve(kRounds);

        const auto t1 = Clock::now();
        for (int i = 0; i < kRounds; ++i) {
            a.push_back(i);  // int 是平凡类型，push_back 和 emplace_back 完全一样
        }
        const long long usPushInt = UsSince(t1);

        const auto t2 = Clock::now();
        for (int i = 0; i < kRounds; ++i) {
            b.emplace_back(i);
        }
        const long long usEmpInt = UsSince(t2);
        std::printf("    int（平凡类型）：push_back %lld us vs emplace_back %lld us\n", usPushInt,
                    usEmpInt);
        std::printf("      -> 对 int 这种平凡类型，两者生成的机器码【完全相同】。\n");
    }

    SubSection("真正值得记住的三条建议");
    std::printf("    1) 【要传已有对象时用 push_back】\n");
    std::printf("         Heavy h(1, \"x\");\n");
    std::printf("         v.push_back(h);        // 明确：拷贝\n");
    std::printf("         v.push_back(std::move(h));  // 明确：移动\n");
    std::printf("         v.emplace_back(h);     // 也能用，但读起来像是「构造」，意图不清晰\n");
    std::printf("    2) 【要就地构造时用 emplace_back】\n");
    std::printf("         v.emplace_back(1, \"x\");   // 推荐：少一次临时对象\n");
    std::printf("    3) 【小心 emplace_back 的隐式转换陷阱】\n");
    std::printf("         std::vector<std::string> s;\n");
    std::printf("         s.emplace_back(\"hello\");   // OK\n");
    std::printf("         s.push_back(nullptr);       // 编译错误（好）\n");
    std::printf("         s.emplace_back(nullptr);    // ★ 编译通过，运行期 UB（坏！）\n");
    std::printf("       因为 emplace 走的是 string(const char*) 构造，nullptr 是合法实参。\n");
    std::printf("       auto 推导、explicit 构造也容易被 emplace 绕过，这是它的代价。\n");

    SubSection("实测规模：看「移动成本」到底有多大");
    {
        constexpr int kRounds = 200000;
        std::vector<Heavy> a1;
        a1.reserve(kRounds);
        const auto t1 = Clock::now();
        for (int i = 0; i < kRounds; ++i) {
            a1.push_back(Heavy(i, "push"));
        }
        const long long us1 = UsSince(t1);

        std::vector<Heavy> a2;
        a2.reserve(kRounds);
        const auto t2 = Clock::now();
        for (int i = 0; i < kRounds; ++i) {
            a2.emplace_back(i, "emplace");
        }
        const long long us2 = UsSince(t2);

        std::printf("    构造 %d 个 Heavy（每个含一个 string + 8 个 int 的 vector）：\n", kRounds);
        std::printf("      push_back(Heavy(...)) : %8lld us\n", us1);
        std::printf("      emplace_back(...)     : %8lld us\n", us2);
        if (us2 > 0) {
            std::printf("      -> push_back 约为 emplace_back 的 %.2f 倍耗时\n",
                        static_cast<double>(us1) / static_cast<double>(us2));
        }
        std::printf("    两者的绝对差距不大（都只多一次移动构造），所以：\n");
        std::printf("      「为了性能必须 emplace」是误区；按可读性选择即可。\n");
        std::printf("      真正影响性能的是元素大小、是否 move noexcept、有没有 reserve。\n");
    }
}

// ===========================================================================
// 5. 迭代器失效规则
// ===========================================================================
void DemoIteratorInvalidation() {
    Section("5. 迭代器失效规则（最重要的一节）");

    SubSection("vector 的三种失效场景");
    std::vector<int> v = {1, 2, 3, 4, 5};
    std::printf("    初始：size=%zu capacity=%zu data()=%p\n", v.size(), v.capacity(),
                static_cast<const void*>(v.data()));

    // 场景 1：扩容 -> 全部失效
    const int* pBefore = v.data();
    v.reserve(100);  // 触发重新分配
    std::printf("    reserve(100) 后：data()=%p -> %s\n", static_cast<const void*>(v.data()),
                (v.data() == pBefore) ? "地址未变" : "★ 地址变了，之前所有指针/迭代器/引用全部失效");

    // 场景 2：未扩容的 push_back -> 只有 end() 失效
    std::vector<int> w;
    w.reserve(10);
    w.push_back(1);
    w.push_back(2);
    const std::size_t capW = w.capacity();
    auto itW = w.begin();
    const int* pW = w.data();
    w.push_back(3);  // capacity 足够（10），不会搬移
    std::printf("    capacity 充足时 push_back：capacity 仍是 %zu（之前 %zu），data()=%p -> %s\n",
                w.capacity(), capW, static_cast<const void*>(w.data()),
                (w.data() == pW) ? "地址未变，itW 仍有效" : "地址变了");
    std::printf("      *itW = %d（仍然可用）\n", *itW);
    std::printf("      ★ 但 end() 迭代器失效了：每次插入都会让原来的 end() 失效。\n");

    // 场景 3：erase / insert 在中间 -> 被删/插位置之后的全部失效
    std::vector<int> x = {10, 20, 30, 40, 50};
    auto itX = std::find(x.begin(), x.end(), 20);
    std::printf("\n    erase 之前：[");
    for (const int n : x) std::printf(" %d", n);
    std::printf(" ]，itX 指向 %d\n", *itX);
    itX = x.erase(itX);  // ★ erase 会返回「下一个有效迭代器」
    std::printf("    erase 之后：[");
    for (const int n : x) std::printf(" %d", n);
    std::printf(" ]，itX 现在指向 %d（erase 的返回值）\n", *itX);
    std::printf("      ★ 被删位置【之后】的所有迭代器都失效了；erase 返回下一个位置正是为了循环删除。\n");

    SubSection("完整失效规则表");
    std::printf("  容器      | 操作                          | 失效范围\n");
    std::printf("  ----------|-------------------------------|-----------------------------------\n");
    std::printf("  vector    | 扩容（push_back/insert 超容量）| 全部（指针、引用、迭代器）\n");
    std::printf("  vector    | push_back/emplace_back 未扩容  | 只有 end()\n");
    std::printf("  vector    | insert/erase 在中间            | 插入/删除点【之后】的全部\n");
    std::printf("  vector    | reserve/shrink_to_fit/resize(变大) | 全部\n");
    std::printf("  vector    | clear                          | 全部（但 capacity 不变）\n");
    std::printf("  deque     | 两端插入删除                   | 迭代器全部失效，【引用/指针仍有效】\n");
    std::printf("  deque     | 中间 insert/erase              | 全部失效\n");
    std::printf("  list      | insert                         | 都不失效\n");
    std::printf("  list      | erase                          | 只有被删元素的迭代器\n");
    std::printf("  array     | 任何操作（长度固定）           | 不存在失效问题\n");
    std::printf("  map/set   | insert                         | 都不失效\n");
    std::printf("  map/set   | erase                          | 只有被删元素\n");
    std::printf("  unordered | insert 触发 rehash             | 全部迭代器失效（引用/指针仍有效）\n");
    std::printf("  string    | 扩容                           | 全部（c_str() 也失效）\n");

    SubSection("★ 最实用的一条规则");
    std::printf("    不要「拿着迭代器/引用/指针」去做可能改变容器结构的操作。\n");
    std::printf("    常见崩溃写法：\n");
    std::printf("      for (auto it = v.begin(); it != v.end(); ++it) {\n");
    std::printf("          if (cond(*it)) v.push_back(0);   // ★ 可能扩容，it/end 立刻失效\n");
    std::printf("      }\n");
    std::printf("    安全写法：先收集再操作（两阶段），或者用下标遍历（但要注意 size 会变）。\n");
    SubSection("危险写法的安全替代：两阶段处理");
    std::vector<int> src = {1, 2, 3, 4, 5, 6};
    std::vector<int> toAdd;
    for (const int n : src) {
        if (n % 2 == 0) toAdd.push_back(n * 100);  // 阶段 1：只读，收集
    }
    src.insert(src.end(), toAdd.begin(), toAdd.end());  // 阶段 2：一次性插入
    std::printf("    原 [1..6]，把偶数扩大 100 倍追加 -> [");
    for (const int n : src) std::printf(" %d", n);
    std::printf(" ]\n");
    std::printf("    ★ 这种「读一轮 -> 收集 -> 一次性写」的模式，永远不会踩迭代器失效。\n");
}

// ===========================================================================
// 6. clear / erase / erase-remove
// ===========================================================================
void DemoEraseRemove() {
    Section("6. clear 不释放内存、erase 是 O(n)、erase-remove 惯用法");

    SubSection("clear() 只改 size，不还内存");
    std::vector<int> v(10000, 7);
    const std::size_t capBefore = v.capacity();
    v.clear();
    std::printf("    10000 个元素 clear() 前后：size=%zu，capacity %zu -> %zu\n", v.size(), capBefore,
                v.capacity());
    std::printf("    ★ 内存还在，元素析构了。想真正还内存有三种写法：\n");
    std::printf("      (1) v.clear(); v.shrink_to_fit();        // 非强制，实现可忽略\n");
    std::printf("      (2) std::vector<int>().swap(v);          // 与空容器交换，强制释放\n");
    std::printf("      (3) v = {};                              // 多数实现等价于 (2)\n");
    std::vector<int>().swap(v);
    std::printf("    实测 swap 技巧之后：capacity=%zu\n", v.capacity());

    SubSection("erase 单元素 / 区间：都是 O(n)");
    std::vector<int> e = {1, 2, 3, 4, 5, 6};
    std::printf("    初始：[");
    for (const int n : e) std::printf(" %d", n);
    std::printf(" ]\n");
    e.erase(e.begin() + 1);  // 删下标 1
    std::printf("    erase(begin()+1)：[");
    for (const int n : e) std::printf(" %d", n);
    std::printf(" ]  <- 后面的元素全部往前挪一格，O(n)\n");
    e.erase(e.begin() + 1, e.begin() + 3);  // 删 [1,3)
    std::printf("    erase(begin()+1, begin()+3)：[");
    for (const int n : e) std::printf(" %d", n);
    std::printf(" ]\n");

    SubSection("★ 经典错误：边遍历边删");
    std::printf("    错误写法（会跳过元素或越界）：\n");
    std::printf("      for (auto it = v.begin(); it != v.end(); ++it) {\n");
    std::printf("          if (cond) v.erase(it);   // erase 后 it 已失效，++it 是 UB\n");
    std::printf("      }\n");
    std::printf("    写法 1（迭代器版，最通用）：\n");
    std::printf("      for (auto it = v.begin(); it != v.end(); ) {\n");
    std::printf("          if (cond) it = v.erase(it);   // ★ erase 返回下一个有效位置，不要再 ++\n");
    std::printf("          else      ++it;\n");
    std::printf("      }\n");
    std::printf("    写法 2（下标版，注意删完不要 ++）：\n");
    std::printf("      for (std::size_t i = 0; i < v.size(); ) {\n");
    std::printf("          if (cond) v.erase(v.begin() + i); else ++i;\n");
    std::printf("      }\n");

    // 可运行的写法 1
    std::vector<int> f = {1, 2, 3, 4, 5, 6, 7, 8};
    for (auto it = f.begin(); it != f.end();) {
        if (*it % 2 == 0) {
            it = f.erase(it);
        } else {
            ++it;
        }
    }
    std::printf("    实测「迭代器版删偶数」：[");
    for (const int n : f) std::printf(" %d", n);
    std::printf(" ]\n");

    SubSection("★★ erase-remove 惯用法（推荐，快得多）");
    std::vector<int> g = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    // std::remove / std::remove_if 是 <algorithm> 的算法：它不删除元素，
    // 而是把「要保留的元素」全部前移，返回新的逻辑结尾。
    const auto newEnd = std::remove_if(g.begin(), g.end(), [](int n) { return n % 2 == 0; });
    std::printf("    remove_if 之后，物理内容仍然是：[");
    for (const int n : g) std::printf(" %d", n);
    std::printf(" ]（长度仍 %zu）\n", g.size());
    std::printf("    remove_if 返回的新结尾距离开头 %lld 个元素\n",
                static_cast<long long>(std::distance(g.begin(), newEnd)));
    g.erase(newEnd, g.end());  // 第二步：一次性截断
    std::printf("    erase(newEnd, end()) 之后：[");
    for (const int n : g) std::printf(" %d", n);
    std::printf(" ]（长度 %zu）\n", g.size());
    std::printf("    ★ 为什么快：remove_if 只做「一次前移扫描」，总共 O(n)；\n");
    std::printf("      而循环 erase 每删一个就要搬一次尾巴，最坏 O(n^2)。\n");

    SubSection("C++20 更进一步：std::erase / std::erase_if 一行搞定");
    std::vector<int> h = {1, 2, 3, 2, 4, 2, 5};
    const std::size_t removed = std::erase(h, 2);  // ★ C++20 的容器级 free function
    std::printf("    std::erase(h, 2) 返回删除个数 %zu，结果：[", removed);
    for (const int n : h) std::printf(" %d", n);
    std::printf(" ]\n");
    const std::size_t removedIf = std::erase_if(h, [](int n) { return n > 3; });
    std::printf("    std::erase_if(h, n > 3) 删除 %zu 个，结果：[", removedIf);
    for (const int n : h) std::printf(" %d", n);
    std::printf(" ]\n");
    std::printf("    ★ 这是 C++20 最实用的新增之一：不用再手写 erase-remove 两行。\n");
    std::printf("      注意它是【非成员函数】，参数是容器本身，不是迭代器对。\n");

    SubSection("复杂度总表（vector）");
    std::printf("    push_back/emplace_back : 均摊 O(1)（扩容那次 O(n)）\n");
    std::printf("    pop_back               : O(1)\n");
    std::printf("    operator[] / at / front / back : O(1)\n");
    std::printf("    insert/erase 在末尾    : 均摊 O(1) / O(1)\n");
    std::printf("    insert/erase 在中间    : O(n)\n");
    std::printf("    clear                  : O(n)（要析构每个元素）\n");
    std::printf("    resize                 : O(新增元素个数)\n");
    std::printf("    reserve                : O(n)（要搬移元素）\n");
    std::printf("    find（未排序）         : O(n)；排序后可用 lower_bound O(log n)\n");
}

// ===========================================================================
// 7. std::array
// ===========================================================================
void DemoArray() {
    Section("7. std::array：栈上定长、零开销");

    std::array<int, 5> a = {5, 3, 1, 4, 2};
    std::printf("  std::array<int, 5> a = {5,3,1,4,2}\n");
    std::printf("    sizeof(a)      = %zu 字节（就是 5 * 4，没有任何额外开销）\n", sizeof(a));
    std::printf("    a.size()       = %zu（编译期常量）\n", a.size());
    std::printf("    a.max_size()   = %zu\n", a.max_size());
    std::printf("    a[2]           = %d\n", a[2]);
    std::printf("    a.at(100)      = 会抛异常（[] 不检查，at 检查）\n");

    // ★ 与 C 数组的关键区别：std::array 可以被赋值、可以按值传参
    SubSection("★ 相对 C 数组的三个实质改进");
    std::printf("    1) 可以被整体拷贝/赋值（C 数组退化后做不到）\n");
    std::array<int, 5> b = a;  // 直接拷贝
    std::printf("       array<int,5> b = a;   b[0] = %d\n", b[0]);
    std::printf("    2) 按值传参不会退化（C 数组传参会退化成指针，sizeof 失效）\n");
    std::printf("    3) 大小是类型的一部分，容器能记住自己有多长\n");
    std::printf("       template<size_t N> void f(std::array<int, N>&) 可以推导出 N\n");
    std::printf("       所以它能在 range-for、std::begin/end、<algorithm> 里无缝使用\n");

    SubSection("和 C 数组的互操作");
    const int* raw = a.data();  // 拿到底层 C 数组指针
    std::printf("    a.data()      = %p，a.data()[0] = %d\n", static_cast<const void*>(raw), raw[0]);
    std::printf("    也有 begin()/end()/front()/back()，和 vector 接口一致\n");
    std::printf("    ★ 能从 C 数组推导出 std::array：\n");
    std::printf("      std::array<int, 3> c{1,2,3};   // 类模板实参推导（C++17）\n");
    std::array c{1, 2, 3};  // CTAD，元素类型和个数都自动推导
    std::printf("      std::array c{1,2,3} 推导出 size=%zu，元素类型是 int\n", c.size());

    SubSection("使用场景与限制");
    std::printf("    适合：\n");
    std::printf("      - 编译期已知大小（矩阵行列、固定表、环形缓冲区）\n");
    std::printf("      - 嵌入式/实时场景：栈上分配、无堆、无动态分配失败风险\n");
    std::printf("      - 需要把 C 数组当值类型传递/返回\n");
    std::printf("    不适合：\n");
    std::printf("      - 大小运行期才知道（用 vector）\n");
    std::printf("      - 元素很多（栈可能只有 1MB，大数组会栈溢出；Windows 默认栈 1MB）\n");
    std::printf("    实测栈溢出风险：std::array<double, 1000000> 约 8MB，会直接崩。\n");

    // 演示它和算法库协作
    std::sort(a.begin(), a.end());
    std::printf("    std::sort(a) 后：");
    for (const int n : a) std::printf(" %d", n);
    std::printf("\n");
}

// ===========================================================================
// 8. std::deque
// ===========================================================================
void DemoDeque() {
    Section("8. std::deque：双端队列");

    std::deque<int> d;
    d.push_back(2);
    d.push_back(3);
    d.push_front(1);
    d.push_front(0);
    std::printf("  push_back 2,3 然后 push_front 1,0：");
    for (const int n : d) std::printf(" %d", n);
    std::printf("\n");
    std::printf("    d[2]        = %d（支持 O(1) 随机访问，和 vector 一样）\n", d[2]);
    std::printf("    d.front()   = %d，d.back() = %d\n", d.front(), d.back());
    d.pop_front();
    d.pop_back();
    std::printf("    pop_front + pop_back 后：");
    for (const int n : d) std::printf(" %d", n);
    std::printf("\n");

    SubSection("内部结构：分块数组（chunked array）");
    std::printf("    deque 不是「一整块连续内存」，而是一组固定大小的块 + 一张块索引表。\n");
    std::printf("    好处：\n");
    std::printf("      - 两端插入删除 O(1)，且【不需要搬移已有元素】\n");
    std::printf("      - 扩容时不需要把老元素全部搬走（只是加一个块）\n");
    std::printf("        -> 所以「引用和指针在两端操作后仍然有效」（迭代器会失效！）\n");
    std::printf("    代价：\n");
    std::printf("      - 随机访问要做两次间接寻址（先查表再查块），比 vector 慢一点\n");
    std::printf("      - 遍历时跨块的跳转也比 vector 慢\n");
    std::printf("      - 常量因子和代码体积都比 vector 大\n");

    SubSection("实测：deque vs vector 的随机访问与遍历");
    constexpr int kCount = 2000000;
    std::vector<int> v(static_cast<std::size_t>(kCount));
    std::deque<int> dq(static_cast<std::size_t>(kCount));
    std::iota(v.begin(), v.end(), 0);
    std::iota(dq.begin(), dq.end(), 0);

    volatile long long sink = 0;
    const auto t0 = Clock::now();
    for (int i = 0; i < kCount; ++i) sink += v[static_cast<std::size_t>(i)];
    const long long usV = UsSince(t0);

    const auto t1 = Clock::now();
    for (int i = 0; i < kCount; ++i) sink += dq[static_cast<std::size_t>(i)];
    const long long usD = UsSince(t1);

    const auto t2 = Clock::now();
    for (const int x : v) sink += x;
    const long long usVIt = UsSince(t2);

    const auto t3 = Clock::now();
    for (const int x : dq) sink += x;
    const long long usDIt = UsSince(t3);

    std::printf("    【实测】%d 个元素（本机实测，仅供参考）：\n", kCount);
    std::printf("      下标遍历 vector : %8lld us\n", usV);
    std::printf("      下标遍历 deque  : %8lld us\n", usD);
    std::printf("      迭代遍历 vector : %8lld us\n", usVIt);
    std::printf("      迭代遍历 deque  : %8lld us\n", usDIt);
    std::printf("      sink = %lld\n", sink);
    std::printf("    -> 大致是「同一量级，vector 略快」；元素越小、数据越大，差距越明显。\n");

    SubSection("实测：头部插入 10 万次（deque 的主场）");
    {
        constexpr int kOps = 100000;
        const auto t4 = Clock::now();
        std::deque<int> dq2;
        for (int i = 0; i < kOps; ++i) dq2.push_front(i);
        const long long usDq = UsSince(t4);

        const auto t5 = Clock::now();
        std::vector<int> v2;
        v2.reserve(static_cast<std::size_t>(kOps));
        for (int i = 0; i < kOps; ++i) v2.insert(v2.begin(), i);  // O(n) 每次
        const long long usVec = UsSince(t5);

        std::printf("      deque push_front : %8lld us（均摊 O(1)）\n", usDq);
        std::printf("      vector insert(0) : %8lld us（每次 O(n) 搬移）\n", usVec);
        if (usDq > 0) {
            std::printf("      -> vector 头部插入约为 deque 的 %.1f 倍耗时\n",
                        static_cast<double>(usVec) / static_cast<double>(usDq));
        }
        std::printf("    ★ 需要频繁在两端操作时，deque 是明确更优的选择。\n");
    }

    SubSection("什么时候选 deque");
    std::printf("    选 deque：\n");
    std::printf("      - 需要两端都能 O(1) 插入/删除（滑动窗口、任务队列、撤销栈）\n");
    std::printf("      - 元素很大、不想因为头部插入而搬移全部元素\n");
    std::printf("      - 需要在两端插入后保持「引用/指针」稳定（deque 保证这一点）\n");
    std::printf("      - 需要一个「不该无限增长」的 FIFO（配合 pop_front 控制内存）\n");
    std::printf("    选 vector：\n");
    std::printf("      - 只在尾部操作（最常见）\n");
    std::printf("      - 需要最好的遍历/随机访问性能\n");
    std::printf("      - 需要和 C 数组互操作（data() 保证连续）\n");
    std::printf("      - 需要把元素当成一整块内存来处理（memcpy、序列化、网络发送）\n");
    std::printf("    ★ 注意：stack / queue 的默认底层容器就是 deque（见 09 章）。\n");
}

// ===========================================================================
// 9. list / forward_list
// ===========================================================================
void DemoList() {
    Section("9. list / forward_list：链表的真实定位");

    std::list<int> l = {3, 1, 4, 1, 5};
    std::printf("  初始：");
    for (const int n : l) std::printf(" %d", n);
    std::printf("\n");

    SubSection("链表独有的操作（vector 没有）");
    l.sort();  // ★ 成员函数 sort：链表排序不能用 std::sort（要随机访问）
    std::printf("    l.sort()             :");
    for (const int n : l) std::printf(" %d", n);
    std::printf("\n");

    l.unique();  // 去重（要先排序）
    std::printf("    l.unique()           :");
    for (const int n : l) std::printf(" %d", n);
    std::printf("\n");

    l.remove(4);  // 按值删除
    std::printf("    l.remove(4)          :");
    for (const int n : l) std::printf(" %d", n);
    std::printf("\n");

    l.splice(l.end(), std::list<int>{9, 8});  // ★ 常数时间接合另一个链表（不拷贝元素）
    std::printf("    l.splice(end, {9,8}):");
    for (const int n : l) std::printf(" %d", n);
    std::printf("\n");

    l.reverse();
    std::printf("    l.reverse()          :");
    for (const int n : l) std::printf(" %d", n);
    std::printf("\n");

    std::printf("    ★ 这些是 list 的【成员函数】而不是 <algorithm> 的算法：\n");
    std::printf("      std::sort 需要随机访问迭代器，list 只有双向迭代器，编译不过。\n");
    std::printf("      这也是「链表不是 vector 的替代品」的第一条证据 —— 接口都不一样。\n");

    SubSection("forward_list：单向链表，更省内存但限制更多");
    std::forward_list<int> fl = {1, 2, 3};
    fl.push_front(0);  // ★ 只有 push_front，没有 push_back
    std::printf("    forward_list 只有 push_front：");
    for (const int n : fl) std::printf(" %d", n);
    std::printf("\n");
    std::printf("    没有 size()（要 O(n) 数，所以标准干脆不提供，用 std::distance 自己算，O(n)）\n");
    std::printf("    没有 back()；insert/erase 需要「前一个元素」的迭代器（before_begin）\n");
    std::printf("    每个节点比 list 省一个指针，适合内存极度受限且只需单向遍历的场景。\n");

    SubSection("每个节点的实际内存开销（实测）");
    std::printf("    sizeof(std::list<int>::value_type)   = %zu 字节（就一个 int）\n",
                sizeof(std::list<int>::value_type));
    std::printf("    sizeof(std::list<int>)               = %zu 字节（容器本身）\n",
                sizeof(std::list<int>));
    std::printf("    sizeof(std::vector<int>)             = %zu 字节（3 个指针）\n",
                sizeof(std::vector<int>));
    std::printf("    sizeof(std::deque<int>)              = %zu 字节\n", sizeof(std::deque<int>));
    std::printf("    ★ 但真正的开销在【堆】上：list 的每个元素是一个独立分配的节点，\n");
    std::printf("      节点里有两个指针 = 16 字节（64 位），再加上 malloc 的块头和对齐填充，\n");
    std::printf("      实际每个 int 可能占 48~64 字节 —— 比数据本身大 10 倍以上。\n");
    std::printf("      100 万个 int：vector 约 4MB，list 可能 50MB+。这个差距在真实项目里很致命。\n");

    SubSection("内存占用的直接对比（用 capacity / 遍历数节点）");
    std::vector<int> v(1000000, 1);
    std::list<int> l2(1000000, 1);
    std::printf("    100 万个 int：vector capacity = %zu 个元素 = %.1f MB\n", v.capacity(),
                static_cast<double>(v.capacity() * sizeof(int)) / (1024.0 * 1024.0));
    std::printf("                    list  = %zu 个节点，每个节点至少 %zu 字节 -> 至少 %.1f MB\n",
                l2.size(), sizeof(void*) * 2 + sizeof(int),
                static_cast<double>(l2.size() * (sizeof(void*) * 2 + sizeof(int))) / (1024.0 * 1024.0));
    std::printf("    （还没算 malloc 的块头和对齐填充，实际差距更大。）\n");

    SubSection("结论：什么时候才真的用链表");
    std::printf("    用 list 的正当理由（请对照检查，大部分需求都不满足）：\n");
    std::printf("      1) 元素很大（比如每个元素几 KB）且移动成本极高，\n");
    std::printf("         同时又必须频繁在中间插入删除；\n");
    std::printf("      2) 需要「插入/删除后其他元素的迭代器和引用绝不失效」；\n");
    std::printf("      3) 需要 splice 的常数时间接合（LRU 缓存、任务调度队列是经典例子）；\n");
    std::printf("      4) 内存碎片不是问题（实时系统里恰恰是问题，所以嵌入式更少用 list）。\n");
    std::printf("    ★ LRU 缓存是 list 少见的正当场景：\n");
    std::printf("      unordered_map<Key, list<Node>::iterator> + list<Node>\n");
    std::printf("      命中时用 splice 把节点搬到链表头部，O(1) 且不需要移动元素。\n");
    std::printf("      这个例子同时用到了「迭代器稳定」和「splice」两个特性。\n");
}

// ===========================================================================
// 10. vector<bool>
// ===========================================================================
void DemoVectorBool() {
    Section("10. vector<bool> 是特化，不是容器");

    std::vector<bool> vb = {true, false, true, true, false};
    std::printf("  std::vector<bool> vb = {true,false,true,true,false}\n");
    std::printf("    vb.size()                 = %zu\n", vb.size());
    std::printf("    sizeof(vb)                = %zu 字节\n", sizeof(vb));
    std::printf("    sizeof(std::vector<char>) = %zu 字节（对照）\n", sizeof(std::vector<char>));
    std::printf("    ★ 5 个 bool 在普通实现里要 5 字节（或 5 * sizeof(bool)），\n");
    std::printf("      而 vector<bool> 把它们压进【1 个字节里的 5 个 bit】。\n");

    SubSection("后果 1：operator[] 返回的不是 bool&，而是代理对象");
    // auto 会把代理对象【按值】拷出来，这个对象引用着某一位
    auto proxy = vb[0];
    std::printf("    auto proxy = vb[0];  -> 推导出的类型是 std::vector<bool>::reference\n");
    std::printf("    这是一个【代理类】（proxy reference），它内部存了「哪个字节的哪个 bit」。\n");
    std::printf("    proxy = false; 会真的改到 vb[0]：");
    proxy = false;
    std::printf("vb[0] 现在是 %s\n", vb[0] ? "true" : "false");

    SubSection("后果 2：不能取地址、没有真正的引用");
    std::printf("    bool* p = &vb[0];        // ★ 编译错误！代理对象是右值，取不到地址\n");
    std::printf("    bool& r = vb[0];         // ★ 编译错误！同理\n");
    std::printf("    vb.data()                // ★ 不存在！因为没有「元素数组」这回事\n");
    std::printf("    -> 任何「需要 bool* 或 bool& 的 C 接口」都无法直接对接 vector<bool>。\n");

    SubSection("后果 3：模板代码会挂");
    std::printf("    template<class T> void f(std::vector<T>& v) {\n");
    std::printf("        T* p = v.data();     // 对 T=bool 直接编译失败\n");
    std::printf("        for (T& x : v) ...   // 对 T=bool 直接编译失败\n");
    std::printf("    }\n");
    std::printf("    -> 这是真实的工程问题：泛型代码遇到 vector<bool> 会莫名其妙编译不过。\n");

    SubSection("后果 4：迭代器是「代理迭代器」，不能用 std::sort 等需要交换的算法");
    std::printf("    std::sort(vb.begin(), vb.end());  // ★ 编译失败（代理引用不满足可交换要求）\n");
    std::printf("    std::find / count 这类只读算法倒是可以用。\n");

    SubSection("那为什么标准还是这么定？历史原因");
    std::printf("    1994 年为了「节省内存」，vector<bool> 被写成特化；\n");
    std::printf("    后来发现它破坏了容器的泛型契约，但标准已经发布，无法更改（ABI 兼容）。\n");
    std::printf("    所以它成了一个「著名的历史包袱」。\n");

    SubSection("替代方案");
    std::printf("    1) 需要普通 bool 容器（可取地址、可泛型）：\n");
    std::printf("         std::vector<char>  或  std::vector<std::uint8_t>\n");
    std::printf("       sizeof(char) = 1，虽然每个只存 1 bit 信息，但行为完全正常。\n");
    std::printf("    2) 需要真正的位压缩（内存敏感 / 位掩码）：\n");
    std::printf("         std::bitset<N>            （大小编译期固定，接口干净）\n");
    std::printf("         std::vector<std::uint64_t>（自己按 64 位分块，最灵活）\n");
    std::printf("    3) 只想要「动态大小的位数组」：\n");
    std::printf("         自写一个 bitset 包装，或者用第三方（如 boost::dynamic_bitset）。\n");

    // bitset 演示：位压缩的正确姿势
    SubSection("std::bitset 演示（真正该用来做位操作的）");
    std::bitset<8> bs(0b00001010);
    std::printf("    std::bitset<8> bs(0b00001010) = %s\n", bs.to_string().c_str());
    bs.flip(0);
    std::printf("    bs.flip(0)                    = %s\n", bs.to_string().c_str());
    bs.set(7);
    std::printf("    bs.set(7)                     = %s\n", bs.to_string().c_str());
    std::printf("    bs.count()                    = %zu（1 的个数）\n", bs.count());
    std::printf("    bs.test(0)                    = %s（越界会抛 out_of_range）\n",
                bs.test(0) ? "true" : "false");
    std::printf("    bs.size()                     = %zu（编译期常量）\n", bs.size());
    std::printf("    ★ bitset 做位运算：bs & bs2 / | / ^ / ~ / << / >> 全部支持，\n");
    std::printf("      而且 to_ullong() / to_string() 直接可读，比手写移位清晰得多。\n");
}

}  // namespace

int main() {
    EnableUtf8Console();

    std::printf("==========================================================\n");
    std::printf(" 07_sequence_containers.cpp —— vector / array / deque / list\n");
    std::printf("==========================================================\n");

    DemoVectorBasics();
    DemoVectorReserve();
    DemoVectorVsList();
    DemoPushVsEmplace();
    DemoIteratorInvalidation();
    DemoEraseRemove();
    DemoArray();
    DemoDeque();
    DemoList();
    DemoVectorBool();

    std::printf("\n================ 小结 ================\n");
    std::printf("1. 默认用 std::vector；知道最终大小时先 reserve（省分配次数、省搬移、稳定延迟）。\n");
    std::printf("2. push_back 是均摊 O(1)；扩容按倍数增长（MSVC 约 1.5 倍），扩容那一次是 O(n)。\n");
    std::printf("3. 实测：vector 在遍历和中间插入上都比 list 快（缓存局部性的胜利）。\n");
    std::printf("   list 只在「手持迭代器做插入/删除」或「需要 splice」时才有意义。\n");
    std::printf("4. 迭代器失效：vector 扩容即全失效；中间 erase 让删除点之后的全部失效；\n");
    std::printf("   erase 的返回值是下一个有效位置，循环删除必须用它。\n");
    std::printf("5. 批量条件删除用 erase-remove（C++20 直接 std::erase_if），别在循环里逐个 erase。\n");
    std::printf("6. clear() 不释放内存；要还内存用 std::vector<T>().swap(v)。\n");
    std::printf("7. std::array 是零开销的定长数组，可拷贝、可按值传、可与算法库无缝协作。\n");
    std::printf("8. 需要两端 O(1) 操作用 deque；它的引用/指针在两端操作后仍然有效（迭代器会失效）。\n");
    std::printf("9. vector<bool> 是位压缩特化，operator[] 返回代理对象 —— 不要当普通容器用；\n");
    std::printf("   要普通 bool 容器用 vector<char>，要位操作用 std::bitset。\n");
    return 0;
}
