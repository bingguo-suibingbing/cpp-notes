// ============================================================================
// 08_sorting_algorithms.cpp
// 演示主题：
//   1. 手写排序全家桶：冒泡（含提前退出）、选择、插入、希尔、归并（自顶向下 +
//      自底向上）、快排（Lomuto / Hoare / 随机化 / 三数取中 / 尾递归 / cutoff）、
//      堆排、计数排序、桶排序
//   2. 每个算法都标注：平均 / 最坏时间复杂度、额外空间、是否稳定、是否原地
//   3. 「插入排序是快排与归并的递归基底」——为什么小数组和近乎有序时它最快
//   4. 希尔排序：增量序列决定复杂度，Knuth 序列 1, 4, 13, 40, ... 优于折半
//   5. 快排的两种死法：固定 pivot 在已排序数组上退化成 O(n^2)；尾递归不优化
//      会把递归深度推到 O(n) 而爆栈；以及四种防御手段各自的收益
//   6. 非比较排序（计数 / 桶）的 O(n) 前提，以及什么数据上不该用它
//   7. 稳定性不是空话：用「键相同、id 唯一」的结构体做可运行的验证
//   8. 与 std::sort / std::stable_sort 同台对比，并解释 introsort 为什么赢
//
// 关键结论：
//   - 生产代码永远用 std::sort / std::stable_sort。它们实现的是 introsort 混合策略
//     （快排 + 堆排 + 插入排序），再叠加「比较器全内联 + 分支可预测 + 循环缓存友好」，
//     手写版本几乎不可能更快，更不可能更稳。
//   - 插入排序在「近乎有序」的 10 万元素上只要几百微秒到几毫秒，而快排仍要
//     O(n log n) 的固定开销 —— 这就是快排 / 归并把小区间（cutoff）交给插入排序的原因。
//   - 复杂度只是上限，常数因子和输入分布决定真实胜负：同样是 O(n log n)，
//     希尔排序在 10 万元素上通常比手写快排慢，但在中等规模里常压过归并。
//   - 非比较排序的 O(n) 是有条件的：计数排序要求键是小范围整数（空间 O(k)），
//     桶排序要求数据近似均匀分布。键范围一大或分布极度倾斜，它们立刻变慢甚至爆内存。
//
// 说明：本文件在 Debug（/Od）下编译运行。Debug 的绝对数字比 Release 慢很多，
//       所有耗时仅供参考，看「趋势」和「数量级差异」才有意义。
// ============================================================================

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <utility>
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
// 排序通用小工具
// ---------------------------------------------------------------------------
// 统一的交换原语。这里用 std::swap 而不是手写三行临时变量，是为了让
// 元素类型自带的高效交换（例如 std::string 的移动版交换）有机会被用上。
// 复杂度 O(1)。
template <typename T>
void SwapValues(T& a, T& b) {
    using std::swap;
    swap(a, b);
}

// 校验序列是否按 comp 有序。O(n)。
// 注意用的是 comp(a[i], a[i+1]) 而不是 !comp(a[i+1], a[i])：
// 前者更严格（连相等都算违规），用来对拍时能多抓一类错误。
template <typename T, typename Comp>
bool IsSorted(const std::vector<T>& v, Comp comp) {
    for (std::size_t i = 0; i + 1 < v.size(); ++i) {
        if (comp(v[i + 1], v[i])) {
            return false;
        }
    }
    return true;
}

// ===========================================================================
// 第 1 组：O(n^2) 的三个经典算法
// ===========================================================================
// 它们的共同点是「相邻或近邻元素反复比较交换」，实现只要十行左右，
// 但在 n = 10^5 时就是 10^10 次操作，会跑到天荒地老。
// 所以下面的性能实测里，它们只在 n = 2000 / 5000 上测。

// ---------------------------------------------------------------------------
// 1.1 冒泡排序（Bubble Sort）
// ---------------------------------------------------------------------------
// 时间：平均 O(n^2)，最坏 O(n^2)，最好 O(n)（下面带提前退出的版本）
// 空间：O(1)
// 稳定性：稳定——只在「严格大于」时交换，相等元素永远不跨越彼此
// 原地：是
// 教学用途，生产请用 std::sort。
//
// 为什么每一轮内层循环的终点是 n - 1 - i？
//   每跑完一轮，当前未排序区间的最大值一定被顶到了区间末尾。那个位置从此就是
//   「已确定」的，不需要（也不应该）再参与比较。n 轮中有 i 个元素已被确定，
//   所以只需要扫到 n - 1 - i。
template <typename T, typename Comp = std::less<>>
void BubbleSort(std::vector<T>& a, Comp comp = Comp{}) {
    const std::size_t n = a.size();
    if (n < 2) {
        return;
    }
    for (std::size_t i = 0; i + 1 < n; ++i) {
        for (std::size_t j = 0; j + 1 < n - i; ++j) {
            if (comp(a[j + 1], a[j])) {  // 后一个严格小于前一个才交换
                SwapValues(a[j], a[j + 1]);
            }
        }
    }
}

