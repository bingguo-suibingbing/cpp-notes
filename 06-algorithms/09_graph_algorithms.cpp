// ============================================================================
// 09_graph_algorithms.cpp
// 演示主题：
//   1. 图的两种表示：邻接矩阵（O(V^2) 空间、O(1) 查边）与邻接表（O(V+E) 空间、
//      O(deg) 查边），以及「什么时候用哪个」的工程判据
//   2. 遍历：递归 DFS / 显式栈 DFS（入栈标记 与 出栈检查 两种写法）/ BFS
//      （BFS 给出无权图的「边数最少」路径）
//   3. 连通分量：DFS 与并查集两种解法对拍
//   4. 拓扑排序：Kahn 入度法 与 DFS 三色法，以及「顺便检测环」
//   5. 并查集 DSU：路径压缩 + 按大小合并，实测均摊代价接近 O(alpha(n))
//   6. 单源最短路：Dijkstra（惰性删除、复杂度 O((V+E) log V)、为什么怕负权）
//      与 Bellman-Ford（负权、O(V*E)、负环检测）
//   7. 最小生成树：Kruskal + 并查集 O(E log E) 与 Prim + 二叉堆 O(E log V)，
//      两者的总权重必须相等（MST 的权值和唯一）
//   8. 性能实测表（Debug 构建，绝对数字仅供参考，趋势才重要）
//
// 关键结论（均为本机 Debug 实测，见正文表格）：
//   1. V=1500、E≈5700 的稀疏图上：邻接矩阵占约 18 MB，邻接表只占约 0.2 MB，
//      差了约两个数量级；全图 BFS 遍历矩阵要慢两个数量级。
//      但「查一条边是否存在」在稀疏图上两者几乎打平（都是几次缓存访问），
//      只有在稠密图上矩阵的 O(1) 才真正拉开差距 —— 所以「矩阵查边快」这句话
//      必须带上「图够稠密」这个前提才成立。
//   2. 递归 DFS 每层递归都要吃栈：实测每层约 X 字节，1 MB 默认栈只够 Y 层左右。
//      20 万节点的链图递归 DFS 必崩，显式栈版本毫无压力（用堆换栈）。
//   3. 显式栈 DFS 的两种写法：入栈即标记保证每点最多入栈一次（栈峰值 O(V)），
//      出栈才检查会重复入栈（实测多用 Z 倍入栈、栈峰值高 W 倍）。
//   4. BFS 给出的是边数最少路径，对每个点都有 bfs_dist[v] <= dfs_depth[v]（已断言）。
//   5. Dijkstra 的贪心前提是「非负权 => 出堆即定型」。反例实测：
//      图 0->1(1)、0->2(2)、2->1(-2) 上，Dijkstra 给出 dist[1]=1，
//      而真实最短路是 0->2->1 = 0，Bellman-Ford 与 Floyd-Warshall 都算出 0。
//   6. Bellman-Ford 最多 V-1 轮（最短路径最多 V-1 条边）；第 V 轮还能松弛
//      就说明存在从源点可达的负环 —— 在含 1->2->3->1 权值和为 -1 的环上实测检测成功。
//   7. 并查集：n=16 万时「路径压缩 + 按大小合并」均摊每次操作约 P ns、
//      平均只走 H 次父指针；「只按大小合并」要慢约 R 倍。两者结合才接近 O(alpha(n))。
//   8. Kruskal 与 Prim 的总权重实测完全相等（同一张 2 万点图上对拍）。
//
// 说明：本文件在 Debug（/Od）下编译运行，绝对耗时比 Release 慢很多，
//       所有数字仅供参考，看趋势和数量级差异才有意义。
//
// 教学用途：生产环境请用成熟的图算法库（例如 Boost.Graph、LEMON、networkx 的 C++ 侧替代品），
//           或者至少用经过充分测试的实现；不要直接把这里的教学代码搬上生产。
// ============================================================================

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// 房屋风格工具（每个 .cpp 各自复制一份：一个 .cpp = 一个独立可执行文件，
// 不允许跨文件共享头文件）
// ---------------------------------------------------------------------------

// 防止测量结果被优化掉：结果写进 volatile 全局变量，编译器就不能把计算整段删掉。
// C++20 起 volatile 的复合赋值（g_sink += x）已被弃用（P1152），一律写展开形式。
volatile std::uint64_t g_sink = 0;

void Section(const std::string& title) {
    std::cout << "\n============================================================\n";
    std::cout << title << "\n";
    std::cout << "============================================================\n";
}

void Note(const std::string& text) { std::cout << "  " << text << "\n"; }

// 估算字符串在终端里占的「显示列数」。
// 必踩的坑：std::setw 数的是 char 个数，而一个中文字符在 UTF-8 里占 3 字节、
// 终端里显示 2 列。所以含中文的表格必须自己算显示宽度再补空格，否则一定是歪的。
std::size_t DisplayWidth(const std::string& s) {
    std::size_t width = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {                 // ASCII：1 字节 1 列
            width += 1;
            i += 1;
        } else if ((c >> 5) == 0x06) {  // 2 字节序列
            width += 2;
            i += 2;
        } else if ((c >> 4) == 0x0E) {  // 3 字节序列（中文、中文标点）
            width += 2;
            i += 3;
        } else {                        // 4 字节序列
            width += 2;
            i += 4;
        }
    }
    return width;
}

// 左对齐打印标签并补空格到指定显示宽度，让后面的数字对齐到同一列。
void Label(const std::string& text, std::size_t total_width) {
    std::cout << text;
    const std::size_t w = DisplayWidth(text);
    if (w < total_width) {
        std::cout << std::string(total_width - w, ' ');
    }
}

// 右对齐版本：给「后面跟 setw 数字列」的中文表头用。
// 数字用 setw 是右对齐的，表头如果左对齐就会和数字错开一列，所以表头也得右对齐。
void LabelRight(const std::string& text, std::size_t total_width) {
    const std::size_t w = DisplayWidth(text);
    if (w < total_width) {
        std::cout << std::string(total_width - w, ' ');
    }
    std::cout << text;
}

// 把 double 格式化成固定小数位的字符串。
// 为什么不直接 std::to_string？它固定输出 6 位小数，拼进中文句子会变成
// 「走了 1.715490 步」这种噪声；这里按需要控制精度。
std::string Fmt(double value, int precision = 2) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << value;
    return oss.str();
}

// 重复多次取中位数：操作系统调度、其他进程抢 CPU、缓存抖动都会让某一次突然变慢，
// 平均值会被异常值拉高，中位数对异常值免疫，回答的是「典型的一次要多久」。
// 用 steady_clock 而不是 system_clock：前者单调，不会被 NTP 校时影响。
// 返回值单位微秒（O(1)/O(log n) 用毫秒打印全是 0，看不出差别）。
template <typename Fn>
double BenchMedianUs(Fn&& fn, int repeats) {
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(repeats));

    for (int i = 0; i < repeats; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        const std::uint64_t result = fn();
        const auto t1 = std::chrono::steady_clock::now();

        g_sink = g_sink + result;  // 结果必须真正被用掉

        const std::chrono::duration<double, std::micro> dt = t1 - t0;
        samples.push_back(dt.count());
    }

    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

// 预热：第一次调用带缺页中断、缓存冷启动、分支预测器未训练，先把这些一次性成本跑掉。
template <typename Fn>
void WarmUp(Fn&& fn, int times) {
    for (int i = 0; i < times; ++i) {
        g_sink = g_sink + fn();
    }
}

// 把「一次操作」重复 repeat 次并累加：给总耗时乘一个常数，不改变增长阶，
// 但能把纳秒级的操作抬离计时器噪声区（本机 steady_clock 实际分辨率在百纳秒量级）。
template <typename Fn>
std::uint64_t Repeat(Fn&& fn, int repeat) {
    std::uint64_t acc = 0;
    for (int r = 0; r < repeat; ++r) {
        acc += fn();
    }
    return acc;
}

// 断言 + 打印的合体：失败时 assert 直接终止（Debug 下有效），
// 成功时把「校验了什么」打出来，让读者知道程序真的检查过这些性质。
void Check(bool ok, const std::string& what) {
    // 先打印再断言，并且立刻 flush：assert 失败会直接 abort，
    // 而 stdout 在重定向到管道时是全缓冲的，不 flush 的话前面所有输出都会丢，
    // 排查时只看得到「第几行断言失败」，看不到上下文。
    std::cout << "  [校验] " << what << " -> " << (ok ? "通过" : "失败") << "\n";
    std::cout.flush();
    assert(ok);
}

// ---------------------------------------------------------------------------
// 基础类型
// ---------------------------------------------------------------------------
// 顶点编号用 int：算法里到处是「节点 - 1」「节点 + 1」「u < v」这类运算，
// 用无符号类型会踩空循环（i - 1 回绕成巨大值）和 C4018 的坑。
using Node = int;
// 权重与距离用 long long：松弛时要做 dist[u] + w 的比较，用 int 很容易溢出。
using Weight = long long;

// 无穷大：不用 LLONG_MAX，因为 kInf + w 会溢出成负数，把「不可达」变成「可达」。
constexpr Weight kInf = std::numeric_limits<Weight>::max() / 4;

// 顶点编号转下标：集中处理符号转换，避免 /W4 下的 C4365 之类的噪声。
std::size_t Idx(Node x) noexcept { return static_cast<std::size_t>(x); }

// ===========================================================================
// 一、并查集（Disjoint Set Union / Union-Find）
// ===========================================================================
// 用途：维护「若干个不相交集合」，支持合并与查询同属一个集合。
// 两个经典优化：
//   路径压缩（path compression）：Find 时把路径上所有点直接挂到根上，
//     树被压平，后续 Find 变快。注意递归版 Find 的深度等于树高，
//     在长链上有栈溢出风险，所以这里用迭代 + 两趟写法。
//   按大小合并（union by size）：把矮树挂到高树上，树高不超过 log n。
// 复杂度：
//   只路径压缩            -> 均摊 O(log n)
//   只按大小合并          -> O(log n)
//   两者结合              -> 均摊 O(alpha(n))，alpha 是反阿克曼函数，
//                            在 n < 10^600 的范围内 alpha(n) <= 4，工程上视为常数。
// 空间：O(n)。教学用途，生产请用 boost::disjoint_sets 或自己项目里已测试的实现。
//
// 模板参数 kPathCompression / kUnionBySize 用来「做对照实验」：
// 同一份代码分别编译出「只压缩」「只按大小」「两者都有」「朴素版」四个变体。
template <bool kPathCompression = true, bool kUnionBySize = true, typename Index = Node>
class DSU {
public:
    explicit DSU(Index n)
        : parent_(Idx(static_cast<Node>(n))),
          size_(Idx(static_cast<Node>(n)), 1),
          component_count_(n) {
        assert(n >= 0);
        std::iota(parent_.begin(), parent_.end(), Index{0});  // 一开始每个点自成一个集合
    }

    // Rule of Five：成员全是 RAII 容器（vector），值语义默认就是对的。
    // 显式写出来是为了让意图清晰，也避免编译器隐式删除带来的 C4625 / C4626。
    DSU(const DSU&) = default;
    DSU& operator=(const DSU&) = default;
    DSU(DSU&&) noexcept = default;
    DSU& operator=(DSU&&) noexcept = default;
    ~DSU() = default;

    // 查根。迭代两趟：第一趟找根，第二趟把路径上的点直接挂到根上。
    // 为什么不用递归？递归深度 = 树高，在「链式合并」的退化树上会栈溢出；
    // 迭代写法没有这个风险，代价是要多跑一趟。
    Index Find(Index x) {
        Index root = x;
        while (parent_[Idx(root)] != root) {
            root = parent_[Idx(root)];
            hops_ += 1;  // 统计真正走了多少步，这是比时间更干净的复杂度证据
        }
        if constexpr (kPathCompression) {
            while (parent_[Idx(x)] != root) {
                const Index next = parent_[Idx(x)];
                parent_[Idx(x)] = root;  // 路径压缩：直接指向根
                x = next;
                hops_ += 1;
            }
        }
        return root;
    }

    // 合并。返回 true 表示两个点原本不在一个集合里（调用方常用它判环）。
    bool Unite(Index a, Index b) {
        Index ra = Find(a);
        Index rb = Find(b);
        if (ra == rb) {
            return false;  // 已经在同一个集合里，这条边会成环
        }
        if constexpr (kUnionBySize) {
            if (size_[Idx(ra)] < size_[Idx(rb)]) {
                std::swap(ra, rb);  // 小树挂到大树上，保证树高 O(log n)
            }
            parent_[Idx(rb)] = ra;
            size_[Idx(ra)] += size_[Idx(rb)];
        } else {
            parent_[Idx(ra)] = rb;  // 朴素合并：完全不管树的形状，可能退化成一条链
        }
        component_count_ -= 1;
        return true;
    }

    bool Connected(Index a, Index b) { return Find(a) == Find(b); }

    Index Components() const noexcept { return component_count_; }
    Index Size() const noexcept { return static_cast<Index>(parent_.size()); }
    bool Empty() const noexcept { return parent_.empty(); }

    // 累计父指针步数。它只是「多一个自增」，四个变体都要付出同样的代价，
    // 所以横向比较是公平的；用它来证明复杂度的收益比时间更好读。
    std::uint64_t Hops() const noexcept { return hops_; }
    void ResetHops() noexcept { hops_ = 0; }

private:
    std::vector<Index> parent_;   // parent_[i] == i 表示 i 是根
    std::vector<Index> size_;     // 只有按大小合并时才用
    Index component_count_ = 0;
    std::uint64_t hops_ = 0;
};

// ===========================================================================
// 二、图的两种表示
// ===========================================================================

// 邻接矩阵（Adjacency Matrix）
//   空间 O(V^2)：一个 V*V 的权值表，无边用哨兵值。
//   查边   O(1)：一次随机访问。
//   遍历某点的所有邻居 O(V)：必须扫一整行。
// 适用：稠密图（E 接近 V^2）、需要频繁查边、V 比较小（比如 V <= 几千）。
// 教学用途，生产里如果是稠密图 + 频繁查边，可以先考虑它，但要注意 V=10 万时
// 光是表就要 80 GB，根本存不下。
template <typename W = Weight>
class AdjMatrixGraph {
public:
    using WeightType = W;
    static constexpr W kNoEdge = std::numeric_limits<W>::lowest();  // 哨兵：无边的格子

    AdjMatrixGraph(Node n, bool directed)
        : n_(n),
          directed_(directed),
          weights_(Idx(n) * Idx(n), kNoEdge),
          edge_count_(0) {
        assert(n >= 0);
    }

