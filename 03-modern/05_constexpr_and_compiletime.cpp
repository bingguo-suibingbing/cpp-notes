// ============================================================================
//  05_constexpr_and_compiletime.cpp
// ----------------------------------------------------------------------------
//  演示主题：
//    1. const / constexpr / consteval / constinit 四者的区别与选择
//    2. constexpr 函数：同一个函数既能编译期求值，也能运行期求值
//    3. static_assert：把「约定」变成编译期错误
//    4. if constexpr 与普通 if 的本质差别
//    5. consteval 立即函数：强制只在编译期求值
//    6. 编译期查找表 / 字符串哈希的实际价值
//    7. std::array + constexpr 生成表
//    8. 编译期与运行期的边界：哪些标准库工具是 constexpr
//
//  关键结论：
//    - const 是「运行期不可改」，constexpr 是「编译期可求值」，两者语义正交。
//    - constexpr 函数不是「一定在编译期执行」，而是「允许在编译期执行」；
//      用 consteval 才能强制。
//    - if constexpr 会「丢弃」未选中分支（不实例化、不要求可编译），
//      普通 if 只是运行期分支，两侧都必须合法。
//    - 编译期算表的收益是「把启动期/首次调用的成本挪到编译期」，
//      代价是二进制变大、编译变慢；表越大越应该权衡。
// ============================================================================

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

// ---------------------------------------------------------------- constexpr 基础
constexpr int square(int x) { return x * x; }

// 同一个函数在两种语境下分别被编译期 / 运行期求值
constexpr int factorial(int n) {
    // C++14 起 constexpr 函数允许有局部变量、循环、分支
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

// 编译期错误：越界直接让编译失败，而不是运行期 UB
[[maybe_unused]] constexpr int checked_factorial(int n) {
    if (n < 0 || n > 12) {
        throw std::invalid_argument("factorial 参数超出可表示范围");  // 编译期求值时就是编译错误
    }
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

// ---------------------------------------------------------------- consteval
// 立即函数：只要出现调用，就必须在编译期求值，不允许退化成运行期调用
consteval int compile_time_only(int x) { return x * 3; }

// 用 consteval 做「编译期参数校验」是很实用的手段
consteval std::size_t require_power_of_two(std::size_t n) {
    if (n == 0 || (n & (n - 1)) != 0) {
        throw std::invalid_argument("必须是 2 的幂");
    }
    return n;
}

// ---------------------------------------------------------------- 编译期素数表
constexpr bool is_prime(std::size_t n) {
    if (n < 2) {
        return false;
    }
    for (std::size_t d = 2; d * d <= n; ++d) {
        if (n % d == 0) {
            return false;
        }
    }
    return true;
}

constexpr std::size_t kPrimeCount = 64;

// 用立即执行的 lambda 在编译期把表填满（constexpr lambda 从 C++17 起可用）
constexpr std::array<std::uint32_t, kPrimeCount> make_prime_table() {
    std::array<std::uint32_t, kPrimeCount> table{};
    std::size_t index = 0;
    std::uint32_t candidate = 2;
    while (index < kPrimeCount) {
        if (is_prime(candidate)) {
            table[index] = candidate;
            ++index;
        }
        ++candidate;
    }
    return table;
}

inline constexpr auto kPrimes = make_prime_table();

// 编译期验证表的正确性：这几个断言不成立就直接编译失败
static_assert(kPrimes[0] == 2, "第 0 个素数应为 2");
static_assert(kPrimes[5] == 13, "第 5 个素数应为 13");
static_assert(kPrimes[kPrimeCount - 1] == 311, "第 63 个素数应为 311");
static_assert(kPrimes.size() == kPrimeCount);

// 运行期对同一张表做二分查找：表是编译期产物，运行期零初始化成本
constexpr bool table_contains(std::uint32_t value) {
    std::size_t lo = 0;
    std::size_t hi = kPrimes.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (kPrimes[mid] == value) {
            return true;
        }
        if (kPrimes[mid] < value) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return false;
}

// ---------------------------------------------------------------- 编译期字符串哈希
// FNV-1a：实现简单、雪崩性够用，适合做「编译期算好的字符串 switch / 查表键」
constexpr std::uint64_t fnv1a(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const char c : text) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= 1099511628211ULL;
    }
    return hash;
}

