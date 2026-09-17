// ============================================================================
// 09_container_adaptors_and_algorithms.cpp
//        容器适配器（stack/queue/priority_queue）与高频算法串讲
//
// 演示主题：
//   第一部分 容器适配器
//     1. std::stack：LIFO。底层默认 deque，为什么不是 vector
//     2. std::queue：FIFO。为什么默认底层必须是 deque（不能是 vector）
//     3. std::priority_queue：默认大顶堆；用自定义比较器做小顶堆；底层是 vector + 堆算法
//     4. 实测：priority_queue 的构建与弹出，以及「自写堆」的对照
//   第二部分 pair / tuple
//     5. std::pair 与 std::tuple：构造、std::get、std::tie、结构化绑定
//     6. 用 tuple 做「多返回值」和「字典序比较」
//   第三部分 算法串讲（按用途分组，全部可运行）
//     7. 排序族：sort / stable_sort / partial_sort / nth_element（复杂度与选择）
//     8. 二分族：lower_bound / upper_bound / equal_range / binary_search（必须已排序）
//     9. 查找与计数：find / find_if / count / count_if / min_element / max_element
//    10. 变换与累积：transform / accumulate / reduce / iota / fill / generate
//    11. 谓词判断：all_of / any_of / none_of / clamp
//    12. 复制与删除：copy / copy_if / remove_if（+erase）/ unique（+erase）/ reverse / rotate
//    13. 随机：shuffle / sample
//    14. std::optional / std::variant 作为 STL 工具箱成员的简要介绍
//
// 关键结论：
//   - 容器适配器「不提供迭代器」，因为它们要限制你的访问方式（只能从一端进/出）。
//     想遍历它们只能用 while (!s.empty()) { ... s.pop(); }，而且会清空容器。
//   - priority_queue 默认是【大顶堆】（top 是最大元素）。做小顶堆要传 std::greater<>，
//     而不是「把元素取负」那种土办法。
//   - 二分查找族（lower_bound 等）要求区间【已排序】，否则行为未定义（结果是垃圾）。
//   - sort 不稳定；要保序用 stable_sort；只要前 k 个用 partial_sort；只要第 k 名用 nth_element。
//   - std::accumulate 是顺序折叠，不能并行；std::reduce 允许并行和重排，
//     所以要求操作满足结合律与交换律（浮点加法不满足，用 reduce 求浮点和会得到不同结果）。
//
// 说明：所有性能数字都用 steady_clock 实测，标注了构建配置。
// ============================================================================

#include <algorithm>   // 所有算法
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>  // std::greater / std::less / std::plus
#include <iomanip>
#include <iostream>
#include <iterator>    // std::back_inserter / std::ostream_iterator
#include <list>
#include <map>
#include <numeric>     // std::accumulate / std::reduce / std::iota
#include <optional>
#include <queue>       // std::queue / std::priority_queue
#include <ranges>      // C++20 ranges 重载
#include <random>
#include <set>
#include <stack>
#include <string>
#include <tuple>       // std::tuple / std::tie / std::get
#include <utility>     // std::pair / std::swap
#include <variant>
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

template <class C>
void Print(const char* label, const C& c) {
    std::printf("    %-22s [", label);
    bool first = true;
    for (const auto& x : c) {
        std::printf("%s%g", first ? "" : " ", static_cast<double>(x));
        first = false;
    }
    std::printf("]\n");
}

// ===========================================================================
// 1. stack
// ===========================================================================
void DemoStack() {
    Section("1. std::stack：后进先出（LIFO）");

    std::stack<int> s;
    s.push(1);
    s.push(2);
    s.push(3);
    s.emplace(4);  // 和 push 类似，但就地构造
    std::printf("  依次 push 1,2,3 emplace 4 之后：size=%zu，top=%d\n", s.size(), s.top());

    std::printf("  弹出顺序（后进的先出）：");
    while (!s.empty()) {
        std::printf(" %d", s.top());
        s.pop();
    }
    std::printf("\n");

    std::stack<int> t;
    t.push(10);
    const int topValue = t.top();  // ★ 必须先取值
    t.pop();                       // ★ 再弹出（顺序不能反，也不能合并成一行）
    std::printf("    push / emplace : 均摊 O(1)\n");
    std::printf("    pop            : O(1)（★ 返回 void，想取值必须【先 top() 再 pop()】）\n");
    std::printf("    top            : O(1)（空栈调用是 UB，必须先判 empty）\n");
    std::printf("    empty / size   : O(1)\n");
    std::printf("    ★ 没有 begin()/end()、没有迭代器、没有 operator[]、不能遍历（这是有意的）。\n");
    std::printf("    ★ pop() 不返回值是设计决定：如果返回被删元素，异常安全就没法保证\n");
    std::printf("      （拷贝构造抛异常时元素已经弹出了，数据就丢了）。\n");
    std::printf("    实测：t.top()=%d 然后 t.pop() -> size=%zu\n", topValue, t.size());

    SubSection("底层容器：默认 deque，也可以指定 vector / list");
    std::stack<int, std::vector<int>> sv;  // 底层用 vector
    std::stack<int, std::list<int>> sl;    // 底层用 list
    std::stack<int, std::deque<int>> sd;   // 默认
    sv.push(1);
    sl.push(1);
    sd.push(1);
    std::printf("    stack<int, vector<int>> : top=%d\n", sv.top());
    std::printf("    stack<int, list<int>>   : top=%d\n", sl.top());
    std::printf("    stack<int, deque<int>>  : top=%d（默认）\n", sd.top());
    std::printf("    ★ 为什么默认是 deque 而不是 vector？\n");
    std::printf("      vector 在扩容时要搬移所有元素；deque 只在两端操作，扩容代价小。\n");
    std::printf("      但对纯 LIFO 用法，vector 版本其实更快（连续内存、无块索引表），\n");
    std::printf("      实测中很多人会显式写 stack<T, vector<T>>。\n");

    SubSection("经典用途：括号匹配、表达式求值、DFS、撤销栈");
    const char* exprs[] = {"(a[b]{c})", "(a[b){c}]", "{[()]}"};
    for (const char* e : exprs) {
        std::stack<char> st;
        bool ok = true;
        for (const char* p = e; *p != '\0' && ok; ++p) {
            if (*p == '(' || *p == '[' || *p == '{') {
                st.push(*p);
            } else if (*p == ')' || *p == ']' || *p == '}') {
                if (st.empty()) {
                    ok = false;
                    break;
                }
                const char open = st.top();
                st.pop();
                const bool match = (open == '(' && *p == ')') || (open == '[' && *p == ']') ||
                                   (open == '{' && *p == '}');
                if (!match) ok = false;
            }
        }
        if (!st.empty()) ok = false;
        std::printf("    \"%s\" 括号匹配？%s\n", e, ok ? "是" : "否");
    }
    std::printf("    ★ 这是 stack 最经典的应用 —— 用「后进先出」天然匹配「最近的未闭合括号」。\n");
}

