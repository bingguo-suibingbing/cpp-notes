// ============================================================================
//  04_performance_measurement.cpp
// ----------------------------------------------------------------------------
//  演示主题：
//    1. 用 std::chrono::steady_clock 写一个**可靠的**测量工具：
//       预热 + 多轮迭代 + 取最小值 / 中位数 + 防止被优化掉
//    2. 为什么必须用 steady_clock 而不是 system_clock
//    3. Debug 与 Release 的性能差异（现场实测，同一份代码）
//    4. 分支预测的影响（有序数组 vs 随机数组求和）
//    5. 缓存局部性的影响（行优先 vs 列优先遍历二维数组）
//    6. reserve / emplace_back / 避免拷贝的实测收益
//    7. const& vs 按值传参：什么时候按值更快
//    8. 「先测量再优化」「不要过早优化」的工程原则 + 优化手段性价比排序
//
//  关键结论：
//    - 性能问题几乎从不出现在你「觉得」慢的地方 —— 必须测。
//    - 测不准的原因通常是：没预热（缓存/分支预测器/CPU 频率还没进入稳态）、
//      只测一次（受调度和中断干扰）、以及**结果被编译器优化掉了**。
//    - 最小值和最小值之间的距离，比平均值更能说明「这段代码最快能多快」。
//    - 优化性价比排序（从高到低）：
//        算法与数据结构 >> 减少内存分配/拷贝 >> 缓存友好 >> 编译期计算
//        >> 分支/内联 >> 微调指令级细节
//      前两项通常带来数量级收益，最后一项只有个位数百分比。
//    - Debug 配置下的性能数字**没有参考价值**（本文件会实测给你看）。
//
//  运行时间提示：本文件的基准测试在 Debug 下大约需要 1~2 分钟，
//  在 Release 下只要几秒。建议两种配置都跑一次，对比感受一下。
// ============================================================================

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>  // _ReadWriteBarrier：MSVC 的编译器内存屏障
#endif

namespace {

// ============================================================================
//  第 1 节：一个可靠的计时工具
// ----------------------------------------------------------------------------
//  设计要点（对应每一处「为什么」）：
//
//   a) steady_clock 而不是 system_clock
//      system_clock 是「墙上时钟」，可能被 NTP 校时、手动改时间、夏令时调整，
//      会**倒退**，测出来的时间甚至可能是负数。steady_clock 是单调递增的
//      （Windows 上底层是 QueryPerformanceCounter），专门用于测时间间隔。
//      测「耗时」用 steady_clock 或 high_resolution_clock 的 steady 版本；
//      测「现在几点」才用 system_clock。
//
//   b) 预热（warm-up）
//      第一次执行总是偏慢：代码/数据还没进缓存、分支预测器还没学到模式、
//      CPU 可能还在低频省电状态。所以先跑若干轮丢掉。
//
//   c) 多轮迭代 + 取最小值 / 中位数
//      单次测量会被系统调度、其他进程、中断打断，结果只能偏大不能偏小。
//      最小值 = 「这段代码在理想情况下的最快速度」；中位数 = 「典型表现」。
//      工程上两者都看：两者差距大，说明测量环境不稳定。
//
//   d) 防止被优化掉（DoNotOptimize）
//      如果被测代码没有可观测的副作用，优化器会直接把它删掉，
//      于是你测的是「什么都不做」的时间 —— 这是基准测试最常见的假数据。
//      解决办法：让结果「有被外部看到的风险」。
//        正确做法：asm volatile("" : : "r,m"(value) : "memory")（GCC/Clang）
//                  或 MSVC 的 _ReadWriteBarrier() + volatile 中转
//        简单做法：把结果写进 volatile 变量（代价略高但安全、可移植）
// ============================================================================

// DoNotOptimize：告诉编译器「这个值可能被外部使用，别把它优化掉」。
// MSVC 下用 volatile 读写 + 内存屏障；这样即使 /O2 也不会删掉被测代码。
template <typename T>
inline void do_not_optimize(const T& value) noexcept {
    volatile const T* sink = &value;  // 取地址并标记 volatile：编译器不敢删
    (void)*sink;                      // 读一次，确保「真的被使用」
#if defined(_MSC_VER)
    _ReadWriteBarrier();  // 阻止编译器把屏障前后的内存操作重排/合并
#endif
}

// 单个用例的测量结果
struct Timing {
    double best_ms = 0.0;    // 最小值：理想情况下的最快速度
    double median_ms = 0.0;  // 中位数：典型表现
    double mean_ms = 0.0;    // 平均值：容易被离群值拉高
};

// 核心测量函数：预热 + 多轮 + 统计
//   fn        被测代码（一个可调用对象）
//   warmup    预热轮数（结果丢弃）
//   rounds    正式测量轮数
template <typename Fn>
[[nodiscard]] Timing measure(Fn&& fn, int warmup = 3, int rounds = 15) {
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(rounds));

