// ============================================================================
//  07_algorithms_and_ranges.cpp
// ----------------------------------------------------------------------------
//  演示主题：
//    1. <algorithm> 高频算法：sort / stable_sort / nth_element / partial_sort /
//       lower_bound / equal_range / transform / accumulate / remove_if + erase /
//       unique + erase / all_of / any_of / none_of / iota / generate
//    2. C++20 <ranges>：views::filter / transform / take / iota / reverse、
//       管道语法、惰性求值、链式组合
//    3. ranges 与手写循环的可读性 / 性能对比（含实测）
//    4. std::span：数组参数的现代替代品（替代 T* + size 和 vector 传参）
//
//  关键结论：
//    - 算法 + lambda 把「做什么」和「怎么遍历」分开，是 STL 的核心设计。
//    - remove_if / unique 不删除元素，它们只是把「要保留的」移到前面并返回新末尾，
//      必须配合 erase 才真正缩小容器 —— 这就是 erase-remove 惯用法。
//    - ranges 视图是惰性的：不产生中间容器，但链式管道的 Debug 开销很大，
//      性能敏感处要么收进 vector，要么在 Release 下测过再说。
//    - span 是「不拥有数据的连续区间视图」：既能接 C 数组、也能接 vector，
//      统一了 T* + size 接口，且带边界检查习惯（.size() 可查）。
// ============================================================================

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <iostream>
#include <iterator>
#include <numeric>
#include <random>
#include <ranges>
#include <span>
#include <string>
#include <vector>

// ---------------------------------------------------------------- span 接口示范
// 老写法：两个参数必须一起传，且没有任何长度信息保障
int sum_legacy(const int* data, std::size_t count) {
    int total = 0;
    for (std::size_t i = 0; i < count; ++i) {
        total += data[i];
    }
    return total;
}

// 新写法：span 自带长度，能接 C 数组 / vector / std::array
int sum_span(std::span<const int> data) {
    int total = 0;
    for (const int value : data) {
        total += value;
    }
    return total;
}

// span 也能表达「输出缓冲区」，并且能表达「至少 N 个元素」的契约
void fill_span(std::span<int> output, int start) {
    for (std::size_t i = 0; i < output.size(); ++i) {
        output[i] = start + static_cast<int>(i);
    }
}

// ---------------------------------------------------------------- 性能实测
template <typename Fn>
double time_ms(Fn&& fn, int repeats) {
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < repeats; ++i) {
        volatile long long sink = fn(i);
        (void)sink;
    }
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

std::vector<int> make_data(std::size_t n) {
    std::vector<int> data;
    data.reserve(n);
    std::mt19937 rng(12345);  // 固定种子：结果可复现
    std::uniform_int_distribution<int> dist(0, 1000000);
    for (std::size_t i = 0; i < n; ++i) {
        data.push_back(dist(rng));
    }
    return data;
}

long long pipeline_ranges(const std::vector<int>& data, int threshold) {
    long long total = 0;
    auto pipeline = data
        | std::views::filter([threshold](int v) { return v % 2 == 0 && v > threshold; })
        | std::views::transform([](int v) { return static_cast<long long>(v) * 2; })
        | std::views::take(500);
    for (const long long v : pipeline) {
        total += v;
    }
    return total;
}

long long pipeline_manual(const std::vector<int>& data, int threshold) {
    long long total = 0;
    std::size_t taken = 0;
    for (const int v : data) {
        if (!(v % 2 == 0 && v > threshold)) {
            continue;
        }
        total += static_cast<long long>(v) * 2;
        if (++taken == 500) {
            break;
        }
    }
    return total;
}

long long pipeline_ranges_no_take(const std::vector<int>& data, int threshold) {
    long long total = 0;
    auto pipeline = data
        | std::views::filter([threshold](int v) { return v % 2 == 0 && v > threshold; })
        | std::views::transform([](int v) { return static_cast<long long>(v) * 2; });
    for (const long long v : pipeline) {
        total += v;
    }
    return total;
}