// ===========================================================================
// 2. queue
// ===========================================================================
void DemoQueue() {
    Section("2. std::queue：先进先出（FIFO）");

    std::queue<std::string> q;
    q.push("first");
    q.push("second");
    q.emplace("third");
    std::printf("  依次入队 first/second/third：size=%zu，front=\"%s\"，back=\"%s\"\n", q.size(),
                q.front().c_str(), q.back().c_str());
    std::printf("  出队顺序：");
    while (!q.empty()) {
        std::printf(" [%s]", q.front().c_str());
        q.pop();
    }
    std::printf("\n");

    SubSection("接口与复杂度");
    std::printf("    push / emplace : 均摊 O(1)（在【尾部】入队）\n");
    std::printf("    pop            : O(1)（从【头部】出队；同样返回 void）\n");
    std::printf("    front / back   : O(1)\n");
    std::printf("    ★ 也没有迭代器。想遍历只能出队，会破坏队列。\n");
    std::printf("      需要遍历时别用 queue，直接用 deque 并自己控制 push_back/pop_front。\n");

    SubSection("★ 为什么 queue 的底层【不能】是 vector");
    std::printf("    queue 要求「尾部插入 + 头部删除」都高效。\n");
    std::printf("    vector 头部删除是 O(n)（要把后面所有元素往前挪），所以标准禁止用 vector。\n");
    std::printf("    合法底层容器必须同时提供：back() / push_back() / front() / pop_front()：\n");
    std::printf("      deque（默认，两端 O(1)）和 list（也满足，但每个节点都要 malloc）\n");
    std::queue<int, std::list<int>> ql;  // 合法：list 有 pop_front
    ql.push(1);
    ql.push(2);
    std::printf("    实测 queue<int, list<int>> 可用：front=%d\n", ql.front());

    SubSection("经典用途：BFS、任务队列、生产者-消费者缓冲、消息排队");
    // BFS 演示：在一个小网格上做层次遍历
    const int rows = 4, cols = 5;
    const char grid[4][6] = {"..#..", ".#...", ".....", "..#.."};
    std::vector<std::vector<int>> dist(rows, std::vector<int>(cols, -1));
    std::queue<std::pair<int, int>> bfs;
    bfs.push({0, 0});
    dist[0][0] = 0;
    const int dx[4] = {0, 0, 1, -1};
    const int dy[4] = {1, -1, 0, 0};
    while (!bfs.empty()) {
        const auto [r, c] = bfs.front();
        bfs.pop();
        for (int d = 0; d < 4; ++d) {
            const int nr = r + dx[d], nc = c + dy[d];
            if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
            if (grid[nr][nc] == '#') continue;
            if (dist[nr][nc] != -1) continue;
            dist[nr][nc] = dist[r][c] + 1;
            bfs.push({nr, nc});
        }
    }
    std::printf("    BFS 从 (0,0) 出发的最短距离图（-1 = 不通）：\n");
    for (int r = 0; r < rows; ++r) {
        std::printf("      ");
        for (int c = 0; c < cols; ++c) {
            if (dist[r][c] < 0) {
                std::printf("  #");
            } else {
                std::printf("%3d", dist[r][c]);
            }
        }
        std::printf("\n");
    }
    std::printf("    ★ queue 保证 BFS 按「距离递增」的顺序访问节点，这是最短路正确性的基础。\n");
}

// ===========================================================================
// 3. priority_queue
// ===========================================================================
void DemoPriorityQueue() {
    Section("3. std::priority_queue：堆（默认大顶堆）");

    std::priority_queue<int> maxHeap;
    for (const int v : {3, 1, 4, 1, 5, 9, 2, 6}) {
        maxHeap.push(v);
    }
    std::printf("  push {3,1,4,1,5,9,2,6} 之后 size=%zu，top=%d（最大元素）\n", maxHeap.size(),
                maxHeap.top());
    std::printf("  弹出顺序：");
    while (!maxHeap.empty()) {
        std::printf(" %d", maxHeap.top());
        maxHeap.pop();
    }
    std::printf("\n  ★ 默认是【大顶堆】：top() 是最大元素，弹出顺序是从大到小。\n");

    SubSection("★ 小顶堆：必须传比较器");
    // 记忆口诀：priority_queue 的第三个参数是「谁排在后面」——用 greater 就变成小顶堆。
    // 更准确的说法：Compare 是「严格弱序」，默认 less<T> 表示「大的优先级高」。
    std::priority_queue<int, std::vector<int>, std::greater<int>> minHeap;
    for (const int v : {3, 1, 4, 1, 5, 9, 2, 6}) {
        minHeap.push(v);
    }
    std::printf("  小顶堆 top=%d，弹出顺序：", minHeap.top());
    while (!minHeap.empty()) {
        std::printf(" %d", minHeap.top());
        minHeap.pop();
    }
    std::printf("\n  ★ 常见错误写法：priority_queue<int, vector<int>, greater<int>> ok, 但写成\n");
    std::printf("    priority_queue<int, greater<int>> 会编译失败 —— 第二个参数是底层容器，\n");
    std::printf("    第三个才是比较器。顺序不能省。\n");
    std::printf("  ★ 另一个土办法是「存 -x 模拟小顶堆」，但那只对整数可行，且可读性差。\n");

    SubSection("用自定义比较器排序自定义对象（按优先级）");
    struct Task {
        int priority;      // 数值越小越紧急
        std::string name;
    };
    // 自定义比较器：返回 true 表示 a 的优先级【低于】b（即 a 排在 b 后面）
    const auto cmp = [](const Task& a, const Task& b) {
        if (a.priority != b.priority) return a.priority > b.priority;  // 小数值优先
        return a.name > b.name;  // 同优先级按名字倒序（保证严格弱序）
    };
    std::priority_queue<Task, std::vector<Task>, decltype(cmp)> tasks(cmp);
    tasks.push({3, "log-cleanup"});
    tasks.push({1, "fix-crash"});
    tasks.push({2, "review-pr"});
    tasks.push({1, "answer-page"});
    std::printf("    任务队列（数字小的先执行）：\n");
    while (!tasks.empty()) {
        const Task& t = tasks.top();
        std::printf("      priority=%d  %s\n", t.priority, t.name.c_str());
        tasks.pop();
    }
    std::printf("    ★ 比较器必须给出严格弱序，否则堆的内部结构会被破坏。\n");
    std::printf("    ★ 用 lambda 时必须把 lambda 对象【传给构造函数】（decltype(cmp) 拿类型）。\n");

    SubSection("底层实现：vector + make_heap/push_heap/pop_heap");
    std::printf("    priority_queue 默认底层是 std::vector，用二叉堆维护：\n");
    std::printf("      push  : O(log n)  —— push_back + push_heap 上浮\n");
    std::printf("      pop   : O(log n)  —— pop_heap 下沉 + pop_back\n");
    std::printf("      top   : O(1)\n");
    std::printf("      建堆  : O(n)（不是 O(n log n)）\n");
    std::printf("    ★ 用 <algorithm> 的手工版本可以做到「先建堆再一次性取出全部」：\n");
    std::vector<int> data = {3, 1, 4, 1, 5, 9, 2, 6};
    std::make_heap(data.begin(), data.end());  // O(n) 建堆
    std::printf("      make_heap 之后 front = %d（堆顶）\n", data.front());
    std::sort_heap(data.begin(), data.end());  // 堆排序：反复 pop_heap
    std::printf("      sort_heap 之后：");
    for (const int v : data) std::printf(" %d", v);
    std::printf("\n    ★ 场景价值：已经有 vector 数据时，直接 make_heap 是 O(n)，\n");
    std::printf("      比逐个 push 进 priority_queue（O(n log n)）快。\n");

    SubSection("实测：逐个 push 建堆 vs make_heap 一次建堆");
    {
        constexpr int kN = 300000;
        std::vector<int> values(static_cast<std::size_t>(kN));
        std::mt19937 gen(7);
        std::uniform_int_distribution<int> dist(0, 1000000);
        for (int& v : values) v = dist(gen);

        const auto t0 = Clock::now();
        std::priority_queue<int> pq;
        for (const int v : values) pq.push(v);
        const long long usPush = UsSince(t0);

        std::vector<int> copy = values;
        const auto t1 = Clock::now();
        std::make_heap(copy.begin(), copy.end());
        const long long usHeap = UsSince(t1);

        std::printf("      %d 个元素（本机实测，仅供参考）：\n", kN);
        std::printf("        逐个 push 进 priority_queue : %8lld us（O(n log n)）\n", usPush);
        std::printf("        make_heap 一次建堆           : %8lld us（O(n)）\n", usHeap);
        if (usHeap > 0) {
            std::printf("        -> 逐个 push 约为 make_heap 的 %.1f 倍耗时\n",
                        static_cast<double>(usPush) / static_cast<double>(usHeap));
        }
        std::printf("      ★ 只看建堆：make_heap 明显更快；但如果后面还要不断 push/pop，\n");
        std::printf("        用 priority_queue 更省心（不用自己维护堆不变量）。\n");
    }

    SubSection("经典用途：Top-K、合并 K 个有序链表、Dijkstra、任务调度、定时器");
    // Top-K：用大小为 K 的小顶堆，空间 O(K)，时间 O(n log K)
    const std::vector<int> all = {42, 7, 99, 13, 58, 76, 24, 88, 5, 61};
    constexpr std::size_t kK = 3;
    std::priority_queue<int, std::vector<int>, std::greater<int>> topk;
    for (const int v : all) {
        topk.push(v);
        if (topk.size() > kK) topk.pop();  // 把最小的挤出去
    }
    std::printf("    从 %zu 个数里取最大的 %zu 个（小顶堆法，空间只需 O(K)）：", all.size(), kK);
    std::vector<int> result;
    while (!topk.empty()) {
        result.push_back(topk.top());
        topk.pop();
    }
    std::reverse(result.begin(), result.end());
    for (const int v : result) std::printf(" %d", v);
    std::printf("\n    ★ 对比全排序 O(n log n)：Top-K 用堆是 O(n log K)，K 很小时优势明显；\n");
    std::printf("      而且不需要把全部数据放进内存（流式数据也能做）。\n");
}