// 用哈希把「字符串比较」变成「整数比较」，字符串长度不限
constexpr int command_id(std::string_view name) {
    switch (fnv1a(name)) {
        case fnv1a("start"):   return 1;
        case fnv1a("stop"):    return 2;
        case fnv1a("restart"): return 3;
        default:               return 0;
    }
}

// ---------------------------------------------------------------- if constexpr
// 通用「转字符串」：未选中的分支被丢弃，所以不会因为 v.size() 不存在而编译失败
template <typename T>
std::string stringify(const T& value) {
    if constexpr (std::is_arithmetic_v<T>) {
        return std::to_string(value);
    } else if constexpr (requires { value.size(); }) {  // C++20 requires 表达式
        return std::string(value.data(), value.size());
    } else {
        return "<无法自动转换>";
    }
}

// 对比：用普通 if 写同样的逻辑是编译不过的
template <typename T>
std::string stringify_with_plain_if(const T& value) {
    if (std::is_arithmetic_v<T>) {
        return std::to_string(value);
    }
    // 下面的分支对 int 也会被实例化，std::to_string(value) 仍会参与重载解析
    // 结论：普通 if 的两个分支都必须能编译
    return std::to_string(value);
}

// ---------------------------------------------------------------- constinit
// constinit 只保证「静态初始化」（编译期完成），不要求对象不可改。
// 它解决的是静态初始化顺序问题：避免在别的翻译单元的静态构造里用到未初始化的值。
constinit int g_startup_counter = 7;              // 可改，但一定在编译期就完成初始化
constexpr int g_compile_time_limit = 100;         // 编译期常量，不可改
const int g_runtime_const = factorial(4);         // const：不可改，但初始化可以发生在运行期

