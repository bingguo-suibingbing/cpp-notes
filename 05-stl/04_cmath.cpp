// ============================================================================
// 04_cmath.cpp  ——  <cmath> / <math.h>：数学函数（含参考手册要点）
//
// 演示主题：
//   1. 圆周率的三种取法：M_PI / 自写常量 / C++20 std::numbers::pi
//   2. fabs / abs 与 INT_MIN 的溢出坑
//   3. 四种取整：floor / ceil / trunc / round（负数时结果完全不同）
//   4. 三角函数：参数是弧度不是角度；atan2 能判象限；acos 前必须 clamp
//   5. 指数与对数：log 是自然对数；定义域检查；换底公式
//   6. 取余：% 只能用于整数，浮点用 fmod（符号跟着被除数）
//   7. fmin / fmax / fdim / copysign / modf / frexp / hypot / fma
//   8. 浮点判等：绝不能用 ==，必须用容差（绝对容差 vs 相对容差）
//   9. NaN 与 inf：判断只能用 isnan / isinf / isfinite；整数除 0 会崩，浮点不会
//  10. 实战：一元二次方程、海伦公式、余弦定理（都把「先校验再计算」写进流程）
//
// 关键结论：
//   - <math.h> 和 <cmath> 是同一个头文件的两种写法：前者把函数放在全局命名空间，
//     后者放在 std 命名空间。新代码一律写 <cmath> + std:: 前缀（本仓库约定）。
//   - M_PI 不是标准的一部分（MSVC 需要 _USE_MATH_DEFINES，而且是先 define 后 include）。
//     C++20 起用 std::numbers::pi，既标准又免宏。
//   - 数学函数越界时【不抛异常、不崩溃】，而是返回 NaN / inf 并一路传播下去，
//     最后你只看到一个 "-nan(ind)"。所以「算完立刻 if (!std::isfinite(r)) 检查」
//     是工程习惯，不是可选项。
//   - 浮点除以 0 得到 inf；整数除以 0 是未定义行为（Windows 上直接崩溃）。
//
// 说明：本文件不用 C 风格的 M_PI，也不需要 _USE_MATH_DEFINES —— 这正是
//       「现代写法比原素材更省事」的一个例子（原素材靠 _USE_MATH_DEFINES）。
// ============================================================================

#include <algorithm>   // std::clamp
#include <cfloat>      // DBL_MAX / DBL_EPSILON / DBL_DIG
#include <chrono>      // 计时对比 pow 与连乘
#include <climits>     // INT_MIN / INT_MAX
#include <cmath>       // C++ 版数学函数（std:: 命名空间）
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>      // std::numeric_limits：C++ 版的极值宏
#include <numbers>     // C++20：std::numbers::pi（标准的圆周率常量）
#include <string>

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

// 浮点判等的绝对容差。数值量级接近 1 时够用。
constexpr double kEps = 1e-9;

bool NearlyEqual(double a, double b, double eps = kEps) {
    return std::fabs(a - b) < eps;
}

// 数值量级很大时必须用相对容差，否则 1e15 和 1e15+0.1 会被判成「相等」
bool NearlyEqualRel(double a, double b, double rel = 1e-9) {
    return std::fabs(a - b) < rel * std::fmax(std::fabs(a), std::fabs(b));
}

// 把 NaN / inf 打印成人能看懂的文字，避免初学者看到 "-nan(ind)" 一头雾水
std::string Show(double v) {
    if (std::isnan(v)) return "非数字(NaN)";
    if (std::isinf(v)) return v > 0 ? "+无穷大(inf)" : "-无穷大(-inf)";
    return std::to_string(v);
}

// 角弧度互转：三角函数只认弧度，这是第一大坑
double ToRad(double deg) { return deg * std::numbers::pi / 180.0; }
double ToDeg(double rad) { return rad * 180.0 / std::numbers::pi; }