// ===========================================================================
// 4. pair
// ===========================================================================
void DemoPair() {
    Section("4. std::pair：两个值的组合");

    // 构造方式
    std::pair<int, std::string> p1{1, "one"};
    std::pair p2 = std::make_pair(2, std::string("two"));  // CTAD（C++17）
    std::pair p3{3.5, 'x'};                                // 自动推导 <double, char>
    std::printf("  p1 = (%d, \"%s\")\n", p1.first, p1.second.c_str());
    std::printf("  p2 = (%d, \"%s\")\n", p2.first, p2.second.c_str());
    std::printf("  p3 = (%.1f, '%c')\n", p3.first, p3.second);

    SubSection("结构化绑定（C++17）：推荐用法");
    const auto [num, word] = p1;
    std::printf("    const auto [num, word] = p1;  -> num=%d, word=\"%s\"\n", num, word.c_str());
    std::printf("    ★ 对比 C++11 的写法：int num = p1.first; std::string word = p1.second;\n");
    std::printf("      或者 std::tie(num, word) = p1;（tie 需要变量先声明好）\n");

    SubSection("std::tie：把多个值「打包」比较 / 解包");
    int a = 0;
    std::string b;
    std::tie(a, b) = p1;  // 解包到已存在的变量
    std::printf("    std::tie(a, b) = p1;  -> a=%d, b=\"%s\"\n", a, b.c_str());
    std::printf("    ★ tie 的经典用法是「按多个字段依次比较」：\n");
    const std::pair<int, std::string> x{1, "z"}, y{1, "a"};
    const bool xLess = std::tie(x.first, x.second) < std::tie(y.first, y.second);
    std::printf("      std::tie(x.first,x.second) < std::tie(y.first,y.second) = %s\n",
                xLess ? "true" : "false");
    std::printf("      （first 相等时比较 second：\"z\" > \"a\"，所以 false）\n");
    std::printf("    ★ pair 本身就支持 <，是按 (first, second) 字典序比较的 —— 这是它的隐藏福利：\n");
    std::printf("      std::sort 一个 vector<pair<int,int>> 就等于「先按 int，再按第二个」排序。\n");

    SubSection("pair 在标准库里的位置");
    std::printf("    std::map 的 value_type 就是 std::pair<const Key, T>\n");
    std::printf("    insert 返回 std::pair<iterator, bool>\n");
    std::printf("    equal_range 返回 std::pair<iterator, iterator>\n");
    std::printf("    std::minmax 返回 std::pair<const T&, const T&>\n");
    std::printf("    ★ 所以「会用 pair 的结构化绑定」直接决定了读 map 代码的舒适度。\n");

    SubSection("可运行示例：sort 一个 vector<pair>");
    std::vector<std::pair<std::string, int>> scores = {
        {"alice", 90}, {"bob", 95}, {"carol", 90}, {"dave", 85}};
    std::sort(scores.begin(), scores.end());  // 先按名字，再按分数
    std::printf("    按 pair 默认比较排序（名字升序）：\n");
    for (const auto& [name, score] : scores) {
        std::printf("      %-8s %d\n", name.c_str(), score);
    }
    std::sort(scores.begin(), scores.end(), [](const auto& l, const auto& r) {
        if (l.second != r.second) return l.second > r.second;  // 分数降序
        return l.first < r.first;                              // 同分按名字升序
    });
    std::printf("    按「分数降序、同分按名字升序」排序：\n");
    for (const auto& [name, score] : scores) {
        std::printf("      %-8s %d\n", name.c_str(), score);
    }
}

// ===========================================================================
// 5. tuple
// ===========================================================================
void DemoTuple() {
    Section("5. std::tuple：任意多个值的组合");

    std::tuple<int, std::string, double> t{1, "hello", 2.5};
    std::printf("  tuple<int,string,double> t{1,\"hello\",2.5}\n");
    std::printf("    std::get<0>(t) = %d\n", std::get<0>(t));
    std::printf("    std::get<1>(t) = \"%s\"\n", std::get<1>(t).c_str());
    std::printf("    std::get<2>(t) = %.1f\n", std::get<2>(t));
    std::printf("    std::get<double>(t) = %.1f（也支持按【类型】取，类型必须唯一）\n",
                std::get<double>(t));
    std::printf("    std::tuple_size<decltype(t)>::value = %zu（元素个数）\n",
                std::tuple_size<decltype(t)>::value);

    SubSection("结构化绑定：tuple 最舒服的用法");
    const auto [i, s, d] = t;
    std::printf("    const auto [i, s, d] = t;  -> i=%d, s=\"%s\", d=%.1f\n", i, s.c_str(), d);
    auto& [ri, rs, rd] = t;  // 引用绑定：可以修改原 tuple
    ri = 100;
    rd = 9.9;
    std::printf("    用 auto& 绑定后修改：t 变成 (%d, \"%s\", %.1f)\n", std::get<0>(t),
                std::get<1>(t).c_str(), std::get<2>(t));
    std::printf("    ★ 结构化绑定是 C++17 最重要的语法糖之一，几乎所有返回多值的代码都该用它。\n");

    SubSection("★ 用 tuple 返回多个值（工程里最实用的场景）");
    const auto Divide = [](double a, double b) -> std::tuple<bool, double, std::string> {
        if (std::fabs(b) < 1e-12) {
            return {false, 0.0, "除数为 0"};
        }
        return {true, a / b, ""};
    };
    const auto [ok1, q1, err1] = Divide(10.0, 4.0);
    std::printf("    Divide(10, 4) -> ok=%s, 商=%.4f, 错误=\"%s\"\n", ok1 ? "true" : "false", q1,
                err1.c_str());
    const auto [ok2, q2, err2] = Divide(10.0, 0.0);
    std::printf("    Divide(10, 0) -> ok=%s, 商=%.4f, 错误=\"%s\"\n", ok2 ? "true" : "false", q2,
                err2.c_str());
    std::printf("    ★ 对比「用输出参数 void f(int& out, bool& ok)」：\n");
    std::printf("      tuple 版本调用处一眼能看出返回了三个东西，而且 const 友好。\n");
    std::printf("    ★ 只有两个值用 std::pair，三个及以上用 std::tuple；\n");
    std::printf("      如果返回值语义重要（比如 ok/err/value），考虑定义一个具名 struct 更清晰。\n");

    SubSection("tuple 的比较：字典序，且可直接用于排序");
    std::vector<std::tuple<int, int, std::string>> rows = {
        {2, 1, "b"}, {1, 3, "a"}, {1, 1, "c"}, {1, 1, "a"}};
    std::sort(rows.begin(), rows.end());  // 按 (int,int,string) 字典序
    std::printf("    sort 之后的 (int,int,string) 列表：\n");
    for (const auto& [p, q, r] : rows) {
        std::printf("      (%d, %d, \"%s\")\n", p, q, r.c_str());
    }
    std::printf("    ★ tuple 的 < 是逐元素字典序比较，语义明确，不需要手写比较器。\n");
    std::printf("      这正好适合「多级排序键」：把排序键按优先级放进 tuple 即可。\n");

    SubSection("std::tie 与 std::make_tuple");
    int v1 = 0;
    std::string v2;
    std::tie(v1, v2) = std::make_tuple(7, std::string("seven"));
    std::printf("    tie(v1, v2) = make_tuple(7, \"seven\") -> v1=%d, v2=\"%s\"\n", v1, v2.c_str());
    std::printf("    std::ignore 可以忽略某个位置：std::tie(v1, std::ignore) = ...\n");

    SubSection("C++17 的 apply：把 tuple 展开成函数实参");
    const auto Add3 = [](int a, int b, int c) { return a + b + c; };
    const std::tuple<int, int, int> argT{1, 2, 3};
    std::printf("    std::apply(Add3, argT) = %d\n", std::apply(Add3, argT));
    std::printf("    ★ 泛型代码（比如日志、序列化、反射模拟）里非常有用。\n");

    SubSection("代价提醒");
    std::printf("    tuple 不是「免费的」：\n");
    std::printf("      - 元素按声明顺序排列，可能有对齐填充（顺序影响大小）；\n");
    std::printf("      - get<N> 是编译期下标，但调试器里看不到字段名，可读性差；\n");
    std::printf("      - 字段一多（> 4 个）就应该定义具名 struct，\n");
    std::printf("        否则三个月后你自己都记不清 get<3> 是什么。\n");
}