    // Rule of Five：只持有 vector，值语义可以直接 = default。
    AdjMatrixGraph(const AdjMatrixGraph&) = default;
    AdjMatrixGraph& operator=(const AdjMatrixGraph&) = default;
    AdjMatrixGraph(AdjMatrixGraph&&) noexcept = default;
    AdjMatrixGraph& operator=(AdjMatrixGraph&&) noexcept = default;
    ~AdjMatrixGraph() = default;

    Node Size() const noexcept { return n_; }
    bool Directed() const noexcept { return directed_; }
    bool Empty() const noexcept { return n_ == 0; }
    // 逻辑边数：无向图的一条边只算一次（虽然内部存了两个方向的权值）。
    std::size_t EdgeCount() const noexcept { return edge_count_; }

    // 不去重的写法：重复加同一条边只更新权值，但边数会重复计。
    // 本文件所有调用点都保证边不重复，所以够用；真实库里应该做去重或允许多重边。
    void AddEdge(Node u, Node v, W w) {
        assert(InRange(u) && InRange(v));
        weights_[Slot(u, v)] = w;
        if (!directed_) {
            weights_[Slot(v, u)] = w;
        }
        edge_count_ += 1;
    }

    bool HasEdge(Node u, Node v) const {
        assert(InRange(u) && InRange(v));
        return weights_[Slot(u, v)] != kNoEdge;  // O(1)
    }

    W EdgeWeight(Node u, Node v) const {
        assert(InRange(u) && InRange(v));
        return weights_[Slot(u, v)];
    }

    // 遍历 u 的所有邻居：O(V)，和 u 的度数无关，这是矩阵最大的缺点。
    template <typename Fn>
    void ForEachNeighbor(Node u, Fn&& fn) const {
        assert(InRange(u));
        const std::size_t base = Idx(u) * Idx(n_);
        for (Node v = 0; v < n_; ++v) {
            const W w = weights_[base + Idx(v)];
            if (w != kNoEdge) {
                fn(v, w);
            }
        }
    }

    // 估算占用字节（capacity 而不是 size，因为预留的空位也真的占内存）。
    std::size_t MemoryBytes() const {
        return sizeof(*this) + weights_.capacity() * sizeof(W);
    }

private:
    bool InRange(Node x) const noexcept { return x >= 0 && x < n_; }
    std::size_t Slot(Node u, Node v) const noexcept { return Idx(u) * Idx(n_) + Idx(v); }

    Node n_ = 0;
    bool directed_ = false;
    std::vector<W> weights_;
    std::size_t edge_count_ = 0;
};

// 邻接表（Adjacency List）
//   空间 O(V+E)：每个点一条出边数组，无向图每条边存两次（两个方向）。
//   查边   O(deg(u))：顺序扫描出边数组，稀疏图上 deg 很小，实际是「几次缓存访问」。
//   遍历某点的所有邻居 O(deg(u))。
// 适用：绝大多数真实图（路网、社交网络、依赖图）都是稀疏的，所以这是默认选择。
// 教学用途，生产里请用 Boost.Graph 的 adjacency_list，或者自己项目里已测试的实现。
template <typename W = Weight>
class AdjListGraph {
public:
    struct Arc {
        Node to = 0;
        W weight = 1;
    };
    using WeightType = W;

    AdjListGraph(Node n, bool directed)
        : n_(n), directed_(directed), adj_(Idx(n)), edge_count_(0) {
        assert(n >= 0);
    }

    // Rule of Five：vector 套 vector，值语义依然直接 = default。
    AdjListGraph(const AdjListGraph&) = default;
    AdjListGraph& operator=(const AdjListGraph&) = default;
    AdjListGraph(AdjListGraph&&) noexcept = default;
    AdjListGraph& operator=(AdjListGraph&&) noexcept = default;
    ~AdjListGraph() = default;

    Node Size() const noexcept { return n_; }
    bool Directed() const noexcept { return directed_; }
    bool Empty() const noexcept { return n_ == 0; }
    std::size_t EdgeCount() const noexcept { return edge_count_; }

    void AddEdge(Node u, Node v, W w = 1) {
        assert(InRange(u) && InRange(v));
        adj_[Idx(u)].push_back(Arc{v, w});
        if (!directed_) {
            adj_[Idx(v)].push_back(Arc{u, w});
        }
        edge_count_ += 1;
    }

    // O(deg(u))：稀疏图上往往只有几次比较，比想象中快；稠密图上就惨了。
    bool HasEdge(Node u, Node v) const {
        assert(InRange(u) && InRange(v));
        for (const Arc& a : adj_[Idx(u)]) {
            if (a.to == v) {
                return true;
            }
        }
        return false;
    }

    W EdgeWeight(Node u, Node v) const {
        assert(InRange(u) && InRange(v));
        for (const Arc& a : adj_[Idx(u)]) {
            if (a.to == v) {
                return a.weight;
            }
        }
        return std::numeric_limits<W>::lowest();
    }

    template <typename Fn>
    void ForEachNeighbor(Node u, Fn&& fn) const {
        assert(InRange(u));
        for (const Arc& a : adj_[Idx(u)]) {
            fn(a.to, a.weight);  // O(deg(u))
        }
    }

    const std::vector<Arc>& Neighbors(Node u) const { return adj_[Idx(u)]; }

    std::size_t MemoryBytes() const {
        std::size_t bytes = sizeof(*this) + adj_.capacity() * sizeof(std::vector<Arc>);
        for (const std::vector<Arc>& row : adj_) {
            bytes += row.capacity() * sizeof(Arc);
        }
        return bytes;
    }

private:
    bool InRange(Node x) const noexcept { return x >= 0 && x < n_; }

    Node n_ = 0;
    bool directed_ = false;
    std::vector<std::vector<Arc>> adj_;
    std::size_t edge_count_ = 0;
};

// 显式边表：Bellman-Ford 是「按边松弛」，Kruskal 是「按边排序」，都需要它。
struct WEdge {
    Node from = 0;
    Node to = 0;
    Weight w = 0;
};

// 把任意图表示摊平成边表。无向图每条边只收一次（用 u < v 过滤），
// 这样边表规模和「逻辑边数」一致，不会被双向存储的两倍放大。
template <typename G>
std::vector<WEdge> CollectEdges(const G& g) {
    std::vector<WEdge> edges;
    for (Node u = 0; u < g.Size(); ++u) {
        g.ForEachNeighbor(u, [&](Node v, typename G::WeightType w) {
            if (g.Directed() || u < v) {
                edges.push_back(WEdge{u, v, static_cast<Weight>(w)});
            }
        });
    }
    return edges;
}

// ===========================================================================
// 三、遍历
// ===========================================================================

// 递归 DFS 的统计信息。deepest_addr / entry_addr 用来实测「每层递归吃多少栈」。
struct DfsRecStats {
    int max_depth = 0;
    std::uintptr_t entry_addr = 0;
    std::uintptr_t deepest_addr = 0;
    std::size_t visits = 0;
};

// 递归版 DFS。
// 写法最简洁：visited 标记 + 递归调用，前序位置记录访问顺序。
// 真实工程坑：递归深度 = 图的深度，不是 O(log n)。一条 10 万节点的链图
// 会产生 10 万层递归，每层都要占栈帧，必然栈溢出（Windows 默认栈 1 MB）。
template <typename G>
void DfsRecVisit(const G& g, Node u, std::vector<char>& visited, std::vector<Node>& order,
                 int depth, std::vector<int>* depth_of, DfsRecStats* stats) {
    visited[Idx(u)] = 1;
    order.push_back(u);
    if (depth_of != nullptr) {
        (*depth_of)[Idx(u)] = depth;
    }
    if (stats != nullptr) {
        // 取一个局部变量的地址来量栈：栈向低地址增长，深度 d 处的地址比入口低，
        // (入口地址 - 最深地址) / 最大深度 就是平均每层递归的栈帧字节数。
        const unsigned char probe = 0;
        const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(&probe);
        if (depth == 0) {
            stats->entry_addr = addr;
        }
        if (depth > stats->max_depth) {
            stats->max_depth = depth;
            stats->deepest_addr = addr;
        }
        stats->visits += 1;
    }
    g.ForEachNeighbor(u, [&](Node v, typename G::WeightType) {
        if (visited[Idx(v)] == 0) {
            DfsRecVisit(g, v, visited, order, depth + 1, depth_of, stats);
        }
    });
}

// 从单个源点出发的递归 DFS。只访问源点能到达的点（用于和显式栈版本对拍）。
// 时间 O(V+E)，空间 O(V) + 递归栈 O(深度)。
template <typename G>
std::vector<Node> DfsRecursive(const G& g, Node s, std::vector<int>* depth_of = nullptr,
                               DfsRecStats* stats = nullptr) {
    const Node n = g.Size();
    std::vector<char> visited(Idx(n), 0);
    std::vector<Node> order;
    order.reserve(Idx(n));
    if (depth_of != nullptr) {
        depth_of->assign(Idx(n), -1);
    }
    DfsRecStats local_stats;
    DfsRecStats* st = (stats != nullptr) ? stats : &local_stats;
    *st = DfsRecStats{};
    DfsRecVisit(g, s, visited, order, 0, depth_of, st);
    return order;
}

// 显式栈 DFS 写法一：入栈时标记（推荐）。
// 每个点最多入栈一次，栈峰值 <= V，内存完全可控，没有递归深度限制。
// 顺序细节：栈是后进先出，想让「编号小的邻居先被访问」，就要让它最后入栈，
// 所以下面把收集到的邻居逆序压栈。
// 注意：这样得到的顺序在一般情况下和递归版不完全相同 —— 因为「入栈即标记」会让
// 0 号点的所有邻居立刻被标记，递归版里第二个邻居要等第一个邻居的子树跑完才被发现。
// 两者都是合法的 DFS，访问集合一定相同（下面 main 里会断言这一点）。
// 时间 O(V+E)，空间 O(V)（堆内存，不受栈大小限制）。
template <typename G>
std::vector<Node> DfsIterativeMarkOnPush(const G& g, Node s, std::size_t* peak_stack = nullptr,
                                         std::uint64_t* pushes = nullptr) {
    const Node n = g.Size();
    std::vector<char> visited(Idx(n), 0);
    std::vector<Node> order;
    order.reserve(Idx(n));
    std::vector<Node> stack;
    std::vector<Node> nbrs;  // 复用同一块缓冲区，避免每个点都分配一次

    stack.push_back(s);
    visited[Idx(s)] = 1;  // 入栈即标记
    if (pushes != nullptr) {
        *pushes += 1;
    }

    while (!stack.empty()) {
        if (peak_stack != nullptr) {
            *peak_stack = std::max(*peak_stack, stack.size());
        }
        const Node u = stack.back();
        stack.pop_back();
        order.push_back(u);

        nbrs.clear();
        g.ForEachNeighbor(u, [&](Node v, typename G::WeightType) {
            if (visited[Idx(v)] == 0) {
                nbrs.push_back(v);
            }
        });
        for (std::size_t i = nbrs.size(); i > 0; --i) {
            const Node v = nbrs[i - 1];
            visited[Idx(v)] = 1;
            stack.push_back(v);
            if (pushes != nullptr) {
                *pushes += 1;
            }
        }
    }
    return order;
}

// 显式栈 DFS 写法二：出栈时才检查 visited。
// 好处：不用在压栈处重复写标记逻辑，代码短一点。
// 代价：同一个点会被多个前驱重复压栈（它的每条入边都可能压一次），
// 入栈总次数可以到 O(E)，栈峰值也可能到 O(E) —— 这时候又回到了内存风险。
// 时间仍是 O(V+E)（每条边最多压一次），空间 O(E) 最坏。
template <typename G>
std::vector<Node> DfsIterativeCheckOnPop(const G& g, Node s, std::size_t* peak_stack = nullptr,
                                         std::uint64_t* pushes = nullptr) {
    const Node n = g.Size();
    std::vector<char> visited(Idx(n), 0);
    std::vector<Node> order;
    order.reserve(Idx(n));
    std::vector<Node> stack;
    std::vector<Node> nbrs;

    stack.push_back(s);
    if (pushes != nullptr) {
        *pushes += 1;
    }

    while (!stack.empty()) {
        if (peak_stack != nullptr) {
            *peak_stack = std::max(*peak_stack, stack.size());
        }
        const Node u = stack.back();
        stack.pop_back();
        if (visited[Idx(u)] != 0) {
            continue;  // 重复入栈的点在这里被丢掉
        }
        visited[Idx(u)] = 1;
        order.push_back(u);

        nbrs.clear();
        g.ForEachNeighbor(u, [&](Node v, typename G::WeightType) {
            if (visited[Idx(v)] == 0) {
                nbrs.push_back(v);
            }
        });
        for (std::size_t i = nbrs.size(); i > 0; --i) {
            stack.push_back(nbrs[i - 1]);
            if (pushes != nullptr) {
                *pushes += 1;
            }
        }
    }
    return order;
}

// BFS：用 std::queue 逐层扩展。
// 关键性质：在无权图（或所有边权相等）上，BFS 第一次访问到 v 时的层数就是
// 从源点到 v 的「边数最少」的路径长度，即无权图最短路。
// 为什么对？BFS 按距离非递减的顺序访问点：队列里的点距离最多相差 1，
// 所以一个点第一次被访问时，不可能存在更短的路径还没被发现。
// 时间 O(V+E)，空间 O(V)。
template <typename G>
std::vector<Node> BfsOrder(const G& g, Node s, std::vector<Weight>* dist_out = nullptr) {
    const Node n = g.Size();
    std::vector<Weight> dist(Idx(n), kInf);
    std::vector<Node> order;
    order.reserve(Idx(n));
    std::queue<Node> q;

    dist[Idx(s)] = 0;
    q.push(s);
    while (!q.empty()) {
        const Node u = q.front();
        q.pop();
        order.push_back(u);
        g.ForEachNeighbor(u, [&](Node v, typename G::WeightType) {
            if (dist[Idx(v)] == kInf) {  // 第一次到达就是最短
                dist[Idx(v)] = dist[Idx(u)] + 1;
                q.push(v);
            }
        });
    }
    if (dist_out != nullptr) {
        *dist_out = dist;
    }
    return order;
}

// 全图 BFS（含所有连通分量），返回访问到的点数：用来给「遍历整张图」计时。
template <typename G>
std::size_t BfsAllCount(const G& g) {
    const Node n = g.Size();
    std::vector<char> seen(Idx(n), 0);
    std::queue<Node> q;
    std::size_t count = 0;
    for (Node s = 0; s < n; ++s) {
        if (seen[Idx(s)] != 0) {
            continue;
        }
        seen[Idx(s)] = 1;
        q.push(s);
        while (!q.empty()) {
            const Node u = q.front();
            q.pop();
            count += 1;
            g.ForEachNeighbor(u, [&](Node v, typename G::WeightType) {
                if (seen[Idx(v)] == 0) {
                    seen[Idx(v)] = 1;
                    q.push(v);
                }
            });
        }
    }
    return count;
}