// ===========================================================================
// 1. 圆周率怎么取
// ===========================================================================
void DemoConstants() {
    Section("1. 圆周率与数学常量");

    std::printf("  std::numbers::pi        = %.17g   <- C++20 标准写法，推荐\n",
                std::numbers::pi);
    std::printf("  std::numbers::e         = %.17g\n", std::numbers::e);
    std::printf("  std::numbers::sqrt2     = %.17g\n", std::numbers::sqrt2);
    std::printf("  std::numbers::ln2       = %.17g\n", std::numbers::ln2);

    constexpr double kPi = 3.14159265358979323846;
    std::printf("  自写 const double PI    = %.17g   <- 不依赖任何扩展，永远可用\n", kPi);
    std::printf("  std::numbers::pi == 自写 PI ? %s\n",
                (std::numbers::pi == kPi) ? "是（同一份位模式）" : "否");

    std::printf("  运行期算：std::acos(-1.0) = %.17g   <- 结果一致，但白花一次函数调用\n",
                std::acos(-1.0));

    std::printf("\n  ★ M_PI 想用也可以，但：\n");
    std::printf("    - 它不是 C/C++ 标准，是 POSIX/实现扩展；\n");
    std::printf("    - MSVC 必须在 #include <math.h> 【之前】 #define _USE_MATH_DEFINES；\n");
    std::printf("    - 一旦忘写、或者被别的头文件先包含了 math.h，就是「M_PI 未定义」。\n");
    std::printf("    -> 新代码直接 std::numbers::pi，一行解决问题。\n");

    // 极值与精度：C 的宏 vs C++ 的 numeric_limits
    std::printf("\n  极值与精度（C 宏 与 C++ 模板 对照）：\n");
    std::printf("    DBL_MAX      = %.6e   | std::numeric_limits<double>::max()  同值\n", DBL_MAX);
    std::printf("    DBL_EPSILON  = %.6e   | epsilon() 同值（1.0 与下一个可表示 double 之差）\n",
                DBL_EPSILON);
    std::printf("    DBL_DIG      = %d              | digits10 同值（十进制约有 15~16 位有效数字）\n",
                DBL_DIG);
    std::printf("    INT_MAX      = %d\n", INT_MAX);
    std::printf("    INT_MIN      = %d   <- 注意它比 -INT_MAX 还小 1，这是坑\n", INT_MIN);
    std::printf("    double 有 inf 吗：%s，double 有 NaN 吗：%s\n",
                std::numeric_limits<double>::has_infinity ? "有" : "没有",
                std::numeric_limits<double>::has_quiet_NaN ? "有" : "没有");
}

// ===========================================================================
// 2. fabs / abs
// ===========================================================================
void DemoAbs() {
    Section("2. fabs 与 abs：别用错版本");

    std::printf("  std::fabs(-3.5) = %.1f（浮点绝对值）\n", std::fabs(-3.5));
    std::printf("  std::fabs(-0.0) = %.1f  <- 得到的是 +0.0，想知道符号要用 std::signbit\n",
                std::fabs(-0.0));
    std::printf("  std::signbit(-0.0) = %s，std::signbit(0.0) = %s\n",
                std::signbit(-0.0) ? "true" : "false", std::signbit(0.0) ? "true" : "false");

    // std::abs 在 C++ 里是按参数类型重载的：int / long / double 都有
    std::printf("  std::abs(-5)   = %d（整数版）\n", std::abs(-5));
    std::printf("  std::abs(-5.0) = %.1f（C++ 重载会挑 double 版）\n", std::abs(-5.0));
    std::printf("  注意：C 的 abs(int) 只有一个版本；把 1.5 传进去会被截断成 1，返回 1。\n");
    std::printf("       在 C++ 里因为重载，std::abs(1.5) 是 1.5；但写 ::abs(1.5) 仍可能踩坑。\n");

    // INT_MIN 的坑：-INT_MIN 溢出
    std::printf("\n  ★★ std::abs(INT_MIN) 是未定义行为 ★★\n");
    std::printf("     INT_MIN = %d，而 -INT_MIN = %d 超出了 int 能表示的最大值 %d。\n", INT_MIN,
                INT_MAX, INT_MAX);
    std::printf("     所以「取绝对值」在 int 上不封闭：结果是 UB（MSVC 上通常还是负数）。\n");
    std::printf("     正确做法：先提升到更宽的类型再取绝对值：\n");
    const long long safeAbs = std::llabs(static_cast<long long>(INT_MIN));
    std::printf("       std::llabs((long long)INT_MIN) = %lld  <- 这个是正确的\n", safeAbs);
}