// ===========================================================================
// 6. 排序族算法
// ===========================================================================
void DemoSortFamily() {
    Section("6. 排序族：sort / stable_sort / partial_sort / nth_element");

    const std::vector<int> base = {5, 2, 9, 1, 7, 3, 8, 4, 6, 0, 5, 2};

    SubSection("std::sort：最常用，不稳定，O(n log n)");
    std::vector<int> v = base;
    std::sort(v.begin(), v.end());
    Print("sort 升序", v);
    std::sort(v.begin(), v.end(), std::greater<int>());
    Print("sort 降序(greater)", v);
    std::sort(v.begin(), v.end(), [](int a, int b) { return std::abs(a - 5) < std::abs(b - 5); });
    Print("按「离 5 的距离」排序", v);
    std::printf("    复杂度：O(n log n)；实现是 introsort（快排 + 堆排 + 插入排序），保证最坏 O(n log n)。\n");
    std::printf("    ★ 不稳定：相等元素的相对顺序不保证。\n");

    SubSection("std::stable_sort：稳定，保序");
    struct Player {
        std::string name;
        int score;
    };
    std::vector<Player> players = {{"alice", 90}, {"bob", 90}, {"carol", 85}, {"dave", 90}};
    std::printf("    原始顺序：");
    for (const auto& p : players) std::printf("%s(%d) ", p.name.c_str(), p.score);
    std::printf("\n");
    std::stable_sort(players.begin(), players.end(),
                     [](const Player& a, const Player& b) { return a.score > b.score; });
    std::printf("    stable_sort 按分数降序：");
    for (const auto& p : players) std::printf("%s(%d) ", p.name.c_str(), p.score);
    std::printf("\n    ★ 同分的 alice/bob/dave 保持了原来的相对顺序。\n");
    std::printf("    复杂度：有足够内存时 O(n log n)，否则 O(n log^2 n)。\n");
    std::printf("    ★ 多级排序的两种做法：\n");
    std::printf("      (1) 一次比较器里比较所有字段（推荐，最快）；\n");
    std::printf("      (2) 从次要键到主要键【依次】stable_sort（稳定排序的经典技巧）。\n");

    SubSection("std::partial_sort：只要前 k 个有序");
    std::vector<int> p = base;
    std::partial_sort(p.begin(), p.begin() + 5, p.end());  // 只保证前 5 个是最小的 5 个且有序
    std::printf("    partial_sort 前 5 个：");
    for (int i = 0; i < 5; ++i) std::printf(" %d", p[i]);
    std::printf("（后面 %zu 个不保证顺序）\n", p.size() - 5);
    std::printf("    复杂度：O(n log k)。k << n 时明显快于全排序。\n");
    std::printf("    ★ 与 priority_queue Top-K 的区别：partial_sort 需要全部数据在内存且可随机访问，\n");
    std::printf("      但它直接给出有序的前 k 个；堆方案适合流式数据。\n");

    SubSection("std::nth_element：只要「第 k 名」");
    std::vector<int> n = base;
    const std::size_t kth = 5;
    std::nth_element(n.begin(), n.begin() + static_cast<std::ptrdiff_t>(kth), n.end());
    std::printf("    nth_element(k=%zu) 之后：", kth);
    for (const int x : n) std::printf(" %d", x);
    std::printf("\n    第 %zu 个元素（下标 %zu）是 %d —— 它左边全部 <= 它，右边全部 >= 它\n", kth + 1, kth,
                n[kth]);
    std::printf("    但左右两边内部的顺序【不保证】。\n");
    std::printf("    复杂度：平均 O(n)（比排序的 O(n log n) 快）。\n");
    std::printf("    ★ 用途：求中位数、求第 k 大、求百分位数 —— 不需要完整排序。\n");
    std::vector<int> med = base;
    std::nth_element(med.begin(), med.begin() + static_cast<std::ptrdiff_t>(med.size() / 2),
                     med.end());
    std::printf("    用 nth_element 求中位数：%d\n", med[med.size() / 2]);

    SubSection("实测：全排序 vs partial_sort vs nth_element（30 万个元素）");
    {
        constexpr int kN = 300000;
        std::mt19937 gen(11);
        std::uniform_int_distribution<int> dist(0, 10000000);
        std::vector<int> data(static_cast<std::size_t>(kN));
        for (int& x : data) x = dist(gen);

        std::vector<int> c1 = data;
        const auto t0 = Clock::now();
        std::sort(c1.begin(), c1.end());
        const long long usSort = UsSince(t0);

        std::vector<int> c2 = data;
        const auto t1 = Clock::now();
        std::partial_sort(c2.begin(), c2.begin() + 10, c2.end());
        const long long usPartial = UsSince(t1);

        std::vector<int> c3 = data;
        const auto t2 = Clock::now();
        std::nth_element(c3.begin(), c3.begin() + 10, c3.end());
        const long long usNth = UsSince(t2);

        std::printf("      %d 个元素（本机实测，仅供参考）：\n", kN);
        std::printf("        std::sort（全排序）        : %8lld us\n", usSort);
        std::printf("        std::partial_sort（前 10） : %8lld us\n", usPartial);
        std::printf("        std::nth_element（第 10）  : %8lld us\n", usNth);
        std::printf("      ★ 只需要前 k 个或第 k 个时，不要用全排序：\n");
        std::printf("        算法库提供了语义更精确、也更快的选择。\n");
        std::printf("      （本机实测两个构建配置的结论一致：partial_sort 约 8e2 us、\n");
        std::printf("        nth_element 约 4e3 us，都远低于 sort 的 5e4 us；k 越小优势越大。）\n");
    }
}

// ===========================================================================
// 7. 二分族算法
// ===========================================================================
void DemoBinarySearch() {
    Section("7. 二分族：lower_bound / upper_bound / equal_range / binary_search");

    // ★ 前提：区间必须已经排序。未排序时这些算法的结果是未定义的（垃圾），不会报错！
    const std::vector<int> v = {10, 20, 20, 20, 30, 40, 50};
    std::printf("  有序数组（含重复的 20）：");
    for (const int x : v) std::printf(" %d", x);
    std::printf("\n");

    SubSection("语义对照（这是最容易记混的一组）");
    std::printf("    binary_search(b,e,v)      -> bool：在不在（★ 只回答存在性）\n");
    std::printf("    lower_bound(b,e,v)        -> 第一个 >= v 的位置\n");
    std::printf("    upper_bound(b,e,v)        -> 第一个 >  v 的位置\n");
    std::printf("    equal_range(b,e,v)        -> pair(lower_bound, upper_bound)\n");
    std::printf("    ★ 记忆：lower 是「下界」，含等于；upper 是「上界」，不含等于。\n");

    std::printf("\n  实测（在 [10,20,20,20,30,40,50] 上）：\n");
    const int queries[] = {5, 10, 20, 25, 30, 55};
    for (const int q : queries) {
        const auto lb = std::lower_bound(v.begin(), v.end(), q);
        const auto ub = std::upper_bound(v.begin(), v.end(), q);
        const bool found = std::binary_search(v.begin(), v.end(), q);
        const auto lbIdx = std::distance(v.begin(), lb);
        const auto ubIdx = std::distance(v.begin(), ub);
        const auto cnt = std::distance(lb, ub);

        std::printf("    查 %2d: binary_search=%-5s lower_bound=下标%lld upper_bound=下标%lld 个数=%lld",
                    q, found ? "true" : "false", static_cast<long long>(lbIdx),
                    static_cast<long long>(ubIdx), static_cast<long long>(cnt));
        if (lb != v.end()) {
            std::printf(" lb值=%d", *lb);
        }
        std::printf("\n");
    }

    SubSection("三种「在不在」写法的区别");
    std::printf("    std::binary_search(...)              : 最省事，只给 bool\n");
    std::printf("    std::find(...) != end()              : 线性 O(n)，无需排序\n");
    std::printf("    lower_bound(...) != end() && *it==v   : 可以顺便拿到位置\n");
    std::printf("    ★ 已经排序的数据用二分 O(log n)；未排序的数据 std::find 是 O(n)。\n");
    std::printf("      如果只查一次，排序 + 二分的 O(n log n) 反而更慢 —— 要按使用次数决定。\n");

    SubSection("lower_bound / upper_bound 的常见错误");
    std::printf("    错误 1：在未排序的区间上调用 —— 结果是垃圾，而且不会报错。\n");
    std::vector<int> unsorted = {30, 10, 50, 20};
    const bool wrong = std::binary_search(unsorted.begin(), unsorted.end(), 10);
    std::printf("      对 {30,10,50,20} 调 binary_search(10) 得到 %s（实际 10 存在！）\n",
                wrong ? "true" : "false");
    std::printf("      这是最危险的一类 bug：结果错了但程序不崩。\n");
    std::printf("    错误 2：比较器与排序时用的比较器不一致。\n");
    std::vector<int> desc = {50, 40, 30, 20, 10};  // 降序
    const auto d1 = std::lower_bound(desc.begin(), desc.end(), 30);  // 默认 less -> 错
    const auto d2 = std::lower_bound(desc.begin(), desc.end(), 30, std::greater<int>());  // 对
    std::printf("      降序数组上用默认 lower_bound: *it = %d（错）\n", *d1);
    std::printf("      降序数组上用 std::greater<>:  *it = %d（对）\n", *d2);
    std::printf("      ★ 排序和查找必须使用【同一个】比较关系。\n");

    SubSection("实用封装：区间计数 / 前驱后继");
    const auto CountInRange = [](const std::vector<int>& data, int lo, int hi) {
        const auto first = std::lower_bound(data.begin(), data.end(), lo);
        const auto last = std::upper_bound(data.begin(), data.end(), hi);
        return std::distance(first, last);
    };
    std::printf("    [20, 30] 区间内的元素个数 = %lld\n",
                static_cast<long long>(CountInRange(v, 20, 30)));
    std::printf("    [21, 29] 区间内的元素个数 = %lld\n",
                static_cast<long long>(CountInRange(v, 21, 29)));
    std::printf("    ★ 这是「按分数段统计」「按时间窗口聚合」的标准写法，复杂度 O(log n)。\n");

    const auto LowerNeighbor = [](const std::vector<int>& data, int q) -> std::optional<int> {
        const auto it = std::lower_bound(data.begin(), data.end(), q);
        if (it == data.begin()) return std::nullopt;
        return *std::prev(it);
    };
    const auto UpperNeighbor = [](const std::vector<int>& data, int q) -> std::optional<int> {
        const auto it = std::upper_bound(data.begin(), data.end(), q);
        if (it == data.end()) return std::nullopt;
        return *it;
    };
    const auto ln = LowerNeighbor(v, 25);
    const auto un = UpperNeighbor(v, 25);
    std::printf("    25 的前驱（<25 的最大值）= %s\n", ln ? std::to_string(*ln).c_str() : "不存在");
    std::printf("    25 的后继（>25 的最小值）= %s\n", un ? std::to_string(*un).c_str() : "不存在");
}

