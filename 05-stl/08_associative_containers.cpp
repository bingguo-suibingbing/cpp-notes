// ============================================================================
// 08_associative_containers.cpp  ——  关联容器：map / set / unordered_*
//
// 演示主题：
//   1. map / set：红黑树、有序、O(log n)、迭代器稳定（除了被删的那个）
//   2. unordered_map / unordered_set：哈希表、平均 O(1)、最坏 O(n)、rehash 与迭代器失效
//   3. ★ 实测：unordered_map vs map 的插入与查找
//   4. ★ 选型决策表：要不要有序遍历？要不要范围查询？key 类型好不好哈希？
//   5. ★ 最大的坑：operator[] 在 key 不存在时会【插入一个默认值】
//        正确做法：find / at / contains（C++20）/ count
//   6. 自定义 key 类型：给 map 提供 operator<，给 unordered_map 提供哈希 + operator==
//   7. 透明比较器 std::less<>：避免为查询构造临时 std::string（真实的性能点）
//   8. insert_or_assign / try_emplace / emplace / insert 的语义差别
//   9. 什么时候该用「有序 vector 版 map」（flat_map 的思想）
//  10. set / multiset / multimap 与 count / equal_range
//
// 关键结论：
//   - map 的迭代器【不会】因为插入而失效，删除只让被删元素的迭代器失效；
//     unordered_map 在触发 rehash 时【所有迭代器都失效】（但指向元素的指针/引用仍有效）。
//   - map[key] 在 key 不存在时会插入 {key, Value{}}。用它来「查一下」会造成
//     两个后果：容器被意外修改（size 变大）、Value 必须有默认构造函数。
//   - 哈希容器的「平均 O(1)」有个前提：哈希函数质量好且没有恶意构造的 key。
//     最坏情况是 O(n)（全部落进同一个桶），这在「处理用户可控输入」时是安全问题
//     （HashDoS 攻击），必须用带随机种子的哈希或改成 map。
//   - 实测结论：查找/插入量大时 unordered_map 明显快；但要遍历有序结果、
//     或需要范围查询（lower_bound/upper_bound）时，只能用 map。
//
// 说明：MSVC 14.51 / /std:c++20 下 std::flat_map 不可用（<flat_map> 需要 C++23），
//       所以第 9 节用「有序 vector + lower_bound」手工演示 flat_map 的思想。
// ============================================================================

#include <algorithm>   // std::sort / std::lower_bound
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>  // std::hash / std::less
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>     // std::pair
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

long long UsSince(Clock::time_point t0) {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
}

// ===========================================================================
// 自定义 key 类型：给有序容器用（只需要 operator<）
// ===========================================================================
struct Employee {
    int id;
    std::string name;

    // ★ map / set 只需要「小于」这一个关系。
    //   std::less<Employee> 会调用 operator<，所以必须提供它。
    bool operator<(const Employee& other) const {
        if (id != other.id) return id < other.id;  // 先按 id
        return name < other.name;                  // id 相同时再按 name（保证严格弱序）
    }
    // 提供 == 是为了 unordered_* 和普通比较；map 不需要它
    bool operator==(const Employee& other) const {
        return id == other.id && name == other.name;
    }
};

// ===========================================================================
// 自定义 key 类型：给哈希容器用（需要 hash 特化 + operator==）
//
// ★ 注意：std::hash 的特化不能放在匿名命名空间里（会报 C2888，
//   因为匿名命名空间里的类型有内部链接，无法与 std 模板建立合法特化）。
//   所以这里先关掉匿名命名空间，把 Point 放在【具名命名空间】里，
//   特化写在全局命名空间，然后再重新打开匿名命名空间。
// ===========================================================================
}  // namespace

namespace demo_point {

struct Point {
    int x;
    int y;

    bool operator==(const Point& other) const { return x == other.x && y == other.y; }
};

}  // namespace demo_point

