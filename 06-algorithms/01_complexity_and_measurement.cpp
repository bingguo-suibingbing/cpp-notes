// ============================================================================
// 01_complexity_and_measurement.cpp
// 演示主题：
//   1. 大 O 记号的工程含义：忽略常数与低阶项，但「常数」在真实机器上决定胜负
//   2. 常见增长阶的实测曲线：O(1) / O(log n) / O(n) / O(n log n) / O(n^2) / O(2^n)
//   3. 均摊复杂度：std::vector::push_back 为什么是均摊 O(1)
//   4. 最好 / 最坏 / 平均情况是三个不同的数字
//   5. 空间复杂度：容量、对齐、以及「时间换空间」的取舍
//   6. 怎么用 std::chrono 做「可信」的测量
//      （预热、多次重复、取中位数、防止结果被优化掉）
//
// 关键结论：
//   - 大 O 只描述「增长趋势」。n 不够大时，低阶算法完全可能因为常数小而反超；
//   - 所以工程上的顺序永远是「先测量，再优化」，而不是「凭复杂度下结论」；
//   - 测量本身是技术活：单次计时几乎总是垃圾数据，必须重复取中位数；
//   - 均摊 O(1) 不等于每次 O(1)：单次扩容的那一下是 O(n)，只是被后面的操作摊薄了。
//
// 说明：本文件在 Debug（/Od）下编译运行。Debug 的绝对数字比 Release 慢很多，
//       这里所有数字仅供参考，看「趋势」和「数量级差异」才有意义。
//
// 教学用途说明：本文件里的 BenchMedianUs / Repeat / g_sink 这一套是【教学用的
// 最小测量框架】，目的是把测量原理摊开给你看。生产环境做基准测试请用成熟的
// 基准库（C++ 用 Google Benchmark，或用 Catch2 的 BENCHMARK 宏），
// 它们处理了预热、统计显著性、编译器屏障、结果上报等一大堆容易做错的细节。
// ============================================================================

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <list>
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
// steady_clock 的实际分辨率在百纳秒量级——直接测一次，测到的全是噪声
// （这正是前面 O(1) 倍数「不可信」的原因）。
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
// 造数据
// ---------------------------------------------------------------------------
std::vector<int> MakeRandom(std::size_t n, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<int> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = static_cast<int>(rng() % 1000000u);
    }
    return v;
}

// ===========================================================================
// 各增长阶的「工作负载」
// 每个函数都返回一个 uint64_t，目的只是让结果能喂给 g_sink，避免被优化掉。
// ===========================================================================

// O(1)：无论 n 多大，只做一次数组访问。
std::uint64_t OpConstant(const std::vector<int>& v) {
    return static_cast<std::uint64_t>(v[0]);
}

// O(log n)：二分查找。每次比较都能把候选区间砍掉一半。
std::uint64_t OpLogarithmic(const std::vector<int>& sorted_v) {
    const int target = sorted_v[sorted_v.size() / 2];
    const auto it = std::lower_bound(sorted_v.begin(), sorted_v.end(), target);
    return static_cast<std::uint64_t>(it - sorted_v.begin());
}

// O(n)：一次线性求和。
std::uint64_t OpLinear(const std::vector<int>& v) {
    std::uint64_t sum = 0;
    for (const int x : v) {
        sum += static_cast<std::uint64_t>(x);
    }
    return sum;
}

// O(n log n)：完整排一遍序。先拷贝再排序，保证每次测量的输入都是乱序的
// （已排好序的输入对排序算法来说是最坏/最好情况，会污染数据）。
std::uint64_t OpLinearithmic(std::vector<int>& buf, const std::vector<int>& src) {
    std::copy(src.begin(), src.end(), buf.begin());
    std::sort(buf.begin(), buf.end());
    return static_cast<std::uint64_t>(buf[buf.size() / 2]);
}

// O(n^2)：双重循环。n 翻倍，工作量翻 4 倍——这就是「平方爆炸」。
std::uint64_t OpQuadratic(const std::vector<int>& v) {
    std::uint64_t sum = 0;
    const std::size_t n = v.size();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            sum += static_cast<std::uint64_t>(v[i] ^ v[j]);
        }
    }
    return sum;
}