// ===========================================================================
// 3. 四种取整
// ===========================================================================
void DemoRounding() {
    Section("3. floor / ceil / trunc / round");

    std::printf("  %10s %12s %12s %12s %12s\n", "输入", "floor", "ceil", "trunc", "round");
    const double values[] = {2.5, -2.5, 2.4, -2.4, 3.0, -0.5, -0.0};
    for (const double v : values) {
        std::printf("  %10.1f %12.1f %12.1f %12.1f %12.1f\n", v, std::floor(v), std::ceil(v),
                    std::trunc(v), std::round(v));
    }

    std::printf("\n  方向记法：\n");
    std::printf("    floor  朝 -∞：floor(-2.5) = -3\n");
    std::printf("    ceil   朝 +∞：ceil(-2.5)  = -2\n");
    std::printf("    trunc  朝 0 ：trunc(-2.9) = -2（和 (int) 强制转换一样）\n");
    std::printf("    round  四舍五入，.5 远离 0：round(2.5) = 3，round(-2.5) = -3\n");
    std::printf("  ★ 它们都只改数值不改类型：std::floor(1.5) 仍是 double。\n");
    std::printf("  ★ (int)-2.9 = -2 而 std::floor(-2.9) = -3 —— 负数时两者不同，这是常见 bug。\n");

    // 想直接拿整数类型用 lround / llround
    const long rounded = std::lround(3.7);
    std::printf("\n  std::lround(3.7) = %ld（返回 long，省掉一次转换）\n", rounded);

    // 工程套路 1：四舍五入保留 n 位小数
    const double price = 3.14159;
    const double roundedTo2 = std::round(price * 100.0) / 100.0;
    std::printf("  保留 2 位小数：round(3.14159 * 100) / 100 = %.2f\n", roundedTo2);
    std::printf("  ★ 但这只是「显示用」的做法。钱的运算必须用整数分（long long cents），\n");
    std::printf("    否则累加误差会让你对不上账。\n");

    // 工程套路 2：整除向上取整（分页）
    const int total = 100, perPage = 30;
    const int pagesCeil = static_cast<int>(std::ceil(static_cast<double>(total) / perPage));
    const int pagesInt = (total + perPage - 1) / perPage;  // 纯整数写法，更精确
    std::printf("  100 个元素每页 30 个：ceil 版 = %d 页，整数版 = %d 页（两者应一致）\n", pagesCeil,
                pagesInt);
    std::printf("  ★ 纯整数写法 (total + perPage - 1) / perPage 没有浮点精度问题，更推荐。\n");

    std::printf("  另外：std::rint / std::nearbyint 按当前舍入模式（默认「四舍五入到偶数」），\n");
    std::printf("        即所谓「银行家舍入」，round 不是这个行为。\n");
}

// ===========================================================================
// 4. 三角函数与反三角函数
// ===========================================================================
void DemoTrig() {
    Section("4. 三角函数：弧度、atan2、acos 的 clamp");

    std::printf("  ★ 规则 1：参数是【弧度】不是角度。\n");
    std::printf("    std::sin(30.0)          = %+.4f  <- 这是 30 弧度，错的\n", std::sin(30.0));
    std::printf("    std::sin(ToRad(30.0))   = %+.4f  <- 这才是 30 度\n", std::sin(ToRad(30.0)));

    std::printf("\n  常用角的 sin/cos 对照表（用两种方式互相验证）：\n");
    struct Row {
        double deg;
        double sinVal;
        double cosVal;
    };
    const double s2 = std::sqrt(2.0) / 2.0;
    const double s3 = std::sqrt(3.0) / 2.0;
    const Row rows[] = {{0, 0.0, 1.0}, {30, 0.5, s3}, {45, s2, s2}, {60, s3, 0.5}, {90, 1.0, 0.0}};
    std::printf("    %6s %14s %14s %10s\n", "角度", "sin", "cos", "是否吻合");
    for (const Row& r : rows) {
        const double s = std::sin(ToRad(r.deg));
        const double c = std::cos(ToRad(r.deg));
        const bool ok = NearlyEqual(s, r.sinVal) && NearlyEqual(c, r.cosVal);
        std::printf("    %6.0f %14.10f %14.10f %10s\n", r.deg, s, c, ok ? "是" : "否");
    }

    std::printf("\n  ★ 规则 2：sin(pi) 不等于 0（浮点误差约 1.2e-16），判等必须用容差。\n");
    std::printf("    std::sin(std::numbers::pi)           = %.3e\n", std::sin(std::numbers::pi));
    std::printf("    std::sin(pi) == 0.0 ?                %s\n",
                (std::sin(std::numbers::pi) == 0.0) ? "true" : "false");
    std::printf("    NearlyEqual(std::sin(pi), 0.0) ?     %s\n",
                NearlyEqual(std::sin(std::numbers::pi), 0.0) ? "true" : "false");

    std::printf("\n  tan(90 度) 不是 inf，而是一个很大的有限数：%.3e\n", std::tan(ToRad(90.0)));
    std::printf("    因为 ToRad(90.0) 只是 pi/2 的近似值，tan 在那个点上不会刚好发散。\n");

    // ★★★ atan2：能判象限的反三角函数
    std::printf("\n  ★★★ atan2(y, x) —— 求方向角必须用它 ★★★\n");
    std::printf("    atan(y/x) 只能给出 (-90, 90)，分不清第一象限和第三象限，x=0 还会除零。\n");
    std::printf("    atan2(y, x) 自动处理四个象限，结果范围 (-180, 180]。\n");
    struct P {
        double x;
        double y;
        double expectedDeg;
    };
    const P pts[] = {{1, 1, 45}, {-1, 1, 135}, {-1, -1, -135}, {1, -1, -45}, {0, 1, 90}, {-1, 0, 180}};
    std::printf("    %10s %10s %14s %14s\n", "x", "y", "atan2(度)", "atan(y/x)(度)");
    for (const P& p : pts) {
        const double a2 = ToDeg(std::atan2(p.y, p.x));
        const bool asExpected = NearlyEqual(a2, p.expectedDeg, 1e-9);
        std::printf("    %10.1f %10.1f %14.4f", p.x, p.y, a2);
        if (std::fabs(p.x) > 1e-12) {
            const double a1 = ToDeg(std::atan(p.y / p.x));
            // 只有当 atan 和 atan2 差得远时，才说明 atan 判错了象限
            std::printf(" %14.4f  %s", a1,
                        NearlyEqual(a1, a2, 1e-9) ? "（此象限下恰好也对）" : "（atan 判错象限）");
        } else {
            std::printf(" %14s", "(x=0，atan 里要除零，根本算不了)");
        }
        std::printf("  期望=%g %s\n", p.expectedDeg, asExpected ? "OK" : "★不符");
    }

    // ★ acos 的 clamp：浮点误差可能让余弦值变成 1.0000000000000002
    std::printf("\n  ★ acos 的定义域是 [-1, 1]，越界返回 NaN，所以传参前必须 clamp：\n");
    const double badCos = 1.0000000000000002;  // 只比 1 大一点点，但足以让 acos 变 NaN
    std::printf("    std::acos(%.17g) = %s  <- 越界，直接变 NaN\n", badCos,
                Show(std::acos(badCos)).c_str());
    const double clamped = std::clamp(badCos, -1.0, 1.0);
    std::printf("    std::acos(std::clamp(值, -1.0, 1.0)) = %.6f 度  <- 正确做法\n",
                ToDeg(std::acos(clamped)));

    std::printf("\n  双曲与反双曲：std::sinh / cosh / tanh / asinh / acosh / atanh 用法相同。\n");
}