// ===========================================================================
// 8. 查找与计数
// ===========================================================================
void DemoFindCount() {
    Section("8. 查找与计数：find / find_if / count / count_if / min_element / max_element");

    const std::vector<int> v = {5, 2, 9, 1, 7, 3, 8, 4, 6, 0};

    SubSection("find：线性查找第一个匹配");
    const auto it = std::find(v.begin(), v.end(), 7);
    std::printf("    find(7) -> %s（下标 %lld）\n", (it != v.end()) ? "找到" : "未找到",
                static_cast<long long>(std::distance(v.begin(), it)));
    std::printf("    find(100) -> %s\n", (std::find(v.begin(), v.end(), 100) != v.end()) ? "找到" : "未找到");
    std::printf("    复杂度：O(n)。★ 数据有序时用 lower_bound 的 O(log n)。\n");

    SubSection("find_if：按条件查找");
    const auto evenIt = std::find_if(v.begin(), v.end(), [](int x) { return x % 2 == 0; });
    std::printf("    find_if(是偶数) -> %d（第一个偶数）\n", *evenIt);
    const auto bigIt = std::find_if(v.begin(), v.end(), [](int x) { return x > 5; });
    std::printf("    find_if(x > 5)  -> %d（第一个大于 5 的）\n", *bigIt);
    std::printf("    ★ 想找【最后一个】满足条件的：用 find_if + 反向迭代器：\n");
    const auto lastEven = std::find_if(v.rbegin(), v.rend(), [](int x) { return x % 2 == 0; });
    std::printf("      最后一个偶数 = %d\n", *lastEven);

    SubSection("find_first_of / adjacent_find / search");
    const std::vector<int> needle = {9, 1};
    const auto sub = std::search(v.begin(), v.end(), needle.begin(), needle.end());
    std::printf("    search({9,1})          -> 下标 %lld（查找【子序列】，不是排序意义上的子串）\n",
                static_cast<long long>(std::distance(v.begin(), sub)));
    const std::vector<int> targets = {8, 9};
    const auto anyOfThem = std::find_first_of(v.begin(), v.end(), targets.begin(), targets.end());
    std::printf("    find_first_of({8,9})   -> %d（任一字符首次出现）\n", *anyOfThem);
    const auto adj = std::adjacent_find(v.begin(), v.end());
    std::printf("    adjacent_find(v)       -> %s（找第一对相邻的重复元素）\n",
                (adj == v.end()) ? "没有相邻重复" : "有");

    SubSection("count / count_if");
    const std::vector<int> dup = {1, 2, 2, 3, 3, 3, 4, 4, 4, 4};
    std::printf("    count(dup, 3)                       = %lld\n",
                static_cast<long long>(std::count(dup.begin(), dup.end(), 3)));
    std::printf("    count_if(dup, x > 2)                = %lld\n",
                static_cast<long long>(
                    std::count_if(dup.begin(), dup.end(), [](int x) { return x > 2; })));
    std::printf("    复杂度：都是 O(n)。\n");
    std::printf("    ★ 对 map/set 想「判断存在」用 count(k) != 0 或 contains(k)（O(log n)）；\n");
    std::printf("      对 vector 想「判断存在」用 std::find，不要把 count 当 find 用\n");
    std::printf("      （count 会扫完整个区间，find 找到就能停）。\n");

    SubSection("min_element / max_element / minmax_element");
    const auto [minIt, maxIt] = std::minmax_element(v.begin(), v.end());  // ★ 一次遍历同时得到两个
    std::printf("    minmax_element -> 最小值 %d（下标 %lld），最大值 %d（下标 %lld）\n", *minIt,
                static_cast<long long>(std::distance(v.begin(), minIt)), *maxIt,
                static_cast<long long>(std::distance(v.begin(), maxIt)));
    std::printf("    ★ 用 minmax_element 而不是分别调 min_element + max_element：\n");
    std::printf("      前者只遍历 1 次（约 3n/2 次比较），后者要遍历 2 次。\n");
    const auto byAbs = std::max_element(v.begin(), v.end(), [](int a, int b) {
        return std::abs(a - 5) < std::abs(b - 5);  // 自定义「最大」的标准
    });
    std::printf("    自定义比较器求「离 5 最远」的元素 = %d\n", *byAbs);

    SubSection("查找的复杂度总表");
    std::printf("    算法                        | 要求      | 复杂度\n");
    std::printf("    ----------------------------|-----------|--------------\n");
    std::printf("    std::find                   | 无        | O(n)\n");
    std::printf("    std::find_if                | 无        | O(n)\n");
    std::printf("    std::count / count_if       | 无        | O(n)\n");
    std::printf("    std::min_element            | 无        | O(n)\n");
    std::printf("    std::minmax_element         | 无        | O(n)，约 3n/2 次比较\n");
    std::printf("    std::binary_search          | 【已排序】| O(log n)\n");
    std::printf("    std::lower_bound            | 【已排序】| O(log n)\n");
    std::printf("    std::adjacent_find          | 无        | O(n)\n");
    std::printf("    std::search                 | 无        | O(n*m)\n");
    std::printf("    set/map::find               | 红黑树    | O(log n)\n");
    std::printf("    unordered_map::find         | 哈希      | 平均 O(1)，最坏 O(n)\n");
}

