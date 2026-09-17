// ============================================================================
// 02_cstdlib.cpp  ——  <cstdlib> / <stdlib.h>：通用工具函数
//
// 演示主题：
//   1. rand / srand 的老问题 + C++ 现代替代 <random>（std::mt19937）
//   2. malloc / free：C 风格动态内存，以及「为什么 C++ 该用 new 或容器」
//   3. atoi / atol / strtol：字符串转数字，以及「无法报告错误」这个致命缺陷
//   4. qsort：C 风格泛型排序（函数指针 + void*），与 std::sort 的三维对比
//   5. std::from_chars：C++17 起唯一「不抛异常、不受 locale 影响」的转换方案
//   6. system：调用外部命令（有安全与可移植性代价）
//
// 关键结论：
//   - rand() 的问题：RAND_MAX 可能只有 32767、低位随机性差、全局状态、
//     没有分布保证。「结果 % n」还会引入取模偏置（modulo bias）。
//   - atoi 完全无法报告错误：越界是未定义行为，非法输入返回 0，
//     你分不清「用户输入了 0」和「用户输入了 abc」。
//   - qsort 的比较函数必须返回 int 且用 (x > y) - (x < y) 的写法；
//     直接 return x - y; 在两数相差很大时会 int 溢出，是经典 bug。
//   - std::sort 一般比 qsort 快数倍：前者可以内联比较逻辑，
//     后者必须通过函数指针间接调用，且要先做类型转换。
//
// 说明：本文件同样定义了 _CRT_SECURE_NO_WARNINGS，理由见 01_cstdio.cpp 头部。
// ============================================================================
#define _CRT_SECURE_NO_WARNINGS  // 教学对照用，生产代码用安全版或 C++ 替代品

#include <algorithm>   // std::sort / std::shuffle / std::sample
#include <cerrno>      // errno / ERANGE
#include <charconv>    // std::from_chars（C++17）
#include <chrono>      // 性能计时用 steady_clock
#include <climits>     // INT_MAX / INT_MIN
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>       // std::time：给 srand 播种
#include <iostream>
#include <iterator>    // std::back_inserter
#include <random>      // 现代随机数
#include <string>
#include <system_error>  // std::errc
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

// ---------------------------------------------------------------------------
// 毫秒计时工具：所有性能数字都用它测量，单位毫秒（ms）
// 注意：/MDd + /Zi 的 Debug 构建没有内联优化，绝对值仅供参考，
//       「谁比谁快、快几倍」的趋势在 Release 下会更明显。
// ---------------------------------------------------------------------------
using Clock = std::chrono::steady_clock;

long long ElapsedMs(Clock::time_point t0, Clock::time_point t1) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
}

// ---------------------------------------------------------------------------
// qsort 的比较函数：形参必须是 const void*
// ★ 正确写法是 (x > y) - (x < y)，只返回 -1 / 0 / 1。
//   错误写法 return x - y; 当 x = 2e9, y = -2e9 时整数溢出（UB）。
// ---------------------------------------------------------------------------
int CompareIntAsc(const void* a, const void* b) {
    const int x = *static_cast<const int*>(a);
    const int y = *static_cast<const int*>(b);
    return (x > y) - (x < y);  // 升序；改成 (x < y) - (x > y) 就是降序
}

// 演示「比较函数写错会怎样」：这个版本在两数差距大时会溢出
int CompareIntBuggy(const void* a, const void* b) {
    const int x = *static_cast<const int*>(a);
    const int y = *static_cast<const int*>(b);
    return x - y;  // ★ 危险：x - y 可能超出 int 范围，是未定义行为
}

int CompareDouble(const void* a, const void* b) {
    const double x = *static_cast<const double*>(a);
    const double y = *static_cast<const double*>(b);
    return (x > y) - (x < y);
}

