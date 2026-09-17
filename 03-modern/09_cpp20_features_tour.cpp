// ============================================================================
//  09_cpp20_features_tour.cpp
// ----------------------------------------------------------------------------
//  演示主题（每个特性一个短小可运行例子）：
//    1. concepts：约束模板参数，把「模板错误」变成「一句人话」
//    2. <=> 三路比较运算符（spaceship operator）
//    3. std::format：比 printf 安全、比 iostream 好读
//    4. std::span：连续区间的非拥有视图
//    5. ranges：视图 + 管道
//    6. std::jthread：自动 join + 协作式取消
//    7. consteval：必须编译期求值
//    8. [[likely]] / [[unlikely]]：分支提示
//    9. [[nodiscard]]：忽略返回值要给出理由
//   10. using enum：把枚举成员引入作用域
//   11. 指定初始化（designated initializers）
//   12. std::bit_cast：类型双关的安全写法
//   13. std::source_location：替代 __FILE__ / __LINE__ 做日志
//
//  关键结论：
//    - 这些特性不是「炫技」，每个都对应一个真实工程痛点：
//      报错难读、格式化易错、所有权传递啰嗦、线程生命周期易错、日志宏丑陋。
//    - 采用顺序建议：concepts / format / ranges / source_location 收益最大，优先上；
//      [[likely]] 这类提示先别急着撒，没有 profile 数据就是噪声。
// ============================================================================

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <iterator>
#include <limits>
#include <numbers>
#include <optional>
#include <ranges>
#include <source_location>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// ---------------------------------------------------------------- 1. concepts
// 自定义 concept：要求类型支持 + 和 *，并且结果能转成 double
template <typename T>
concept Numeric = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
    { a * b } -> std::convertible_to<T>;
    requires std::is_arithmetic_v<T>;
};

// 用 concept 约束模板：不满足时编译器直接指出「哪个约束没满足」
template <Numeric T>
T doubled(T value) {
    return value + value;
}

// C++20 简写语法（constrained auto）等价于上面
Numeric auto halved(Numeric auto value) {
    return value / static_cast<decltype(value)>(2);
}

// ---------------------------------------------------------------- 2. <=>
struct Version {
    int major{};
    int minor{};
    int patch{};

    // 一行 <=> 自动生成 <, <=, >, >=，并且 == 也需要（或单独 default）
    constexpr auto operator<=>(const Version& other) const = default;
    constexpr bool operator==(const Version& other) const = default;
};

// 自定义 <=> 返回类别：partial_ordering 表示「可能不可比」
struct Temperature {
    double celsius{};
    constexpr std::partial_ordering operator<=>(const Temperature& other) const {
        return celsius <=> other.celsius;
    }
    constexpr bool operator==(const Temperature& other) const = default;
};

// ---------------------------------------------------------------- 3. std::format
template <typename... Args>
std::string log_line(std::string_view level, std::format_string<Args...> fmt, Args&&... args) {
    // 编译期检查格式串：类型不匹配是编译错误，不是运行期崩溃
    return std::format("[{}] {}", level, std::format(fmt, std::forward<Args>(args)...));
}

// ---------------------------------------------------------------- 4. span
int sum_values(std::span<const int> values) {
    int total = 0;
    for (const int v : values) {
        total += v;
    }
    return total;
}

// ---------------------------------------------------------------- 9. nodiscard
[[nodiscard]] int parse_or_throw(std::string_view text) {
    if (text.empty()) {
        throw std::invalid_argument("空输入");
    }
    return static_cast<int>(text.size());
}

// [[nodiscard]] 也可以带理由（C++20 起），编译器警告里会带上这句话
[[nodiscard("忽略错误码会导致故障被静默吞掉")]] int risky_operation(bool fail) {
    return fail ? -1 : 0;
}

// ---------------------------------------------------------------- 10. using enum
enum class Level { debug, info, warning, error };

std::string_view to_text(Level level) {
    using enum Level;  // 把枚举成员引入作用域，写 debug 而不是 Level::debug
    switch (level) {
        case debug:   return "DEBUG";
        case info:    return "INFO";
        case warning: return "WARN";
        case error:   return "ERROR";
    }
    return "UNKNOWN";
}

// ---------------------------------------------------------------- 11. 指定初始化
struct ServerConfig {
    std::string host{"127.0.0.1"};
    int port{8080};
    int timeout_ms{3000};
    bool use_tls{false};
};