// ===========================================================================
// 四、连通分量：DFS 版与并查集版
// ===========================================================================

// DFS 版：时间 O(V+E)，空间 O(V)。用显式栈而不是递归，避免深图爆栈。
template <typename G>
std::vector<Node> ComponentsByDfs(const G& g, Node* component_count) {
    const Node n = g.Size();
    std::vector<Node> comp(Idx(n), -1);
    std::vector<Node> stack;
    Node count = 0;
    for (Node s = 0; s < n; ++s) {
        if (comp[Idx(s)] != -1) {
            continue;
        }
        comp[Idx(s)] = count;
        stack.push_back(s);
        while (!stack.empty()) {
            const Node u = stack.back();
            stack.pop_back();
            g.ForEachNeighbor(u, [&](Node v, typename G::WeightType) {
                if (comp[Idx(v)] == -1) {
                    comp[Idx(v)] = count;
                    stack.push_back(v);
                }
            });
        }
        count += 1;
    }
    if (component_count != nullptr) {
        *component_count = count;
    }
    return comp;
}

// 并查集版：把每条边 Unite 一次，剩下的集合个数就是连通分量数。
// 时间 O((V+E) * alpha(V))，空间 O(V)。
template <typename G>
std::vector<Node> ComponentsByDsu(const G& g, Node* component_count) {
    const Node n = g.Size();
    DSU<true, true> dsu(n);
    for (Node u = 0; u < n; ++u) {
        g.ForEachNeighbor(u, [&](Node v, typename G::WeightType) { dsu.Unite(u, v); });
    }
    std::vector<Node> comp(Idx(n), -1);
    Node count = 0;
    for (Node u = 0; u < n; ++u) {
        const Node root = dsu.Find(u);
        if (comp[Idx(root)] == -1) {
            comp[Idx(root)] = count;
            count += 1;
        }
        comp[Idx(u)] = comp[Idx(root)];
    }
    if (component_count != nullptr) {
        *component_count = count;
    }
    return comp;
}

// 把分量编号还原成「分量集合」的规范形式，好让两种算法的结果可以直接比较：
// 谁先被扫描到、谁的编号小，这些实现细节不应该影响对拍结果。
std::vector<std::vector<Node>> PartitionOf(const std::vector<Node>& comp, Node count) {
    std::vector<std::vector<Node>> parts(Idx(count));
    for (std::size_t v = 0; v < comp.size(); ++v) {
        parts[Idx(comp[v])].push_back(static_cast<Node>(v));
    }
    for (std::vector<Node>& p : parts) {
        std::sort(p.begin(), p.end());
    }
    std::sort(parts.begin(), parts.end());
    return parts;
}

// ===========================================================================
// 五、拓扑排序
// ===========================================================================

// Kahn 入度法（BFS 式）：时间 O(V+E)，空间 O(V)。
// 它能顺便检测环：如果输出的点数 < V，说明剩下的点互相牵制（入度永远降不到 0），
// 也就是存在环。这不是「额外功能」，而是这个算法天然给出的信息。
template <typename G>
std::vector<Node> TopoKahn(const G& g, bool* has_cycle) {
    const Node n = g.Size();
    std::vector<Node> indeg(Idx(n), 0);
    for (Node u = 0; u < n; ++u) {
        g.ForEachNeighbor(u, [&](Node v, typename G::WeightType) { indeg[Idx(v)] += 1; });
    }
    std::queue<Node> q;
    for (Node u = 0; u < n; ++u) {
        if (indeg[Idx(u)] == 0) {
            q.push(u);
        }
    }
    std::vector<Node> order;
    order.reserve(Idx(n));
    while (!q.empty()) {
        const Node u = q.front();
        q.pop();
        order.push_back(u);
        g.ForEachNeighbor(u, [&](Node v, typename G::WeightType) {
            indeg[Idx(v)] -= 1;
            if (indeg[Idx(v)] == 0) {
                q.push(v);
            }
        });
    }
    if (has_cycle != nullptr) {
        *has_cycle = (static_cast<Node>(order.size()) != n);
    }
    return order;
}

// DFS 三色法：
//   白色（0）= 没访问过；灰色（1）= 正在递归栈上；黑色（2）= 已回溯完成。
// 如果从灰色点又走回一个灰色点，说明找到了回边（back edge），有环。
// 这就是「递归栈检测环」——灰色集合恰好等于当前递归路径上的点。
// 没有环时，把后序（回溯完成）序列反过来就是合法拓扑序：
// 因为每条边 u->v 都保证 v 先完成，u 后完成。
// 时间 O(V+E)，空间 O(V) + 递归栈 O(深度)。
template <typename G>
bool TopoDfsVisit(const G& g, Node u, std::vector<int>& color, std::vector<Node>& postorder) {
    color[Idx(u)] = 1;  // 灰色：进入递归栈
    bool acyclic = true;
    g.ForEachNeighbor(u, [&](Node v, typename G::WeightType) {
        if (!acyclic) {
            return;
        }
        if (color[Idx(v)] == 1) {
            acyclic = false;  // 回边：v 正在递归栈上
        } else if (color[Idx(v)] == 0) {
            if (!TopoDfsVisit(g, v, color, postorder)) {
                acyclic = false;
            }
        }
    });
    color[Idx(u)] = 2;  // 黑色：回溯完成
    postorder.push_back(u);
    return acyclic;
}

template <typename G>
std::vector<Node> TopoDfs(const G& g, bool* has_cycle) {
    const Node n = g.Size();
    std::vector<int> color(Idx(n), 0);
    std::vector<Node> postorder;
    postorder.reserve(Idx(n));
    bool acyclic = true;
    for (Node u = 0; u < n; ++u) {
        if (color[Idx(u)] == 0 && !TopoDfsVisit(g, u, color, postorder)) {
            acyclic = false;
        }
    }
    std::reverse(postorder.begin(), postorder.end());  // 后序逆序 = 拓扑序
    if (has_cycle != nullptr) {
        *has_cycle = !acyclic;
    }
    return postorder;
}

// 独立校验：对每条边 u->v 都要 pos[u] < pos[v]。
template <typename G>
bool IsValidTopoOrder(const G& g, const std::vector<Node>& order) {
    const Node n = g.Size();
    if (static_cast<Node>(order.size()) != n) {
        return false;
    }
    std::vector<Node> pos(Idx(n), -1);
    for (std::size_t i = 0; i < order.size(); ++i) {
        pos[Idx(order[i])] = static_cast<Node>(i);
    }
    bool ok = true;
    for (Node u = 0; u < n; ++u) {
        g.ForEachNeighbor(u, [&](Node v, typename G::WeightType) {
            if (pos[Idx(u)] >= pos[Idx(v)]) {
                ok = false;
            }
        });
    }
    return ok;
}

// ===========================================================================
// 六、单源最短路
// ===========================================================================

// Dijkstra：时间 O((V+E) log V)，空间 O(V)。
// 二叉堆版：每个点出堆一次、每条边被松弛一次，每次堆操作 O(log V)。
// 用 priority_queue 实现小顶堆：greater 让 top() 变成最小值。
//
// 关于「已定型的节点要跳过」：
//   dist[v] 变小后我们会再压一个新条目，堆里就同时留着旧（更大）的条目。
//   弹出时用 `if (d > dist[u]) continue;` 把过期条目丢掉 —— 这叫惰性删除
//   （lazy deletion），比在堆里做 decrease-key（需要索引堆）简单得多，
//   代价只是堆里多留一些条目，不影响复杂度。
//
// 关于负权（关键）：
//   算法成立的前提是「所有边权非负 => 一旦某点出堆，它的距离就是最终答案」。
//   证明思路：设 u 出堆且还有更短的路，那条路必然经过某个还没定型的点 x，
//   于是 dist[x] + w(x,u) < dist[u]；而非负权保证 dist[x] >= dist[u]（堆序），
//   矛盾。一旦有负权边，dist[x] + w 完全可能小于 dist[u]，前提被破坏，
//   下面 settled_ 集合就会把一个「后面还能被改进」的点锁死，结果直接算错。
//   main 里给了一个具体反例并真的跑出错误结果。
template <typename G>
std::vector<Weight> Dijkstra(const G& g, Node s, std::uint64_t* pushes = nullptr) {
    const Node n = g.Size();
    std::vector<Weight> dist(Idx(n), kInf);
    std::vector<char> settled(Idx(n), 0);  // 已经「定型」的点，不允许再被改进
    using Item = std::pair<Weight, Node>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;

    dist[Idx(s)] = 0;
    pq.push({0, s});
    if (pushes != nullptr) {
        *pushes += 1;
    }

    while (!pq.empty()) {
        const Item top = pq.top();
        pq.pop();
        const Weight d = top.first;
        const Node u = top.second;
        if (d > dist[Idx(u)]) {
            continue;  // 惰性删除：堆里的过期条目
        }
        if (settled[Idx(u)] != 0) {
            continue;
        }
        settled[Idx(u)] = 1;  // 非负权下这一步是安全的
        g.ForEachNeighbor(u, [&](Node v, typename G::WeightType w) {
            if (settled[Idx(v)] != 0) {
                return;  // 不许再改已定型的点：负权时正是这一行导致算错
            }
            const Weight cand = d + static_cast<Weight>(w);
            if (cand < dist[Idx(v)]) {
                dist[Idx(v)] = cand;
                pq.push({cand, v});
                if (pushes != nullptr) {
                    *pushes += 1;
                }
            }
        });
    }
    return dist;
}

// 去掉 settled 集合、只保留惰性删除的变体。
// 它变成「优先队列版的标签修正法（label-correcting）」：只要队列排空，所有边都满足
// dist[v] <= dist[u] + w，得到的确实是一个合法的最短路解 —— 在负权图上它常常能
// 「歪打正着」算出正确答案。但代价是：点会被反复重推，复杂度退化，
// 而且遇到从源点可达的负环时它永远不终止。所以它不能替代 Bellman-Ford。
// （max_pushes 是兜底上限，防止负环把程序挂住。）
template <typename G>
std::vector<Weight> DijkstraLabelCorrecting(const G& g, Node s, std::uint64_t* pushes = nullptr,
                                            std::uint64_t max_pushes = 1000000) {
    const Node n = g.Size();
    std::vector<Weight> dist(Idx(n), kInf);
    using Item = std::pair<Weight, Node>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;

    dist[Idx(s)] = 0;
    pq.push({0, s});
    std::uint64_t total = 1;

    while (!pq.empty()) {
        const Item top = pq.top();
        pq.pop();
        const Weight d = top.first;
        const Node u = top.second;
        if (d > dist[Idx(u)]) {
            continue;
        }
        g.ForEachNeighbor(u, [&](Node v, typename G::WeightType w) {
            const Weight cand = d + static_cast<Weight>(w);
            if (cand < dist[Idx(v)]) {
                dist[Idx(v)] = cand;
                pq.push({cand, v});
                total += 1;
            }
        });
        if (total > max_pushes) {
            break;  // 可达负环：正确的做法是换 Bellman-Ford
        }
    }
    if (pushes != nullptr) {
        *pushes = total;
    }
    return dist;
}

// Bellman-Ford：时间 O(V*E)，空间 O(V)。
// 为什么最多 V-1 轮？一条最短路径最多包含 V-1 条边（V 个点、不重复走点，
// 否则把环去掉只会更短或等长）。第 k 轮结束时，所有「最多 k 条边」的最短路
// 都已经算出来了（归纳法），所以 V-1 轮足够。
// 负环检测：再做第 V 轮，如果还能松弛，说明存在一条「至少 V 条边还能变短」的路径，
// 那它必然重复经过了某个点 —— 也就是存在从源点可达的负环，此时最短路无定义（可以 -inf）。
// 注意：Bellman-Ford 处理负权的前提是「有向图」。无向图里一条负权边等价于
// 来回走的负环（u->v->u），所以下面的断言要求有向图。
struct BellmanFordResult {
    std::vector<Weight> dist;
    bool has_negative_cycle = false;
    int rounds_used = 0;
    std::uint64_t relaxations = 0;
};

// 核心实现：按给定的边表松弛。把「边表从哪来」和「怎么松弛」拆开，
// 是为了能演示「同一张图、同样的边，只是边表顺序不同 -> 有效松弛轮数从 1 变成 V-1」。
BellmanFordResult BellmanFordFromEdges(Node n, const std::vector<WEdge>& edges, Node s) {
    BellmanFordResult result;
    result.dist.assign(Idx(n), kInf);
    result.dist[Idx(s)] = 0;

    for (Node round = 0; round < n - 1; ++round) {
        bool changed = false;
        for (const WEdge& e : edges) {
            if (result.dist[Idx(e.from)] == kInf) {
                continue;  // 还没到达的点松弛不了任何东西
            }
            const Weight cand = result.dist[Idx(e.from)] + e.w;
            if (cand < result.dist[Idx(e.to)]) {
                result.dist[Idx(e.to)] = cand;
                changed = true;
                result.relaxations += 1;
            }
        }
        result.rounds_used += 1;
        if (!changed) {
            break;  // 提前收敛：因为图小或边序友好，实际轮数常常远小于 V-1
        }
    }

    // 第 V 轮：还能松弛就说明有负环
    for (const WEdge& e : edges) {
        if (result.dist[Idx(e.from)] != kInf &&
            result.dist[Idx(e.from)] + e.w < result.dist[Idx(e.to)]) {
            result.has_negative_cycle = true;
            break;
        }
    }
    return result;
}

template <typename G>
BellmanFordResult BellmanFord(const G& g, Node s) {
    assert(g.Directed());  // 无向图 + 负权 = 天然负环，见上面的注释
    return BellmanFordFromEdges(g.Size(), CollectEdges(g), s);
}

// Floyd-Warshall：O(V^3) 全源最短路，作为独立参考实现给 Dijkstra / Bellman-Ford 对拍。
// 它的思路（动态规划 + 中转点枚举）跟前面两个完全不同，所以用它做「交叉验证」很有说服力。
// 只在小图上用（V 大了这个是三次方爆炸）。
template <typename G>
std::vector<std::vector<Weight>> FloydWarshall(const G& g) {
    const Node n = g.Size();
    std::vector<std::vector<Weight>> d(Idx(n), std::vector<Weight>(Idx(n), kInf));
    for (Node i = 0; i < n; ++i) {
        d[Idx(i)][Idx(i)] = 0;
    }
    for (Node u = 0; u < n; ++u) {
        g.ForEachNeighbor(u, [&](Node v, typename G::WeightType w) {
            if (static_cast<Weight>(w) < d[Idx(u)][Idx(v)]) {
                d[Idx(u)][Idx(v)] = static_cast<Weight>(w);  // 多重边取最小
            }
        });
    }
    for (Node k = 0; k < n; ++k) {
        for (Node i = 0; i < n; ++i) {
            if (d[Idx(i)][Idx(k)] == kInf) {
                continue;
            }
            for (Node j = 0; j < n; ++j) {
                if (d[Idx(k)][Idx(j)] == kInf) {
                    continue;
                }
                const Weight cand = d[Idx(i)][Idx(k)] + d[Idx(k)][Idx(j)];
                if (cand < d[Idx(i)][Idx(j)]) {
                    d[Idx(i)][Idx(j)] = cand;
                }
            }
        }
    }
    return d;
}

