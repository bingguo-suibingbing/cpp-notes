// ============================================================================
//  02_assert_and_contracts.cpp
// ----------------------------------------------------------------------------
//  演示主题：
//    1. assert 的真实语义：只在 Debug 生效（NDEBUG 一开就被编译掉），
//       因此它的表达式**绝不能有副作用**
//    2. 断言 vs 异常的界线：断言管「程序员写错了」（不可能发生），
//       异常管「世界不配合」（文件不存在、网络断了、用户输入非法）
//    3. 用断言表达前置条件 / 后置条件 / 类不变量
//    4. static_assert（编译期）vs assert（运行期 Debug）的分工
//    5. 自定义 CHECK 宏：带文件、行号、表达式文本，Debug 断点 + Release 记录
//    6. std::source_location（C++20）：不用宏也能拿到调用点
//    7. [[nodiscard]] + std::optional 表达「可能失败」
//    8. 什么时候绝不该用断言：用户输入、网络、磁盘、任何外部数据
//
//  关键结论：
//    - 断言是「可执行的文档」：它把「这个函数要求什么」写成机器能检查的形式。
//    - Release 版里 assert 会整个消失 —— 所以 `assert(do_something());` 是灾难。
//    - 判据很简单：**如果这段检查在 Release 里被删掉会导致行为错误，
//      那它就不是断言，是错误处理。**
//    - 对外部输入做断言，等于把「用户敲错一个字符」变成「程序崩溃」。
// ============================================================================

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

// ============================================================================
//  第 1 节：assert 的真实语义
// ----------------------------------------------------------------------------
//  语义（C 标准与 C++ 标准一致）：
//    - 条件为真：什么都不做（仅在 Debug 下做一次求值）
//    - 条件为假：打印「表达式文本 + 文件 + 行号」到 stderr，然后 abort()
//    - 定义了 NDEBUG：整个 assert 语句**在预处理阶段被替换成 ((void)0)**，
//      表达式根本不参与编译 —— 连语法错误都查不出来，副作用当然也没了。
//
//  推论（工程上极其重要）：
//    (a) assert 里的表达式不能有副作用：`assert(++i < n)` 在 Release 下 i 不增长。
//    (b) assert 里的表达式不能是「必须执行的检查」：`assert(p != nullptr)`
//        在 Release 下不检查，之后解引用就崩。
//    (c) assert 不能用来处理可预期的失败（用户输入、IO）。它一失败就是 abort，
//        连清理和错误提示的机会都没有。
// ============================================================================

// NDEBUG 是否定义，决定 assert 是否生效。这是「Debug / Release 行为不同」的
// 主要来源之一，也是最常见的「Debug 能跑、Release 崩」的原因。
const char* assert_state() {
#ifdef NDEBUG
    return "NDEBUG 已定义 -> assert 被整体消除（Release 语义）";
#else
    return "NDEBUG 未定义 -> assert 生效（Debug 语义）";
#endif
}

// 演示副作用陷阱：这个函数用「哨兵计数」证明 assert 里的表达式有没有被执行。
int g_side_effect_counter = 0;

int bump() {
    ++g_side_effect_counter;
    return 1;
}

void side_effect_demo() {
    g_side_effect_counter = 0;
    // assert 里调用有副作用的函数：Debug 下 g_side_effect_counter 会变成 1，
    // Release 下整个表达式被删掉，仍然是 0 —— 行为不一致，是 bug 的温床。
    // 这里写成 `assert(bump() == 1)` 只是演示，为了让两种配置都通过编译。
    assert(bump() == 1);
    std::cout << "  assert(bump() == 1) 之后，计数器 = " << g_side_effect_counter
              << "（Debug 应为 1，Release 为 0）\n";
}

// ============================================================================
//  第 2 节：断言 vs 异常 —— 界线怎么划
// ----------------------------------------------------------------------------
//  断言（assert / CHECK）用于：**违反了「不可能发生」的编程假设**
//    - 内部函数的前置条件（调用者必须已经保证指针非空）
//    - 类不变量（size_ <= capacity_、链表首尾指针一致）
//    - 后置条件（排序之后 a[i] <= a[i+1]）
//    - 「switch 走到这里说明枚举漏了」
//    → 失败代表「代码有 bug」，正确处理方式就是**尽快、响亮地挂掉**，
//      让开发者在 Debug 里看到调用栈，而不是带着坏状态继续跑。
//
//  异常（throw）用于：**可预期的、来自外部的失败**
//    - 文件打不开、网络断开、端口被占
//    - 用户输入非法、配置项缺失
//    - 内存不足
//    → 失败代表「环境不配合」，正确处理方式是**向上报告并让调用者决定**：
//      重试、降级、提示用户、记录日志。
//
//  一句话判据：
//    「如果这个条件不成立，是『我们写错了』还是『世界变了』？」
//     写错了 -> 断言；世界变了 -> 异常 / 返回值。
// ============================================================================