// ---------------------------------------------------------------- 12. bit_cast
struct FloatBits {
    std::uint32_t raw;
};

// ---------------------------------------------------------------- 13. source_location
void log_with_location(std::string_view message,
                       const std::source_location& location = std::source_location::current()) {
    std::cout << "    " << location.file_name() << ":" << location.line() << " ("
              << location.function_name() << ") " << message << "\n";
}

// 日志封装里可以直接取到调用者的位置，这是 __FILE__ / __LINE__ 宏的主要替代动机
template <typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    log_with_location(std::format(fmt, std::forward<Args>(args)...));
}

int main() {
    std::cout << "==== 1. concepts ====\n";
    {
        std::cout << "  doubled(21)     = " << doubled(21) << "\n";
        std::cout << "  doubled(1.5)    = " << doubled(1.5) << "\n";
        std::cout << "  halved(84)      = " << halved(84) << "\n";

        // 标准库 concept 可以直接用在 static_assert 里
        static_assert(std::integral<int>);
        static_assert(!std::integral<double>);
        static_assert(std::floating_point<double>);
        static_assert(std::same_as<decltype(doubled(1)), int>);
        std::cout << "  标准库 concept: integral / floating_point / same_as / convertible_to ...\n";

        // 如果写 doubled(std::string("x"))，编译错误会直接说「约束 Numeric<std::string> 不满足」，
        // 而不是展开几百行模板实例化信息 —— 这才是 concepts 最大的工程价值
        std::cout << "  价值：把「模板报错难读」变成「一句人话的错误信息」\n";
    }

    std::cout << "\n==== 2. <=> 三路比较 ====\n";
    {
        constexpr Version a{1, 2, 3};
        constexpr Version b{1, 10, 0};
        static_assert((a <=> b) < 0);
        static_assert(a < b);          // 由 <=> 自动生成
        static_assert(b > a);          // 由 <=> 自动生成
        static_assert(a == Version{1, 2, 3});  // 由 default 的 == 生成

        std::cout << "  Version{1,2,3} < Version{1,10,0} ? " << (a < b ? "true" : "false") << "\n";
        std::cout << "  <=> 返回值的类型告诉你比较强度：\n";
        std::cout << "    strong_ordering  ：可完全比较（整数、字符串）\n";
        std::cout << "    weak_ordering    ：等价但不相等（忽略大小写比较）\n";
        std::cout << "    partial_ordering ：可能不可比（浮点 NaN）\n";

        const Temperature cold{std::numbers::pi * -10.0};
        const Temperature hot{30.0};
        const Temperature nan{std::numeric_limits<double>::quiet_NaN()};
        std::cout << "  cold < hot  = " << (cold < hot ? "true" : "false") << "\n";
        std::cout << "  nan < hot   = " << (nan < hot ? "true" : "false")
                  << "，nan > hot = " << (nan > hot ? "true" : "false")
                  << "（都 false -> 不可比，这就是 partial_ordering）\n";
        std::cout << "  工程收益：比较运算符只需维护一处，容器排序 / 去重自动跟着对\n";
    }

    std::cout << "\n==== 3. std::format ====\n";
    {
        const std::string name = "张三";
        const int age = 31;
        const double pi = std::numbers::pi;

        std::cout << "  " << std::format("姓名={}，年龄={}", name, age) << "\n";
        std::cout << "  " << std::format("pi = {:.3f}", pi) << "\n";
        std::cout << "  " << std::format("十六进制 = {:#x}，二进制 = {:#010b}", 255, 5) << "\n";
        std::cout << "  " << std::format("宽度对齐 = [{:>8}]，左对齐 = [{:<8}]，居中 = [{:^9}]",
                                         "id", "id", "id") << "\n";
        std::cout << "  " << std::format("补零 = {:08d}，带符号 = {:+d}", 42, 42) << "\n";
        std::cout << "  " << std::format("复用参数 = {0} 和 {0}，第二个 = {1}", "same", 2) << "\n";

        const std::string log = log_line("INFO", "用户 {} 登录，端口 {}", name, 8080);
        std::cout << "  " << log << "\n";

        // 运行期格式串必须用 vformat（编译期检查做不了）
        std::cout << "  运行期格式串："
                  << std::vformat("来自配置的模板 = {}", std::make_format_args(age)) << "\n";

        std::cout << "  对比：\n";
        std::cout << "    printf : 类型不匹配是 UB / 崩溃，参数个数靠人眼核对\n";
        std::cout << "    iostream: 类型安全但拼接冗长、格式控制要记操纵符\n";
        std::cout << "    format : 编译期检查格式串 + 位置参数 + 类型安全（本示例就是）\n";
        std::cout << "  注意：MSVC 需要 /std:c++20 且较新版本标准库；"
                     "std::println 属于 C++23，本环境未提供\n";
    }

    std::cout << "\n==== 4. std::span ====\n";
    {
        int raw[4] = {1, 2, 3, 4};
        const std::vector<int> dynamic{10, 20, 30};
        const std::array<int, 3> fixed{5, 5, 5};

        std::cout << "  sum_values(C 数组)   = " << sum_values(raw) << "\n";
        std::cout << "  sum_values(vector)   = " << sum_values(dynamic) << "\n";
        std::cout << "  sum_values(array)    = " << sum_values(fixed) << "\n";
        const std::span<const int> view(raw);
        std::cout << "  first(2) / last(2)   = " << sum_values(view.first(2)) << " / "
                  << sum_values(view.last(2)) << "\n";
        std::cout << "  subspan(1, 2)        = " << sum_values(view.subspan(1, 2)) << "\n";
        std::cout << "  sizeof(span) = " << sizeof(view) << " 字节：指针 + 长度，零开销\n";
    }

    std::cout << "\n==== 5. ranges ====\n";
    {
        const std::vector<int> data{5, 12, 7, 20, 3, 18};
        std::cout << "  iota(1, 6) | transform(平方)：";
        for (const int v : std::views::iota(1, 6) | std::views::transform([](int x) { return x * x; })) {
            std::cout << v << ' ';
        }
        std::cout << "\n  data | filter(>10) | take(2)：";
        for (const int v : data | std::views::filter([](int x) { return x > 10; })
                 | std::views::take(2)) {
            std::cout << v << ' ';
        }
        std::cout << "\n  data | reverse | take(3)：";
        for (const int v : data | std::views::reverse | std::views::take(3)) {
            std::cout << v << ' ';
        }
        std::cout << "\n  ranges 算法不需要 begin()/end()：ranges::max_element(data) = "
                  << *std::ranges::max_element(data) << "\n";
        std::cout << "  价值：可读性 + 惰性求值（不分配中间容器）+ 可组合\n";
    }

    std::cout << "\n==== 6. std::jthread ====\n";
    {
        {
            std::jthread worker([](std::stop_token token) {
                int ticks = 0;
                while (!token.stop_requested()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(15));
                    ++ticks;
                }
                std::cout << "    [jthread] 收到停止请求，退出前跑了 " << ticks << " 次\n";
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            worker.request_stop();
        }  // 离开作用域自动 join，不会 terminate
        std::cout << "  jthread 已自动 join（作用域退出），无需手写 join\n";
        std::cout << "  工程收益：异常路径也不会漏 join；stop_token 让「取消」变成类型安全的协议\n";
    }

    std::cout << "\n==== 7. consteval ====\n";
    {
        constexpr int computed = []() consteval { return 6 * 7; }();
        std::cout << "  consteval lambda 编译期结果 = " << computed << "\n";
        static_assert(computed == 42);

        // 立即函数不能用在运行期语境：
        // const int runtime_value = 3;
        // consteval_fn(runtime_value);  // 编译错误
        std::cout << "  与 constexpr 的区别：constexpr 允许运行期调用，consteval 强制编译期\n";
        std::cout << "  用途：编译期校验参数（缓冲区大小必须是 2 的幂）、生成常量表\n";
    }

    std::cout << "\n==== 8. [[likely]] / [[unlikely]] ====\n";
    {
        int error_hits = 0;
        for (int i = 0; i < 100; ++i) {
            if (i == 42) [[unlikely]] {
                ++error_hits;   // 罕见路径：提示编译器把它排到分支末尾
            } else [[likely]] {
                // 常见路径：提示编译器优先布局
            }
        }
        std::cout << "  [[unlikely]] 分支命中 " << error_hits << " 次\n";
        std::cout << "  实用建议：这些提示只在 profile 显示分支预测失败是瓶颈时才加；\n";
        std::cout << "            乱加会让代码更难读，收益通常小于 1%，"
                     "现代 CPU 的动态预测往往更懂你的数据\n";
    }

    std::cout << "\n==== 9. [[nodiscard]] ====\n";
    {
        const int length = parse_or_throw("hello");
        std::cout << "  parse_or_throw(\"hello\") = " << length << "\n";
        std::cout << "  如果写成 parse_or_throw(\"hello\"); 丢弃返回值，编译器会给出警告\n";

        const int code = risky_operation(false);
        if (code != 0) {
            std::cout << "  错误码 = " << code << "\n";
        } else {
            std::cout << "  [[nodiscard]] 保证调用者至少「看见」了返回值（本示例检查了 code）\n";
        }
        std::cout << "  适用：错误码、工厂函数返回的指针、lock()、capacity() 这类「不看就有坑」的返回值\n";
    }

    std::cout << "\n==== 10. using enum ====\n";
    for (const Level level : {Level::debug, Level::info, Level::warning, Level::error}) {
        std::cout << "  " << to_text(level) << "\n";
    }
    std::cout << "  价值：switch 里不用重复写 Level:: 前缀，同时保留 enum class 的类型安全\n";

    std::cout << "\n==== 11. 指定初始化 ====\n";
    {
        // 按声明顺序初始化，跳过成员用其默认值；可读性远好于位置初始化
        const ServerConfig config{
            .host = "0.0.0.0",
            .port = 9090,
            .use_tls = true,
        };
        std::cout << "  host=" << config.host << ", port=" << config.port
                  << ", timeout=" << config.timeout_ms << "（未指定 -> 用默认值）"
                  << ", tls=" << (config.use_tls ? "true" : "false") << "\n";
        std::cout << "  规则：顺序必须与声明一致；不能跳过然后回头再写前面的成员；\n";
        std::cout << "        这让「新增字段」不会静默改变既有初始化的含义\n";
    }

    std::cout << "\n==== 12. std::bit_cast ====\n";
    {
        const float value = 1.0f;
        const auto bits = std::bit_cast<std::uint32_t>(value);   // 编译期即可完成
        const auto back = std::bit_cast<float>(bits);
        static_assert(std::bit_cast<std::uint32_t>(1.0f) == 0x3F800000U);
        std::cout << "  bit_cast<uint32_t>(1.0f) = 0x" << std::hex << bits << std::dec
                  << "（十六进制输出，之后恢复十进制）\n";
        std::cout << "  bit_cast 回去 = " << back << "（IEEE 754 单精度的 1.0 就是 0x3F800000）\n";

        const FloatBits packed{bits};
        std::cout << "  与 memcpy / union 双关相比：bit_cast 是 constexpr 友好、无别名违规、"
                     "要求两类型大小相同（否则编译错误）\n";
        std::cout << "  packed.raw = 0x" << std::hex << packed.raw << std::dec << "\n";
        std::cout << "  用途：协议序列化、浮点比较技巧、位运算密集的编解码\n";
    }

    std::cout << "\n==== 13. std::source_location ====\n";
    {
        log_with_location("直接调用：位置就是这一行");
        info("带格式参数的日志：用户 {} 在端口 {}", "李四", 8080);
        std::cout << "  价值：日志函数不需要宏就能拿到「调用者」的文件/行/函数名\n";
        std::cout << "  对比 __FILE__/__LINE__：宏会污染调用点、无法类型安全地加参数、"
                     "无法放进命名空间里的普通函数\n";
        std::cout << "  注意：这是编译期常量，零运行期开销，但路径可能包含构建机绝对路径，"
                     "发布前要裁剪\n";
    }

    std::cout << "\n==== 采用建议（按投入产出排序）====\n";
    std::cout << "  立刻用起来 ：std::format、std::span、source_location、concepts、"
                 "ranges（基础管道）\n";
    std::cout << "  按需使用   ：<=>、jthread、designated initializers、using enum、bit_cast、"
                 "[[nodiscard]]\n";
    std::cout << "  谨慎使用   ：[[likely]] / [[unlikely]]（没有 profile 数据就别写）\n";
    std::cout << "  注意可移植 ：std::format / ranges / jthread 需要较新的标准库实现，"
                 "老工具链上要准备降级方案\n";
    return 0;
}