// ===========================================================================
// 1. rand / srand —— 先讲清它为什么不该用
// ===========================================================================
void DemoRand() {
    Section("1. rand / srand：老接口的问题");

    // 不播种 -> 每次运行得到完全相同的序列（调试友好，但做游戏/抽样就错了）
    std::printf("  没播种时前 5 个 rand()：");
    for (int i = 0; i < 5; ++i) {
        std::printf(" %d", std::rand());
    }
    std::printf("\n  RAND_MAX = %d（MSVC 上是 32767，很多平台只有 16 位）\n", RAND_MAX);

    // 用时间播种：同一秒内启动的两个进程会得到相同序列
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    std::printf("  用 time(nullptr) 播种后前 5 个：");
    for (int i = 0; i < 5; ++i) {
        std::printf(" %d", std::rand() % 100);
    }
    std::printf("\n");

    // 取模偏置：无论 RAND_MAX 是否为 n 的整数倍，% n 都会让小区间偏多
    const int kN = 7;
    const int kRounds = 700000;
    int bucket[7] = {};
    for (int i = 0; i < kRounds; ++i) {
        bucket[std::rand() % kN]++;  // ★ 老代码最常用的写法，但它有偏置
    }
    std::printf("  %% %d 的分布（理想每格 %d 次，看最大最小差）：\n", kN, kRounds / kN);
    std::printf("   ");
    for (int i = 0; i < kN; ++i) {
        std::printf(" %d", bucket[i]);
    }
    std::printf("\n  -> 用量大时偏差会很显眼；RAND_MAX 不能整除 kN 时偏置一定存在。\n");

    // 现代做法：<random> 的 mt19937 + 分布类，范围、分布、种子都由标准保证
    std::mt19937 gen(std::random_device{}());  // random_device 提供真随机种子
    std::uniform_int_distribution<int> dice(1, 6);      // 闭区间 [1, 6]
    std::uniform_real_distribution<double> unit(0.0, 1.0);  // 半开区间 [0, 1)

    std::printf("  <random> 掷 10 次骰子：");
    for (int i = 0; i < 10; ++i) {
        std::printf(" %d", dice(gen));
    }
    std::printf("\n  <random> 5 个 [0,1) 实数：");
    std::printf(" %.4f %.4f %.4f %.4f %.4f\n", unit(gen), unit(gen), unit(gen), unit(gen), unit(gen));

    // 从容器里无重复抽样：std::sample（C++17），比手写洗牌更直观
    const std::vector<int> pool = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::vector<int> picked;
    std::sample(pool.begin(), pool.end(), std::back_inserter(picked), 3, gen);
    std::printf("  从 1..10 中无放回抽 3 个：");
    for (int v : picked) {
        std::printf(" %d", v);
    }
    std::printf("\n  结论：新代码一律用 <random>；rand() 只在维护老代码时出现。\n");
}

// ===========================================================================
// 2. malloc / free
// ===========================================================================
void DemoMalloc() {
    Section("2. malloc / free：C 风格动态内存");

    const int kCount = 5;
    // malloc 只负责「要一块原始字节」，返回 void*，C++ 里必须显式转换
    int* arr = static_cast<int*>(std::malloc(sizeof(int) * static_cast<std::size_t>(kCount)));
    if (arr == nullptr) {  // ★ 必须判空，分配失败返回 nullptr（不会抛异常）
        std::printf("  malloc 失败！\n");
        return;
    }
    for (int i = 0; i < kCount; ++i) {
        arr[i] = (i + 1) * 10;
    }
    std::printf("  malloc 得到的数组：");
    for (int i = 0; i < kCount; ++i) {
        std::printf(" %d", arr[i]);
    }
    std::printf("\n");

    // calloc：分配并清零（malloc 不清零，内容不确定）
    int* zeroed = static_cast<int*>(std::calloc(static_cast<std::size_t>(kCount), sizeof(int)));
    if (zeroed != nullptr) {
        std::printf("  calloc 自动清零：%d %d %d\n", zeroed[0], zeroed[1], zeroed[2]);
        // realloc：调整已分配块的大小，可能搬移内存（返回新地址！）
        int* grown = static_cast<int*>(std::realloc(zeroed, sizeof(int) * 10));
        if (grown != nullptr) {
            zeroed = grown;  // ★ 必须用返回值覆盖原指针；直接写 p = realloc(p, n) 会泄漏
            grown[9] = 999;
            std::printf("  realloc 扩到 10 个元素成功，最后一个 = %d\n", zeroed[9]);
        }
        std::free(zeroed);
    }

    std::free(arr);   // ★ 有 malloc 就必须有 free，否则内存泄漏
    arr = nullptr;    // ★ 释放后置空，避免野指针再次被 free（double free 是崩溃级错误）

    // C++ 的替代方案对比
    std::printf("\n  对照：同样的事情在 C++ 里怎么写\n");
    std::printf("    vector<int> v(5, 0);        // 自动管理内存，自动清零，自动释放\n");
    std::printf("    unique_ptr<int[]> p(new int[5]);  // 需要裸数组语义时用智能指针\n");
    std::printf("    auto p2 = std::make_unique<int[]>(5);  // 推荐写法\n");
    std::printf("  -> malloc/free 在 C++ 里只应出现在「对接 C 库」的边界上。\n");

    // 真正的 C++ 写法（可运行）
    std::vector<int> v(static_cast<std::size_t>(kCount), 0);
    for (std::size_t i = 0; i < v.size(); ++i) {
        v[i] = static_cast<int>((i + 1) * 10);
    }
    std::printf("  vector 版本：");
    for (int x : v) {
        std::printf(" %d", x);
    }
    std::printf("（容量 %zu，size %zu，析构时自动释放）\n", v.capacity(), v.size());
}