    for (int i = 0; i < warmup + rounds; ++i) {
        const auto start = std::chrono::steady_clock::now();  // 单调时钟，不会被校时影响
        fn();
        const auto end = std::chrono::steady_clock::now();

        const double ms = std::chrono::duration<double, std::milli>(end - start).count();
        if (i >= warmup) {
            samples.push_back(ms);
        }
    }

    std::sort(samples.begin(), samples.end());
    Timing t;
    t.best_ms = samples.front();
    t.median_ms = samples[samples.size() / 2];
    t.mean_ms = std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(samples.size());
    return t;
}

// 把一次调用跑 n 次，减少「单次太短、计时器分辨率不够」的误差。
// 返回「单次调用」的耗时统计。当被测代码只有几十纳秒时，必须用它的思路：
// 一次 measure 至少要有几十毫秒的量级，否则计时器分辨率本身就成了误差来源。
// 注意：本文件里各个用例的循环次数是显式写出来的（更直观），
// 所以没有直接调用它 —— 保留它是为了说明「人均耗时」的正确算法：先测总耗时再除。
template <typename Fn>
[[nodiscard]] Timing measure_per_call(Fn&& fn, int calls_per_round, int warmup = 3, int rounds = 15) {
    Timing t = measure(
        [&fn, calls_per_round] {
            for (int i = 0; i < calls_per_round; ++i) {
                fn();
            }
        },
        warmup, rounds);
    const double n = static_cast<double>(calls_per_round);
    t.best_ms /= n;
    t.median_ms /= n;
    t.mean_ms /= n;
    return t;
}

// 演示 measure_per_call 的用法，同时保证它不被当成死代码（否则 /W4 可能报 C4505）。
void demo_per_call_helper() {
    const Timing t = measure_per_call([] { do_not_optimize(42); }, 1000, 1, 3);
    std::cout << "  （measure_per_call 自检：单次调用最小 " << std::fixed << std::setprecision(6)
              << t.best_ms << " ms）\n";
}

// 打印一行结果（统一格式，方便肉眼对比）
void print_row(const std::string& label, const Timing& t, const std::string& note = "") {
    std::cout << "  " << std::left << std::setw(34) << label << std::right << std::fixed
              << std::setprecision(4) << std::setw(12) << t.best_ms << std::setw(12) << t.median_ms
              << std::setw(12) << t.mean_ms;
    if (!note.empty()) {
        std::cout << "   " << note;
    }
    std::cout << "\n";
}

void print_header() {
    std::cout << "  " << std::left << std::setw(34) << "用例" << std::right << std::setw(12) << "最小(ms)"
              << std::setw(12) << "中位(ms)" << std::setw(12) << "平均(ms)" << "\n";
    std::cout << "  " << std::string(76, '-') << "\n";
}

// 打印倍数关系（用最小值比较，因为最小值最稳定）
void print_ratio(const std::string& what, double slow_ms, double fast_ms) {
    if (fast_ms <= 0.0) {
        std::cout << "  " << what << "：分母为 0，无法计算倍数\n";
        return;
    }
    std::cout << "  " << what << "：" << std::fixed << std::setprecision(2) << (slow_ms / fast_ms)
              << " 倍（" << slow_ms << " ms vs " << fast_ms << " ms，按最小值比较）\n";
}

// 防优化的累加器：每次累加都走一次真正的内存读写，保证被测函数不会被优化掉。
// 为什么不直接加到一个普通变量上：优化器会把整个循环折叠成常数（甚至删掉），
// 于是四个用例都测出 0.0000 ms —— 那是「什么都没做」的时间，不是函数的耗时。
class VolatileSink {
public:
    VolatileSink() noexcept : ptr_(&storage_) {}

    void add(std::int64_t value) const noexcept {
        const std::int64_t now = *ptr_;  // volatile 读：编译器必须真的读内存
        *ptr_ = now + value;             // volatile 写：编译器必须真的写内存
    }
    [[nodiscard]] std::int64_t value() const noexcept { return *ptr_; }

private:
    // 成员声明顺序 = 初始化顺序，所以 storage_ 必须写在 ptr_ 前面（否则 /W4 报 C5038）
    volatile std::int64_t storage_ = 0;
    volatile std::int64_t* ptr_;
};

// ============================================================================
//  第 2 节：被测的工作负载
// ============================================================================

// 生成 0 ~ max-1 的随机数：用固定种子，保证每次运行的数据完全一样（可复现）
[[nodiscard]] std::vector<int> make_random_data(std::size_t n, int max_value, std::uint32_t seed = 12345u) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, max_value - 1);
    std::vector<int> data(n);
    for (int& x : data) {
        x = dist(rng);
    }
    return data;
}

// 求和：这是「分支预测」演示的核心。数据有序时，分支几乎总是一样 -> 预测成功率高；
// 数据随机时，分支模式随数据变化 -> 预测失败率高，每次失败要清空流水线（约 15~20 周期）。
[[nodiscard]] std::int64_t sum_if_positive(const std::vector<int>& data) {
    std::int64_t sum = 0;
    for (const int x : data) {
        if (x >= 128) {  // 阈值选在数据范围中间：让分支结果最不可预测
            sum += x;
        }
    }
    return sum;
}