// ===========================================================================
// 5. 指数与对数
// ===========================================================================
void DemoExpLog() {
    Section("5. exp / log / log10 / log2");

    std::printf("  std::exp(1.0)    = %.10f（e 的 1 次方，应等于 std::numbers::e）\n", std::exp(1.0));
    std::printf("  std::log(std::numbers::e) = %.10f（log 是【自然对数】ln）\n",
                std::log(std::numbers::e));
    std::printf("  std::log10(1000.0) = %.10f\n", std::log10(1000.0));
    std::printf("  std::log2(256.0)   = %.10f（二分查找层数、比特位宽常用）\n", std::log2(256.0));

    // exp 与 log 互为反函数
    const double v = 4.2;
    std::printf("  互逆验证：std::log(std::exp(4.2)) = %.15f（应回到 4.2）\n", std::log(std::exp(v)));

    std::printf("\n  ★ 定义域是 x > 0：\n");
    std::printf("    std::log(0.0)  = %s  <- 趋向 -inf\n", Show(std::log(0.0)).c_str());
    std::printf("    std::log(-5.0) = %s       <- 直接是 NaN\n", Show(std::log(-5.0)).c_str());
    std::printf("    所以必须写成 if (x > 0.0) 才调用，或者算完用 isfinite 检查。\n");

    std::printf("\n  ★ exp 很快溢出：double 上限约 1.8e308，exp(710) 就是 +inf。\n");
    std::printf("    std::exp(700.0) = %.6e\n", std::exp(700.0));
    std::printf("    std::exp(710.0) = %s\n", Show(std::exp(710.0)).c_str());

    // 换底公式
    std::printf("\n  换底公式 log_a(b) = log(b) / log(a)：以 3 为底 81 的对数 = %.10f\n",
                std::log(81.0) / std::log(3.0));

    // 高精度技巧：x 很小时 log1p / expm1 比 log(1+x) / exp(x)-1 精确得多
    std::printf("\n  ★ 小量场景必须用 log1p / expm1（实测数值）：\n");
    std::printf("    %-8s %-24s %-24s %s\n", "x", "log(1+x)", "log1p(x)", "相对误差（越大越糟）");
    for (const double tiny : {1e-5, 1e-9, 1e-15, 1e-16}) {
        const double naive = std::log(1.0 + tiny);
        const double good = std::log1p(tiny);
        std::printf("    %-8.0e %-24.17g %-24.17g %.3e\n", tiny, naive, good,
                    std::fabs(naive - good) / good);
    }
    std::printf("    <- 根因：1 + 1e-16 在 double 里就被舍入成 1.0 了，log 直接返回 0，\n");
    std::printf("       相对误差高达 100%%。log1p 内部用专门算法绕开这次加法。\n");
    std::printf("    expm1 同理：std::exp(1e-16) - 1.0 == 0，而 std::expm1(1e-16) = 1e-16。\n");
    std::printf("    -> 金融利率、概率、物理小量计算里，这两个函数是刚需。\n");
    std::printf("    实测：expm1(1e-16) = %.17g，而 exp(1e-16) - 1.0 = %.17g\n", std::expm1(1e-16),
                std::exp(1e-16) - 1.0);

    // 位数计算：log10 的整数部分 + 1
    const int n = 123456;
    std::printf("  %d 的位数 = (int)std::log10(%d) + 1 = %d\n", n, n,
                static_cast<int>(std::log10(static_cast<double>(n))) + 1);
}