// ===========================================================================
// 3. atoi / strtol：字符串转数字
// ===========================================================================
void DemoAtoi() {
    Section("3. atoi / atol / strtol / strtod");

    const char* samples[] = {"2026", "  -42", "12ab", "abc", "", "2147483648",
                             "99999999999999999999999999"};
    std::printf("  atoi 的结果（注意它完全无法报告错误）：\n");
    for (const char* s : samples) {
        // atoi 的行为：跳过前导空白，解析可选符号 + 连续数字，遇到第一个非数字字符停止。
        // 一个数字都没有 -> 返回 0；数值超出 int 范围 -> 未定义行为（MSVC 上通常得到
        // INT_MIN 或截断值，但标准不保证任何结果）。
        std::printf("    atoi(\"%s\") = %d\n", s, std::atoi(s));
    }
    std::printf("  -> \"abc\" 和 \"0\" 都返回 0，调用方无法区分；\"2147483648\" 已经越界。\n");

    // strtol：能通过 endptr 和 errno 报告错误，是 C 里正确的做法
    std::printf("\n  strtol 的正确用法（检查 errno 和 endptr）：\n");
    // 下面的样例数组里已经包含 "2147483648" 和超长数字串，覆盖越界场景
    for (const char* s : samples) {
        errno = 0;  // ★ 调用前必须手动清零：函数成功时不会帮你清掉旧的错误码
        char* end = nullptr;
        const long value = std::strtol(s, &end, 10);  // 10 = 十进制
        const bool overflow = (errno == ERANGE);
        const bool noDigits = (end == s);  // 一个字符都没消费 -> 根本没有数字
        const bool trailing = (end != nullptr && *end != '\0' && !noDigits);

        std::printf("    strtol(\"%s\") = %ld | 溢出=%s 无数字=%s 有尾部垃圾=%s\n", s, value,
                    overflow ? "是" : "否", noDigits ? "是" : "否", trailing ? "是" : "否");
    }
    std::printf("  -> C 风格可用，但每次都要写 4 行样板代码，非常容易漏。\n");

    // strtod：浮点版本，同样支持 endptr + errno
    char* end2 = nullptr;
    errno = 0;
    const double d = std::strtod("3.14abc", &end2);
    std::printf("\n  strtod(\"3.14abc\") = %.4f，停在 \"%s\"\n", d, end2);

    std::printf("  -> 在 C++ 里更好用的两个替代：std::stoi（抛异常）和 std::from_chars（不抛）。\n");
}

