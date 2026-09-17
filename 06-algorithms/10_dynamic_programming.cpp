// ============================================================================
// 10_dynamic_programming.cpp
// 演示主题：
//   1. 动态规划的三步演进：朴素递归 -> 记忆化搜索（自顶向下）-> 递推（自底向上）
//   2. 「重叠子问题」才是记忆化能救命的根本原因：调用次数对比比耗时更直观
//   3. 朴素递归的实测曲线，以及按实测增长率外推 n = 40（外推值，不是实测值）
//   4. 0-1 背包 / 完全背包：二维写法、一维滚动数组，以及「倒序 vs 正序」这个最经典的坑
//   5. 最长公共子序列 LCS：二维转移 + 回溯重建出一个具体答案
//   6. 最长递增子序列 LIS：O(n^2) DP 与 O(n log n) 贪心 + 二分（tails + lower_bound）
//   7. 编辑距离（Levenshtein）：插入 / 删除 / 替换分别对应转移矩阵的三个方向
//   8. 打家劫舍：选与不选的转移，以及 O(1) 空间优化
//   9. 零钱兑换：最少硬币数与方案数两个变体，以及方案数为什么必须外层遍历硬币
//  10. 怎么识别一道题该用 DP：最优子结构 / 重叠子问题 / 无后效性
//
// 关键结论：
//   1. 记忆化搜索 = 朴素递归 + 一张缓存表，把 O(phi^n) 压成 O(n)。斐波那契 n = 32 时
//      朴素递归要调用 7049155 次函数，记忆化只进入 63 次（2n-1）、其中真正求解的
//      子问题只有 33 个（n+1），比值约 11 万倍——这不是更快的机器省下来的，
//      是「不重复计算」省下来的；
//   2. 朴素递归的耗时随 n 每增加 1 就乘约 1.618（phi）。n = 40 时函数调用次数约
//      3.31e8（三亿次），本文件只做外推、不做实测——三亿次调用本身就是浪费，而且
//      外推值的绝对值取决于机器（本机 Debug 实测单次调用约 1 到 1.5 ns，外推约 0.4 到
//      0.7 秒；换台慢机器或加了检查工具的环境就是几十秒到几分钟）。机器无关的事实
//      是调用次数；
//   3. 0-1 背包的一维写法必须倒序遍历容量。故意写成正序会得到完全背包的答案：
//      本文件的实验里正确值是 10，正序写法的错误值是 12（它把同一件物品 A(重 2,价 3)
//      连放了 4 次），而这个 12 恰好等于完全背包的正确答案——所以「正序 = 完全背包」
//      不是巧合，是可证伪也可证实的等价关系（代码里有 assert 直接对拍）；
//   4. 空间优化不只是省内存，通常还更快：一维滚动数组的元素连续、缓存命中率高，
//      在 Debug 下实测 0-1 背包一维写法比二维写法快约 2 到 4 倍（见第三节表格）；
//   5. LIS 的 O(n log n) 解法不是「DP」，是贪心 + 二分：tails[k] 表示长度为 k+1 的
//      递增子序列的最小结尾元素。严格递增用 lower_bound，非递减必须换成 upper_bound
//      （序列 {2,2,2,2} 上两者分别给出 1 和 4，差 4 倍）；
//   6. 零钱兑换求方案数时外层必须遍历硬币：外层金额会得到排列数（金额 5、硬币
//      {1,2,5} 时组合数是 4，排列数是 9）。
//
// 教学用途提示：本文件是为了讲清「状态定义 -> 转移 -> 边界 -> 空间优化」这条思路而写的
//             教学代码。生产代码请优先用经过验证的库 / 模板（例如 std::string 的编辑距离
//             可以用成熟的文本库），并且先想清楚状态定义再写代码——DP 写错的代价是静默的
//             错误答案，而它跑得又快又稳，测试不覆盖就发现不了。
//
// 说明：本文件在 Debug（/Od）下编译运行。Debug 的绝对数字比 Release 慢很多，
//       这里所有数字仅供参考，看「趋势」和「数量级差异」才有意义。
// ============================================================================

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// 防止测量结果被优化掉
// ---------------------------------------------------------------------------
// 编译器（尤其 Release 的 /O2）发现一个纯计算的返回值没人用，会直接把整段代码删掉，
// 于是你测出来的是「0 毫秒」——这不是算法快，是你的代码根本没跑。
// 解决办法：把结果写进一个 volatile 全局变量。volatile 强制产生真实的读写副作用，
// 编译器不能假设它没用，也就不能删掉计算过程。
//
// 注意 C++20 起 volatile 的复合赋值（g_sink += r）和 ++/-- 已被弃用（P1152），
// 所以下面坚持写成「先读、再算、再写」的展开形式。
volatile std::uint64_t g_sink = 0;

// ---------------------------------------------------------------------------
// 打印小工具
// ---------------------------------------------------------------------------
void Section(const std::string& title) {
    std::cout << "\n============================================================\n";
    std::cout << title << "\n";
    std::cout << "============================================================\n";
}

void Note(const std::string& text) { std::cout << "  " << text << "\n"; }

// 估算字符串在终端里占的「显示列数」。
// 这里有个必踩的坑：std::setw 数的是 char 的个数，而一个中文字符在 UTF-8 里占 3 个
// 字节、在终端里显示为 2 列。所以直接用 setw 去对齐含中文的表头和数字，结果一定是歪的。
// 自己算显示宽度、自己补空格，表格才是齐的。
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

// 左对齐打印标签并补空格到指定显示宽度，这样后面的数字就能对齐到同一列。
void Label(const std::string& text, std::size_t total_width) {
    std::cout << text;
    const std::size_t w = DisplayWidth(text);
    if (w < total_width) {
        std::cout << std::string(total_width - w, ' ');
    }
}

// ---------------------------------------------------------------------------
// 计时核心：重复多次，取中位数
// ---------------------------------------------------------------------------
// 为什么是「中位数」而不是「平均值」？
//   操作系统调度、其他进程抢 CPU、缓存被换出，都会让某一次测量突然变得很慢。
//   平均值会被这些异常值拉高，中位数则对异常值免疫——它回答的是
//   「典型的一次到底要多久」。
//
// 为什么用 steady_clock 而不是 system_clock？
//   steady_clock 是单调时钟，不会被 NTP 校时或用户改系统时间影响；
//   system_clock 表示「墙上时间」，可能在任何时刻向前或向后跳。
//   测「经过了多少时间」永远用 steady_clock。
//
// 返回值单位：微秒（us）。用微秒是因为 O(1) 和 O(log n) 用毫秒会是 0.000x，
// 打印出来全是 0，看不出差别。
template <typename Fn>
double BenchMedianUs(Fn&& fn, int repeats) {
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(repeats));

    for (int i = 0; i < repeats; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        const std::uint64_t result = fn();
        const auto t1 = std::chrono::steady_clock::now();

        // 这一行是「防止被优化掉」的落点：结果必须真正被用掉。
        g_sink = g_sink + result;

        const std::chrono::duration<double, std::micro> dt = t1 - t0;
        samples.push_back(dt.count());
    }

    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];  // 中位数
}

// 预热：第一次调用会带上「缺页中断 + 缓存冷启动 + 分支预测器未训练」的开销。
// 先空跑几遍，让这些一次性成本落在预热里，而不是落在正式测量里。
template <typename Fn>
void WarmUp(Fn&& fn, int times) {
    for (int i = 0; i < times; ++i) {
        g_sink = g_sink + fn();
    }
}

// 把「一次操作」重复 repeat 次并累加结果。
// 为什么需要它：O(1) 和 O(log n) 的工作负载只要几纳秒到几十纳秒，而本机
// steady_clock 的实际分辨率在百纳秒量级——直接测一次，测到的全是噪声。
// 重复 repeat 次只是给总耗时乘了一个常数，不改变增长阶，
// 却能让测量落在一个可信的量级上。这是做性能测量最常用的技巧。
template <typename Fn>
std::uint64_t Repeat(Fn&& fn, int repeat) {
    std::uint64_t acc = 0;
    for (int r = 0; r < repeat; ++r) {
        acc += fn();
    }
    return acc;
}

// ---------------------------------------------------------------------------
// 调用计数器
// ---------------------------------------------------------------------------
// 本节最有说服力的证据不是耗时，而是「同一个子问题被算了多少遍」。
// 耗时受机器、编译器、Debug/Release 影响，调用次数是纯数学事实。
//
// 这里统计两个不同的量，别把它们混为一谈：
//   g_calls  —— 函数【进入】次数（也包括命中缓存的那种进入）；
//   g_solves —— 真正【求解】过的不同子问题个数（只有缓存未命中才算）。
// 记忆化版的差距恰恰藏在这两个数之间：进入 2n-1 次，其中只有 n+1 个是真正的计算。
std::uint64_t g_calls = 0;
std::uint64_t g_solves = 0;

void ResetCalls() noexcept {
    g_calls = 0;
    g_solves = 0;
}

// 造数据
std::vector<int> MakeRandomInts(std::size_t n, int lo, int hi, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<int> v(n);
    const unsigned span = static_cast<unsigned>(hi - lo + 1);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = lo + static_cast<int>(rng() % span);
    }
    return v;
}

// 随机字符串（只用小写字母表，方便控制重复率）。
// 注意这里刻意用 std::string 存 ASCII 字符：LCS / 编辑距离按下标取的是「字节」，
// 如果放中文，一个汉字占 3 字节，dp 表会按字节切分，得到的是「字节级」而不是
// 「字符级」的答案。真要处理 Unicode，请先把字符串拆成码点数组再喂给 DP。
std::string MakeRandomStringLike(std::size_t n, const std::string& alphabet, unsigned seed) {
    std::mt19937 rng(seed);
    std::string s;
    s.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        s.push_back(alphabet[rng() % alphabet.size()]);
    }
    return s;
}

// ===========================================================================
// 第一部分：从递归 -> 记忆化搜索 -> 递推（三步演进）
// 例子一：斐波那契
// ===========================================================================

// --- 第 1 步：朴素递归 -----------------------------------------------------
// 状态定义：fib(n) = 斐波那契数列第 n 项。
// 转移方程：fib(n) = fib(n-1) + fib(n-2)。
// 边界条件：fib(0) = 0，fib(1) = 1。
// 空间优化：谈不上——这个版本连「状态」都没存下来。
// 复杂度：时间 O(phi^n)（phi = (1+sqrt5)/2 ≈ 1.618），因为 T(n) = T(n-1) + T(n-2) + O(1)；
//         空间 O(n)，是递归栈的最大深度（注意：不是 O(2^n)，深度只有 n）。
std::uint64_t FibNaive(int n) {
    if (n < 2) {
        return static_cast<std::uint64_t>(n);
    }
    return FibNaive(n - 1) + FibNaive(n - 2);
}

// 带计数器的版本。计数的代价是每次调用多一次全局变量加法，
// 所以这个版本比上一个略慢——但两者只用来各自回答一个问题：
// 「值对不对」和「算了几次」。
std::uint64_t FibNaiveCounted(int n) {
    g_calls = g_calls + 1;
    if (n < 2) {
        return static_cast<std::uint64_t>(n);
    }
    return FibNaiveCounted(n - 1) + FibNaiveCounted(n - 2);
}

// --- 第 2 步：记忆化搜索（自顶向下 + 缓存表）--------------------------------
// 状态定义：同朴素递归，但每个状态只算一次，结果存进 cache。
// 转移方程：不变（fib(n) = fib(n-1) + fib(n-2)）。
// 边界条件：不变，只是多了「查表命中就直接返回」这一条。
// 空间优化：cache 用 std::vector 而不是 std::unordered_map——
//           状态空间是 0..n 的连续整数，数组下标直接映射，比哈希表快且省。
//           哈希表只在状态是「稀疏、不好当数组下标」时才划算（例如状态是字符串）。
// 复杂度：时间 O(n)（每个状态恰好被真正求解一次），空间 O(n)（表 + 递归栈）。
//
// 为什么记忆化能行？——因为存在「重叠子问题」：
//   朴素递归的递归树里，fib(30) 会被算很多很多遍。缓存把第一次的结果留下，
//   后面所有相同的子问题都变成了一次数组访问。
//
// 关于 Rule of Five：本类的唯一资源是 std::vector 成员，隐式生成的五个特殊成员
// 本来就是正确的；这里仍然显式写全，一是让「可拷贝、可移动」成为接口的一部分，
// 二是防止将来加成员（比如裸指针缓存）时不小心退化成错误的隐式浅拷贝。
class MemoFib {
public:
    explicit MemoFib(std::size_t n) : cache_(n + 1, kUnknown) {}

    MemoFib(const MemoFib&) = default;
    MemoFib& operator=(const MemoFib&) = default;
    MemoFib(MemoFib&&) noexcept = default;
    MemoFib& operator=(MemoFib&&) noexcept = default;
    ~MemoFib() = default;