// 前置条件用断言：调用者传了空指针是编程错误，不是环境问题。
// 后置条件也用断言：函数自己承诺的结果，自己检查。
// [[nodiscard]] 让「必须使用返回值」也成为契约的一部分。
[[nodiscard]] double average_of(const std::vector<int>& v) {
    // 前置条件：空序列的平均值没有定义 —— 这是调用者的责任，用断言表达。
    // 注意：如果这个函数是要给别人处理「用户上传的任意数据」，
    // 那这里就不该断言，而应该返回 std::optional 或抛异常（见第 7 节）。
    assert(!v.empty() && "average_of 要求非空序列");

    std::int64_t sum = 0;
    for (const int x : v) {
        sum += x;
    }
    const double result = static_cast<double>(sum) / static_cast<double>(v.size());

    // 后置条件：平均值必须落在最小值和最大值之间，否则说明算错了。
    assert(result >= static_cast<double>(*std::min_element(v.begin(), v.end())));
    assert(result <= static_cast<double>(*std::max_element(v.begin(), v.end())));
    return result;
}

// ============================================================================
//  第 3 节：类不变量 —— 用私有成员 + 断言把「状态一定合法」变成事实
// ----------------------------------------------------------------------------
//  思路：把「合法状态」写成 check_invariant()，在**构造完成时、每次修改后**
//  调用。这样一旦某处把对象改坏，第一时间就在「改坏的那一行」附近暴露，
//  而不是几百行之后在完全无关的地方崩掉。
//
//  这也是「封装」的真正价值：成员私有 -> 所有修改都经过成员函数 ->
//  所有成员函数都能维护不变量 -> 不变量永远成立。
// ============================================================================

class FixedBuffer {
public:
    explicit FixedBuffer(std::size_t capacity)
        : data_(capacity, 0), size_(0) {  // 注意初始化顺序必须和声明顺序一致（否则报 C5038）
        check_invariant();                // 构造完成即校验：对象从出生起就是合法的
    }

    // 写入：对外部世界的「容量不够」用异常（可恢复、可预期）；
    // 对内部的「索引越界」用断言（调用者的编程错误）。
    void push_back(int value) {
        if (size_ == data_.size()) {
            throw std::length_error("FixedBuffer：容量已满，无法再写入");
        }
        assert(size_ < data_.size() && "push_back 内部错误：size_ 不该超过容量");
        data_[size_] = value;
        ++size_;
        check_invariant();
    }