// O(2^n)：朴素递归斐波那契。fib(n) 调用 fib(n-1) 和 fib(n-2)，
// 而 fib(n-2) 会被重复计算无数次——调用次数本身按指数增长。
// 复杂度递推：T(n) = T(n-1) + T(n-2) + O(1)，解出来是 O(phi^n)，phi ≈ 1.618，
// 也就是 O(2^0.694n)。这就是「指数爆炸」的来源。
std::uint64_t FibNaive(int n) {
    if (n < 2) {
        return static_cast<std::uint64_t>(n);
    }
    return FibNaive(n - 1) + FibNaive(n - 2);
}

// ---------------------------------------------------------------------------
// 第 4 节：常数因子的威力——同样 O(n) 的求和，性能差一个数量级
// ---------------------------------------------------------------------------
// 两者都是「把 n 个数加一遍」，理论复杂度完全一样，都是 O(n)。
// 但 vector 的元素在内存里连续排列，CPU 预取器能猜到你下一步要访问哪，
// 一次内存传输（cache line, 通常 64 字节）就能带回 16 个 int；
// list 的节点散落在堆的各个角落，每走一步都可能是一次 cache miss。
// 这是「复杂度相同 ≠ 性能相同」最直观的例子。
std::uint64_t SumContiguous(const std::vector<int>& v) {
    std::uint64_t sum = 0;
    for (const int x : v) {
        sum += static_cast<std::uint64_t>(x);
    }
    return sum;
}

std::uint64_t SumPointerChasing(const std::list<int>& l) {
    std::uint64_t sum = 0;
    for (const int x : l) {
        sum += static_cast<std::uint64_t>(x);
    }
    return sum;
}

// ---------------------------------------------------------------------------
// 第 6 节：最好 / 最坏 / 平均情况
// ---------------------------------------------------------------------------
std::size_t LinearSearch(const std::vector<int>& v, int target) {
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == target) {
            return i;
        }
    }
    return v.size();  // 没找到
}

void PrintRatioHeader() {
    std::cout << std::setw(10) << "n"
              << std::setw(16) << "O(1) us"
              << std::setw(16) << "O(log n) us"
              << std::setw(16) << "O(n) us"
              << std::setw(16) << "O(n log n) us"
              << std::setw(16) << "O(n^2) us" << "\n";
    std::cout << std::string(90, '-') << "\n";
}

}  // namespace

