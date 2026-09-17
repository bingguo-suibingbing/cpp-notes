// ============================================================================
//  02_integer_and_float.cpp  —— 01-basics 第 2 篇
// ----------------------------------------------------------------------------
//  演示主题：
//    1. 无符号整数：回绕是「定义良好的模运算」，不是 UB
//    2. 有符号整数：溢出是未定义行为（UB），演示编译器会怎么处理
//    3. unsigned 做循环变量 / 做倒序循环的反模式与正确写法
//    4. 补码的正确说法：模 2^n，-x 的补码 = 2^n - x
//    5. 浮点：0.1 + 0.2 != 0.3 的机制、float vs double 的精度差
//    6. 比较浮点的正确做法（绝对误差 + 相对误差 + nextafter）
//    7. <limits>：numeric_limits 的常用成员
//
//  关键结论：
//    * 无符号溢出 = 模 2^n 回绕（有定义）；有符号溢出 = UB（没定义）。
//    * 「能用无符号就用无符号」是错的；只有做位运算/表示位模式时才用无符号。
//    * 浮点是用二进制分数近似十进制的，0.1 本身就存不准，误差会累积。
//    * 比较浮点数不要用 ==，用带容差的比较函数；整数场景用整数。
// ============================================================================

#define NOMINMAX
// getenv 被 MSVC 标为「不安全」并给 C4996；本文件只用它做一个可选开关，
// 不接收任何用户可控的缓冲区，所以这里就地关掉这个警告。
// 生产代码中更推荐 _dupenv_s / std::getenv 配合明确的长度检查，或者干脆不用环境变量。
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>

namespace {

void use_utf8_console() {
    static_cast<void>(SetConsoleOutputCP(CP_UTF8));
}

void title(const char* text) {
    std::cout << "\n==== " << text << " ====\n";
}

// ---------------------------------------------------------------------------
// 判断两个浮点数是否「实质相等」。
// 只用绝对容差在数值很大时会失效（1e18 的误差本来就大于 1e-9），
// 只用相对容差在数值接近 0 时会失效，所以两者都要。
// ---------------------------------------------------------------------------
bool nearly_equal(double a, double b, double abs_eps = 1e-12, double rel_eps = 1e-9) {
    const double diff = std::fabs(a - b);
    if (diff <= abs_eps) {
        return true;  // 两者都接近 0，绝对误差说了算
    }
    return diff <= rel_eps * std::fmax(std::fabs(a), std::fabs(b));
}

}  // namespace