    [[nodiscard]] int at(std::size_t index) const {
        assert(index < size_ && "at 的下标越界：这是调用者的编程错误");
        return data_[index];
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

private:
    // 不变量：0 <= size_ <= capacity_，且已写入位置之外的槽位保持初始值 0。
    void check_invariant() const {
        assert(size_ <= data_.size() && "不变量被破坏：size_ 超过了容量");
        for (std::size_t i = size_; i < data_.size(); ++i) {
            assert(data_[i] == 0 && "不变量被破坏：未写入的槽位必须是 0");
        }
    }

    std::vector<int> data_;
    std::size_t size_;
};

// ============================================================================
//  第 4 节：static_assert vs assert —— 一张表说清
// ----------------------------------------------------------------------------
//    | 维度       | static_assert            | assert                     |
//    |------------|--------------------------|----------------------------|
//    | 检查时机   | 编译期                   | 运行期（仅 Debug）         |
//    | Release 下 | 依然生效                 | 完全消失（NDEBUG）         |
//    | 运行期成本 | 零                       | Debug 下一次比较           |
//    | 能查什么   | 类型、常量、sizeof、契约 | 变量的实际取值、对象状态   |
//    | 失败表现   | 编译错误 + 自定义消息    | 打印 + abort()             |
//    | 典型用途   | 类型/平台/模板契约       | 前置/后置条件、不变量      |
//
//  两者互补：能编译期查的一律用 static_assert（免费、跑不掉），
//  只有依赖运行期数据（用户输入、文件内容、对象状态）才用 assert。
// ============================================================================

template <typename T>
class Range {
    static_assert(std::is_arithmetic_v<T>, "Range<T> 只支持算术类型：把 T 写清楚，别让模板报错刷屏");
    static_assert(std::is_trivially_copyable_v<T>, "Range<T> 需要 T 可以平凡复制（用于按值返回）");

public:
    Range(T lo, T hi) : lo_(lo), hi_(hi) {
        // 这里只能是 assert 而不是 static_assert：lo/hi 是运行期值。
        assert(lo_ <= hi_ && "Range 的前置条件：下界不能大于上界");
    }
    [[nodiscard]] bool contains(T v) const noexcept { return v >= lo_ && v <= hi_; }
    [[nodiscard]] T lo() const noexcept { return lo_; }
    [[nodiscard]] T hi() const noexcept { return hi_; }

private:
    T lo_;
    T hi_;
};

// ============================================================================
//  第 5 节：自定义 CHECK 宏 —— 比 assert 更好用的工程版
// ----------------------------------------------------------------------------
//  assert 的三个不足：
//    (a) Release 下完全消失，有些「不该发生但发生了要记录」的情况会丢信息；
//    (b) 不能附带上下文（比如「期望 size<=8，实际 12」）；
//    (c) 输出格式固定，接不进自己的日志系统。
//
//  工程版 CHECK 宏的常见做法：
//    - 打印文件、行号、函数名、表达式文本
//    - 允许附加上下文（CHECK_MSG）
//    - Debug 下 __debugbreak()（能把调试器停在这一行，看完整调用栈）
//    - Release 下也保留一条日志（不 abort，方便线上留证据）
//
//  注意：宏里绝不能有副作用求值两次的问题 —— 本实现只求值一次。
// ============================================================================

// 用一个「永远不会被优化掉」的输出函数，避免 Release 下日志被删。
void report_failure(std::string_view text) {
    // 统一走 std::cout，保证示例输出顺序与期望一致；真实项目里应写到 stderr 或日志文件。
    std::cout << text << '\n';
}

#ifdef NDEBUG
// Release：不断言崩溃，只留一条 ERROR 记录（线上「坏状态也要留证据」）
#define CHECK(expr)                                                          \
    do {                                                                     \
        if (!(expr)) {                                                       \
            report_failure(std::string("[CHECK-FAILED][Release] ") +         \
                           std::string(__FILE__) + ":" +                     \
                           std::to_string(__LINE__) + " : " #expr);          \
        }                                                                    \
    } while (false)
#else
// Debug：打印后直接断在调试器上，让你能立刻看到调用栈
#define CHECK(expr)                                                          \
    do {                                                                     \
        if (!(expr)) {                                                       \
            report_failure(std::string("[CHECK-FAILED][Debug] ") +           \
                           std::string(__FILE__) + ":" +                     \
                           std::to_string(__LINE__) + " : " #expr);          \
            __debugbreak();                                                  \
        }                                                                    \
    } while (false)
#endif

// 带上下文的版本：把「期望值 / 实际值」一起打出来，排查时省一次复现。
#define CHECK_EQ_MSG(a, b, msg)                                              \
    do {                                                                     \
        if (!((a) == (b))) {                                                 \
            report_failure(std::string("[CHECK-EQ] ") + std::string(__FILE__) + \
                           ":" + std::to_string(__LINE__) + " : " #a " == " #b \
                           " 失败 -> " + (msg));                             \
            __debugbreak();                                                  \
        }                                                                    \
    } while (false)

// ============================================================================
//  第 6 节：std::source_location（C++20）—— 不用宏也能知道调用点
// ----------------------------------------------------------------------------
//  宏的老问题是「难调试、难复用、污染命名空间」。C++20 的
//  std::source_location 允许用默认实参的方式，把「调用点的文件/行/函数」
//  自动传进来，于是可以写出真正的函数而不是宏。
//
//  限制：默认实参在**调用点**求值，所以必须是「调用者不传」的最后一个参数；
//  它拿到的永远是直接调用点，不是调用栈（要调用栈得用 std::stacktrace，C++23）。
// ============================================================================

void log_with_location(std::string_view message,
                       const std::source_location& loc = std::source_location::current()) {
    std::cout << "  [" << loc.file_name() << ":" << loc.line() << " in " << loc.function_name()
              << "] " << message << "\n";
}

// 更适合工程的形态：一个不依赖宏的「断言函数」，失败时打印精确调用点。
bool contract_check(bool condition, std::string_view what,
                    const std::source_location& loc = std::source_location::current()) {
    if (!condition) {
        report_failure(std::string("[CONTRACT] ") + loc.file_name() + ":" +
                       std::to_string(loc.line()) + " 契约失败：" + std::string(what));
    }
    return condition;
}

// ============================================================================
//  第 7 节：[[nodiscard]] + std::optional 表达「可能失败」
// ----------------------------------------------------------------------------
//  断言处理「不可能失败」，optional 处理「可能失败但是正常情况」。
//  这是「不用异常也不用错误码」的第三种选择：
//    - 调用者无法「忘记检查」：[[nodiscard]] 保证编译期提醒；
//    - 失败不是异常情况：没有异常开销，也没有异常语义的歧义；
//    - 代价：错误原因丢失（只有「没有值」）。要带原因就用 std::expected（C++23）。
// ============================================================================

[[nodiscard]] std::optional<std::size_t> find_index(const std::vector<int>& v, int target) noexcept {
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == target) {
            return i;
        }
    }
    return std::nullopt;  // 「没找到」是正常结果，不是错误
}

// ============================================================================
//  第 8 节：什么时候**绝不该**用断言
// ----------------------------------------------------------------------------
//  下面这些都是「世界变了」，不是「我们写错了」，一律用错误处理：
//
//    1. 用户输入：age 输入了 "abc"、邮箱格式不对
//       -> 断言会把「用户敲错字」变成程序崩溃，且 Release 下根本不检查。
//    2. 网络：connect 超时、对端提前关闭、返回了畸形报文
//       -> 这些每一秒都可能发生，属于正常业务路径。
//    3. 磁盘 / 文件：文件不存在、权限不足、磁盘写满
//       -> 必须能提示用户、能重试、能降级。
//    4. 配置文件 / 环境变量缺失
//       -> 要给出可读的错误信息和默认值，而不是 abort。
//    5. 第三方库的返回值、系统调用的 errno
//       -> 是合约边界，必须每次都检查（[[nodiscard]] + if）。
//
//  正确的做法：把「断言」放在**已经验证过的数据**上，
//  在系统边界（输入、IO、网络、第三方）做**真正的校验**，
//  校验通过之后内部就只依赖断言 —— 这就是「契约式设计」的分层。
// ============================================================================

// 边界处：返回错误信息，不 assert，不抛异常（调用者自己决定怎么处理）
[[nodiscard]] std::optional<int> parse_port(std::string_view text) {
    // 这里假设 text 来自用户/配置文件，因此必须完整校验，不能靠断言。
    if (text.empty() || text.size() > 5) {
        return std::nullopt;
    }
    int value = 0;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') {
            return std::nullopt;
        }
        value = value * 10 + (ch - '0');
    }
    if (value < 1 || value > 65535) {
        return std::nullopt;  // 端口范围校验：这是业务规则，不是断言
    }
    return value;
}