int main() {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "本文件在 Debug 配置下运行，绝对耗时比 Release 慢，请只看趋势。\n";

    // =======================================================================
    Section("0. 先证明工作负载本身是对的（否则测出来的时间没有意义）");
    // =======================================================================
    Note("性能测量有一个隐含前提：被测代码必须真的在做你想测的那件事。");
    Note("如果工作负载本身算错了（求和漏了一项、二分找错了位置、排序没排干净），");
    Note("那么再精确的计时也只是在精确地测量一个错误的东西。");
    Note("所以先用 assert 把每个工作负载的正确性钉死，再去测它的耗时。");
    std::cout << "\n";

    {
        const std::vector<int> small = {5, 3, 9, 1, 7, 2, 8, 4, 6, 0};

        // O(1)：读的就是首元素
        assert(OpConstant(small) == 5u);

        // O(n) 求和：与手写的独立参照对拍
        const std::uint64_t direct_sum = OpLinear(small);
        std::uint64_t reference_sum = 0;
        for (const int x : small) {
            reference_sum += static_cast<std::uint64_t>(x);
        }
        assert(direct_sum == reference_sum);

        // O(log n) 二分：找到的位置必须真的是目标值，且是最左边那个（lower_bound 的定义）
        std::vector<int> s = small;
        std::sort(s.begin(), s.end());
        const std::size_t pos = static_cast<std::size_t>(OpLogarithmic(s));
        assert(pos < s.size());
        assert(s[pos] == s[s.size() / 2]);
        assert(pos == 0 || s[pos - 1] < s[pos]);

        // O(n log n)：排完之后必须真的有序，且必须是原数组的一个排列
        std::vector<int> buf(small.size());
        (void)OpLinearithmic(buf, small);
        assert(std::is_sorted(buf.begin(), buf.end()));
        std::vector<int> sorted_small = small;
        std::sort(sorted_small.begin(), sorted_small.end());
        assert(buf == sorted_small);

        // O(n^2)：在极小规模上暴力核对
        {
            const std::vector<int> tiny = {1, 2, 3};
            std::uint64_t expect = 0;
            for (const int a : tiny) {
                for (const int b : tiny) {
                    expect += static_cast<std::uint64_t>(a ^ b);
                }
            }
            assert(OpQuadratic(tiny) == expect);
        }

        // O(2^n)：与已知的斐波那契数列对拍
        const std::uint64_t kKnownFib[] = {0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55};
        for (int i = 0; i <= 10; ++i) {
            assert(FibNaive(i) == kKnownFib[i]);
        }

        // 两个「都是 O(n)」的求和必须给出同一个结果——它们本来就是同一件事的两种实现
        const std::list<int> small_list(small.begin(), small.end());
        assert(SumContiguous(small) == SumPointerChasing(small_list));

        // 线性查找的三种情况都返回正确下标
        assert(LinearSearch(small, 5) == 0);
        assert(LinearSearch(small, 0) == 9);
        assert(LinearSearch(small, -1) == small.size());  // 找不到则返回 size()

        std::cout << "  所有工作负载都与独立参照实现一致 -> OK\n";
        std::cout << "  （对拍通过之后，后面的计时才有可信度）\n";
    }

    // =======================================================================
    Section("1. 常见增长阶的实测曲线");
    // =======================================================================
    Note("做法：对同一批 n，分别测量每个增长阶的工作负载，重复 3 次取中位数。");
    Note("看点：n 每次翻倍时，耗时变成原来的几倍；这个「倍数」就是增长阶的指纹。");
    Note("      O(1) -> 1 倍，O(log n) -> 略大于 1 倍，O(n) -> 约 2 倍，");
    Note("      O(n log n) -> 约 2.1 倍，O(n^2) -> 约 4 倍。");
    std::cout << "\n";
    Note("测量细节：O(1)、O(log n)、O(n) 在 n 很小时只要几纳秒，直接计时会被");
    Note("计时器噪声淹没。所以给它们各自套一个「内部重复次数」，把总耗时抬到");
    Note("几十微秒以上。乘一个常数不改变增长阶，只提高信噪比。");
    std::cout << "\n";

    const int kRepeats = 3;
    const std::size_t kSizes[] = {500, 1000, 2000, 4000};
    constexpr std::size_t kSizeCount = sizeof(kSizes) / sizeof(kSizes[0]);

    // 各工作负载的内部重复次数：只为把单次测量抬离噪声区，不影响结论。
    constexpr int kRepConstant = 200000;  // 一次只要几纳秒，必须跑很多次
    constexpr int kRepLog = 20000;        // 一次几十纳秒
    constexpr int kRepLinear = 50;        // 一次几微秒
    constexpr int kRepNLogn = 1;          // 一次几百微秒，本来就够大
    constexpr int kRepQuadratic = 1;      // 一次几毫秒以上

    // 统一的测量入口：op 表示「一次操作」，repeat 是内部重复次数。
    auto measure = [&](auto&& op, int repeat) {
        return BenchMedianUs([&] { return Repeat(op, repeat); }, kRepeats);
    };

    // 结果表：results[i][j] = 第 i 个 n 下第 j 种增长阶的耗时（微秒）。
    // 先全部收集再打印，避免为了算倍数而把昂贵的负载测两遍。
    double results[kSizeCount][5] = {};

    PrintRatioHeader();

    for (std::size_t i = 0; i < kSizeCount; ++i) {
        const std::size_t n = kSizes[i];
        std::vector<int> data = MakeRandom(n, 12345u);
        std::vector<int> sorted_data = data;
        std::sort(sorted_data.begin(), sorted_data.end());
        std::vector<int> sort_buf(n);

        results[i][0] = measure([&] { return OpConstant(data); }, kRepConstant);
        results[i][1] = measure([&] { return OpLogarithmic(sorted_data); }, kRepLog);
        results[i][2] = measure([&] { return OpLinear(data); }, kRepLinear);
        results[i][3] = measure([&] { return OpLinearithmic(sort_buf, data); }, kRepNLogn);
        results[i][4] = measure([&] { return OpQuadratic(data); }, kRepQuadratic);

        std::cout << std::setw(10) << n;
        for (int col = 0; col < 5; ++col) {
            std::cout << std::setw(16) << results[i][col];
        }
        std::cout << "\n";
    }

    Note("");
    Note("结论：O(n^2) 那一列在 n 只有 4000 的时候就已经比 O(n log n) 慢了两个数量级；");
    Note("      n 越大，差距只会越拉越开。这就是「选对算法」比「写快代码」重要得多的原因。");

    // 增长倍数表：用最后两行算比例（n 从 2000 到 4000，正好翻倍）。
    {
        constexpr double kResolutionFloorUs = 0.05;
        auto ratio = [&](const std::string& name, double base, double cur) {
            Label(std::string("    ") + name, 34);
            if (base < kResolutionFloorUs) {
                // 基准值掉到计时器分辨率附近时，算出来的倍数纯粹是噪声。
                // 承认测不准，比编一个好看的数字诚实得多。
                std::cout << " 低于计时器分辨率，倍数不可信\n";
            } else {
                std::cout << " x " << (cur / base) << "\n";
            }
        };

        constexpr std::size_t kLo = 2;  // kSizes[2] = 2000
        constexpr std::size_t kHi = 3;  // kSizes[3] = 4000

        std::cout << "\n";
        Note("n 从 2000 翻倍到 4000 时的耗时倍数（理论预期见括号）：");
        ratio("O(1)        (理论 1)", results[kLo][0], results[kHi][0]);
        ratio("O(log n)    (理论约 1.1)", results[kLo][1], results[kHi][1]);
        ratio("O(n)        (理论 2)", results[kLo][2], results[kHi][2]);
        ratio("O(n log n)  (理论约 2.1)", results[kLo][3], results[kHi][3]);
        ratio("O(n^2)      (理论 4)", results[kLo][4], results[kHi][4]);
        Note("实测倍数不会精确等于理论值：常数项、缓存效应、测量噪声都会干扰。");
        Note("但只要趋势对得上，就说明你的复杂度分析是对的。");
    }

    // =======================================================================
    Section("2. 指数爆炸：O(2^n) 单独看");
    // =======================================================================
    Note("朴素递归斐波那契。注意 n 只从 10 加到 28，耗时就已经涨了几千倍。");
    Note("如果 n = 40，按这个趋势要再慢约 1000 倍以上——这就是「肉眼可见的卡死」。");
    Note("（工程解法是记忆化 / 递推，见 10_dynamic_programming.cpp。）");
    std::cout << "\n";

    Label("n", 8);
    Label("fib(n) 耗时 (us)", 22);
    Label("相对前一档倍数", 20);
    std::cout << "\n" << std::string(50, '-') << "\n";

    double prev_fib = 0.0;
    for (const int n : {10, 14, 18, 22, 26, 28}) {
        const double t = BenchMedianUs([&] { return FibNaive(n); }, 3);
        std::cout << std::setw(8) << n << std::setw(22) << t;
        if (prev_fib > 0.0) {
            std::cout << std::setw(20) << (t / prev_fib);
        } else {
            std::cout << std::setw(20) << "-";
        }
        std::cout << "\n";
        prev_fib = t;
    }
    Note("");
    Note("结论：每加 4 到 n，耗时大约乘以 phi^4 ≈ 6.85。指数增长的可怕之处在于");
    Note("      它不是「慢一点」，而是「换个 n 就不可用」。");

    // =======================================================================
    Section("3. 举一反三：均摊复杂度（vector::push_back）");
    // =======================================================================
    Note("vector 满了以后必须重新分配一块更大的内存，并把老元素搬过去。");
    Note("这一次 push_back 的代价是 O(n)，但为什么我们说 push_back 是均摊 O(1)？");
    Note("因为扩容的间隔是按倍数拉长的：容量 1,2,3,4,6,9,13... 每次扩容前");
    Note("都已经插入了和当前容量同量级的元素。把总搬移次数加起来：");
    Note("  n + n/1.5 + n/2.25 + ... < 3n，是 O(n)。摊到 n 次 push_back 上就是 O(1)。");
    std::cout << "\n";

    {
        const int kPushCount = 200000;

        // (a) 观察 MSVC 的扩容倍数，并统计「总搬移次数」。
        std::vector<int> v;
        std::size_t last_capacity = v.capacity();
        std::size_t total_moves = 0;
        std::vector<std::size_t> capacity_steps;

        for (int i = 0; i < kPushCount; ++i) {
            v.push_back(i);
            if (v.capacity() != last_capacity) {
                total_moves += last_capacity;  // 这次扩容要搬走的老元素个数
                last_capacity = v.capacity();
                if (capacity_steps.size() < 8) {
                    capacity_steps.push_back(last_capacity);
                }
            }
        }

        std::cout << "  容量增长序列（前 8 次）: ";
        for (const std::size_t c : capacity_steps) {
            std::cout << c << " ";
        }
        std::cout << "...\n";
        std::cout << "  最终 size = " << v.size() << "，capacity = " << v.capacity() << "\n";
        std::cout << "  累计搬移元素次数 = " << total_moves
                  << "（约 " << static_cast<double>(total_moves) / static_cast<double>(kPushCount)
                  << " 倍 n，而不是 n 倍 n）\n";
        Note("MSVC 的 vector 用 1.5 倍增长（GCC/libstdc++ 用 2 倍）。为什么不是 2 倍，");
        Note("见 02_dynamic_array_vector.cpp 的详细讨论。");

        // (b) 不给 reserve vs 给 reserve，实测差距。
        const double t_no_reserve = BenchMedianUs(
            [&] {
                std::vector<int> a;
                for (int i = 0; i < kPushCount; ++i) {
                    a.push_back(i);
                }
                return static_cast<std::uint64_t>(a.size());
            },
            5);

        const double t_with_reserve = BenchMedianUs(
            [&] {
                std::vector<int> a;
                a.reserve(static_cast<std::size_t>(kPushCount));
                for (int i = 0; i < kPushCount; ++i) {
                    a.push_back(i);
                }
                return static_cast<std::uint64_t>(a.size());
            },
            5);

        std::cout << "\n";
        Label("  push_back 200000 次（不 reserve）", 44);
        std::cout << ": " << t_no_reserve << " us\n";
        Label("  push_back 200000 次（先 reserve）", 44);
        std::cout << ": " << t_with_reserve << " us\n";
        if (t_with_reserve > 0.0) {
            Label("  两者倍数", 44);
            std::cout << ": " << (t_no_reserve / t_with_reserve) << " x\n";
        }
        Note("");
        Note("注意：这里 reserve 只快了十几个百分点，而不是「快好几倍」。原因有两个：");
        Note("  1) 扩容成本本来就是均摊 O(1)，整个过程一共只搬移了约 2.07n 个元素，");
        Note("     相对于 n 次 push_back 本身并不是主要开销；");
        Note("  2) Debug 构建下 push_back 自身的函数调用与边界检查开销远大于内存拷贝，");
        Note("     把扩容的收益掩盖掉了。元素类型越大（拷贝越贵），差距就越明显。");
        Note("工程结论：能预估大小时仍然应该先 reserve。它是零风险优化，而且还能避免");
        Note("      扩容过程中的内存峰值（老块和新块会同时存在，瞬时占用接近 2.5 倍）。");
    }

    // =======================================================================
    Section("4. 同样 O(n)，常数因子能差多少：vector vs list");
    // =======================================================================
    Note("两个求和函数复杂度完全一样，都是 O(n)。差别只在内存布局：");
    Note("  vector: 元素连续，缓存行（64B）一次带回 16 个 int，CPU 预取器高度有效；");
    Note("  list  : 节点散落在堆上，每走一步都可能 cache miss，还可能触发预取器失效。");
    std::cout << "\n";

    {
        const std::size_t kCount = 200000;
        const std::vector<int> vec_data = MakeRandom(kCount, 999u);
        const std::list<int> list_data(vec_data.begin(), vec_data.end());

        WarmUp([&] { return SumContiguous(vec_data); }, 3);
        WarmUp([&] { return SumPointerChasing(list_data); }, 3);

        const double t_vec = BenchMedianUs([&] { return SumContiguous(vec_data); }, 7);
        const double t_list = BenchMedianUs([&] { return SumPointerChasing(list_data); }, 7);

        std::cout << std::setw(30) << "vector 求和 200000 个 int: " << t_vec << " us\n";
        std::cout << std::setw(30) << "list   求和 200000 个 int: " << t_list << " us\n";
        if (t_vec > 0.0) {
            std::cout << std::setw(30) << "list / vector : " << (t_list / t_vec) << " x 慢\n";
        }
        Note("");
        Note("结论：链表在这种「顺序遍历」场景下几乎总是输。这解释了为什么");
        Note("      std::list 在真实项目里出现得极少（详见 03_linked_list.cpp）。");
    }

    // =======================================================================
    Section("5. 最好 / 最坏 / 平均：三个不同的数字");
    // =======================================================================
    Note("以线性查找为例，同一份数据、同一个算法，输入不同结果差 n 倍：");
    Note("  最好情况：目标就在第一个位置        -> 1 次比较，O(1)");
    Note("  最坏情况：目标不存在，或恰好在末尾  -> n 次比较，O(n)");
    Note("  平均情况：目标等概率出现在任意位置  -> n/2 次比较，O(n)");
    Note("算法复杂度分析默认谈「最坏情况」，因为它才是你能承诺给用户的上限。");
    std::cout << "\n";

    {
        const std::size_t kCount = 100000;
        std::vector<int> data = MakeRandom(kCount, 4242u);
        std::vector<int> sorted_data = data;
        std::sort(sorted_data.begin(), sorted_data.end());

        const int hit_first = data[0];
        const int miss = -12345;  // 数据全是非负数，一定找不到

        // 平均情况：随机挑 1000 个目标各自查一次，模仿真实的随机访问负载。
        std::mt19937 rng(2024u);

        // 关键：四组测量都做「1000 次查找」，否则数字之间没有可比性
        // （1 次查找和 1000 次查找放一起比，是在偷换单位）。
        constexpr int kLookups = 1000;

        // 最好情况：每次都命中首元素，只比较 1 次。
        const double t_best = BenchMedianUs(
            [&] {
                std::uint64_t acc = 0;
                for (int i = 0; i < kLookups; ++i) {
                    acc += static_cast<std::uint64_t>(LinearSearch(data, hit_first));
                }
                return acc;
            },
            7);

        // 最坏情况：目标一定不存在，每次都要扫完整个数组。
        const double t_worst = BenchMedianUs(
            [&] {
                std::uint64_t acc = 0;
                for (int i = 0; i < kLookups; ++i) {
                    acc += static_cast<std::uint64_t>(LinearSearch(data, miss));
                }
                return acc;
            },
            7);

        // 平均情况：目标在数组里随机分布，期望比较 n/2 次。
        const double t_avg = BenchMedianUs(
            [&] {
                std::uint64_t acc = 0;
                for (int i = 0; i < kLookups; ++i) {
                    const int target = data[rng() % kCount];
                    acc += static_cast<std::uint64_t>(LinearSearch(data, target));
                }
                return acc;
            },
            7);

        const double t_binary = BenchMedianUs(
            [&] {
                std::uint64_t acc = 0;
                for (int i = 0; i < kLookups; ++i) {
                    const int target = data[rng() % kCount];
                    const auto it = std::lower_bound(sorted_data.begin(), sorted_data.end(), target);
                    acc += static_cast<std::uint64_t>(it != sorted_data.end());
                }
                return acc;
            },
            7);

        Label("  线性查找 - 最好情况（命中首元素）", 42);
        std::cout << ": " << t_best << " us\n";
        Label("  线性查找 - 最坏情况（一定找不到）", 42);
        std::cout << ": " << t_worst << " us\n";
        Label("  线性查找 - 平均情况（随机目标）", 42);
        std::cout << ": " << t_avg << " us\n";
        Label("  二分查找 - 最坏情况（已排序数组）", 42);
        std::cout << ": " << t_binary << " us\n";
        Note("");
        Note("四个数字都是「查 1000 次」的总耗时，可以直接横向比较：");
        Note("  最好和最坏差了约两个数量级（1 次比较 vs 10 万次比较）；");
        Note("  平均情况大致落在最坏情况的一半（期望比较 n/2 次）；");
        Note("  二分的「最坏」比线性的「最好」贵，却比线性的「平均」便宜一个数量级以上。");
        Note("");
        Note("工程启示：一次 O(n log n) 的排序，换来之后每次查询 O(log n)，");
        Note("          在「一次构建、多次查询」的场景里是稳赚的买卖。");
    }

    // =======================================================================
    Section("6. 空间复杂度：不只是「用了几个变量」");
    // =======================================================================
    Note("空间复杂度和时间一样要按增长阶来算，而且要算上「看不见的开销」。");
    std::cout << "\n";

    {
        std::cout << "  sizeof(std::vector<int>)   = " << sizeof(std::vector<int>)
                  << " 字节（通常 3 个指针：begin / end / capacity_end）\n";
        std::cout << "  sizeof(std::list<int>)     = " << sizeof(std::list<int>)
                  << " 字节（2 个指针，但每个节点额外背 2 个指针）\n";
        std::cout << "  sizeof(int)                = " << sizeof(int) << " 字节\n";
        std::cout << "  sizeof(std::string)        = " << sizeof(std::string)
                  << " 字节（含 SSO 小字符串缓冲区）\n";
        std::cout << "\n";

        std::vector<int> v;
        for (int i = 0; i < 1000; ++i) {
            v.push_back(i);
        }
        const std::size_t bytes_used = v.capacity() * sizeof(int);
        std::cout << "  vector<int> size=1000, capacity=" << v.capacity()
                  << " -> 实占内存约 " << bytes_used << " 字节\n";
        std::cout << "  元素本身只需要 " << (v.size() * sizeof(int)) << " 字节，"
                  << "多出来的是预留给未来 push_back 的空位\n";

        std::list<int> l(v.begin(), v.end());
        // 每个 list 节点 = 数据 + 前驱指针 + 后继指针（MSVC 下还可能有对齐填充）
        const std::size_t node_size = sizeof(int) + 2 * sizeof(void*);
        std::cout << "  list<int> 1000 个元素 -> 约 " << (node_size * 1000)
                  << " 字节（每节点至少 " << node_size << " 字节，指针开销远超数据本身）\n";
        std::cout << "  l.size() = " << l.size() << "\n";
        Note("");
        Note("结论：算空间复杂度时，链表每个元素的常数开销常常是 vector 的 3 倍以上，");
        Note("      这也是它慢的原因之一——同样的数据要占用更多缓存行。");
        Note("      另外 vector 有 capacity 冗余，list 没有，两者各有取舍。");
    }

    // =======================================================================
    Section("7. 测量方法论：这样测出来的数字才可信");
    // =======================================================================
    Note("1) 用 steady_clock，不要用 system_clock，也不要用 clock()（它测的是 CPU 时间，");
    Note("   多线程下会失真，且不包含 IO 等待）。");
    Note("2) 预热：第一次调用带着缺页中断和缓存冷启动，至少空跑 1 到 3 次再开始计时。");
    Note("3) 重复多次取中位数：单次测量几乎一定是垃圾数据。");
    Note("4) 防止被优化掉：把结果写进 volatile 变量，否则 Release 下整段代码会被删掉。");
    Note("5) 保证每次测量的输入一致：排序这类算法对输入分布极其敏感，");
    Note("   要么每次重新拷贝乱序数据，要么至少说明清楚你测的是哪种输入。");
    Note("6) 控制变量：一次只改一个东西。改完先怀疑测量方法，再怀疑代码。");
    Note("7) 分辨率下限：如果一个操作只要几纳秒，而你的计时器分辨率是 100ns，");
    Note("   那就把它放进循环里跑几百万次再除以次数，而不是直接测一次。");
    std::cout << "\n";

    Note("本文件演示的手法，后面 02 到 10 每个文件都在用同一套 BenchMedianUs 思路。");
    Note("再次提醒：以上所有绝对耗时都来自 Debug 构建，请只比较相对差异和增长趋势。");

    // 让 g_sink 真正被「读过」一次，确保它不会被整个优化掉。
    std::cout << "\n[校验] 累加器非零，说明所有测量结果都真实参与了运算: "
              << (g_sink != 0 ? "是" : "否") << "\n";

    return 0;
}