// ===========================================================================
// 6. 取余：% 与 fmod
// ===========================================================================
void DemoFmod() {
    Section("6. 取余：% 只能用于整数，浮点用 fmod");

    std::printf("  整数版：7 %% 3 = %d\n", 7 % 3);
    std::printf("  ★ 7.5 %% 2 编译不通过 —— %% 运算符只接受整数。\n");
    std::printf("  浮点版：std::fmod(7.5, 2.0) = %.1f\n", std::fmod(7.5, 2.0));
    std::printf("  std::fmod(-7.0, 3.0) = %.1f  <- 符号跟着【被除数】，不是数学上的 2\n",
                std::fmod(-7.0, 3.0));
    std::printf("  std::remainder(-7.0, 3.0) = %.1f  <- IEEE 版：取「最接近 0」的余数\n",
                std::remainder(-7.0, 3.0));
    std::printf("    所以 remainder(-7, 3) = -1（-7 = -2*3 + (-1)，|-1| 已经是最小）\n");

    // 工程套路：把角度规整到 [0, 360)
    const double angles[] = {370.0, -30.0, 720.0, 45.0};
    std::printf("\n  角度规整到 [0, 360)：");
    for (const double a : angles) {
        double norm = std::fmod(a, 360.0);
        if (norm < 0.0) norm += 360.0;  // ★ fmod 会留下负数，必须补一次
        std::printf(" %.0f->%.0f", a, norm);
    }
    std::printf("\n");

    // 工程套路：判断能不能整除（浮点必须用容差）
    const double amount = 1.5, unit = 0.5;
    const bool divisible = NearlyEqual(std::fmod(amount, unit), 0.0);
    std::printf("  1.5 是 0.5 的整数倍吗？std::fmod(1.5, 0.5) = %.1f -> %s\n",
                std::fmod(amount, unit), divisible ? "是" : "否");
    std::printf("  ★ 不能写 std::fmod(a, b) == 0.0 —— 浮点误差会让本该为 0 的结果变成 4.4e-16。\n");
}