    std::uint64_t Solve(std::size_t n) {
        g_calls = g_calls + 1;
        // 先查表、再判断边界：让基例走同一条路径，语义更统一。
        // 注意：即使这样写，函数【进入】次数仍然是 2n-1 次——
        // 因为每次递归调用都会进入函数一次（包括命中缓存的那次进入）。
        // 真正被【求解】的是 n+1 个子问题（状态 0..n 各一次），
        // 剩下的 n-2 次进入都是缓存命中，O(1) 返回。
        if (cache_[n] != kUnknown) {
            return cache_[n];  // 命中缓存：这就是省下来的全部时间
        }
        g_solves = g_solves + 1;  // 只有走到这里才算「真正求解了一个子问题」
        const std::uint64_t r = (n < 2) ? static_cast<std::uint64_t>(n)
                                        : Solve(n - 1) + Solve(n - 2);
        cache_[n] = r;
        return r;
    }

    std::size_t CacheBytes() const noexcept { return cache_.size() * sizeof(std::uint64_t); }

private:
    // 哨兵值。能不能用 0 当哨兵？不能：fib(0) = 0 本身就是合法答案，
    // 用 0 当「未计算」会把「答案是 0」误判成「还没算」。这是记忆化最常踩的坑之一。
    // std::uint64_t 的最大值在 n <= 93 的范围内不会成为合法答案，可以安全当哨兵。
    static constexpr std::uint64_t kUnknown = std::numeric_limits<std::uint64_t>::max();
    std::vector<std::uint64_t> cache_;
};

// --- 第 3 步：递推 / 自底向上 ------------------------------------------------
// 状态定义：只要两个「滚动变量」就够：a = F(i-1)，b = F(i)。
// 转移方程：next = a + b，然后整体向前滚动一格。
// 边界条件：a = F(0) = 0，b = F(1) = 1。
// 空间优化：这是空间优化的极端形态——算出 F(n) 只依赖前两项，
//           所以整张 O(n) 的表可以压成 O(1) 的两个变量。
// 复杂度：时间 O(n)，空间 O(1)。
std::uint64_t FibIterative(int n) {
    std::uint64_t a = 0;  // F(0)
    std::uint64_t b = 1;  // F(1)
    for (int i = 0; i < n; ++i) {
        const std::uint64_t next = a + b;
        a = b;
        b = next;
    }
    return a;
}

// 用「加法回绕检测」算出 uint64_t 能表示到第几项。
// 很多资料含糊地说「n = 90 会溢出」，其实 fib(90) = 2880067194370816120，
// 离 uint64_t 的上限 18446744073709551615 还差一个数量级；真正的边界要大得多。
// 与其背下来，不如让程序自己算：一旦 a + b 的结果比 b 还小，就说明无符号加法回绕了。
int MaxFibIndexInUint64() {
    std::uint64_t a = 0;  // F(0)
    std::uint64_t b = 1;  // F(1)
    int n = 1;
    for (;;) {
        const std::uint64_t next = a + b;
        if (next < b) {
            return n;  // F(n) 是最后一个还能用 uint64_t 表示的项
        }
        a = b;
        b = next;
        n += 1;
    }
}

// ===========================================================================
// 第一部分：三步演进的第二个例子——爬楼梯
// ===========================================================================
// 题目：一次可以走 1 阶或 2 阶，走到第 n 阶共有多少种走法？
//
// 状态定义：ways(i) = 走到第 i 阶的走法数。
// 转移方程：ways(i) = ways(i-1) + ways(i-2)。
//           理由：最后一步只有两种可能——从第 i-1 阶迈 1 阶上来，或从第 i-2 阶迈 2 阶上来。
//           这两种情况互斥且完备，所以直接相加（这是「加法原理」）。
// 边界条件：ways(0) = 1（站在地面，什么都不做也算 1 种走法），ways(1) = 1。
// 空间优化：只需要前两项，O(1) 空间。
//
// 它和斐波那契的关系：ways(n) = F(n+1)。同一个递推，换了个故事。
// 为什么还要再讲一遍？因为「把题目翻译成状态」才是 DP 最难的一步，
// 递推式反而是最简单的部分。学会看出「最后一步是什么」就抓住了大半。
std::uint64_t ClimbNaive(int n) {
    g_calls = g_calls + 1;  // 这个版本只用来演示调用次数
    if (n <= 1) {
        return 1;
    }
    return ClimbNaive(n - 1) + ClimbNaive(n - 2);
}

std::uint64_t ClimbMemoRec(int n, std::vector<std::uint64_t>& cache) {
    g_calls = g_calls + 1;
    const std::size_t idx = static_cast<std::size_t>(n);
    if (cache[idx] != 0) {
        return cache[idx];  // 这里可以用 0 当哨兵：走法数永远 >= 1，0 不可能是合法答案
    }
    g_solves = g_solves + 1;
    cache[idx] = (n <= 1) ? 1 : ClimbMemoRec(n - 1, cache) + ClimbMemoRec(n - 2, cache);
    return cache[idx];
}

std::uint64_t ClimbMemo(int n) {
    std::vector<std::uint64_t> cache(static_cast<std::size_t>(n) + 1, 0);
    return ClimbMemoRec(n, cache);
}

std::uint64_t ClimbIterative(int n) {
    std::uint64_t a = 1;  // ways(0)
    std::uint64_t b = 1;  // ways(1)
    for (int i = 2; i <= n; ++i) {
        const std::uint64_t next = a + b;
        a = b;
        b = next;
    }
    return n <= 1 ? 1 : b;
}

// ===========================================================================
// 第二部分 1：0-1 背包 与 完全背包
// ===========================================================================

struct Item {
    std::size_t weight = 0;
    int value = 0;
};

// --- 0-1 背包（每件物品最多选一次）-----------------------------------------
// 状态定义：dp[i][j] = 只考虑前 i 件物品、背包容量不超过 j 时能拿到的最大价值。
// 转移方程：dp[i][j] = max(dp[i-1][j],                     // 不选第 i 件
//                          dp[i-1][j - w_i] + v_i)         // 选第 i 件
//           关键在第二项取的是 i-1 行：第 i 件只能从「前 i-1 件」的状态转移过来，
//           否则就等于允许第 i 件被再选一次。
// 边界条件：dp[0][j] = 0（一件都不考虑，价值为 0），dp[i][0] = 0（容量为 0）。
// 空间优化：第 i 行只依赖第 i-1 行 -> 可以把二维表压成一维数组 dp[j]，
//           但内层循环必须【倒序】遍历 j（这一点第三节会用实验证明）。
// 复杂度：时间 O(n * W)；空间二维 O(n * W)，一维 O(W)。
std::vector<std::vector<int>> Knapsack01Table(const std::vector<Item>& items, std::size_t capacity) {
    const std::size_t n = items.size();
    std::vector<std::vector<int>> dp(n + 1, std::vector<int>(capacity + 1, 0));
    for (std::size_t i = 1; i <= n; ++i) {
        const Item& it = items[i - 1];
        for (std::size_t j = 0; j <= capacity; ++j) {
            dp[i][j] = dp[i - 1][j];  // 不选
            if (j >= it.weight) {
                dp[i][j] = std::max(dp[i][j], dp[i - 1][j - it.weight] + it.value);  // 选
            }
        }
    }
    return dp;
}

// 一维滚动数组（正确写法）
std::vector<int> Knapsack01RollingFull(const std::vector<Item>& items, std::size_t capacity) {
    std::vector<int> dp(capacity + 1, 0);
    for (const Item& it : items) {
        if (it.weight > capacity) {
            continue;
        }
        // 倒序！容量从大到小遍历。
        // 原因：dp[j - weight] 必须还是「上一件物品处理完之后」的旧值（即二维表的第 i-1 行）。
        // 倒序时 j - weight < j，而 j - weight 还没被本轮更新过，读到的正好是旧值。
        //
        // 顺便解释「未初始化的 dp[j - weight] 被当成 0 是否正确」：
        // 我们要的语义是「容量不超过 j」，容量没用完的部分价值就是 0，
        // 所以 dp[] 全 0 初始化恰好等于「容量不超过」语义，不需要额外的负无穷。
        for (std::size_t j = capacity; j >= it.weight; --j) {
            dp[j] = std::max(dp[j], dp[j - it.weight] + it.value);
        }
    }
    return dp;
}

int Knapsack01Rolling(const std::vector<Item>& items, std::size_t capacity) {
    return Knapsack01RollingFull(items, capacity)[capacity];
}

// ---------------------------------------------------------------------------
// 故意写错的版本：0-1 背包 + 正序遍历容量
// ---------------------------------------------------------------------------
// 正序时 dp[j - weight] 已经被本轮更新过，可能已经包含第 i 件物品了；
// 于是「再放一次第 i 件」变成合法操作——这正好就是完全背包的语义。
// 所以这个函数算出来的不是「错误答案」，而是「另一道题的正确答案」。
// 后面的实验会打印出两个数字并 assert 它们分别等于 0-1 与完全背包的解。
std::vector<int> Knapsack01RollingForwardBugFull(const std::vector<Item>& items, std::size_t capacity) {
    std::vector<int> dp(capacity + 1, 0);
    for (const Item& it : items) {
        if (it.weight > capacity) {
            continue;
        }
        for (std::size_t j = it.weight; j <= capacity; ++j) {  // 正序：本意是 0-1，实际成了完全背包
            dp[j] = std::max(dp[j], dp[j - it.weight] + it.value);
        }
    }
    return dp;
}

// 回溯出 0-1 背包选了哪些物品（顺便证明 DP 表里存的不只是「一个数字」）
std::vector<std::size_t> Knapsack01Reconstruct(const std::vector<std::vector<int>>& dp,
                                               const std::vector<Item>& items,
                                               std::size_t capacity) {
    std::vector<std::size_t> chosen;
    std::size_t j = capacity;
    for (std::size_t i = items.size(); i > 0; --i) {
        // 价值发生了变化，说明第 i 件物品一定被选了（价值全为正，选了必然更大）
        if (dp[i][j] != dp[i - 1][j]) {
            chosen.push_back(i - 1);
            j -= items[i - 1].weight;
        }
    }
    std::reverse(chosen.begin(), chosen.end());
    return chosen;
}

// --- 完全背包（每件物品可以选无限多次）-------------------------------------
// 状态定义：dp[i][j] = 只考虑前 i 种物品、容量不超过 j 时的最大价值。
// 转移方程：dp[i][j] = max(dp[i-1][j],               // 不选第 i 种
//                          dp[i][j - w_i] + v_i)     // 再选一件第 i 种（注意是 i 行，不是 i-1 行）
//           和 0-1 的唯一区别就是右下角那个 i：允许「同一行」继续转移，
//           也就是允许第 i 种物品被反复添加。
// 边界条件：dp[0][j] = 0，dp[i][0] = 0。
// 空间优化：同样压成一维，但内层必须【正序】遍历 j——
//           因为这里恰好需要 dp[j - w] 是「本轮已经更新过」的值，
//           正序保证了这一点，也就等价于二维表的同一行转移。
// 复杂度：时间 O(n * W)（弱多项式，W 是容量数值，不是输入规模），空间一维 O(W)。
std::vector<std::vector<int>> CompleteKnapsackTable(const std::vector<Item>& items, std::size_t capacity) {
    const std::size_t n = items.size();
    std::vector<std::vector<int>> dp(n + 1, std::vector<int>(capacity + 1, 0));
    for (std::size_t i = 1; i <= n; ++i) {
        const Item& it = items[i - 1];
        for (std::size_t j = 0; j <= capacity; ++j) {
            dp[i][j] = dp[i - 1][j];  // 不选第 i 种
            if (j >= it.weight) {
                dp[i][j] = std::max(dp[i][j], dp[i][j - it.weight] + it.value);  // 同一行，可以再用
            }
        }
    }
    return dp;
}

std::vector<int> CompleteKnapsackRollingFull(const std::vector<Item>& items, std::size_t capacity) {
    std::vector<int> dp(capacity + 1, 0);
    for (const Item& it : items) {
        if (it.weight > capacity) {
            continue;
        }
        for (std::size_t j = it.weight; j <= capacity; ++j) {  // 正序：同一件物品可以被反复使用
            dp[j] = std::max(dp[j], dp[j - it.weight] + it.value);
        }
    }
    return dp;
}

int CompleteKnapsackRolling(const std::vector<Item>& items, std::size_t capacity) {
    return CompleteKnapsackRollingFull(items, capacity)[capacity];
}