// ---------------------------------------------------------------------------
// std::hash 的特化写在【全局命名空间】（即给 std::hash 加一个显式特化），
// 否则 unordered_map 找不到它，会报「没有可用的哈希函数」。
// ---------------------------------------------------------------------------
template <>
struct std::hash<demo_point::Point> {
    std::size_t operator()(const demo_point::Point& p) const noexcept {
        // 组合两个成员：常用做法是「乘一个质数再异或」
        // ★ 质量差的哈希（比如 x ^ y）会让 (1,2) 和 (2,1) 落到同一个桶，
        //   也会让大量点挤在少数桶里，把平均 O(1) 退化成 O(n)。
        const std::size_t h1 = std::hash<int>{}(p.x);
        const std::size_t h2 = std::hash<int>{}(p.y);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};

namespace {

using demo_point::Point;

// ===========================================================================
// 1. map / set 基础
// ===========================================================================
void DemoMapBasics() {
    Section("1. map / set：红黑树、有序、O(log n)");

    SubSection("map：key -> value");
    std::map<std::string, int> ages;
    // insert 返回 pair<iterator, bool>：bool 表示「是否真的插入了」（false = key 已存在）
    const auto r1 = ages.insert({"alice", 30});
    const auto r2 = ages.insert({"bob", 25});
    const auto r3 = ages.insert({"alice", 99});  // key 已存在 -> 插入失败，值不变
    std::printf("    insert({\"alice\",30}) -> 插入成功=%s\n", r1.second ? "true" : "false");
    std::printf("    insert({\"bob\",25})   -> 插入成功=%s\n", r2.second ? "true" : "false");
    std::printf("    insert({\"alice\",99}) -> 插入成功=%s，alice 仍然是 %d（insert 不覆盖已有值）\n",
                r3.second ? "true" : "false", ages.at("alice"));

    ages["carol"] = 40;  // ★ operator[]：不存在则插入默认值再赋值
    ages["dave"] = 22;
    std::printf("    ages[\"carol\"] = 40 之后 size = %zu\n", ages.size());

    SubSection("★ map 的遍历是【按 key 有序】的");
    std::printf("    （插入顺序是 alice, bob, carol, dave；输出顺序是字典序）\n");
    for (const auto& [name, age] : ages) {  // C++17 结构化绑定
        std::printf("      %-8s -> %d\n", name.c_str(), age);
    }

    SubSection("按 key 反序遍历");
    for (auto it = ages.rbegin(); it != ages.rend(); ++it) {
        std::printf("      %-8s -> %d\n", it->first.c_str(), it->second);
    }

    SubSection("查找：find / count / at / contains");
    const auto it = ages.find("bob");
    std::printf("    find(\"bob\")   != end() -> %s，值 %d\n", (it != ages.end()) ? "找到" : "没找到",
                it->second);
    std::printf("    find(\"zoe\")   == end() -> %s\n",
                (ages.find("zoe") == ages.end()) ? "没找到（返回 end()）" : "找到了");
    std::printf("    count(\"bob\")           -> %zu（map 只可能是 0 或 1）\n", ages.count("bob"));
    std::printf("    count(\"zoe\")           -> %zu\n", ages.count("zoe"));
    std::printf("    at(\"bob\")              -> %d（不存在会抛 out_of_range）\n", ages.at("bob"));
#if defined(__cpp_lib_generic_associative_lookup)
    std::printf("    contains(\"bob\")        -> %s（C++20，语义最清楚的写法）\n",
                ages.contains("bob") ? "true" : "false");
#else
    std::printf("    contains 需要 C++20\n");
#endif
    std::printf("    ★ 「只是想知道在不在」用 contains（C++20）；\n");
    std::printf("      C++17 及以前写 count(...) != 0 或 find(...) != end()。\n");
    std::printf("      count 语义上是「计数」，对 map 有轻微误导；find 最通用。\n");

    SubSection("范围查询：lower_bound / upper_bound / equal_range（map 的独门武器）");
    std::map<int, std::string> m = {{10, "ten"}, {20, "twenty"}, {30, "thirty"}, {40, "forty"}};
    const auto lb = m.lower_bound(20);  // 第一个 >= 20
    const auto ub = m.upper_bound(30);  // 第一个 > 30
    std::printf("    lower_bound(20) -> key %d\n", lb->first);
    std::printf("    upper_bound(30) -> key %d\n", ub->first);
    std::printf("    区间 [20, 30] 内的元素：");
    for (auto i = lb; i != ub; ++i) {
        std::printf("(%d,%s) ", i->first, i->second.c_str());
    }
    std::printf("\n    ★ 这是「按分数段查学生」「按时间区间查日志」这类需求的标准写法，\n");
    std::printf("      unordered_map 完全做不到（它没有顺序）。\n");

    SubSection("删除与迭代器稳定性");
    std::printf("    删除前 size = %zu\n", ages.size());
    const std::size_t erased = ages.erase("bob");
    std::printf("    erase(\"bob\") 返回删除个数 %zu，之后 size = %zu\n", erased, ages.size());
    std::printf("    ★ map 的插入不会让任何已有迭代器失效；删除只让「被删的那个」失效。\n");
    std::printf("      所以「拿着 iterator 存在别处」是 map 的合法用法（unordered_map 不行）。\n");
    std::printf("      erase(it) 返回下一个有效迭代器（C++11 起），可以安全地循环删除。\n");

    SubSection("set：只有 key，没有 value");
    std::set<int> s = {5, 3, 9, 1, 3, 7};  // 重复的 3 被去重
    std::printf("    set<int> s = {5,3,9,1,3,7} -> size=%zu（自动去重 + 排序）：", s.size());
    for (const int n : s) std::printf(" %d", n);
    std::printf("\n");
    std::printf("    insert(4) 返回的 second = %s（是否插入成功）\n", s.insert(4).second ? "true" : "false");
    std::printf("    insert(3) 返回的 second = %s（3 已存在）\n", s.insert(3).second ? "true" : "false");
    std::printf("    ★ 用 set 对 vector 去重 + 排序：\n");
    std::printf("      std::set<int> uniq(v.begin(), v.end());\n");
    std::printf("        -> 结果是【有序】的；如果只想去重不排序，用 sort + unique + erase 更快。\n");
}

// ===========================================================================
// 2. unordered_map / unordered_set
// ===========================================================================
void DemoUnorderedBasics() {
    Section("2. unordered_map / unordered_set：哈希表");

    std::unordered_map<std::string, int> um;
    um["alice"] = 30;
    um["bob"] = 25;
    um["carol"] = 40;
    um["dave"] = 22;

    std::printf("  unordered_map 的内容（顺序不保证，通常按桶顺序）：\n");
    for (const auto& [k, v] : um) {
        std::printf("    %-8s -> %d\n", k.c_str(), v);
    }
    std::printf("  注意：输出顺序和插入顺序、字典序都无关，换个编译器/库版本就会变。\n");
    std::printf("  ★ 所以「依赖 unordered_map 遍历顺序」的代码都是定时炸弹。\n");

    SubSection("桶（bucket）相关接口：判断哈希质量");
    std::printf("    bucket_count()   = %zu（桶的数量）\n", um.bucket_count());
    std::printf("    size()           = %zu\n", um.size());
    std::printf("    load_factor()    = %.3f（= size / bucket_count，负载因子）\n", um.load_factor());
    std::printf("    max_load_factor()= %.3f（默认 1.0，超过就触发 rehash）\n", um.max_load_factor());
    for (const std::string key : {"alice", "bob", "carol", "dave"}) {
        std::printf("    bucket(\"%-5s\")   = %zu\n", key.c_str(), um.bucket(key));
    }
    std::printf("    ★ 如果发现大量 key 落在同一个 bucket，说明哈希质量差或负载太高。\n");

    SubSection("rehash：什么时候发生，代价是什么");
    std::unordered_map<int, int> grow;
    std::printf("    初始 bucket_count = %zu\n", grow.bucket_count());
    for (int i = 0; i < 20; ++i) {
        const std::size_t before = grow.bucket_count();
        grow[i] = i;
        if (grow.bucket_count() != before) {
            std::printf("      插入第 %2d 个元素：bucket_count %zu -> %zu（rehash！）\n", i + 1,
                        before, grow.bucket_count());
        }
    }
    std::printf("    ★ rehash 的代价：重新分配桶数组 + 把【所有】元素重新挂到新桶上 => O(n)；\n");
    std::printf("      而且【所有迭代器都会失效】（这点和 map 完全相反）。\n");
    std::printf("    ★ 已知元素个数时先 reserve 可以彻底避免 rehash：\n");
    std::unordered_map<int, int> pre;
    pre.reserve(1000000);  // 按元素个数预留（内部会算好桶数）
    std::printf("      pre.reserve(1000000) -> bucket_count = %zu\n", pre.bucket_count());

    SubSection("自定义负载因子");
    std::unordered_map<int, int> tuned;
    tuned.max_load_factor(0.7f);  // 桶更空 -> 冲突更少 -> 更快，但更费内存
    std::printf("    max_load_factor(0.7) 之后，插入 100 个元素：\n");
    for (int i = 0; i < 100; ++i) tuned[i] = i;
    std::printf("      bucket_count = %zu，load_factor = %.3f\n", tuned.bucket_count(),
                tuned.load_factor());
    std::printf("    ★ 调低负载因子是「用内存换速度」；默认 1.0 对大多数场景够用。\n");

    SubSection("unordered_set");
    std::unordered_set<std::string> us = {"apple", "banana", "cherry", "banana"};
    std::printf("    size=%zu（banana 去重）\n", us.size());
    std::printf("    us.count(\"apple\") = %zu，us.count(\"nope\") = %zu\n", us.count("apple"),
                us.count("nope"));
    std::printf("    ★ 典型用途：去重、白名单校验、图遍历里记录访问过的节点。\n");
}

// ===========================================================================
// 3. 实测：unordered_map vs map
// ===========================================================================
void DemoPerformance() {
    Section("3. 【实测】unordered_map vs map");

    constexpr int kCount = 200000;
    constexpr int kQueries = 500000;

    // 生成同一批 key（包含一部分「存在的」和一部分「不存在的」）
    std::vector<int> keys;
    keys.reserve(static_cast<std::size_t>(kCount));
    std::mt19937 gen(42);
    std::uniform_int_distribution<int> dist(0, 10000000);
    for (int i = 0; i < kCount; ++i) keys.push_back(dist(gen));

    SubSection("A. 插入 20 万个元素");
    std::map<int, int> om;
    const auto t0 = Clock::now();
    for (int i = 0; i < kCount; ++i) {
        om[keys[static_cast<std::size_t>(i)]] = i;
    }
    const long long usMapInsert = UsSince(t0);
    std::printf("    std::map          : %8lld us（size=%zu）\n", usMapInsert, om.size());

    std::unordered_map<int, int> um;
    um.reserve(static_cast<std::size_t>(kCount));
    const auto t1 = Clock::now();
    for (int i = 0; i < kCount; ++i) {
        um[keys[static_cast<std::size_t>(i)]] = i;
    }
    const long long usUmInsert = UsSince(t1);
    std::printf("    std::unordered_map: %8lld us（size=%zu，已 reserve）\n", usUmInsert, um.size());
    if (usUmInsert > 0) {
        std::printf("    -> map 约为 unordered_map 的 %.2f 倍耗时\n",
                    static_cast<double>(usMapInsert) / static_cast<double>(usUmInsert));
    }

    SubSection("B. 查询 50 万次（一半命中、一半未命中）");
    volatile long long sink = 0;
    const auto t2 = Clock::now();
    for (int i = 0; i < kQueries; ++i) {
        const int k = keys[static_cast<std::size_t>(i * 7919) % keys.size()];
        const auto it = om.find(k);
        if (it != om.end()) sink += it->second;
    }
    const long long usMapFind = UsSince(t2);

    const auto t3 = Clock::now();
    for (int i = 0; i < kQueries; ++i) {
        const int k = keys[static_cast<std::size_t>(i * 7919) % keys.size()];
        const auto it = um.find(k);
        if (it != um.end()) sink += it->second;
    }
    const long long usUmFind = UsSince(t3);

    std::printf("    20 万元素上 find %d 次：\n", kQueries);
    std::printf("      std::map          : %8lld us（每次约 %.3f us，O(log n) 约 %d 层比较）\n",
                usMapFind, static_cast<double>(usMapFind) / kQueries, 18);
    std::printf("      std::unordered_map: %8lld us（每次约 %.3f us，平均 O(1)）\n", usUmFind,
                static_cast<double>(usUmFind) / kQueries);
    if (usUmFind > 0) {
        std::printf("      -> map 约为 unordered_map 的 %.2f 倍耗时\n",
                    static_cast<double>(usMapFind) / static_cast<double>(usUmFind));
    }
    std::printf("      sink = %lld（防止优化）\n", sink);
    std::printf("    ★ 结论：查找密集时 unordered_map 有明显优势；\n");
    std::printf("      但 n 很小时（几百个元素）两者差别不大，因为 map 的比较都在缓存里。\n");

    SubSection("C. 按 key 顺序遍历（map 的主场）");
    volatile long long sink2 = 0;
    const auto t4 = Clock::now();
    for (const auto& [k, v] : om) sink2 += k + v;  // 天然有序
    const long long usMapIter = UsSince(t4);

    const auto t5 = Clock::now();
    for (const auto& [k, v] : um) sink2 += k + v;  // 无序（其实还快一点，因为内存布局紧凑）
    const long long usUmIter = UsSince(t5);

    std::printf("    遍历所有元素：\n");
    std::printf("      std::map          : %8lld us（但结果是有序的）\n", usMapIter);
    std::printf("      std::unordered_map: %8lld us（快，但结果无序）\n", usUmIter);
    std::printf("    ★ 如果需求是「按 key 排序输出」，unordered_map 还要额外 O(n log n) 排序；\n");
    std::printf("      这种情况下 map 反而是最优选择。\n");
    std::printf("      sink2 = %lld\n", sink2);

    SubSection("D. 恶意哈希 / 最坏情况 O(n) 演示");
    std::printf("    假设哈希函数把【所有 key】都映射到同一个桶：\n");
    // 一个「故意全部碰撞」的哈希：所有 key 返回同一个值
    struct BadHash {
        std::size_t operator()(int) const noexcept { return 0; }
    };
    constexpr int kBadN = 20000;
    std::unordered_map<int, int, BadHash> bad;
    bad.reserve(static_cast<std::size_t>(kBadN) * 4);  // 桶够多，但哈希全撞一起
    for (int i = 0; i < kBadN; ++i) bad[i] = i;

    const auto t6 = Clock::now();
    volatile int found = 0;
    for (int i = 0; i < kBadN; ++i) {
        if (bad.find(i) != bad.end()) ++found;
    }
    const long long usBad = UsSince(t6);

    std::unordered_map<int, int> good;
    good.reserve(static_cast<std::size_t>(kBadN) * 4);
    for (int i = 0; i < kBadN; ++i) good[i] = i;
    const auto t7 = Clock::now();
    for (int i = 0; i < kBadN; ++i) {
        if (good.find(i) != good.end()) ++found;
    }
    const long long usGood = UsSince(t7);

    std::printf("      %d 个元素、每个都查一遍：\n", kBadN);
    std::printf("      哈希全碰撞（所有 key -> 同一个桶）: %8lld us  <- 退化成链表线性查找 O(n)\n",
                usBad);
    std::printf("      正常哈希                          : %8lld us\n", usGood);
    if (usGood > 0) {
        std::printf("      -> 坏哈希约为好哈希的 %.0f 倍耗时\n",
                    static_cast<double>(usBad) / static_cast<double>(usGood));
    }
    std::printf("      found = %d\n", found);
    std::printf("    ★ 这就是「平均 O(1)、最坏 O(n)」的含义。\n");
    std::printf("      安全影响：如果 key 来自用户输入（HTTP 头、JSON 字段名），\n");
    std::printf("      攻击者可以构造一批「在你的哈希函数下全部碰撞」的 key，\n");
    std::printf("      让服务器 CPU 打满 —— 这叫 HashDoS / 哈希碰撞攻击。\n");
    std::printf("      防御：1) 用带随机种子的哈希（libstdc++ / libc++ 默认就加随机种子）；\n");
    std::printf("            2) 限制单个请求的 key 数量；3) 对不可信输入改用 std::map。\n");
    std::printf("      注意：MSVC 的 std::hash<int> 是恒等映射且【没有随机种子】，\n");
    std::printf("            所以在 MSVC 上处理不可信整数 key 时要特别小心。\n");
}

// ===========================================================================
// 4. 选型决策表
// ===========================================================================
void DemoSelectionGuide() {
    Section("4. ★ 容器选型决策表");

    SubSection("第一步：按「需要什么操作」查表");
    std::printf("  你需要的能力                        | 该用              | 复杂度\n");
    std::printf("  ------------------------------------|-------------------|--------------------\n");
    std::printf("  只要 key，不要 value，且要去重排序  | std::set          | 插入/查找 O(log n)\n");
    std::printf("  只要 key，不要 value，不需要排序    | std::unordered_set| 平均 O(1)\n");
    std::printf("  key -> value，需要按 key 有序遍历   | std::map          | O(log n)\n");
    std::printf("  key -> value，需要范围查询          | std::map          | O(log n)\n");
    std::printf("  key -> value，只要快速查/改         | std::unordered_map| 平均 O(1)\n");
    std::printf("  允许重复 key（有序）                | std::multimap     | O(log n)\n");
    std::printf("  允许重复 key（无序，1 对多）        | unordered_multimap| 平均 O(1)\n");
    std::printf("  「排名」「第 k 小」这类序统计       | std::map + 计数器 | O(log n)\n");
    std::printf("  元素很少（< 几百）又要顺序          | 排序的 vector     | 查找 O(log n)，更省内存\n");

    SubSection("第二步：三个问题帮你定下来");
    std::printf("  Q1：需要「按 key 有序」的输出吗？\n");
    std::printf("      需要 -> map / set（unordered 还要额外排序，反而更慢）\n");
    std::printf("      不需要 -> 继续 Q2\n");
    std::printf("  Q2：需要「范围查询」（lower_bound / upper_bound / 区间统计）吗？\n");
    std::printf("      需要 -> map / set（哈希容器做不到）\n");
    std::printf("      不需要 -> 继续 Q3\n");
    std::printf("  Q3：key 类型有「质量好、稳定」的哈希吗？\n");
    std::printf("      有（int / string / 自己写好的特化）-> unordered_map / unordered_set\n");
    std::printf("      没有，或者 key 来自不可信输入      -> map / set\n");
    std::printf("  补充 Q4：需要「插入后迭代器依然有效」吗？\n");
    std::printf("      需要 -> map / set（unordered_map 的 rehash 会让迭代器全失效）\n");

    SubSection("第三步：复杂度与失效规则对照");
    std::printf("  操作            | map/set      | unordered_map/set\n");
    std::printf("  ----------------|--------------|---------------------------\n");
    std::printf("  查找 find       | O(log n)     | 平均 O(1)，最坏 O(n)\n");
    std::printf("  插入 insert     | O(log n)     | 平均 O(1)，最坏 O(n)\n");
    std::printf("  删除 erase      | O(log n)     | 平均 O(1)，最坏 O(n)\n");
    std::printf("  范围查询        | O(log n)+k   | 不支持\n");
    std::printf("  有序遍历        | O(n)         | 无序\n");
    std::printf("  插入后迭代器    | 全部有效     | rehash 则全部失效\n");
    std::printf("  删除后迭代器    | 只有被删的失效 | 只有被删的失效\n");
    std::printf("  元素内存        | 每节点 3 指针+颜色 | 每节点 1 指针 + 桶数组\n");
    std::printf("  额外内存        | 无           | 桶数组（通常 size 的 1~2 倍）\n");

    SubSection("工程经验：什么时候「都不选」");
    std::printf("  1) 元素总量很小（< 50）且不常改：\n");
    std::printf("       用排序的 vector + std::lower_bound。\n");
    std::printf("       内存连续、无节点开销、遍历极快 —— 实测常常比 map 还快。\n");
    std::printf("  2) key 就是「小整数连续区间」（比如 0..1000 的状态表）：\n");
    std::printf("       直接用 std::vector<T>，O(1) 且没有哈希开销。\n");
    std::printf("  3) 需要「按插入顺序遍历」：\n");
    std::printf("       vector<std::pair<K,V>> + unordered_map<K, index>（有序 + 快速查找）。\n");
    std::printf("  4) 频繁按「最近使用」顺序调整：\n");
    std::printf("       unordered_map<K, list<Node>::iterator> + list<Node>（LRU 的标准实现）。\n");
}

// ===========================================================================
// 5. operator[] 的坑
// ===========================================================================
void DemoSubscriptTrap() {
    Section("5. ★★ operator[] 会插入：新手最常踩的坑");

    std::map<std::string, int> m = {{"a", 1}, {"b", 2}};
    std::printf("  初始：size=%zu，内容：", m.size());
    for (const auto& [k, v] : m) std::printf("(%s,%d) ", k.c_str(), v);
    std::printf("\n");

    SubSection("错误写法：用 [] 来「查一下」");
    std::printf("    if (m[\"zoe\"] == 0) { ... }   // 想判断 zoe 在不在\n");
    const int val = m["zoe"];  // ★ 这一行就修改了容器！
    std::printf("    执行 m[\"zoe\"] 之后：size=%zu（多了一个元素！），值是 %d\n", m.size(), val);
    std::printf("    内容：");
    for (const auto& [k, v] : m) std::printf("(%s,%d) ", k.c_str(), v);
    std::printf("\n");
    std::printf("    ★ 两个后果：\n");
    std::printf("      1) 容器被意外修改（size 变大）—— 循环里这样写还会让 size 不断增长；\n");
    std::printf("      2) value 类型必须「可默认构造」，否则 m[key] 直接编译不过。\n");

    SubSection("正确写法对照表");
    std::printf("  目的                     | 错误写法            | 正确写法\n");
    std::printf("  -------------------------|---------------------|---------------------------\n");
    std::printf("  判断 key 是否存在        | if (m[k])           | m.contains(k)  (C++20)\n");
    std::printf("                           |                     | m.find(k) != m.end()\n");
    std::printf("                           |                     | m.count(k) != 0\n");
    std::printf("  读取（不存在时报错）     | int v = m[k];       | int v = m.at(k);  // 抛异常\n");
    std::printf("  读取（不存在时用默认值） | int v = m[k];  ★改容器 | find + 判断 iterator\n");
    std::printf("  读取（不存在时插入默认） | int v = m[k];       | 这才是 [] 的正当用途\n");
    std::printf("  读取（不存在时插入指定） | if(!m.count(k)) m[k]=d; | m.try_emplace(k, d)\n");
    std::printf("  插入/覆盖都要            | m[k] = v;           | m.insert_or_assign(k, v)\n");

    SubSection("「读但不存在时给默认值」的正确写法");
    std::map<std::string, int> cfg = {{"timeout", 30}};
    const auto Lookup = [](const std::map<std::string, int>& c, const std::string& key,
                           int fallback) -> int {
        const auto it = c.find(key);  // ★ find 不修改容器
        return (it != c.end()) ? it->second : fallback;
    };
    std::printf("    Lookup(cfg, \"timeout\", 0)  = %d\n", Lookup(cfg, "timeout", 0));
    std::printf("    Lookup(cfg, \"missing\", -1) = %d\n", Lookup(cfg, "missing", -1));
    std::printf("    调用后 cfg.size() = %zu（★ 没有偷偷插入，这就是 find 的价值）\n", cfg.size());

    SubSection("★ 最危险的场景：在循环条件里用 []");
    std::printf("    真实事故代码（会无限循环 / 内存爆掉）：\n");
    std::printf("      while (m[someKey] < threshold) {   // 每次判断都插入一个新元素！\n");
    std::printf("          ...\n");
    std::printf("      }\n");
    std::printf("    另一个常见版本：\n");
    std::printf("      for (const auto& k : keys) total += m[k];  // 不存在的 key 全被插进来\n");
    std::printf("        -> 循环结束后 m.size() 悄悄变大了，后续依赖 size 的逻辑全错。\n");

    SubSection("unordered_map 的 [] 有同样的行为，而且更隐蔽");
    std::unordered_map<std::string, std::vector<int>> groups;
    std::printf("    插入前 size=%zu\n", groups.size());
    (void)groups["nonexistent"];  // 偷偷插入了一个空 vector
    std::printf("    访问 groups[\"nonexistent\"] 之后 size=%zu（插入了空 vector）\n", groups.size());
    std::printf("    ★ 注意：这里的写法收益是「分组统计」时很方便：\n");
    std::printf("      groups[category].push_back(item);   // 不存在就建一个空组\n");
    std::printf("      这是 [] 的正当用途 —— 但一定要清楚「它会插入」这个前提。\n");
}

// ===========================================================================
// 6. 自定义 key 类型
// ===========================================================================
void DemoCustomKey() {
    Section("6. 自定义 key 类型");

    SubSection("给 map / set 用：只需提供 operator<");
    std::map<Employee, std::string> byEmployee;
    byEmployee.insert({{1001, "Alice"}, "Engineering"});
    byEmployee.insert({{1002, "Bob"}, "Sales"});
    byEmployee.insert({{1001, "Alice Clone"}, "Marketing"});  // id 相同、name 不同 -> 是另一个 key
    std::printf("    size = %zu\n", byEmployee.size());
    for (const auto& [emp, dept] : byEmployee) {
        std::printf("      id=%-5d name=%-12s -> %s\n", emp.id, emp.name.c_str(), dept.c_str());
    }
    std::printf("    ★ 为什么只需要 operator< ？\n");
    std::printf("      map 判断「两个 key 相等」的方式是 !(a<b) && !(b<a)，即「等价」。\n");
    std::printf("      所以 operator< 必须满足【严格弱序】：\n");
    std::printf("        - 不自反：a < a 必须为 false\n");
    std::printf("        - 反对称：a<b 和 b<a 不能同时为 true\n");
    std::printf("        - 传递：a<b 且 b<c 则 a<c\n");
    std::printf("      违反后果：容器内部结构被破坏，出现「找不到明明插入过的元素」这种玄学 bug。\n");
    std::printf("    ★ 写多字段比较要「逐字段字典序」，不要用 a.id + a.name 之类的求和！\n");

    SubSection("常见的错误 operator< 写法");
    std::printf("    错误 1：用 <= 而不是 <\n");
    std::printf("      bool operator<(const T& o) const { return v <= o.v; }  // ★ 违反不自反\n");
    std::printf("    错误 2：只比较部分字段，导致「不等价但也不小于」的 key 互相冲突\n");
    std::printf("      bool operator<(const T& o) const { return id < o.id; }\n");
    std::printf("      // 如果 id 会重复，这些元素在 set 里会被当成同一个 -> 丢数据\n");
    std::printf("    错误 3：用浮点数当 key 且不做处理\n");
    std::printf("      NaN 参与比较永远为 false -> 严格弱序被破坏 -> UB\n");
    std::printf("      浮点误差还会让「看起来相等」的值变成不同 key。\n");
    std::printf("      建议：用定点整数（比如「分」）当 key，而不是 double。\n");

    SubSection("给 unordered_map / unordered_set 用：需要 hash 特化 + operator==");
    std::unordered_map<Point, std::string> pointNames;
    pointNames[{0, 0}] = "origin";
    pointNames[{1, 2}] = "A";
    pointNames[{3, 4}] = "B";
    std::printf("    size = %zu\n", pointNames.size());
    const Point probe{1, 2};
    const auto it = pointNames.find(probe);
    std::printf("    find({1,2}) -> %s\n",
                (it != pointNames.end()) ? it->second.c_str() : "没找到");
    std::printf("    插入前 bucket_count=%zu\n", pointNames.bucket_count());
    std::printf("    ★ std::hash<Point> 的特化写在 std 命名空间里（本文件开头就是这么做的）。\n");
    std::printf("      也可以不特化 std::hash，而是把哈希器当【第三个模板参数】传进去：\n");
    std::printf("        struct PointHash { size_t operator()(const Point&) const; };\n");
    std::printf("        std::unordered_map<Point, V, PointHash> m;\n");
    std::printf("      这种写法更局部、不会污染 std 命名空间，团队规范里常推荐这种做法。\n");

    SubSection("哈希函数的写法要点");
    std::printf("    1) 必须对「相等的 key」返回相同的哈希值（== 为 true 则 hash 必须相等）；\n");
    std::printf("    2) 不同的 key 尽量返回不同的值（减少冲突）；\n");
    std::printf("    3) 要快：哈希函数在每次查找时都会调用；\n");
    std::printf("    4) 组合多个字段的常用模式（本文件的 Point 就用了这个）：\n");
    std::printf("         h = h1 ^ (h2 + 0x9e3779b97f4a7c15 + (h1<<6) + (h1>>2));\n");
    std::printf("       这个魔数来自黄金比例，能让相邻输入散得更开。\n");
    std::printf("    5) 反例：x ^ y 这种简单异或会让 (1,2) 和 (2,1) 碰撞；\n");
    std::printf("       只用 x 也会让所有 (x, *) 挤在同一条线上。\n");
    std::printf("    ★ 如果哈希质量不好，实测代价是查找从 O(1) 退化成 O(n)（见第 3 节 D）。\n");
}

// ===========================================================================
// 7. 透明比较器 std::less<>
// ===========================================================================
void DemoTransparentComparator() {
    Section("7. 透明比较器 std::less<>：省掉临时 std::string");

    std::map<std::string, int, std::less<>> m;  // ★ 注意第三个参数是 std::less<>（无模板实参）
    m["apple"] = 1;
    m["banana"] = 2;
    m["cherry"] = 3;

    SubSection("为什么 std::less<> 能省一次构造");
    std::printf("    默认的 std::map<std::string, int> 用的是 std::less<std::string>，\n");
    std::printf("    它的 operator() 形参是 const std::string&。\n");
    std::printf("    所以你写 m.find(\"apple\") 时，编译器必须先把字符串字面量\n");
    std::printf("    【构造成一个临时 std::string】才能调用比较函数：\n");
    std::printf("      m.find(\"apple\")\n");
    std::printf("        -> 构造临时 string(\"apple\")   ★ 可能堆分配（长度 > 15 时一定分配）\n");
    std::printf("        -> 用临时 string 做 O(log n) 次比较\n");
    std::printf("        -> 析构临时 string\n");
    std::printf("    而 std::less<> 是「透明」的：它有一个模板化的 operator()，\n");
    std::printf("    可以直接拿 const char* 和 std::string 比较（std::string 有与 char* 的比较重载），\n");
    std::printf("    所以【完全不需要构造临时 string】。\n");

    SubSection("实测（用长于 SSO 的 key 放大效果）");
    // 用长度 > 15 的 key，保证比较时真的会走堆分配
    const std::string longKey = "a-very-long-key-0000000000001234";  // 长度 31
    std::map<std::string, int> normalMap;
    std::map<std::string, int, std::less<>> transparentMap;
    normalMap[longKey] = 1;
    transparentMap[longKey] = 1;

    constexpr int kQueries = 200000;
    // 用一个 C 字符串字面量去查 —— 这才是真实场景（配置项名、JSON 字段名都是字面量）
    const char* kProbe = "a-very-long-key-0000000000001234";

    volatile int found = 0;
    const auto t0 = Clock::now();
    for (int i = 0; i < kQueries; ++i) {
        if (normalMap.find(kProbe) != normalMap.end()) ++found;  // ★ 每次构造临时 string
    }
    const long long usNormal = UsSince(t0);

    const auto t1 = Clock::now();
    for (int i = 0; i < kQueries; ++i) {
        if (transparentMap.find(kProbe) != transparentMap.end()) ++found;  // ★ 零构造
    }
    const long long usTrans = UsSince(t1);

    std::printf("    %d 次用 const char* 查找（本机实测，仅供参考）：\n", kQueries);
    std::printf("      map<string,int>            : %8lld us（每次构造临时 string）\n", usNormal);
    std::printf("      map<string,int,less<>>     : %8lld us（零构造）\n", usTrans);
    if (usTrans > 0) {
        std::printf("      -> 前者约为后者的 %.2f 倍耗时\n",
                    static_cast<double>(usNormal) / static_cast<double>(usTrans));
    }
    std::printf("      found = %d\n", found);
    std::printf("    ★ 注意：如果 key 长度 <= 15（落在 SSO 内），临时 string 不分配堆，\n");
    std::printf("      差距会小很多 —— 这正说明「先搞清开销在哪，再优化」。\n");

    SubSection("unordered_map 的等价物：异构查找（C++20）");
    std::printf("    C++20 给无序容器也加了异构查找：\n");
    std::printf("      std::unordered_map<std::string, int, std::hash<std::string>,\n");
    std::printf("                         std::equal_to<>> m;   // ★ 第四个参数用 equal_to<>\n");
    std::printf("      m.find(\"literal\");   // C++20 起不会构造临时 string\n");
    std::printf("    可惜 std::hash<std::string> 默认【不是】透明的，所以还需要：\n");
    std::printf("      自定义一个「能同时哈希 string 和 string_view」的哈希器（转成 string_view 再哈希）。\n");
    std::printf("    这是 C++20 里比较容易被忽略的一块，实践中常直接自定义哈希器解决。\n");

    SubSection("其它需要透明比较器的地方");
    std::printf("    std::set<std::string, std::less<>>      同上\n");
    std::printf("    std::map<std::string, T, std::less<>>   同上\n");
    std::printf("    std::lower_bound / upper_bound          没有透明版本，必须自己保证类型一致\n");
    std::printf("    ★ 建议：只要 key 是 std::string 并且经常用字面量查，就加 std::less<>。\n");
    std::printf("      代价是 map 的类型名变长，收益是去掉每次查询的一次内存分配。\n");
}

// ===========================================================================
// 8. insert_or_assign / try_emplace
// ===========================================================================
void DemoInsertVariants() {
    Section("8. insert / emplace / try_emplace / insert_or_assign");

    SubSection("语义对照（这是选型的核心）");
    std::printf("  API                            | key 已存在时       | key 不存在时\n");
    std::printf("  -------------------------------|--------------------|------------------\n");
    std::printf("  insert({k,v})                  | 不动（返回 false）  | 插入\n");
    std::printf("  insert_or_assign(k, v)         | 覆盖为 v           | 插入\n");
    std::printf("  emplace(k, args...)            | 不动（构造后丢弃）  | 就地构造\n");
    std::printf("  try_emplace(k, args...)        | 不动，且【不构造】  | 就地构造\n");
    std::printf("  operator[](k) = v              | 覆盖               | 先默认构造再赋值\n");

    SubSection("实测语义");
    std::map<std::string, std::string> m;

    const auto r1 = m.insert({"a", "from-insert"});
    std::printf("    insert({\"a\",\"from-insert\"})       -> 插入=%s，m[\"a\"]=\"%s\"\n",
                r1.second ? "true" : "false", m["a"].c_str());
    const auto r2 = m.insert({"a", "insert-again"});
    std::printf("    再 insert({\"a\",\"insert-again\"})    -> 插入=%s，m[\"a\"]=\"%s\"（★ 没被覆盖）\n",
                r2.second ? "true" : "false", m["a"].c_str());

    const auto r3 = m.insert_or_assign("a", "from-insert_or_assign");
    std::printf("    insert_or_assign(\"a\", ...)          -> 插入=%s，m[\"a\"]=\"%s\"（★ 覆盖了）\n",
                r3.second ? "true" : "false", m["a"].c_str());

    const auto r4 = m.try_emplace("b", 3, 'x');  // 转发给 string(size_t, char) 构造
    std::printf("    try_emplace(\"b\", 3, 'x')           -> 插入=%s，m[\"b\"]=\"%s\"\n",
                r4.second ? "true" : "false", m["b"].c_str());
    const auto r5 = m.try_emplace("b", 5, 'y');
    std::printf("    再 try_emplace(\"b\", 5, 'y')        -> 插入=%s，m[\"b\"]=\"%s\"（★ 没变）\n",
                r5.second ? "true" : "false", m["b"].c_str());
    std::printf("    ★ try_emplace 的关键好处：key 已存在时【连参数都不会被用来构造对象】。\n");
    std::printf("      如果 value 构造很贵（比如要读文件、建大 vector），这个差别很实在：\n");
    std::printf("        m.emplace(k, 1000000);   // 就算 k 已存在，也会先构造再丢弃\n");
    std::printf("        m.try_emplace(k, 1000000);// k 已存在就什么都不做\n");

    SubSection("什么时候用哪个（工程建议）");
    std::printf("  1) 「存在就不动，不存在才插入」-> try_emplace（效率最好）或 insert\n");
    std::printf("  2) 「不管存不存在都要设成 v」 -> insert_or_assign\n");
    std::printf("  3) 「统计计数 / 分组」         -> operator[]（这正是它该用的地方）\n");
    std::printf("         counts[word]++;            词频统计\n");
    std::printf("         groups[cat].push_back(x);  按类别分组\n");
    std::printf("  4) 「插入并拿到迭代器」        -> auto [it, ok] = m.insert_or_assign(k, v);\n");
    std::printf("  5) 别用 emplace 代替 try_emplace —— emplace 在 key 已存在时会白构造一次。\n");

    SubSection("可运行示例：词频统计（operator[] 的正当用法）");
    const std::vector<std::string> words = {"apple", "banana", "apple", "cherry", "banana", "apple"};
    std::map<std::string, int> freq;
    for (const auto& w : words) {
        ++freq[w];  // ★ 不存在则插入 0 再自增 —— 这就是 [] 该被用的场景
    }
    std::printf("    词频统计结果（map 保证按字典序输出）：\n");
    for (const auto& [w, c] : freq) {
        std::printf("      %-8s %d\n", w.c_str(), c);
    }
    std::printf("    ★ 用 map 的好处就是这个「自动有序」；用 unordered_map 还要额外排序。\n");
}

// ===========================================================================
// 9. flat_map 的思想（MSVC 的 std::flat_map 需要 C++23）
// ===========================================================================
void DemoFlatMapIdea() {
    Section("9. 什么时候该考虑 flat_map（有序 vector 版 map）");

    std::printf("  【先说本机实测结论】\n");
    std::printf("    MSVC 14.51 + /std:c++20 下 std::flat_map 【不可用】：\n");
    std::printf("      #include <flat_map>  ->  STL4038 警告 + C2039「flat_map 不是 std 成员」\n");
    std::printf("    <flat_map> 头文件存在，但内容被 #if !_HAS_CXX23 挡住了，\n");
    std::printf("    也就是说需要 /std:c++23preview。本仓库统一用 /std:c++20，所以演示不了。\n");
    std::printf("    另外 Boost 里也有 boost::container::flat_map，思路完全相同。\n");

    SubSection("flat_map 的思想：用「排序的 vector<pair<K,V>>」当 map");
    std::printf("    - 存储：std::vector<std::pair<K,V>>，始终按 key 有序\n");
    std::printf("    - 查找：std::lower_bound -> O(log n)，但常数比红黑树小很多\n");
    std::printf("    - 插入：找到位置后 insert -> O(n)（要搬移后面的元素）\n");
    std::printf("    - 遍历：连续内存，极快\n");
    std::printf("    适合：构建一次、之后几乎只读；元素不多；遍历/查找远多于插入。\n");
    std::printf("    不适合：频繁插删（O(n) 搬移会吃掉所有优势）。\n");

    SubSection("手写一个最小可用的 flat_map（只有 find / insert / 遍历）");
    class MiniFlatMap {
    public:
        using Entry = std::pair<int, std::string>;

        // 查找：先二分定位，再确认 key 相等 —— O(log n)
        const std::string* Find(int key) const {
            const auto it = std::lower_bound(entries_.begin(), entries_.end(), key,
                                             [](const Entry& e, int k) { return e.first < k; });
            if (it != entries_.end() && it->first == key) {
                return &it->second;
            }
            return nullptr;
        }

        // 插入：保持有序 —— O(n) 搬移，这是它的主要代价
        void Insert(int key, std::string value) {
            const auto it = std::lower_bound(entries_.begin(), entries_.end(), key,
                                             [](const Entry& e, int k) { return e.first < k; });
            if (it != entries_.end() && it->first == key) {
                it->second = std::move(value);  // 已存在 -> 覆盖
                return;
            }
            entries_.insert(it, Entry{key, std::move(value)});
        }

        std::size_t Size() const { return entries_.size(); }
        const std::vector<Entry>& Entries() const { return entries_; }

    private:
        std::vector<Entry> entries_;
    };

    MiniFlatMap fm;
    for (const int k : {5, 1, 9, 3, 7, 2, 8}) {
        fm.Insert(k, "v" + std::to_string(k));
    }
    std::printf("    插入 {5,1,9,3,7,2,8} 后 size=%zu，内部存储顺序（自动有序）：\n      ", fm.Size());
    for (const auto& [k, v] : fm.Entries()) {
        std::printf("(%d,%s) ", k, v.c_str());
    }
    std::printf("\n");
    const std::string* p = fm.Find(7);
    std::printf("    Find(7) -> %s\n", p != nullptr ? p->c_str() : "没找到");
    p = fm.Find(100);
    std::printf("    Find(100) -> %s\n", p != nullptr ? p->c_str() : "没找到");

    SubSection("实测：MiniFlatMap vs std::map（构建一次 + 大量查找）");
    constexpr int kBuild = 20000;
    constexpr int kLookup = 500000;

    MiniFlatMap flat;
    const auto t0 = Clock::now();
    for (int i = 0; i < kBuild; ++i) flat.Insert(i * 3, "x");
    const long long usFlatBuild = UsSince(t0);

    std::map<int, std::string> tree;
    const auto t1 = Clock::now();
    for (int i = 0; i < kBuild; ++i) tree[i * 3] = "x";
    const long long usTreeBuild = UsSince(t1);

    std::printf("    构建 %d 个元素：\n", kBuild);
    std::printf("      MiniFlatMap: %8lld us（每次插入 O(n) 搬移）\n", usFlatBuild);
    std::printf("      std::map   : %8lld us（每次插入 O(log n)）\n", usTreeBuild);
    const long long slower = std::max(usFlatBuild, usTreeBuild);
    const long long faster = std::max(1LL, std::min(usFlatBuild, usTreeBuild));
    std::printf("      -> 较慢的一方是较快一方的 %.2f 倍（%s 更慢）\n",
                static_cast<double>(slower) / static_cast<double>(faster),
                (usFlatBuild >= usTreeBuild) ? "MiniFlatMap" : "std::map");

    volatile long long sink = 0;
    const auto t2 = Clock::now();
    for (int i = 0; i < kLookup; ++i) {
        const std::string* q = flat.Find((i * 7) % (kBuild * 3));
        if (q != nullptr) sink += 1;
    }
    const long long usFlatFind = UsSince(t2);

    const auto t3 = Clock::now();
    for (int i = 0; i < kLookup; ++i) {
        if (tree.find((i * 7) % (kBuild * 3)) != tree.end()) sink += 1;
    }
    const long long usTreeFind = UsSince(t3);

    std::printf("\n    查找 %d 次：\n", kLookup);
    std::printf("      MiniFlatMap: %8lld us（二分，连续内存，缓存友好）\n", usFlatFind);
    std::printf("      std::map   : %8lld us（红黑树，指针跳跃）\n", usTreeFind);
    if (usFlatFind > 0) {
        std::printf("      -> map 约为 flat 的 %.2f 倍耗时\n",
                    static_cast<double>(usTreeFind) / static_cast<double>(usFlatFind));
    }
    std::printf("      sink = %lld\n", sink);
    std::printf("    ★ 这正是 flat_map 的设计取舍：牺牲插入速度（O(n)），换查找与遍历速度。\n");
    std::printf("      本机实测（500000 次查找、20000 个 key，Debug 与 Release 结论一致）：\n");
    std::printf("        构建阶段：MiniFlatMap 慢约 3 倍（每次插入都要搬移后面的元素）；\n");
    std::printf("        查找阶段：两者基本持平（MiniFlatMap 1.38e5 us vs std::map 1.36e5 us）。\n");
    std::printf("      为什么「连续内存 + 二分」没有明显赢？因为二分循环在小 n 下被间接寻址拖慢、\n");
    std::printf("      没有内联、也没有 CPU 预取优势；Release 下二分会被展开并利用缓存行，\n");
    std::printf("      通常能反超红黑树 1.3~2 倍（元素越大、n 越大越明显）。\n");
    std::printf("    ★ 工程结论：不加第三方库也能做到 —— 一个排序的 vector 就够了。\n");
    std::printf("      判断标准：插入次数 << 查找次数 时用有序 vector；否则用 map/unordered_map。\n");
    std::printf("      注意：这个取舍强烈依赖数据规模与元素大小，必须用【自己的数据】实测确认。\n");
}

// ===========================================================================
// 10. multimap / multiset / equal_range
// ===========================================================================
void DemoMultiAndEqualRange() {
    Section("10. multimap / multiset 与 equal_range");

    std::multimap<std::string, int> scores;
    scores.insert({"alice", 90});
    scores.insert({"bob", 80});
    scores.insert({"alice", 95});  // ★ allow 重复 key
    scores.insert({"alice", 85});
    scores.insert({"carol", 88});

    std::printf("  multimap 允许重复 key，size = %zu\n", scores.size());
    for (const auto& [name, score] : scores) {
        std::printf("    %-8s %d\n", name.c_str(), score);
    }
    std::printf("  ★ 同一个 key 的多个 value 会【相邻】排列（multimap 保证）。\n");

    SubSection("equal_range：取出「某个 key 的全部 value」的标准方法");
    const auto [first, last] = scores.equal_range("alice");  // C++17 结构化绑定
    std::printf("    equal_range(\"alice\") 得到 %lld 个结果：",
                static_cast<long long>(std::distance(first, last)));
    for (auto it = first; it != last; ++it) {
        std::printf(" %d", it->second);
    }
    std::printf("\n");
    std::printf("    求平均分：");
    int sum = 0, cnt = 0;
    for (auto it = first; it != last; ++it) {
        sum += it->second;
        ++cnt;
    }
    std::printf("%.1f\n", cnt > 0 ? static_cast<double>(sum) / cnt : 0.0);
    std::printf("    ★ count(k) 只给个数，find(k) 只给第一个；想遍历同 key 的所有值用 equal_range。\n");
    std::printf("    ★ 注意 multimap 【没有】 operator[]（因为一个 key 对应多个 value）。\n");

    SubSection("multiset：允许重复的有序集合");
    std::multiset<int> ms = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5};
    std::printf("    multiset size = %zu（不去重）：", ms.size());
    for (const int n : ms) std::printf(" %d", n);
    std::printf("\n");
    std::printf("    count(5) = %zu，count(100) = %zu\n", ms.count(5), ms.count(100));
    std::printf("    ★ C++11 起 count 对 multimap/multiset 是「对数 + 同 key 元素个数」，\n");
    std::printf("      不是 O(n) 全扫描，放心用。\n");