// ===========================================================================
// 9. 变换与累积
// ===========================================================================
void DemoTransformAccumulate() {
    Section("9. 变换与累积：transform / accumulate / reduce / iota / fill / generate");

    SubSection("std::transform：一对一变换（写回或写新容器）");
    std::vector<int> v = {1, 2, 3, 4, 5};
    std::vector<int> squared;
    squared.reserve(v.size());
    std::transform(v.begin(), v.end(), std::back_inserter(squared), [](int x) { return x * x; });
    std::printf("    transform(平方) -> ");
    for (const int x : squared) std::printf(" %d", x);
    std::printf("\n");

    std::transform(v.begin(), v.end(), v.begin(), [](int x) { return x * 10; });  // 原地变换
    std::printf("    transform(原地 x10) -> ");
    for (const int x : v) std::printf(" %d", x);
    std::printf("\n");
    std::printf("    ★ 原地变换时「输出区间 == 输入区间」是允许的（逐个写不会互相干扰）。\n");

    // 双输入版本：两个区间逐元素合并
    const std::vector<int> a = {1, 2, 3};
    const std::vector<int> b = {10, 20, 30};
    std::vector<int> sum;
    sum.reserve(a.size());
    std::transform(a.begin(), a.end(), b.begin(), std::back_inserter(sum), std::plus<int>());
    std::printf("    transform(a, b, plus) -> ");
    for (const int x : sum) std::printf(" %d", x);
    std::printf("\n    ★ 双输入版本要求第二个区间至少一样长（它不做边界检查！）。\n");

    SubSection("std::accumulate：顺序折叠");
    const std::vector<int> nums = {1, 2, 3, 4, 5};
    const int total = std::accumulate(nums.begin(), nums.end(), 0);  // ★ 初值 0 决定了类型！
    std::printf("    accumulate(求和, 初值 0)          = %d\n", total);
    const double avg = std::accumulate(nums.begin(), nums.end(), 0.0) / nums.size();
    std::printf("    accumulate(初值 0.0) 再除以个数    = %.3f\n", avg);
    const int product = std::accumulate(nums.begin(), nums.end(), 1, std::multiplies<int>());
    std::printf("    accumulate(初值 1, multiplies)     = %d（连乘）\n", product);
    const std::string joined = std::accumulate(
        nums.begin(), nums.end(), std::string("结果: "),
        [](const std::string& acc, int x) { return acc + std::to_string(x) + " "; });
    std::printf("    accumulate(拼字符串)               = \"%s\"\n", joined.c_str());

    std::printf("\n    ★★ 初学者最容易踩的坑：初值类型决定了整个计算类型\n");
    const std::vector<int> big = {1000000000, 1000000000, 1000000000};
    const int overflow = std::accumulate(big.begin(), big.end(), 0);  // int 溢出！
    const long long safe = std::accumulate(big.begin(), big.end(), 0LL);  // 用 long long
    std::printf("      accumulate(..., 0)   = %d   <- ★ int 溢出，结果错\n", overflow);
    std::printf("      accumulate(..., 0LL) = %lld <- 正确\n", safe);
    std::printf("    所以求和大数组时，初值一定要写成 0LL / 0.0 这种明确类型。\n");

    SubSection("std::reduce：允许并行（C++17）");
    const long long r1 = std::reduce(nums.begin(), nums.end(), 0LL);
    std::printf("    reduce(求和) = %lld\n", r1);
    const long long r2 = std::reduce(nums.begin(), nums.end(), 0LL, std::plus<long long>());
    std::printf("    reduce(求和, plus) = %lld\n", r2);
    std::printf("    ★ accumulate vs reduce 的差别：\n");
    std::printf("      accumulate 严格从左到右顺序折叠 —— 结果是确定的；\n");
    std::printf("      reduce 允许实现重排、分组、并行 —— 所以要求运算满足【结合律和交换律】。\n");
    std::printf("    ★★ 浮点求和不满足结合律，所以：\n");
    const std::vector<double> fm = {0.1, 0.2, 0.3, 0.4, 0.5};
    std::printf("      accumulate(浮点) = %.17g\n", std::accumulate(fm.begin(), fm.end(), 0.0));
    std::printf("      reduce(浮点)     = %.17g\n", std::reduce(fm.begin(), fm.end(), 0.0));
    std::printf("      两者可能不同，而且 reduce 的结果在不同编译器/线程数下都可能变。\n");
    std::printf("      对精度敏感的场景（金融、科学计算）用 accumulate；\n");
    std::printf("      只关心「大致和」且数据量大时用 reduce 换并行加速。\n");

    SubSection("iota：填充递增序列");
    std::vector<int> seq(10);
    std::iota(seq.begin(), seq.end(), 100);  // 从 100 开始递增
    std::printf("    iota(seq, 起始 100) -> ");
    for (const int x : seq) std::printf(" %d", x);
    std::printf("\n    ★ 经典用途：生成索引数组再排序，实现「按值排索引」：\n");
    const std::vector<int> values = {30, 10, 50, 20, 40};
    std::vector<int> idx(values.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(),
              [&values](int i, int j) { return values[static_cast<std::size_t>(i)] <
                                        values[static_cast<std::size_t>(j)]; });
    std::printf("      values = ");
    for (const int x : values) std::printf(" %d", x);
    std::printf("\n      按值排序后的下标 = ");
    for (const int i : idx) std::printf(" %d", i);
    std::printf("\n      -> 即 values 的升序下标序列（30 是第 2 小，所以第一个是 1）\n");

    SubSection("fill / generate：填充");
    std::vector<int> f(5);
    std::fill(f.begin(), f.end(), 7);
    std::printf("    fill(7) -> ");
    for (const int x : f) std::printf(" %d", x);
    std::printf("\n");
    int counter = 0;
    std::generate(f.begin(), f.end(), [&counter] { return counter += 10; });
    std::printf("    generate(每次 +10) -> ");
    for (const int x : f) std::printf(" %d", x);
    std::printf("\n    ★ fill_n / generate_n 是「写 n 个」的版本，配合 back_inserter 更好用。\n");
    std::printf("    ★ fill 对 POD 数组等价于 memset 但类型安全；对非平凡类型 memset 是 UB。\n");
}

// ===========================================================================
// 10. 谓词判断与 clamp
// ===========================================================================
void DemoPredicates() {
    Section("10. all_of / any_of / none_of / clamp");

    const std::vector<int> v = {2, 4, 6, 8, 10};
    const std::vector<int> mixed = {2, 4, 5, 8, 10};

    SubSection("三个谓词判断（都短路求值）");
    const auto isEven = [](int x) { return x % 2 == 0; };
    std::printf("    v     = {2,4,6,8,10}（全偶）\n");
    std::printf("      all_of(isEven)  = %s\n", std::all_of(v.begin(), v.end(), isEven) ? "true" : "false");
    std::printf("      any_of(isEven)  = %s\n", std::any_of(v.begin(), v.end(), isEven) ? "true" : "false");
    std::printf("      none_of(isEven) = %s\n", std::none_of(v.begin(), v.end(), isEven) ? "true" : "false");
    std::printf("    mixed = {2,4,5,8,10}（有一个奇数）\n");
    std::printf("      all_of(isEven)  = %s\n",
                std::all_of(mixed.begin(), mixed.end(), isEven) ? "true" : "false");
    std::printf("      any_of(isEven)  = %s\n",
                std::any_of(mixed.begin(), mixed.end(), isEven) ? "true" : "false");
    std::printf("      none_of(isEven) = %s\n",
                std::none_of(mixed.begin(), mixed.end(), isEven) ? "true" : "false");
    std::printf("    ★ 三者都【短路】：all_of 遇到第一个不符合就返回 false，不会扫完整个区间。\n");
    std::printf("    ★ 空区间时：all_of 返回 true（空真）、any_of 返回 false、none_of 返回 true。\n");
    std::vector<int> emptyVec;
    std::printf("      实测空 vector：all_of=%s any_of=%s none_of=%s\n",
                std::all_of(emptyVec.begin(), emptyVec.end(), isEven) ? "true" : "false",
                std::any_of(emptyVec.begin(), emptyVec.end(), isEven) ? "true" : "false",
                std::none_of(emptyVec.begin(), emptyVec.end(), isEven) ? "true" : "false");

    SubSection("工程用法：输入校验 / 配置检查");
    const auto IsValidId = [](const std::string& s) {
        return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) {
                   return std::isdigit(static_cast<unsigned char>(c)) != 0;
               });
    };
    std::printf("    IsValidId(\"12345\") = %s\n", IsValidId("12345") ? "true" : "false");
    std::printf("    IsValidId(\"12a45\") = %s\n", IsValidId("12a45") ? "true" : "false");
    std::printf("    IsValidId(\"\")      = %s（★ 空串必须单独判，否则 all_of 会返回 true）\n",
                IsValidId("") ? "true" : "false");

    SubSection("std::clamp（C++17）：把值夹到区间内");
    std::printf("    clamp(150, 0, 100) = %d（血量上限）\n", std::clamp(150, 0, 100));
    std::printf("    clamp(-20, 0, 100) = %d（血量下限）\n", std::clamp(-20, 0, 100));
    std::printf("    clamp( 42, 0, 100) = %d\n", std::clamp(42, 0, 100));
    std::printf("    clamp(3.7, 0.0, 1.0) = %.1f（也支持浮点）\n", std::clamp(3.7, 0.0, 1.0));
    std::printf("    自定义比较器：clamp(v, lo, hi, [](int a, int b){ return a > b; })\n");
    std::printf("    ★ 注意：clamp 要求 lo <= hi，否则是 UB（不会报错！）。\n");
    std::printf("    ★ 它是纯计算，不修改原值：hp = std::clamp(hp, 0, 100);\n");
    std::printf("    ★ 有 NaN 的浮点数据不要用 std::clamp/std::min/std::max，用 std::fmin/std::fmax。\n");
}