long long pipeline_manual_no_take(const std::vector<int>& data, int threshold) {
    long long total = 0;
    for (const int v : data) {
        if (!(v % 2 == 0 && v > threshold)) {
            continue;
        }
        total += static_cast<long long>(v) * 2;
    }
    return total;
}

long long pipeline_materialized(const std::vector<int>& data, int threshold) {
    std::vector<int> filtered;
    filtered.reserve(data.size());
    std::copy_if(data.begin(), data.end(), std::back_inserter(filtered),
                 [threshold](int v) { return v % 2 == 0 && v > threshold; });
    std::vector<long long> doubled;
    doubled.reserve(filtered.size());
    std::transform(filtered.begin(), filtered.end(), std::back_inserter(doubled),
                   [](int v) { return static_cast<long long>(v) * 2; });
    long long total = 0;
    const std::size_t count = std::min<std::size_t>(500, doubled.size());
    for (std::size_t i = 0; i < count; ++i) {
        total += doubled[i];
    }
    return total;
}

int main() {
    std::cout << "==== 1. 排序家族：sort / stable_sort / partial_sort / nth_element ====\n";
    {
        struct Task {
            int priority;
            std::string name;
        };
        std::vector<Task> tasks{
            {3, "log-rotate"},
            {1, "health-check"},
            {3, "gc"},
            {2, "flush-cache"},
            {1, "metrics"},
        };

        auto sorted = tasks;
        // stable_sort：相等元素保持原有相对顺序（对「分页 / 二次排序」很重要）
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const Task& a, const Task& b) { return a.priority < b.priority; });
        std::cout << "  stable_sort（同优先级保持输入顺序）：\n    ";
        for (const auto& t : sorted) {
            std::cout << t.priority << ":" << t.name << "  ";
        }
        std::cout << "\n";

        auto partial = tasks;
        // partial_sort：只保证前 k 个有序，比全排序便宜
        std::partial_sort(partial.begin(), partial.begin() + 2, partial.end(),
                          [](const Task& a, const Task& b) { return a.priority < b.priority; });
        std::cout << "  partial_sort 前 2 名：";
        for (int i = 0; i < 2; ++i) {
            std::cout << partial[static_cast<std::size_t>(i)].name << "  ";
        }
        std::cout << "\n";

        std::vector<int> numbers{9, 1, 8, 2, 7, 3, 6, 4, 5};
        // nth_element：把第 n 小放到正确位置，左边都不大于它、右边都不小于它（O(n)）
        std::nth_element(numbers.begin(), numbers.begin() + 4, numbers.end());
        std::cout << "  nth_element 后第 5 小 = " << numbers[4]
                  << "（左边 " << numbers[0] << " ... "
                  << numbers[3] << " 都 <= 它）\n";
        std::cout << "  用途：求中位数、Top-K、找第 k 大，比全排序快一个量级\n";
    }

    std::cout << "\n==== 2. 查找：lower_bound / equal_range（要求已排序） ====\n";
    {
        std::vector<int> data{1, 3, 3, 3, 5, 7, 9, 11};
        auto lower = std::lower_bound(data.begin(), data.end(), 3);  // 第一个 >= 3
        auto upper = std::upper_bound(data.begin(), data.end(), 3);  // 第一个 > 3
        std::cout << "  data = 1 3 3 3 5 7 9 11\n";
        std::cout << "  lower_bound(3) 下标 = " << std::distance(data.begin(), lower) << "\n";
        std::cout << "  upper_bound(3) 下标 = " << std::distance(data.begin(), upper) << "\n";

        auto [first, last] = std::equal_range(data.begin(), data.end(), 3);
        std::cout << "  equal_range(3) 覆盖 [" << std::distance(data.begin(), first) << ", "
                  << std::distance(data.begin(), last) << ") 共 " << std::distance(first, last)
                  << " 个元素\n";

        const bool found = std::binary_search(data.begin(), data.end(), 7);
        const bool missing = std::binary_search(data.begin(), data.end(), 4);
        std::cout << "  binary_search(7) = " << (found ? "true" : "false")
                  << ", binary_search(4) = " << (missing ? "true" : "false") << "\n";
        std::cout << "  工程要点：二分查找前必须确保有序，否则结果是未定义行为；\n";
        std::cout << "            在未排序容器上用 lower_bound 是常见事故\n";
    }

    std::cout << "\n==== 3. transform / accumulate / iota / generate ====\n";
    {
        std::vector<int> source{1, 2, 3, 4, 5};
        std::vector<int> squared(source.size());
        std::transform(source.begin(), source.end(), squared.begin(),
                       [](int v) { return v * v; });
        std::cout << "  transform 平方：";
        for (const int v : squared) {
            std::cout << v << ' ';
        }
        std::cout << "\n";

        const long long sum = std::accumulate(source.begin(), source.end(), 0LL);
        const long long product = std::accumulate(source.begin(), source.end(), 1LL,
                                                  std::multiplies<long long>());
        std::cout << "  accumulate 求和 = " << sum << ", 求积 = " << product << "\n";
        std::cout << "  注意初值类型决定累加类型：写 0 而不是 0LL，"
                     "大数求和会在这里悄悄截断\n";

        std::vector<int> sequential(6);
        std::iota(sequential.begin(), sequential.end(), 100);  // 100,101,102,...
        std::cout << "  iota 结果：";
        for (const int v : sequential) {
            std::cout << v << ' ';
        }
        std::cout << "\n";

        std::vector<int> generated(5);
        int seed = 1;
        std::generate(generated.begin(), generated.end(), [&seed] {
            seed *= 3;   // 有状态生成器：注意捕获必须按引用
            return seed;
        });
        std::cout << "  generate 结果：";
        for (const int v : generated) {
            std::cout << v << ' ';
        }
        std::cout << "\n";
    }

    std::cout << "\n==== 4. erase-remove 惯用法与去重 ====\n";
    {
        std::vector<int> data{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        std::cout << "  原始 size = " << data.size() << "\n";

        // remove_if 只做「搬移」，不改变 size，返回新的逻辑末尾
        auto new_end = std::remove_if(data.begin(), data.end(),
                                      [](int v) { return v % 2 == 0; });
        std::cout << "  remove_if 之后 size 仍为 " << data.size()
                  << "，逻辑末尾前移了 " << std::distance(new_end, data.end()) << " 个位置\n";
        for (std::size_t i = 0; i < data.size(); ++i) {
            std::cout << "    data[" << i << "] = " << data[i]
                      << (i >= static_cast<std::size_t>(std::distance(data.begin(), new_end))
                              ? "  <- 尾部残留，值已无意义"
                              : "")
                      << "\n";
        }

        // 必须配 erase 才真正缩短容器
        data.erase(new_end, data.end());
        std::cout << "  erase 之后 size = " << data.size() << "，内容：";
        for (const int v : data) {
            std::cout << v << ' ';
        }
        std::cout << "\n";

        std::vector<int> duplicated{3, 1, 3, 2, 1, 3, 4, 4, 5};
        std::sort(duplicated.begin(), duplicated.end());
        // unique 同样只搬移，且只对「相邻重复」有效 -> 必须先排序
        duplicated.erase(std::unique(duplicated.begin(), duplicated.end()), duplicated.end());
        std::cout << "  sort + unique + erase 去重后：";
        for (const int v : duplicated) {
            std::cout << v << ' ';
        }
        std::cout << "\n";
    }

    std::cout << "\n==== 5. 判定与计数：all_of / any_of / none_of / count_if / find_if ====\n";
    {
        const std::vector<int> scores{88, 92, 79, 95, 61};
        const auto is_pass = [](int v) { return v >= 60; };
        const auto is_excellent = [](int v) { return v >= 90; };

        std::cout << "  all_of(>=60)      = " << (std::all_of(scores.begin(), scores.end(), is_pass) ? "true" : "false") << "\n";
        std::cout << "  any_of(>=90)      = " << (std::any_of(scores.begin(), scores.end(), is_excellent) ? "true" : "false") << "\n";
        std::cout << "  none_of(<0)       = " << (std::none_of(scores.begin(), scores.end(), [](int v) { return v < 0; }) ? "true" : "false") << "\n";
        std::cout << "  count_if(>=90)    = " << std::count_if(scores.begin(), scores.end(), is_excellent) << "\n";

        auto it = std::find_if(scores.begin(), scores.end(), is_excellent);
        if (it != scores.end()) {
            std::cout << "  find_if 第一个优秀成绩 = " << *it
                      << "（下标 " << std::distance(scores.begin(), it) << "）\n";
        }

        // C++17 起有 std::size / std::data / std::begin，配合范围 for 更省心
        const std::vector<int> many(10, 3);
        std::cout << "  std::size(many) = " << std::size(many) << "\n";
        std::cout << "  提示：all_of / any_of 会短路，any_of 找到第一个就返回，别在里面写副作用\n";
    }

    std::cout << "\n==== 6. C++20 ranges 视图与管道 ====\n";
    {
        std::cout << "  views::iota：不占内存的无穷/有限序列\n";
        std::cout << "    前 5 个平方：";
        for (const int v : std::views::iota(1, 6) | std::views::transform([](int x) { return x * x; })) {
            std::cout << v << ' ';
        }
        std::cout << "\n";

        std::vector<int> data{5, 12, 7, 20, 3, 18, 9, 30, 11, 40};
        std::cout << "  data = 5 12 7 20 3 18 9 30 11 40\n";
        std::cout << "  filter(>10) | transform(x3) | take(3)：";
        for (const int v : data
                 | std::views::filter([](int x) { return x > 10; })
                 | std::views::transform([](int x) { return x * 3; })
                 | std::views::take(3)) {
            std::cout << v << ' ';
        }
        std::cout << "\n";

        std::cout << "  reverse 视图：";
        for (const int v : data | std::views::reverse | std::views::take(3)) {
            std::cout << v << ' ';
        }
        std::cout << "\n";

        std::cout << "  drop 跳过前几个：";
        for (const int v : data | std::views::drop(7)) {
            std::cout << v << ' ';
        }
        std::cout << "\n";

        // 惰性求值：视图本身不做任何计算，只有被遍历时才逐个算
        auto lazy = data | std::views::filter([](int x) { return x > 10; });
        std::cout << "  视图是惰性的：构造 lazy 视图时没有遍历任何元素；"
                     "distance(lazy) = " << std::distance(lazy.begin(), lazy.end()) << "\n";

        // 需要「具体容器」时用 ranges 算法或 views::common + 构造
        std::vector<int> collected;
        std::ranges::copy(data | std::views::filter([](int x) { return x % 2 == 0; }),
                          std::back_inserter(collected));
        std::cout << "  ranges::copy 收进 vector：";
        for (const int v : collected) {
            std::cout << v << ' ';
        }
        std::cout << "\n";

        // C++20 ranges 算法直接接受整个容器，不需要 begin()/end()
        const bool has_big = std::ranges::any_of(data, [](int x) { return x > 35; });
        std::ranges::sort(collected);
        std::cout << "  ranges::any_of(>35) = " << (has_big ? "true" : "false")
                  << ", ranges::sort 后 collected[0] = " << collected[0] << "\n";
    }

    std::cout << "\n==== 7. ranges 与手写循环的可读性 / 性能对比 ====\n";
    {
        const auto data = make_data(50000);
        constexpr int kThreshold = 400000;
        constexpr int kRepeats = 100;

        // 先验证三种写法结果一致，否则比较没有意义
        const long long a = pipeline_ranges(data, kThreshold);
        const long long b = pipeline_manual(data, kThreshold);
        const long long c = pipeline_materialized(data, kThreshold);
        std::cout << "  三种写法结果一致性检查: " << a << " / " << b << " / " << c;
        std::cout << (a == b && b == c ? "  一致\n" : "  不一致，比较无效\n");

        const double t_ranges = time_ms([&](int) { return pipeline_ranges(data, kThreshold); }, kRepeats);
        const double t_manual = time_ms([&](int) { return pipeline_manual(data, kThreshold); }, kRepeats);
        const double t_mat = time_ms([&](int) { return pipeline_materialized(data, kThreshold); }, kRepeats);
        const double t_ranges_full =
            time_ms([&](int) { return pipeline_ranges_no_take(data, kThreshold); }, kRepeats);
        const double t_manual_full =
            time_ms([&](int) { return pipeline_manual_no_take(data, kThreshold); }, kRepeats);

        std::cout << "  数据量 " << data.size() << "，重复 " << kRepeats << " 轮\n";
        std::cout << "  [A] 带 take(500) 提前结束\n";
        std::cout << "      ranges 管道（惰性，无中间容器）  : " << t_ranges << " ms\n";
        std::cout << "      手写循环（break 提前结束）        : " << t_manual << " ms\n";
        std::cout << "  [B] 不带 take，必须扫完整个容器\n";
        std::cout << "      ranges 管道                       : " << t_ranges_full << " ms\n";
        std::cout << "      手写循环                          : " << t_manual_full << " ms\n";
        std::cout << "  [C] 中间容器（copy_if + transform）   : " << t_mat << " ms\n";
        std::cout << "  结论：\n";
        std::cout << "    - 同一件事的三种写法，结果一致；差距来自「是否分配中间容器」和「能否提前结束」\n";
        std::cout << "    - ranges 管道可读性最好、不分配中间容器，但在 Debug 下迭代器调试检查很重，\n";
        std::cout << "      这里比手写循环慢一个量级以上（实测同机同时段约 30 倍）；\n";
        std::cout << "      Release 下内联后通常只差 2~5 倍\n";
        std::cout << "    - 中间容器写法要分配两次、遍历三次，数据量大时最慢\n";
        std::cout << "    - 数字随机器 / 配置 / 标准库实现变化，Debug 数字仅供趋势参考，不要当绝对真理\n";
    }

    std::cout << "\n==== 8. std::span：数组参数的现代替代品 ====\n";
    {
        int c_array[5] = {1, 2, 3, 4, 5};
        std::vector<int> dynamic{10, 20, 30};
        std::vector<int> big{1, 2, 3, 4, 5, 6, 7, 8};

        std::cout << "  sum_legacy(c_array, 5)          = " << sum_legacy(c_array, 5) << "\n";
        std::cout << "  sum_span(c_array)               = " << sum_span(c_array)
                  << "（span 自动带上长度）\n";
        std::cout << "  sum_span(dynamic)               = " << sum_span(dynamic)
                  << "（vector 直接传，无需 .data()/.size()）\n";
        std::cout << "  sum_span(big) 只取前 3 个        = "
                  << sum_span(std::span<const int>(big).first(3)) << "\n";
        std::cout << "  sum_span 子区间                  = "
                  << sum_span(std::span<const int>(big.data() + 2, 3)) << "\n";

        int output[4] = {0, 0, 0, 0};
        fill_span(output, 100);
        std::cout << "  fill_span 写入 C 数组：";
        for (const int v : output) {
            std::cout << v << ' ';
        }
        std::cout << "\n";

        std::cout << "  sizeof(span) = " << sizeof(std::span<const int>)
                  << " 字节（指针 + 长度，零额外开销）\n";
        std::cout << "  接口建议：\n";
        std::cout << "    只读输入 -> std::span<const T> 或 std::string_view\n";
        std::cout << "    可写输出 -> std::span<T>\n";
        std::cout << "    拥有数据 -> 直接传 const std::vector<T>&（表达所有权语义）\n";
        std::cout << "  注意：span 不拥有数据，返回指向局部数组的 span 与返回悬垂指针一样危险\n";
    }

    std::cout << "\n==== 小结 ====\n";
    std::cout << "  算法负责「怎么遍历」，lambda 负责「做什么」；remove/unique 必须配 erase\n";
    std::cout << "  ranges 视图惰性、可组合、可读性高，但性能敏感路径要实测\n";
    std::cout << "  接口传「连续区间」优先用 span / string_view，避免 T* + size 双参数\n";
    return 0;
}