int main() {
    std::cout << "==== 1. const / constexpr / consteval / constinit ====\n";
    {
        int runtime_input = 5;
        const int a = runtime_input;        // 合法：const 只要求「之后不能改」
        std::cout << "  const int a = runtime_input;  -> a = " << a
                  << "（初始化表达式是运行期值）\n";

        constexpr int b = 5;                // 必须在编译期可求值
        std::cout << "  constexpr int b = 5;          -> b = " << b << "\n";
        // a = 6;  // 编译错误：const
        // constexpr int c = runtime_input;  // 编译错误：runtime_input 不是常量表达式

        constexpr int d = compile_time_only(4);  // consteval 必须在编译期求值
        std::cout << "  consteval 函数的结果 -> d = " << d << "\n";
        // int e = compile_time_only(runtime_input);  // 编译错误：实参非常量表达式

        g_startup_counter += 1;
        std::cout << "  constinit 变量可以改：g_startup_counter = " << g_startup_counter
                  << "，但它的初始化发生在编译期（无静态初始化顺序问题）\n";
        std::cout << "  const 运行期初始化示例：factorial(4) = " << g_runtime_const << "\n";

        constexpr std::size_t buffer_size = require_power_of_two(1024);
        std::cout << "  编译期校验参数：require_power_of_two(1024) = " << buffer_size << "\n";
        std::cout << "  提示：这行如果写 1000，编译就会失败，比运行期断言更早发现问题\n";
    }

    std::cout << "\n==== 2. constexpr 函数的双重身份 ====\n";
    {
        constexpr int compile_time_value = square(12);  // 编译期求值
        int runtime_input = 7;
        const int runtime_value = square(runtime_input);  // 同一个函数，运行期求值

        std::cout << "  square(12) 编译期 = " << compile_time_value << "\n";
        std::cout << "  square(runtime_input) 运行期 = " << runtime_value << "\n";
        std::cout << "  编译期求值的结果会进只读数据段，运行期零成本；"
                     "运行期调用就只是一次普通函数调用\n";

        // 注意别真的算出 32 位溢出：13! 已经超过 int 表示范围，属于有符号溢出（UB）
        const volatile int runtime_n = 12;
        std::cout << "  factorial(12) 运行期调用 = " << factorial(static_cast<int>(runtime_n))
                  << "（同一个函数还能在运行期用；再大就会溢出 int）\n";
    }

    std::cout << "\n==== 3. static_assert：把约定变成编译期错误 ====\n";
    {
        static_assert(sizeof(int) >= 4, "本示例假设 int 至少 4 字节");
        static_assert(factorial(5) == 120, "factorial(5) 应当是 120");
        static_assert(std::is_trivially_copyable_v<std::array<int, 4>>,
                      "std::array 应当是平凡可拷贝的");
        std::cout << "  所有 static_assert 都在编译期通过了（运行时这行才执行）\n";
        std::cout << "  用法：static_assert(sizeof(T) == 16, \"协议头大小必须为 16\") "
                     "—— 结构体布局漂移会立刻编译失败\n";
    }

    std::cout << "\n==== 4. if constexpr 与普通 if 的本质差别 ====\n";
    {
        std::cout << "  stringify(3.5)                = " << stringify(3.5) << "\n";
        std::cout << "  stringify(std::string(\"abc\")) = " << stringify(std::string("abc")) << "\n";

        struct NoStringify {
            int dummy{1};
        };
        std::cout << "  stringify(NoStringify{})      = " << stringify(NoStringify{})
                  << "（走到最后的 else 分支）\n";
        std::cout << "  if constexpr 丢弃未选中分支 -> 不实例化、不要求可编译，"
                     "模板才能对不同类型给出不同实现\n";
        std::cout << "  普通 if 只是运行期分支 -> 两侧都要合法，下面这个只能对算术类型用：\n";
        std::cout << "    stringify_with_plain_if(42) = " << stringify_with_plain_if(42) << "\n";
        std::cout << "  注意：if constexpr 在非模板函数里没有「丢弃」优势，"
                     "依然要求两侧可编译\n";
    }

    std::cout << "\n==== 5. 编译期查找表 ====\n";
    {
        std::cout << "  前 16 个素数（编译期生成，运行期零构建成本）：\n    ";
        for (std::size_t i = 0; i < 16; ++i) {
            std::cout << kPrimes[i] << ' ';
        }
        std::cout << "\n  第 63 个素数是 " << kPrimes[kPrimeCount - 1] << "\n";
        std::cout << "  table_contains(97)  = " << (table_contains(97) ? "true" : "false") << "\n";
        std::cout << "  table_contains(100) = " << (table_contains(100) ? "true" : "false") << "\n";
        std::cout << "  价值：is_prime 的运行期成本从「每次试除」变成「一次二分查找」；"
                     "也可以直接展开成布尔位图\n";
        std::cout << "  代价：表进只读数据段，二进制变大；表越大越要权衡\n";
    }

    std::cout << "\n==== 6. 编译期字符串哈希 ====\n";
    {
        constexpr std::uint64_t h_start = fnv1a("start");
        std::cout << "  fnv1a(\"start\") 编译期常量 = " << h_start << "\n";
        std::cout << "  command_id(\"start\")   = " << command_id("start") << "\n";
        std::cout << "  command_id(\"stop\")    = " << command_id("stop") << "\n";
        std::cout << "  command_id(\"restart\") = " << command_id("restart") << "\n";
        std::cout << "  command_id(\"unknown\") = " << command_id("unknown") << "\n";
        std::cout << "  价值：把一段 if/else 字符串比较换成一次整数 switch，"
                     "常用于配置项、命令行子命令、协议字段分派\n";
        std::cout << "  风险：哈希碰撞会让不同字符串走到同一分支，"
                     "所以关键分派仍要在分支内再校验一次原文\n";

        static_assert(command_id("restart") == 3, "编译期就能验证分派表正确");
    }

    std::cout << "\n==== 7. 编译期与运行期的边界 ====\n";
    std::cout << "  constexpr 的 std::array / std::string_view / std::pair / std::tuple：可以\n";
    std::cout << "  constexpr 的 std::vector / std::string（C++20）：可以常量求值，"
                 "但不能留在编译期结果里（编译期内存随求值结束释放）\n";
    std::cout << "  非 constexpr：iostream、动态分配 + 无法回收的所有权、虚函数调用（常量求值受限）\n";
    std::cout << "  判断方法：直接写 constexpr auto x = f(); 编译器会告诉你行不行\n";

    std::cout << "\n==== 小结 ====\n";
    std::cout << "  const     运行期不可改\n";
    std::cout << "  constexpr 可在编译期求值（也允许运行期）\n";
    std::cout << "  consteval 必须在编译期求值\n";
    std::cout << "  constinit 必须静态初始化（但不限制可改性）\n";
    std::cout << "  优先把「能算的」算在编译期；把「必须知道的」用 static_assert 钉死\n";
    return 0;
}