// ===========================================================================
// 4. std::from_chars：C++17 起推荐的转换方案
// ===========================================================================
void DemoFromChars() {
    Section("4. std::from_chars（C++17）：不抛异常、不受 locale 影响");

    // 为什么推荐：
    //   1) 不抛异常：返回值是 {ptr, ec}，错误处理走返回值，适合热路径与 noexcept 代码。
    //   2) 不受 locale 影响：没有 operator>> 的千分位/小数点逗号问题。
    //   3) 更快：无需构造 std::string / std::istringstream，省掉分配和虚函数。
    //   4) 不需要以 '\0' 结尾：给 [first, last) 区间即可，天然配合 string_view。
    const char* kInput = "1279xyz";
    int value = 0;
    const std::from_chars_result r =
        std::from_chars(kInput, kInput + std::strlen(kInput), value);
    // from_chars_result { const char* ptr; std::errc ec; }
    std::printf("  from_chars(\"%s\") -> value = %d, ec = %s, 剩余 = \"%s\"\n", kInput, value,
                r.ec == std::errc{} ? "无错误" : "有错误", r.ptr);

    // 越界会被明确报成 std::errc::result_out_of_range，而不是 UB
    const char* kOverflow = "99999999999999999999";
    int v2 = 0;
    const std::from_chars_result r2 =
        std::from_chars(kOverflow, kOverflow + std::strlen(kOverflow), v2);
    std::printf("  from_chars(\"%s\") -> ec = %s（value 未被使用）\n", kOverflow,
                r2.ec == std::errc::result_out_of_range ? "result_out_of_range" : "其它");

    double dv = 0.0;
    const char* kFloat = "3.14159,";
    const std::from_chars_result r3 = std::from_chars(kFloat, kFloat + 8, dv);
    std::printf("  from_chars(\"%s\") -> dv = %.5f，停在 '%c'（%zu 个字符被消费）\n", kFloat, dv,
                *r3.ptr, static_cast<std::size_t>(r3.ptr - kFloat));
    std::printf("  -> 解析 CSV 这种「值后面紧跟分隔符」的场景，from_chars 是天然合适的。\n");
}

// ===========================================================================
// 5. qsort vs std::sort：三维对比（写法 / 安全性 / 性能）
// ===========================================================================
void DemoQsortVsSort() {
    Section("5. qsort vs std::sort");

    int a[] = {5, 2, 9, 1, 7, 3, -4, 0};
    const int n = static_cast<int>(sizeof(a) / sizeof(a[0]));

    // --- 写法维度 ---
    // qsort 四件套：首地址、元素个数、单个元素字节数、比较函数指针。
    // 三个地方一旦写错就是运行期 UB（不会编译报错）：
    //   元素个数写成字节数、元素大小写错、比较函数签名签错。
    std::qsort(a, static_cast<std::size_t>(n), sizeof(int), CompareIntAsc);
    std::printf("  qsort 升序：");
    for (int i = 0; i < n; ++i) {
        std::printf(" %d", a[i]);
    }
    std::printf("\n");

    // 比较函数写错时的后果演示（这里只算一次，不真的排序，避免 UB 扩散）
    const int big = INT_MAX, small = INT_MIN + 1;
    std::printf("  错误比较函数演示：CompareIntBuggy(%d, %d) 返回 ", big, small);
    std::printf("%d（真实差值是 %lld，说明 int 装不下 -> UB）\n", CompareIntBuggy(&big, &small),
                static_cast<long long>(big) - static_cast<long long>(small));

    // --- 类型安全维度 ---
    // std::sort 是模板：元素类型自动推导，比较逻辑可以内联，还能用 lambda。
    std::vector<int> v(std::begin(a), std::end(a));
    std::sort(v.begin(), v.end());
    std::printf("  std::sort 升序：");
    for (int x : v) {
        std::printf(" %d", x);
    }
    std::printf("\n");

    std::sort(v.begin(), v.end(), [](int x, int y) { return x > y; });  // lambda 直接写比较
    std::printf("  std::sort + lambda 降序：");
    for (int x : v) {
        std::printf(" %d", x);
    }
    std::printf("\n");

    // qsort 排 double：必须为每种类型单独写比较函数（C 里没有泛型 lambda）
    double dd[] = {3.3, 1.1, 2.2};
    std::qsort(dd, 3, sizeof(double), CompareDouble);
    std::printf("  qsort 排 double：%.1f %.1f %.1f\n", dd[0], dd[1], dd[2]);

    // --- 性能维度：实测 ---
    const int kSize = 300000;
    std::mt19937 gen(12345);  // 固定种子，保证两者排的是同一份数据
    std::uniform_int_distribution<int> dist(0, 1000000);
    std::vector<int> data(static_cast<std::size_t>(kSize));
    for (int& x : data) {
        x = dist(gen);
    }

    std::vector<int> v1 = data;
    const auto t0 = Clock::now();
    std::sort(v1.begin(), v1.end());
    const auto t1 = Clock::now();

    std::vector<int> v2 = data;
    const auto t2 = Clock::now();
    std::qsort(v2.data(), v2.size(), sizeof(int), CompareIntAsc);
    const auto t3 = Clock::now();

    const long long msSort = ElapsedMs(t0, t1);
    const long long msQsort = ElapsedMs(t2, t3);
    std::printf("\n  【实测】排序 %d 个 int（本机实测，仅供参考，Release 下差距更明显）：\n", kSize);
    std::printf("    std::sort : %lld ms\n", msSort);
    std::printf("    qsort     : %lld ms\n", msQsort);
    if (msSort > 0) {
        std::printf("    倍数      : qsort 约为 std::sort 的 %.2f 倍耗时\n",
                    static_cast<double>(msQsort) / static_cast<double>(msSort));
    } else {
        std::printf("    （耗时太小，Debug 下测不出稳定倍数）\n");
    }
    std::printf("  原因：std::sort 的比较器可内联、无需 void* 转换、无需函数指针间接调用。\n");
    std::printf("  另外 std::sort 是 introsort（快排 + 堆排 + 插排），保证 O(n log n) 上界；\n");
    std::printf("  qsort 允许最坏 O(n^2)，且不稳定。\n");

    // 验证两次排序结果一致，避免「快但排错了」
    std::printf("  两者结果一致？%s\n", (v1 == v2) ? "是" : "否");
}