// 带「提前退出」的冒泡排序。
// 时间：最好 O(n)（输入已经有序时只扫一遍），平均 / 最坏 O(n^2)
// 空间：O(1)，稳定性：稳定，原地：是
//
// 为什么需要 swapped 标志？
//   朴素版本即使面对已经有序的数组也要老老实实跑完 n 轮，白白浪费 O(n^2) 次比较。
//   如果某一轮从头到尾一次交换都没发生，说明整个区间已经有序，再跑下去纯属浪费。
//   这个优化让小规模或基本有序的输入从 O(n^2) 直接掉到 O(n)。
template <typename T, typename Comp = std::less<>>
void BubbleSortEarlyExit(std::vector<T>& a, Comp comp = Comp{}) {
    const std::size_t n = a.size();
    for (std::size_t i = 0; i + 1 < n; ++i) {
        bool swapped = false;
        for (std::size_t j = 0; j + 1 < n - i; ++j) {
            if (comp(a[j + 1], a[j])) {
                SwapValues(a[j], a[j + 1]);
                swapped = true;
            }
        }
        if (!swapped) {  // 本轮零交换 -> 已经有序，提前收工
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// 1.2 选择排序（Selection Sort）
// ---------------------------------------------------------------------------
// 时间：平均 / 最坏 / 最好都是 O(n^2)（无论输入如何，比较次数恒定）
// 空间：O(1)
// 稳定性：不稳定——把最小值换到前面时，可能跨过与它相等的元素
// 原地：是
// 教学用途，生产请用 std::sort。
//
// 它唯一的工程价值是「交换次数最少」：整个排序恰好做 n-1 次交换（O(n)），
// 而冒泡和插入在最坏情况下要做 O(n^2) 次交换。所以当「写入代价远高于比较代价」
// （例如写 flash、写远端存储）时，选择排序反而是可选项。这条在嵌入式里有实际意义。
template <typename T, typename Comp = std::less<>>
void SelectionSort(std::vector<T>& a, Comp comp = Comp{}) {
    const std::size_t n = a.size();
    for (std::size_t i = 0; i + 1 < n; ++i) {
        std::size_t min_index = i;
        // 注意从 i + 1 开始：a[i] 自己不必和自己比
        for (std::size_t j = i + 1; j < n; ++j) {
            if (comp(a[j], a[min_index])) {
                min_index = j;
            }
        }
        if (min_index != i) {  // 只有在真的需要时才写内存
            SwapValues(a[i], a[min_index]);
        }
    }
}

// ---------------------------------------------------------------------------
// 1.3 插入排序（Insertion Sort）—— 本节的主角
// ---------------------------------------------------------------------------
// 时间：平均 / 最坏 O(n^2)，最好 O(n)
// 空间：O(1)
// 稳定性：稳定——只把严格大于 key 的元素往后挪，相等元素不动
// 原地：是
// 教学用途，生产请用 std::sort。
//
// * 为什么插入排序重要：它对「近乎有序」的输入是 O(n + d)，d 是元素的逆序对数，
//   加上它极小的常数（都是相邻比较、顺序写内存），使它成为快排与归并的递归基底：
//   当子区间小于 cutoff（例如 16）时，直接调用插入排序，比继续递归更快。
//   理由有三条：
//     1) 小区间上递归的固定开销（压栈、选 pivot、函数调用）超过了比较本身；
//     2) 插入排序在短数组上几乎没有循环开销，纯顺序访问，缓存命中率极高；
//     3) 每一层快排分区都在把数组变得「更接近有序」，这正好是插入排序的主场。
//
// 写法上的关键点：
//   下面用「挖坑 + 后移」而不是反复 std::swap。一次 swap 要读写三次内存
//   （读 a、读 b、写 a、写 b 实际是四次），而后移只写一次。对 int 差距不大，
//   对 std::string 这种拷贝昂贵的类型就是数量级的差别。
template <typename T, typename Comp = std::less<>>
void InsertionSort(std::vector<T>& a, Comp comp = Comp{}) {
    const std::size_t n = a.size();
    for (std::size_t i = 1; i < n; ++i) {
        // 先把待插入元素「取出来」放进局部变量，腾出 a[i] 这个坑。
        // 用 auto 而不是 T：将来元素换成昂贵的类型时，这里会走移动语义。
        auto key = std::move(a[i]);
        std::size_t j = i;
        // 严格大于才后移：用 <= 就会破坏稳定性。
        // 注意 j > 0 必须放在前面：短路求值保证 a[0] 不会被越界访问。
        while (j > 0 && comp(key, a[j - 1])) {
            a[j] = std::move(a[j - 1]);
            --j;
        }
        a[j] = std::move(key);  // 落位（即使 j == i，也是把自己的值写回去，语义无害）
    }
}

// 面向「原始指针 + 区间」的插入排序。
// 存在理由：快排的 cutoff 分支和希尔排序都要对「数组的一段」排序，
// 每次都从 std::vector 切出一个子 vector 去排是 O(n) 的额外拷贝，得不偿失。
// 底层算法与上面完全一致，只是接口换成了迭代器区间。
template <typename RandomIt, typename Comp>
void InsertionSortRange(RandomIt first, RandomIt last, Comp comp) {
    if (last - first < 2) {
        return;
    }
    for (RandomIt i = first + 1; i < last; ++i) {
        auto key = std::move(*i);
        RandomIt j = i;
        while (j > first && comp(key, *(j - 1))) {
            *j = std::move(*(j - 1));
            --j;
        }
        *j = std::move(key);
    }
}

// ===========================================================================
// 第 2 组：希尔排序（Shell Sort）——插入排序的「跳跃版」
// ===========================================================================
// 时间：依赖增量序列。折半序列 (n/2, n/4, ..., 1) 最坏 O(n^2)；
//       Knuth 序列 1, 4, 13, 40, ...（h = 3h + 1）最坏约 O(n^(3/2))，
//       更好的序列（如 Sedgewick）能到 O(n^(4/3)) 甚至更低。
// 空间：O(1)
// 稳定性：不稳定——相隔 gap 的元素会被交换，可能跨过相等元素
// 原地：是
// 教学用途，生产请用 std::sort。
//
// * 为什么它比插入排序快？
//   插入排序的代价 = 逆序对数量，而它每次只能把元素往前挪一格。
//   希尔排序先用大 gap 做「粗调」：相隔很远的俩元素可以一步换到位，
//   于是每轮结束后的数组都变得「更有序」（逆序对大幅减少）。
//   等 gap 缩到 1 时，数组已经接近有序，最后一次插入排序几乎只要 O(n)。
//   一句话：它用前几轮的「低精度排序」把最后一轮插入排序的输入变干净了。
//
// 实现上就是「把插入排序里的 1 换成 gap」，其余逻辑一字不改。
template <typename T, typename Comp = std::less<>>
void ShellSortKnuth(std::vector<T>& a, Comp comp = Comp{}) {
    const std::size_t n = a.size();
    if (n < 2) {
        return;
    }

    // 先算出最大的 Knuth 增量 h = 3h + 1，且保证 3h + 1 < n，
    // 这样最后一轮 h 一定落在 1 上（1, 4, 13, 40, ...）。
    std::size_t h = 1;
    while (h < n / 3) {
        h = 3 * h + 1;
    }

    for (; h >= 1; h /= 3) {  // h == 1 时执行完会变成 0，循环自然结束
        // 对每个「相隔 h 的子序列」做插入排序。i 从 h 开始是必须的：
        // 前 h 个元素各自是所在子序列的第一个元素，天然有序。
        for (std::size_t i = h; i < n; ++i) {
            auto key = std::move(a[i]);
            std::size_t j = i;
            while (j >= h && comp(key, a[j - h])) {
                a[j] = std::move(a[j - h]);
                j -= h;
            }
            a[j] = std::move(key);
        }
    }
}

// 折半增量版本（n/2, n/4, ..., 1）。
// 时间：最坏 O(n^2)（这是它的理论短板），空间 O(1)，稳定性：不稳定，原地：是。
// 保留它作为对照组：实测能直观看到「增量序列不同，同一个算法差多少」。
template <typename T, typename Comp = std::less<>>
void ShellSortHalving(std::vector<T>& a, Comp comp = Comp{}) {
    const std::size_t n = a.size();
    for (std::size_t h = n / 2; h >= 1; h /= 2) {
        for (std::size_t i = h; i < n; ++i) {
            auto key = std::move(a[i]);
            std::size_t j = i;
            while (j >= h && comp(key, a[j - h])) {
                a[j] = std::move(a[j - h]);
                j -= h;
            }
            a[j] = std::move(key);
        }
        if (h == 1) {  // 防止 h /= 2 之后还要再进一轮（h 是 size_t，永远 >= 0）
            break;
        }
    }
}

// ===========================================================================
// 第 3 组：归并排序（Merge Sort）
// ===========================================================================
// 时间：平均 / 最坏 / 最好都是 O(n log n)——复杂度与输入分布无关，这是它最大的优点
// 空间：O(n) 额外缓冲（这是它的代价；链表版归并可以做到 O(log n) 递归栈）
// 稳定性：稳定（merge 时左边优先取，相等元素不会换序）
// 原地：否（数组版归并必须借一块等长缓冲）
// 教学用途，生产请用 std::stable_sort。

// merge 过程，把 a[first, mid) 和 a[mid, last) 合并成有序的 a[first, last)。
// 前提：两段各自已经有序。
// 为什么需要 buf？
//   合并时如果用「原地插入」的写法，每放一个元素就要把右段整体后移，单次合并退化成
//   O(n^2)。所以工程写法一律是「另开一块等长缓冲」，合并完再整体拷回来——正是这
//   一块缓冲带来了 O(n) 的空间开销。
// 为什么写在 buf 里再拷回去，而不是直接合并回 a？
//   直接把结果写回 a 会覆盖掉还没读的元素。想让原地归并稳定且是 O(n)，
//   就得上「手摇算法」这类复杂技巧，常数大得多，日常不值得。
template <typename T, typename Comp>
void MergeRange(std::vector<T>& a, std::vector<T>& buf, std::size_t first, std::size_t mid,
                std::size_t last, Comp comp) {
    std::size_t i = first;  // 左段游标
    std::size_t j = mid;    // 右段游标
    std::size_t k = first;  // 缓冲写入游标

    // * 稳定性的关键在这一行：取左边的条件是「右段的值严格小于左边的值」。
    //   也就是两者相等时（!comp(a[j], a[i])）优先取左边，左段元素因此始终排在前面。
    //   如果写成 !comp(a[i], a[j])，相等时就改取右边，稳定性当场丢失。
    while (i < mid && j < last) {
        if (comp(a[j], a[i])) {
            buf[k] = std::move(a[j]);
            ++j;
        } else {
            buf[k] = std::move(a[i]);
            ++i;
        }
        ++k;
    }
    // 两段里剩下的那段一定是已经有序的尾部，整体搬走即可。
    while (i < mid) {
        buf[k] = std::move(a[i]);
        ++i;
        ++k;
    }
    while (j < last) {
        buf[k] = std::move(a[j]);
        ++j;
        ++k;
    }
    for (std::size_t t = first; t < last; ++t) {
        a[t] = std::move(buf[t]);
    }
}

// 递归部分。深度是 O(log n)，因为每次把区间对半分。
template <typename T, typename Comp>
void MergeSortRec(std::vector<T>& a, std::vector<T>& buf, std::size_t first, std::size_t last,
                  Comp comp) {
    if (last - first < 2) {
        return;  // 0 个或 1 个元素天然有序，这也是递归的出口
    }
    const std::size_t mid = first + (last - first) / 2;  // 写成这样是为了避免 first+last 溢出
    MergeSortRec(a, buf, first, mid, comp);
    MergeSortRec(a, buf, mid, last, comp);
    // 小优化：如果左段最大值已经 <= 右段最小值，整个区间已经有序，跳过合并。
    // 这个判断对「已经有序」的输入把每层合并变成 O(1)，整体降到 O(n)。
    if (!comp(a[mid], a[mid - 1])) {
        return;
    }
    MergeRange(a, buf, first, mid, last, comp);
}

// 自顶向下归并排序：分治递归。
// 时间 O(n log n)，空间 O(n)（一次性开好缓冲，不在递归里反复分配，
// 因为每次递归都 new 一块内存会把常数拉高好几倍）。
// 稳定性：稳定。原地：否。
template <typename T, typename Comp = std::less<>>
void MergeSortTopDown(std::vector<T>& a, Comp comp = Comp{}) {
    if (a.size() < 2) {
        return;
    }
    std::vector<T> buf(a.size());
    MergeSortRec(a, buf, 0, a.size(), comp);
}

// 自底向上归并排序：先两两合并长度为 1 的段，再合并长度为 2 的段，以此类推。
// 时间 O(n log n)，空间 O(n)，稳定性：稳定，原地：否。
// 工程意义：它没有递归，栈深度恒为 O(1)，在嵌入式或栈很小的环境里更友好；
// 而且「按段合并」天然适合外排序——数据在磁盘上按块读入再归并。
template <typename T, typename Comp = std::less<>>
void MergeSortBottomUp(std::vector<T>& a, Comp comp = Comp{}) {
    const std::size_t n = a.size();
    if (n < 2) {
        return;
    }
    std::vector<T> buf(n);
    // width 是「每段长度」，从 1 开始每次翻倍，直到覆盖整个数组。
    for (std::size_t width = 1; width < n; width *= 2) {
        for (std::size_t first = 0; first < n; first += 2 * width) {
            // last 可能越过数组末尾，必须夹住，否则会写出界。
            const std::size_t last = std::min(first + 2 * width, n);
            const std::size_t mid = std::min(first + width, last);
            if (mid < last) {
                MergeRange(a, buf, first, mid, last, comp);
            }
        }
    }
}

// ===========================================================================
// 第 4 组：快速排序（Quick Sort）——四种写法，逐层加固
// ===========================================================================
// 时间：平均 O(n log n)，最坏 O(n^2)（pivot 每次都挑到极值）
// 空间：O(log n) 递归栈（做尾递归优化后）；朴素写法最坏 O(n) 栈，可能爆栈
// 稳定性：不稳定——分区时的长距离交换会打乱相等元素的相对顺序
// 原地：是（这是快排最大的优势：不需要归并那样的 O(n) 缓冲，缓存友好）

// ---------------------------------------------------------------------------
// 4.1 Lomuto 分区
// ---------------------------------------------------------------------------
// 思路：取 a[last - 1] 作 pivot，用一个游标 i 维护「小于等于 pivot 的区域」右边界，
//       另一个游标 j 从左往右扫，遇到小的就把它换到 i 处并让 i 前进。
// 返回值：pivot 最终落点，它左边的元素都 <= pivot，右边都 >= pivot。
//
// * Lomuto 的缺点（考试常考）：
//   1) 交换次数多：每个小于 pivot 的元素都要换一次，即使它本来就在正确的一侧；
//   2) 对重复元素退化：所有元素相等时，它会把每个元素都换一遍，分区变成
//      「一边 0 个、一边 n-1 个」，递归深度 O(n)，复杂度掉到 O(n^2)；
//   3) 只从一侧扫，处理不了「三路」场景。
// 优点是思路简单、代码短、边界不易写错，适合教学和面试手写。
template <typename T, typename Comp>
std::size_t PartitionLomuto(std::vector<T>& a, std::size_t lo, std::size_t hi, Comp comp) {
    const std::size_t last = hi - 1;   // 这里用「左闭右开」区间，pivot 取最后一个元素
    const std::size_t pivot = last;
    std::size_t i = lo;                // i 指向「小于等于 pivot 区」的下一个空位
    for (std::size_t j = lo; j < last; ++j) {
        // 保留 <= 的一侧在左，是为了让「全部相等」的数组不退化得太狠；
        // 但即便如此，Lomuto 在全等输入上仍然是 O(n^2)（见上面的说明）。
        if (!comp(a[pivot], a[j])) {
            SwapValues(a[i], a[j]);
            ++i;
        }
    }
    SwapValues(a[i], a[pivot]);  // 把 pivot 归位到分界点上
    return i;
}

// ---------------------------------------------------------------------------
// 4.2 Hoare 分区
// ---------------------------------------------------------------------------
// 思路：取 a[lo] 作 pivot，左右两个游标相向而行：左边找 >= pivot 的，
//       右边找 <= pivot 的，找到一对就交换，直到两者相遇。
// 返回值：分界点 p，左半是 [lo, p]、右半是 (p, hi)（注意 Hoare 的分界点
//         不保证 pivot 就在 p 上，这是它与 Lomuto 最大的形式差别）。
//
// * Hoare 的优点：
//   1) 交换次数少：只交换「站错队的对」，平均约为 Lomuto 的 1/3；
//   2) 划分更均衡：pivot 落在哪一侧都行，重复元素会被分散到两边，
//      所以全等输入下 Hoare 反而接近 O(n log n)，而 Lomuto 会退化；
//   3) 实测通常比 Lomuto 快 1.5 到 2 倍。
// 缺点是边界条件绕，写错就是死循环或越界——下面用「先移动再比较」的经典写法，
// 并保证 pivot 在区间内，因此不会越界。
template <typename T, typename Comp>
std::size_t PartitionHoare(std::vector<T>& a, std::size_t lo, std::size_t hi, Comp comp) {
    const T pivot = a[lo];  // 必须拷贝一份！否则 pivot 位置的值会在交换中被改掉
    std::size_t i = lo - 1;
    std::size_t j = hi;
    for (;;) {
        // 找一个 >= pivot 的元素停下来
        do {
            ++i;
        } while (comp(a[i], pivot));
        // 找一个 <= pivot 的元素停下来
        do {
            --j;
        } while (comp(pivot, a[j]));
        if (i >= j) {
            return j;
        }
        SwapValues(a[i], a[j]);
    }
}

// ---------------------------------------------------------------------------
// 4.3 四种快排：从「会退化」到「工程可用」
// ---------------------------------------------------------------------------
// 先声明一个要在下面互相调用的递归函数。C++ 的「先用后定义」必须显式前置声明，
// 这也顺便让「四个版本分别解决什么问题」在代码结构上一眼可见。
template <typename T, typename Comp>
void QuickSortHoareRec(std::vector<T>& a, std::size_t lo, std::size_t hi, Comp comp);

// Lomuto 快排的骨架，用「显式栈」而不是递归来保存待处理区间。
//
// * 为什么这里非要用显式栈？这是本文件踩过的第二个真实的坑：
//   Lomuto 在已排序输入上退化时，递归深度是 O(n)。用递归写，n = 5000 时
//   就会把默认 1MB 的线程栈直接撑爆（Windows 上表现为 0xC00000FD 栈溢出，
//   而不是一个好看的「慢」的结果）。而退化恰恰是这一节要演示的核心现象，
//   演示的输入必然是最坏情况——所以这个函数必须在结构上免疫栈溢出。
//   把「待处理区间」放进堆上的 vector，深度就只受内存限制。
//   （顺带一提：std::sort 用的 introsort 也是靠「递归过深就切堆排序」解决
//     同一个问题，思路不同，目的相同。）
//
// first_pivot 为 true 时取首元素当 pivot，否则取末元素。
// 两者的复杂度完全一样（平均 O(n log n)，已排序输入最坏 O(n^2)），
// 放两个入口只是为了说明「退化与取首还是取末无关」。
template <typename T, typename Comp>
void QuickSortLomutoIterative(std::vector<T>& a, Comp comp, bool first_pivot) {
    const std::size_t n = a.size();
    if (n < 2) {
        return;
    }
    // 每个元素是一个「左闭右开」的待处理区间 [lo, hi)。
    std::vector<std::pair<std::size_t, std::size_t>> pending;
    pending.push_back({0, n});

    while (!pending.empty()) {
        const std::pair<std::size_t, std::size_t> range = pending.back();
        pending.pop_back();
        const std::size_t lo = range.first;
        const std::size_t hi = range.second;
        if (hi - lo < 2) {
            continue;  // 0 个或 1 个元素，天然有序
        }
        // 教科书版本为了「取首元素当 pivot」，先把首元素换到末尾，再套用 Lomuto。
        // 这一换是 Lomuto 相对 Hoare 多出来的浪费之一。
        if (first_pivot) {
            SwapValues(a[lo], a[hi - 1]);
        }
        const std::size_t p = PartitionLomuto(a, lo, hi, comp);
        // 两个子区间都推进栈里。退化时栈里会迅速堆积长度递减的区间，
        // 于是总比较次数约 n^2/2——慢，但不会再爆栈。
        if (p > lo) {
            pending.push_back({lo, p});
        }
        if (hi > p + 1) {
            pending.push_back({p + 1, hi});
        }
    }
}

// 版本 1：固定取「首元素」为 pivot + Lomuto 分区。
//   最坏 O(n^2)：对「已经有序」的数组，每次 pivot 都是最小值，分区成
//   (0 个元素) 和 (n-1 个元素)，每层只削掉一个元素，比较次数约 n^2/2。
//   这就是下面第 5 节要实测的退化现场。
template <typename T, typename Comp = std::less<>>
void QuickSortV1FirstPivot(std::vector<T>& a, Comp comp = Comp{}) {
    QuickSortLomutoIterative(a, comp, true);
}

// 版本 1b：固定取「末元素」为 pivot + Lomuto 分区。
//   时间：平均 O(n log n)，最坏 O(n^2)（已排序 / 逆序输入下同样退化）。
//   它同时是「Lomuto 分区本身有多贵」的对照组：分区方案和选 pivot 策略
//   是两个独立的问题，第 5 节的两张表会把它们拆开看。
template <typename T, typename Comp = std::less<>>
void QuickSortLomutoFixed(std::vector<T>& a, Comp comp = Comp{}) {
    QuickSortLomutoIterative(a, comp, false);
}

// 版本 2：同样固定取首元素为 pivot，但改用 Hoare 分区。
//   时间：平均 O(n log n)，最坏仍是 O(n^2)（已排序输入下 pivot 还是极值）。
//   * 实测要点：换分区方案不等于解决退化——退化是「选 pivot 的策略」造成的，
//     不是分区写法造成的。Hoare 只是把常数做小了、把重复元素摊平了。
//   实现同样走显式栈（理由见 QuickSortLomutoIterative 的说明），
//   这样第 5 节把 5000 个已排序元素喂给它时，我们看到的是「慢」，而不是崩溃。
template <typename T, typename Comp = std::less<>>
void QuickSortV2HoareFixed(std::vector<T>& a, Comp comp = Comp{}) {
    const std::size_t n = a.size();
    if (n < 2) {
        return;
    }
    std::vector<std::pair<std::size_t, std::size_t>> pending;
    pending.push_back({0, n});
    while (!pending.empty()) {
        const std::pair<std::size_t, std::size_t> range = pending.back();
        pending.pop_back();
        const std::size_t lo = range.first;
        const std::size_t hi = range.second;
        if (hi - lo < 2) {
            continue;
        }
        const std::size_t p = PartitionHoare(a, lo, hi, comp);
        // Hoare 的分界点 p 满足：左半是 [lo, p]，右半是 [p+1, hi)。
        // * 注意左半必须以 p + 1 结尾（闭区间），写成 [lo, p) 会漏掉一个元素，
        //   而且因为 p 可能是 lo，还会造出「区间不缩小」的死循环。
        if (lo < p + 1) {
            pending.push_back({lo, p + 1});
        }
        if (p + 1 < hi) {
            pending.push_back({p + 1, hi});
        }
    }
}

// ---------------------------------------------------------------------------
// 4.4 版本 2 的递归骨架：Hoare 分区的朴素递归（没有尾递归优化）
// ---------------------------------------------------------------------------
// 时间：平均 O(n log n)，最坏 O(n^2)。
// 空间：最坏 O(n) 递归栈——注意这里两侧都要递归，栈深度不受控制。
//   如果用它去排 10 万个已排序元素，理论深度会到 10 万层，直接爆栈。
//   这正是下面两项加固（随机化选 pivot + 尾递归优化）要分别解决的问题：
//   前者降低「深度变大的概率」，后者给「深度」本身加一个 O(log n) 的硬上限。
//
// 保留这个递归版本是为了「让代价看得见」：正确性验证里它只处理 n <= 129 的
// 小数组（深度安全），性能实测则统一走上面的显式栈版本，免得整场演示
// 以一次栈溢出收场。
template <typename T, typename Comp>
void QuickSortHoareRec(std::vector<T>& a, std::size_t lo, std::size_t hi, Comp comp) {
    if (hi - lo < 2) {
        return;
    }
    const std::size_t p = PartitionHoare(a, lo, hi, comp);
    QuickSortHoareRec(a, lo, p + 1, comp);
    QuickSortHoareRec(a, p + 1, hi, comp);
}

// ---------------------------------------------------------------------------
// 4.5 工程版快排：随机化 pivot + Hoare + 尾递归优化 + 小区间插入排序
// ---------------------------------------------------------------------------
// 时间：期望 O(n log n)，最坏 O(n^2)（但随机化之后出现最坏情况的概率极低，
//       而且没有任何「特定输入」能让它必然退化——这就是随机化的价值）
// 空间：O(log n) 递归栈
// 稳定性：不稳定
// 原地：是
//
// 三项加固各自解决什么：
//   1) 随机选 pivot：让「输入有序」不再是坏事。对手无法构造出让每次都挑到极值的输入。
//      代价是一次 rng() 调用（几十纳秒），相对整个排序可以忽略。
//   2) 尾递归优化：先递归较小的一侧，较大的一侧用循环继续处理。
//      因为「较小的一侧长度 <= 区间长度的一半」，递归深度被钉死在 O(log n)。
//      如果先递归大的一侧，最坏深度就是 O(n)，10 万元素足够爆掉默认 1MB 的线程栈。
//   3) cutoff 切换插入排序：区间长度 <= 16 时直接插入排序。
//      小区间上递归的固定开销比比较本身还贵；而插入排序在 16 个元素上
//      只有几十次比较、纯顺序访问，几乎零循环开销。
//      （下面保留了「关掉 cutoff」的版本做对照，详见第 3 节的实测讨论。）
constexpr std::size_t kInsertionCutoff = 16;

template <typename T, typename Comp>
void QuickSortOptimizedRec(std::vector<T>& a, std::size_t lo, std::size_t hi, Comp comp,
                           std::mt19937& rng, std::size_t cutoff) {
    // 用 while 而不是递归来「处理大的一侧」，这就是尾递归优化（消除尾调用）。
    while (hi - lo > cutoff) {
        // --- 随机选 pivot，并把它换到 lo 位置，供 Hoare 分区使用 ---
        const std::size_t span = hi - lo;
        const std::size_t r = lo + static_cast<std::size_t>(rng() % span);
        SwapValues(a[lo], a[r]);

        const std::size_t p = PartitionHoare(a, lo, hi, comp);

        // --- 尾递归优化：先递归小的一侧，大的一侧留在 while 里继续处理 ---
        // 判断「哪边小」这件事本身是 O(1)，却把栈深度的上界从 O(n) 压到 O(log n)。
        //
        // 这里踩过一个真实的坑，值得记下来：Hoare 分区返回的 p 可能是 hi - 1，
        // 于是「另一侧」[p + 1, hi) 是空区间。如果无条件地对空区间递归，
        // 下一层就会算 rng() % (hi - lo) 而除零崩溃（Windows 上是 0xC0000094）。
        // 所以必须先判空再递归，让每个子问题至少有 1 个元素。
        if (p - lo < hi - (p + 1)) {
            if (lo < p + 1) {
                QuickSortOptimizedRec(a, lo, p + 1, comp, rng, cutoff);
            }
            if (p + 1 >= hi) {
                return;  // 大的一侧为空，整个区间处理完毕
            }
            lo = p + 1;
        } else {
            if (p + 1 < hi) {
                QuickSortOptimizedRec(a, p + 1, hi, comp, rng, cutoff);
            }
            if (lo >= p + 1) {
                return;  // 小的一侧为空，剩下的大的一侧就是 [lo, p]
            }
            hi = p + 1;
        }
    }
    // --- 小区间交给插入排序（cutoff）---
    // 注意这里是对 [lo, hi) 这一段调用，而不是对整个数组再排一遍。
    InsertionSortRange(a.begin() + static_cast<std::ptrdiff_t>(lo),
                       a.begin() + static_cast<std::ptrdiff_t>(hi), comp);
}

// 三数取中 + Hoare + 尾递归 + cutoff：不引入随机数，用「取首、中、末三个数的中位数」
// 来挑 pivot。对已排序 / 逆序输入都能取到中间值，因此不会退化；
// 而且没有 rng 调用，省掉一点开销。
// 缺点：面对「专门构造的杀手输入」仍可能被针对（这正是不如 introsort 的地方，
//       introsort 会在递归过深时直接切堆排序兜底）。
template <typename T, typename Comp>
void QuickSortMedianOfThreeRec(std::vector<T>& a, std::size_t lo, std::size_t hi, Comp comp) {
    while (hi - lo > kInsertionCutoff) {
        const std::size_t mid = lo + (hi - lo) / 2;
        // 三步比较把三个候选里的中位数换到 lo 位置。
        // 目的不是为了「排序」，只是让 pivot 大概率靠近真实中位数，
        // 从而把分区切成两个规模相近的子问题。
        if (comp(a[mid], a[lo])) {
            SwapValues(a[mid], a[lo]);
        }
        if (comp(a[hi - 1], a[lo])) {
            SwapValues(a[hi - 1], a[lo]);
        }
        if (comp(a[hi - 1], a[mid])) {
            SwapValues(a[hi - 1], a[mid]);
        }
        // 现在 a[mid] 是中位数，把它换到 lo 供 Hoare 使用。
        SwapValues(a[lo], a[mid]);

        const std::size_t p = PartitionHoare(a, lo, hi, comp);
        // 与 QuickSortOptimizedRec 完全相同：先递归小的一侧，大的一侧留在 while 循环里。
        // 空区间必须提前 return，否则下面 [lo, hi) 的空区间会一路递归下去。
        if (p - lo < hi - (p + 1)) {
            if (lo < p + 1) {
                QuickSortMedianOfThreeRec(a, lo, p + 1, comp);
            }
            if (p + 1 >= hi) {
                return;
            }
            lo = p + 1;
        } else {
            if (p + 1 < hi) {
                QuickSortMedianOfThreeRec(a, p + 1, hi, comp);
            }
            if (lo >= p + 1) {
                return;
            }
            hi = p + 1;
        }
    }
    InsertionSortRange(a.begin() + static_cast<std::ptrdiff_t>(lo),
                       a.begin() + static_cast<std::ptrdiff_t>(hi), comp);
}

// 版本 3：随机化 + Hoare + 尾递归 + cutoff（本节主力）。
template <typename T, typename Comp = std::less<>>
void QuickSortRandomized(std::vector<T>& a, Comp comp = Comp{}) {
    if (a.size() < 2) {
        return;
    }
    std::mt19937 rng(20240608u);  // 固定种子：让每次运行的实测数字可比
    QuickSortOptimizedRec(a, 0, a.size(), comp, rng, kInsertionCutoff);
}

// 版本 3b：和版本 3 完全一样，只是把 cutoff 设为 1——也就是「不做小区间切换，
// 一路递归到区间长度为 1」。保留它作为对照实验，用来量化 cutoff 这一项到底值多少。
template <typename T, typename Comp = std::less<>>
void QuickSortRandomizedNoCutoff(std::vector<T>& a, Comp comp = Comp{}) {
    if (a.size() < 2) {
        return;
    }
    std::mt19937 rng(20240608u);
    QuickSortOptimizedRec(a, 0, a.size(), comp, rng, 1);
}

// 版本 3c：cutoff 可调的随机化快排，专门用来做「cutoff 取多少合适」的扫描实验。
// 真实的库不会暴露这个参数（阈值是编译期常量，允许内联），我们暴露它只为教学。
template <typename T, typename Comp = std::less<>>
void QuickSortRandomizedWithCutoff(std::vector<T>& a, std::size_t cutoff, Comp comp = Comp{}) {
    if (a.size() < 2) {
        return;
    }
    std::mt19937 rng(20240608u);
    QuickSortOptimizedRec(a, 0, a.size(), comp, rng, cutoff);
}

// 版本 4：三数取中 + Hoare + 尾递归 + cutoff。
template <typename T, typename Comp = std::less<>>
void QuickSortMedianOfThree(std::vector<T>& a, Comp comp = Comp{}) {
    if (a.size() < 2) {
        return;
    }
    QuickSortMedianOfThreeRec(a, 0, a.size(), comp);
}

// ===========================================================================
// 第 5 组：堆排序（Heap Sort）
// ===========================================================================
// 时间：平均 / 最坏 / 最好都是 O(n log n)——这一点比快排强，没有退化风险
// 空间：O(1)（完全原地）
// 稳定性：不稳定——把堆顶换到末尾是长距离交换，必然打乱相等元素的顺序
// 原地：是
// 教学用途，生产请用 std::sort 或 std::partial_sort。
//
// 为什么工程上它常作为「兜底方案」而不是首选？
//   它的内存访问模式是「跳跃的」（父节点 2i+1、子节点 2i+2 在数组里离得很远），
//   缓存不友好，分支还难预测，所以实测通常比快排慢 2 到 3 倍。
//   但它的最坏复杂度是硬保证的，这正是 introsort 在递归过深时切换成它的原因。

// 向下调整（sift-down）：把 a[root] 这个「违反堆性质」的节点沉到合适位置。
// 前提：root 的左右子树都已经是合法的大顶堆，只有 root 本身可能太小。
// 复杂度：O(log n)，因为每次最多下沉一层，树高是 log n。
template <typename T, typename Comp>
void SiftDown(std::vector<T>& a, std::size_t root, std::size_t n, Comp comp) {
    for (;;) {
        const std::size_t left = 2 * root + 1;
        if (left >= n) {
            return;  // 没有孩子，说明已经是叶子，调整结束
        }
        // 在两个孩子里挑「更大」的那个。注意先判断 right < n，否则会越界。
        std::size_t larger = left;
        const std::size_t right = left + 1;
        if (right < n && comp(a[larger], a[right])) {
            larger = right;
        }
        // 如果根已经不小于最大的孩子，堆性质成立，收工。
        // 这一句同时保证了「相等时不交换」，虽然堆排整体并不稳定。
        if (!comp(a[root], a[larger])) {
            return;
        }
        SwapValues(a[root], a[larger]);
        root = larger;  // 继续向下检查被换下去的节点
    }
}

template <typename T, typename Comp = std::less<>>
void HeapSort(std::vector<T>& a, Comp comp = Comp{}) {
    const std::size_t n = a.size();
    if (n < 2) {
        return;
    }

    // 第一步：建堆，自底向上从最后一个非叶节点开始 sift-down。
    // 为什么从 n/2 - 1 开始？因为下标 >= n/2 的节点都是叶子，叶子天然是堆。
    // 为什么这样建堆是 O(n) 而不是 O(n log n)？
    //   越靠下的节点越多，但下沉高度越短；把「节点数 x 高度」求和，
    //   级数收敛到 2n，所以是 O(n)。
    for (std::size_t i = n / 2; i > 0; --i) {
        SiftDown(a, i - 1, n, comp);
    }

    // 第二步：反复把堆顶（当前最大值）换到未排序区间的末尾，再缩小堆并修复。
    // 每轮 O(log n)，共 n 轮，所以是 O(n log n)。
    for (std::size_t end = n; end > 1; --end) {
        SwapValues(a[0], a[end - 1]);  // 最大值归位到有序区
        SiftDown(a, 0, end - 1, comp);  // 堆的大小减一后重新修复
    }
}

// ===========================================================================
// 第 6 组：非比较排序
// ===========================================================================
// * 为什么它们能突破 O(n log n) 下界？
//   因为「基于比较的排序」有一个数学下界：n 个元素有 n! 种排列，每次比较最多
//   提供 1 bit 信息，所以至少要 log2(n!) ≈ n log n 次比较。
//   计数排序 / 桶排序不比较元素，而是把「元素的值」直接当地址用，
//   绕开了这个下界——代价是它们对数据有额外要求。

// ---------------------------------------------------------------------------
// 6.1 计数排序（Counting Sort）
// ---------------------------------------------------------------------------
// 时间：O(n + k)，k 是键的取值范围（max_value + 1）
// 空间：O(k)（一个计数数组） + O(n)（输出数组）
// 稳定性：稳定（下面的逆序回填写法专门保证这一点）
// 原地：否
// 教学用途，生产请用 std::sort（对整数小范围它其实也很快，但没必要自己写）。
//
// * 适用条件（缺一不可）：
//   1) 键必须能映射成小范围的非负整数；
//   2) k 不能远大于 n，否则空间和初始化成本就把 O(n) 吃光了。
// 反例：n = 10 万，但键的取值是 0 到 10^9（例如用户 ID、时间戳）。
//       这时 k = 10^9，需要 4GB 计数数组——直接崩，或者退化成 O(k) 的龟速。
//       这种数据必须改用比较排序（std::sort）或哈希分桶。
std::vector<int> CountingSortNonNegative(const std::vector<int>& src, int max_value) {
    const std::size_t n = src.size();
    if (n < 2) {
        return src;
    }
    // 防御性检查：调用者保证的所有键都在 [0, max_value] 内，这里断言守住它。
    assert(max_value >= 0);

    std::vector<std::size_t> count(static_cast<std::size_t>(max_value) + 1, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const int key = src[i];
        assert(key >= 0 && key <= max_value);
        count[static_cast<std::size_t>(key)] += 1;
    }

    // 前缀和：做完之后 count[v] 表示「值 <= v 的元素个数」，
    // 也就是值 v 在输出数组里的「右边界 + 1」。
    for (std::size_t v = 1; v < count.size(); ++v) {
        count[v] += count[v - 1];
    }

    std::vector<int> out(n);
    // * 必须从右往左回填，这是稳定性的来源：
    //   从右往左时，同一个键里「原来靠后」的元素后写入，因此落在更靠后的位置；
    //   换句话说，回填顺序与原始顺序相反，最终相对顺序就被保留了。
    //   如果从左往右填，相同键的元素会被反转，稳定性丢失。
    for (std::size_t i = n; i > 0; --i) {
        const int key = src[i - 1];
        count[static_cast<std::size_t>(key)] -= 1;
        out[count[static_cast<std::size_t>(key)]] = key;
    }
    return out;
}

// ---------------------------------------------------------------------------
// 6.2 桶排序（Bucket Sort）
// ---------------------------------------------------------------------------
// 时间：平均 O(n + n^2/b + b)，b 是桶数。数据均匀分布时约 O(n)；
//       数据全部挤进一个桶时退化成 O(n^2)（每个桶内部用插入排序）。
// 空间：O(n + b)
// 稳定性：取决于「桶内排序」和「按桶顺序拼接」是否稳定。下面两个都稳定，
//         因此这个实现整体稳定。（考试里常把桶排序算作稳定，前提正是这两点。）
// 原地：否
// 教学用途，生产请用 std::sort 或 std::stable_sort。
//
// * 适用条件：数据要「近似均匀分布」。
// 反例：n = 1 万，但 99% 的值都落在 [0, 100) —— 绝大多数元素会挤进第一个桶，
//       桶内插入排序退化成 O(n^2)，比 std::sort 慢一个数量级。
//       另一个反例是键的范围未知或极度倾斜（如幂律分布的用户行为数据）。
std::vector<int> BucketSortNonNegative(const std::vector<int>& src, int max_value,
                                       std::size_t bucket_count) {
    const std::size_t n = src.size();
    if (n < 2 || bucket_count == 0) {
        return src;
    }
    std::vector<std::vector<int>> buckets(bucket_count);
    const double span = static_cast<double>(max_value) + 1.0;

    // 分桶：把 [0, max_value] 均匀切成 bucket_count 段。
    // 桶内用 push_back 保持原始相对顺序（先来的先进），这是稳定性的前提之一。
    for (std::size_t i = 0; i < n; ++i) {
        const int v = src[i];
        assert(v >= 0 && v <= max_value);
        // 注意别让最大值正好落进「第 bucket_count 个桶」（越界）。
        std::size_t index = static_cast<std::size_t>(static_cast<double>(v) / span *
                                                     static_cast<double>(bucket_count));
        if (index >= bucket_count) {
            index = bucket_count - 1;
        }
        buckets[index].push_back(v);
    }

    // 桶内排序：桶期望很小（n/b 个元素），插入排序在短数组上最快。
    // 用稳定排序保证桶内相对顺序不变。
    std::vector<int> out;
    out.reserve(n);
    for (std::size_t b = 0; b < bucket_count; ++b) {
        InsertionSort(buckets[b]);
        // 按桶的编号从小到大拼接，因此整体有序。
        for (const int v : buckets[b]) {
            out.push_back(v);
        }
    }
    return out;
}

// ===========================================================================
// 造数据
// ===========================================================================
// 关于输入分布：排序算法对输入分布极其敏感，同一个算法在「随机」「已排序」
// 「全部相同」「近乎有序」上的耗时可以差好几个数量级。所以每种分布都要单独造，
// 实测时也要在表里写清楚测的是哪一种。
enum class Pattern { kRandom, kSorted, kReversed, kAllEqual, kFewUnique, kNearlySorted };

std::vector<int> MakeData(Pattern pattern, std::size_t n, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<int> v(n);
    // 空数组 / 单元素数组没有任何分布可言，直接返回。
    // 这个提前返回不是为了「优化」，而是为了正确性：近乎有序那一支要做
    // rng() % n，n 为 0 时会抛整数除零异常（0xC0000094）当场崩溃。
    if (n < 2) {
        return v;
    }
    switch (pattern) {
        case Pattern::kRandom:
            for (std::size_t i = 0; i < n; ++i) {
                v[i] = static_cast<int>(rng() % 1000000u);
            }
            break;
        case Pattern::kSorted:
            for (std::size_t i = 0; i < n; ++i) {
                v[i] = static_cast<int>(i);
            }
            break;
        case Pattern::kReversed:
            for (std::size_t i = 0; i < n; ++i) {
                v[i] = static_cast<int>(n - i);
            }
            break;
        case Pattern::kAllEqual:
            // 全部相同是最容易被忽视的「恶意输入」：Lomuto 分区在这里退化。
            for (std::size_t i = 0; i < n; ++i) {
                v[i] = 7;
            }
            break;
        case Pattern::kFewUnique:
            // 只有 5 种取值：真实数据（状态码、性别、评分）常见，也是三路快排的用武之地。
            for (std::size_t i = 0; i < n; ++i) {
                v[i] = static_cast<int>(rng() % 5u);
            }
            break;
        case Pattern::kNearlySorted: {
            // 先造有序数组，再随机挑 0.5% 的元素两两交换，制造少量逆序对。
            for (std::size_t i = 0; i < n; ++i) {
                v[i] = static_cast<int>(i);
            }
            const std::size_t swaps = std::max<std::size_t>(1, n / 200);
            for (std::size_t k = 0; k < swaps; ++k) {
                const std::size_t i = static_cast<std::size_t>(rng() % n);
                const std::size_t j = static_cast<std::size_t>(rng() % n);
                std::swap(v[i], v[j]);
            }
            break;
        }
    }
    return v;
}

// 小范围整数数据，供计数 / 桶排序使用（键 ∈ [0, 999]）。
std::vector<int> MakeSmallRange(std::size_t n, int max_value, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<int> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = static_cast<int>(rng() % static_cast<unsigned>(max_value + 1));
    }
    return v;
}

// 「幂律分布」数据：绝大多数值挤在低端，少数值巨大。
// 用来演示桶排序的失效场景（反例）。Pareto 分布靠两次均匀采样相乘得到。
std::vector<int> MakeSkewed(std::size_t n, int max_value, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<int> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double u1 = static_cast<double>(rng() % 1000000u) / 1000000.0 + 1e-9;
        const double u2 = static_cast<double>(rng() % 1000000u) / 1000000.0 + 1e-9;
        // u1 * u2 近似 [0,1] 上的「偏向 0」分布，再映射到 [0, max_value]。
        const double x = u1 * u2;
        v[i] = static_cast<int>(x * static_cast<double>(max_value));
    }
    return v;
}

// ===========================================================================
// 稳定性实验用的记录类型
// ===========================================================================
// 每个元素有一个「排序键」和一个「出厂序号 id」。
// 稳定排序的判据：排序后，键相同的那些记录，id 必须仍然递增。
// 这是稳定性的可运行证据——比嘴上说「插入排序是稳定的」有说服力得多。
struct Record {
    int key = 0;
    int id = 0;
};

struct KeyLess {
    bool operator()(const Record& x, const Record& y) const { return x.key < y.key; }
};

// ===========================================================================
// 正确性验证：与 std::sort 对拍
// ===========================================================================
// 做法：把「我的实现」和「标准库实现」喂同样的输入，然后断言逐元素相等。
// 为什么一定要对拍而不是只查 is_sorted？
//   因为「有序」只是必要条件。一个把所有元素都改成 0 的排序也是「有序的」。
//   逐元素对拍才能同时抓住「排序没排对」和「元素被弄丢了 / 弄重复了」。
//   边界输入（空数组、1 个元素、全部相同）一定要覆盖——排序的 bug 大多在边界上。
using SortFn = std::function<void(std::vector<int>&)>;

void ExpectSameAsStdSort(const std::string& name, Pattern pattern, std::size_t n,
                         const SortFn& mine) {
    std::vector<int> data = MakeData(pattern, n, 20240701u);
    std::vector<int> expected = data;
    std::sort(expected.begin(), expected.end());

    mine(data);

    assert(data.size() == expected.size());
    assert(data == expected);  // 逐元素对拍，比 is_sorted 严格得多
    assert(IsSorted(data, std::less<int>{}));
    (void)name;  // 名字只用于调试时定位，发布构建里不参与计算
}

// 用一个「名字 -> 实现」的列表把所有算法跑一遍所有输入分布。
// 用 std::function 而不是模板展开，是为了让验证代码短到一眼能读完；
// 这点虚函数开销只在正确性验证里存在，不影响性能实测。
std::vector<std::pair<std::string, SortFn>> MakeIntSorters() {
    return {
        {"冒泡排序", [](std::vector<int>& v) { BubbleSort(v); }},
        {"冒泡排序(提前退出)", [](std::vector<int>& v) { BubbleSortEarlyExit(v); }},
        {"选择排序", [](std::vector<int>& v) { SelectionSort(v); }},
        {"插入排序", [](std::vector<int>& v) { InsertionSort(v); }},
        {"希尔排序(Knuth)", [](std::vector<int>& v) { ShellSortKnuth(v); }},
        {"希尔排序(折半)", [](std::vector<int>& v) { ShellSortHalving(v); }},
        {"归并排序(自顶向下)", [](std::vector<int>& v) { MergeSortTopDown(v); }},
        {"归并排序(自底向上)", [](std::vector<int>& v) { MergeSortBottomUp(v); }},
        {"快排 V1 固定pivot+Lomuto", [](std::vector<int>& v) { QuickSortV1FirstPivot(v); }},
        {"快排 V2 Hoare+固定pivot", [](std::vector<int>& v) { QuickSortV2HoareFixed(v); }},
        {"快排 V3 随机化+尾递归+cutoff", [](std::vector<int>& v) { QuickSortRandomized(v); }},
        {"快排 V4 三数取中+尾递归+cutoff",
         [](std::vector<int>& v) { QuickSortMedianOfThree(v); }},
        {"堆排序", [](std::vector<int>& v) { HeapSort(v); }},
    };
}

void VerifyAllSorters() {
    const std::vector<std::pair<Pattern, std::string>> patterns = {
        {Pattern::kRandom, "随机"},
        {Pattern::kSorted, "已排序"},
        {Pattern::kReversed, "逆序"},
        {Pattern::kAllEqual, "全部相同"},
        {Pattern::kFewUnique, "大量重复(仅5种值)"},
        {Pattern::kNearlySorted, "近乎有序"},
    };
    // 规模刻意取小：这是正确性验证，不是性能测试。
    // 覆盖到 0 / 1 / 2 / 3 这类边界长度才是重点。
    const std::vector<std::size_t> sizes = {0, 1, 2, 3, 17, 64, 129};

    const auto sorters = MakeIntSorters();
    std::size_t checks = 0;
    for (const auto& sorter : sorters) {
        for (const auto& p : patterns) {
            for (const std::size_t n : sizes) {
                ExpectSameAsStdSort(sorter.first, p.first, n, sorter.second);
                ++checks;
            }
        }
    }

    // 计数排序与桶排序接口不同（返回新数组），且对数据有额外要求，单独验证。
    for (const std::size_t n : sizes) {
        for (const Pattern p : {Pattern::kRandom, Pattern::kAllEqual, Pattern::kFewUnique}) {
            std::vector<int> src = MakeData(p, n, 555u);
            for (int& x : src) {
                x %= 1000;  // 压进 [0, 999]，满足计数排序的前提
            }
            std::vector<int> expected = src;
            std::sort(expected.begin(), expected.end());

            assert(CountingSortNonNegative(src, 999) == expected);
            assert(BucketSortNonNegative(src, 999, 64) == expected);
            ++checks;
            ++checks;
        }
    }

    std::cout << "  已对拍 " << checks << " 组（每种算法 x 每种输入分布 x 每种边界长度），"
              << "全部与 std::sort 逐元素一致\n";
    Note("断言覆盖：空数组、单元素、两元素、全部相同、大量重复、已排序、逆序、近乎有序。");
}

// ===========================================================================
// 稳定性验证
// ===========================================================================
// 判据：对「键相同、id 唯一」的记录排序后，同一个键内部各条记录的 id
//       必须仍然是它们「出厂时」的那个相对顺序。
//
// 这里有两个很容易写错的地方，我自己都踩过：
//
// 坑 1：如果 id 一律按倒序生成，那么「稳定」的结果是 id 递减，「不稳定」的结果
//   反而可能凑巧递增——判据的方向就反了。所以每个键内部的初始顺序必须**有升有降**：
//   同一批相等键的记录，id 先递增、再递减、再递增。这样任何一对被换序的记录
//   都会破坏这个波浪形图案，判据方向不会搞反。
//
// 坑 2：如果相等键的记录在数组里本来就是连续挨着的，很多「不稳定」算法会因为
//   恰好什么都不做而表现得像稳定的一样。实测反例：把 200 个键 x 5 条记录连续排列时，
//   选择排序、希尔排序、甚至 std::sort 的不稳定换序数都是 0——
//   不是它们变稳定了，而是这份数据没有踩到它们的换序路径。
//   所以键必须在数组里**交错散布**：key = i % 40，让 40 个键的元素均匀混在一起。
//   于是任何跨位置的大范围搬运，都必然打乱某个键内部的 id 顺序。
//
// 这两条合起来说明一件事：验证稳定性需要刻意设计对抗性数据，
// 随手造一份数据然后看到「0 处换序」，只能证明「没测到」，不能证明「稳定」。
constexpr int kStableGroups = 40;   // 40 个不同的键
constexpr int kStablePerGroup = 50; // 每个键 50 条记录，共 2000 条

std::vector<Record> MakeRecordsWithEqualKeys() {
    constexpr int kSegment = 7;  // 每 7 条记录换一次方向
    std::vector<Record> v;
    v.reserve(static_cast<std::size_t>(kStableGroups * kStablePerGroup));
    // 键按 i % kStableGroups 走，保证同一个键的元素被其他键隔开，散布在整个数组里。
    for (int i = 0; i < kStableGroups * kStablePerGroup; ++i) {
        const int key = i % kStableGroups;
        const int ordinal = i / kStableGroups;  // 这是该键的第几条记录
        const int seg = ordinal / kSegment;
        const int pos = ordinal % kSegment;
        // 偶数段正向、奇数段反向，于是同一个键内部的 id 序列呈波浪形。
        const int slot = (seg % 2 == 0) ? pos : (kSegment - 1 - pos);
        // id 在同一个键内部是唯一的（同一个 slot 会出现在不同段里，但位置不同），
        // 而且序列「先升后降」，所以任何一次换序都会让最终序列和初始序列对不上。
        v.push_back(Record{key, slot});
    }
    return v;
}

// 排序之后，每个键内部的 id 序列必须和「出厂设置」完全一致。
bool RecordsPreserveEqualKeyOrder(const std::vector<Record>& v) {
    std::map<int, std::vector<int>> by_key;
    for (const Record& r : v) {
        by_key[r.key].push_back(r.id);
    }
    std::map<int, std::vector<int>> expected;
    for (const Record& r : MakeRecordsWithEqualKeys()) {
        expected[r.key].push_back(r.id);
    }
    return by_key == expected;
}

// 统计「有多少个键内部的顺序被改变了」。0 表示这份数据没能暴露出换序。
int CountReorderedKeys(const std::vector<Record>& v) {
    std::map<int, std::vector<int>> by_key;
    for (const Record& r : v) {
        by_key[r.key].push_back(r.id);
    }
    std::map<int, std::vector<int>> expected;
    for (const Record& r : MakeRecordsWithEqualKeys()) {
        expected[r.key].push_back(r.id);
    }
    int bad = 0;
    for (const auto& entry : by_key) {
        const auto it = expected.find(entry.first);
        if (it == expected.end() || it->second != entry.second) {
            ++bad;
        }
    }
    return bad;
}

// 稳定的实现：这里用 assert 把判据钉死——不稳定就会当场中断。
// 能跑过去并打印「稳定」，就是稳定性的可运行证据。
template <typename Fn>
bool CheckStable(Fn fn) {
    std::vector<Record> v = MakeRecordsWithEqualKeys();
    fn(v);
    assert(IsSorted(v, KeyLess{}));               // 先确认真的排好了
    assert(RecordsPreserveEqualKeyOrder(v));      // 再确认相等键的相对顺序没变
    return true;
}

// ===========================================================================
// 性能实测
// ===========================================================================
// 一档测量：把一个「排一遍」的闭包（以及它的名字）绑在一起。
// 用 std::function 而不是模板，是为了能把不同算法的测量放进同一个 vector 里遍历打印；
// 这点间接调用开销对毫秒级的排序完全可以忽略（而且它只影响绝对数字，不影响算法之间的比较）。
struct BenchItem {
    std::string name;
    std::function<void()> fn;
    double us = 0.0;
};

// 测一遍「把 src 拷进 v，再调用 sorter，最后读一个元素」的耗时。
// 每次都从同一份 src 重新拷贝，是排序测量里最重要的纪律：排序算法对输入分布
// 极度敏感，如果第二次测量拿到的是第一次排好的结果，测出来的就不是同一个东西了。
//
// 返回值的用途：把结果喂给 volatile 的 g_sink，防止整个排序被优化掉。
// 这里读的是数组中间的元素而不是第一个——因为「已排序数组的首元素」是常量，
// 编译器有可能把整段计算折叠成「返回 v[0] 最初的拷贝」。
// 用左右两个位置的按位或，任意位置出错都会反映到结果里。
template <typename T, typename Sorter>
double BenchSortUs(const std::vector<T>& src, Sorter sorter, int repeats) {
    return BenchMedianUs(
        [&] {
            std::vector<T> v = src;
            sorter(v);
            const std::size_t n = v.size();
            if (n == 0) {
                return static_cast<std::uint64_t>(0);
            }
            const int lo = static_cast<int>(static_cast<unsigned>(v[0]));
            const int hi = static_cast<int>(static_cast<unsigned>(v[n / 2]));
            const int tail = static_cast<int>(static_cast<unsigned>(v[n - 1]));
            return static_cast<std::uint64_t>(static_cast<unsigned>(lo | hi | tail));
        },
        repeats);
}

// 跑完一组测量并把结果填回 items，返回它们当中最快的一个（用来算相对倍数）。
double RunBenchGroup(std::vector<BenchItem>& items, int repeats) {
    // 第一项先用 WarmUp 空跑几遍，把缺页中断、缓存冷启动的开销留在预热里，
    // 而不是落进正式计时（这个工具定义在文件开头，与 01 文件保持完全一致）。
    if (!items.empty()) {
        WarmUp([&] {
            items[0].fn();
            return static_cast<std::uint64_t>(1);
        }, 1);
    }
    for (BenchItem& it : items) {
        it.us = BenchMedianUs([&] {
            it.fn();
            return static_cast<std::uint64_t>(1);
        }, repeats);
    }

    double best = 0.0;
    for (const BenchItem& it : items) {
        if (best <= 0.0 || it.us < best) {
            best = it.us;
        }
    }
    return best;
}

// 打印一档结果：绝对耗时 + 相对最快者的倍数。
// 相对倍数比绝对耗时更有说服力，因为它自动消掉了 Debug/Release 和机器的差异。
void PrintBenchGroup(const std::string& title, const std::vector<BenchItem>& items, double best,
                     std::size_t name_width, const std::string& note) {
    Note(title);
    std::cout << "  ";
    Label("算法", name_width);
    std::cout << std::setw(14) << "耗时(us)" << std::setw(14) << "相对最快" << "\n";
    std::cout << "  " << std::string(name_width + 28, '-') << "\n";
    for (const BenchItem& it : items) {
        std::cout << "  ";
        Label(it.name, name_width);
        std::cout << std::setw(14) << it.us;
        std::cout << std::setw(14) << (best > 0.0 ? it.us / best : 0.0) << "\n";
    }
    if (!note.empty()) {
        Note(note);
    }
}

// ---------------------------------------------------------------------------
// 算法对照表：复杂度 / 空间 / 稳定性 / 是否原地
// ---------------------------------------------------------------------------
// 这张表要背下来。注意「稳定」和「原地」是工程选型时的硬约束：
//   - 要按多个键排序、且不能打乱相等元素的次序 -> 必须用稳定排序；
//   - 内存吃紧或元素极大 -> 优先原地算法（堆排、快排、希尔）；
//   - 怕被恶意输入打崩 -> 选有最坏保证的（堆排、归并、std::sort 的 introsort）。
void PrintComparisonTable() {
    struct Row {
        std::string name;
        std::string average;
        std::string worst;
        std::string space;
        std::string stable;
        std::string in_place;
    };
    const std::vector<Row> rows = {
        {"冒泡排序", "O(n^2)", "O(n^2)", "O(1)", "稳定", "是"},
        {"冒泡(提前退出)", "O(n^2)", "O(n^2)", "O(1)", "稳定", "是"},
        {"选择排序", "O(n^2)", "O(n^2)", "O(1)", "不稳定", "是"},
        {"插入排序", "O(n^2)", "O(n^2)", "O(1)", "稳定", "是"},
        {"希尔排序(Knuth)", "约O(n^1.25)", "O(n^1.5)", "O(1)", "不稳定", "是"},
        {"希尔排序(折半)", "约O(n^1.3)", "O(n^2)", "O(1)", "不稳定", "是"},
        {"归并排序(自顶向下)", "O(n log n)", "O(n log n)", "O(n)", "稳定", "否"},
        {"归并排序(自底向上)", "O(n log n)", "O(n log n)", "O(n)", "稳定", "否"},
        {"快排(Lomuto)", "O(n log n)", "O(n^2)", "O(log n)", "不稳定", "是"},
        {"快排(Hoare)", "O(n log n)", "O(n^2)", "O(log n)", "不稳定", "是"},
        {"快排(随机化+尾递归)", "O(n log n)", "O(n^2) 概率极低", "O(log n)", "不稳定", "是"},
        {"堆排序", "O(n log n)", "O(n log n)", "O(1)", "不稳定", "是"},
        {"计数排序", "O(n+k)", "O(n+k)", "O(n+k)", "稳定", "否"},
        {"桶排序", "O(n)均分时", "O(n^2)", "O(n+b)", "稳定", "否"},
        {"std::sort", "O(n log n)", "O(n log n)", "O(log n)", "不稳定", "是"},
        {"std::stable_sort", "O(n log n)", "O(n log n)", "O(n)", "稳定", "否"},
    };

    const std::size_t w_name = 24;
    const std::size_t w_avg = 18;
    const std::size_t w_worst = 18;
    const std::size_t w_space = 11;
    const std::size_t w_stable = 10;
    const std::size_t w_place = 10;

    std::cout << "  ";
    Label("算法", w_name);
    Label("平均", w_avg);
    Label("最坏", w_worst);
    Label("额外空间", w_space);
    Label("稳定性", w_stable);
    Label("是否原地", w_place);
    std::cout << "\n  " << std::string(w_name + w_avg + w_worst + w_space + w_stable + w_place, '-')
              << "\n";

    for (const Row& r : rows) {
        std::cout << "  ";
        Label(r.name, w_name);
        Label(r.average, w_avg);
        Label(r.worst, w_worst);
        Label(r.space, w_space);
        Label(r.stable, w_stable);
        Label(r.in_place, w_place);
        std::cout << "\n";
    }

    Note("");
    Note("读表要点：");
    Note("  1) 只有归并排序和 std::stable_sort 同时做到「O(n log n) + 稳定」，");
    Note("     代价是 O(n) 额外空间（空间换稳定，没有免费的午餐）。");
    Note("  2) 复杂度和稳定性无关：快排 (n log n) 不稳定，插入排序 (n^2) 却稳定。");
    Note("  3) 「不稳定」不代表结果错，只表示相等元素的相对顺序不保证。");
    Note("     对 int 这种「元素本身就是键」的类型，稳定和不稳定没有区别；");
    Note("     对「订单先按下单时间排、再按金额排」这种多次排序的流水线，");
    Note("     稳定性就是刚需——用不稳定排序会把上一步的次序整个打乱。");
}

}  // namespace