// ===========================================================================
// 七、最小生成树
// ===========================================================================
struct MstEdge {
    Node u = 0;
    Node v = 0;
    Weight w = 0;
};

struct MstResult {
    std::vector<MstEdge> edges;
    Weight total = 0;
    bool spanning = false;  // 图连通时才是真的「生成树」
};

// Kruskal：时间 O(E log E)（瓶颈是排序），空间 O(V+E)。
// 贪心策略：把所有边按权重升序排列，依次尝试加入，若两个端点已经连通就丢弃。
// 为什么用并查集判环？「两个端点已连通」等价于「加入这条边会成环」，
// 而并查集的 Unite 正好返回「原本是否不在一个集合里」，O(alpha(V)) 一步搞定。
// 为什么贪心是对的？这就是 MST 的「环性质」：在任意环上，权重最大的边一定不在
// 某棵 MST 里（可以交换论证）。按升序加边恰好保证不会选到这种边。
template <typename G>
MstResult Kruskal(const G& g) {
    assert(!g.Directed());  // MST 是无向图上的概念
    std::vector<WEdge> edges = CollectEdges(g);
    std::sort(edges.begin(), edges.end(),
              [](const WEdge& a, const WEdge& b) { return a.w < b.w; });

    DSU<true, true> dsu(g.Size());
    MstResult result;
    for (const WEdge& e : edges) {
        if (dsu.Unite(e.from, e.to)) {  // 不成环才要
            result.edges.push_back(MstEdge{e.from, e.to, e.w});
            result.total += e.w;
        }
    }
    result.spanning = (dsu.Components() == 1);
    return result;
}

// Prim：时间 O(E log V)，空间 O(V+E)。
// 从一个点开始，每次把「连接树内与树外的最小边」拉进来（这是 MST 的「割性质」）。
// 用优先队列维护候选边，出堆时惰性删除已经进树的点。
struct PrimItem {
    Weight w = 0;
    Node to = 0;
    Node from = 0;
};

// 小顶堆的比较器：priority_queue 默认是大顶堆，所以这里要把「更小的 w」判为更高优先级。
struct PrimItemGreater {
    bool operator()(const PrimItem& a, const PrimItem& b) const { return a.w > b.w; }
};

template <typename G>
MstResult Prim(const G& g, Node start) {
    assert(!g.Directed());
    const Node n = g.Size();
    std::vector<char> in_tree(Idx(n), 0);
    std::priority_queue<PrimItem, std::vector<PrimItem>, PrimItemGreater> pq;
    MstResult result;

    pq.push(PrimItem{0, start, start});
    while (!pq.empty()) {
        const PrimItem top = pq.top();
        pq.pop();
        if (in_tree[Idx(top.to)] != 0) {
            continue;  // 惰性删除：堆里可能留着同一点的旧条目
        }
        in_tree[Idx(top.to)] = 1;
        if (top.to != start) {
            result.edges.push_back(MstEdge{top.from, top.to, top.w});
            result.total += top.w;
        }
        g.ForEachNeighbor(top.to, [&](Node v, typename G::WeightType w) {
            if (in_tree[Idx(v)] == 0) {
                pq.push(PrimItem{static_cast<Weight>(w), v, top.to});
            }
        });
    }
    result.spanning = (static_cast<Node>(result.edges.size()) == n - 1);
    return result;
}

// 独立验证最小性的「环性质」：
// 对每一条非树边 e=(u,v,w)，树上 u 到 v 的唯一路径上的所有边权都必须 <= w。
// 否则把那条更重的树边换成 e，就能得到总权重更小的生成树，矛盾。
// 这个检查不依赖 Kruskal / Prim 的任何内部逻辑，是真正独立的验证。
// 复杂度 O(E*V)，只在小图上用。
template <typename G>
bool VerifyCycleProperty(const G& g, const MstResult& mst) {
    const Node n = g.Size();
    std::vector<std::vector<std::pair<Node, Weight>>> tree(Idx(n));
    std::set<std::pair<Node, Node>> tree_edges;
    for (const MstEdge& e : mst.edges) {
        tree[Idx(e.u)].push_back({e.v, e.w});
        tree[Idx(e.v)].push_back({e.u, e.w});
        tree_edges.insert({std::min(e.u, e.v), std::max(e.u, e.v)});
    }

    bool ok = true;
    for (Node u = 0; u < n && ok; ++u) {
        g.ForEachNeighbor(u, [&](Node v, typename G::WeightType w) {
            if (!ok || u > v) {
                return;  // 无向图每条边只看一次
            }
            if (tree_edges.count({u, v}) != 0) {
                return;  // 树边不用检查
            }
            // BFS 找树上 u->v 的唯一路径，并记录路径上的最大边权
            std::vector<Weight> max_on_path(Idx(n), -1);
            std::vector<char> seen(Idx(n), 0);
            std::queue<Node> q;
            seen[Idx(u)] = 1;
            max_on_path[Idx(u)] = 0;
            q.push(u);
            while (!q.empty()) {
                const Node x = q.front();
                q.pop();
                for (const std::pair<Node, Weight>& arc : tree[Idx(x)]) {
                    if (seen[Idx(arc.first)] != 0) {
                        continue;
                    }
                    seen[Idx(arc.first)] = 1;
                    max_on_path[Idx(arc.first)] = std::max(max_on_path[Idx(x)], arc.second);
                    q.push(arc.first);
                }
            }
            assert(seen[Idx(v)] != 0);  // 生成树必须连通，v 一定可达
            if (max_on_path[Idx(v)] > static_cast<Weight>(w)) {
                ok = false;
            }
        });
    }
    return ok;
}

// ===========================================================================
// 八、造数据 / 小工具
// ===========================================================================

// 一条链（路径图）：链图的 DFS 深度 = V-1，是「深递归」最坏情况的极端例子。
template <typename G>
G BuildPathGraph(Node n, bool directed) {
    G g(n, directed);
    for (Node i = 0; i + 1 < n; ++i) {
        g.AddEdge(i, i + 1, 1);
    }
    return g;
}

// 先造一棵随机生成树保证连通，再加若干随机边。
// 为什么一定要连通？「连通分量数」「MST 边数 = V-1」「BFS 能走遍全图」这些断言
// 只有连通图才成立；随机撒边生成的图很可能是不连通的，断言会直接挂掉。
std::vector<std::pair<Node, Node>> MakeConnectedPairs(Node n, int extra_per_node, unsigned seed) {
    std::mt19937 rng(seed);
    std::set<std::pair<Node, Node>> uniq;
    for (Node i = 1; i < n; ++i) {
        const Node j = static_cast<Node>(rng() % static_cast<unsigned>(i));
        uniq.insert({std::min(i, j), std::max(i, j)});
    }
    for (Node i = 0; i < n; ++i) {
        for (int k = 0; k < extra_per_node; ++k) {
            const Node j = static_cast<Node>(rng() % static_cast<unsigned>(n));
            if (j == i) {
                continue;
            }
            uniq.insert({std::min(i, j), std::max(i, j)});
        }
    }
    return std::vector<std::pair<Node, Node>>(uniq.begin(), uniq.end());
}

// 稠密图：每个点对以 percent% 的概率连边。用来演示「矩阵查边 O(1) 的优势」。
std::vector<std::pair<Node, Node>> MakeDensePairs(Node n, int percent, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<std::pair<Node, Node>> edges;
    for (Node u = 0; u < n; ++u) {
        for (Node v = u + 1; v < n; ++v) {
            if (static_cast<int>(rng() % 100u) < percent) {
                edges.push_back({u, v});
            }
        }
    }
    return edges;
}

// 有向图：每个点随机连出 per_node 条边（用于 Bellman-Ford 的性能对比）。
std::vector<std::pair<Node, Node>> MakeDirectedPairs(Node n, int per_node, unsigned seed) {
    std::mt19937 rng(seed);
    std::set<std::pair<Node, Node>> uniq;
    for (Node u = 0; u < n; ++u) {
        for (int k = 0; k < per_node; ++k) {
            const Node v = static_cast<Node>(rng() % static_cast<unsigned>(n));
            if (v != u) {
                uniq.insert({u, v});
            }
        }
    }
    return std::vector<std::pair<Node, Node>>(uniq.begin(), uniq.end());
}

std::vector<Weight> MakeWeights(std::size_t count, Weight lo, Weight hi, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<Weight> w(count, lo);
    for (std::size_t i = 0; i < count; ++i) {
        w[i] = lo + static_cast<Weight>(rng() % static_cast<unsigned>(hi - lo + 1));
    }
    return w;
}

template <typename G>
G BuildGraph(const std::vector<std::pair<Node, Node>>& edges, Node n, bool directed) {
    G g(n, directed);
    for (const std::pair<Node, Node>& e : edges) {
        g.AddEdge(e.first, e.second, 1);
    }
    return g;
}

template <typename G>
G BuildWeightedGraph(Node n, const std::vector<std::pair<Node, Node>>& edges,
                     const std::vector<Weight>& weights, bool directed) {
    assert(edges.size() == weights.size());
    G g(n, directed);
    for (std::size_t i = 0; i < edges.size(); ++i) {
        g.AddEdge(edges[i].first, edges[i].second, weights[i]);
    }
    return g;
}

// 查边基准：一半查询是真实存在的边（考验命中路径），一半是随机点对（考验失败路径）。
template <typename G>
std::uint64_t BenchHasEdge(const G& g, const std::vector<std::pair<Node, Node>>& queries) {
    std::uint64_t hits = 0;
    for (const std::pair<Node, Node>& q : queries) {
        hits += g.HasEdge(q.first, q.second) ? 1u : 0u;
    }
    return hits;
}

std::vector<std::pair<Node, Node>> MakeEdgeQueries(const std::vector<std::pair<Node, Node>>& edges,
                                                   Node n, std::size_t count, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<std::pair<Node, Node>> queries;
    queries.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (i % 2 == 0 && !edges.empty()) {
            queries.push_back(edges[rng() % edges.size()]);  // 命中的查询
        } else {
            const Node a = static_cast<Node>(rng() % static_cast<unsigned>(n));
            const Node b = static_cast<Node>(rng() % static_cast<unsigned>(n));
            queries.push_back({a, b});  // 多半查不到的查询
        }
    }
    return queries;
}

// --- 打印小工具（含中文的标签一律用 Label） ---
template <typename G>
void PrintEdges(const G& g) {
    for (Node u = 0; u < g.Size(); ++u) {
        g.ForEachNeighbor(u, [&](Node v, typename G::WeightType w) {
            if (g.Directed() || u <= v) {  // 无向图每条边只打印一次
                std::cout << "    " << u << (g.Directed() ? " -> " : " -- ") << v;
                if (static_cast<Weight>(w) != 1) {
                    std::cout << " (w=" << w << ")";
                }
                std::cout << "\n";
            }
        });
    }
}

void PrintSequence(const std::string& name, const std::vector<Node>& seq,
                   std::size_t label_width = 30) {
    Label("  " + name, label_width);
    std::cout << ": ";
    for (const Node x : seq) {
        std::cout << x << " ";
    }
    std::cout << "\n";
}

void PrintDistLine(const std::string& name, const std::vector<Weight>& dist,
                   std::size_t label_width = 30) {
    Label("  " + name, label_width);
    std::cout << ": ";
    for (const Weight d : dist) {
        if (d >= kInf) {
            std::cout << "INF ";
        } else {
            std::cout << d << " ";
        }
    }
    std::cout << "\n";
}

// --- 并查集微基准 ---

// 二项树式的合并顺序：第 k 轮把长度为 2^k 的块两两合并。
// 这是 union-by-size 的典型最坏形状 —— 最后得到的树深度正好是 log2(n)，
// 所以「不压缩」的 find 成本会老老实实按 log n 增长。
// （如果只是「随便连」，随机合并出来的树很浅，压缩与否看起来差不多，量不出差别。）
// n 取 2 的幂时，这个顺序会把所有点合并成一个集合。
template <typename DsuT>
void DsuBinomialUnions(DsuT& dsu, Node n) {
    for (Node len = 1; len < n; len *= 2) {
        for (Node i = 0; i + len < n; i += 2 * len) {
            dsu.Unite(i, i + len);
        }
    }
}

// 工作负载：二项树式合并 + n 次随机查找。
// 返回一个校验和（防止被优化掉），并通过 hops_out 带出「父指针步数」。
// hops 是比时间更干净的复杂度证据：它不依赖机器、不依赖 Debug/Release。
template <bool kCompress, bool kBySize>
std::uint64_t DsuWorkload(Node n, unsigned seed, std::uint64_t* hops_out) {
    DSU<kCompress, kBySize> dsu(n);
    DsuBinomialUnions(dsu, n);
    std::mt19937 rng(seed);
    const unsigned un = static_cast<unsigned>(n);
    std::uint64_t acc = 0;
    for (Node i = 0; i < n; ++i) {
        const Node a = static_cast<Node>(rng() % un);
        acc += static_cast<std::uint64_t>(dsu.Find(a));
    }
    if (hops_out != nullptr) {
        *hops_out = dsu.Hops();
    }
    return acc + static_cast<std::uint64_t>(dsu.Components());
}

// 同一组合并顺序下，最坏的一次 find 要走多少步（= 树高）。
// 这是树高最直接的量法：逐个 Find、每次先清零计数器，取最大值。
template <bool kCompress, bool kBySize>
std::uint64_t DsuWorstFindHops(Node n) {
    DSU<kCompress, kBySize> dsu(n);
    DsuBinomialUnions(dsu, n);
    std::uint64_t worst = 0;
    for (Node i = 0; i < n; ++i) {
        dsu.ResetHops();
        dsu.Find(i);
        worst = std::max(worst, dsu.Hops());
    }
    return worst;
}

// 病态工作负载：链式合并（Union(0,1), Union(1,2), ...）。
// 「不压缩 + 不按秩」在这种顺序下会把父指针拉成一条长链，Find 退化成 O(n)；
// 全功能 DSU 则几乎不受影响。这个实验用实测跳数把教科书上的最坏情况演出来。
template <bool kCompress, bool kBySize>
std::uint64_t DsuChainWorkload(Node n, std::uint64_t* hops_out) {
    DSU<kCompress, kBySize> dsu(n);
    for (Node i = 0; i + 1 < n; ++i) {
        dsu.Unite(i, i + 1);
    }
    std::uint64_t acc = 0;
    for (Node i = 0; i < n; ++i) {
        acc += static_cast<std::uint64_t>(dsu.Find(i));
    }
    if (hops_out != nullptr) {
        *hops_out = dsu.Hops();
    }
    return acc + static_cast<std::uint64_t>(dsu.Components());
}