// 暴力对拍：枚举所有 2^n 个子集。只适合 n <= 20 的小规模。
// 对拍是验证 DP 最重要的手段：DP 写错了不会崩，只会安静地给出错误答案。
int KnapsackBruteForce(const std::vector<Item>& items, std::size_t capacity) {
    const std::size_t n = items.size();
    int best = 0;
    const std::size_t total = std::size_t{1} << n;
    for (std::size_t mask = 0; mask < total; ++mask) {
        std::size_t w = 0;
        int v = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (((mask >> i) & std::size_t{1}) != 0) {
                w += items[i].weight;
                v += items[i].value;
            }
        }
        if (w <= capacity && v > best) {
            best = v;
        }
    }
    return best;
}

// ===========================================================================
// 第二部分 2：最长公共子序列 LCS
// ===========================================================================
// 状态定义：dp[i][j] = a 的前 i 个字符与 b 的前 j 个字符的 LCS 长度。
// 转移方程：a[i-1] == b[j-1] 时，dp[i][j] = dp[i-1][j-1] + 1（这个字符一定可以放进答案）；
//           否则 dp[i][j] = max(dp[i-1][j], dp[i][j-1])——放弃 a 的最后一个字符，
//           或放弃 b 的最后一个字符，取更好的那个。
// 边界条件：dp[0][j] = dp[i][0] = 0（有一边是空串，公共子序列只能是空串）。
// 空间优化：第 i 行只依赖第 i-1 行，可以滚成一维；但 dp[i-1][j-1]（左上角）
//           会被 dp[j-1] 覆盖，所以必须用一个额外变量把「被覆盖前的 dp[j-1]」存下来，
//           这就是「滚动数组 + 一个对角线变量」的经典写法。
// 复杂度：时间 O(|a| * |b|)；空间二维 O(|a| * |b|)，一维 O(|b|)。
std::vector<std::vector<int>> LcsTable(const std::string& a, const std::string& b) {
    std::vector<std::vector<int>> dp(a.size() + 1, std::vector<int>(b.size() + 1, 0));
    for (std::size_t i = 1; i <= a.size(); ++i) {
        for (std::size_t j = 1; j <= b.size(); ++j) {
            if (a[i - 1] == b[j - 1]) {
                dp[i][j] = dp[i - 1][j - 1] + 1;
            } else {
                dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);
            }
        }
    }
    return dp;
}

// 回溯重建一个具体的 LCS（不是长度，是字符串本身）。
// 注意：LCS 可能有多个，回溯的走法决定了你拿到哪一个——所以测试里只能断言
// 「长度最优」和「它确实是两个串的公共子序列」，不能断言字符串内容唯一。
std::string LcsReconstruct(const std::string& a, const std::string& b,
                           const std::vector<std::vector<int>>& dp) {
    std::string out;
    std::size_t i = a.size();
    std::size_t j = b.size();
    while (i > 0 && j > 0) {
        if (a[i - 1] == b[j - 1]) {
            out.push_back(a[i - 1]);
            --i;
            --j;
        } else if (dp[i - 1][j] >= dp[i][j - 1]) {
            --i;  // 答案不在 a[i-1] 这一列里，往回退一步
        } else {
            --j;
        }
    }
    std::reverse(out.begin(), out.end());
    return out;
}

// 滚动数组版（只要长度）
int LcsLengthRolling(const std::string& a, const std::string& b) {
    std::vector<int> dp(b.size() + 1, 0);
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diag = 0;  // 保存 dp[i-1][j-1]：本轮覆盖 dp[j] 之前，先把旧的 dp[j] 留一份给下一列
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t saved = static_cast<std::size_t>(dp[j]);  // 覆盖前存档
            if (a[i - 1] == b[j - 1]) {
                dp[j] = static_cast<int>(diag) + 1;
            } else {
                dp[j] = std::max(dp[j], dp[j - 1]);
            }
            diag = saved;
        }
    }
    return dp[b.size()];
}

// 暴力版：不加缓存地按定义递归。指数复杂度，只适合长度 10 左右的小串。
int LcsBruteRec(const std::string& a, std::size_t i, const std::string& b, std::size_t j) {
    if (i == a.size() || j == b.size()) {
        return 0;
    }
    if (a[i] == b[j]) {
        return 1 + LcsBruteRec(a, i + 1, b, j + 1);
    }
    return std::max(LcsBruteRec(a, i + 1, b, j), LcsBruteRec(a, i, b, j + 1));
}

int LcsBruteForce(const std::string& a, const std::string& b) { return LcsBruteRec(a, 0, b, 0); }

// 判断 sub 是否为 s 的子序列：验证「重建出来的 LCS 确实是公共子序列」。
bool IsSubsequence(const std::string& sub, const std::string& s) {
    std::size_t k = 0;
    for (const char c : s) {
        if (k < sub.size() && sub[k] == c) {
            ++k;
        }
    }
    return k == sub.size();
}

// ===========================================================================
// 第二部分 3：最长递增子序列 LIS
// ===========================================================================
// 解法一：O(n^2) DP。
// 状态定义：dp[i] = 以 nums[i] 结尾的最长严格递增子序列的长度。
// 转移方程：dp[i] = max(dp[j] + 1)，对所有 j < i 且 nums[j] < nums[i]。
// 边界条件：dp[i] 至少是 1（每个元素自己构成一个长度为 1 的子序列）。
// 空间优化：朴素 O(n) 空间；另需一个 best 变量记录全局最大值。
// 答案：max(dp[i])，不是 dp[n-1]——「以最后一个元素结尾」并不一定最优，
//       这是初学者最容易写错的一行。
// 复杂度：时间 O(n^2)，空间 O(n)。
std::size_t LisQuadratic(const std::vector<int>& nums) {
    if (nums.empty()) {
        return 0;
    }
    std::vector<std::size_t> dp(nums.size(), 1);
    std::size_t best = 1;
    for (std::size_t i = 1; i < nums.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (nums[j] < nums[i] && dp[j] + 1 > dp[i]) {
                dp[i] = dp[j] + 1;
            }
        }
        if (dp[i] > best) {
            best = dp[i];
        }
    }
    return best;
}

// 同上，但允许相等（非递减子序列）。用 ?: 参数化太绕，就直接写第二份做对照。
std::size_t LndsQuadratic(const std::vector<int>& nums) {
    if (nums.empty()) {
        return 0;
    }
    std::vector<std::size_t> dp(nums.size(), 1);
    std::size_t best = 1;
    for (std::size_t i = 1; i < nums.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (nums[j] <= nums[i] && dp[j] + 1 > dp[i]) {
                dp[i] = dp[j] + 1;
            }
        }
        if (dp[i] > best) {
            best = dp[i];
        }
    }
    return best;
}

// 解法二：O(n log n) 贪心 + 二分。
//
// tails[k] 的含义：所有长度为 k+1 的严格递增子序列中，结尾元素的最小值。
// 为什么它可以用二分：tails 本身是严格递增的。
//   证明思路：若存在长度为 k+1 且结尾为 x 的递增子序列，把它去掉最后一个元素，
//   就得到一个长度为 k、结尾 y < x 的递增子序列，于是 tails[k-1] <= y < x <= tails[k]。
//   所以 tails 严格递增，可以在上面二分。
//
// 为什么这样是对的（贪心论证）：「结尾越小，后面越容易接上」。
//   同一个长度下，结尾更小的那个子序列在信息上严格更强——它能接的元素，大的那个不一定能接。
//   所以每个长度只需要保留最小的结尾。
//
// 严格递增 vs 非递减的坑：
//   严格递增（不能取等）用 std::lower_bound：找第一个 >= x 的位置替换。
//   非递减（可以取等）必须换 std::upper_bound：找第一个 > x 的位置替换。
//   如果该用 upper_bound 时写了 lower_bound，重复元素会被当成递增，答案偏小。
//
// 复杂度：时间 O(n log n)（每个元素一次二分），空间 O(n)（tails 最长 n）。
std::size_t LisNLogN(const std::vector<int>& nums) {
    std::vector<int> tails;
    tails.reserve(nums.size());
    for (const int x : nums) {
        const auto it = std::lower_bound(tails.begin(), tails.end(), x);
        if (it == tails.end()) {
            tails.push_back(x);  // x 比所有结尾都大：可以接在最长的后面，长度 +1
        } else {
            *it = x;             // 否则替换第一个 >= x 的位置，让这个长度的结尾更小
        }
    }
    return tails.size();
}

std::size_t LndsNLogN(const std::vector<int>& nums) {
    std::vector<int> tails;
    tails.reserve(nums.size());
    for (const int x : nums) {
        const auto it = std::upper_bound(tails.begin(), tails.end(), x);  // 找第一个 > x
        if (it == tails.end()) {
            tails.push_back(x);
        } else {
            *it = x;
        }
    }
    return tails.size();
}

// tails 的演化过程（教学演示专用）：
// 把每一步的 tails 打出来，可以看到「替换」而不是「追加」是怎么发生的。
//
// 这里刻意先做判断、再做修改：push_back 可能触发扩容，扩容后原来的迭代器全部失效，
// 再去比较一个已经失效的 it 就是未定义行为（MSVC 的调试迭代器会当场断言崩掉）。
// 「先存下判断结果、再改容器」是写这类代码的通用纪律。
void PrintTailsEvolution(const std::vector<int>& nums) {
    std::vector<int> tails;
    for (const int x : nums) {
        const auto it = std::lower_bound(tails.begin(), tails.end(), x);
        const bool appended = (it == tails.end());
        const std::size_t pos =
            appended ? tails.size() : static_cast<std::size_t>(it - tails.begin());
        if (appended) {
            tails.push_back(x);
        } else {
            *it = x;
        }
        std::cout << "    x = " << std::setw(4) << x << "  ->  tails = [";
        for (std::size_t k = 0; k < tails.size(); ++k) {
            std::cout << (k == 0 ? "" : ", ") << tails[k];
        }
        std::cout << "]";
        if (appended) {
            std::cout << "  （追加，长度变 " << tails.size() << "）";
        } else {
            std::cout << "  （替换下标 " << pos << "，长度不变）";
        }
        std::cout << "\n";
    }
}

// 再重建一条具体的 LIS（tails 本身不是答案，它只是「每个长度的最小结尾」）
std::vector<int> LisReconstruct(const std::vector<int>& nums) {
    constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> tail_pos;  // 与 tails 一一对应，记录元素在 nums 里的下标
    std::vector<std::size_t> parent(nums.size(), kNone);
    for (std::size_t i = 0; i < nums.size(); ++i) {
        const auto it = std::lower_bound(
            tail_pos.begin(), tail_pos.end(), nums[i],
            [&nums](std::size_t pos, int value) { return nums[pos] < value; });
        const std::size_t k = static_cast<std::size_t>(it - tail_pos.begin());
        parent[i] = (k == 0) ? kNone : tail_pos[k - 1];  // 记下前驱，方便最后回溯
        if (it == tail_pos.end()) {
            tail_pos.push_back(i);
        } else {
            *it = i;
        }
    }
    std::vector<int> seq;
    if (tail_pos.empty()) {
        return seq;
    }
    for (std::size_t p = tail_pos.back(); p != kNone; p = parent[p]) {
        seq.push_back(nums[p]);
    }
    std::reverse(seq.begin(), seq.end());
    return seq;
}

// 暴力对拍：枚举所有子集，检查是否严格递增。n <= 16 可用。
std::size_t LisBruteForce(const std::vector<int>& nums) {
    const std::size_t n = nums.size();
    std::size_t best = 0;
    const std::size_t total = std::size_t{1} << n;
    for (std::size_t mask = 0; mask < total; ++mask) {
        std::size_t len = 0;
        bool ok = true;
        bool has_prev = false;
        int prev = 0;
        for (std::size_t i = 0; i < n && ok; ++i) {
            if (((mask >> i) & std::size_t{1}) != 0) {
                if (has_prev && !(prev < nums[i])) {
                    ok = false;
                } else {
                    prev = nums[i];
                    has_prev = true;
                    len += 1;
                }
            }
        }
        if (ok && len > best) {
            best = len;
        }
    }
    return best;
}

// ===========================================================================
// 第二部分 4：编辑距离（Levenshtein Distance）
// ===========================================================================
// 状态定义：dp[i][j] = 把 a 的前 i 个字符变成 b 的前 j 个字符所需的最少操作数。
// 转移方程：dp[i][j] = min(
//               dp[i-1][j]   + 1,                      // 删除 a[i-1]：a 变短，b 还没配上
//               dp[i][j-1]   + 1,                      // 插入 b[j-1]：a 先补一个字符
//               dp[i-1][j-1] + (a[i-1] != b[j-1])      // 替换；相等时免费
//           )
// 边界条件：dp[i][0] = i（a 的前 i 个字符全删掉），dp[0][j] = j（从空串插入 j 个字符）。
// 空间优化：只依赖上一行和本行左边一格，可以滚成一维（左上角同样要靠一个 diag 变量存下来）。
// 复杂度：时间 O(|a| * |b|)，空间二维 O(|a| * |b|)，一维 O(|b|)。
std::vector<std::vector<std::size_t>> EditDistanceTable(const std::string& a, const std::string& b) {
    std::vector<std::vector<std::size_t>> dp(
        a.size() + 1, std::vector<std::size_t>(b.size() + 1, 0));
    for (std::size_t i = 0; i <= a.size(); ++i) {
        dp[i][0] = i;
    }
    for (std::size_t j = 0; j <= b.size(); ++j) {
        dp[0][j] = j;
    }
    for (std::size_t i = 1; i <= a.size(); ++i) {
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t del = dp[i - 1][j] + 1;
            const std::size_t ins = dp[i][j - 1] + 1;
            const std::size_t sub = dp[i - 1][j - 1] + (a[i - 1] == b[j - 1] ? 0u : 1u);
            dp[i][j] = std::min(del, std::min(ins, sub));
        }
    }
    return dp;
}