// ===========================================================================
// 6. system：调用外部命令
// ===========================================================================
void DemoSystem() {
    Section("6. std::system 调用外部命令");

    std::printf("  system 的作用：把字符串交给系统的命令解释器执行。\n");
    std::printf("  退出码语义：返回 -1 表示无法启动 shell，否则是命令的退出状态。\n");

    // 注意：这里用的是「绝对无害」的命令，且输出重定向掉，不污染本程序的输出。
    const int rc = std::system("ver > nul 2>&1");
    std::printf("  system(\"ver > nul 2>&1\") 返回 %d（0 通常表示成功）\n", rc);

    std::printf("\n  ★ 工程上要谨慎使用 system 的原因：\n");
    std::printf("    1) 命令串被 shell 解析：如果拼进了用户输入，就是命令注入漏洞。\n");
    std::printf("    2) 不可移植：Unix 要用 /bin/sh 的语法，Windows 用 cmd.exe 的语法。\n");
    std::printf("    3) 无法获取命令的输出：只能拿到退出码，要读输出必须自己重定向到文件。\n");
    std::printf("    4) 阻塞当前线程，且没有超时控制。\n");
    std::printf("  -> 需要执行外部程序时，Unix 用 fork+exec、Windows 用 CreateProcess；\n");
    std::printf("     C++ 标准库本身不提供进程创建能力。\n");
    std::printf("     纯想「退出程序」请用 std::exit / std::quick_exit，不要 system(\"exit\")。\n");
}

}  // namespace

int main() {
    EnableUtf8Console();

    std::printf("==========================================================\n");
    std::printf(" 02_cstdlib.cpp —— <cstdlib> / <stdlib.h>\n");
    std::printf("==========================================================\n");

    DemoRand();
    DemoMalloc();
    DemoAtoi();
    DemoFromChars();
    DemoQsortVsSort();
    DemoSystem();

    std::printf("\n================ 小结 ================\n");
    std::printf("1. 随机数用 <random> 的 mt19937 + distribution，不要用 rand()。\n");
    std::printf("2. malloc/free 要配对，free 后置空；C++ 里优先 vector / unique_ptr。\n");
    std::printf("3. atoi 无法报告错误，别在新代码里用；C 用 strtol + errno，C++ 用 from_chars / stoi。\n");
    std::printf("4. qsort 的比较函数写 (x > y) - (x < y)，绝不能写 x - y。\n");
    std::printf("5. 排序一律 std::sort：更安全、可内联、保证 O(n log n) 上界。\n");
    return 0;
}