// 内部函数：参数已经过边界校验，于是可以放心用断言表达前置条件
[[nodiscard]] std::string format_port(int port) {
    assert(port >= 1 && port <= 65535 && "format_port 的前置条件：端口必须已校验");
    return "0.0.0.0:" + std::to_string(port);
}

void demo_assert_vs_error() {
    std::cout << "---- 8. 边界校验（对手）vs 内部断言（对自己）----\n";
    const std::string_view inputs[] = {"8080", "0", "70000", "abc", ""};
    for (const std::string_view in : inputs) {
        const std::optional<int> port = parse_port(in);
        if (port.has_value()) {
            std::cout << "  parse_port(\"" << in << "\") -> " << *port
                      << "，格式化后 = " << format_port(*port) << "\n";
        } else {
            std::cout << "  parse_port(\"" << in << "\") -> 失败（返回 nullopt，程序继续运行）\n";
        }
    }
    std::cout << "  结论：外部输入永远用返回值/异常处理；只有内部「已经校验过」的数据才用断言。\n\n";
}

}  // namespace

int main() {
    std::cout << "==== 02 断言与契约 ====\n\n";

    std::cout << "---- 1. assert 的语义 ----\n";
    std::cout << "  当前状态：" << assert_state() << "\n";
    side_effect_demo();
    std::cout << "  结论：assert 失败会打印「表达式 + 文件:行号」然后 abort()，\n";
    std::cout << "        没有任何清理、没有析构、无法被捕获 —— 所以它只适合「必须立刻停下」的 bug。\n\n";

    std::cout << "---- 2. 前置/后置条件用断言表达 ----\n";
    const std::vector<int> scores{60, 75, 88, 92, 100};
    const std::vector<int> single{42};
    std::cout << "  average_of({60,75,88,92,100}) = " << average_of(scores) << "\n";
    std::cout << "  average_of({42})             = " << average_of(single) << "\n";
    std::cout << "  结论：average_of({}) 会触发断言（Debug 下 abort），因为「空序列求平均」是编程错误；\n";
    std::cout << "        对外接口若必须接受空序列，则应改返回 optional 或抛异常。\n\n";

    std::cout << "---- 3. 类不变量 ----\n";
    FixedBuffer buf(4);
    buf.push_back(10);
    buf.push_back(20);
    std::cout << "  size = " << buf.size() << " / capacity = " << buf.capacity() << "\n";
    std::cout << "  at(1) = " << buf.at(1) << "\n";
    try {
        buf.push_back(30);
        buf.push_back(40);
        buf.push_back(50);  // 第 5 个：容量 4，抛 length_error
    } catch (const std::length_error& e) {
        std::cout << "  捕获异常：" << e.what() << "\n";
    }
    std::cout << "  结论：容量不够是「可预期的失败」-> 异常；下标越界是「调用者的错」-> 断言。\n";
    std::cout << "        每次修改后调用 check_invariant()，坏状态在发生的那一行附近就暴露。\n\n";

    std::cout << "---- 4. static_assert 与 assert 的分工 ----\n";
    const Range<int> valid(1, 100);
    std::cout << "  Range<int>(1, 100).contains(50) = " << std::boolalpha << valid.contains(50) << "\n";
    std::cout << "  Range<int>(1, 100).contains(200) = " << valid.contains(200) << std::noboolalpha << "\n";
    std::cout << "  结论：类型/常量契约 -> static_assert（编译期，Release 也在）；\n";
    std::cout << "        运行期取值 -> assert（Debug 生效）。\n\n";

    std::cout << "---- 5. 自定义 CHECK 宏 ----\n";
    CHECK(buf.size() == 4);
    CHECK(!buf.empty());
    CHECK_EQ_MSG(buf.capacity(), static_cast<std::size_t>(4), "容量应为 4");
    std::cout << "  三条 CHECK 全部通过（失败会打印文件:行号 + 表达式文本，Debug 下还会断在调试器）\n";
    std::cout << "  结论：CHECK 比 assert 多了上下文与行号，且 Release 下仍可留日志。\n\n";

    std::cout << "---- 6. std::source_location：不用宏也知道调用点 ----\n";
    log_with_location("这条日志自动带上了文件、行号、函数名");  // 看：调用点没有传任何位置参数
    const bool ok = contract_check(parse_port("8080").has_value(), "端口 8080 必须能解析");
    std::cout << "  contract_check 返回 " << std::boolalpha << ok << std::noboolalpha << "\n";
    std::cout << "  结论：source_location 是「宏的替代品」，能写进函数签名，可读性和可调试性都更好。\n\n";

    std::cout << "---- 7. [[nodiscard]] + std::optional 表达可能失败 ----\n";
    const std::vector<int> data{7, 13, 21, 34};
    if (const std::optional<std::size_t> idx = find_index(data, 21)) {
        std::cout << "  find_index(data, 21) = " << *idx << "\n";
    }
    std::cout << "  find_index(data, 99) = "
              << (find_index(data, 99).has_value() ? "有值" : "nullopt（正常结果，不是错误）") << "\n";
    std::cout << "  结论：忘记检查返回值在编译期就会被 [[nodiscard]] 拦下。\n\n";

    demo_assert_vs_error();

    std::cout << "---- 总结：断言 vs 异常，一句话判据 ----\n";
    std::cout << "  条件不成立时，是「我们写错了」还是「世界变了」？\n";
    std::cout << "    写错了   -> assert / CHECK（Debug 立刻挂，Release 记录）\n";
    std::cout << "    世界变了 -> 返回值 / optional / expected / 异常\n";
    std::cout << "  断言里的表达式永远不能有副作用；对外部数据永远不要断言。\n";
    std::cout << "\n==== 结论：断言是可执行的文档，写下来比写在注释里可靠 ====\n";
    return 0;
}