std::size_t EditDistanceRolling(const std::string& a, const std::string& b) {
    std::vector<std::size_t> dp(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) {
        dp[j] = j;  // 第一行：从空串变成 b 的前 j 个字符，需要 j 次插入
    }
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diag = dp[0];  // dp[i-1][0]，也就是上一行的第一格
        dp[0] = i;                 // dp[i][0] = i 次删除
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t saved = dp[j];  // 覆盖前存档，它就是下一列的左上角
            const std::size_t del = dp[j] + 1;
            const std::size_t ins = dp[j - 1] + 1;
            const std::size_t sub = diag + (a[i - 1] == b[j - 1] ? 0u : 1u);
            dp[j] = std::min(del, std::min(ins, sub));
            diag = saved;
        }
    }
    return dp[b.size()];
}

// 暴力版：按定义递归，不缓存。指数级，只用来给很小的串对拍。
std::size_t EditDistanceBruteRec(const std::string& a, std::size_t i,
                                 const std::string& b, std::size_t j) {
    if (i == 0) {
        return j;  // a 已经空了，只能插入 b 剩下的 j 个字符
    }
    if (j == 0) {
        return i;  // b 已经空了，只能删掉 a 剩下的 i 个字符
    }
    if (a[i - 1] == b[j - 1]) {
        return EditDistanceBruteRec(a, i - 1, b, j - 1);
    }
    const std::size_t del = EditDistanceBruteRec(a, i - 1, b, j);
    const std::size_t ins = EditDistanceBruteRec(a, i, b, j - 1);
    const std::size_t sub = EditDistanceBruteRec(a, i - 1, b, j - 1);
    return 1 + std::min(del, std::min(ins, sub));
}

std::size_t EditDistanceBruteForce(const std::string& a, const std::string& b) {
    return EditDistanceBruteRec(a, a.size(), b, b.size());
}

// ===========================================================================
// 第二部分 5：打家劫舍（House Robber）
// ===========================================================================
// 状态定义：dp[i] = 只考虑前 i 间房子能偷到的最大金额。
// 转移方程：dp[i] = max(dp[i-1],           // 不偷第 i 间
//                       dp[i-2] + money[i-1])  // 偷第 i 间，那么第 i-1 间必须放过
// 边界条件：dp[0] = 0，dp[1] = money[0]。
// 空间优化：dp[i] 只依赖前两项 -> 两个变量，O(1) 空间。
// 复杂度：时间 O(n)，空间 O(n) -> O(1)。
int RobLinear(const std::vector<int>& money) {
    std::vector<int> dp(money.size() + 1, 0);
    for (std::size_t i = 1; i <= money.size(); ++i) {
        const int skip = dp[i - 1];
        const int take = (i >= 2 ? dp[i - 2] : 0) + money[i - 1];
        dp[i] = std::max(skip, take);
    }
    return dp[money.size()];
}

int RobConstantSpace(const std::vector<int>& money) {
    int prev2 = 0;  // dp[i-2]
    int prev1 = 0;  // dp[i-1]
    for (const int m : money) {
        const int cur = std::max(prev1, prev2 + m);
        prev2 = prev1;
        prev1 = cur;
    }
    return prev1;
}

// 暴力对拍：枚举所有「不相邻」的子集。钱数非负，所以 2^n 枚举可行。
int RobBruteForce(const std::vector<int>& money) {
    const std::size_t n = money.size();
    int best = 0;
    const std::size_t total = std::size_t{1} << n;
    for (std::size_t mask = 0; mask < total; ++mask) {
        if ((mask & (mask << 1)) != 0) {
            continue;  // 有两个相邻的 1，方案非法
        }
        int sum = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (((mask >> i) & std::size_t{1}) != 0) {
                sum += money[i];
            }
        }
        if (sum > best) {
            best = sum;
        }
    }
    return best;
}

// ===========================================================================
// 第二部分 6：零钱兑换（Coin Change）
// ===========================================================================

// 变体 1：最少硬币数。
// 状态定义：dp[a] = 凑出金额 a 所需的最少硬币数。
// 转移方程：dp[a] = min(dp[a], dp[a - c] + 1)，对所有 c <= a。
// 边界条件：dp[0] = 0；其余初始化为「不可达」。
//          这里用 amount + 1 当无穷大：任何合法答案都不会超过 amount（全用 1 元），
//          用 INT_MAX 的话 dp[a-c] + 1 会整型溢出，是个隐蔽的坑。
// 空间优化：只保留一维，O(amount)。
// 复杂度：时间 O(amount * coins)，空间 O(amount)。
//
// 注意：min 对「加法的顺序」不敏感，所以这里外层金额、内层硬币是安全的。
//       但求方案数时就不能这么随意了，见下一个函数。
int CoinChangeMinCoins(const std::vector<int>& coins, int amount) {
    const int kUnreachable = amount + 1;
    std::vector<int> dp(static_cast<std::size_t>(amount) + 1, kUnreachable);
    dp[0] = 0;
    for (int a = 1; a <= amount; ++a) {
        for (const int c : coins) {
            if (c <= a) {
                const std::size_t cur = static_cast<std::size_t>(a);
                const std::size_t from = static_cast<std::size_t>(a - c);
                if (dp[from] + 1 < dp[cur]) {
                    dp[cur] = dp[from] + 1;
                }
            }
        }
    }
    return dp[static_cast<std::size_t>(amount)] > amount ? -1
                                                         : dp[static_cast<std::size_t>(amount)];
}

// 变体 2：方案数（组合数，不区分顺序）。
// 状态定义：dp[a] = 用给定的硬币凑出金额 a 的方案数。
// 转移方程：dp[a] += dp[a - c]。
// 边界条件：dp[0] = 1（凑 0 元有 1 种方案：一枚都不选。这是计数类 DP 的通用边界，
//                     写成 0 的话所有方案数都会变成 0）。
// 空间优化：一维，O(amount)。
// 复杂度：时间 O(coins * amount)，空间 O(amount)。
//
// 关键：外层必须遍历硬币，内层遍历金额。
//   外层硬币时，每枚硬币只在「之前已经考虑过的硬币集合」基础上做一次完全背包式累加，
//   于是 {1,2} 这个组合只会被数一次（先放 1 再放 2 的顺序被结构性地排除了）。
//   如果反过来（外层金额），dp[a] 每次都重新考虑所有硬币，
//   {1,2} 和 {2,1} 会被当成两种不同的方案——那算的是「排列数」（composition），
//   是另一道题的答案。下面同时实现两个版本，用真实数字把这个区别摆出来。
std::uint64_t CoinChangeWaysCombination(const std::vector<int>& coins, int amount) {
    std::vector<std::uint64_t> dp(static_cast<std::size_t>(amount) + 1, 0);
    dp[0] = 1;
    for (const int c : coins) {
        for (int a = c; a <= amount; ++a) {
            const std::size_t cur = static_cast<std::size_t>(a);
            const std::size_t from = static_cast<std::size_t>(a - c);
            dp[cur] += dp[from];
        }
    }
    return dp[static_cast<std::size_t>(amount)];
}

// 反面教材：外层金额、内层硬币 -> 算出来的是「有序方案」即排列数。
// 它本身没错，只是回答的不是同一道题。很多「方案数答案偏大」的 bug 都源于此。
std::uint64_t CoinChangeWaysPermutation(const std::vector<int>& coins, int amount) {
    std::vector<std::uint64_t> dp(static_cast<std::size_t>(amount) + 1, 0);
    dp[0] = 1;
    for (int a = 1; a <= amount; ++a) {
        for (const int c : coins) {
            if (c <= a) {
                const std::size_t cur = static_cast<std::size_t>(a);
                const std::size_t from = static_cast<std::size_t>(a - c);
                dp[cur] += dp[from];
            }
        }
    }
    return dp[static_cast<std::size_t>(amount)];
}

// 暴力对拍：按「硬币种类从前往后、每种可以用多次」的方式穷举，天然只数组合。
std::uint64_t CoinChangeWaysBruteRec(const std::vector<int>& coins, std::size_t idx, int remaining) {
    if (remaining == 0) {
        return 1;
    }
    if (remaining < 0 || idx >= coins.size()) {
        return 0;
    }
    return CoinChangeWaysBruteRec(coins, idx, remaining - coins[idx])     // 再用一枚第 idx 种
         + CoinChangeWaysBruteRec(coins, idx + 1, remaining);            // 换下一种硬币
}

std::uint64_t CoinChangeWaysBruteForce(const std::vector<int>& coins, int amount) {
    return CoinChangeWaysBruteRec(coins, 0, amount);
}

// 最少硬币数的暴力对拍
int CoinChangeMinBruteRec(const std::vector<int>& coins, std::size_t idx, int remaining) {
    if (remaining == 0) {
        return 0;
    }
    if (remaining < 0 || idx >= coins.size()) {
        return -1;  // 不可达
    }
    const int use = CoinChangeMinBruteRec(coins, idx, remaining - coins[idx]);
    const int skip = CoinChangeMinBruteRec(coins, idx + 1, remaining);
    if (use < 0) {
        return skip;
    }
    if (skip < 0) {
        return use + 1;
    }
    return std::min(use + 1, skip);
}

int CoinChangeMinBruteForce(const std::vector<int>& coins, int amount) {
    return CoinChangeMinBruteRec(coins, 0, amount);
}

// 打印时给「DP 四段式」留个统一格式，避免每节手写一遍还写得不一样。
void PrintDpRecipe(const std::string& state, const std::string& transition,
                   const std::string& boundary, const std::string& space) {
    Label("  状态定义：", 14);
    std::cout << state << "\n";
    Label("  转移方程：", 14);
    std::cout << transition << "\n";
    Label("  边界条件：", 14);
    std::cout << boundary << "\n";
    Label("  空间优化：", 14);
    std::cout << space << "\n";
}

}  // namespace