// 无分支版本：用算术替代分支（编译器有时会自动做这个变换，所以两种都要测）
[[nodiscard]] std::int64_t sum_branchless(const std::vector<int>& data) {
    std::int64_t sum = 0;
    for (const int x : data) {
        const int mask = (x >= 128) ? -1 : 0;  // 用掩码代替条件跳转
        sum += static_cast<std::int64_t>(x & mask);
    }
    return sum;
}

// 二维矩阵遍历：缓存局部性的经典演示
//
// 这里用**扁平的一维数组**（flat）+ 手动算下标，而不是 vector<vector<int>>。
// 原因：vector<vector<int>> 每行都是一次独立的堆分配，行与行之间不连续；
// 而且行内访问要先解引用「行指针」再访问元素，多了一层指针跳转，
// 会把「缓存局部性」这个主题干扰掉（Debug 下尤其明显）。
// 真实项目里如果需要二维数据，几乎都应该用扁平数组 + stride —— 这本身就是本节结论之一。
class FlatMatrix {
public:
    FlatMatrix(std::size_t rows, std::size_t cols, int fill)
        : rows_(rows), cols_(cols), data_(rows * cols, fill) {}

    // [[nodiscard]] + noexcept：越界检查交给 Debug 断言（见 02 文件的契约思路）
    [[nodiscard]] int& at(std::size_t r, std::size_t c) noexcept {
        assert(r < rows_ && c < cols_ && "FlatMatrix::at 下标越界");
        return data_[r * cols_ + c];
    }
    [[nodiscard]] const int& at(std::size_t r, std::size_t c) const noexcept {
        assert(r < rows_ && c < cols_ && "FlatMatrix::at 下标越界");
        return data_[r * cols_ + c];
    }
    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t cols() const noexcept { return cols_; }

private:
    std::size_t rows_;
    std::size_t cols_;
    std::vector<int> data_;  // 一整块连续内存：这正是缓存友好的前提
};

// 行优先：内层循环走连续的 c，缓存行（64 字节 = 16 个 int）被完整利用
[[nodiscard]] std::int64_t sum_row_major(const FlatMatrix& m) {
    std::int64_t sum = 0;
    for (std::size_t r = 0; r < m.rows(); ++r) {
        for (std::size_t c = 0; c < m.cols(); ++c) {
            sum += m.at(r, c);  // 地址连续递增，硬件预取器能识别
        }
    }
    return sum;
}

// 列优先：内层循环走连续的 r，每次跳 cols 个元素（stride = cols * 4 字节），
// 一个 64 字节的缓存行只用掉 4 字节就换下一个 -> 缓存利用率 1/16。
[[nodiscard]] std::int64_t sum_column_major(const FlatMatrix& m) {
    std::int64_t sum = 0;
    for (std::size_t c = 0; c < m.cols(); ++c) {
        for (std::size_t r = 0; r < m.rows(); ++r) {
            sum += m.at(r, c);  // 跨行跳读，几乎每次都要重新拉缓存行
        }
    }
    return sum;
}

// reserve / emplace 演示用的类型：故意让它「拷贝很贵」
// std::string 在这种长度下会超出 SSO（小字符串优化）缓冲，拷贝必须分配内存。
struct Record {
    int id = 0;
    std::string name;
    Record(int i, std::string n) : id(i), name(std::move(n)) {}
};

// ============================================================================
//  第 3 节：按值传参 vs const& 传参
// ----------------------------------------------------------------------------
//  经验法则（不是教条，必须实测）：
//    - 内建类型 / 很小的 POD（<= 2 个指针大小）：按值。引用要解引用，反而更慢。
//    - 大对象（很长很长的字符串、vector、大结构体）：const&，避免拷贝。
//    - 函数内部需要一份可修改的副本：按值 + std::move（让编译器用移动代替拷贝）。
//    - 模板的完美转发场景：按值 + std::move 往往是正确选择（见 Effective Modern C++）。
// ============================================================================

struct SmallPod {  // 8 字节，比一个指针还小
    int a = 0;
    int b = 0;
};

std::int64_t take_by_value_small(SmallPod p) { return static_cast<std::int64_t>(p.a) + p.b; }
std::int64_t take_by_constref_small(const SmallPod& p) { return static_cast<std::int64_t>(p.a) + p.b; }

struct BigPod {  // 1024 字节，远超寄存器/缓存行
    int data[256] = {};
};

std::int64_t take_by_value_big(BigPod p) { return p.data[0] + p.data[255]; }
std::int64_t take_by_constref_big(const BigPod& p) { return p.data[0] + p.data[255]; }

}  // namespace