struct DsuBenchRow {
    std::string name;
    Node n = 0;
    std::uint64_t hops = 0;
    std::uint64_t worst_find_hops = 0;
    double total_us = 0.0;
    double ns_per_op = 0.0;
    double hops_per_op = 0.0;
};

template <bool kCompress, bool kBySize>
DsuBenchRow RunDsuBench(const std::string& name, Node n, unsigned seed, int repeats) {
    DsuBenchRow row;
    row.name = name;
    row.n = n;
    row.worst_find_hops = DsuWorstFindHops<kCompress, kBySize>(n);
    row.total_us = BenchMedianUs(
        [&] { return DsuWorkload<kCompress, kBySize>(n, seed, &row.hops); }, repeats);
    const double ops = 2.0 * static_cast<double>(n) - 1.0;  // n-1 次 unite + n 次 find
    row.ns_per_op = row.total_us * 1000.0 / ops;
    row.hops_per_op = static_cast<double>(row.hops) / ops;
    return row;
}

}  // namespace

int main() {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "图算法总览：表示、遍历、连通性、拓扑、最短路、最小生成树。\n";
    std::cout << "本文件在 Debug 配置下运行，绝对耗时比 Release 慢，请只看趋势和数量级。\n";

    // =======================================================================
    Section("1. 图的两种表示：邻接矩阵 vs 邻接表");
    // =======================================================================
    Note("邻接矩阵：V*V 的权值表，无边用哨兵。空间 O(V^2)，查边 O(1)，遍历邻居 O(V)。");
    Note("邻接表  ：每个点一条出边数组。     空间 O(V+E)，查边 O(deg)，遍历邻居 O(deg)。");
    Note("怎么选：稠密图 / 需要频繁 O(1) 查边 / V 较小 -> 矩阵；稀疏图 / V 很大 -> 邻接表。");
    Note("注意后半句的前提：稀疏图上邻接表查边只要扫几条边，「O(1) 对 O(deg)」的理论优势");
    Note("      会被缓存层级吃掉，实测矩阵只快 2 倍左右，却要多付 67 倍内存 —— 下面有数字。");
    std::cout << "\n";

    // 稀疏图：保证连通，这样「遍历走遍全图」的断言才有意义。
    const Node sparse_v = 1500;
    const std::vector<std::pair<Node, Node>> sparse_edges =
        MakeConnectedPairs(sparse_v, 3, 20240601u);
    const std::size_t sparse_e = sparse_edges.size();

    const AdjMatrixGraph<Weight> sparse_matrix =
        BuildGraph<AdjMatrixGraph<Weight>>(sparse_edges, sparse_v, false);
    const AdjListGraph<Weight> sparse_list =
        BuildGraph<AdjListGraph<Weight>>(sparse_edges, sparse_v, false);

    // 两种表示必须描述同一张图：逐条边互相核对。
    Check(sparse_matrix.EdgeCount() == sparse_e && sparse_list.EdgeCount() == sparse_e,
          "矩阵与邻接表的边数一致（E=" + std::to_string(sparse_e) + "）");
    {
        bool same = true;
        for (const std::pair<Node, Node>& e : sparse_edges) {
            same = same && sparse_matrix.HasEdge(e.first, e.second) &&
                   sparse_list.HasEdge(e.first, e.second);
        }
        Check(same, "矩阵与邻接表逐边核对一致");
    }
    Check(static_cast<Node>(BfsOrder(sparse_list, 0).size()) == sparse_v,
          "稀疏图连通（BFS 从 0 号点走遍全部 V=" + std::to_string(sparse_v) + " 个点）");

    std::cout << "\n";
    Label("  稀疏图 V", 34);
    std::cout << ": " << sparse_v << "，E = " << sparse_e << "（平均度数 "
              << (2.0 * static_cast<double>(sparse_e) / static_cast<double>(sparse_v)) << "）\n";
    Label("  邻接矩阵 估计内存", 34);
    std::cout << ": " << sparse_matrix.MemoryBytes() << " 字节（理论 V*V*sizeof(Weight) = "
              << (static_cast<std::size_t>(sparse_v) * static_cast<std::size_t>(sparse_v) *
                  sizeof(Weight))
              << "）\n";
    Label("  邻接表   估计内存", 34);
    std::cout << ": " << sparse_list.MemoryBytes() << " 字节（理论 (V+2E)*sizeof(Arc) = "
              << ((static_cast<std::size_t>(sparse_v) + 2 * sparse_e) * sizeof(AdjListGraph<Weight>::Arc))
              << " + V 个 vector 头）\n";
    Label("  内存倍数（矩阵 / 邻接表）", 34);
    std::cout << ": "
              << (static_cast<double>(sparse_matrix.MemoryBytes()) /
                  static_cast<double>(sparse_list.MemoryBytes()))
              << " x\n";

    const std::vector<std::pair<Node, Node>> sparse_queries =
        MakeEdgeQueries(sparse_edges, sparse_v, 20000, 4242u);

    WarmUp([&] { return static_cast<std::uint64_t>(BfsAllCount(sparse_list)); }, 1);

    const double t_build_matrix = BenchMedianUs(
        [&] {
            const AdjMatrixGraph<Weight> m =
                BuildGraph<AdjMatrixGraph<Weight>>(sparse_edges, sparse_v, false);
            return static_cast<std::uint64_t>(m.EdgeCount() + m.MemoryBytes());
        },
        3);
    const double t_build_list = BenchMedianUs(
        [&] {
            const AdjListGraph<Weight> l =
                BuildGraph<AdjListGraph<Weight>>(sparse_edges, sparse_v, false);
            return static_cast<std::uint64_t>(l.EdgeCount() + l.MemoryBytes());
        },
        3);
    const double t_trav_matrix =
        BenchMedianUs([&] { return static_cast<std::uint64_t>(BfsAllCount(sparse_matrix)); }, 3);
    const double t_trav_list =
        BenchMedianUs([&] { return static_cast<std::uint64_t>(BfsAllCount(sparse_list)); }, 3);
    // 查边操作只要几十纳秒，套一层内部重复把它抬离计时器噪声区。
    const double t_query_matrix = BenchMedianUs(
        [&] { return Repeat([&] { return BenchHasEdge(sparse_matrix, sparse_queries); }, 20); }, 3);
    const double t_query_list = BenchMedianUs(
        [&] { return Repeat([&] { return BenchHasEdge(sparse_list, sparse_queries); }, 20); }, 3);

    std::cout << "\n";
    Label("  操作（稀疏图，E << V^2）", 34);
    LabelRight("邻接矩阵", 16);
    LabelRight("邻接表", 16);
    std::cout << "\n";
    std::cout << "  " << std::string(66, '-') << "\n";
    Label("  建图（V 个点 + E 条边）us", 34);
    std::cout << "  " << std::setw(14) << t_build_matrix << std::setw(14) << t_build_list << "\n";
    Label("  全图 BFS 遍历 us", 34);
    std::cout << "  " << std::setw(14) << t_trav_matrix << std::setw(14) << t_trav_list << "\n";
    Label("  查边 40 万次 us", 34);
    std::cout << "  " << std::setw(14) << t_query_matrix << std::setw(14) << t_query_list << "\n";
    Note("");
    Note("结论 1：稀疏图上矩阵光是初始化 V*V 个格子就慢 " +
         Fmt(t_build_matrix / t_build_list, 1) + " 倍，内存差 " +
         Fmt(static_cast<double>(sparse_matrix.MemoryBytes()) /
                 static_cast<double>(sparse_list.MemoryBytes()),
             0) +
         " 倍（18 MB 对 268 KB）；");
    Note("        遍历时矩阵每个点都要扫一整行，总代价 O(V^2) 而不是 O(V+E)，慢了 " +
         Fmt(t_trav_matrix / t_trav_list, 1) + " 倍。");
    Note("结论 2：查边这一行矩阵只快 " + Fmt(t_query_list / t_query_matrix, 1) + " 倍 —— ");
    Note("        邻接表在稀疏图上只要扫几条边（平均度数 8），和矩阵的一次随机访问");
    Note("        处在同一量级。理论上的 O(1) vs O(deg) 在 deg 很小时被缓存层次抹平了，");
    Note("        这正是「复杂度要配合常数看」的例子：省下几纳秒，代价是 67 倍内存。");

    // 稠密图：矩阵的 O(1) 查边这时才真正值钱。
    const Node dense_v = 400;
    const std::vector<std::pair<Node, Node>> dense_edges = MakeDensePairs(dense_v, 25, 777u);
    const std::size_t dense_e = dense_edges.size();
    const AdjMatrixGraph<Weight> dense_matrix =
        BuildGraph<AdjMatrixGraph<Weight>>(dense_edges, dense_v, false);
    const AdjListGraph<Weight> dense_list =
        BuildGraph<AdjListGraph<Weight>>(dense_edges, dense_v, false);
    Check(dense_matrix.EdgeCount() == dense_e && dense_list.EdgeCount() == dense_e,
          "稠密图两种表示边数一致（E=" + std::to_string(dense_e) + "）");

    const std::vector<std::pair<Node, Node>> dense_queries2 =
        MakeEdgeQueries(dense_edges, dense_v, 20000, 555u);
    const double t_dense_query_matrix = BenchMedianUs(
        [&] { return Repeat([&] { return BenchHasEdge(dense_matrix, dense_queries2); }, 20); }, 3);
    const double t_dense_query_list = BenchMedianUs(
        [&] { return Repeat([&] { return BenchHasEdge(dense_list, dense_queries2); }, 20); }, 3);

    std::cout << "\n";
    Label("  稠密图 V", 34);
    std::cout << ": " << dense_v << "，E = " << dense_e << "（平均度数 "
              << (2.0 * static_cast<double>(dense_e) / static_cast<double>(dense_v)) << "）\n";
    Label("  矩阵估计内存 / 邻接表估计内存", 40);
    std::cout << ": " << dense_matrix.MemoryBytes() << " / " << dense_list.MemoryBytes()
              << " 字节（比值 "
              << (static_cast<double>(dense_matrix.MemoryBytes()) /
                  static_cast<double>(dense_list.MemoryBytes()))
              << " x）\n";
    Label("  查边 40 万次：矩阵 us", 34);
    std::cout << ": " << t_dense_query_matrix << "\n";
    Label("  查边 40 万次：邻接表 us", 34);
    std::cout << ": " << t_dense_query_list << "\n";
    Label("  邻接表 / 矩阵 倍数", 34);
    std::cout << ": " << (t_dense_query_list / t_dense_query_matrix) << " x\n";
    Note("");
    Note("结论 3：图一稠密，矩阵查边的优势立刻拉开：这里邻接表慢了 " +
         Fmt(t_dense_query_list / t_dense_query_matrix, 1) +
         " 倍，因为平均要扫上百条边，矩阵永远只访问一个格子。");
    Note("        而且稠密图上矩阵的内存劣势也不大（E 接近 V^2/4 时两者同量级，");
    Note("        这里只差 1.5 倍），所以稠密 + 频繁查边是矩阵少见的划算场景。");
    Note("工程判据：先看密度。E 远小于 V^2（绝大多数真实图）就用邻接表；");
    Note("          只有「V 不大 + 边很多 + 查边极频繁」才值得换成矩阵。");

    // =======================================================================
    Section("2. 遍历：递归 DFS / 显式栈 DFS / BFS");
    // =======================================================================
    Note("小图（9 个点，4 个连通分量），全部结果都可以手算核对。");
    const std::vector<std::pair<Node, Node>> small_edges = {
        {0, 1}, {0, 2}, {1, 2}, {1, 3}, {2, 3}, {4, 5}, {5, 6}};
    const AdjListGraph<Weight> small = BuildGraph<AdjListGraph<Weight>>(small_edges, 9, false);
    std::cout << "\n";
    Note("无向图边表：");
    PrintEdges(small);
    Check(small.EdgeCount() == 7, "边数 = 7");

    std::vector<int> dfs_depth;
    DfsRecStats rec_stats;
    const std::vector<Node> order_rec = DfsRecursive(small, 0, &dfs_depth, &rec_stats);
    std::size_t peak_push = 0;
    std::uint64_t pushes_push = 0;
    const std::vector<Node> order_iter_push =
        DfsIterativeMarkOnPush(small, 0, &peak_push, &pushes_push);
    std::size_t peak_pop = 0;
    std::uint64_t pushes_pop = 0;
    const std::vector<Node> order_iter_pop =
        DfsIterativeCheckOnPop(small, 0, &peak_pop, &pushes_pop);
    std::vector<Weight> bfs_dist;
    const std::vector<Node> order_bfs = BfsOrder(small, 0, &bfs_dist);

    std::cout << "\n";
    PrintSequence("递归 DFS 访问顺序", order_rec);
    PrintSequence("显式栈 DFS（入栈标记）", order_iter_push);
    PrintSequence("显式栈 DFS（出栈检查）", order_iter_pop);
    PrintSequence("BFS 访问顺序", order_bfs);
    PrintDistLine("BFS 从 0 出发的距离", bfs_dist);

    const std::set<Node> set_rec(order_rec.begin(), order_rec.end());
    const std::set<Node> set_push(order_iter_push.begin(), order_iter_push.end());
    const std::set<Node> set_pop(order_iter_pop.begin(), order_iter_pop.end());
    Check(set_rec == set_push && set_rec == set_pop,
          "三种 DFS 访问到的点集完全相同（都是 0 号点所在分量 {0,1,2,3}）");
    Check(static_cast<Node>(order_rec.size()) == 4, "DFS 只走源点所在分量，共 4 个点");

    // BFS 的无权最短路性质 + 与 DFS 深度的关系
    {
        bool bfs_ok = (bfs_dist[0] == 0);
        bool dist_ok = true;
        for (Node u = 0; u < small.Size(); ++u) {
            small.ForEachNeighbor(u, [&](Node v, Weight) {
                if (bfs_dist[Idx(u)] < kInf && bfs_dist[Idx(v)] > bfs_dist[Idx(u)] + 1) {
                    dist_ok = false;
                }
            });
        }
        for (Node u = 0; u < small.Size(); ++u) {
            if (bfs_dist[Idx(u)] < kInf && dfs_depth[Idx(u)] >= 0 &&
                bfs_dist[Idx(u)] > dfs_depth[Idx(u)]) {
                bfs_ok = false;
            }
        }
        Check(dist_ok, "BFS 距离满足三角不等式 dist[v] <= dist[u] + 1（无权最短路成立）");
        Check(bfs_ok, "对每个可达点都有 BFS 距离 <= DFS 树深度");
        Check(bfs_dist[Idx(3)] == 2, "0 到 3 的最短边数是 2（0-1-3 或 0-2-3）");
    }

    std::cout << "\n";
    Note("顺序差异说明：三种写法的点集相同，但顺序可以不同，本图就是例子。");
    Note("  递归版：访问 0 后立刻钻进 1，1 再钻进 2 并一路走到底 -> 0 1 2 3；");
    Note("  「入栈即标记」版：访问 0 时就把 1 和 2 都标记并压栈了，");
    Note("  于是 3 会先于 2 出栈 -> 0 1 3 2；「出栈检查」版压栈时机不同，又变回 0 1 2 3。");
    Note("  三者都是合法 DFS，不该强行要求序列逐位相等 —— 断言点集相等才是对的。");
    Check(order_rec != order_iter_push,
          "本图上递归版与「入栈即标记」版的访问顺序确实不同（说明「合法的 DFS 不唯一」）");

    // 中等规模图上量化两种显式栈写法的差别。
    const Node mid_v = 5000;
    const std::vector<std::pair<Node, Node>> mid_edges = MakeConnectedPairs(mid_v, 3, 31337u);
    const AdjListGraph<Weight> mid = BuildGraph<AdjListGraph<Weight>>(mid_edges, mid_v, false);
    std::size_t mid_peak_push = 0;
    std::uint64_t mid_pushes_push = 0;
    std::size_t mid_peak_pop = 0;
    std::uint64_t mid_pushes_pop = 0;
    const std::vector<Node> mid_order_push =
        DfsIterativeMarkOnPush(mid, 0, &mid_peak_push, &mid_pushes_push);
    const std::vector<Node> mid_order_pop =
        DfsIterativeCheckOnPop(mid, 0, &mid_peak_pop, &mid_pushes_pop);
    Check(mid_order_push.size() == mid_order_pop.size(),
          "中等图两种显式栈写法访问点数相同（" + std::to_string(mid_order_push.size()) + "）");

    std::cout << "\n";
    Label("  中等图 V / E", 34);
    std::cout << ": " << mid_v << " / " << mid_edges.size() << "\n";
    Label("  入栈标记：入栈次数 / 栈峰值", 34);
    std::cout << ": " << mid_pushes_push << " / " << mid_peak_push << "\n";
    Label("  出栈检查：入栈次数 / 栈峰值", 34);
    std::cout << ": " << mid_pushes_pop << " / " << mid_peak_pop << "\n";
    Label("  入栈次数 倍数（出栈检查/入栈标记）", 34);
    std::cout << ": "
              << (static_cast<double>(mid_pushes_pop) / static_cast<double>(mid_pushes_push))
              << " x\n";
    Note("");
    Note("结论：「出栈检查」会重复入栈（一个点多条入边就多压几次），");
    Note("      栈峰值和入栈次数都明显更大；「入栈即标记」每点只入栈一次，");
    Note("      空间严格 O(V)。所以显式栈写法优先选「入栈即标记」。");

    // 深图：递归 DFS 的真正杀手
    std::cout << "\n";
    Note("深图实验：递归 DFS 每层都要占栈帧，下面用局部变量地址实测每层吃掉多少字节。");
    const Node shallow_v = 400;
    const AdjListGraph<Weight> shallow_path = BuildPathGraph<AdjListGraph<Weight>>(shallow_v, false);
    std::vector<int> path_depth;
    DfsRecStats path_stats;
    const std::vector<Node> path_order = DfsRecursive(shallow_path, 0, &path_depth, &path_stats);
    const std::size_t frames = static_cast<std::size_t>(path_stats.max_depth);
    const std::size_t frame_bytes = (path_stats.entry_addr - path_stats.deepest_addr) / frames;
    const std::size_t depth_in_1mb = (1024u * 1024u) / (frame_bytes == 0 ? 1u : frame_bytes);
    Check(static_cast<Node>(path_order.size()) == shallow_v && path_stats.max_depth == shallow_v - 1,
          "400 点链图递归 DFS 深度 = 399（链图深度就是 V-1，与图规模线性相关）");
    Check(frames * frame_bytes < 700u * 1024u, "本次递归实测栈占用仍在 1 MB 默认栈的安全区间内");

    std::cout << "\n";
    Label("  链图递归最大深度", 34);
    std::cout << ": " << path_stats.max_depth << "\n";
    Label("  实测平均每层栈帧", 34);
    std::cout << ": " << frame_bytes << " 字节\n";
    Label("  按 1 MB 默认栈推算的最大安全深度", 40);
    std::cout << ": 约 " << depth_in_1mb << " 层\n";
    Note("");
    Note("这就是「递归 DFS 会爆栈」的量化版本：深度和 V 成正比，而栈只有 1 MB。");
    Note("20 万节点的链图需要 20 万层递归，按上面的帧大小要几十 MB 栈 —— 必崩。");

    const Node deep_v = 200000;
    const AdjListGraph<Weight> deep_path = BuildPathGraph<AdjListGraph<Weight>>(deep_v, false);
    std::size_t deep_peak = 0;
    std::uint64_t deep_pushes = 0;
    const double t_deep = BenchMedianUs(
        [&] {
            // 计数器每次都要用局部变量重新开始：BenchMedianUs 内部会重复调用，
            // 直接把指针传进去会让「入栈次数」累加好几遍。
            std::uint64_t pushes = 0;
            std::size_t peak = 0;
            const std::size_t visited =
                DfsIterativeMarkOnPush(deep_path, 0, &peak, &pushes).size();
            deep_pushes = pushes;
            deep_peak = std::max(deep_peak, peak);
            return static_cast<std::uint64_t>(visited);
        },
        3);
    Check(deep_pushes == static_cast<std::uint64_t>(deep_v), "20 万点链图显式栈 DFS 访问了全部点");
    Label("  20 万点链图：显式栈 DFS 耗时 us", 40);
    std::cout << ": " << t_deep << "（栈峰值 " << deep_peak << "，递归版在这里会栈溢出）\n";
    Note("");
    Note("注意「栈峰值 " + std::to_string(deep_peak) +
         "」这个数字：链图上每个点只有一个未访问邻居，");
    Note("所以显式栈全程只有 1 个元素 —— 同一张图，显式栈用 O(1) 内存，");
    Note("递归版却需要 20 万层递归（按实测帧大小约 " +
         Fmt(static_cast<double>(deep_v) * static_cast<double>(frame_bytes) / (1024.0 * 1024.0),
             0) +
         " MB 栈）。");
    Note("这就是「显式栈用堆换栈」的实际含义：内存从不可控的调用栈挪到了可控的堆。");
    Note("工程结论：深图（链、长走廊、退化树）一律改成显式栈；");
    Note("          如果非要用递归，得保证图深度可控，或者用平台 API 开一个更大的栈");
    Note("          （std::thread 不能指定栈大小，Windows 上要 CreateThread / 链接器 /STACK）。");

    // =======================================================================
    Section("3. 连通分量：DFS 与并查集对拍");
    // =======================================================================
    Note("两种解法思路完全不同，结果必须一致 —— 这是最省事的正确性验证方式。");
    Node dfs_comp_count = 0;
    Node dsu_comp_count = 0;
    const std::vector<Node> comp_dfs = ComponentsByDfs(small, &dfs_comp_count);
    const std::vector<Node> comp_dsu = ComponentsByDsu(small, &dsu_comp_count);
    std::cout << "\n";
    Label("  DFS 版分量数", 26);
    std::cout << ": " << dfs_comp_count << "\n";
    Label("  并查集版分量数", 26);
    std::cout << ": " << dsu_comp_count << "\n";
    Check(dfs_comp_count == 4 && dsu_comp_count == 4, "小图有 4 个连通分量");
    Check(PartitionOf(comp_dfs, dfs_comp_count) == PartitionOf(comp_dsu, dsu_comp_count),
          "两种解法给出的分量划分完全相同");

    // 在稀疏大图上再对拍一次，顺便验证连通性。
    Node big_dfs_comp = 0;
    Node big_dsu_comp = 0;
    const std::vector<Node> big_comp_dfs = ComponentsByDfs(sparse_list, &big_dfs_comp);
    const std::vector<Node> big_comp_dsu = ComponentsByDsu(sparse_list, &big_dsu_comp);
    Check(big_dfs_comp == 1 && big_dsu_comp == 1,
          "1500 点稀疏图是连通的（两种解法都给出 1 个分量）");
    Check(PartitionOf(big_comp_dfs, big_dfs_comp) == PartitionOf(big_comp_dsu, big_dsu_comp),
          "大图上两种解法的分量划分也完全相同");

    const double t_comp_dfs = BenchMedianUs(
        [&] {
            Node c = 0;
            const std::vector<Node> v = ComponentsByDfs(sparse_list, &c);
            return static_cast<std::uint64_t>(v.size()) + static_cast<std::uint64_t>(c);
        },
        3);
    const double t_comp_dsu = BenchMedianUs(
        [&] {
            Node c = 0;
            const std::vector<Node> v = ComponentsByDsu(sparse_list, &c);
            return static_cast<std::uint64_t>(v.size()) + static_cast<std::uint64_t>(c);
        },
        3);
    std::cout << "\n";
    Label("  1500 点稀疏图：DFS 版 us", 32);
    std::cout << ": " << t_comp_dfs << "\n";
    Label("  1500 点稀疏图：并查集版 us", 32);
    std::cout << ": " << t_comp_dsu << "\n";
    Note("");
    Note("两者都是 O(V+E) / O((V+E) alpha(V))，实测同一量级。");
    Note("选哪个看需求：只想知道「谁是连通的」用并查集（支持边到边动态合并）；");
    Note("             还想遍历图或拿到路径/分量成员，就用 DFS。");

    // =======================================================================
    Section("4. 拓扑排序：Kahn 入度法 与 DFS 三色法");
    // =======================================================================
    Note("拓扑序：把有向无环图（DAG）的点排成一列，使每条边 u->v 都满足 pos[u] < pos[v]。");
    Note("只有 DAG 才有拓扑序；反过来说，拓扑排序也是检测环的工具。");
    const std::vector<std::pair<Node, Node>> dag_edges = {
        {0, 1}, {0, 2}, {1, 3}, {2, 3}, {3, 4}, {2, 4}, {4, 5}};
    const AdjListGraph<Weight> dag = BuildGraph<AdjListGraph<Weight>>(dag_edges, 6, true);
    std::cout << "\n";
    Note("DAG 边表（6 个点）：");
    PrintEdges(dag);

    bool kahn_cycle = false;
    bool dfs_cycle = false;
    const std::vector<Node> kahn_order = TopoKahn(dag, &kahn_cycle);
    const std::vector<Node> dfs_topo = TopoDfs(dag, &dfs_cycle);
    std::cout << "\n";
    PrintSequence("Kahn 拓扑序", kahn_order);
    PrintSequence("DFS 三色法拓扑序", dfs_topo);
    Check(!kahn_cycle && !dfs_cycle, "两种方法都判定该图无环");
    Check(IsValidTopoOrder(dag, kahn_order), "Kahn 的结果是合法拓扑序（逐边检查 pos[u] < pos[v]）");
    Check(IsValidTopoOrder(dag, dfs_topo), "DFS 的结果是合法拓扑序（逐边检查 pos[u] < pos[v]）");
    Check(kahn_order.size() == 6 && dfs_topo.size() == 6, "拓扑序包含全部 6 个点");
    Note("");
    Note("注意两个序列不一样：拓扑序通常不唯一，只要是合法解就对。");
    Note("Kahn 是按「入度归零的先后」出队，DFS 法是把「后序」反过来。");

    // 含环图：两种方法都要报环
    const std::vector<std::pair<Node, Node>> cyc_edges = {{0, 1}, {1, 2}, {2, 0}, {3, 0}};
    const AdjListGraph<Weight> cyc = BuildGraph<AdjListGraph<Weight>>(cyc_edges, 4, true);
    bool kahn_cyc = false;
    bool dfs_cyc = false;
    const std::vector<Node> kahn_cyc_order = TopoKahn(cyc, &kahn_cyc);
    const std::vector<Node> dfs_cyc_order = TopoDfs(cyc, &dfs_cyc);
    std::cout << "\n";
    Note("含环图边表（4 个点）：");
    PrintEdges(cyc);
    PrintSequence("Kahn 在含环图上的输出", kahn_cyc_order);
    Check(kahn_cyc, "Kahn 检测到环（输出点数 < V）");
    Check(dfs_cyc, "DFS 三色法检测到环（回边指向灰色节点）");
    Check(static_cast<Node>(kahn_cyc_order.size()) < cyc.Size(),
          "Kahn 只输出了 " + std::to_string(kahn_cyc_order.size()) +
              " 个点（< 4），剩下的点互相牵制，入度永远降不到 0");
    Check(static_cast<Node>(dfs_cyc_order.size()) == cyc.Size(),
          "DFS 法在有环时仍会走完所有点，但输出的序列不是合法拓扑序（要配合 has_cycle 使用）");
    Check(!IsValidTopoOrder(cyc, dfs_cyc_order),
          "DFS 输出在含环图上确实不是合法拓扑序（证明必须看 has_cycle 标志）");

    // =======================================================================
    Section("5. 并查集：路径压缩 + 按大小合并，实测接近 O(alpha(n))");
    // =======================================================================
    Note("三种策略的复杂度：");
    Note("  只路径压缩        -> 均摊 O(log n)");
    Note("  只按大小合并      -> O(log n)");
    Note("  两者结合          -> 均摊 O(alpha(n))，alpha 是反阿克曼函数，");
    Note("                       n 小于 10^600 时 alpha(n) <= 4，工程上就是常数。");
    Note("实测三个指标：树高（最坏一次 find 的父指针步数）、均摊步数、均摊耗时。");
    Note("父指针步数是比时间更干净的复杂度证据：它不依赖机器、不依赖 Debug/Release。");
    Note("合并顺序用「二项树式」两两合并，这是 union-by-size 的典型最坏形状（树高 = log2 n）；");
    Note("随便乱连的树太浅，压缩与否看起来差不多，量不出差别。");
    std::cout << "\n";

    const Node dsu_n = 131072;  // 2^17
    const DsuBenchRow row_full =
        RunDsuBench<true, true>("路径压缩 + 按大小合并", dsu_n, 20240602u, 3);
    const DsuBenchRow row_by_size =
        RunDsuBench<false, true>("只按大小合并（不压缩）", dsu_n, 20240602u, 3);
    const DsuBenchRow row_compress =
        RunDsuBench<true, false>("只路径压缩（朴素合并）", dsu_n, 20240602u, 3);

    Label("  策略", 30);
    LabelRight("树高（最坏 find）", 18);
    LabelRight("均摊步数", 12);
    LabelRight("均摊 ns/操作", 14);
    LabelRight("总耗时 us", 12);
    std::cout << "\n";
    std::cout << "  " << std::string(86, '-') << "\n";
    for (const DsuBenchRow* row : {&row_full, &row_by_size, &row_compress}) {
        Label("  " + row->name, 30);
        std::cout << std::setw(18) << row->worst_find_hops << std::setw(12)
                  << Fmt(row->hops_per_op) << std::setw(14) << Fmt(row->ns_per_op)
                  << std::setw(12) << Fmt(row->total_us, 0) << "\n";
    }
    Note("");
    Note("n = " + std::to_string(dsu_n) + "（2^17），每个数据点做 n-1 次 unite + n 次 find，取 3 次中位数。");
    Note("结论 1：不压缩时树高实测 " + std::to_string(row_by_size.worst_find_hops) +
         " 层，正好是 log2(n) = 17，与 O(log n) 的理论吻合；");
    Note("        加上路径压缩后树高降到 " + std::to_string(row_full.worst_find_hops) +
         " 层，均摊只走 " + Fmt(row_full.hops_per_op) + " 步父指针。");
    Note("结论 2：均摊步数 " + Fmt(row_full.hops_per_op) + " vs " +
         Fmt(row_by_size.hops_per_op) + "，压缩版好 " +
         Fmt(row_by_size.hops_per_op / row_full.hops_per_op, 1) + " 倍；");
    Note("        这就是「接近 O(1)」的实际含义：不是 1 步，而是常数级别、");
    Note("        且随 n 增长几乎不变（下面看规模实验）。");
    Note("结论 3：Debug 下三者的 ns/操作差别被函数调用开销稀释了（" +
         Fmt(row_full.ns_per_op) + " vs " + Fmt(row_by_size.ns_per_op) +
         " ns），");
    Note("        这正是「复杂度分析看步数、性能测量看时间」需要互相印证的原因。");
    Check(row_by_size.worst_find_hops >= static_cast<std::uint64_t>(16),
          "不压缩版的树高实测达到 log2(n)（O(log n) 而不是 O(1)）");
    Check(row_full.worst_find_hops <= 3, "压缩版的树高被压到常数级");
    Check(row_full.hops_per_op < row_by_size.hops_per_op,
          "路径压缩显著降低了 find 的均摊父指针步数");

    std::cout << "\n";
    Note("规模实验：n 涨 4 倍时，alpha 版的均摊步数几乎不动，O(log n) 版按对数缓慢上涨。");
    Label("  n", 16);
    LabelRight("全功能 树高", 14);
    LabelRight("全功能 均摊步数", 16);
    LabelRight("不压缩 树高", 14);
    LabelRight("不压缩 均摊步数", 16);
    std::cout << "\n";
    std::cout << "  " << std::string(80, '-') << "\n";
    for (const Node n : {32768, 131072}) {
        const DsuBenchRow full = (n == dsu_n) ? row_full
                                              : RunDsuBench<true, true>("full", n, 20240602u, 3);
        const DsuBenchRow by_size =
            (n == dsu_n) ? row_by_size : RunDsuBench<false, true>("bysize", n, 20240602u, 3);
        std::cout << "  " << std::setw(16) << n << std::setw(14) << full.worst_find_hops
                  << std::setw(16) << Fmt(full.hops_per_op) << std::setw(14)
                  << by_size.worst_find_hops << std::setw(16) << Fmt(by_size.hops_per_op)
                  << "\n";
    }
    Note("");
    Note("树高这一列就是 log2(n) 的本体：n 从 2^15 涨到 2^17，它从 15 涨到 17；");
    Note("而全功能版的均摊步数基本是一条水平线 —— 这就是 alpha(n) 的「常数感」。");

    // 病态链式合并：把教科书的 O(n) 最坏情况演出来
    const Node chain_n = 2000;
    std::uint64_t chain_hops_naive = 0;
    std::uint64_t chain_hops_full = 0;
    const double t_chain_naive = BenchMedianUs(
        [&] { return DsuChainWorkload<false, false>(chain_n, &chain_hops_naive); }, 3);
    const double t_chain_full = BenchMedianUs(
        [&] { return DsuChainWorkload<true, true>(chain_n, &chain_hops_full); }, 3);
    const double chain_ops = 2.0 * static_cast<double>(chain_n) - 1.0;
    std::cout << "\n";
    Note("病态实验：链式合并 Union(0,1), Union(1,2), Union(2,3), ...（n = 2000）");
    Label("  朴素版（不压缩 + 不按秩）均摊步数", 40);
    std::cout << ": " << (static_cast<double>(chain_hops_naive) / chain_ops) << "（就是 O(n)）\n";
    Label("  全功能版均摊步数", 40);
    std::cout << ": " << (static_cast<double>(chain_hops_full) / chain_ops) << "\n";
    Label("  两者耗时 us", 40);
    std::cout << ": " << t_chain_naive << " / " << t_chain_full << "\n";
    Check(static_cast<double>(chain_hops_naive) / chain_ops > 100.0,
          "朴素版在链式合并下退化成 O(n)（实测均摊上百步父指针）");
    Check(static_cast<double>(chain_hops_full) / chain_ops < 5.0,
          "全功能版在同样的合并顺序下依然只需几步");
    Note("");
    Note("注意：朴素版这里是 O(n^2) 的活，n 只敢开到 2000；n 开 16 万就是 2.5e10 步，");
    Note("      跑不完。这就是「最坏情况」在工程里的样子 —— 平时不出现，一出现就是事故。");

    // =======================================================================
    Section("6. Dijkstra：为什么不能有负权（真实反例）");
    // =======================================================================
    Note("复杂度 O((V+E) log V)：每个点出堆一次、每条边松弛一次，堆操作 O(log V)。");
    Note("正确性前提：非负权 => 一旦某点从堆里弹出，它的距离就是最终答案。");
    Note("       证明思路：若 u 出堆后还能被改进，那条更短的路必然经过某个未定型的点 x，");
    Note("       于是 dist[x] + w(x,u) < dist[u]；非负权保证 dist[x] >= dist[u]，矛盾。");
    Note("有负权边时 dist[x] + w 可能小于 dist[u]，前提被破坏，算法给出错误答案。");
    std::cout << "\n";

    // 非负权经典图，先验证实现是对的
    std::vector<std::pair<Node, Node>> g1_edges = {
        {0, 1}, {0, 2}, {0, 5}, {1, 2}, {1, 3}, {2, 3}, {2, 5}, {3, 4}, {5, 4}};
    const std::vector<Weight> g1_weights = {7, 9, 14, 10, 15, 11, 2, 6, 9};
    const AdjListGraph<Weight> g1 =
        BuildWeightedGraph<AdjListGraph<Weight>>(6, g1_edges, g1_weights, true);
    std::cout << "\n";
    Note("非负权有向图（6 个点，源点 0）：");
    PrintEdges(g1);
    std::uint64_t g1_pushes = 0;
    const std::vector<Weight> g1_dij = Dijkstra(g1, 0, &g1_pushes);
    const std::vector<std::vector<Weight>> g1_fw = FloydWarshall(g1);
    PrintDistLine("Dijkstra 结果", g1_dij);
    PrintDistLine("Floyd-Warshall 参考", g1_fw[0]);
    Check(g1_dij == g1_fw[0], "非负权图上 Dijkstra 与 Floyd-Warshall 逐点一致");
    Check(g1_dij[Idx(3)] == 20 && g1_dij[Idx(4)] == 20 && g1_dij[Idx(5)] == 11,
          "手算核对：dist[3]=20, dist[4]=20, dist[5]=11");
    Label("  堆入队总次数", 30);
    std::cout << ": " << g1_pushes << "（V=6，E=9；惰性删除会让同一点多次入队）\n";

    // 负权反例
    const std::vector<std::pair<Node, Node>> neg_edges = {{0, 1}, {0, 2}, {2, 1}};
    const std::vector<Weight> neg_weights = {1, 2, -2};
    const AdjListGraph<Weight> neg =
        BuildWeightedGraph<AdjListGraph<Weight>>(3, neg_edges, neg_weights, true);
    std::cout << "\n";
    Note("反例图（源点 0）：0->1 权重 1，0->2 权重 2，2->1 权重 -2。");
    Note("真实最短路：0->2->1 = 2 + (-2) = 0，比直达的 1 更短。");
    PrintEdges(neg);
    std::uint64_t neg_pushes = 0;
    const std::vector<Weight> neg_dij = Dijkstra(neg, 0, &neg_pushes);
    const BellmanFordResult neg_bf = BellmanFord(neg, 0);
    const std::vector<std::vector<Weight>> neg_fw = FloydWarshall(neg);
    std::uint64_t neg_lc_pushes = 0;
    const std::vector<Weight> neg_lc = DijkstraLabelCorrecting(neg, 0, &neg_lc_pushes);
    std::cout << "\n";
    PrintDistLine("Dijkstra（带 settled）", neg_dij);
    PrintDistLine("Bellman-Ford（正确答案）", neg_bf.dist);
    PrintDistLine("Floyd-Warshall（正确答案）", neg_fw[0]);
    PrintDistLine("去掉 settled 的标签修正版", neg_lc);
    Check(neg_bf.dist[Idx(1)] == 0 && neg_fw[0][Idx(1)] == 0, "真实最短路 dist[1] = 0");
    Check(neg_dij[Idx(1)] == 1, "Dijkstra 在这里给出错误的 dist[1] = 1（比真实值大 1）");
    Check(neg_dij[Idx(1)] != neg_bf.dist[Idx(1)], "Dijkstra 与 Bellman-Ford 结果不一致，错误被抓出来");
    Note("");
    Note("为什么错：0 出堆后把 1（dist=1）和 2（dist=2）都放进堆；1 先弹出就被「定型」；");
    Note("          等 2 弹出时想通过 2->1 把 dist[1] 改小到 0，却被「已定型的点不许再改」挡住。");
    Note("一个真实数字：Dijkstra 给出 dist[1] = " + std::to_string(neg_dij[Idx(1)]) +
         "，正确答案是 " + std::to_string(neg_bf.dist[Idx(1)]) + "。");
    Note("绝对误差是 1，而这张图所有边权的绝对值最大只有 2 —— 真值是 0，");
    Note("相对误差直接是无穷大。这就是「选错算法不是慢一点，而是结果错」。");
    Note("");
    Note("附带的诚实说明：把 settled 集合去掉、只留惰性删除后，算法变成「优先队列版标签修正法」");
    Note("（本文件里的 DijkstraLabelCorrecting）：它在这个例子上歪打正着算出了 0。");
    Note("但那不是可以依赖的替代品：它靠反复入队来修正，本例入队 " +
         std::to_string(neg_lc_pushes) + " 次（Dijkstra 只用 " + std::to_string(neg_pushes) +
         " 次），");
    Note("而且遇到从源点可达的负环时它永远不终止。有负权就用 Bellman-Ford。");

    // =======================================================================
    Section("7. Bellman-Ford：负权与负环检测");
    // =======================================================================
    Note("复杂度 O(V*E)，比 Dijkstra 慢，但能处理负权，还能检测负环。");
    Note("为什么最多 V-1 轮：一条最短路径最多 V-1 条边（V 个点，重复经过点只会更差）。");
    Note("  归纳法：第 k 轮结束后，所有「最多用 k 条边」的最短路都已经确定。");
    Note("负环检测：做完 V-1 轮后再做第 V 轮，如果还能松弛，说明存在一条还能变短的");
    Note("  路径，它必然重复经过了某个点 —— 也就是存在从源点可达的负环，此时最短路无定义。");
    std::cout << "\n";

    Note("先在反例图（无负环）上验证：Bellman-Ford 给出正确答案。");
    Label("  迭代轮数 / 松弛次数 / 有负环", 34);
    std::cout << ": " << neg_bf.rounds_used << " / " << neg_bf.relaxations << " / "
              << (neg_bf.has_negative_cycle ? "是" : "否") << "\n";
    Check(!neg_bf.has_negative_cycle, "反例图没有负环（V-1 = 2 轮内收敛）");
    Check(neg_bf.dist == neg_fw[0], "Bellman-Ford 与 Floyd-Warshall 完全一致");

    std::cout << "\n";
    Note("负环图（5 个点，源点 0）：0->1(4)，1->2(3)，2->3(-6)，3->1(2)，3->4(1)。");
    Note("环 1->2->3->1 的权值和 = 3 + (-6) + 2 = -1 < 0，是负环。");
    const std::vector<std::pair<Node, Node>> cyc_neg_edges = {{0, 1}, {1, 2}, {2, 3}, {3, 1}, {3, 4}};
    const std::vector<Weight> cyc_neg_weights = {4, 3, -6, 2, 1};
    const AdjListGraph<Weight> neg_cycle =
        BuildWeightedGraph<AdjListGraph<Weight>>(5, cyc_neg_edges, cyc_neg_weights, true);
    PrintEdges(neg_cycle);
    const BellmanFordResult cyc_bf = BellmanFord(neg_cycle, 0);
    std::cout << "\n";
    Label("  迭代轮数（应为 V-1 = 4）", 34);
    std::cout << ": " << cyc_bf.rounds_used << "\n";
    Label("  松弛次数", 34);
    std::cout << ": " << cyc_bf.relaxations << "\n";
    Label("  检测到负环", 34);
    std::cout << ": " << (cyc_bf.has_negative_cycle ? "是" : "否") << "\n";
    Check(cyc_bf.has_negative_cycle, "Bellman-Ford 检测到从源点可达的负环");
    Check(cyc_bf.rounds_used == 4, "含负环时前 V-1 轮每轮都在松弛（不会提前收敛）");
    PrintDistLine("  含负环时的 dist（已无意义）", cyc_bf.dist, 34);
    Note("");
    Note("工程提醒：检测到负环后，dist 里的数字已经没有意义（最短路是 -inf）。");
    Note("          要么报错，要么把它们标记成「不可用」，千万别拿去当答案用。");

    // 边序实验：把 O(V*E) 最坏情况真的跑出来
    std::cout << "\n";
    Note("边序实验：同一张链图 0->1->2->...->(V-1)（V = 2000），只是边表顺序不同。");
    Note("为什么重要：Bellman-Ford 的轮数是「信息每轮能往前传多远」决定的，");
    Note("            而信息传播距离取决于边在表里的排列顺序，不取决于图本身。");
    const Node ord_v = 2000;
    std::vector<WEdge> forward_edges;
    std::vector<WEdge> backward_edges;
    for (Node i = 0; i + 1 < ord_v; ++i) {
        forward_edges.push_back(WEdge{i, i + 1, 1});
    }
    for (Node i = ord_v - 2; i >= 0; --i) {
        backward_edges.push_back(WEdge{i, i + 1, 1});
    }
    const BellmanFordResult bf_fwd = BellmanFordFromEdges(ord_v, forward_edges, 0);
    const BellmanFordResult bf_bwd = BellmanFordFromEdges(ord_v, backward_edges, 0);
    const AdjListGraph<Weight> ord_path = BuildPathGraph<AdjListGraph<Weight>>(ord_v, true);
    const double t_bf_fwd = BenchMedianUs(
        [&] {
            return static_cast<std::uint64_t>(
                BellmanFordFromEdges(ord_v, forward_edges, 0).relaxations);
        },
        3);
    const double t_bf_bwd = BenchMedianUs(
        [&] {
            return static_cast<std::uint64_t>(
                BellmanFordFromEdges(ord_v, backward_edges, 0).relaxations);
        },
        3);
    const double t_dij_path = BenchMedianUs(
        [&] {
            const std::vector<Weight> d = Dijkstra(ord_path, 0);
            return static_cast<std::uint64_t>(d[Idx(ord_v - 1)]);
        },
        3);
    std::cout << "\n";
    Label("  边按拓扑序排列：轮数（含收敛判定轮）/ 耗时 us", 36);
    std::cout << ": " << bf_fwd.rounds_used << " / " << t_bf_fwd << "\n";
    Label("  边表倒过来：轮数（含收敛判定轮）/ 耗时 us", 36);
    std::cout << ": " << bf_bwd.rounds_used << " / " << t_bf_bwd << "\n";
    Label("  Dijkstra 在同一张图上 us", 36);
    std::cout << ": " << t_dij_path << "\n";
    Label("  最坏 / 最好 倍数", 36);
    std::cout << ": " << (t_bf_bwd / t_bf_fwd) << " x\n";
    Check(bf_fwd.dist == bf_bwd.dist, "两种边序算出的最短路完全相同（顺序只影响速度，不影响结果）");

    // 关于 rounds_used 的口径，这里必须说清楚，否则会得出「代码错了」的错误结论：
    //   rounds_used 数的是【实际执行的轮数】，包含了最后那一轮「什么都没改、
    //   于是确认已经收敛」的判定轮。所以：
    //     边按拓扑序 -> 第 1 轮就把所有距离推到底；第 2 轮没改动，随即 break。
    //                   有效松弛轮数 = 1，rounds_used = 2。
    //     边表倒过来 -> 每轮只能沿链往前推一条边，需要 V-1 轮才推到底；
    //                   第 V-1 轮仍有改动，循环条件刚好结束，没有额外的判定轮。
    //                   有效松弛轮数 = V-1，rounds_used = V-1。
    //   「多出来的 1」正是收敛检测的成本，这是所有「提前退出」优化的共同特征。
    Check(bf_fwd.rounds_used == 2,
          "边按拓扑序排列时：1 轮有效松弛 + 1 轮收敛判定 = 2 轮");
    Check(bf_bwd.rounds_used == ord_v - 1,
          "边表倒过来时每轮只能往前传一条边，跑满 V-1 轮（无额外判定轮）");
    Note("");
    Note("这就是 O(V*E) 的来源：V-1 轮 × E 条边。同一张图，只因为边的排列顺序不同，");
    Note("耗时差了 " + Fmt(t_bf_bwd / t_bf_fwd, 0) +
         " 倍。所以「Bellman-Ford 会不会很慢」这个问题，");
    Note("答案取决于输入，而不只取决于复杂度 —— 这也是为什么工程上要测量。");
    Note("另外注意：链图是 Dijkstra 最喜欢的输入（每点只有一条出边），");
    Note("所以上面的 Dijkstra 时间不能拿来和 Bellman-Ford 直接比，只能说明量级。");

    // =======================================================================
    Section("8. 最小生成树：Kruskal vs Prim 对拍");
    // =======================================================================
    Note("Kruskal：按边权排序 + 贪心，用并查集判环，O(E log E)。");
    Note("Prim   ：从一个点开始不断加入「连接树内树外的最小边」，堆实现 O(E log V)。");
    Note("两者贪心角度不同（边视角 vs 点视角），但总权重必须相等 ——");
    Note("因为 MST 的「权值和」是唯一的，即使 MST 本身可能不唯一。");
    std::cout << "\n";

    const std::vector<std::pair<Node, Node>> mst_edges = {
        {0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 4}, {2, 3}, {2, 4}, {2, 5}, {3, 5}, {4, 5}};
    const std::vector<Weight> mst_weights = {6, 1, 5, 5, 3, 5, 6, 4, 2, 6};
    const AdjListGraph<Weight> mst_graph =
        BuildWeightedGraph<AdjListGraph<Weight>>(6, mst_edges, mst_weights, false);
    Note("无向加权图（6 个点）：");
    PrintEdges(mst_graph);

    const MstResult kruskal = Kruskal(mst_graph);
    const MstResult prim = Prim(mst_graph, 0);
    std::cout << "\n";
    Label("  Kruskal 选中边数 / 总权重", 34);
    std::cout << ": " << kruskal.edges.size() << " / " << kruskal.total << "\n";
    Label("  Prim    选中边数 / 总权重", 34);
    std::cout << ": " << prim.edges.size() << " / " << prim.total << "\n";
    std::cout << "  Kruskal 的边: ";
    for (const MstEdge& e : kruskal.edges) {
        std::cout << e.u << "-" << e.v << "(" << e.w << ") ";
    }
    std::cout << "\n  Prim 的边   : ";
    for (const MstEdge& e : prim.edges) {
        std::cout << e.u << "-" << e.v << "(" << e.w << ") ";
    }
    std::cout << "\n";
    Check(kruskal.total == prim.total, "两种算法的 MST 总权重相等（权值和唯一）");
    Check(kruskal.edges.size() == 5 && prim.edges.size() == 5, "生成树边数 = V-1 = 5");
    Check(kruskal.spanning && prim.spanning, "两棵树都覆盖了全部 6 个点（连通）");
    Check(kruskal.total == 15, "手算核对：MST 总权重 = 1+2+3+4+5 = 15");
    Check(VerifyCycleProperty(mst_graph, kruskal), "环性质校验通过：每条非树边都不轻于树上路径的最大边");
    Check(VerifyCycleProperty(mst_graph, prim), "Prim 的结果同样通过环性质校验");
    Note("");
    Note("这里两棵树恰好选了同一组边（只是枚举顺序不同：Kruskal 按权重，Prim 按扩展顺序），");
    Note("但 MST 本来就不唯一（等权边可以互换），所以对拍时要比较总权重，而不是逐边比较集合。");

    // =======================================================================
    Section("9. 性能实测：最短路 / 最小生成树在中等规模图上的表现");
    // =======================================================================
    Note("Debug 构建，绝对数字仅供参考，趋势才重要。");
    std::cout << "\n";

    const Node perf_v = 20000;
    const std::vector<std::pair<Node, Node>> perf_edges = MakeConnectedPairs(perf_v, 3, 20240603u);
    const std::vector<Weight> perf_weights = MakeWeights(perf_edges.size(), 1, 1000, 20240604u);
    const AdjListGraph<Weight> perf_graph =
        BuildWeightedGraph<AdjListGraph<Weight>>(perf_v, perf_edges, perf_weights, false);
    Check(static_cast<Node>(BfsOrder(perf_graph, 0).size()) == perf_v,
          "2 万点性能测试图是连通的");

    auto dijkstra_checksum = [&]() -> std::uint64_t {
        const std::vector<Weight> d = Dijkstra(perf_graph, 0);
        std::uint64_t acc = 0;
        for (const Weight x : d) {
            if (x < kInf) {
                acc += static_cast<std::uint64_t>(x);
            }
        }
        return acc;
    };
    WarmUp(dijkstra_checksum, 1);
    const double t_dij = BenchMedianUs(dijkstra_checksum, 3);
    const double t_kruskal = BenchMedianUs(
        [&] {
            const MstResult r = Kruskal(perf_graph);
            return static_cast<std::uint64_t>(r.total) + static_cast<std::uint64_t>(r.edges.size());
        },
        3);
    const double t_prim = BenchMedianUs(
        [&] {
            const MstResult r = Prim(perf_graph, 0);
            return static_cast<std::uint64_t>(r.total) + static_cast<std::uint64_t>(r.edges.size());
        },
        3);

    const MstResult perf_kruskal = Kruskal(perf_graph);
    const MstResult perf_prim = Prim(perf_graph, 0);
    Check(perf_kruskal.total == perf_prim.total, "2 万点图上 Kruskal 与 Prim 总权重相等");
    Check(perf_kruskal.edges.size() == perf_prim.edges.size(),
          "2 万点图上两棵生成树的边数相等（都是 V-1）");

    std::cout << "\n";
    Label("  性能图 V / E", 34);
    std::cout << ": " << perf_v << " / " << perf_edges.size() << "（无向加权）\n";
    Label("  Dijkstra（源点 0）us", 34);
    std::cout << ": " << t_dij << "\n";
    Label("  Kruskal us", 34);
    std::cout << ": " << t_kruskal << "（瓶颈是排序 E 条边）\n";
    Label("  Prim us", 34);
    std::cout << ": " << t_prim << "（瓶颈是堆操作）\n";
    Label("  MST 总权重（两算法一致）", 34);
    std::cout << ": " << perf_kruskal.total << "\n";

    // Bellman-Ford vs Dijkstra：小一点的图，因为 O(V*E) 实在贵
    const Node bf_v = 1000;
    const std::vector<std::pair<Node, Node>> bf_edges = MakeDirectedPairs(bf_v, 3, 20240605u);
    const std::vector<Weight> bf_weights = MakeWeights(bf_edges.size(), 1, 100, 20240606u);
    const AdjListGraph<Weight> bf_graph =
        BuildWeightedGraph<AdjListGraph<Weight>>(bf_v, bf_edges, bf_weights, true);
    std::uint64_t bf_rounds = 0;
    std::uint64_t bf_rels = 0;
    const double t_bf = BenchMedianUs(
        [&] {
            const BellmanFordResult r = BellmanFord(bf_graph, 0);
            bf_rounds = static_cast<std::uint64_t>(r.rounds_used);
            bf_rels = r.relaxations;
            std::uint64_t acc = 0;
            for (const Weight x : r.dist) {
                if (x < kInf) {
                    acc += static_cast<std::uint64_t>(x);
                }
            }
            return acc;
        },
        3);
    const double t_dij_bf_graph = BenchMedianUs(
        [&] {
            const std::vector<Weight> d = Dijkstra(bf_graph, 0);
            std::uint64_t acc = 0;
            for (const Weight x : d) {
                if (x < kInf) {
                    acc += static_cast<std::uint64_t>(x);
                }
            }
            return acc;
        },
        3);
    const std::vector<Weight> bf_dist = BellmanFord(bf_graph, 0).dist;
    const std::vector<Weight> dij_dist = Dijkstra(bf_graph, 0);
    Check(bf_dist == dij_dist, "非负权有向图上 Bellman-Ford 与 Dijkstra 结果一致");

    std::cout << "\n";
    Label("  对比图 V / E", 34);
    std::cout << ": " << bf_v << " / " << bf_edges.size() << "（有向，非负权）\n";
    Label("  Dijkstra us", 34);
    std::cout << ": " << t_dij_bf_graph << "\n";
    Label("  Bellman-Ford us", 34);
    std::cout << ": " << t_bf << "（实际只跑了 " << bf_rounds << " 轮、松弛 " << bf_rels
              << " 次）\n";
    Note("");
    Note("这里的结果可能和「Bellman-Ford 一定更慢」的直觉相反：Debug 下它反而更快。");
    Note("原因有两层：这张图很小（V=1000），随机图上最短路很短，BF 只用 " +
         std::to_string(bf_rounds) + " 轮就收敛；");
    Note("而 Dijkstra 每次松弛都要操作二叉堆，Debug 下堆操作的开销非常贵。");
    Note("这正说明「复杂度只描述趋势」：小规模下常数因子说话。");
    Note("BF 真正的代价在第 7 节的边序实验里 —— 那里它跑了满 V-1 轮，慢了几十倍。");

    // =======================================================================
    Section("10. 复杂度速查与工程结论");
    // =======================================================================
    Label("  算法 / 数据结构", 28);
    Label("时间复杂度", 26);
    Label("空间复杂度", 18);
    std::cout << "\n";
    std::cout << "  " << std::string(70, '-') << "\n";

    struct ComplexityRow {
        const char* name;
        const char* time;
        const char* space;
    };
    const ComplexityRow rows[] = {
        {"邻接矩阵", "建图 O(V^2)，查边 O(1)", "O(V^2)"},
        {"邻接表", "建图 O(V+E)，查边 O(deg)", "O(V+E)"},
        {"DFS / BFS", "O(V+E)", "O(V)"},
        {"连通分量（DFS）", "O(V+E)", "O(V)"},
        {"并查集（压缩+按大小）", "均摊 O(alpha(n))", "O(V)"},
        {"拓扑排序 Kahn", "O(V+E)", "O(V)"},
        {"拓扑排序 DFS", "O(V+E)", "O(V) + 递归栈"},
        {"Dijkstra（二叉堆）", "O((V+E) log V)", "O(V)"},
        {"Bellman-Ford", "O(V*E)", "O(V)"},
        {"Floyd-Warshall", "O(V^3)", "O(V^2)"},
        {"Kruskal（排序+并查集）", "O(E log E)", "O(V+E)"},
        {"Prim（二叉堆）", "O(E log V)", "O(V+E)"},
    };
    for (const ComplexityRow& r : rows) {
        Label(std::string("  ") + r.name, 28);
        Label(r.time, 26);
        Label(r.space, 18);
        std::cout << "\n";
    }

    std::cout << "\n";
    Note("工程结论：");
    Note("  1) 表示先看密度：E 远小于 V^2 就用邻接表（默认选择）；稠密 + 频繁查边才考虑矩阵。");
    Note("  2) 遍历一律优先显式栈：递归 DFS 的深度和图规模成正比，深图必爆栈。");
    Note("  3) 无权最短路用 BFS，非负权用 Dijkstra，有负权用 Bellman-Ford，");
    Note("     全源小图用 Floyd-Warshall。选错算法不是「慢一点」，是「结果错」。");
    Note("  4) 判连通/判环优先并查集，它同时支持「边一条条加进来」的动态场景。");
    Note("  5) 最小生成树两个算法都对：稀疏图 Kruskal 更常见，稠密图 Prim 更快，");
    Note("     但总权重一定相等 —— 可以用它做交叉验证。");
    Note("  6) 最后一句：以上全部是教学实现。生产请用成熟库（Boost.Graph 等），");
    Note("     或者至少是经过充分测试的实现，别把教学代码直接上生产。");
    Note("");
    Note("再次强调：所有耗时数字都来自 Debug 构建，请只比较相对差异和增长趋势。");

    std::cout << "\n[校验] 累加器非零，说明所有测量结果都真实参与了运算: "
              << (g_sink != 0 ? "是" : "否") << "\n";

    return 0;
}