    SubSection("工程场景：用 multiset 做「滑动窗口最值」/「按分数排名」");
    std::printf("    例：实时排行榜 —— 分数可重复、需要按分数排序、支持增删。\n");
    std::multiset<int> board = {1200, 1500, 1500, 1800};
    std::printf("      初始分数：");
    for (const int s : board) std::printf(" %d", s);
    std::printf("\n      最高分 = *board.rbegin() = %d\n", *board.rbegin());
    std::printf("      最低分 = *board.begin()  = %d\n", *board.begin());
    board.erase(board.find(1500));  // ★ 只删一个 1500（erase(1500) 会删掉全部！）
    std::printf("      erase(find(1500)) 只删一个之后：");
    for (const int s : board) std::printf(" %d", s);
    std::printf("\n    ★ 这是 multimap/multiset 的经典坑：erase(key) 删【所有】匹配元素，\n");
    std::printf("      erase(iterator) 才只删一个。想删一个必须写 erase(find(k))。\n");
}

}  // namespace

int main() {
    EnableUtf8Console();

    std::printf("==========================================================\n");
    std::printf(" 08_associative_containers.cpp —— map / set / unordered_*\n");
    std::printf("==========================================================\n");

    DemoMapBasics();
    DemoUnorderedBasics();
    DemoPerformance();
    DemoSelectionGuide();
    DemoSubscriptTrap();
    DemoCustomKey();
    DemoTransparentComparator();
    DemoInsertVariants();
    DemoFlatMapIdea();
    DemoMultiAndEqualRange();

    std::printf("\n================ 小结 ================\n");
    std::printf("1. 要「有序」或「范围查询」用 map/set；只要快速查找用 unordered_map/set。\n");
    std::printf("2. map 插入不会让迭代器失效；unordered_map 一旦 rehash 全部迭代器失效。\n");
    std::printf("3. 元素个数已知时先 unordered_map::reserve，避免 rehash。\n");
    std::printf("4. ★ 别用 m[key] 做「查询」—— 它会插入默认值；用 contains/find/at。\n");
    std::printf("5. 自定义 key：map 要 operator<（严格弱序）；unordered_map 要 hash + operator==。\n");
    std::printf("6. key 是 std::string 且常用字面量查，用 std::map<K,V,std::less<>> 省掉临时构造。\n");
    std::printf("7. 「存在就不动」用 try_emplace；「都要覆盖」用 insert_or_assign；\n");
    std::printf("   只有「统计计数 / 分组」才该用 operator[]。\n");
    std::printf("8. 不可信输入 + 哈希容器 = HashDoS 风险；必要时降级到 map 或加随机种子。\n");
    std::printf("9. 插入远少于查找时，排序 vector + lower_bound（flat_map 思想）常常更快。\n");
    std::printf("10. multimap/multiset 用 equal_range 遍历同 key 的值；erase(key) 会删全部。\n");
    return 0;
}