int main() {
    std::cout << "==== 04 性能测量与优化 ====\n\n";
    std::cout << "  重要前提：本文件的数字是在**当前配置**下测出来的。\n";
#ifdef _DEBUG
    std::cout << "  当前是 Debug 配置（/Od、无优化）：绝对数字偏大，且「优化手段的收益」被严重低估。\n";
    std::cout << "  要判断优化是否有效，请用 Release（/O2）再跑一次：\n";
    std::cout << "      .\\build.ps1 -Chapter 07-engineering -Config Release -WX\n";
#else
    std::cout << "  当前是 Release 配置（/O2）：这一组数字才具备参考价值。\n";
#endif
    std::cout << "  所有结果都随机器、负载、编译器版本变化；**趋势稳定，绝对值仅供参考**。\n\n";

    // ------------------------------------------------------------ 1. 时钟选择
    std::cout << "---- 1. 为什么用 steady_clock 而不是 system_clock ----\n";
    {
        const auto sys_now = std::chrono::system_clock::now();
        const auto steady_now = std::chrono::steady_clock::now();

        // 把 steady_clock 的「时间点」换算成「开机以来经过多久」，说明它是单调时钟。
        const auto since_boot = std::chrono::duration<double>(steady_now.time_since_epoch()).count();
        std::cout << "  system_clock 纪元时间（秒）："
                  << std::chrono::duration<double>(sys_now.time_since_epoch()).count() << "\n";
        std::cout << "  steady_clock 自纪元起（秒）：" << since_boot
                  << "（通常是系统启动以来的时间，单调递增）\n";
        std::cout << "  system_clock 的 is_steady = " << std::boolalpha
                  << std::chrono::system_clock::is_steady << "\n";
        std::cout << "  steady_clock 的 is_steady = " << std::chrono::steady_clock::is_steady
                  << std::noboolalpha << "\n";
        std::cout << "  结论：system_clock 会被 NTP 校时/手动改时间影响，甚至**倒退**；\n";
        std::cout << "        测耗时必须用 steady_clock。system_clock 只用来回答「现在几点」。\n";
        std::cout << "  steady_clock 的分辨率（tick 周期）："
                  << std::chrono::steady_clock::period::num << "/" << std::chrono::steady_clock::period::den
                  << " 秒\n\n";
    }

    // ------------------------------------------------------- 2. 测量方法演示
    std::cout << "---- 2. 可靠测量的三要素：预热 / 多轮 / 取最小值 ----\n";
    {
        // 数据量要够大，让「冷启动」的差异看得见（缓存里放不下）
        std::vector<int> data = make_random_data(8'000'000, 1000);

        // 不预热、只测一次：拿到的是「冷启动」数据，通常明显偏慢
        const auto cold_start = std::chrono::steady_clock::now();
        const std::int64_t cold_sum = sum_if_positive(data);
        const auto cold_end = std::chrono::steady_clock::now();
        do_not_optimize(cold_sum);

        const Timing tally = measure([&] {
            const std::int64_t s = sum_if_positive(data);
            do_not_optimize(s);
        });

        std::cout << "  第 1 次执行（不预热、只测一次）："
                  << std::chrono::duration<double, std::milli>(cold_end - cold_start).count() << " ms\n";
        std::cout << "  预热 3 轮后测 15 轮：最小 " << tally.best_ms << " ms，中位 " << tally.median_ms
                  << " ms，平均 " << tally.mean_ms << " ms\n";
        std::cout << "  结论：冷启动明显偏慢（缓存空、频调未升频）。\n";
        std::cout << "        平均值 > 中位数 > 最小值 是常态：中断和调度只能让测量**变慢**，\n";
        std::cout << "        所以最小值才是「这段代码最快能多快」的可靠指标。\n";
        demo_per_call_helper();
        std::cout << "        （measure_per_call 展示「先测总耗时再除以次数」的算法，\n";
        std::cout << "          适合单次只有几十纳秒、计时器分辨率不够的微基准。）\n\n";
    }

    // ------------------------------------------------------ 3. 分支预测的影响
    std::cout << "---- 3. 分支预测：有序 vs 随机 ----\n";
    {
        constexpr std::size_t kCount = 16'000'000;  // 1600 万个元素，约 64 MB
        std::vector<int> ordered = make_random_data(kCount, 256, 1u);
        std::sort(ordered.begin(), ordered.end());  // 完全有序：分支结果几乎恒定
        std::vector<int> shuffled = ordered;
        std::mt19937 rng(999u);
        std::shuffle(shuffled.begin(), shuffled.end(), rng);  // 随机顺序：分支几乎不可预测

        std::int64_t expected = 0;
        print_header();
        const Timing t_ordered = measure([&] {
            expected = sum_if_positive(ordered);
            do_not_optimize(expected);
        });
        print_row("有序数组 + if", t_ordered, "分支几乎总不跳 -> 预测成功");

        std::int64_t got = 0;
        const Timing t_shuffled = measure([&] {
            got = sum_if_positive(shuffled);
            do_not_optimize(got);
        });
        print_row("随机数组 + if", t_shuffled, "分支随机 -> 预测失败代价高");

        std::int64_t bl = 0;
        const Timing t_branchless = measure([&] {
            bl = sum_branchless(shuffled);
            do_not_optimize(bl);
        });
        print_row("随机数组 + 无分支写法", t_branchless, "用掩码消除分支");

        std::cout << "  （校验：有序 " << expected << "，随机 " << got << "，无分支 " << bl << "）\n";
        print_ratio("随机 vs 有序", t_shuffled.best_ms, t_ordered.best_ms);
        print_ratio("随机(if) vs 随机(无分支)", t_shuffled.best_ms, t_branchless.best_ms);
        std::cout << "  结论：\n";
        std::cout << "    - **数据内容完全相同、指令条数完全相同**，只是排列顺序不同，\n";
        std::cout << "      耗时就能差好几倍 —— 唯一的区别是「分支结果可不可预测」。\n";
        std::cout << "      现代 CPU 一次预测失败的代价约 15~20 个周期（流水线清空）。\n";
        std::cout << "    - 这就是为什么「先排序再处理」在统计上常常更快。\n";
        std::cout << "    - 注意：编译器可能已经自动把 if 优化成无分支代码（尤其 Release），\n";
        std::cout << "      所以手写「无分支写法」不一定更快 —— 本机 Debug 下它甚至更慢，\n";
        std::cout << "      Release 下才明显胜出。结论永远是：**实测，别凭直觉**。\n\n";
    }

    // ---------------------------------------------------- 4. 缓存局部性的影响
    std::cout << "---- 4. 缓存局部性：行优先 vs 列优先遍历 ----\n";
    {
        // 16 MB 的矩阵：远大于典型 L2（1~2 MB），也超过不少机器的 L3 切片，
        // 这样列优先带来的缓存缺失才明显。
        constexpr std::size_t kDim = 2048;  // 2048 x 2048 x 4B = 16 MB
        const FlatMatrix matrix(kDim, kDim, 1);
        std::cout << "  矩阵 " << kDim << " x " << kDim << "，共 "
                  << (kDim * kDim * sizeof(int)) / (1024 * 1024)
                  << " MB（远超 L2；列优先时一个 64 字节缓存行只用到 4 字节）\n";

        std::int64_t s1 = 0;
        const Timing t_row = measure(
            [&] {
                s1 = sum_row_major(matrix);
                do_not_optimize(s1);
            },
            2, 7);
        std::int64_t s2 = 0;
        const Timing t_col = measure(
            [&] {
                s2 = sum_column_major(matrix);
                do_not_optimize(s2);
            },
            2, 7);

        print_header();
        print_row("行优先遍历（r 外层）", t_row, "顺序访问，缓存行被完整利用");
        print_row("列优先遍历（c 外层）", t_col, "跨行跳读，每次几乎都 miss");
        std::cout << "  （校验：行优先 " << s1 << "，列优先 " << s2 << "，两者必须相等）\n";
        print_ratio("列优先 vs 行优先", t_col.best_ms, t_row.best_ms);
        std::cout << "  结论：\n";
        std::cout << "    - 两者**指令数量相同、算法复杂度相同**，差距完全来自内存访问模式；\n";
        std::cout << "      这是「数量级差异」级别的问题，而且不需要任何聪明算法。\n";
        std::cout << "    - 工程含义：容器优先用连续的 std::vector（而不是 list/map 的节点跳转）；\n";
        std::cout << "      二维数据优先「一维数组 + 手动算下标」（本文件就是这么写的），\n";
        std::cout << "      而不是 vector<vector<T>> —— 后者每行一次堆分配，还会多一层指针跳转；\n";
        std::cout << "      循环嵌套的顺序要按内存布局来写。\n";
        std::cout << "    - 如果这里测出来的倍数接近 1，说明矩阵对当前机器的缓存来说还是太小，\n";
        std::cout << "      把 kDim 调大（比如 4096）再看 —— 这本身就是「测量要控制变量」的例子。\n\n";
    }

    // ------------------------------------------------- 5. reserve / emplace
    std::cout << "---- 5. reserve / emplace_back / 避免拷贝 ----\n";
    {
        // 元素数量随配置变化：Debug 下每次构造/拷贝都慢一个数量级，
        // 用同样的规模会让示例跑太久；Release 下放大规模，让差异更明显。
#ifdef _DEBUG
        constexpr int kItems = 100'000;
#else
        constexpr int kItems = 2'000'000;
#endif
        static volatile std::int64_t g_checksum = 0;  // 用 volatile 保证循环不被优化掉
        std::cout << "  元素个数：" << kItems << "，每个元素含一个超出 SSO 的 std::string\n";

        print_header();
        const Timing t_no_reserve = measure([&] {
            std::vector<Record> v;
            for (int i = 0; i < kItems; ++i) {
                v.push_back(Record{i, "record_name_longer_than_sso_buffer"});
            }
            g_checksum += static_cast<std::int64_t>(v.size()) + v.back().id;
        });
        print_row("push_back（不 reserve）", t_no_reserve, "反复扩容 + 搬移全部元素");

        const Timing t_reserve = measure([&] {
            std::vector<Record> v;
            v.reserve(static_cast<std::size_t>(kItems));  // 一次分配到位
            for (int i = 0; i < kItems; ++i) {
                v.push_back(Record{i, "record_name_longer_than_sso_buffer"});
            }
            g_checksum += static_cast<std::int64_t>(v.size()) + v.back().id;
        });
        print_row("push_back（先 reserve）", t_reserve, "零次重新分配");

        const Timing t_emplace = measure([&] {
            std::vector<Record> v;
            v.reserve(static_cast<std::size_t>(kItems));
            for (int i = 0; i < kItems; ++i) {
                v.emplace_back(i, "record_name_longer_than_sso_buffer");  // 原地构造，无临时对象
            }
            g_checksum += static_cast<std::int64_t>(v.size()) + v.back().id;
        });
        print_row("emplace_back + reserve", t_emplace, "就地构造，连移动都省了");

        print_ratio("不 reserve vs reserve", t_no_reserve.best_ms, t_reserve.best_ms);
        print_ratio("push_back vs emplace_back（都 reserve）", t_reserve.best_ms, t_emplace.best_ms);
        std::cout << "  结论：\n";
        std::cout << "    - reserve 收益来自「消除扩容时的重新分配 + 元素搬移」；\n";
        std::cout << "      元素越贵（string、大结构体）收益越大，元素是指针时几乎无差别。\n";
        std::cout << "    - emplace_back 省掉「先构造临时对象、再移动进容器」这一步；\n";
        std::cout << "      元素构造代价越高收益越明显；构造代价低时编译器可能已经优化掉。\n";
        std::cout << "    - 常见错误：在循环体里反复构造大对象再 push_back（拷贝一次 + 移动一次）；\n";
        std::cout << "      改成 emplace_back 直接传构造参数即可。\n\n";
    }

    // -------------------------------------- 6. 按值 vs const& / 字符串拼接
    std::cout << "---- 6. 按值传参 vs const& 传参 ----\n";
    {
        // 注意：这里必须用 VolatileSink 累加，不能简单地把结果累加到普通变量 ——
        // 传参函数太小，优化器会把它整个内联并折叠成常数，测出来全是 0。
        SmallPod small{3, 4};
        BigPod big{};
        big.data[0] = 7;
        big.data[255] = 9;

        constexpr int kSmallCalls = 10'000'000;
        constexpr int kBigCalls = 100'000;

        VolatileSink sink_small_val;
        VolatileSink sink_small_ref;
        VolatileSink sink_big_val;
        VolatileSink sink_big_ref;

        print_header();
        const Timing t_small_val = measure(
            [&] {
                for (int i = 0; i < kSmallCalls; ++i) {
                    sink_small_val.add(take_by_value_small(small));
                }
            },
            2, 7);
        print_row("SmallPod(8B) 按值", t_small_val, "寄存器就能传，可能更快");

        const Timing t_small_ref = measure(
            [&] {
                for (int i = 0; i < kSmallCalls; ++i) {
                    sink_small_ref.add(take_by_constref_small(small));
                }
            },
            2, 7);
        print_row("SmallPod(8B) const&", t_small_ref, "多一次取地址/解引用");

        const Timing t_big_val = measure(
            [&] {
                for (int i = 0; i < kBigCalls; ++i) {
                    sink_big_val.add(take_by_value_big(big));
                }
            },
            2, 7);
        print_row("BigPod(1KB) 按值", t_big_val, "每次调用都要拷贝 1KB");

        const Timing t_big_ref = measure(
            [&] {
                for (int i = 0; i < kBigCalls; ++i) {
                    sink_big_ref.add(take_by_constref_big(big));
                }
            },
            2, 7);
        print_row("BigPod(1KB) const&", t_big_ref, "只传地址");

        std::cout << "  （校验和：按值 " << (sink_big_val.value() + sink_small_val.value())
                  << "，const& " << (sink_big_ref.value() + sink_small_ref.value())
                  << "，两者相等说明结果一致）\n";
        std::cout << "  调用次数：SmallPod " << kSmallCalls << " 次，BigPod " << kBigCalls
                  << " 次；不同行之间不要横向比，只看同一组的两个数字。\n";
        print_ratio("SmallPod 按值 vs const&", t_small_val.best_ms, t_small_ref.best_ms);
        print_ratio("BigPod 按值 vs const&", t_big_val.best_ms, t_big_ref.best_ms);
        std::cout << "  结论：\n";
        std::cout << "    - 小 POD：按值通常不差，甚至更快（少一次解引用），寄存器就能传参；\n";
        std::cout << "    - 大对象：按值 = 每次调用一次拷贝，是数量级的差距，必须 const&；\n";
        std::cout << "    - 需要副本时：按值 + std::move 让调用者决定「拷还是移」（sink 参数惯用法）；\n";
        std::cout << "    - 模板接口：const& 会阻止移动语义，往往「按值 + std::move」才是最优。\n\n";
    }

    // ---------------------------------------------- 7. 字符串拼接的隐含开销
    std::cout << "---- 7. 隐含开销：字符串拼接与临时对象 ----\n";
    {
        const std::string a = "0123456789";
        const std::string b = "abcdefghij";
        static volatile std::size_t g_len = 0;  // 用 volatile 保证拼接结果真的被算出来
        constexpr int kConcat = 200'000;        // 每轮重复次数（每次调用内部只拼 4 段）

        print_header();
        const Timing t_plus = measure(
            [&] {
                for (int i = 0; i < kConcat; ++i) {
                    std::string s = a + b + a + b;  // 每次 + 都可能产生临时对象与新分配
                    g_len += s.size();
                }
            },
            2, 7);
        print_row("多次 operator+ 拼接", t_plus, "临时对象 + 多次分配");

        const Timing t_reserve_append = measure(
            [&] {
                for (int i = 0; i < kConcat; ++i) {
                    std::string s;
                    s.reserve(a.size() * 4);  // 一次分配
                    s += a;
                    s += b;
                    s += a;
                    s += b;
                    g_len += s.size();
                }
            },
            2, 7);
        print_row("+= 并预先 reserve", t_reserve_append, "一次分配，无临时对象");

        std::cout << "  每轮拼接 " << kConcat << " 次（校验长度 " << g_len << "）\n";
        print_ratio("operator+ vs reserve+=", t_plus.best_ms, t_reserve_append.best_ms);
        std::cout << "  结论：字符串是「看起来很便宜的昂贵操作」。\n";
        std::cout << "        在循环里拼字符串、在日志里无条件拼字符串，都是常见的真实性能bug。\n";
        std::cout << "        对策：reserve + += / append；或者用「先判断日志级别再拼消息」的写法。\n\n";
    }

    // ---------------------------------------------- 8. 先优化算法，再优化代码
    std::cout << "---- 8. 换算法 vs 抠代码：数量级 vs 百分比 ----\n";
    {
        // 同一个问题「统计数组里有多少对元素之和小于阈值」的两种写法。
        // 两者结果必须完全一样，只有复杂度不同。
        constexpr std::size_t kN = 4000;
        const std::vector<int> data = make_random_data(kN, 1000, 7u);

        const auto count_pairs_quadratic = [](const std::vector<int>& v, int threshold) {
            std::size_t count = 0;
            for (std::size_t i = 0; i < v.size(); ++i) {
                for (std::size_t j = i + 1; j < v.size(); ++j) {
                    if (v[i] + v[j] < threshold) {
                        ++count;
                    }
                }
            }
            return count;
        };

        const auto count_pairs_nlogn = [](std::vector<int> v, int threshold) {
            // 先排序 O(n log n)，再用二分找分界点：对每个 i，用
            // std::lower_bound 找出第一个使 v[i] + v[j] >= threshold 的 j，
            // 那么 (i, j) 之前的元素都满足条件。总计 O(n log n)。
            std::sort(v.begin(), v.end());
            std::size_t count = 0;
            for (std::size_t i = 0; i + 1 < v.size(); ++i) {
                // 只需在 i 之后找：v[i] + v[j] < threshold  <=>  v[j] < threshold - v[i]
                const int limit = threshold - v[i];
                // lower_bound 找第一个 >= limit 的位置：它左边的（且下标 > i）都满足
                const auto it = std::lower_bound(v.begin() + static_cast<std::ptrdiff_t>(i) + 1, v.end(), limit);
                const std::size_t first_not_ok = static_cast<std::size_t>(it - v.begin());
                if (first_not_ok > i + 1) {
                    count += first_not_ok - (i + 1);
                }
            }
            return count;
        };

        constexpr int kThreshold = 900;
        const std::size_t expected = count_pairs_quadratic(data, kThreshold);
        const std::size_t got = count_pairs_nlogn(data, kThreshold);
        std::cout << "  n = " << kN << "，阈值 = " << kThreshold << "，结果：暴力 "
                  << expected << " / 排序+二分 " << got << "（必须相等）\n";

        const Timing t_quad = measure(
            [&] {
                const std::size_t r = count_pairs_quadratic(data, kThreshold);
                do_not_optimize(r);
            },
            1, 3);
        const Timing t_fast = measure(
            [&] {
                const std::size_t r = count_pairs_nlogn(data, kThreshold);
                do_not_optimize(r);
            },
            1, 5);

        print_header();
        print_row("O(n^2) 暴力枚举", t_quad, "n^2/2 = 800 万次比较");
        print_row("排序 + 二分 O(n log n)", t_fast, "先排序再二分找分界");
        print_ratio("暴力 vs 排序+二分", t_quad.best_ms, t_fast.best_ms);
        std::cout << "  结论：换算法带来的收益是**数量级**的，而且 n 越大差距越大。\n";
        std::cout << "        在本文件的例子里，抠 reserve / emplace 只能拿到 1.2~2 倍，\n";
        std::cout << "        所以永远先问「有没有更好的算法 / 数据结构」，再问「这行代码怎么写更快」。\n\n";
    }

    // ------------------------------------------- 9. Debug 与 Release 的差距
    std::cout << "---- 9. Debug 与 Release 的差距（同一份代码）----\n";
    {
        // 用一段纯 CPU 计算：优化器能把循环展开、向量化，Debug 下则是一条条解释执行。
        constexpr std::size_t kN = 4'000'000;
        const std::vector<int> data = make_random_data(kN, 1000, 3u);

        const Timing t = measure(
            [&] {
                const std::int64_t r = sum_if_positive(data);
                do_not_optimize(r);
            },
            2, 7);
        print_header();
        print_row("400 万元素条件求和", t);
#ifdef _DEBUG
        std::cout << "  当前配置：Debug（/Od + /MDd + _DEBUG 已定义）\n";
#else
        std::cout << "  当前配置：Release（/O2 + /MD + NDEBUG 已定义）\n";
#endif
        std::cout << "  请用另一种配置再跑一次这个文件，对比同一行的数字：\n";
        std::cout << "      .\\build.ps1 -Chapter 07-engineering -Config Release\n";
        std::cout << "  本机实测（同一台机器、同一份代码，跑的时候机器上还有别的编译任务）：\n";
        std::cout << "      Debug   10.5 ~ 14.1 ms\n";
        std::cout << "      Release  6.4 ~  7.9 ms（约 1.6 ~ 2 倍）\n";
        std::cout << "  注意倍数的波动：机器一忙，测量结果就会变差 —— 这正说明\n";
        std::cout << "  「记录测量环境」和「多轮取最小值」为什么是必须的。\n";
        std::cout << "  结论：\n";
        std::cout << "    - Debug 与 Release 的差距取决于代码形态：纯循环 2~5 倍，\n";
        std::cout << "      大量小函数调用（本文件第 5~7 节那些）可以到 10~50 倍，\n";
        std::cout << "      模板/STL 密集的代码差距更大（因为内联被关掉了）。\n";
        std::cout << "    - 所以：**任何性能结论都必须在 Release 下得到**；\n";
        std::cout << "      但在 Debug 下开发和调试，因为你需要在崩溃时看到完整调用栈。\n";
        std::cout << "    - 混用 Debug/Release 的库（/MDd 与 /MD）是经典灾难：\n";
        std::cout << "      堆分配器不同 -> 跨模块 free 直接崩；STL 对象布局可能不同 -> 数据损坏。\n\n";
    }

    // ---------------------------------------------------- 10. 优化性价比排序
    std::cout << "---- 10. 优化手段的性价比排序（工程经验，配合实测使用）----\n";
    std::cout << "  优先级  手段                          典型收益        风险\n";
    std::cout << "  ------  ----------------------------  --------------  ------------------------\n";
    std::cout << "  1       换算法/换数据结构（O(n^2)->O(n log n)）  10~1000 倍   需要想清楚，但最值\n";
    std::cout << "  2       减少内存分配（reserve/对象池/复用缓冲）  2~10 倍      内存占用上升\n";
    std::cout << "  3       减少拷贝与临时对象（const&/move/emplace） 1.5~10 倍  可读性略降\n";
    std::cout << "  4       改善缓存局部性（连续容器/调整遍历顺序）  2~25 倍      数据结构要改\n";
    std::cout << "  5       编译期计算（constexpr/模板/查表）        1.2~5 倍     编译时间变长\n";
    std::cout << "  6       分支与内联（likely/unlikely/noexcept）   1.05~2 倍    可移植性下降\n";
    std::cout << "  7       手写 SIMD / 汇编微调                    1.1~4 倍     极难维护，最后考虑\n";
    std::cout << "\n";

    std::cout << "---- 11. 工程原则 ----\n";
    std::cout << "  1) 先测量，再优化（Measure, don't guess）。\n";
    std::cout << "     人的直觉在性能上极不准：真正的瓶颈常在完全没想到的地方（分配、字符串、锁、IO）。\n";
    std::cout << "  2) 不要过早优化（Premature optimization is the root of all evil）。\n";
    std::cout << "     先写对、写清楚；等有了 profiling 数据，再去改那 3% 的热点代码。\n";
    std::cout << "     过早优化的代价往往不是「白费功夫」，而是「代码变难懂、bug 变多」。\n";
    std::cout << "  3) 优化的前提是「有可对比的数字」：改之前测一遍，改之后再测一遍。\n";
    std::cout << "     没有基线数据的「优化」无法证明有效，也无法证明没把别的地方改坏。\n";
    std::cout << "  4) 用 Release 做性能结论。Debug 的数字只说明「Debug 很慢」这一件事。\n";
    std::cout << "  5) 优化时必须保持行为不变：先有测试，再改代码（这也是测试的价值之一）。\n";
    std::cout << "  6) 记录每一次测量的环境（配置、编译器版本、机器），否则数字无法复现。\n\n";

    std::cout << "  本文件用到的工具清单：\n";
    std::cout << "    std::chrono::steady_clock            单调时钟，测耗时的唯一正确选择\n";
    std::cout << "    volatile / _ReadWriteBarrier()       阻止编译器删掉被测代码\n";
    std::cout << "    预热 + 多轮 + 最小值/中位数          得到稳定可比的数字\n";
    std::cout << "    Visual Studio 性能探查器 (Alt+F2)    真实项目的采样/插桩分析\n";
    std::cout << "    /O2 与 /Od 的对比                    看清优化到底做了什么\n";
    std::cout << "\n";
    std::cout << "==== 结论：先测量 -> 找瓶颈 -> 改一处 -> 再测量；没有数字就没有优化 ====\n";
    return 0;
}