// ===========================================================================
// 7. 比较、拆解与其它常用函数
// ===========================================================================
void DemoMisc() {
    Section("7. fmin/fmax/fdim/copysign/modf/hypot/fma");

    std::printf("  std::fmin(3.0, 7.0) = %.1f，std::fmax(3.0, 7.0) = %.1f\n", std::fmin(3.0, 7.0),
                std::fmax(3.0, 7.0));
    std::printf("  std::fdim(7.0, 3.0) = %.1f（x>y ? x-y : 0，不会出现负数）\n",
                std::fdim(7.0, 3.0));
    std::printf("  std::fdim(3.0, 7.0) = %.1f\n", std::fdim(3.0, 7.0));
    std::printf("  std::copysign(3.0, -1.0) = %.1f（把 -1 的符号贴到 3 上）\n",
                std::copysign(3.0, -1.0));

    // ★ fmin/fmax 遇到 NaN 会返回「另一个数」；std::min/std::max 不保证
    const double nanVal = std::numeric_limits<double>::quiet_NaN();
    std::printf("\n  ★ NaN 时的区别（处理传感器数据、缺失值时很关键）：\n");
    std::printf("    std::fmax(NaN, 5.0)  = %.1f   <- NaN 被忽略，返回另一个操作数\n",
                std::fmax(nanVal, 5.0));
    std::printf("    std::fmin(NaN, 5.0)  = %.1f\n", std::fmin(nanVal, 5.0));
    std::printf("    std::max(NaN, 5.0)   = %.1f   <- 不可靠：它用 operator< 比较，\n",
                std::max(nanVal, 5.0));
    std::printf("        std::max(a,b) 的实现是 (a < b) ? b : a，而 NaN < 5.0 是 false，\n");
    std::printf("        所以直接返回第一个参数 a，也就是 NaN。行为容易让人误解。\n");
    std::printf("    -> 处理可能有 NaN 的浮点数据时，用 fmax/fmin 更稳妥。\n");

    // hypot：两点距离，比 sqrt(x*x + y*y) 安全
    std::printf("\n  std::hypot(3.0, 4.0) = %.1f（3-4-5 直角三角形）\n", std::hypot(3.0, 4.0));
    const double big = 1e200;
    std::printf("  ★ 大数场景：x = 1e200 时\n");
    std::printf("    std::sqrt(x*x + x*x) = %s  <- 中间结果溢出，失败\n",
                Show(std::sqrt(big * big + big * big)).c_str());
    std::printf("    std::hypot(x, x)     = %.6e  <- 正确（hypot 内部先做缩放）\n",
                std::hypot(big, big));

    // modf：拆整数/小数部分（注意第二个参数是【指针】）
    double intPart = 0.0;
    const double fracPart = std::modf(3.75, &intPart);
    std::printf("\n  std::modf(3.75, &ip) -> 整数部分 %.1f，小数部分 %.2f  ★ 是返回值给小数\n",
                intPart, fracPart);

    // frexp / ldexp：拆成尾数 x 2^指数
    int expo = 0;
    const double mant = std::frexp(12.0, &expo);
    std::printf("  std::frexp(12.0, &e) -> 尾数 %.2f，指数 %d（12 = 0.75 x 2^4）\n", mant, expo);
    std::printf("  std::ldexp(0.75, 4)  = %.1f（反着拼回去）\n", std::ldexp(0.75, 4));

    // fma：a*b+c 只舍入一次，精度更高（也是硬件 FMA 指令的入口）
    std::printf("\n  std::fma(a, b, c) 演示「中间结果不截断」的价值（实测数值）：\n");
    std::printf("    %-38s %-16s %s\n", "a, b, c", "a*b + c", "fma(a, b, c)");
    struct FmaCase {
        const char* name;
        double a;
        double b;
        double c;
    };
    const FmaCase cases[] = {
        {"1e16, 3.0000000000000004, -3e16", 1e16, 3.0000000000000004, -3e16},
        {"1e16, 1.0000000000000002, -1e16", 1e16, 1.0000000000000002, -1e16},
        {"3.0, 4.0, -12.0（简单场景）", 3.0, 4.0, -12.0},
    };
    for (const FmaCase& fc : cases) {
        std::printf("    %-34s %-16.17g %.17g\n", fc.name, fc.a * fc.b + fc.c,
                    std::fma(fc.a, fc.b, fc.c));
    }
    std::printf("    a = 1e16, b = 3.0000000000000004, c = -3e16 时：\n");
    std::printf("      数学上的精确结果 = (3.0000000000000004 - 3) * 1e16 = 4.4408920985006...\n");
    std::printf("      普通写法先算 a*b 并舍入，再减 c，零头被吃掉 -> 得到 4；\n");
    std::printf("      fma 把乘法结果完整保留到加法之后才舍入 -> 得到 4.4408920985006261617。\n");
    std::printf("    用途：Kahan 求和、行列式/判别式、几何求交等对误差敏感的地方。\n");

    // 性能：pow 做平方比自己乘慢很多（这里用一点循环放大差距）
    constexpr int kIters = 2000000;
    volatile double sink = 0.0;  // volatile 防止整个循环被优化掉
    volatile double base = 1.000001;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIters; ++i) sink = std::pow(base, 2.0);
    const auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIters; ++i) sink = base * base;
    const auto t2 = std::chrono::steady_clock::now();
    const auto usPow =
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    const auto usMul =
        std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
    std::printf("\n  【实测】%d 次「平方」运算（本机实测，仅供参考）：\n", kIters);
    std::printf("    std::pow(x, 2.0) : %lld us\n", static_cast<long long>(usPow));
    std::printf("    x * x            : %lld us\n", static_cast<long long>(usMul));
    if (usMul > 0) {
        std::printf("    -> pow 约为连乘的 %.1f 倍耗时（Release 下差距同样明显）\n",
                    static_cast<double>(usPow) / static_cast<double>(usMul));
    } else {
        std::printf("    -> 连乘快到测不出来（几乎是 0 us），pow 却要 %lld us\n",
                    static_cast<long long>(usPow));
    }
    std::printf("    原因：pow 内部是 exp(y * log(x)) 的通用算法，还带一堆边界判断。\n");
    std::printf("    sink = %.20g（防止优化掉循环）\n", sink);
}