int main() {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "本文件在 Debug 配置下运行，绝对耗时比 Release 慢很多，请只看趋势与数量级。\n";

    // =======================================================================
    Section("1. 三步演进（一）：斐波那契 —— 朴素递归 / 记忆化 / 递推");
    // =======================================================================
    Note("同一个递推式 fib(n) = fib(n-1) + fib(n-2)，三种实现的差距是数量级级别的。");
    Note("先看调用次数：这个数字和机器、编译器无关，是纯数学事实。");
    std::cout << "\n";

    Label("n", 6);
    Label("朴素递归调用次数", 22);
    Label("记忆化进入次数", 20);
    Label("其中真正求解", 16);
    Label("倍数", 14);
    std::cout << "\n" << std::string(78, '-') << "\n";

    std::uint64_t naive_calls_at_32 = 0;
    std::uint64_t memo_calls_at_32 = 0;
    std::uint64_t memo_solves_at_32 = 0;
    {
        for (const int n : {18, 22, 26, 30, 32}) {
            ResetCalls();
            const std::uint64_t v1 = FibNaiveCounted(n);
            const std::uint64_t naive_calls = g_calls;

            ResetCalls();
            MemoFib memo(static_cast<std::size_t>(n));
            const std::uint64_t v2 = memo.Solve(static_cast<std::size_t>(n));
            const std::uint64_t memo_calls = g_calls;
            const std::uint64_t memo_solves = g_solves;

            assert(v1 == v2);  // 两个版本必须给出同一个答案
            assert(v2 == FibIterative(n));

            if (n == 32) {
                naive_calls_at_32 = naive_calls;
                memo_calls_at_32 = memo_calls;
                memo_solves_at_32 = memo_solves;
            }

            std::cout << std::setw(6) << n << std::setw(22) << naive_calls << std::setw(20)
                      << memo_calls << std::setw(16) << memo_solves << std::setw(14)
                      << (naive_calls / memo_calls) << "\n";
        }
    }
    Note("");
    Note("看到了吗：朴素递归的调用次数是 8361 / 57313 / 392835 / 2692537 / 7049155，");
    Note("每加 4 就涨约 6.85 倍（phi^4）；记忆化的进入次数是 2n-1（63 而不是 700 万），");
    Note("其中真正被求解的子问题只有 n+1 个，其余的进入都是缓存命中。");
    Note("换个说法：朴素递归把 700 万个节点重新算了一遍又一遍，而问题本体只有 33 个状态。");
    Note("这就是「重叠子问题」的量化——调用次数比耗时更能说明问题，");
    Note("因为它不受机器、编译器、Debug/Release 的影响。");

    // -----------------------------------------------------------------------
    // 耗时对比
    // -----------------------------------------------------------------------
    std::cout << "\n";
    Note("接下来测耗时。朴素递归只在 n = 24 / 26 / 28 / 30 / 32 上实测：");
    Note("n = 40 的调用次数约 3.31e8（三亿次），实测它就是纯粹的浪费，所以只做外推。");
    Note("三亿次这个数字是机器无关的事实；至于它到底要跑 0.4 秒还是几分钟，取决于单次");
    Note("调用成本，下面会把两项分开列出来，让大家看清外推值是怎么来的。");
    std::cout << "\n";

    // 预热：让缺页中断、分支预测器训练这些一次性成本落在测量之外。
    // 这里刻意用接近正式规模的负载预热（n = 24 要 20 多万次调用）：
    // 如果只预热一个很小的 n，CPU 还停在低频状态，测出来的第一档（n = 25）
    // 单次成本会明显偏高，把增长倍率带歪。
    WarmUp([&] { return FibNaive(24); }, 3);
    WarmUp([&] { return FibIterative(40); }, 2);

    constexpr double kPhi = 1.6180339887498949;

    Label("n", 6);
    Label("朴素递归实测 (us)", 22);
    Label("相对上一档倍数", 20);
    std::cout << "\n" << std::string(58, '-') << "\n";

    // 每档间隔固定为 2，这样「相邻两档的倍数」就直接对应理论值 phi^2 ≈ 2.618，
    // 一眼就能看出增长阶对不对。间隔不统一的话，倍数根本没法横向比较。
    const int naive_sizes[] = {24, 26, 28, 30, 32};
    constexpr std::size_t kNaiveCount = sizeof(naive_sizes) / sizeof(naive_sizes[0]);
    // 重复次数随规模递减：总调用次数控制在三千万次以内，整段跑完还在几十毫秒级
    const int naive_repeats[] = {5, 5, 4, 3, 2};
    double naive_us[kNaiveCount] = {};

    for (std::size_t i = 0; i < kNaiveCount; ++i) {
        const int n = naive_sizes[i];
        naive_us[i] = BenchMedianUs([&] { return FibNaive(n); }, naive_repeats[i]);
        std::cout << std::setw(6) << n << std::setw(22) << naive_us[i];
        if (i >= 1 && naive_us[i - 1] > 0.0) {
            std::cout << std::setw(20) << (naive_us[i] / naive_us[i - 1]);
        } else {
            std::cout << std::setw(20) << "-";
        }
        std::cout << "\n";
    }

    Note("");
    Note("看「相对上一档倍数」这一列：它应该在 2.618（phi^2）附近上下浮动。");
    Note("实测值不会精确等于理论值——Debug 下函数调用成本、CPU 频率都会漂，");
    Note("但只要趋势对得上，就说明「耗时由调用次数决定」这个判断是对的。");
    Note("下面的增长率拟合会把五个点一起用上，比单看某一档稳健得多。");

    // -----------------------------------------------------------------------
    // 外推 n = 40
    // -----------------------------------------------------------------------
    std::cout << "\n";
    Note("【外推】n = 40 的朴素递归预计耗时（下面是算出来的，不是测出来的）：");

    // 调用次数的解析式：C(n) = 2 * F(n+1) - 1
    const std::uint64_t calls40 = 2 * FibIterative(41) - 1;
    // 用四档实测数据做增长率的拟合。
    // 为什么不是简单地把首尾两个点相除？因为那相当于把两端的测量噪声放大 7 次方。
    // 这里对 ln(耗时) 关于 n 做最小二乘直线拟合，斜率就是「每加 1 的耗时倍数」，
    // 四个点一起参与，噪声被平均掉，稳健得多。
    double sum_x = 0.0;
    double sum_y = 0.0;
    double sum_xy = 0.0;
    double sum_xx = 0.0;
    for (std::size_t i = 0; i < kNaiveCount; ++i) {
        const double x = static_cast<double>(naive_sizes[i]);
        const double y = std::log(naive_us[i]);
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_xx += x * x;
    }
    const double cnt = static_cast<double>(kNaiveCount);
    const double per_step = std::exp((cnt * sum_xy - sum_x * sum_y) / (cnt * sum_xx - sum_x * sum_x));
    const double est_from_fit = naive_us[kNaiveCount - 1] * std::pow(per_step, 8.0);
    const double est_from_phi = naive_us[kNaiveCount - 1] * std::pow(kPhi, 8.0);
    const double ns_per_call = naive_us[kNaiveCount - 1] * 1000.0
                             / static_cast<double>(naive_calls_at_32);
    // 单位换算：次数 x 纳秒 = 纳秒，再除以 1e9 才是秒（这里曾经写成 1e6，差了 1000 倍）
    const double est_from_calls = static_cast<double>(calls40) * ns_per_call / 1.0e9;  // 秒

    Label("  n = 32 实测调用次数", 40);
    std::cout << ": " << naive_calls_at_32 << "\n";
    Label("  n = 32 实测耗时", 40);
    std::cout << ": " << naive_us[kNaiveCount - 1] << " us\n";
    Label("  折算每次调用的成本", 40);
    std::cout << ": " << ns_per_call << " ns\n";
    Label("  n = 40 的调用次数 2*F(41)-1", 40);
    std::cout << ": " << calls40 << "（约 3.31e8，三亿次）\n";
    Label("  实测拟合的每步增长率", 40);
    std::cout << ": " << per_step << "（理论 phi = " << kPhi << "）\n";
    Label("  按实测增长率外推 phi^8", 40);
    std::cout << ": " << (est_from_fit / 1.0e6) << " 秒\n";
    Label("  按理论 phi^8 外推", 40);
    std::cout << ": " << (est_from_phi / 1.0e6) << " 秒\n";
    Label("  按「调用次数 x 单次成本」外推", 40);
    std::cout << ": " << est_from_calls << " 秒\n";
    Note("");
    Note("三个估计值互相吻合，说明「朴素递归的代价 = 调用次数 x 单次成本」这个模型是对的。");
    Note("【为什么这一行只外推、不实测】");
    Note("  1) 三亿次函数调用本身就是纯粹的浪费，而结论早就可以算出来了；");
    Note("  2) 这个外推值的绝对值高度依赖机器：本机单次调用只要约 1 ns（代码极小、");
    Note("     分支完全可预测，现代 CPU 的乱序执行能把递归的开销重叠掉），");
    Note("     而换到老 CPU、虚拟机、或者挂了内存检查工具的环境，单次成本上到 100 ns");
    Note("     就意味着三亿次要跑半分钟以上——那时它才是「肉眼可见的卡死」。");
    Note("     机器无关的事实只有一个：调用次数是 3.31e8，指数增长不会因为机器快而消失。");
    Note("  3) 就算它只跑不到 1 秒，也比重头算的记忆化 / 递推慢约 5 个数量级；");
    Note("     n = 50 时外推值还要再乘 phi^10 ≈ 123 倍。换算法才是正解。");

    // -----------------------------------------------------------------------
    // 记忆化 / 递推的耗时
    // -----------------------------------------------------------------------
    std::cout << "\n";
    Note("记忆化与递推：同样的 n，可以直接算到大得多的规模。");
    std::cout << "\n";

    Label("n", 6);
    Label("记忆化（每次新建缓存）", 26);
    Label("递推 O(1) 空间", 22);
    Label("两者结果", 14);
    std::cout << "\n" << std::string(68, '-') << "\n";

    const int fast_sizes[] = {32, 40, 50, 70, 90};
    for (const int n : fast_sizes) {
        const double t_memo = BenchMedianUs(
            [&] {
                return Repeat(
                    [&] {
                        MemoFib memo(static_cast<std::size_t>(n));
                        return memo.Solve(static_cast<std::size_t>(n));
                    },
                    50);
            },
            3);
        const double t_iter = BenchMedianUs([&] { return Repeat([&] { return FibIterative(n); }, 2000); }, 3);

        // 交叉验证：记忆化 / 递推 在同一 n 上必须给出同一个值
        // （朴素递归只在前面的小节里验证小规模，这里不重复调用，太贵）
        {
            ResetCalls();
            MemoFib verify(static_cast<std::size_t>(n));
            const std::uint64_t v_memo = verify.Solve(static_cast<std::size_t>(n));
            assert(v_memo == FibIterative(n));
        }
        std::cout << std::setw(6) << n << std::setw(26) << t_memo << std::setw(22) << t_iter
                  << std::setw(14) << "一致" << "\n";
    }
    Note("");
    Note("上表两列都是「50 次 / 2000 次调用」的总耗时，单位微秒；单次成本要再除一下。");
    Note("注意记忆化那一列包含了「每次新建一个大小为 n+1 的缓存表」的开销——");
    Note("这是自顶向下的固有代价：你得先准备一张可能只用一部分的表（或者用哈希表，");
    Note("换来更好的稀疏性但更差的常数）。");
    Note("递推版则完全不需要表，是这类「只依赖前几项」的递推能达到的最优形态。");
    {
        // 顺手把「空间优化」也量化一下，别只说「O(n) 变 O(1)」这种空话
        const MemoFib probe(90);
        std::cout << "  空间对照：n = 90 时记忆化的缓存表占 " << probe.CacheBytes()
                  << " 字节（91 个 uint64_t），递推版只占 " << (2 * sizeof(std::uint64_t))
                  << " 字节（两个滚动变量）。\n";
    }

    // 第 32 档的三方对比（朴素递归在这里还能实测）
    {
        std::cout << "\n";
        Note("同一个 n = 32，三种实现的直接对比（朴素递归在这里已经是几毫秒级）：");
        const double t_naive = naive_us[kNaiveCount - 1];
        const double t_memo = BenchMedianUs(
            [&] {
                return Repeat(
                    [&] {
                        MemoFib memo(32);
                        return memo.Solve(32);
                    },
                    50);
            },
            3);
        const double t_iter = BenchMedianUs([&] { return Repeat([&] { return FibIterative(32); }, 2000); }, 3);
        Label("  朴素递归（1 次）", 30);
        std::cout << ": " << t_naive << " us\n";
        Label("  记忆化（50 次的总耗时）", 30);
        std::cout << ": " << t_memo << " us  -> 单次约 " << (t_memo / 50.0) << " us\n";
        Label("  递推（2000 次的总耗时）", 30);
        std::cout << ": " << t_iter << " us  -> 单次约 " << (t_iter / 2000.0) << " us\n";
        Label("  朴素 / 记忆化（单次）倍数", 30);
        std::cout << ": " << (t_naive / (t_memo / 50.0)) << " x\n";
    }

    // -----------------------------------------------------------------------
    // 溢出边界
    // -----------------------------------------------------------------------
    std::cout << "\n";
    Note("顺带把一个常见误解澄清掉：斐波那契用 uint64_t 到底能算到第几项？");
    const int max_n = MaxFibIndexInUint64();
    std::cout << "  程序自己探测的结果：uint64_t 最大能表示到 fib(" << max_n << ") = "
              << FibIterative(max_n) << "\n";
    std::cout << "  下一项 fib(" << (max_n + 1) << ") = 1.97e19 已经超过 uint64_t 上限 1.84e19，会回绕。\n";
    Note("所以「n = 90 就溢出」是不准确的说法：fib(90) 才 2.88e18，离上限还远。");
    Note("本文件选择 n = 90 作为演示上限，是为了留足安全余量、不被边界问题打断教学；");
    Note("真要顶到边界，n = 93 才是 uint64_t 的最后一站。");

    // =======================================================================
    Section("2. 三步演进（二）：爬楼梯 —— 同一个递推，换个故事");
    // =======================================================================
    Note("题目：一次走 1 阶或 2 阶，走到第 n 阶有多少种走法？");
    Note("状态定义：ways(i) = 走到第 i 阶的走法数。");
    Note("转移方程：ways(i) = ways(i-1) + ways(i-2)。");
    Note("  理由是「看最后一步」：最后一步要么从 i-1 迈 1 阶，要么从 i-2 迈 2 阶，");
    Note("  两种情况互斥且覆盖全部，所以相加。这是 DP 里最常用的思考方式。");
    Note("边界条件：ways(0) = 1（站在地面什么也不做，算 1 种走法），ways(1) = 1。");
    Note("空间优化：只依赖前两项 -> O(1) 空间。");
    Note("它其实就是 ways(n) = fib(n+1)：把题目翻译成状态才是 DP 的难点，递推式反而最简单。");
    std::cout << "\n";

    Label("n", 6);
    Label("朴素递归调用次数", 22);
    Label("记忆化进入次数", 20);
    Label("其中真正求解", 16);
    Label("ways(n)", 18);
    std::cout << "\n" << std::string(82, '-') << "\n";
    for (const int n : {18, 22, 26, 30}) {
        ResetCalls();
        const std::uint64_t v1 = ClimbNaive(n);
        const std::uint64_t c1 = g_calls;
        ResetCalls();
        const std::uint64_t v2 = ClimbMemo(n);
        const std::uint64_t c2 = g_calls;
        const std::uint64_t s2 = g_solves;
        const std::uint64_t v3 = ClimbIterative(n);
        assert(v1 == v2 && v2 == v3);
        assert(v3 == FibIterative(n + 1));  // ways(n) == fib(n+1)
        assert(s2 == static_cast<std::uint64_t>(n) + 1);
        std::cout << std::setw(6) << n << std::setw(22) << c1 << std::setw(20) << c2
                  << std::setw(16) << s2 << std::setw(18) << v3 << "\n";
    }
    Note("");
    Note("爬楼梯的调用次数增长规律和斐波那契完全一样——因为它们本来就是同一个递推。");
    Note("练习：如果一次可以走 1 / 2 / 3 阶呢？（提示：ways(i) = ways(i-1)+ways(i-2)+ways(i-3)，");
    Note("      边界变成 ways(0)=1, ways(1)=1, ways(2)=2。状态定义不变，只多了两项。）");

    // =======================================================================
    Section("3. 0-1 背包 与 完全背包：倒序与正序这个最经典的坑");
    // =======================================================================
    // 这一节是全文件最重要的实验：用可运行的代码证明「正序 = 完全背包」。
    const std::vector<Item> demo_items = {
        {2, 3},  // A
        {3, 4},  // B
        {4, 5},  // C
        {5, 6},  // D
    };
    const std::size_t demo_cap = 8;

    std::cout << "\n";
    Note("0-1 背包的四段式：");
    PrintDpRecipe("dp[i][j] = 只考虑前 i 件物品、容量不超过 j 时的最大价值。",
                  "dp[i][j] = max(dp[i-1][j], dp[i-1][j-w_i] + v_i)  注意第二项取 i-1 行。",
                  "dp[0][j] = 0（一件都不考虑）；dp[i][0] = 0（容量为 0）。",
                  "第 i 行只依赖第 i-1 行 -> 压成一维 dp[j]，但内层必须【倒序】遍历容量。");
    std::cout << "\n";
    Note("完全背包的四段式：");
    PrintDpRecipe("dp[i][j] = 只考虑前 i 种物品、容量不超过 j 时的最大价值。",
                  "dp[i][j] = max(dp[i-1][j], dp[i][j-w_i] + v_i)  注意第二项是 i 行（同一行）。",
                  "dp[0][j] = 0；dp[i][0] = 0。",
                  "同样压成一维，但内层必须【正序】遍历容量（正序才等价于同一行转移）。");
    std::cout << "\n";
    Note("复杂度：两者都是时间 O(n * W)；空间二维 O(n * W)，一维 O(W)。");
    Note("        注意 W 是容量的【数值】而不是输入的位数，所以这是「弱多项式」算法——");
    Note("        W 很大（比如 1e18）时背包问题是 NP-hard 的，DP 并不能救你。");

    // -----------------------------------------------------------------------
    // 实验：0-1 背包的正序写法到底算出了什么
    // -----------------------------------------------------------------------
    std::cout << "\n";
    Note("实验数据：4 件物品 A(重2,价3) B(重3,价4) C(重4,价5) D(重5,价6)，容量 8。");
    std::cout << "\n";

    const std::vector<std::vector<int>> table01 = Knapsack01Table(demo_items, demo_cap);
    const std::vector<int> rolling01 = Knapsack01RollingFull(demo_items, demo_cap);
    const std::vector<int> forward_bug = Knapsack01RollingForwardBugFull(demo_items, demo_cap);
    const std::vector<std::vector<int>> table_complete = CompleteKnapsackTable(demo_items, demo_cap);
    const std::vector<int> rolling_complete = CompleteKnapsackRollingFull(demo_items, demo_cap);
    const int brute = KnapsackBruteForce(demo_items, demo_cap);

    const int v_2d = table01.back().back();
    const int v_1d = rolling01[demo_cap];
    const int v_bug = forward_bug[demo_cap];
    const int v_complete_2d = table_complete.back().back();
    const int v_complete_1d = rolling_complete[demo_cap];

    Label("  0-1 背包 二维 DP（正确）", 40);
    std::cout << ": " << v_2d << "\n";
    Label("  0-1 背包 一维倒序（正确）", 40);
    std::cout << ": " << v_1d << "\n";
    Label("  0-1 背包 一维【正序】写错", 40);
    std::cout << ": " << v_bug << "   <- 偏大，答案错了\n";
    Label("  完全背包 二维 DP", 40);
    std::cout << ": " << v_complete_2d << "\n";
    Label("  完全背包 一维正序", 40);
    std::cout << ": " << v_complete_1d << "\n";
    Label("  暴力枚举 2^4 个子集", 40);
    std::cout << ": " << brute << "\n";

    // 这些断言就是本节的结论：写错的那个版本恰好等于完全背包的正确答案。
    assert(v_2d == v_1d);
    assert(v_1d == brute);
    assert(v_bug == v_complete_1d);      // 正序的 0-1 == 完全背包：不是巧合
    assert(v_bug == v_complete_2d);
    assert(forward_bug == rolling_complete);  // 整张表都相等，不只最后一个格子

    Note("");
    Note("为什么错误值是 12 而不是 10？追一下这条路径就明白了：");
    Note("  正序处理物品 A(重2,价3) 时，dp[2]=3 -> dp[4]=dp[2]+3=6 -> dp[6]=9 -> dp[8]=12。");
    Note("  也就是说，同一件 A 被连着放了 4 次（4 x 重2 = 8，4 x 价3 = 12）。");
    Note("  而 0-1 背包规定每件物品最多一次，正确答案是 B+D（重 3+5=8，价 4+6=10）。");
    Note("");
    Note("所以「倒序」这个细节不是风格问题，它是 0-1 与完全背包在代码上唯一的分界线：");
    Note("  倒序 -> dp[j - w] 仍是上一件物品处理完的旧值 -> 每件只能用一次；");
    Note("  正序 -> dp[j - w] 已包含本件物品 -> 等价于这件物品可以无限用。");
    Note("记忆方法：0-1 背包「从后往前推」，完全背包「从前往后放」。");

    // 重建一组 0-1 的最优解，证明 DP 表里存的不只是一个数字
    {
        const std::vector<std::size_t> chosen = Knapsack01Reconstruct(table01, demo_items, demo_cap);
        std::size_t w = 0;
        int v = 0;
        std::cout << "\n  回溯出的最优组合（二维表）：";
        for (const std::size_t idx : chosen) {
            std::cout << static_cast<char>('A' + static_cast<int>(idx)) << " ";
            w += demo_items[idx].weight;
            v += demo_items[idx].value;
        }
        std::cout << " 总重 " << w << "（<= " << demo_cap << "），总价 " << v << "\n";
        assert(w <= demo_cap);
        assert(v == v_2d);  // 回溯出来的方案必须和最优值一致
    }

    // -----------------------------------------------------------------------
    // 小规模随机对拍
    // -----------------------------------------------------------------------
    std::cout << "\n";
    Note("随机对拍：15 件物品、容量 40，把 2D / 1D / 暴力枚举三个版本放在一起比。");
    {
        std::mt19937 rng(20240501u);
        for (int trial = 0; trial < 20; ++trial) {
            std::vector<Item> items;
            const std::size_t n = 10 + static_cast<std::size_t>(trial % 5);  // 10..14 件
            for (std::size_t i = 0; i < n; ++i) {
                items.push_back(Item{1 + static_cast<std::size_t>(rng() % 12u),
                                     1 + static_cast<int>(rng() % 30u)});
            }
            const std::size_t cap = 20 + static_cast<std::size_t>(rng() % 25u);
            const int a = Knapsack01Table(items, cap).back().back();
            const int b = Knapsack01Rolling(items, cap);
            const int c = KnapsackBruteForce(items, cap);
            assert(a == b);
            assert(b == c);
        }
        Note("  20 组随机用例全部通过（2D == 1D == 暴力枚举）。");
    }

    // -----------------------------------------------------------------------
    // 空间优化的额外收益：一维还更快
    // -----------------------------------------------------------------------
    std::cout << "\n";
    Note("空间优化的附带好处：一维数组的内存访问更集中，通常还更快。实测：");
    {
        const std::size_t big_n = 400;
        const std::size_t big_cap = 2000;
        std::mt19937 rng(777u);
        std::vector<Item> big_items;
        big_items.reserve(big_n);
        for (std::size_t i = 0; i < big_n; ++i) {
            big_items.push_back(Item{1 + static_cast<std::size_t>(rng() % 60u),
                                     1 + static_cast<int>(rng() % 100u)});
        }

        WarmUp([&] { return static_cast<std::uint64_t>(Knapsack01Rolling(big_items, big_cap)); }, 1);

        const double t_2d = BenchMedianUs(
            [&] {
                return static_cast<std::uint64_t>(
                    Knapsack01Table(big_items, big_cap).back().back());
            },
            3);
        const double t_1d = BenchMedianUs(
            [&] { return static_cast<std::uint64_t>(Knapsack01Rolling(big_items, big_cap)); }, 3);
        const double t_complete = BenchMedianUs(
            [&] { return static_cast<std::uint64_t>(CompleteKnapsackRolling(big_items, big_cap)); }, 3);

        assert(Knapsack01Rolling(big_items, big_cap) ==
               Knapsack01Table(big_items, big_cap).back().back());

        Label("  0-1 背包 二维 O(n*W) 空间", 42);
        std::cout << ": " << t_2d << " us\n";
        Label("  0-1 背包 一维 O(W) 空间", 42);
        std::cout << ": " << t_1d << " us\n";
        Label("  完全背包 一维 O(W) 空间", 42);
        std::cout << ": " << t_complete << " us\n";
        Label("  二维 / 一维 倍数", 42);
        std::cout << ": " << (t_2d / t_1d) << " x\n";
        Note("");
        Note("规模：400 件物品 x 容量 2000 = 80 万个 DP 格子。");
        Note("一维写法除了省下 n 倍内存，还因为内存连续、把每一轮的表都留在了缓存里，");
        Note("所以同时赢得了时间和空间。工程上「能用一维就用一维」。");
    }

    // =======================================================================
    Section("4. 最长公共子序列 LCS");
    // =======================================================================
    const std::string sa = "ABCBDAB";
    const std::string sb = "BDCABA";

    std::cout << "\n";
    PrintDpRecipe("dp[i][j] = a 的前 i 个字符与 b 的前 j 个字符的最长公共子序列长度。",
                  "a[i-1]==b[j-1]: dp[i][j] = dp[i-1][j-1] + 1；否则 dp[i][j] = max(dp[i-1][j], dp[i][j-1])。",
                  "dp[0][j] = dp[i][0] = 0（有一边是空串，只能匹配出空串）。",
                  "第 i 行只依赖第 i-1 行 -> 一维滚动 + 一个 diag 变量保存被覆盖的左上角。");
    std::cout << "\n";

    const std::vector<std::vector<int>> lcs_table = LcsTable(sa, sb);
    const std::string lcs = LcsReconstruct(sa, sb, lcs_table);
    const int lcs_len = lcs_table.back().back();
    const int lcs_roll = LcsLengthRolling(sa, sb);
    const int lcs_brute = LcsBruteForce(sa, sb);

    std::cout << "  a = \"" << sa << "\"，b = \"" << sb << "\"\n";
    std::cout << "  LCS 长度（二维 DP） = " << lcs_len << "\n";
    std::cout << "  LCS 长度（滚动数组） = " << lcs_roll << "\n";
    std::cout << "  LCS 长度（暴力递归） = " << lcs_brute << "\n";
    std::cout << "  回溯重建出的一个 LCS = \"" << lcs << "\"\n";
    assert(lcs_len == lcs_roll);
    assert(lcs_len == lcs_brute);
    assert(static_cast<std::size_t>(lcs_len) == lcs.size());
    // 回溯出来的必须真的是两个串的公共子序列（LCS 不唯一，只能验证这一点 + 长度最优）
    assert(IsSubsequence(lcs, sa));
    assert(IsSubsequence(lcs, sb));
    Note("");
    Note("注意：LCS 通常不唯一。本例长度 4 的解有 \"BCBA\" 和 \"BDAB\" 等好几个，");
    Note("回溯时「相等就取、否则先往哪边退」的策略决定了你拿到哪一个。");
    Note("所以测试只能断言「长度最优 + 是公共子序列」，断言字符串内容会写出脆弱测试。");
    Note("复杂度：时间 O(|a|*|b|)，空间二维 O(|a|*|b|) / 滚动 O(|b|)。");

    // 随机小规模对拍
    std::cout << "\n";
    Note("随机对拍：12 组长度 8 到 12 的随机串，二维 DP / 滚动数组 / 暴力递归三方对比。");
    {
        std::mt19937 rng(4242u);
        for (int trial = 0; trial < 12; ++trial) {
            std::string x;
            std::string y;
            const std::size_t lx = 8 + static_cast<std::size_t>(rng() % 5u);
            const std::size_t ly = 8 + static_cast<std::size_t>(rng() % 5u);
            for (std::size_t i = 0; i < lx; ++i) {
                x.push_back(static_cast<char>('a' + static_cast<int>(rng() % 4u)));
            }
            for (std::size_t i = 0; i < ly; ++i) {
                y.push_back(static_cast<char>('a' + static_cast<int>(rng() % 4u)));
            }
            const int len = LcsTable(x, y).back().back();
            assert(len == LcsLengthRolling(x, y));
            assert(len == LcsBruteForce(x, y));
            const std::string one = LcsReconstruct(x, y, LcsTable(x, y));
            assert(IsSubsequence(one, x));
            assert(IsSubsequence(one, y));
        }
        Note("  12 组全部通过。（字母表故意只留 4 个字母，好让随机串里有较多重复、");
        Note("  LCS 更长，更容易暴露下标偏移类的错误。）");
    }

    // 大串计时
    {
        const std::string big_a = MakeRandomStringLike(600, "abcde", 31337u);
        const std::string big_b = MakeRandomStringLike(600, "abcde", 90210u);
        const double t_2d = BenchMedianUs(
            [&] {
                return static_cast<std::uint64_t>(LcsTable(big_a, big_b).back().back());
            },
            3);
        const double t_1d = BenchMedianUs(
            [&] { return static_cast<std::uint64_t>(LcsLengthRolling(big_a, big_b)); }, 3);
        std::cout << "\n";
        Label("  两个 600 字符的随机串，二维 DP", 42);
        std::cout << ": " << t_2d << " us\n";
        Label("  同一组数据，滚动数组", 42);
        std::cout << ": " << t_1d << " us\n";
        Label("  二维 / 一维 倍数", 42);
        std::cout << ": " << (t_2d / t_1d) << " x\n";
        assert(LcsTable(big_a, big_b).back().back() == LcsLengthRolling(big_a, big_b));
    }

    // =======================================================================
    Section("5. 最长递增子序列 LIS：O(n^2) DP 与 O(n log n) 贪心 + 二分");
    // =======================================================================
    std::cout << "\n";
    Note("解法一 O(n^2) 的四段式：");
    PrintDpRecipe("dp[i] = 以 nums[i] 结尾的最长严格递增子序列的长度。",
                  "dp[i] = max(dp[j] + 1)，对所有 j < i 且 nums[j] < nums[i]。",
                  "dp[i] 至少为 1（每个元素自己就是长度 1 的子序列）。",
                  "只需一个 best 变量记录 max(dp[i])；答案不是 dp[n-1]，必须取全局最大。");
    std::cout << "\n";
    Note("解法二 O(n log n) 的四段式：");
    PrintDpRecipe("tails[k] = 所有长度为 k+1 的严格递增子序列中，结尾元素的最小值。",
                  "对每个 x：二分找 tails 中第一个 >= x 的位置 pos；"
                  "pos 在末尾则追加（长度 +1），否则 tails[pos] = x。",
                  "tails 为空；空数组答案是 0。",
                  "只需要 tails 数组本身，O(n) 空间（严格说这已经不是 DP，是贪心 + 二分）。");
    std::cout << "\n";

    const std::vector<int> lis_demo = {10, 9, 2, 5, 3, 7, 101, 18};
    std::cout << "  演示序列：10, 9, 2, 5, 3, 7, 101, 18\n";
    std::cout << "  每步的 tails（每行末尾标明是追加还是替换）：\n";
    PrintTailsEvolution(lis_demo);
    const std::size_t lis_demo_len = LisNLogN(lis_demo);
    const std::vector<int> lis_demo_seq = LisReconstruct(lis_demo);
    std::cout << "  O(n log n) 结果长度 = " << lis_demo_len << "，O(n^2) 结果长度 = "
              << LisQuadratic(lis_demo) << "，暴力 = " << LisBruteForce(lis_demo) << "\n";
    std::cout << "  重建出的一条 LIS = ";
    for (const int x : lis_demo_seq) {
        std::cout << x << " ";
    }
    std::cout << "\n";
    assert(lis_demo_len == LisQuadratic(lis_demo));
    assert(lis_demo_len == LisBruteForce(lis_demo));
    assert(lis_demo_len == lis_demo_seq.size());
    Note("");
    Note("关键理解：tails 不是答案，也不能直接拼成答案。它只是「每个长度能达到的最小结尾」。");
    Note("要还原具体序列，必须额外记录前驱下标（见 LisReconstruct）；只用 tails 是还原不出来的。");
    Note("二分可行的原因是 tails 单调递增（代码注释里有证明思路）。");

    // 严格递增 vs 非递减
    {
        const std::vector<int> dup = {2, 2, 2, 2};
        std::cout << "\n";
        std::cout << "  重复元素 {2,2,2,2}：严格递增用 lower_bound -> " << LisNLogN(dup)
                  << "；非递减用 upper_bound -> " << LndsNLogN(dup) << "\n";
        assert(LisNLogN(dup) == 1);
        assert(LndsNLogN(dup) == 4);
        assert(LndsNLogN(dup) == LndsQuadratic(dup));
        assert(LisNLogN(dup) == LisQuadratic(dup));
        Note("  这就是那个坑：写非递减 LIS 时如果沿用 lower_bound，答案会偏小。");
        Note("  记忆法：lower_bound 找第一个 >= x（会把相等的替换掉 -> 不容相等）；");
        Note("          upper_bound 找第一个 > x（相等的留在后面 -> 允许相等）。");
    }

    // 随机对拍
    std::cout << "\n";
    Note("随机对拍：20 组长度 <= 14 的随机数组，暴力枚举与两种 DP 必须完全一致。");
    {
        std::mt19937 rng(31415u);
        for (int trial = 0; trial < 20; ++trial) {
            std::vector<int> nums = MakeRandomInts(12 + static_cast<std::size_t>(rng() % 3u), -5, 9, rng());
            const std::size_t a = LisQuadratic(nums);
            const std::size_t b = LisNLogN(nums);
            const std::size_t c = LisBruteForce(nums);
            assert(a == b);
            assert(b == c);
            const std::vector<int> seq = LisReconstruct(nums);
            assert(seq.size() == b);
            for (std::size_t i = 1; i < seq.size(); ++i) {
                assert(seq[i - 1] < seq[i]);
            }
        }
        Note("  20 组全部通过（O(n^2) == O(n log n) == 暴力枚举），且重建序列确实严格递增。");
    }

    // 计时对比
    {
        std::cout << "\n";
        Note("耗时对比（同一个随机数组，n 逐档翻倍，看增长趋势）：");
        Label("n", 8);
        Label("O(n^2) us", 16);
        Label("O(n log n) us", 18);
        Label("倍数", 12);
        std::cout << "\n" << std::string(56, '-') << "\n";
        for (const std::size_t n : {std::size_t{1000}, std::size_t{2000}, std::size_t{4000}}) {
            const std::vector<int> nums = MakeRandomInts(n, 0, 100000, 2718u);
            const double t_slow = BenchMedianUs([&] { return LisQuadratic(nums); }, 3);
            const double t_fast = BenchMedianUs(
                [&] { return static_cast<std::uint64_t>(Repeat([&] { return LisNLogN(nums); }, 5)); }, 3);
            assert(LisQuadratic(nums) == LisNLogN(nums));
            std::cout << std::setw(8) << n << std::setw(16) << t_slow << std::setw(18) << t_fast
                      << std::setw(12) << (t_slow / t_fast) << "\n";
        }
        Note("");
        Note("注意第二列套了 5 次内部重复（它太快，直接测会落进计时器噪声），");
        Note("所以「倍数」这一列被这 5 倍除小了，看趋势就好：n 翻倍时，");
        Note("O(n^2) 涨约 4 倍，O(n log n) 涨约 2.2 倍——n 越大差距拉得越开。");
    }

    // =======================================================================
    Section("6. 编辑距离（Levenshtein Distance）");
    // =======================================================================
    std::cout << "\n";
    PrintDpRecipe("dp[i][j] = 把 a 的前 i 个字符改成 b 的前 j 个字符所需的最少操作数。",
                  "dp[i][j] = min(dp[i-1][j] + 1 /*删除*/, "
                  "dp[i][j-1] + 1 /*插入*/, dp[i-1][j-1] + (a[i-1]!=b[j-1]) /*替换*/)。",
                  "dp[i][0] = i（删掉 a 的前 i 个字符）；dp[0][j] = j（插入 j 个字符）。",
                  "只依赖上一行和本行左边一格 -> 一维滚动 + diag 变量保存左上角。");
    std::cout << "\n";
    Note("三个方向的直觉：");
    Note("  左  dp[i][j-1] -> 插入：a 已经能变成 b 的前 j-1 个字符了，再补一个 b[j-1]；");
    Note("  上  dp[i-1][j] -> 删除：a 的前 i-1 个字符已经能变成 b 的前 j 个，多出来的 a[i-1] 删掉；");
    Note("  左上 dp[i-1][j-1] -> 替换：把 a[i-1] 改写成 b[j-1]（相等时就不用改，免费）。");
    std::cout << "\n";

    struct EditCase {
        const char* a;
        const char* b;
        std::size_t expected;
    };
    const EditCase cases[] = {
        {"kitten", "sitting", 3},
        {"Saturday", "Sunday", 3},
        {"", "", 0},
        {"abc", "", 3},
        {"", "xyz", 3},
        {"abc", "abc", 0},
        {"flaw", "lawn", 2},
    };
    Label("  a", 14);
    Label("b", 14);
    Label("DP", 8);
    Label("滚动", 8);
    Label("暴力", 8);
    Label("期望", 8);
    std::cout << "\n  " << std::string(56, '-') << "\n";
    for (const EditCase& c : cases) {
        const std::string x = c.a;
        const std::string y = c.b;
        const std::size_t d2 = EditDistanceTable(x, y).back().back();
        const std::size_t d1 = EditDistanceRolling(x, y);
        const std::size_t db = EditDistanceBruteForce(x, y);
        assert(d2 == d1);
        assert(d1 == db);
        assert(d2 == c.expected);
        Label(std::string("  ") + (x.empty() ? "(空)" : x), 14);
        Label(y.empty() ? "(空)" : y, 14);
        std::cout << std::setw(8) << d2 << std::setw(8) << d1 << std::setw(8) << db
                  << std::setw(8) << c.expected << "\n";
    }
    Note("");
    Note("kitten -> sitting 需要 3 步（k->s 替换、e->i 替换、末尾插入 g），");
    Note("这是编辑距离最经典的教科书例子，用来验证实现时把它写进 assert 最省事。");

    // 随机对拍 + 大串计时
    {
        std::mt19937 rng(1618u);
        for (int trial = 0; trial < 15; ++trial) {
            std::string x;
            std::string y;
            const std::size_t lx = 1 + static_cast<std::size_t>(rng() % 6u);
            const std::size_t ly = 1 + static_cast<std::size_t>(rng() % 6u);
            for (std::size_t i = 0; i < lx; ++i) {
                x.push_back(static_cast<char>('a' + static_cast<int>(rng() % 3u)));
            }
            for (std::size_t i = 0; i < ly; ++i) {
                y.push_back(static_cast<char>('a' + static_cast<int>(rng() % 3u)));
            }
            assert(EditDistanceTable(x, y).back().back() == EditDistanceRolling(x, y));
            assert(EditDistanceRolling(x, y) == EditDistanceBruteForce(x, y));
            // 编辑距离必须对称：把 a 变成 b 和把 b 变成 a 的代价相同
            assert(EditDistanceRolling(x, y) == EditDistanceRolling(y, x));
        }
        std::cout << "\n";
        Note("15 组随机小串对拍通过，并且额外验证了「编辑距离是对称的」这条性质。");
    }
    {
        const std::string big_a = MakeRandomStringLike(400, "abcd", 5150u);
        const std::string big_b = MakeRandomStringLike(400, "abcd", 6161u);
        const double t_2d = BenchMedianUs(
            [&] {
                return static_cast<std::uint64_t>(EditDistanceTable(big_a, big_b).back().back());
            },
            3);
        const double t_1d = BenchMedianUs(
            [&] { return static_cast<std::uint64_t>(EditDistanceRolling(big_a, big_b)); }, 3);
        std::cout << "\n";
        Label("  两个 400 字符的串，二维 DP", 40);
        std::cout << ": " << t_2d << " us\n";
        Label("  同一组数据，一维滚动", 40);
        std::cout << ": " << t_1d << " us\n";
        Label("  二维 / 一维 倍数", 40);
        std::cout << ": " << (t_2d / t_1d) << " x\n";
        assert(EditDistanceTable(big_a, big_b).back().back() == EditDistanceRolling(big_a, big_b));
    }

    // =======================================================================
    Section("7. 打家劫舍（House Robber）：选与不选");
    // =======================================================================
    std::cout << "\n";
    PrintDpRecipe("dp[i] = 只考虑前 i 间房子能偷到的最大金额。",
                  "dp[i] = max(dp[i-1] /*不偷第 i 间*/, dp[i-2] + money[i-1] /*偷第 i 间*/)。",
                  "dp[0] = 0（没有房子）；dp[1] = money[0]。",
                  "只依赖前两项 -> 两个变量，O(1) 空间。");
    std::cout << "\n";
    Note("为什么偷第 i 间时第 i-1 间必须放过？因为相邻会触发报警，这是题目的硬约束。");
    Note("「选与不选」是最常见的一类转移：把「对当前元素的决策」直接写成 max 的两个分支。");
    std::cout << "\n";

    const std::vector<int> houses = {2, 7, 9, 3, 1};
    const int rob_linear = RobLinear(houses);
    const int rob_const = RobConstantSpace(houses);
    const int rob_brute = RobBruteForce(houses);
    std::cout << "  金额序列：2, 7, 9, 3, 1\n";
    std::cout << "  O(n) 空间 DP = " << rob_linear << "；O(1) 空间 DP = " << rob_const
              << "；暴力枚举 = " << rob_brute << "\n";
    assert(rob_linear == 12);
    assert(rob_linear == rob_const);
    assert(rob_const == rob_brute);
    Note("  答案是 12：偷第 1、3、5 间（2 + 9 + 1），它们互不相邻；");
    Note("  另一条同样拿到 12 的路是 7 + 3 + 1 = 11，不如它；贪心地拿最大的 9 也不是最优。");

    {
        std::mt19937 rng(2718u);
        for (int trial = 0; trial < 20; ++trial) {
            std::vector<int> money = MakeRandomInts(14, 0, 50, rng());
            const int a = RobLinear(money);
            const int b = RobConstantSpace(money);
            const int c = RobBruteForce(money);
            assert(a == b);
            assert(b == c);
        }
        std::cout << "\n";
        Note("20 组随机用例（长度 14）全部通过：O(n) 空间 == O(1) 空间 == 暴力枚举 2^14。");
        Note("复杂度：时间 O(n)；空间从 O(n) 优化到 O(1)。");
    }

    // =======================================================================
    Section("8. 零钱兑换：最少硬币数 vs 方案数");
    // =======================================================================
    const std::vector<int> coins = {1, 2, 5};

    std::cout << "\n";
    Note("变体一：最少硬币数。");
    PrintDpRecipe("dp[a] = 凑出金额 a 所需的最少硬币数。",
                  "dp[a] = min(dp[a], dp[a - c] + 1)，对所有硬币 c <= a。",
                  "dp[0] = 0；其余初始化为 amount+1 当「不可达」（用 INT_MAX 会溢出）。",
                  "一维 O(amount)；min 对遍历顺序不敏感，外层金额或外层硬币都正确。");
    std::cout << "\n";
    Note("变体二：方案数（组合，不区分顺序）。");
    PrintDpRecipe("dp[a] = 用给定硬币凑出金额 a 的方案数。",
                  "dp[a] += dp[a - c]；每枚硬币只在「已考虑过的硬币集合」上累加一次。",
                  "dp[0] = 1（凑 0 元有 1 种方案：什么都不选。写成 0 会让所有方案数变成 0）。",
                  "一维 O(amount)，但外层的遍历顺序从「可选」变成了「必须」：外层遍历硬币。");
    std::cout << "\n";

    const int amount = 11;
    const int min_coins = CoinChangeMinCoins(coins, amount);
    const int min_coins_brute = CoinChangeMinBruteForce(coins, amount);
    std::cout << "  硬币 {1, 2, 5}，目标金额 11：最少硬币数 = " << min_coins
              << "（5+5+1），暴力验证 = " << min_coins_brute << "\n";
    assert(min_coins == 3);
    assert(min_coins == min_coins_brute);
    assert(CoinChangeMinCoins(coins, 3) == 2);   // 2+1
    assert(CoinChangeMinCoins(coins, 7) == 2);   // 5+2
    assert(CoinChangeMinCoins({2}, 3) == -1);    // 凑不出来

    std::cout << "\n";
    Note("方案数那一道题，用金额 5 做对比（这个例子最能说明「顺序」的代价）：");
    const int way_amount = 5;
    const std::uint64_t ways_comb = CoinChangeWaysCombination(coins, way_amount);
    const std::uint64_t ways_perm = CoinChangeWaysPermutation(coins, way_amount);
    const std::uint64_t ways_brute = CoinChangeWaysBruteForce(coins, way_amount);
    std::cout << "  外层硬币（组合数，正确答案） = " << ways_comb << "\n";
    std::cout << "  外层金额（排列数，另一道题） = " << ways_perm << "\n";
    std::cout << "  暴力穷举（组合口径）         = " << ways_brute << "\n";
    assert(ways_comb == 4);            // 5；2+2+1；2+1+1+1；1+1+1+1+1
    assert(ways_comb == ways_brute);
    assert(ways_perm == 9);            // 9 种有序拆分
    assert(ways_perm > ways_comb);
    Note("");
    Note("  组合数 4 种：{5}、{2,2,1}、{2,1,1,1}、{1,1,1,1,1}；");
    Note("  排列数 9 种：上面 4 种里，含多个不同硬币的还会因为顺序不同被重复计数，");
    Note("             例如 2+2+1 / 2+1+2 / 1+2+2 在排列口径下是 3 种。");
    Note("  这就是「答案偏大」类 bug 的典型来源：转移方程写对了，遍历顺序写错了。");

    {
        std::mt19937 rng(999u);
        for (int trial = 0; trial < 25; ++trial) {
            const int amt = 1 + static_cast<int>(rng() % 40u);
            const std::uint64_t comb = CoinChangeWaysCombination(coins, amt);
            const std::uint64_t brute_ways = CoinChangeWaysBruteForce(coins, amt);
            const std::uint64_t perm = CoinChangeWaysPermutation(coins, amt);
            assert(comb == brute_ways);
            assert(comb <= perm);  // 排列数永远不少于组合数
            assert(CoinChangeMinCoins(coins, amt) == CoinChangeMinBruteForce(coins, amt));
        }
        std::cout << "\n";
        Note("25 组随机金额（1 到 40）全部通过：组合数 == 暴力穷举，且组合数 <= 排列数，");
        Note("最少硬币数也与暴力一致。");
        Note("复杂度：两者都是时间 O(amount * coins)，空间 O(amount)。");
    }

    // =======================================================================
    Section("9. 怎么识别一道题该用 DP");
    // =======================================================================
    Note("三个判据，缺一不可：");
    std::cout << "\n";
    Label("  1) 最优子结构", 20);
    std::cout << "最优解可以由子问题的最优解拼出来。\n";
    std::cout << "                    反例：求「最长简单路径」就没有最优子结构，因为子路径"
                 "最优不代表整体最优。\n";
    Label("  2) 重叠子问题", 20);
    std::cout << "暴力递归会反复求解同一个子问题。\n";
    std::cout << "                    本文件第一节的调用次数表就是它的量化证据：n = 32 时"
                 "朴素递归 7049155 次调用，\n";
    std::cout << "                    记忆化只进入 63 次、其中真正求解的只有 33 个子问题。\n";
    Label("  3) 无后效性", 20);
    std::cout << "一旦状态确定，未来的决策只依赖这个状态，与「怎么走到这里」无关。\n";
    std::cout << "                    这条决定了状态里要放什么信息：凡是影响未来的信息"
                 "都必须进状态。\n";
    std::cout << "\n";

    Note("对照清单：看到这些特征，优先想 DP。");
    std::cout << "    求「最大 / 最小 / 最多 / 最少 / 方案数」，而不是「列出所有具体方案」\n";
    std::cout << "    问题能按阶段推进，后面的阶段依赖前面阶段的结果\n";
    std::cout << "    输入里有一个可以当数组下标的量：容量、金额、长度、位置\n";
    std::cout << "    暴力递归的递归树上出现重复节点（画一画就知道）\n";
    std::cout << "    题目问的是「前 i 个……」这种前缀性质\n";
    std::cout << "\n";
    Note("用本文件的例子对号入座：");
    Label("    斐波那契 / 爬楼梯", 26);
    std::cout << "状态是「第几项 / 第几阶」，重叠子问题最纯粹的形态\n";
    Label("    0-1 背包 / 完全背包", 26);
    std::cout << "状态是「前 i 件物品 x 容量 j」，容量可以当下标；选与不选是决策\n";
    Label("    LCS / 编辑距离", 26);
    std::cout << "状态是「两个串的前缀」，天然二维；答案是「最大长度 / 最小操作数」\n";
    Label("    LIS", 26);
    std::cout << "状态是「以 i 结尾」，无后效性靠「结尾元素」这个信息保证\n";
    Label("    打家劫舍", 26);
    std::cout << "状态是「前 i 间房子」，决策是偷或不偷\n";
    Label("    零钱兑换", 26);
    std::cout << "状态是「金额 a」，决策是「最后一枚硬币用哪种」\n";
    std::cout << "\n";

    Note("和别的范式的分界线：");
    Label("    贪心", 22);
    std::cout << "能证明「局部最优 -> 全局最优」时用它，比 DP 快得多也简单得多。\n";
    std::cout << "                          反例就在本文件里：LCS 不能贪心，LIS 的 O(n log n) "
                 "也不是纯贪心，\n";
    std::cout << "                          它是「贪心 + 二分」——而且 tails 数组本身不是答案。\n";
    Label("    分治", 22);
    std::cout << "子问题不重叠时（归并排序、快速排序、二分查找）不需要缓存，分治就够了。\n";
    std::cout << "                          一旦子问题开始重叠，分治就退化成指数级——这正是"
                 "朴素斐波那契的下场。\n";
    Label("    记忆化搜索", 22);
    std::cout << "转移的拓扑序不好写、或者只会用到一部分状态时，用自顶向下更省事。\n";
    std::cout << "                          代价是递归栈和查表的常数；本文件第一节实测"
                 "递推比记忆化更快。\n";
    Label("    搜索 + 剪枝", 20);
    std::cout << "状态空间稀疏、约束强、只要求找一个可行解时，搜索往往比 DP 实用。\n";
    std::cout << "\n";
    Note("最后一句工程建议（也是本文件反复强调的）：先想清楚状态定义，再写代码。");
    Note("DP 写错不会崩、不会慢，只会安静地给出错误答案，而且跑得又快又稳。");
    Note("所以每个解法都配上 assert 或暴力对拍——这不是教学代码的啰嗦，是生产纪律。");

    // =======================================================================
    Section("10. 实测小结");
    // =======================================================================
    Label("  斐波那契 n = 32 朴素递归调用次数", 40);
    std::cout << ": " << naive_calls_at_32 << "\n";
    Label("  斐波那契 n = 32 记忆化进入次数", 40);
    std::cout << ": " << memo_calls_at_32 << "（倍数 " << (naive_calls_at_32 / memo_calls_at_32)
              << " x）\n";
    Label("  斐波那契 n = 32 真正求解的子问题数", 40);
    std::cout << ": " << memo_solves_at_32 << "（n+1）\n";
    Label("  斐波那契 n = 32 朴素递归耗时", 40);
    std::cout << ": " << naive_us[kNaiveCount - 1] << " us（实测）\n";
    Label("  n = 40 朴素递归预计耗时", 40);
    std::cout << ": " << est_from_calls << " 秒（【外推】，不是实测）\n";
    Note("");
    Note("三句话总结：");
    Note("  1) 记忆化搜索把指数级的重复计算消掉，换来的是数量级级别的加速；");
    Note("  2) 递推 + 空间优化通常还能再快一截，因为它没有递归和查表的开销；");
    Note("  3) 背包那一节的「倒序 / 正序」不是细节，它直接决定你解的是哪一道题。");
    Note("");
    Note("再次提醒：以上所有绝对耗时都来自 Debug（/Od）构建，");
    Note("数字仅供参考，趋势和数量级才有意义。");

    std::cout << "\n[校验] 累加器非零，说明所有测量结果都真实参与了运算: "
              << (g_sink != 0 ? "是" : "否") << "\n";
    return 0;
}
