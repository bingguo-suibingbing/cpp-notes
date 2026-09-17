// =============================================================================
//  m4_container.cpp —— 容器：Boost 最有说服力的优势区
//
//  结论速览：
//    * multi_index_container：STL 完全没有。同一份数据同时按多个键索引
//      （每个索引还能各自决定"唯一/非唯一""有序/哈希"）。
//      用 STL 实现只能维护 2~3 个并行容器 + 手工同步，极易出 bug。
//    * circular_buffer：STL 没有环形缓冲。
//    * bimap：双向映射，STL 只能两个 map 互指。
//    * container::flat_map：连续内存的 map，小数据量比 std::map 快得多。
// =============================================================================
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/bimap.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include "demo_api.h"
#include "demo_common.h"

using namespace demo;
namespace bmi = boost::multi_index;

namespace {

struct Employee {
    int         id;
    std::string name;
    std::string dept;
    double      salary;
};

}  // namespace

void demo_m4_container() {
    title("M4. 容器：multi_index_container 是 STL 至今的空白");

    // ===================================================================
    item("1) 用 STL 做同一份数据、多个键索引 —— 手写版有多脆");
    {
        struct CmpName {
            bool operator()(const Employee* a, const Employee* b) const { return a->name < b->name; }
        };
        std::map<int, Employee>   by_id;
        std::set<const Employee*, CmpName> by_name;

        auto insert = [&](Employee e) {
            auto [it, ok] = by_id.emplace(e.id, std::move(e));
            if (ok) by_name.insert(&it->second);      // 必须手工同步第二个索引
        };
        insert({3, "Wang", "RD", 20000});
        insert({1, "Li",   "RD", 18000});
        insert({2, "Zhang","HR", 15000});

        std::ostringstream os;
        os << "  [STL  ] 两份容器各存一份指针，已插入 " << by_id.size()
           << " 条，按名字遍历: ";
        for (auto* p : by_name) os << p->name << " ";
        line(os.str());
        side("STL  ", "致命问题：Employee 的 key 一旦被改（比如改名），");
        side("STL  ", "by_name 集合里的顺序就悄悄失效了，编译器不会提醒你。");
        side("STL  ", "insert / erase / modify 每处都要记得同步两份容器 —— 迟早漏。");
    }

    // ===================================================================
    item("2) Boost.MultiIndex：一份存储，三个索引，改 key 由库帮你重排");
    {
        using namespace bmi;
        using Records = multi_index_container<
            Employee,
            indexed_by<
                ordered_unique<tag<struct ById>,   member<Employee, int,         &Employee::id>>,
                ordered_non_unique<tag<struct ByName>, member<Employee, std::string, &Employee::name>>,
                hashed_non_unique<tag<struct ByDept>,  member<Employee, std::string, &Employee::dept>>
            >>;

        Records recs;
        recs.insert({3, "Wang",  "RD", 20000});
        recs.insert({1, "Li",    "RD", 18000});
        recs.insert({2, "Zhang", "HR", 15000});
        recs.insert({4, "Zhao",  "RD", 22000});

        // --- 用 id 索引（有序、唯一）
        auto& id_idx = recs.get<ById>();
        std::ostringstream os;
        os << "  [Boost] 按 id 有序: ";
        for (auto& e : id_idx) os << e.id << ":" << e.name << " ";
        line(os.str());

        // --- 用 name 索引（有序、可重名）
        auto& name_idx = recs.get<ByName>();
        os.str("");
        os << "  [Boost] 按 name 有序: ";
        for (auto& e : name_idx) os << e.name << " ";
        line(os.str());

        // --- 用 dept 索引（哈希、可重复）
        auto& dept_idx = recs.get<ByDept>();
        auto range = dept_idx.equal_range("RD");
        int n = (int)std::distance(range.first, range.second);
        side("Boost", "哈希索引 equal_range(\"RD\") 直接命中 " + std::to_string(n) + " 人，O(1) 定位");

        // --- 关键优势：修改会被索引的字段，不用手工同步
        auto it = id_idx.find(1);
        id_idx.modify(it, [](Employee& e) { e.name = "AAA-Li"; });
        os.str("");
        os << "  [Boost] modify 改名后，按 name 索引自动重排: ";
        for (auto& e : name_idx) os << e.name << " ";
        line(os.str());
        side("Boost", "modify() 会先摘出元素、改完再插回所有相关索引，绝不留下脏索引");

        line("");
        line("  这就是 multi_index_container 的价值：把多索引一致性从程序员的责任");
        line("  变成容器的内部不变量。金融行情、游戏实体、缓存表都是它的主战场。");
    }

    // ===================================================================
    item("3) circular_buffer：定长环形队列（STL 没有）");
    {
        boost::circular_buffer<int> cb(4);   // 容量 4 的环形缓冲
        for (int i = 1; i <= 6; ++i) cb.push_back(i);   // 1..6，前两个被挤掉

        std::ostringstream os;
        os << "  [Boost] 容量 4，压入 1..6 后内容: ";
        for (int v : cb) os << v << " ";
        os << " | size=" << cb.size() << " full=" << (cb.full() ? "true" : "false");
        line(os.str());
        side("STL  ", "没有对应物：要自己用 std::vector + 取模下标 + 手写 push_back 覆盖逻辑，");
        side("STL  ", "还要处理迭代器跨环形边界的遍历（新手在这里 100% 写出 bug）。");
        line("  典型用途：日志滚动窗口、串口/网络接收缓冲、最近 N 次采样、游戏输入缓冲。");
    }

    // ===================================================================
    item("4) bimap：双向映射");
    {
        // 左边是"英文名 -> 中文名"，同时支持反查
        using Dict = boost::bimap<std::string, std::string>;
        Dict d;
        d.insert({"apple", "苹果"});
        d.insert({"boost", "增强库"});
        d.insert({"cache", "缓存"});

        std::ostringstream os;
        os << "  [Boost] 正向查 apple -> " << d.left.at("apple");
        line(os.str());
        os.str("");
        os << "  [Boost] 反向查 缓存  -> " << d.right.at("缓存");
        line(os.str());
        side("STL  ", "std::map<std::string,std::string> 只支持正向；反查要么线性扫，");
        side("STL  ", "要么再维护一份 map<std::string,std::string> 并保证两边同步。");
        line("  bimap 还能配 unordered_set_of / multiset_of / list_of，灵活度远超手写。");
    }

    // ===================================================================
    item("5) container::flat_map：连续内存的 map");
    {
        constexpr int N = 30000;
        boost::container::flat_map<int, int> fm;
        std::map<int, int>                   sm;

        double t_flat_build = time_ms([&] {
            for (int i = 0; i < N; ++i) fm.insert({i, i * 2});   // 顺序插入
        });
        double t_std_build = time_ms([&] {
            for (int i = 0; i < N; ++i) sm.insert({i, i * 2});
        });

        volatile long long sink = 0;
        double t_flat_lookup = time_ms([&] {
            for (int i = 0; i < N * 20; ++i) sink += fm.find(i % N)->second;
        });
        double t_std_lookup = time_ms([&] {
            for (int i = 0; i < N * 20; ++i) sink += sm.find(i % N)->second;
        });

        char buf[160];
        side("Boost", "flat_map 构建 " + std::to_string(N) + " 条: " + ms(t_flat_build));
        side("STL  ", "std::map 构建 " + std::to_string(N) + " 条: " + ms(t_std_build));
        std::snprintf(buf, sizeof buf, "  ==> flat_map 构建快 %.1f 倍（顺序插入时还能用 hinted insert 更快）",
                      t_std_build / t_flat_build);
        line(buf);
        side("Boost", "flat_map 查找 x" + std::to_string(N * 20) + " 次: " + ms(t_flat_lookup));
        side("STL  ", "std::map  查找 x" + std::to_string(N * 20) + " 次: " + ms(t_std_lookup));
        std::snprintf(buf, sizeof buf, "  ==> flat_map 查找快 %.1f 倍（连续内存 = 缓存友好）",
                      t_std_lookup / t_flat_lookup);
        line(buf);
        line("  代价：flat_map 插入/删除是 O(n)（要搬数组），元素很大或频繁增删时别用。");
        line("  另外 flat_map 的迭代器在插入后会失效，而 std::map 的迭代器稳定 —— 这是硬取舍。");
        (void)sink;
        (void)t_flat_build;
    }

    // ===================================================================
    line();
    line("小结(Boost 赢在): multi_index / circular_buffer / bimap / flat_map 这四样");
    line("  STL 里一个都没有。要用，就只有 Boost 或者自己写（自己写基本都会踩坑）。");
}