// ===========================================================================
// 11. 复制与删除
// ===========================================================================
void DemoCopyRemove() {
    Section("11. copy / copy_if / remove_if（+erase）/ unique（+erase）/ reverse / rotate");

    const std::vector<int> src = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    SubSection("std::copy：区间复制");
    std::vector<int> dst(src.size());
    std::copy(src.begin(), src.end(), dst.begin());  // ★ 目标必须有足够空间！
    std::printf("    copy 到预先分配好的 vector：size=%zu\n", dst.size());

    std::vector<int> dst2;
    std::copy(src.begin(), src.end(), std::back_inserter(dst2));  // ★ 自动扩容，推荐
    std::printf("    copy + back_inserter：size=%zu\n", dst2.size());
    std::printf("    ★ copy 不做边界检查：目标空间不足就是缓冲区溢出（UB）。\n");
    std::printf("      不确定大小时用 back_inserter / inserter；确定时才用 dst.begin()。\n");

    SubSection("std::copy_if：只复制满足条件的");
    std::vector<int> evens;
    std::copy_if(src.begin(), src.end(), std::back_inserter(evens), [](int x) { return x % 2 == 0; });
    std::printf("    copy_if(偶数) -> ");
    for (const int x : evens) std::printf(" %d", x);
    std::printf("\n");

    SubSection("std::remove / remove_if：★ 它不删元素，只是把要保留的前移");
    std::vector<int> v = src;
    const auto newEnd = std::remove_if(v.begin(), v.end(), [](int x) { return x % 3 == 0; });
    std::printf("    remove_if(x%%3==0) 之后物理内容：");
    for (const int x : v) std::printf(" %d", x);
    std::printf("（size 仍是 %zu）\n", v.size());
    std::printf("    返回的新逻辑结尾 = 下标 %lld\n", static_cast<long long>(std::distance(v.begin(), newEnd)));
    v.erase(newEnd, v.end());
    std::printf("    再 erase 之后：");
    for (const int x : v) std::printf(" %d", x);
    std::printf("（size=%zu）\n", v.size());
    std::printf("    ★★ 这是 STL 最著名的「反直觉」设计：remove 的名字骗人，它不改变 size。\n");
    std::printf("      原因：算法只操作迭代器区间，不知道容器是什么，所以无法调用容器的 erase。\n");
    std::printf("      这就是 erase-remove 惯用法必须两步的原因。\n");
    std::printf("    ★ C++20 起可以一步搞定：std::erase_if(v, pred) 或 std::erase(v, value)。\n");

    SubSection("std::unique：去掉【相邻】重复（所以要先排序）");
    std::vector<int> dup = {3, 1, 3, 2, 1, 3, 3, 2};
    std::printf("    原始：");
    for (const int x : dup) std::printf(" %d", x);
    std::printf("\n    直接 unique（只去相邻重复）：");
    std::vector<int> t1 = dup;
    t1.erase(std::unique(t1.begin(), t1.end()), t1.end());
    for (const int x : t1) std::printf(" %d", x);
    std::printf("  <- 没达到去重效果\n");
    std::printf("    先 sort 再 unique：");
    std::vector<int> t2 = dup;
    std::sort(t2.begin(), t2.end());
    t2.erase(std::unique(t2.begin(), t2.end()), t2.end());
    for (const int x : t2) std::printf(" %d", x);
    std::printf("  <- 正确\n");
    std::printf("    ★ 想「去重但保持原顺序」：用 unordered_set 记录见过的元素 + remove_if：\n");
    std::vector<int> t3;
    std::set<int> seen;
    for (const int x : dup) {
        if (seen.insert(x).second) t3.push_back(x);  // 第一次见到才保留
    }
    std::printf("      保持顺序去重结果：");
    for (const int x : t3) std::printf(" %d", x);
    std::printf("\n");

    SubSection("std::reverse / std::rotate");
    std::vector<int> r = {1, 2, 3, 4, 5};
    std::reverse(r.begin(), r.end());
    std::printf("    reverse -> ");
    for (const int x : r) std::printf(" %d", x);
    std::printf("\n");
    std::vector<int> rot = {1, 2, 3, 4, 5};
    std::rotate(rot.begin(), rot.begin() + 2, rot.end());  // 把前 2 个转到后面
    std::printf("    rotate(begin, begin+2, end) -> ");
    for (const int x : rot) std::printf(" %d", x);
    std::printf("（左旋 2 位）\n");
    std::printf("    ★ rotate 的用途：循环缓冲区、轮转调度、数组循环移位，O(n) 原地完成。\n");
    std::printf("      想右旋 1 位就是 rotate(begin, end-1, end)。\n");

    SubSection("更省事的写法：C++20 ranges 版本（不需要写 begin/end）");
    struct Student {
        std::string name;
        int age;
    };
    std::vector<Student> students = {{"alice", 22}, {"bob", 19}, {"carol", 25}, {"dave", 20}};
    // ★ ranges 版本：直接传容器，比较器也不需要用 decltype 拿类型
    std::ranges::sort(students, std::greater{}, &Student::age);  // 第三个参数是「投影」
    std::printf("    std::ranges::sort(students, std::greater{}, &Student::age)（按年龄降序）：\n");
    for (const auto& st : students) {
        std::printf("      %-8s %d\n", st.name.c_str(), st.age);
    }
    // ★ ranges::find 直接传容器和值
    const auto foundSt = std::ranges::find(students, std::string("carol"), &Student::name);
    std::printf("    std::ranges::find(students, \"carol\", &Student::name) -> %s（年龄 %d）\n",
                (foundSt != students.end()) ? "找到" : "没找到", foundSt->age);
    // ★ ranges::copy_if + back_inserter
    std::vector<Student> adults;
    std::ranges::copy_if(students, std::back_inserter(adults),
                         [](const Student& s) { return s.age >= 20; });
    std::printf("    std::ranges::copy_if(age >= 20) -> %zu 人\n", adults.size());

    std::printf("\n    老迭代器版本 vs ranges 版本对照：\n");
    std::printf("      std::sort(v.begin(), v.end());               -> std::ranges::sort(v);\n");
    std::printf("      std::sort(v.begin(), v.end(), std::greater<int>())\n");
    std::printf("                                                   -> std::ranges::sort(v, std::greater{});\n");
    std::printf("      std::sort(p.begin(), p.end(), [](auto&a, auto&b){return a.age<b.age;})\n");
    std::printf("                                                   -> std::ranges::sort(p, {}, &Person::age);\n");
    std::printf("      std::find(v.begin(), v.end(), 42)            -> std::ranges::find(v, 42);\n");
    std::printf("    ★ 优点：更短、不易写错区间、支持【投影】（projection）——\n");
    std::printf("      投影让你「按成员排序/查找」时不必手写 lambda。\n");
    std::printf("    ★ 本机 MSVC 14.51 实测可用（__cpp_lib_ranges=%ld）。\n",
                static_cast<long>(__cpp_lib_ranges));
    std::printf("    ★ 缺点：和老的迭代器版本混用时，重载解析偶尔会有意外（尤其是传 lambda 时\n");
    std::printf("      可能被当成「比较器」而不是「投影」），团队内最好统一风格。\n");
}