// ===========================================================================
// 8. 浮点判等
// ===========================================================================
void DemoCompare() {
    Section("8. 浮点判等：为什么不能用 ==");

    const double a = 0.1 + 0.2;
    const double b = 0.3;
    std::printf("  0.1 + 0.2 = %.20g\n", a);
    std::printf("  0.3       = %.20g\n", b);
    std::printf("  a == b ? %s\n", (a == b) ? "true" : "false");
    std::printf("  NearlyEqual(a, b) ? %s   <- 差值只有 %.3e\n", NearlyEqual(a, b) ? "true" : "false",
                std::fabs(a - b));
    std::printf("  原因：0.1 / 0.2 / 0.3 在二进制里都是无限循环小数，存进 double 就已经被舍入。\n");

    std::printf("\n  两种容差怎么选：\n");
    const double bigA = 1e15 + 0.1, bigB = 1e15;
    std::printf("    a = 1e15 + 0.1, b = 1e15，真实差值 = %.3f\n", std::fabs(bigA - bigB));
    std::printf("    绝对容差 1e-9 判定：%s  <- 误判为「不相等」\n",
                NearlyEqual(bigA, bigB) ? "相等" : "不相等");
    std::printf("    相对容差 1e-9 判定：%s  <- 正确\n",
                NearlyEqualRel(bigA, bigB) ? "相等" : "不相等");
    std::printf("    因为 1e15 附近的 double 间隔约 0.125，比 1e-9 大得多。\n");

    std::printf("\n  工程写法模板：\n");
    std::printf("    // 数值在 1 附近：绝对容差\n");
    std::printf("    bool eq1(double x, double y) { return std::fabs(x - y) < 1e-9; }\n");
    std::printf("    // 数值量级不确定：相对容差\n");
    std::printf("    bool eq2(double x, double y) {\n");
    std::printf("        return std::fabs(x - y) < 1e-9 * std::fmax(std::fabs(x), std::fabs(y)); }\n");
    std::printf("    // 判断「接近 0」：不要写 x == 0.0，写 std::fabs(x) < 1e-12\n");
}

// ===========================================================================
// 9. NaN 与 inf
// ===========================================================================
void DemoSpecialValues() {
    Section("9. NaN / inf 的检查");

    // 用 volatile 挡住编译期的常量折叠：直接写 1.0 / 0.0 会被 MSVC 判成
    // 编译错误 C2124（divide or mod by zero），因为编译器把它当成常量表达式。
    volatile double one = 1.0;
    volatile double zero = 0.0;
    const double inf = one / zero;   // ★ 浮点除以 0 不崩溃，得到 inf
    const double nan = zero / zero;  // ★ 0/0 得到 NaN

    std::printf("  1.0 / 0.0 = %s\n", Show(inf).c_str());
    std::printf("  0.0 / 0.0 = %s\n", Show(nan).c_str());
    std::printf("  std::isnan(nan)    = %s\n", std::isnan(nan) ? "true" : "false");
    std::printf("  std::isinf(inf)    = %s\n", std::isinf(inf) ? "true" : "false");
    std::printf("  std::isfinite(1.5) = %s\n", std::isfinite(1.5) ? "true" : "false");

    std::printf("\n  ★★ NaN 和谁都不相等，连自己都不等：\n");
    std::printf("    nan == nan -> %s\n", (nan == nan) ? "true" : "false");
    std::printf("    nan != nan -> %s\n", (nan != nan) ? "true" : "false");
    std::printf("    所以判断 NaN 【只能】用 std::isnan(x)，不能写 x == NAN。\n");
    std::printf("    （历史上有人写 x != x 当「是 NaN」的检测，能工作但可读性差，别学。）\n");

    // 更细的分类
    std::printf("\n  更细的分类（std::fpclassify 的返回值）：\n");
    std::printf("    fpclassify(nan) = %d (FP_NAN=%d)\n", std::fpclassify(nan), FP_NAN);
    std::printf("    fpclassify(inf) = %d (FP_INFINITE=%d)\n", std::fpclassify(inf), FP_INFINITE);
    std::printf("    fpclassify(0.0) = %d (FP_ZERO=%d)\n", std::fpclassify(0.0), FP_ZERO);
    std::printf("    fpclassify(1.5) = %d (FP_NORMAL=%d)\n", std::fpclassify(1.5), FP_NORMAL);
    std::printf("    std::isnormal(1.5) = %s，std::isnormal(0.0) = %s\n",
                std::isnormal(1.5) ? "true" : "false", std::isnormal(0.0) ? "true" : "false");

    // 工程套路：算完立刻检查
    std::printf("\n  工程套路「先算完立刻检查」：\n");
    const double inputs[] = {-4.0, 0.0, 16.0};
    for (const double x : inputs) {
        const double r = std::sqrt(x);
        std::printf("    sqrt(%6.1f) = %-16s isfinite=%s\n", x, Show(r).c_str(),
                    std::isfinite(r) ? "true" : "false");
    }
    std::printf("    不要等 NaN 一路传播到最后才去查 —— 那时已经找不到源头了。\n");

    std::printf("\n  ★ 整数除以 0 与浮点完全不同：整数 / 0 是未定义行为（Windows 上直接崩）。\n");
    std::printf("    0 作为除数、以及 INT_MIN / -1（商溢出）都是 UB，必须自己判。\n");
}