int main() {
    use_utf8_console();

    // ======================================================================
    title("1. 无符号整数：回绕是有定义的模运算");
    // ======================================================================
    // C++ 标准规定：无符号整数的运算结果按「模 2^N」回绕，其中 N 是该类型的位宽。
    // 也就是说无符号溢出不是错误，而是精确定义的算术——这一点和很多人的直觉相反。
    std::uint8_t u8 = 250;
    u8 = static_cast<std::uint8_t>(u8 + 10);  // 260 mod 256 == 4
    std::cout << "  (uint8_t)250 + 10 = " << static_cast<int>(u8)
              << "   <- 260 按模 256 回绕成 4，标准保证这个结果\n";

    std::uint8_t dec = 0;
    dec = static_cast<std::uint8_t>(dec - 1);  // 0 - 1 mod 256 == 255
    std::cout << "  (uint8_t)0 - 1   = " << static_cast<int>(dec)
              << "   <- 不是 -1，也不是「错误」，而是 255\n";

    constexpr std::uint8_t wrap_evidence = static_cast<std::uint8_t>(0U - 1U);
    static_assert(wrap_evidence == 255, "无符号回绕可以在编译期证明：0-1 == 2^8-1");
    std::cout << "  static_assert 已经在编译期证明了上面两条结论\n";

    // 有符号与无符号互转的语义：也是模运算，不是「截断」。
    int minus_one = -1;
    unsigned int as_unsigned = static_cast<unsigned int>(minus_one);
    static_assert(std::is_same_v<decltype(as_unsigned), unsigned int>);
    std::cout << "  static_cast<unsigned>(-1) = " << as_unsigned
              << "   <- 数学上是 2^32 - 1，即 UINT_MAX\n";
    std::cout << "  UINT_MAX                  = " << std::numeric_limits<unsigned int>::max() << '\n';

    // ======================================================================
    title("2. 有符号整数：溢出是未定义行为");
    // ======================================================================
    // 【错误直觉】「溢出就是变成负数，回绕一下而已，和 unsigned 一样」。
    // 【正确模型】有符号溢出是 UB。标准不给任何保证，编译器有权假设它不发生，
    //            于是可以据此做优化，例如把 x + 1 > x 直接优化成 true。
    // 【工程建议】需要回绕语义就用无符号或 std::int32_t 加位运算；
    //            需要判断溢出就用 __builtin_add_overflow（GCC/Clang）或
    //            先做范围检查，不要依赖 UB 的「实际表现」。
    int max_int = std::numeric_limits<int>::max();
    std::cout << "  INT_MAX = " << max_int << '\n';
    // 下面这行默认不执行：它触发 UB，行为随编译器优化级别变化。
    // 想亲眼看它出问题，设置环境变量 CPPNOTES_SHOW_UB=1 再运行本程序。
    volatile int show_ub_flag = 0;
    if (std::getenv("CPPNOTES_SHOW_UB") != nullptr) {
        show_ub_flag = 1;
    }
    if (show_ub_flag == 1) {
        int overflowed = max_int + 1;  // UB!
        std::cout << "  [UB] INT_MAX + 1 = " << overflowed
                  << "   <- 这个值没有任何保证，换编译器/换优化级别就变，千万别依赖\n";
    } else {
        std::cout << "  （默认跳过 INT_MAX + 1，因为它是 UB；"
                     "设 CPPNOTES_SHOW_UB=1 可强行观察）\n";
    }
    // 用无符号做「同样的事」就是完全合法的：
    unsigned int max_u = std::numeric_limits<unsigned int>::max();
    unsigned int wrapped = max_u + 1U;  // 定义良好：模 2^32
    std::cout << "  UINT_MAX + 1u = " << wrapped << "   <- 完全合法，标准保证是 0\n";

    // ======================================================================
    title("3. unsigned 做循环变量：两个经典反模式");
    // ======================================================================
    // 反模式 A：与 0 比较的倒序循环。当 i == 0 时 --i 给出 UINT_MAX，
    //          条件 i >= 0 恒真，于是死循环（并且会越界访问）。
    std::cout << "  反模式 A 的写法： for (unsigned i = n - 1; i >= 0; --i)\n";
    std::cout << "     问题：i 为 0 时 --i 变成 UINT_MAX，i >= 0 永远成立 -> 死循环\n";
    // 正确写法 1：用有符号类型（推荐，可读性最好）
    {
        const int n = 5;
        std::cout << "     正确写法 1（有符号下标）： ";
        for (int i = n - 1; i >= 0; --i) {            std::cout << i << ' ';
        }
        std::cout << '\n';
    }
    // 正确写法 2：用标准库的反向迭代器（容器场景首选）
    {
        const std::string word = "abcde";
        std::cout << "     正确写法 2（反向迭代器 rbegin/rend）： ";
        for (auto it = word.rbegin(); it != word.rend(); ++it) {
            std::cout << *it << ' ';
        }
        std::cout << '\n';
    }
    // 正确写法 3：真的必须用无符号时，把判断放在「减之前」
    {
        const std::size_t n = 5;
        std::cout << "     正确写法 3（无符号判 i != 0 再自减）： ";
        for (std::size_t i = n; i != 0; --i) {
            std::cout << (i - 1) << ' ';
        }
        std::cout << '\n';
    }

    // 反模式 B：size() 返回的是无符号的 std::size_t，与 int 比较时会触发
    //           C4018（signed/unsigned mismatch），而 /W4 下 C4018 只是警告、
    //           运行时却会给出让你意外的结果——因为负下标被解释成了极大值。
    {
        const std::string text = "hello";
        const int valid_index = 2;
        const int negative_index = -1;
        std::cout << "  反模式 B：int 下标与 text.size()（std::size_t）比较\n";
        const bool valid_ok =
            (static_cast<std::size_t>(valid_index) < text.size());
        std::cout << "     valid_index = 2，比较结果 = " << std::boolalpha << valid_ok
                  << "（下标先显式转成 size_t，意图清楚）\n";
#pragma warning(push)
#pragma warning(disable : 4018)  // 只为演示：正常代码不要关掉这个警告
        const bool trap = negative_index < text.size();
#pragma warning(pop)
        std::cout << "     negative_index = -1，直接比较得到 " << trap
                  << "   <- 因为 -1 被转成 SIZE_MAX，所以「-1 < 5」居然是假\n";
        const bool guarded =
            (negative_index >= 0 && static_cast<std::size_t>(negative_index) < text.size());
        std::cout << "     正确写法：先判 >= 0 再转类型 -> " << guarded << '\n';
        std::cout << "     最省事的正确写法：下标直接声明成 std::size_t，"
                     "循环用范围 for 或迭代器\n";
        std::cout << std::noboolalpha;
    }

    // ======================================================================
    title("4. 补码：模 2^n 与「取补」的正确说法");
    // ======================================================================
    // 【原笔记】「对于负数 用模减去这数的整数，把结果直接用二进制表示，结果为负数补码」。
    // 【问题】「减去这数的整数」指代不清，读者会以为是「减去整数部分」。
    // 【正确模型】把 n 位二进制看成模 2^n 的整数集合：
    //      * 无符号解释：值是二进制本身，范围 [0, 2^n - 1]
    //      * 有符号（补码）解释：
    //          最高位为 0 -> 值就是二进制本身，范围 [0, 2^(n-1) - 1]
    //          最高位为 1 -> 值是「二进制 - 2^n」，范围 [-2^(n-1), -1]
    //      * 求 -x 的补码（x > 0）：取 x 的二进制各位取反再加 1；
    //        等价的算术说法就是 2^n - x。
    //    「各位取反加 1」就是「2^n - x」的位运算形式：
    //        ~x + 1 == (2^n - 1 - x) + 1 == 2^n - x
    //    两种说法完全等价，标准表述用哪一个都可以，但要写清楚是模 2^n。
    constexpr int bits = 8;
    constexpr int modulus = 1 << bits;  // 2^8 = 256
    constexpr int x = 5;
    constexpr int neg_x_direct = modulus - x;         // 2^n - x
    constexpr int neg_x_bitwise = (~x & 0xFF) + 1;    // 各位取反 + 1
    static_assert(neg_x_direct == 251, "2^8 - 5 = 251");
    static_assert(neg_x_bitwise == neg_x_direct, "~x+1 与 2^n-x 等价");
    std::cout << "  8 位下 -5 的补码： 2^8 - 5 = " << neg_x_direct
              << " = 0x" << std::hex << neg_x_direct << std::dec
              << "，等价于 ~5 + 1 = " << neg_x_bitwise << '\n';
    // 直接用有符号类型验证：
    std::int8_t signed_five = -5;
    std::uint8_t bits_of_neg_five = static_cast<std::uint8_t>(signed_five);
    static_assert(static_cast<std::uint8_t>(-5) == 251);
    std::cout << "  (uint8_t)(int8_t)-5 = " << static_cast<int>(bits_of_neg_five)
              << "  —— 位模式完全相同，只有「怎么解释」不同\n";
    std::cout << "  另外注意：C++20 起标准明确规定有符号整数必须是补码表示，\n";
    std::cout << "           所以「二进制 - 2^n」这个解释在所有合规编译器上都成立\n";

    // ======================================================================
    title("5. 浮点：0.1 + 0.2 为什么不等于 0.3");
    // ======================================================================
    // 机制：IEEE-754 双精度用「符号 + 11 位指数 + 52 位尾数」表示 ±1.f × 2^e。
    //      0.1 的十进制小数无法写成有限位二进制分数（就像 1/3 在十进制里写不完），
    //      所以 0.1 存进去时就已经带了一个极小的舍入误差。
    //      两个带误差的数相加，误差按「最后一位的半个单位」（ulp）累积，
    //      于是 0.1 + 0.2 得到 0.30000000000000004，而 0.3 本身是另一个近似值。
    const double sum = 0.1 + 0.2;
    std::cout << std::setprecision(20);
    std::cout << "  0.1 + 0.2 = " << sum << '\n';
    std::cout << "  0.3       = " << 0.3 << '\n';
    std::cout << "  两者是否 == : " << std::boolalpha << (sum == 0.3) << '\n';
    std::cout << "  差值        = " << (sum - 0.3) << '\n';
    std::cout << std::noboolalpha;
    // 注意：static_assert(sum == 0.3) 是不成立的，这就证明了「误差是编译期确定的、
    //       不是随机的」——它来自十进制到二进制的表示本身。
    std::cout << "  这是「表示误差」而不是「计算错误」：同样的表达式在任何合规编译器上结果都一样\n";
    std::cout << std::setprecision(6);

    // float vs double 的精度差：float 只有约 7 位有效十进制数字。
    std::cout << "\n  float  : sizeof=" << sizeof(float) << " digits10="
              << std::numeric_limits<float>::digits10
              << " max_digits10=" << std::numeric_limits<float>::max_digits10 << '\n';
    std::cout << "  double : sizeof=" << sizeof(double) << " digits10="
              << std::numeric_limits<double>::digits10
              << " max_digits10=" << std::numeric_limits<double>::max_digits10 << '\n';
    // 用累加 0.1 的方式直观展示误差累积
    float f_acc = 0.0f;
    double d_acc = 0.0;
    for (int i = 0; i < 100000; ++i) {
        f_acc = static_cast<float>(f_acc + 0.1f);
        d_acc += 0.1;
    }
    std::cout << "  累加 0.1f 十万次（float ）: " << std::setprecision(10) << f_acc
              << "   误差 " << (f_acc - 10000.0) << '\n';
    std::cout << "  累加 0.1  十万次（double）: " << d_acc
              << "   误差 " << (d_acc - 10000.0) << '\n';
    std::cout << std::setprecision(6);
    std::cout << "  float 的误差比 double 大好几个数量级，金融/科学计算一律用 double\n";

    // 等值比较失败的另一种形态：先算再看
    std::cout << "\n  典型翻车场景： if (a + b == c) 做等值判断 -> "
              << "对浮点来说几乎必然失败\n";
    std::cout << "  正确做法： nearly_equal(0.1 + 0.2, 0.3) = " << std::boolalpha
              << nearly_equal(0.1 + 0.2, 0.3) << '\n';
    std::cout << "  注意 nearly_equal 里同时用了绝对误差和相对误差，见文件顶部的实现\n";

    // nextafter：朝某个方向前进「一个 ulp」，可以用来判断两个数是否紧邻。
    const double one_d = 1.0;
    const double next = std::nextafter(one_d, 2.0);
    std::cout << "  std::nextafter(1.0, 2.0) - 1.0 = " << next - one_d
              << "   <- 这就是 1.0 处的 1 ulp（约 2.22e-16）\n";
    std::cout << "  1.0 + 1e-17 == 1.0 ? " << (1.0 + 1e-17 == 1.0)
              << "   <- 小于半个 ulp 的增量会被直接吃掉（吸收）\n";
    std::cout << std::noboolalpha;

    // 浮点的特殊值：可以用 numeric_limits 查，也可以用 std::isnan / std::isinf
    // 这里用 volatile 挡住编译期常量折叠：0.0/0.0 如果被当成常量表达式，
    // 编译期就会报 C2124（divide or mod by zero），而我们想看的是运行期行为。
    volatile double zero_v = 0.0;
    const double zero = zero_v;
    const double nan_value = zero / zero;   // IEEE-754：0/0 -> NaN（不是 UB）
    const double inf_value = 1.0 / zero;    // IEEE-754：1/0 -> +Inf（整数除零才是 UB）
    std::cout << "\n  0.0 / 0.0 = " << nan_value << "  isnan=" << std::boolalpha
              << std::isnan(nan_value) << '\n';
    std::cout << "  1.0 / 0.0 = " << inf_value << "  isinf=" << std::isinf(inf_value) << '\n';
    std::cout << "  NaN != NaN 恒为真，且用 == 比较 NaN 永远失败，必须用 std::isnan\n";
    std::cout << "  注意：整数除以 0 是 UB（会直接崩），浮点除以 0 由 IEEE-754 定义\n";
    std::cout << std::noboolalpha;

    // ======================================================================
    title("6. std::numeric_limits 速查");
    // ======================================================================
    std::cout << "  int  : min=" << std::numeric_limits<int>::min()
              << " max=" << std::numeric_limits<int>::max()
              << " is_signed=" << std::numeric_limits<int>::is_signed << '\n';
    std::cout << "  uint : min=" << std::numeric_limits<unsigned int>::min()
              << " max=" << std::numeric_limits<unsigned int>::max()
              << " is_signed=" << std::numeric_limits<unsigned int>::is_signed << '\n';
    std::cout << "  double: epsilon=" << std::numeric_limits<double>::epsilon()
              << "\n          即「1.0 与下一个可表示值」的差，是机器精度\n";
    std::cout << "  float : epsilon=" << std::numeric_limits<float>::epsilon() << '\n';
    std::cout << "  int 在 C++20 下 is_modulo = " << std::numeric_limits<int>::is_modulo
              << "（0 表示「标准不再承诺有符号回绕」）；unsigned 的 is_modulo = "
              << std::numeric_limits<unsigned int>::is_modulo << '\n';

    std::cout << "\n[02] 结束。下一步：03_operators_and_control.cpp\n";
    return 0;
}