// ===========================================================================
// 12. 随机
// ===========================================================================
void DemoShuffleSample() {
    Section("12. std::shuffle 与 std::sample");

    std::mt19937 gen(std::random_device{}());

    SubSection("std::shuffle：就地打乱");
    std::vector<int> deck(10);
    std::iota(deck.begin(), deck.end(), 1);
    std::printf("    洗牌前：");
    for (const int x : deck) std::printf(" %d", x);
    std::printf("\n");
    std::shuffle(deck.begin(), deck.end(), gen);
    std::printf("    洗牌后：");
    for (const int x : deck) std::printf(" %d", x);
    std::printf("\n    ★ 必须传随机数引擎！不要再用 std::random_shuffle（C++17 已删除）：\n");
    std::printf("      std::random_shuffle 用 rand()，随机性和线程安全性都不可靠。\n");
    std::printf("    ★ 想要「可复现的洗牌」（比如测试、存档）：用固定种子的 mt19937。\n");
    std::mt19937 fixed(12345);
    std::vector<int> deck2(10);
    std::iota(deck2.begin(), deck2.end(), 1);
    std::shuffle(deck2.begin(), deck2.end(), fixed);
    std::printf("      固定种子 12345 的洗牌结果：");
    for (const int x : deck2) std::printf(" %d", x);
    std::printf("\n");

    SubSection("std::sample：无放回抽样（不需要先打乱整个容器）");
    const std::vector<std::string> pool = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J"};
    std::vector<std::string> picked;
    std::sample(pool.begin(), pool.end(), std::back_inserter(picked), 3, gen);
    std::printf("    从 10 个里抽 3 个：");
    for (const auto& s : picked) std::printf(" %s", s.c_str());
    std::printf("\n    ★ 复杂度 O(n)（蓄水池抽样），不需要 shuffle 整个容器，\n");
    std::printf("      也不要求随机访问迭代器 —— 对 std::list 或流式输入同样适用。\n");

    SubSection("注意：discrete_distribution 做加权抽样");
    std::discrete_distribution<int> weighted({1.0, 1.0, 3.0});  // 第 3 个元素权重是 3 倍
    int counts[3] = {};
    for (int i = 0; i < 60000; ++i) {
        ++counts[weighted(gen)];
    }
    std::printf("    权重 {1,1,3} 抽 60000 次的实际分布：%d / %d / %d\n", counts[0], counts[1],
                counts[2]);
    std::printf("    理论比例应是 1:1:3，即约 12000 : 12000 : 36000。\n");
    std::printf("    ★ 用途：抽奖权重、负载均衡、遗传算法轮盘赌选择。\n");
}

// ===========================================================================
// 13. optional / variant
// ===========================================================================
void DemoOptionalVariant() {
    Section("13. std::optional / std::variant：STL 工具箱成员");

    SubSection("std::optional<T>：可能有、也可能没有的值");
    const auto SafeDivide = [](double a, double b) -> std::optional<double> {
        if (std::fabs(b) < 1e-12) return std::nullopt;  // 明确表达「没有结果」
        return a / b;
    };
    const auto r1 = SafeDivide(10.0, 4.0);
    std::printf("    SafeDivide(10, 4) 有值？%s", r1.has_value() ? "true" : "false");
    if (r1) {  // ★ optional 可以当 bool 用
        std::printf("，值 = %.4f，也可以用 *r1 = %.4f 或 r1.value() = %.4f\n", *r1, *r1, r1.value());
    }
    const auto r2 = SafeDivide(10.0, 0.0);
    std::printf("    SafeDivide(10, 0) 有值？%s\n", r2.has_value() ? "true" : "false");
    std::printf("    r2.value_or(-1.0) = %.1f（无值时给默认值）\n", r2.value_or(-1.0));
    try {
        (void)r2.value();  // ★ 无值时 value() 抛 std::bad_optional_access
    } catch (const std::bad_optional_access&) {
        std::printf("    r2.value() 抛出 std::bad_optional_access（★ 用 *r2 则不会检查，是 UB）\n");
    }

    std::printf("\n    ★ optional 解决什么问题：\n");
    std::printf("      1) 替代「用 -1 / 空字符串 / 特殊值表示「没有」」这种土办法；\n");
    std::printf("      2) 替代输出参数 + bool 返回值的写法：\n");
    std::printf("           bool Find(int k, T& out);            // 老写法\n");
    std::printf("           std::optional<T> Find(int k);         // 新写法，调用处更清楚\n");
    std::printf("      3) 延迟构造：optional<ExpensiveType> 可以后置构造，省掉「先默认构造再赋值」。\n");
    std::printf("    ★ 代价：optional<T> 的大小是 sizeof(T) 加一个 bool（有对齐填充），\n");
    std::printf("      实测 sizeof(std::optional<double>) = %zu，sizeof(double) = %zu。\n",
                sizeof(std::optional<double>), sizeof(double));
    std::printf("    ★ 注意 optional<T&> 不是合法的（引用没有「空」表示），要返回引用用指针。\n");

    SubSection("std::variant<A, B, C>：类型安全的联合体");
    using Value = std::variant<int, double, std::string>;
    std::vector<Value> values = {42, 3.14, std::string("hello"), 7};

    const auto Describe = [](const Value& v) -> std::string {
        // std::visit 是访问 variant 的标准方式，会在编译期穷举所有分支
        return std::visit(
            [](const auto& x) -> std::string {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, int>) {
                    return "int: " + std::to_string(x);
                } else if constexpr (std::is_same_v<T, double>) {
                    return "double: " + std::to_string(x);
                } else {
                    return "string: " + x;
                }
            },
            v);
    };
    for (const auto& v : values) {
        std::printf("    %s（当前持有的类型下标 %zu）\n", Describe(v).c_str(), v.index());
    }

    std::printf("\n    ★ variant 相对 union 的三个改进：\n");
    std::printf("      1) 知道当前持有哪个类型（index() / holds_alternative<T>()）；\n");
    std::printf("      2) 会正确调用成员的构造/析构（union 不会）；\n");
    std::printf("      3) std::visit 强制你处理【所有】分支，漏一个编译不过。\n");
    std::printf("    ★ 它是「替代继承 + 虚函数」的一种选择（值语义、无堆分配、无虚表），\n");
    std::printf("      适合类型集合封闭的场景（比如 JSON 值、词法单元、协议字段）。\n");
    std::printf("    ★ 访问失败会抛 std::bad_variant_access：\n");
    try {
        const Value iv = 42;
        (void)std::get<std::string>(iv);  // 类型不对
    } catch (const std::bad_variant_access&) {
        std::printf("      实测 std::get<std::string>(variant 里是 int) 抛出 std::bad_variant_access\n");
    }
    std::printf("      不想抛异常就用 std::get_if<T>(&v)（返回指针，失败返回 nullptr）。\n");

    SubSection("三者（optional / variant / any）怎么选");
    std::printf("    optional<T>    : 「有或没有 T」——两种情况\n");
    std::printf("    variant<A,B,C>: 「是 A 或 B 或 C 之一」——多种互斥情况，类型编译期已知\n");
    std::printf("    any            : 「任意类型」——类型运行期才知道（用 type() 查询，有堆分配风险）\n");
    std::printf("    ★ 能用 optional 就不要用 variant，能用 variant 就不要用 any：\n");
    std::printf("      表达力越弱、约束越强，代码越不容易出错。\n");
}

}  // namespace

int main() {
    EnableUtf8Console();

    std::printf("==========================================================\n");
    std::printf(" 09_container_adaptors_and_algorithms.cpp\n");
    std::printf(" 容器适配器 stack/queue/priority_queue + pair/tuple + 算法串讲\n");
    std::printf("==========================================================\n");
    std::printf("\n########## 第一部分：容器适配器 ##########\n");

    DemoStack();
    DemoQueue();
    DemoPriorityQueue();

    std::printf("\n########## 第二部分：pair / tuple ##########\n");

    DemoPair();
    DemoTuple();

    std::printf("\n########## 第三部分：算法串讲 ##########\n");

    DemoSortFamily();
    DemoBinarySearch();
    DemoFindCount();
    DemoTransformAccumulate();
    DemoPredicates();
    DemoCopyRemove();
    DemoShuffleSample();
    DemoOptionalVariant();

    std::printf("\n================ 小结 ================\n");
    std::printf("1. stack/queue/priority_queue 没有迭代器；用 while(!c.empty()) { top(); pop(); } 遍历（会清空）。\n");
    std::printf("2. priority_queue 默认大顶堆；小顶堆写 priority_queue<T, vector<T>, greater<T>>。\n");
    std::printf("3. 已有数据要建堆，make_heap 是 O(n)，比逐个 push 的 O(n log n) 快。\n");
    std::printf("4. pair 支持字典序比较；tuple 适合多返回值和多级排序键，配合结构化绑定最好用。\n");
    std::printf("5. 排序选择：全排序 sort；保序 stable_sort；前 k 个 partial_sort；第 k 个 nth_element。\n");
    std::printf("6. 二分族要求区间已排序，且排序和查找必须用同一比较关系，否则结果是垃圾。\n");
    std::printf("7. accumulate 的初值类型决定结果类型（0 vs 0LL 是经典溢出 bug）；reduce 可并行但要求可结合。\n");
    std::printf("8. remove/remove_if 不删元素，只是前移，必须配合 erase（或用 C++20 的 std::erase_if）。\n");
    std::printf("9. unique 只去相邻重复，通常先 sort；要保序去重就用 set/unordered_set 记录。\n");
    std::printf("10. optional 表达「可能有值」，variant 表达「多种互斥类型」，两者配合 visit 是值语义的利器。\n");
    return 0;
}