// ===========================================================================
// 10. 实战：三个「先校验再计算」的例子
// ===========================================================================
void SolveQuadratic(double a, double b, double c) {
    std::printf("  方程 %.6g x^2 + %.6g x + %.6g = 0：", a, b, c);
    if (NearlyEqual(a, 0.0)) {
        // ★ 第一件事永远是检查「这还是不是二次方程」
        if (NearlyEqual(b, 0.0)) {
            std::printf("a 和 b 都是 0，不是方程（c=%.6g）\n", c);
        } else {
            std::printf("a=0，退化成一次方程，根 x = %.6f\n", -c / b);
        }
        return;
    }
    const double delta = b * b - 4.0 * a * c;
    if (delta > kEps) {
        const double sq = std::sqrt(delta);  // ★ 只有 delta > 0 才敢开方
        std::printf("delta=%.6g>0，两个实根 x1=%.6f, x2=%.6f\n", delta, (-b + sq) / (2 * a),
                    (-b - sq) / (2 * a));
    } else if (std::fabs(delta) <= kEps) {
        std::printf("delta≈0，唯一实根 x = %.6f\n", -b / (2 * a));
    } else {
        std::printf("delta=%.6g<0，没有实根（直接 sqrt 会得到 NaN）\n", delta);
    }
}

void TriangleArea(double a, double b, double c) {
    std::printf("  三边 %.6g, %.6g, %.6g 的海伦公式面积：", a, b, c);
    // ★ 先判断能不能构成三角形，把非法输入挡在计算之前
    if (a <= 0.0 || b <= 0.0 || c <= 0.0 || a + b <= c || a + c <= b || b + c <= a) {
        std::printf("这三条边构不成三角形，直接返回错误\n");
        return;
    }
    const double s = (a + b + c) / 2.0;
    const double area = std::sqrt(s * (s - a) * (s - b) * (s - c));
    std::printf("半周长=%.6g，面积=%.6f\n", s, area);
}

void TriangleAngle(double a, double b, double c) {
    std::printf("  三边 %.6g, %.6g, %.6g 中边 c 的对角（余弦定理）：", a, b, c);
    if (a <= 0.0 || b <= 0.0 || c <= 0.0 || a + b <= c || a + c <= b || b + c <= a) {
        std::printf("不是合法三角形\n");
        return;
    }
    double cosC = (a * a + b * b - c * c) / (2.0 * a * b);
    // ★★ 关键防御：浮点误差可能让 cosC = 1.0000000000000002，而 acos 的定义域是 [-1,1]
    cosC = std::clamp(cosC, -1.0, 1.0);
    std::printf("cosC=%.15f -> 角 C=%.6f 度\n", cosC, ToDeg(std::acos(cosC)));
}

void DemoRealWorld() {
    Section("10. 实战：把「先校验」写进流程");

    SolveQuadratic(1.0, -3.0, 2.0);   // 两个实根
    SolveQuadratic(1.0, -2.0, 1.0);   // 重根
    SolveQuadratic(1.0, 0.0, 1.0);    // 无实根
    SolveQuadratic(0.0, 2.0, -4.0);   // 退化
    std::printf("\n");
    TriangleArea(3.0, 4.0, 5.0);
    TriangleArea(1.0, 1.0, 10.0);
    std::printf("\n");
    TriangleAngle(3.0, 4.0, 5.0);
    TriangleAngle(1.0, 1.0, 1.0);
}

}  // namespace

int main() {
    EnableUtf8Console();

    std::printf("==========================================================\n");
    std::printf(" 04_cmath.cpp —— <cmath> / <math.h>\n");
    std::printf("==========================================================\n");

    DemoConstants();
    DemoAbs();
    DemoRounding();
    DemoTrig();
    DemoExpLog();
    DemoFmod();
    DemoMisc();
    DemoCompare();
    DemoSpecialValues();
    DemoRealWorld();

    std::printf("\n================ 小结 ================\n");
    std::printf("1. 圆周率用 std::numbers::pi（C++20），不要依赖需要宏开关的 M_PI。\n");
    std::printf("2. 三角函数只认弧度；求方向角用 atan2(y, x)；acos 前一定要 clamp 到 [-1, 1]。\n");
    std::printf("3. abs 有整数版和浮点版，std::abs(INT_MIN) 是 UB，先提升类型。\n");
    std::printf("4. 四种取整方向不同，负数时差别最大；(total+perPage-1)/perPage 比 ceil 更适合分页。\n");
    std::printf("5. %% 只用于整数，浮点取余用 fmod；fmod 的符号跟着被除数。\n");
    std::printf("6. 浮点判等必须用容差；数值大时用相对容差。\n");
    std::printf("7. 数学函数越界返回 NaN/inf 而不报错，算完立刻 isfinite 检查。\n");
    std::printf("8. 平方写 x*x 而不是 pow(x, 2)：又快又准。\n");
    return 0;
}