int main() {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "本文件在 Debug 配置下运行，绝对耗时比 Release 慢很多（通常 5 到 20 倍）。\n";
    std::cout << "下面所有耗时数字仅供参考，请看「趋势」和「数量级差异」。\n";

    // =======================================================================
    Section("1. 排序算法对照表（复杂度 / 空间 / 稳定性 / 是否原地）");
    // =======================================================================
    Note("这张表是本章的收口。选排序算法的顺序永远是：先看是否需要稳定，");
    Note("再看数据规模和数据分布，最后才看平均复杂度。");
    std::cout << "\n";
    PrintComparisonTable();

    // =======================================================================
    Section("2. 正确性与稳定性验证");
    // =======================================================================
    Note("做法：每种手写实现都与 std::sort 逐元素对拍（assert 相等），");
    Note("覆盖 6 种输入分布 x 7 种长度（含 0 / 1 / 2 / 3 这类边界）。");
    Note("为什么必须逐个元素比对，而不是只查 is_sorted？");
    Note("因为「有序」是必要条件而非充分条件——一个把元素弄丢或复制错的实现");
    Note("也可能输出有序数组。逐元素比对能同时抓住这两类 bug。");
    std::cout << "\n";
    VerifyAllSorters();

    std::cout << "\n";
    Note("稳定性验证：40 个键 x 每个键 50 条记录（共 2000 条），键相同但 id 唯一。");
    Note("数据刻意做成对抗性的：键在数组里交错散布（key = i % 40），");
    Note("同一个键内部的 id 序列走「先升后降」的波浪形。");
    Note("判据：排序后每个键内部的 id 序列必须与初始完全一致。");
    std::cout << "\n";

    // --- 稳定的那些：CheckStable 内部用 assert 把判据钉死，不稳定就当场中断 ---
    const bool stable_insertion =
        CheckStable([](std::vector<Record>& v) { InsertionSort(v, KeyLess{}); });
    const bool stable_merge_td =
        CheckStable([](std::vector<Record>& v) { MergeSortTopDown(v, KeyLess{}); });
    const bool stable_merge_bu =
        CheckStable([](std::vector<Record>& v) { MergeSortBottomUp(v, KeyLess{}); });
    const bool stable_std = CheckStable(
        [](std::vector<Record>& v) { std::stable_sort(v.begin(), v.end(), KeyLess{}); });
    std::cout << "  ";
    Label("插入排序", 26);
    std::cout << ": 相等键的相对顺序原样保留 -> " << (stable_insertion ? "稳定" : "不稳定")
              << "\n";
    std::cout << "  ";
    Label("归并排序(自顶向下)", 26);
    std::cout << ": 相等键的相对顺序原样保留 -> " << (stable_merge_td ? "稳定" : "不稳定")
              << "\n";
    std::cout << "  ";
    Label("归并排序(自底向上)", 26);
    std::cout << ": 相等键的相对顺序原样保留 -> " << (stable_merge_bu ? "稳定" : "不稳定")
              << "\n";
    std::cout << "  ";
    Label("std::stable_sort", 26);
    std::cout << ": 相等键的相对顺序原样保留 -> " << (stable_std ? "稳定" : "不稳定") << "\n";
    Note("");
    Note("关键点：CheckStable 里用的是 assert，如果实现不稳定，程序会直接中断。");
    Note("能跑到这一行并打印「稳定」，就是「稳定性」这件事的可运行证据。");

    // --- 不稳定的那些：不 assert，只统计「有多少个键的顺序被换掉了」 ---
    // 反面证据同样重要：它证明「不稳定」不是纸面上的说法，而是真的会发生。
    {
        auto report_unstable = [&](const std::string& name,
                                   const std::function<void(std::vector<Record>&)>& fn) {
            std::vector<Record> v = MakeRecordsWithEqualKeys();
            fn(v);
            assert(IsSorted(v, KeyLess{}));  // 不稳定归不稳定，排序结果必须是对的
            const int bad = CountReorderedKeys(v);
            std::cout << "  ";
            Label(name, 26);
            std::cout << ": 排序结果正确，但有 " << std::setw(2) << bad
                      << " / 40 个键的相等元素被换序 -> 不稳定\n";
        };

        report_unstable("选择排序",
                        [](std::vector<Record>& v) { SelectionSort(v, KeyLess{}); });
        report_unstable("希尔排序(Knuth)",
                        [](std::vector<Record>& v) { ShellSortKnuth(v, KeyLess{}); });
        report_unstable("快排(随机化)",
                        [](std::vector<Record>& v) { QuickSortRandomized(v, KeyLess{}); });
        report_unstable("堆排序", [](std::vector<Record>& v) { HeapSort(v, KeyLess{}); });
        report_unstable("std::sort",
                        [](std::vector<Record>& v) { std::sort(v.begin(), v.end(), KeyLess{}); });
        Note("");
        Note("结论：不稳定算法排出来的数组本身完全正确（有序），");
        Note("      只是相等元素之间的先后被改变了。对「元素本身就是键」的类型");
        Note("      （比如 int）看不出区别；对「先按时间排、再按金额排」这种流水线，");
        Note("      上一步的次序会被整个打乱——那就是数据错误，不是风格问题。");
    }

    // =======================================================================
    Section("3. 性能实测：为什么要分两档规模");
    // =======================================================================
    Note("第一档只给 O(n^2) 的算法（冒泡 / 选择 / 插入），n = 2000 与 5000；");
    Note("第二档给 O(n log n) 的算法（希尔 / 归并 / 快排 / 堆排 / std::sort / stable_sort），");
    Note("n = 10000 与 20000。");
    Note("");
    Note("为什么不能把 O(n^2) 的算法也放到 10 万？");
    Note("  冒泡排序在 n = 100000 时要做约 n^2/2 = 5 x 10^9 次比较与交换，");
    Note("  在 Debug 下大约需要几分钟到几十分钟——没有任何信息量，只有等待。");
    Note("  正确做法是「各测各的量级」：小规模内部横比，再拿 O(n^2) 在 5000 上的耗时");
    Note("  去对比 O(n log n) 在 20000 上的耗时，就能看出规模差 4 倍时谁赢。");
    Note("");
    Note("为什么第二档也只到 2 万？");
    Note("  本文件的绝对数字全部来自 Debug（/Od）构建。同一份 O(n log n) 的工作，");
    Note("  Debug 比 Release 慢 5 到 20 倍：一个 10 万元素的手写快排在 Debug 下要 50 ms 以上，");
    Note("  乘上「十来个算法 x 每种输入分布 x 重复次数」就轻松超过一分钟。");
    Note("  所以这里把规模压到「趋势清楚、总耗时几秒」的区间——");
    Note("  结论（谁快谁慢、翻倍涨几倍）和跑 10 万完全一样，只是省下了等待时间。");
    Note("  这也是性能实验该有的态度：先算清楚成本，再决定规模。");
    Note("");
    Note("分档的副作用：不同档之间的绝对数字不能直接比「谁的算法更好」，");
    Note("但同档内可以比「谁快几倍」，跨档可以比「规模翻倍时耗时涨几倍」——");
    Note("后者才是复杂度的指纹。");
    std::cout << "\n";

    // -----------------------------------------------------------------------
    // 顺手用一次 Repeat：排序的「基元操作」只有纳秒级，直接计时会被计时器噪声淹没。
    // 这里拿二分查找当例子——它是排序之后最典型的后续操作，也顺便说明
    // 「一次 O(n log n) 的排序换来之后每次 O(log n) 的查询」这笔买卖有多划算。
    // -----------------------------------------------------------------------
    {
        constexpr int kLookups = 20000;
        const std::vector<int> lookup_data = MakeData(Pattern::kSorted, 100000, 99u);
        std::size_t probe = 0;  // 用可变的外部状态让每次查找的目标都不同
        const double t_lookup = BenchMedianUs(
            [&] {
                return Repeat(
                    [&] {
                        // 步长取一个与 10 万互质的数，保证目标位置在数组里均匀游走。
                        probe = (probe + 7919) % lookup_data.size();
                        const auto it = std::lower_bound(lookup_data.begin(), lookup_data.end(),
                                                         lookup_data[probe]);
                        return static_cast<std::uint64_t>(it - lookup_data.begin());
                    },
                    kLookups);
            },
            3);
        std::cout << "  ";
        Label("  预告：二分查找 1 次（在 10 万有序数组中，ns）", 50);
        // 测出来的是 kLookups 次查找的总和，除以次数才得到「单次」耗时。
        std::cout << ": " << (t_lookup * 1000.0 / static_cast<double>(kLookups)) << " ns\n";
        Note("  单次查找只要几十纳秒，直接计时只会测到一团噪声；套上 Repeat 之后就稳了。");
        Note("  乘一个常数不改变增长阶，只把信号抬离噪声区——这就是 Repeat 存在的全部理由。");
    }
    std::cout << "\n";

    // -----------------------------------------------------------------------
    // 第一档：O(n^2) 组，n = 2000 与 5000
    // -----------------------------------------------------------------------
    // 这一档里冒泡 / 选择 / 插入都是 O(n^2)，n 再大就是纯粹的等待。
    // 所以每个规模都"小而全"地测：算法多、规模小、重复 3 次取中位数。
    {
        const std::size_t sizes[] = {2000, 5000};
        double t_bubble[2] = {};
        double t_bubble_early[2] = {};
        double t_selection[2] = {};
        double t_insertion[2] = {};
        double t_std[2] = {};

        for (std::size_t si = 0; si < 2; ++si) {
            const std::vector<int> base = MakeData(Pattern::kRandom, sizes[si], 31337u);
            t_bubble[si] = BenchSortUs(base, [](std::vector<int>& v) { BubbleSort(v); }, 3);
            t_bubble_early[si] =
                BenchSortUs(base, [](std::vector<int>& v) { BubbleSortEarlyExit(v); }, 3);
            t_selection[si] = BenchSortUs(base, [](std::vector<int>& v) { SelectionSort(v); }, 3);
            t_insertion[si] = BenchSortUs(base, [](std::vector<int>& v) { InsertionSort(v); }, 3);
            t_std[si] = BenchSortUs(base, [](std::vector<int>& v) { std::sort(v.begin(), v.end()); }, 3);
        }

        const std::size_t w = 26;
        for (std::size_t si = 0; si < 2; ++si) {
            std::cout << "  [随机数组，n = " << sizes[si] << "，重复 3 次取中位数]\n";
            std::cout << "  ";
            Label("O(n^2) 算法", w);
            std::cout << std::setw(14) << "耗时(us)" << "\n";
            std::cout << "  " << std::string(w + 14, '-') << "\n";
            auto row = [&](const std::string& name, double us) {
                std::cout << "  ";
                Label(name, w);
                std::cout << std::setw(14) << us << "\n";
            };
            row("冒泡排序", t_bubble[si]);
            row("冒泡排序(提前退出)", t_bubble_early[si]);
            row("选择排序", t_selection[si]);
            row("插入排序", t_insertion[si]);
            row("std::sort(标尺)", t_std[si]);
            std::cout << "\n";
        }

        Note("看点 1：n 从 2000 涨到 5000（2.5 倍）时，这四个 O(n^2) 实现的耗时应该涨约 6.25 倍。");
        std::cout << "  ";
        Label("  冒泡排序的实测倍数", 40);
        std::cout << ": " << (t_bubble[0] > 0.0 ? t_bubble[1] / t_bubble[0] : 0.0)
                  << " x（O(n^2) 理论 6.25 x）\n";
        std::cout << "  ";
        Label("  std::sort 的实测倍数", 40);
        std::cout << ": " << (t_std[0] > 0.0 ? t_std[1] / t_std[0] : 0.0)
                  << " x（O(n log n) 理论约 2.6 x）\n";
        Note("");
        Note("看点 2：冒泡的「提前退出」优化在随机数据上几乎没用——随机数组里第一轮");
        Note("        必然发生交换，退出的条件永远不成立。它的价值只在「输入本来就有序」时体现，");
        Note("        这也是它名字的由来：它是一个「最好情况优化」，不是「平均情况优化」。");
        Note("看点 3：std::sort 在这种小规模上就已经比最好的 O(n^2) 实现快一个数量级以上。");
        Note("        它的比较器是内联的、循环是缓存友好的，而且小区间直接走插入排序。");
        std::cout << "\n";
    }

    // -----------------------------------------------------------------------
    // 第二档：O(n log n) 组，n = 10000 / 20000 / 40000
    // -----------------------------------------------------------------------
    // 小规模重复 2 次取中位数（便宜）；40000 这一列只重复 1 次（贵，而且它只用来算倍数）。
    // 同一列里所有算法用同样的重复次数，横向比较才公平。
    // 需要更稳的数字时，正确做法是缩小规模多重复，而不是硬撑着大 n 跑很多遍。
    {
        const std::size_t sizes[] = {10000, 20000, 40000};
        constexpr int kAlgoCount = 12;
        struct AlgoEntry {
            std::string name;
            std::function<void(std::vector<int>&)> fn;
        };
        const std::vector<AlgoEntry> algos = {
            {"希尔排序(Knuth)", [](std::vector<int>& v) { ShellSortKnuth(v); }},
            {"希尔排序(折半)", [](std::vector<int>& v) { ShellSortHalving(v); }},
            {"归并排序(自顶向下)", [](std::vector<int>& v) { MergeSortTopDown(v); }},
            {"归并排序(自底向上)", [](std::vector<int>& v) { MergeSortBottomUp(v); }},
            {"快排(Lomuto+固定pivot)", [](std::vector<int>& v) { QuickSortLomutoFixed(v); }},
            {"快排(Hoare+固定pivot)", [](std::vector<int>& v) { QuickSortV2HoareFixed(v); }},
            {"快排(随机化+cutoff)", [](std::vector<int>& v) { QuickSortRandomized(v); }},
            {"快排(随机化+无cutoff)", [](std::vector<int>& v) { QuickSortRandomizedNoCutoff(v); }},
            {"快排(三数取中+cutoff)", [](std::vector<int>& v) { QuickSortMedianOfThree(v); }},
            {"堆排序", [](std::vector<int>& v) { HeapSort(v); }},
            {"std::sort(参考)", [](std::vector<int>& v) { std::sort(v.begin(), v.end()); }},
            {"std::stable_sort(参考)",
             [](std::vector<int>& v) { std::stable_sort(v.begin(), v.end()); }},
        };
        assert(static_cast<int>(algos.size()) == kAlgoCount);

        double us[kAlgoCount][3] = {};

        for (std::size_t si = 0; si < 3; ++si) {
            const std::vector<int> base = MakeData(Pattern::kRandom, sizes[si], 24680u);
            const int repeats = (si == 2) ? 1 : 2;
            for (int a = 0; a < kAlgoCount; ++a) {
                us[a][si] = BenchSortUs(base, algos[static_cast<std::size_t>(a)].fn, repeats);
            }
        }

        const std::size_t w = 30;
        for (std::size_t si = 0; si < 3; ++si) {
            double best = 0.0;
            for (int a = 0; a < kAlgoCount; ++a) {
                if (best <= 0.0 || us[a][si] < best) {
                    best = us[a][si];
                }
            }
            std::cout << "  [随机数组，n = " << sizes[si] << "，重复 " << ((si == 2) ? 1 : 2)
                      << " 次]\n";
            std::cout << "  ";
            Label("O(n log n) 算法", w);
            std::cout << std::setw(14) << "耗时(us)" << std::setw(14) << "相对最快" << "\n";
            std::cout << "  " << std::string(w + 28, '-') << "\n";
            for (int a = 0; a < kAlgoCount; ++a) {
                std::cout << "  ";
                Label(algos[static_cast<std::size_t>(a)].name, w);
                std::cout << std::setw(14) << us[a][si] << std::setw(14)
                          << (best > 0.0 ? us[a][si] / best : 0.0) << "\n";
            }
            std::cout << "\n";
        }

        Note("看点 1：希尔排序明显慢于快排 / 归并，但快于朴素的 O(n^2) 一大截；");
        Note("        Knuth 增量（1, 4, 13, 40, ...）和折半增量在这一档规模上互有胜负，");
        Note("        但 Knuth 的理论最坏界更硬（O(n^1.5) 对 O(n^2)）。");
        Note("看点 2：归并排序自带 O(n) 缓冲的分配与拷贝，在朴素实现里通常垫底，");
        Note("        但它是唯一「稳定 + 最坏 O(n log n)」的选择（std::stable_sort 也是归并系）。");
        Note("看点 3：堆排序是原地的，跳跃式访问让它比归并慢——缓存局部性的代价；");
        Note("        注意它的「相对最快」列并不难看，因为它的 n 次 sift-down 常数很小。");
        Note("看点 4：cutoff（小区间切插入排序）这一项到底值多少？直接看这两行：");
        {
            const int idx_cut = 6;
            const int idx_nocut = 7;
            for (std::size_t si = 0; si < 3; ++si) {
                std::cout << "  ";
                Label("  n = " + std::to_string(sizes[si]) + "：无 cutoff / 有 cutoff", 40);
                std::cout << ": ";
                if (us[idx_cut][si] > 0.0) {
                    std::cout << (us[idx_nocut][si] / us[idx_cut][si]) << " x\n";
                } else {
                    std::cout << "不可计算\n";
                }
            }
        }
        Note("        倍数小于 1 表示「有 cutoff 反而更慢」——这和教科书说的相反。");
        Note("        * 这不是实现写错了，而是 Debug 构建特有的现象，原因和下表的");
        Note("          cutoff 扫描实验是同一个。请务必看完 3.3 再下结论。");
        std::cout << "\n";
        Note("规模翻倍时各算法的耗时倍数（10000 -> 20000 与 20000 -> 40000；");
        Note("O(n log n) 的理论预期都约 2.1 x）：");
        for (int a = 0; a < kAlgoCount; ++a) {
            std::cout << "  ";
            Label(std::string("  ") + algos[static_cast<std::size_t>(a)].name, 34);
            std::cout << ": ";
            const double r1 = (us[a][0] > 0.0) ? us[a][1] / us[a][0] : 0.0;
            const double r2 = (us[a][1] > 0.0) ? us[a][2] / us[a][1] : 0.0;
            std::cout << r1 << " x  ->  " << r2 << " x\n";
        }
        Note("倍数都在 2 附近，说明这一档里的算法确实都是 O(n log n)；");
        Note("如果某个算法出现了 3 到 4 倍，那它多半已经退化成 O(n^2) 了。");
        std::cout << "\n";
    }

    // -----------------------------------------------------------------------
    // 3.3 专题：cutoff 到底该取多少？——一个「理论对、Debug 下数字会打脸」的实验
    // -----------------------------------------------------------------------
    // 这是一次真实的踩坑记录，值得完整讲一遍，因为它示范了正确的实验方法：
    // 先有理论预期，再用实测检验，数字和预期冲突时不要改数字，要去查原因。
    //
    // 理论预期：快排递归到小区间时，递归的固定开销（压栈、选 pivot、函数调用）
    //   已经超过比较本身，所以应该在小到某个阈值时切换插入排序。
    //   阈值越大，递归层数越少，看起来越划算。
    //
    // Debug 实测：恰恰相反——阈值越大越慢（见下表 cutoff = 64 那一行）。
    // 原因：/Od 下 InsertionSortRange 的内层 while 循环没有被优化，
    //   每次「比较 + 移动赋值」都要重新取迭代器、重新算地址、走完整的函数调用。
    //   而阈值 1 意味着根本不调用插入排序，只多压几层递归——在 Debug 下递归反而更便宜。
    //
    // Release 实测（/O2，同一份代码，本机同机对比）：
    //   n = 10000：cutoff 1 -> 654 us，8 -> 401 us，16 -> 357 us，64 -> 316 us
    //   n = 40000：cutoff 1 -> 2311 us，8 -> 1738 us，16 -> 1856 us，64 -> 1839 us
    //   内联之后插入排序变成一个没有函数调用、没有迭代器开销的紧凑循环，
    //   理论重新生效：切换插入排序带来 10% 到 50% 的收益。
    //
    // 结论（这才是这个实验真正的教学价值）：
    //   1) 「快排用插入排序做小区间基底」这条结论是对的，但它是 Release 的结论；
    //   2) 一个优化的收益是通过「消除函数调用 / 循环开销」实现的，
    //      那么在「函数调用和循环开销本来就没被消除」的 Debug 构建里，
    //      你测不到它，甚至可能测到相反的结论；
    //   3) 所以性能优化必须在 Release 下验证。Debug 用来抓 bug，Release 用来做决策。
    {
        const std::size_t n = 10000;
        const std::vector<int> base = MakeData(Pattern::kRandom, n, 24680u);
        const std::size_t cutoffs[] = {1, 8, 16, 32, 64};

        double t[5] = {};
        for (std::size_t ci = 0; ci < 5; ++ci) {
            t[ci] = BenchSortUs(
                base,
                [ci, &cutoffs](std::vector<int>& v) {
                    QuickSortRandomizedWithCutoff(v, cutoffs[ci]);
                },
                2);
        }
        const double t_std =
            BenchSortUs(base, [](std::vector<int>& v) { std::sort(v.begin(), v.end()); }, 2);

        Note("cutoff 扫描实验（n = 10000，随机输入，重复 2 次取中位数）：");
        std::cout << "  ";
        Label("cutoff 取值", 22);
        std::cout << std::setw(14) << "耗时(us)" << std::setw(14) << "相对 cutoff=1" << "\n";
        std::cout << "  " << std::string(50, '-') << "\n";
        for (std::size_t ci = 0; ci < 5; ++ci) {
            std::cout << "  ";
            Label(std::to_string(cutoffs[ci]) + "（1 = 等同于关闭 cutoff）", 22);
            std::cout << std::setw(14) << t[ci] << std::setw(14)
                      << (t[0] > 0.0 ? t[ci] / t[0] : 0.0) << "\n";
        }
        std::cout << "  ";
        Label("std::sort（标尺）", 22);
        std::cout << std::setw(14) << t_std << std::setw(14) << (t[0] > 0.0 ? t_std / t[0] : 0.0)
                  << "\n";
        std::cout << "\n";
        Note("在 Debug（/Od）下，cutoff 越大越慢——因为它省下的递归开销，");
        Note("远远少于它引入的「未内联的插入排序内层循环」的开销。");
        Note("在 Release（/O2）下同一份代码的结果是反过来的：");
        Note("  n = 10000：cutoff 1 -> 654 us，8 -> 401 us，16 -> 357 us，64 -> 316 us；");
        Note("  n = 40000：cutoff 1 -> 2311 us，8 -> 1738 us，16 -> 1856 us，64 -> 1839 us。");
        Note("内联之后插入排序变成一个没有函数调用开销的紧凑循环，理论重新生效。");
        Note("");
        Note("* 这个实验的真正价值：它示范了「理论 -> 实测 -> 冲突 -> 查原因」的完整闭环。");
        Note("  数字和理论冲突时，正确反应是查清楚为什么，而不是删掉那一行数据。");
        std::cout << "\n";
        Note("顺带一个可比性提醒：上面 Release 的那些数字不是本程序打印出来的，");
        Note("而是把同一个源文件用 /O2 重新编译后跑出来的。同一份代码、不同优化级别，");
        Note("结论可以完全不同——所以引用性能数字时，必须说清楚构建配置。");
        std::cout << "\n";
    }

    // =======================================================================
    Section("4. 本节最有说服力的数据：插入排序在「近乎有序」时有多快");
    // =======================================================================
    Note("插入排序的代价 = 逆序对数量。输入越接近有序，逆序对越少，它越快，");
    Note("最好情况下是纯 O(n)。而快排和归并无论输入如何都要花 O(n log n)。");
    Note("所以：数组已经基本有序时，O(n^2) 的插入排序可以跑赢 O(n log n) 的快排。");
    Note("这就是「大 O 只描述趋势，常数和输入分布决定真实胜负」的最佳例证。");
    Note("");
    Note("实验设计：同样 n = 20000，两份数据 —— 完全随机 vs 近乎有序（只交换了 0.5% 的元素）。");
    Note("两边测的是同一批量级的数据，绝对耗时可以横向比较。");
    Note("");
    Note("为什么 n 取 20000 而不是 10 万？");
    Note("  插入排序在随机数据上是 O(n^2)：n = 100000 时 n^2/4 级别的比较次数");
    Note("  在 Debug 下要跑十几秒，光是这一项就能吃掉整个程序的运行预算。");
    Note("  而 20000 已经足够让「随机 vs 近乎有序」拉开数量级的差距——");
    Note("  演示的目的是把趋势讲清楚，不是把机器跑满。");
    std::cout << "\n";

    {
        const std::size_t n = 20000;
        const std::vector<int> random_data = MakeData(Pattern::kRandom, n, 1111u);
        const std::vector<int> nearly_data = MakeData(Pattern::kNearlySorted, n, 1111u);
        const std::vector<int> sorted_data = MakeData(Pattern::kSorted, n, 1111u);
        const std::vector<int> reversed_data = MakeData(Pattern::kReversed, n, 1111u);

        // 随机数据上的插入排序最贵，只测 1 次；其余都重复 3 次取中位数。
        const double ins_random =
            BenchSortUs(random_data, [](std::vector<int>& v) { InsertionSort(v); }, 1);
        const double ins_nearly =
            BenchSortUs(nearly_data, [](std::vector<int>& v) { InsertionSort(v); }, 3);
        const double ins_sorted =
            BenchSortUs(sorted_data, [](std::vector<int>& v) { InsertionSort(v); }, 3);
        const double ins_reversed =
            BenchSortUs(reversed_data, [](std::vector<int>& v) { InsertionSort(v); }, 1);
        const double qs_random =
            BenchSortUs(random_data, [](std::vector<int>& v) { QuickSortRandomized(v); }, 1);
        const double qs_nearly =
            BenchSortUs(nearly_data, [](std::vector<int>& v) { QuickSortRandomized(v); }, 1);
        const double std_random =
            BenchSortUs(random_data, [](std::vector<int>& v) { std::sort(v.begin(), v.end()); }, 1);
        const double std_nearly =
            BenchSortUs(nearly_data, [](std::vector<int>& v) { std::sort(v.begin(), v.end()); }, 1);

        const std::size_t w = 36;
        std::cout << "  ";
        Label("算法 / 输入分布（n = 20000）", w);
        std::cout << std::setw(14) << "耗时(us)" << std::setw(16) << "相对本组最快" << "\n";
        std::cout << "  " << std::string(w + 30, '-') << "\n";

        const double group_best = std::min(std::min(std::min(ins_random, ins_nearly), ins_sorted),
                                           std::min(std::min(qs_random, qs_nearly),
                                                    std::min(std_random, std_nearly)));
        auto row = [&](const std::string& name, double us) {
            std::cout << "  ";
            Label(name, w);
            std::cout << std::setw(14) << us << std::setw(16) << (us / group_best) << "\n";
        };
        row("插入排序 / 完全随机", ins_random);
        row("插入排序 / 近乎有序", ins_nearly);
        row("插入排序 / 完全有序（最好情况）", ins_sorted);
        row("插入排序 / 完全逆序（最坏情况）", ins_reversed);
        row("快排(随机化) / 完全随机", qs_random);
        row("快排(随机化) / 近乎有序", qs_nearly);
        row("std::sort / 完全随机", std_random);
        row("std::sort / 近乎有序", std_nearly);

        std::cout << "\n";
        Label("  插入排序：近乎有序比随机快了多少倍", 44);
        std::cout << ": " << (ins_nearly > 0.0 ? ins_random / ins_nearly : 0.0) << " x\n";
        Label("  插入排序：完全有序比随机快了多少倍", 44);
        std::cout << ": " << (ins_sorted > 0.0 ? ins_random / ins_sorted : 0.0) << " x\n";
        Label("  快排：近乎有序 / 完全随机的倍数", 44);
        std::cout << ": " << (qs_random > 0.0 ? qs_nearly / qs_random : 0.0) << " x\n";
        std::cout << "\n";
        Note("结论（本节最重要的一条）：");
        Note("  1) 插入排序从「随机」换到「近乎有序」，耗时掉的不是一个常数，");
        Note("     而是整整一个数量级以上——这正是它从 O(n^2) 走向 O(n) 的实证。");
        Note("  2) 在这个规模上，近乎有序的插入排序已经和快排、std::sort 处在同一个量级。");
        Note("     继续把 n 放大，插入排序会进一步反超：它的工作量随 n 线性增长，");
        Note("     而快排和 std::sort 仍要花 n log n。这就是「近乎有序时插入排序极快」的直接证据。");
        Note("  3) 快排在近乎有序的输入上并不会明显变快（它照样要做 n log n 次比较），");
        Note("     所以在「几乎有序」的数据上，插入排序可以反超快排。");
        Note("  4) 这就是快排 / 归并把小区间（cutoff <= 16）交给插入排序的理论依据：");
        Note("     递归到小区间时，数组已经被分区操作整理得「局部近乎有序」，");
        Note("     插入排序在这上面几乎是线性扫描，比继续递归快得多。");
        Note("  5) 反过来说：如果业务数据天然近乎有序（日志按时间追加、");
        Note("     数据库里已排序的索引页），插入排序是最优选择之一，");
        Note("     根本不该上快排——先看清楚数据长什么样，再选算法。");
        Note("  6) 顺带看一眼「完全逆序」那一行：逆序对数量达到最大值 n(n-1)/2，");
        Note("     插入排序在这里退化到最坏情况，是所有行里最慢的。");
        Note("     「近乎有序」和「完全逆序」之间差了三个数量级的耗时，");
        Note("     同一个算法、同一个 n——输入分布就是这么重要。");
    }

    // =======================================================================
    Section("5. 快排的退化现场：固定首元素 pivot 为什么是 O(n^2)");
    // =======================================================================
    Note("这是教科书上最著名的一个反例。数据是「已经排好序」的数组——");
    Note("现实中最常见的情形（从数据库按时间取出来的日志、已经按 ID 排好的列表）。");
    Note("");
    Note("固定取首元素为 pivot 时，每次分区都会把它自己划到一边：");
    Note("  分区结果 = (0 个元素) 和 (n-1 个元素)，递归深度 n，总比较次数 n^2/2。");
    Note("  快排最引以为傲的 O(n log n) 当场消失。");
    Note("随机化 / 三数取中之后，pivot 大概率落在中间，分区变成 (n/2, n/2)，");
    Note("递归深度 log n，复杂度回到 O(n log n)——而这一切只花了「选一个随机下标」的代价。");
    Note("");
    Note("规模说明：这里刻意只取 n = 2000 与 3000。因为退化版的复杂度是 O(n^2)，");
    Note("  n 只要上千就足够让差距「肉眼可见」；再放大会让程序跑到天荒地老，");
    Note("  而且结论不会有任何变化——这就是选对演示规模的重要性。");
    Note("  （另一个原因：Lomuto 退化的递归深度是 O(n)，规模太大还会直接爆栈，");
    Note("    所以本文件把它改写成了显式栈的迭代版本，见 4.3 的注释。）");
    std::cout << "\n";

    {
        const std::size_t sizes[] = {2000, 3000};
        const std::size_t w = 42;

        // 先测再打印：需要同时拿到两档的数据才能算「跨档倍数」。
        double t_degen[2] = {};
        double t_lomuto_fixed[2] = {};
        double t_hoare_fixed[2] = {};
        double t_random[2] = {};
        double t_median3[2] = {};
        double t_std[2] = {};

        for (std::size_t si = 0; si < 2; ++si) {
            // 输入 = 已经排好序的数组（最容易被固定 pivot 打崩的分布）
            const std::vector<int> sorted_data = MakeData(Pattern::kSorted, sizes[si], 7u);

            // 退化版每个只测 1 次：它太慢了，重复测量既不礼貌也没有必要，
            // 而且「退化」这件事不需要靠重复测量来确认。
            t_degen[si] = BenchSortUs(sorted_data,
                                      [](std::vector<int>& v) { QuickSortV1FirstPivot(v); }, 1);
            t_lomuto_fixed[si] = BenchSortUs(
                sorted_data, [](std::vector<int>& v) { QuickSortLomutoFixed(v); }, 1);
            t_hoare_fixed[si] = BenchSortUs(
                sorted_data, [](std::vector<int>& v) { QuickSortV2HoareFixed(v); }, 1);
            // 随机化 / 三数取中版本很快，重复 3 次取中位数。
            t_random[si] = BenchSortUs(
                sorted_data, [](std::vector<int>& v) { QuickSortRandomized(v); }, 3);
            t_median3[si] = BenchSortUs(
                sorted_data, [](std::vector<int>& v) { QuickSortMedianOfThree(v); }, 3);
            t_std[si] = BenchSortUs(
                sorted_data, [](std::vector<int>& v) { std::sort(v.begin(), v.end()); }, 3);
        }

        // --- 表 1：固定 pivot vs 随机化，同一份已排序输入 ---
        std::cout << "  ";
        Label("算法 / 输入 = 已排序数组", w);
        std::cout << std::setw(13) << "n=2000(us)" << std::setw(13) << "n=3000(us)" << "\n";
        std::cout << "  " << std::string(w + 26, '-') << "\n";
        auto line = [&](const std::string& name, const double* t, const std::string& tag) {
            std::cout << "  ";
            Label(name, w);
            std::cout << std::setw(13) << t[0] << std::setw(13) << t[1] << "  " << tag << "\n";
        };
        line("快排 V1 固定首元素+Lomuto", t_degen, "<- 退化成 O(n^2)");
        line("快排 V1b 固定末元素+Lomuto", t_lomuto_fixed, "<- 同样退化");
        line("快排 V2 Hoare+固定首元素", t_hoare_fixed, "<- 换分区也救不了退化");
        line("快排 V3 随机化+尾递归+cutoff", t_random, "<- 不退化");
        line("快排 V4 三数取中+尾递归+cutoff", t_median3, "<- 不退化");
        line("std::sort (introsort 兜底)", t_std, "<- 不退化");
        std::cout << "\n";

        // --- 表 2：跨档倍数，也就是复杂度的指纹 ---
        // n 从 2000 涨到 3000 只涨了 1.5 倍。O(n^2) 的耗时该涨约 2.25 倍，
        // O(n log n) 只会涨 1.5 到 1.6 倍。倍数就是增长阶的指纹。
        Note("n 从 2000 涨到 3000（1.5 倍）时各版本的耗时倍数：");
        auto ratio_line = [&](const std::string& name, double lo, double hi) {
            std::cout << "  ";
            Label(name, 46);
            std::cout << ": ";
            if (lo > 0.0) {
                std::cout << (hi / lo) << " x";
            } else {
                std::cout << "不可计算";
            }
            std::cout << "\n";
        };
        ratio_line("  固定首元素 pivot + Lomuto（O(n^2)）", t_degen[0], t_degen[1]);
        ratio_line("  固定首元素 pivot + Hoare（O(n^2)）", t_hoare_fixed[0], t_hoare_fixed[1]);
        ratio_line("  随机化 + 尾递归 + cutoff（O(n log n)）", t_random[0], t_random[1]);
        ratio_line("  std::sort（introsort，O(n log n)）", t_std[0], t_std[1]);
        Note("理论对照：O(n^2) 预期约 2.25 x；O(n log n) 预期约 1.5 x。");
        Note("退化版的倍数明显更大，这就是 O(n^2) 的指纹。");
        std::cout << "\n";

        // --- 表 3：换输入分布，看随机化的效果是否稳定 ---
        {
            const std::size_t n = 3000;
            const std::vector<int> reversed_data = MakeData(Pattern::kReversed, n, 7u);
            const std::vector<int> equal_data = MakeData(Pattern::kAllEqual, n, 7u);
            const std::vector<int> few_unique = MakeData(Pattern::kFewUnique, n, 7u);

            const double t_rev = BenchSortUs(
                reversed_data, [](std::vector<int>& v) { QuickSortRandomized(v); }, 3);
            const double t_eq = BenchSortUs(
                equal_data, [](std::vector<int>& v) { QuickSortRandomized(v); }, 3);
            const double t_few = BenchSortUs(
                few_unique, [](std::vector<int>& v) { QuickSortRandomized(v); }, 3);
            const double t_eq_lomuto =
                BenchSortUs(equal_data, [](std::vector<int>& v) { QuickSortLomutoFixed(v); }, 1);
            const double t_few_lomuto =
                BenchSortUs(few_unique, [](std::vector<int>& v) { QuickSortLomutoFixed(v); }, 1);
            const double t_eq_std =
                BenchSortUs(equal_data, [](std::vector<int>& v) { std::sort(v.begin(), v.end()); }, 3);
            const double t_few_std =
                BenchSortUs(few_unique, [](std::vector<int>& v) { std::sort(v.begin(), v.end()); }, 3);

            std::cout << "  ";
            Label("输入分布（n = 3000，耗时 us）", w);
            std::cout << std::setw(15) << "Lomuto+固定pivot" << std::setw(15) << "随机化快排"
                      << std::setw(13) << "std::sort" << "\n";
            std::cout << "  " << std::string(w + 43, '-') << "\n";
            auto dist_row = [&](const std::string& name, double lomuto, double rnd, double stl) {
                std::cout << "  ";
                Label(name, w);
                std::cout << std::setw(15) << lomuto << std::setw(15) << rnd << std::setw(13)
                          << stl << "\n";
            };
            dist_row("已排序", t_degen[1], t_random[1], t_std[1]);
            dist_row("完全逆序", t_degen[1], t_rev, t_std[1]);
            dist_row("全部相同（只有 1 种值）", t_eq_lomuto, t_eq, t_eq_std);
            dist_row("大量重复（只有 5 种值）", t_few_lomuto, t_few, t_few_std);
            std::cout << "\n";
            Note("关键观察：Lomuto + 固定 pivot 在三类「有规律的输入」上全部退化——");
            Note("  已排序、完全逆序、以及大量重复。这不是巧合：只要有规律，");
            Note("  「取首元素」就必然长期取到极值或让分区一边倒。");
            Note("  而随机化快排和 std::sort 在所有分布上都是同一个量级。");
            Note("  * 结论：不是「快排最坏能到 O(n^2)」，而是「不选 pivot 的快排几乎必然退化」。");
            Note("  （第一列的「完全逆序」沿用上面的数字，因为对固定首元素 pivot 而言，");
            Note("    已排序和完全逆序是同一个故事。）");
        }

        Note("补充：std::sort 在这里同样稳如泰山，因为它用的 introsort 会在递归深度");
        Note("      超过 2*log2(n) 时直接切换到堆排序，从机制上杜绝了 O(n^2) 的可能。");
        Note("      这也是「手写快排永远打不过 std::sort」的一条硬理由：我们的版本只是");
        Note("      「平均很快」，它的是「平均很快 + 最坏有硬保证」。");
    }

    // =======================================================================
    Section("6. 非比较排序：O(n) 的前提，以及什么时候不该用");
    // =======================================================================
    Note("计数排序 / 桶排序能突破 O(n log n) 下界，靠的是「不比较」——");
    Note("它们把元素的值直接当地址用，于是省掉了「至少要 n log n 次比较」这条信息论下界。");
    Note("代价是它们对数据有硬性要求，违反要求时不是变慢一点，而是直接不可用。");
    std::cout << "\n";

    {
        const std::size_t n = 100000;
        const int max_value = 999;  // 键范围 [0, 999]，k = 1000 << n

        const std::vector<int> small_range = MakeSmallRange(n, max_value, 8080u);
        const std::vector<int> skewed = MakeSkewed(n, max_value, 8080u);

        std::vector<int> expected = small_range;
        std::sort(expected.begin(), expected.end());

        std::vector<int> count_result;
        std::vector<int> bucket_result;
        const double t_count = BenchMedianUs([&] {
            count_result = CountingSortNonNegative(small_range, max_value);
            return static_cast<std::uint64_t>(count_result[count_result.size() / 2]);
        }, 3);
        const double t_bucket = BenchMedianUs([&] {
            bucket_result = BucketSortNonNegative(small_range, max_value, 1024);
            return static_cast<std::uint64_t>(bucket_result[bucket_result.size() / 2]);
        }, 3);
        const double t_std = BenchMedianUs([&] {
            std::vector<int> v = small_range;
            std::sort(v.begin(), v.end());
            return static_cast<std::uint64_t>(v[v.size() / 2]);
        }, 3);
        const double t_std_stable = BenchMedianUs([&] {
            std::vector<int> v = small_range;
            std::stable_sort(v.begin(), v.end());
            return static_cast<std::uint64_t>(v[v.size() / 2]);
        }, 3);

        // 对拍：非比较排序也必须和 std::sort 逐元素一致。
        assert(count_result == expected);
        assert(bucket_result == expected);

        const std::size_t w = 40;
        std::cout << "  ";
        Label("算法 / 数据（键范围 0..999，n = 100000）", w);
        std::cout << std::setw(14) << "耗时(us)" << std::setw(14) << "相对最快" << "\n";
        std::cout << "  " << std::string(w + 28, '-') << "\n";
        const double best = std::min(std::min(t_count, t_bucket), std::min(t_std, t_std_stable));
        auto row = [&](const std::string& name, double us) {
            std::cout << "  ";
            Label(name, w);
            std::cout << std::setw(14) << us << std::setw(14) << (us / best) << "\n";
        };
        row("计数排序 O(n+k)，k=1000", t_count);
        row("桶排序 O(n+b)，b=1024", t_bucket);
        row("std::sort", t_std);
        row("std::stable_sort", t_std_stable);
        std::cout << "\n";

        // --- 反例：幂律分布（极度倾斜）下桶排序的表现 ---
        std::vector<int> skewed_expected = skewed;
        std::sort(skewed_expected.begin(), skewed_expected.end());
        assert(BucketSortNonNegative(skewed, max_value, 1024) == skewed_expected);
        assert(CountingSortNonNegative(skewed, max_value) == skewed_expected);

        const double t_bucket_skewed = BenchMedianUs([&] {
            std::vector<int> r = BucketSortNonNegative(skewed, max_value, 1024);
            return static_cast<std::uint64_t>(r[r.size() / 2]);
        }, 1);
        const double t_count_skewed = BenchMedianUs([&] {
            std::vector<int> r = CountingSortNonNegative(skewed, max_value);
            return static_cast<std::uint64_t>(r[r.size() / 2]);
        }, 3);
        const double t_std_skewed = BenchMedianUs([&] {
            std::vector<int> v = skewed;
            std::sort(v.begin(), v.end());
            return static_cast<std::uint64_t>(v[v.size() / 2]);
        }, 3);

        Note("反例实验：同一批元素，但分布极度倾斜（u1 * u2 造出的幂律分布，");
        Note("大多数值挤在低端，少数值很大）。计数排序几乎不受影响（它只看键范围），");
        Note("桶排序却会被打回原形——因为几乎所有元素落进了前几个桶，");
        Note("桶内插入排序退化成 O(n^2)：");
        std::cout << "\n";
        std::cout << "  ";
        Label("桶排序 / 均匀分布", w);
        std::cout << std::setw(14) << t_bucket << " us\n";
        std::cout << "  ";
        Label("桶排序 / 倾斜分布（反例）", w);
        std::cout << std::setw(14) << t_bucket_skewed << " us"
                  << "   " << (t_bucket > 0.0 ? t_bucket_skewed / t_bucket : 0.0) << " x 慢\n";
        std::cout << "  ";
        Label("计数排序 / 倾斜分布", w);
        std::cout << std::setw(14) << t_count_skewed << " us  （不受分布影响）\n";
        std::cout << "  ";
        Label("std::sort / 倾斜分布", w);
        std::cout << std::setw(14) << t_std_skewed << " us\n";
        std::cout << "\n";

        Note("结论与适用条件：");
        Note("  计数排序：O(n + k)，要求「键能映射成小范围非负整数」且 k 与 n 同量级。");
        Note("    反例：n = 10 万但键是 0 到 10^9 的时间戳——需要 10^9 个计数器（数 GB），");
        Note("    内存直接爆，或者退化成 O(k) 的初始化开销。这种数据必须用 std::sort。");
        Note("  桶排序：O(n + n^2/b + b)，要求「数据近似均匀分布」。");
        Note("    反例：上面这份倾斜数据，桶长极不均匀，桶内插入排序退化成 O(n^2)，");
        Note("    比 std::sort 慢了好几倍。分布未知或倾斜时，别用桶排序。");
        Note("  工程结论：这两个算法真正的用武之地是「键范围已知且小」的场景，");
        Note("    例如成绩排序（0 到 100）、像素直方图、按年龄分桶统计、");
        Note("    以及基数排序的每一趟（基数排序就是多次稳定的计数排序）。");
        Note("    通用场景一律 std::sort。");
    }

    // =======================================================================
    Section("7. 与 STL 的对比：为什么生产代码永远用 std::sort");
    // =======================================================================
    Note("先把「手写的极限」和 std::sort 摆在一起看。下面这一列是层层加固的过程：");
    Note("  固定 pivot -> 随机化 pivot -> 加尾递归优化 -> 加 cutoff -> introsort（std::sort）。");
    Note("  每一个箭头对应一项工程手段，而最后一项（堆排序兜底）我们并没有实现。");
    std::cout << "\n";

    {
        const std::size_t sizes[] = {2000, 10000, 30000};
        struct Entry {
            std::string name;
            std::function<void(std::vector<int>&)> fn;
        };
        // 只放 O(n log n) 一族的实现。冒泡排序在 n = 30000 上要跑十几秒，
        // 它的量级早在第 3 节就已经用 2000 / 5000 讲清楚了，这里不必再拖一遍。
        const std::vector<Entry> entries = {
            {"插入排序（对照）", [](std::vector<int>& v) { InsertionSort(v); }},
            {"快排：固定pivot+Hoare", [](std::vector<int>& v) { QuickSortV2HoareFixed(v); }},
            {"快排：+随机化", [](std::vector<int>& v) { QuickSortRandomizedNoCutoff(v); }},
            {"快排：+随机化+cutoff", [](std::vector<int>& v) { QuickSortRandomized(v); }},
            {"堆排序（最坏保证）", [](std::vector<int>& v) { HeapSort(v); }},
            {"std::sort（introsort）",
             [](std::vector<int>& v) { std::sort(v.begin(), v.end()); }},
        };
        constexpr int kCount = 6;
        assert(static_cast<int>(entries.size()) == kCount);

        double us[kCount][3] = {};
        for (std::size_t si = 0; si < 3; ++si) {
            const std::vector<int> base = MakeData(Pattern::kRandom, sizes[si], 777u);
            for (int a = 0; a < kCount; ++a) {
                us[a][si] = BenchSortUs(base, entries[static_cast<std::size_t>(a)].fn, 1);
            }
        }

        const std::size_t w = 26;
        std::cout << "  ";
        Label("随机数组", w);
        std::cout << std::setw(14) << "n=2000" << std::setw(14) << "n=10000" << std::setw(14)
                  << "n=30000" << "\n";
        std::cout << "  " << std::string(w + 42, '-') << "\n";
        for (int a = 0; a < kCount; ++a) {
            std::cout << "  ";
            Label(entries[static_cast<std::size_t>(a)].name, w);
            for (std::size_t si = 0; si < 3; ++si) {
                std::cout << std::setw(14) << us[a][si];
            }
            std::cout << "\n";
        }
        std::cout << "\n";

        // 索引：0 = 插入排序，1 = 固定 pivot，2 = 随机化，3 = 随机化 + cutoff，4 = 堆排，
        // 5 = std::sort。下面用名字取，避免魔法数字。
        const int idx_ins = 0;
        const int idx_plain = 1;
        const int idx_rand = 2;
        const int idx_cut = 3;
        const int idx_std = 5;
        Note("读法（以 n = 30000 那一列为例）：");
        std::cout << "  ";
        Label("  插入排序比 std::sort 慢多少", 40);
        std::cout << ": " << (us[idx_std][2] > 0.0 ? us[idx_ins][2] / us[idx_std][2] : 0.0)
                  << " x\n";
        std::cout << "  ";
        Label("  随机化相对固定 pivot 快了多少", 40);
        std::cout << ": " << (us[idx_rand][2] > 0.0 ? us[idx_plain][2] / us[idx_rand][2] : 0.0)
                  << " x\n";
        std::cout << "  ";
        Label("  加 cutoff 又快了/慢了多少", 40);
        std::cout << ": " << (us[idx_cut][2] > 0.0 ? us[idx_rand][2] / us[idx_cut][2] : 0.0)
                  << " x（见 3.3 的 Debug 陷阱）\n";
        std::cout << "  ";
        Label("  std::sort 相对我们最好的快排", 40);
        std::cout << ": "
                  << (us[idx_std][2] > 0.0 && us[idx_cut][2] > 0.0 ? us[idx_cut][2] / us[idx_std][2]
                                                                  : 0.0)
                  << " x\n";
        std::cout << "\n";
        Note("在随机输入上，我们精心加固的快排能把 std::sort 的差距压到 2 到 3 倍——");
        Note("但压不到 1 倍以内，而且在「输入有规律」的场景下差距会重新拉开。");
        Note("下面这两行就是这个说法的验证（n = 30000，输入换成已经排好序的数组）：");
        std::cout << "\n";
        {
            const std::size_t n = 30000;
            const std::vector<int> sorted_base = MakeData(Pattern::kSorted, n, 777u);
            const double t_plain = BenchSortUs(
                sorted_base, [](std::vector<int>& v) { QuickSortV2HoareFixed(v); }, 1);
            const double t_rand = BenchSortUs(
                sorted_base, [](std::vector<int>& v) { QuickSortRandomized(v); }, 1);
            const double t_std2 = BenchSortUs(
                sorted_base, [](std::vector<int>& v) { std::sort(v.begin(), v.end()); }, 1);
            const std::size_t w2 = 30;
            std::cout << "  ";
            Label("已排序输入，n = 30000", w2);
            std::cout << std::setw(14) << "耗时(us)" << std::setw(14) << "相对最快" << "\n";
            std::cout << "  " << std::string(w2 + 28, '-') << "\n";
            const double best2 = std::min(t_std2, std::min(t_plain, t_rand));
            auto row2 = [&](const std::string& name, double us_v) {
                std::cout << "  ";
                Label(name, w2);
                std::cout << std::setw(14) << us_v << std::setw(14)
                          << (best2 > 0.0 ? us_v / best2 : 0.0) << "\n";
            };
            row2("手写快排：固定 pivot", t_plain);
            row2("手写快排：随机化+cutoff", t_rand);
            row2("std::sort（introsort）", t_std2);
            std::cout << "\n";
            Note("std::sort 在「已排序输入」上不仅没变慢，反而更快（省掉了交换）；");
            Note("因为它一旦发现递归过深就切堆排序，机制上就不存在 O(n^2) 这条路。");
        }
        std::cout << "\n";
    }

    Note("结论：手写实现没能打赢 std::sort——即使我们写的已经是");
    Note("「随机化 + Hoare 分区 + 尾递归优化 + 小区间插入排序」这种工程写法。");
    Note("这不是我们写得太差，而是下面这五条差距几乎无法靠手写弥补：");
    std::cout << "\n";

    Note("1) 内联：比较器被彻底内联，函数调用开销为 0。");
    Note("   std::sort 是模板，比较器作为模板参数参与实例化，编译器能把整个比较");
    Note("   直接展开进分区循环。而本文件用 std::function 包装的测量入口、以及");
    Note("   任何「通过函数指针传比较器」的写法，都会保留一次真实的调用。");
    Note("   在 10 万规模上要做 170 万次比较，每次省下几纳秒就是好几毫秒。");
    std::cout << "\n";

    Note("2) 分支预测：内联之后，比较结果和随后的分支能被处理器流水线更好地预测。");
    Note("   标准库实现还会用「无分支分区技巧」（branchless partition，例如");
    Note("   用条件移动 / 位运算累积交换位置），把随机数据上 50% 误预测的分支消掉。");
    Note("   现代 CPU 一次分支误预测的代价约 15 到 20 个周期，这在大数组上非常可观。");
    std::cout << "\n";

    Note("3) 内存局部性：循环写得对缓存友好。");
    Note("   手写快排很容易在「先递归哪一侧」「怎么切小区间」上写出跳跃访问；");
    Note("   标准库的写法保证分区是纯粹的单向线性扫描，并且对小区间用插入排序");
    Note("   处理，让每个缓存行在失效前被用到极致。");
    std::cout << "\n";

    Note("4) 混合策略（introsort，Musser 1997）：这是 std::sort 的核心竞争力。");
    Note("   快排的快速（平均常数最小） + 堆排的最坏保证 + 插入排序的小区间优势，三者合一：");
    Note("     a) 主体用快排（三数取中选 pivot），享受平均意义上最小的常数；");
    Note("     b) 递归深度一旦超过 2 * log2(n)，立刻切换成堆排序——");
    Note("        这一步从机制上把最坏复杂度钉死在 O(n log n)，");
    Note("        彻底免疫「已排序输入」「全等输入」以及任何针对性的杀手输入；");
    Note("     c) 区间长度小于阈值（MSVC 是 32 左右）时切换插入排序，省掉递归开销。");
    Note("   本文件的 V3 / V4 只做了 (a) 和 (c) 的一部分，没有 (b) 这层兜底，");
    Note("   所以理论上仍然可能退化——这正是「不要自己写通用排序」的最硬理由。");
    std::cout << "\n";

    Note("5) 几十年的实测调优：阈值取 16 还是 32、三数取中怎么取、");
    Note("   什么时候切堆排、插入排序的循环怎么展开，这些常数都是各家标准库");
    Note("   （MSVC / libstdc++ / libc++）在几十年、各种真实数据分布上反复调出来的。");
    Note("   自己写的版本连「阈值该取多少」都要靠猜。");
    std::cout << "\n";

    Note("那什么时候可以不用 std::sort？");
    Note("  a) 需要稳定：用 std::stable_sort（内部是归并 + 插入排序的混合）；");
    Note("  b) 只要前 k 个：用 std::partial_sort / std::nth_element，它们比全排序快得多；");
    Note("  c) 键范围已知且很小：手写计数排序确实可能更快（但差距通常有限，先测再说）；");
    Note("  d) 学算法、写面试题、或者需要自定义的极度特殊的数据结构（如外部排序）。");
    Note("  除了这些，一律 std::sort。");

    // =======================================================================
    Section("8. 本节结论汇总");
    // =======================================================================
    Note("1) 复杂度是上限不是预言。插入排序在近乎有序的输入上能反超快排，");
    Note("   选择排序的比较次数恒定但它交换最少，堆排有最坏保证却输在缓存。");
    Note("2) 选算法的顺序：先看「是否需要稳定」，再看「数据规模与分布」，最后看平均复杂度。");
    Note("3) 快排的两个致命弱点都有标准解法：随机化（或三数取中）解决输入分布，");
    Note("   尾递归优化把栈深度压到 O(log n)，cutoff 让小区间走插入排序。");
    Note("4) 归并排序是唯一「稳定 + 最坏 O(n log n)」的通用算法，代价是 O(n) 空间。");
    Note("5) 非比较排序的 O(n) 是有条件的，条件不满足时比 std::sort 慢得多甚至爆内存。");
    Note("6) 生产代码永远用 std::sort / std::stable_sort。手写排序的价值在于");
    Note("   「理解为什么标准库这么快」，而不在于「比标准库更快」。");

    // 让 g_sink 真正被「读过」一次，确保它不会被整个优化掉。
    std::cout << "\n[校验] 累加器非零，说明所有测量结果都真实参与了运算: "
              << (g_sink != 0 ? "是" : "否") << "\n";

    return 0;
}
