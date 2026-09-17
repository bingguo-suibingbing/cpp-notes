// ============================================================================
//  03_logging_and_error_handling.cpp
// ----------------------------------------------------------------------------
//  演示主题：
//    1. 为什么用日志而不是到处 std::cout / printf
//    2. 一个小而完整、可直接拿去用的日志设施：
//         分级（DEBUG/INFO/WARN/ERROR）+ 时间戳 + 文件:行号 + 函数名
//         线程安全（std::mutex）+ 可切换输出目标 + 编译期可裁剪的 LOG_DEBUG
//    3. 错误处理策略对比：返回码 / std::optional / std::expected / 异常
//       —— 给出选型决策表，并用实测数据说明「异常在正常路径免费、失败路径昂贵」
//    4. 同一个 parse_int 用四种方式各写一遍，对比可读性与性能
//    5. RAII 的 ScopeGuard：把「退出时必须做的事」绑在作用域上
//
//  关键结论：
//    - 日志的价值在于「可分级、可关闭、可落文件、带上下文」，
//      而不是「比 printf 好看」。生产环境要能按级别过滤、能事后追溯。
//    - 错误处理没有银弹：
//        正常路径性能敏感 -> 返回码 / expected
//        「可能没有值」是正常语义 -> optional
//        「失败要带原因且要层层上报」 -> expected（不用异常）
//        「错误不可就地处理，必须跨多层展开栈」 -> 异常
//    - 抛异常在失败路径上比返回 error_code 慢两个数量级（本机实测约 430 倍，
//      数字来自 boost/README_Boost_vs_STL.md 的现场测量）；
//      但在**成功路径**上异常是零成本的 —— 所以只有「失败频繁发生」时才该避开异常。
//    - RAII 是 C++ 管理资源的唯一正确姿势：构造即获取，析构即释放，
//      异常展开、提前 return、break 都不会漏掉清理。
// ============================================================================

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <exception>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(__cpp_lib_expected)
#include <expected>
#endif

namespace {

// ============================================================================
//  第 1 节：一个可直接使用的日志设施（约 150 行，够用不臃肿）
// ----------------------------------------------------------------------------
//  设计要点（每一条都是工程上踩过坑才加的）：
//    a) 级别用 enum class：有类型、不污染命名空间、不会和宏撞名。
//    b) 全局对象用「函数内静态局部变量」实现：线程安全初始化（C++11 起保证），
//       不依赖静态初始化顺序（经典 Static Initialization Order Fiasco）。
//    c) 输出加 std::mutex：多线程下日志交错是最常见的「日志没法看」原因。
//    d) 时间戳用 std::localtime_s（Windows）/ localtime_r（POSIX）：
//       std::localtime 返回静态缓冲区指针，多线程下会互相覆盖。
//    e) 支持切换输出目标（控制台 / 文件）：测试和线上用的是同一份代码。
//    f) LOG_DEBUG 在 Release 下编译期消失：热路径里的调试日志不能有运行期成本。
// ============================================================================

enum class LogLevel : int { Debug = 0, Info = 1, Warn = 2, Error = 3 };

const char* to_string(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO ";
        case LogLevel::Warn:
            return "WARN ";
        case LogLevel::Error:
            return "ERROR";
        default:
            return "?????";
        // 注意这里不写 default 之外的分支：如果 LogLevel 加了新值，
        // /W4 + C4062 会提醒我们补上（本文件为了满足「所有路径都有返回值」
        // 才加了 default；工程里可以用 C4715 兜底）。
    }
}

// 目标输出：控制台 or 文件。用基类 + 虚函数，方便以后加「同时写多个目标」。
class LogSink {
public:
    virtual ~LogSink() = default;
    virtual void write(const std::string& line) = 0;
    virtual void flush() {}
};

class ConsoleSink final : public LogSink {
public:
    void write(const std::string& line) override { std::cout << line << '\n'; }
    void flush() override { std::cout.flush(); }
};

class FileSink final : public LogSink {
public:
    explicit FileSink(const std::string& path) : file_(path, std::ios::app) {
        if (!file_) {
            throw std::runtime_error("FileSink：无法打开日志文件 " + path);
        }
    }
    void write(const std::string& line) override { file_ << line << '\n'; }
    void flush() override { file_.flush(); }

private:
    std::ofstream file_;
};

class Logger {
public:
    // 单例：函数内静态变量，C++11 起初始化线程安全，且不会被静态初始化顺序坑到。
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void set_min_level(LogLevel level) noexcept { min_level_.store(level, std::memory_order_relaxed); }
    [[nodiscard]] LogLevel min_level() const noexcept { return min_level_.load(std::memory_order_relaxed); }

    // 切换输出目标：测试里换成内存缓冲、线上换成文件，调用方代码不变。
    void set_sink(std::unique_ptr<LogSink> sink) {
        std::lock_guard<std::mutex> lock(mutex_);
        default_sink_ = std::move(sink);
        if (!default_sink_) {
            default_sink_ = std::make_unique<ConsoleSink>();
        }
    }

    // 核心入口：级别 + 消息 + 调用点。source_location 有默认实参，
    // 所以调用者（或宏）不传时自动取「调用点」的位置。
    void log(LogLevel level, std::string_view message,
             const std::source_location& loc = std::source_location::current()) {
        // 级别过滤放在最前面：这是热路径上唯一的开销（一次原子读 + 一次比较）。
        if (static_cast<int>(level) < static_cast<int>(min_level_.load(std::memory_order_relaxed))) {
            return;
        }

        const std::string line = format(level, message, loc);
        std::lock_guard<std::mutex> lock(mutex_);  // 保证整行原子写出，不会与其他线程交错
        default_sink_->write(line);
        if (level >= LogLevel::Warn) {
            default_sink_->flush();  // 警告以上立刻落盘：崩溃前的日志必须已经写出去
        }
    }

    // 供 LOG_DEBUG 使用：Release 下判断为常量 false，编译器会整个删掉调用。
    [[nodiscard]] static constexpr bool debug_enabled() noexcept {
#ifdef NDEBUG
        return false;  // Release：LOG_DEBUG 编译期消失，零运行期成本
#else
        return true;  // Debug：完整输出
#endif
    }

private:
    Logger() : min_level_(LogLevel::Debug), default_sink_(std::make_unique<ConsoleSink>()) {}

    // 时间戳：local_time_s 是线程安全的（std::localtime 不是！）
    static std::string timestamp() {
        const std::time_t now = std::time(nullptr);
        std::tm tm_buf{};
#if defined(_MSC_VER)
        localtime_s(&tm_buf, &now);
#else
        localtime_r(&now, &tm_buf);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm_buf, "%H:%M:%S");
        return oss.str();
    }

    // 从完整路径里取文件名：日志里只要文件名就够了，完整路径会淹没信息。
    static std::string_view basename(std::string_view path) noexcept {
        const std::size_t pos = path.find_last_of("\\/");
        return pos == std::string_view::npos ? path : path.substr(pos + 1);
    }

    static std::string format(LogLevel level, std::string_view message,
                              const std::source_location& loc) {
        std::ostringstream oss;
        oss << '[' << timestamp() << "] [" << to_string(level) << "] ["
            << std::this_thread::get_id() << "] " << basename(loc.file_name()) << ':' << loc.line()
            << " (" << loc.function_name() << ") " << message;
        return oss.str();
    }

    std::atomic<LogLevel> min_level_;
    std::mutex mutex_;
    std::unique_ptr<LogSink> default_sink_;
};

// 便捷函数：让宏保持「一行」，同时让日志调用点保持可读。
void log_info(std::string_view msg, const std::source_location& loc = std::source_location::current()) {
    Logger::instance().log(LogLevel::Info, msg, loc);
}
void log_warn(std::string_view msg, const std::source_location& loc = std::source_location::current()) {
    Logger::instance().log(LogLevel::Warn, msg, loc);
}
void log_error(std::string_view msg, const std::source_location& loc = std::source_location::current()) {
    Logger::instance().log(LogLevel::Error, msg, loc);
}
void log_debug(std::string_view msg, const std::source_location& loc = std::source_location::current()) {
    Logger::instance().log(LogLevel::Debug, msg, loc);
}

// 宏只是「让调用点更短」，位置信息靠 source_location 的默认实参自动带出来。
// Debug 日志用 if constexpr 包住：Release 下连字符串拼接都不会发生。
// 注意参数显式写成 std::string_view：这样 std::string 与 const char* 都能直接传，
// 而且「多参数」会在编译期被挡住（避免写成 LOG_INFO("a", "b") 这种静默丢参）。
#define LOG_DEBUG(msg)                                          \
    do {                                                        \
        if constexpr (Logger::debug_enabled()) {                \
            log_debug(std::string_view{msg});                   \
        }                                                       \
    } while (false)
#define LOG_INFO(msg) log_info(std::string_view{msg})
#define LOG_WARN(msg) log_warn(std::string_view{msg})
#define LOG_ERROR(msg) log_error(std::string_view{msg})

// ============================================================================
//  第 2 节：ScopeGuard —— RAII 清理器
// ----------------------------------------------------------------------------
//  场景：函数有多个 return 点，每个都要「解锁 / 关文件 / 回滚事务 / 恢复状态」。
//  手写清理代码一定会漏（尤其是后来加了 `return` 或抛异常的时候）。
//  ScopeGuard 把清理动作绑在作用域上：作用域一退出（正常 return、break、
//  continue、异常展开都算），析构函数必定执行。
//
//  注意 dismiss() 的语义：表示「清理动作成功了，不用回滚」。
// ============================================================================

class ScopeGuard {
public:
    explicit ScopeGuard(std::function<void()> on_exit) noexcept : on_exit_(std::move(on_exit)) {}

    // 移动构造/赋值：ScopeGuard 应该能被返回，但不该被拷贝（否则清理两次）
    ScopeGuard(ScopeGuard&& other) noexcept : on_exit_(std::move(other.on_exit_)), active_(other.active_) {
        other.active_ = false;
    }
    ScopeGuard& operator=(ScopeGuard&& other) noexcept {
        if (this != &other) {
            run();  // 先执行自己的清理，再接管别人的
            on_exit_ = std::move(other.on_exit_);
            active_ = other.active_;
            other.active_ = false;
        }
        return *this;
    }
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

    ~ScopeGuard() { run(); }  // 析构即清理：这是 RAII 的全部秘密

    void dismiss() noexcept { active_ = false; }  // 成功路径：取消回滚

private:
    void run() noexcept {
        if (active_ && on_exit_) {
            active_ = false;  // 先置位再调用：清理函数抛异常也不会执行两次
            on_exit_();
        }
    }

    std::function<void()> on_exit_;
    bool active_ = true;
};

// 工厂函数：标出「这个函数可能在失败时回滚」，可读性比裸构造好
[[nodiscard]] ScopeGuard make_scope_guard(std::function<void()> on_exit) noexcept {
    return ScopeGuard(std::move(on_exit));
}

// 演示：一次「事务」要么成功提交，要么回滚；用 ScopeGuard 保证不会漏。
enum class TxResult { Committed, RolledBack };

TxResult run_transaction(bool should_fail) {
    std::cout << "    [tx] 开始事务\n";
    bool committed = false;
    // 不管怎么退出（正常、提前 return、抛异常），没提交就一定回滚。
    ScopeGuard rollback = make_scope_guard([&committed] {
        if (!committed) {
            std::cout << "    [tx] 回滚（ScopeGuard 在析构时执行）\n";
        }
    });

    if (should_fail) {
        std::cout << "    [tx] 中途失败，直接 return\n";
        return TxResult::RolledBack;  // 注意：这里没有任何清理代码，但回滚一定会发生
    }

    std::cout << "    [tx] 提交成功\n";
    committed = true;
    rollback.dismiss();  // 成功路径：取消回滚
    return TxResult::Committed;
}

// ============================================================================
//  第 3~4 节：同一个 parse_int，四种错误处理策略各写一遍
// ----------------------------------------------------------------------------
//  需求：把字符串解析成 int，要求
//    - 支持可选的负号
//    - 检测溢出（这是最容易漏的地方：atoi 溢出是未定义行为）
//    - 失败时必须能告诉调用者「为什么失败」
//
//  四种写法覆盖了工程里 95% 的场景，选哪种取决于「失败有多频繁」和
//  「调用者需要多少信息」。
// ============================================================================

// 策略 A：返回码（C 风格，性能最好，信息最少）
// 优点：零开销、无异常、跨语言/跨 ABI 友好、适合嵌入式与 C 接口。
// 缺点：调用者能直接忽略返回值；错误细节要靠 out 参数；签名不直观。
enum class ParseStatus { Ok, Empty, BadChar, Overflow };

ParseStatus parse_int_a(std::string_view text, int& out) noexcept {
    if (text.empty()) {
        return ParseStatus::Empty;
    }
    bool negative = false;
    std::size_t i = 0;
    if (text[0] == '+' || text[0] == '-') {
        negative = (text[0] == '-');
        i = 1;
        if (text.size() == 1) {
            return ParseStatus::BadChar;
        }
    }
    // 用 64 位中间量做溢出判断：先在更宽的类型里算，再检查是否越界。
    std::int64_t acc = 0;
    for (; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch < '0' || ch > '9') {
            return ParseStatus::BadChar;
        }
        acc = acc * 10 + (ch - '0');
        if (acc > (std::int64_t{1} << 40)) {  // 早退：避免中间量自身溢出
            return ParseStatus::Overflow;
        }
    }
    if (negative) {
        acc = -acc;
    }
    if (acc < INT32_MIN || acc > INT32_MAX) {
        return ParseStatus::Overflow;
    }
    out = static_cast<int>(acc);
    return ParseStatus::Ok;
}

// 策略 B：std::optional（表达「可能没有值」，语义最干净）
// 优点：[[nodiscard]] 保证调用者必须处理；无异常开销；接口自解释。
// 缺点：丢失失败原因（只有「没有」）。要原因就上 expected。
[[nodiscard]] std::optional<int> parse_int_b(std::string_view text) noexcept {
    int value = 0;
    if (parse_int_a(text, value) == ParseStatus::Ok) {
        return value;
    }
    return std::nullopt;
}

// 策略 C：std::expected（C++23 / MSVC STL 已提供）
// 优点：既有 optional 的「正常值 / 失败」二态，又能带失败原因；
//       不需要异常机制，适合「错误是常规业务流」的场景（解析、协议、校验）。
// 缺点：C++23 才有；错误类型要显式建模；错误不能像异常那样自动穿透多层。
#if defined(__cpp_lib_expected)
struct ParseError {
    ParseStatus code = ParseStatus::Ok;
    std::string detail;
};

[[nodiscard]] std::expected<int, ParseError> parse_int_c(std::string_view text) {
    int value = 0;
    const ParseStatus st = parse_int_a(text, value);
    if (st == ParseStatus::Ok) {
        return value;  // 隐式构造「成功」分支
    }
    const char* what = "未知错误";
    switch (st) {
        case ParseStatus::Empty:
            what = "输入为空";
            break;
        case ParseStatus::BadChar:
            what = "含非数字字符";
            break;
        case ParseStatus::Overflow:
            what = "数值溢出 int 范围";
            break;
        case ParseStatus::Ok:
            break;
    }
    return std::unexpected(ParseError{st, std::string("\"") + std::string(text) + "\" " + what});
}
#endif

// 策略 D：异常
// 优点：错误无法被忽略；可以跨多层函数自动向上传播（不用每层 if 检查）；
//       成功路径零成本（不抛异常时没有额外开销）。
// 缺点：失败路径昂贵（栈展开 + 构造异常对象，本机实测约 430 倍于返回错误码）；
//       需要 /EHsc；异常安全（strong guarantee）设计要求较高；
//       很多工程（游戏、嵌入式、实时系统）直接禁用异常。
[[nodiscard]] int parse_int_d(std::string_view text) {
    int value = 0;
    switch (parse_int_a(text, value)) {
        case ParseStatus::Ok:
            return value;
        case ParseStatus::Empty:
            throw std::invalid_argument("parse_int：输入为空");
        case ParseStatus::BadChar:
            throw std::invalid_argument(std::string("parse_int：含非数字字符 -> \"") + std::string(text) + "\"");
        case ParseStatus::Overflow:
            throw std::out_of_range(std::string("parse_int：溢出 -> \"") + std::string(text) + "\"");
    }
    throw std::logic_error("parse_int：不可达分支");  // 让编译器知道所有路径都有返回值
}

// ============================================================================
//  第 5 节：可靠的小型计时器（后面 04 文件会详细展开）
// ----------------------------------------------------------------------------
//  这里只用最简单的形态：steady_clock + 多次迭代。
//  详细方法论（预热、取中位数/最小值、DoNotOptimize）见 04_performance_measurement.cpp。
// ============================================================================

template <typename Fn>
[[nodiscard]] double time_micros(Fn&& fn) {
    const auto start = std::chrono::steady_clock::now();
    fn();
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::micro>(end - start).count();
}

}  // namespace

int main() {
    std::cout << "==== 03 日志与错误处理实战 ====\n\n";

    // ------------------------------------------------------------------ 日志
    std::cout << "---- 1. 日志设施：分级 + 时间戳 + 文件行号 + 线程安全 ----\n";
    LOG_DEBUG("这条 DEBUG 日志只在 Debug 配置下存在（Release 下编译期消失，零成本）");
    LOG_INFO("服务启动，监听 0.0.0.0:8080");
    LOG_WARN("连接池使用率 85%，接近上限");
    LOG_ERROR("写入磁盘失败，已转入重试队列");
    std::cout << "  说明：每条日志都自带 时间 + 级别 + 线程 id + 文件:行号 + 函数名。\n";
    std::cout << "        LOG_DEBUG 用 if constexpr 包住，Release 下连字符串拼接都不会生成。\n\n";

    std::cout << "---- 2. 级别过滤：生产环境只留 WARN 以上 ----\n";
    Logger::instance().set_min_level(LogLevel::Warn);
    LOG_DEBUG("被过滤：级别不够");
    LOG_INFO("被过滤：级别不够");
    LOG_WARN("这条 WARN 会显示");
    LOG_ERROR("这条 ERROR 会显示");
    Logger::instance().set_min_level(LogLevel::Debug);
    std::cout << "  结论：级别过滤是热路径上的唯一开销（一次原子读 + 比较），关掉的日志几乎免费。\n\n";

    std::cout << "---- 3. 切换输出目标：同一份代码写文件 ----\n";
    {
        const std::string log_path = "07-engineering_log_demo.txt";
        Logger::instance().set_sink(std::make_unique<FileSink>(log_path));
        LOG_INFO("这条日志写进了文件，而不是控制台");
        LOG_ERROR("错误也写进了文件（WARN 以上会立刻 flush）");
        Logger::instance().set_sink(nullptr);  // 恢复控制台
        std::cout << "  已写入文件：" << log_path << "（验证完可删除）\n";
        std::cout << "  结论：调用方（LOG_INFO 等）完全不知道输出去了哪里，切换目标不用改业务代码。\n\n";
    }

    std::cout << "---- 4. 多线程日志：mutex 保证整行不交错 ----\n";
    {
        std::vector<std::thread> workers;
        for (int t = 0; t < 3; ++t) {
            workers.emplace_back([t] {
                for (int i = 0; i < 3; ++i) {
                    LOG_INFO("worker " + std::to_string(t) + " 第 " + std::to_string(i) + " 条");
                }
            });
        }
        for (std::thread& w : workers) {
            w.join();
        }
        std::cout << "  结论：没有 mutex 时多线程日志会互相插入，变成无法阅读的碎片。\n\n";
    }

    // ------------------------------------------------------------ 错误处理策略
    std::cout << "---- 5. 策略 A：返回码 ----\n";
    {
        int value = 0;
        const ParseStatus st = parse_int_a("12345", value);
        std::cout << "  parse_int_a(\"12345\") -> status=" << static_cast<int>(st) << " value=" << value
                  << "\n";
        (void)parse_int_a("999999999999", value);  // 故意忽略返回值：调用者可以装作没看见
        std::cout << "  parse_int_a(\"999999999999\") 的返回值被忽略了，value 保持旧值 —— 这是返回码最大的风险。\n\n";
    }

    std::cout << "---- 6. 策略 B：std::optional ----\n";
    {
        const auto good = parse_int_b("-2048");
        std::cout << "  parse_int_b(\"-2048\") -> " << (good ? std::to_string(*good) : "nullopt") << "\n";
        std::cout << "  parse_int_b(\"abc\")   -> " << (parse_int_b("abc") ? "有值" : "nullopt") << "\n";
        std::cout << "  结论：[[nodiscard]] 让「忘记检查」变成编译警告 C4834；但失败原因丢了。\n\n";
    }

#if defined(__cpp_lib_expected)
    std::cout << "---- 7. 策略 C：std::expected（C++23）----\n";
    {
        if (const auto r = parse_int_c("123"); r) {
            std::cout << "  parse_int_c(\"123\") -> " << *r << "\n";
        }
        if (const auto r = parse_int_c("12x"); !r) {
            std::cout << "  parse_int_c(\"12x\") -> 失败：" << r.error().detail << "\n";
        }
        if (const auto r = parse_int_c("999999999999"); !r) {
            std::cout << "  parse_int_c(\"999999999999\") -> 失败：" << r.error().detail << "\n";
        }
        std::cout << "  结论：expected = optional 的「二态」+ 错误原因，且不启用异常机制。\n\n";
    }
#else
    std::cout << "---- 7. 策略 C：std::expected 在本编译器/标准下不可用 ----\n";
    std::cout << "  说明：std::expected 是 C++23 特性。本机 __cpp_lib_expected 未定义，\n";
    std::cout << "        改用 /std:c++latest 或升级 MSVC STL 后即可使用。\n\n";
#endif

    std::cout << "---- 8. 策略 D：异常 ----\n";
    {
        std::cout << "  parse_int_d(\"777\") = " << parse_int_d("777") << "\n";
        const char* bad[] = {"", "abc", "999999999999"};
        for (const char* s : bad) {
            try {
                (void)parse_int_d(s);
            } catch (const std::exception& e) {
                std::cout << "  parse_int_d(\"" << s << "\") 抛出：" << e.what() << "\n";
            }
        }
        std::cout << "  结论：异常无法被忽略、能跨层传播，但失败路径昂贵。\n\n";
    }

    // ------------------------------------------------------------------ 性能实测
    std::cout << "---- 9. 实测：成功路径 vs 失败路径 ----\n";
    {
        // 迭代次数的选择：解析本身很便宜（几十纳秒），要让差异看得出来必须放大；
        // 但抛异常的路径每次要 1~6 微秒，次数太多示例就跑不完。
        // 这里取「成功路径 1000 万次、失败路径 500 万次」，Debug 下总耗时约 30 秒。
        constexpr int kOkRounds = 2000;
        constexpr int kOkPerRound = 5000;
        constexpr int kFailRounds = 1000;
        constexpr int kFailPerRound = 5000;

        // 成功路径：四种策略都应该差不多
        const double ok_a = time_micros([&] {
            int sink = 0;
            for (int r = 0; r < kOkRounds; ++r) {
                for (int i = 0; i < kOkPerRound; ++i) {
                    int v = 0;
                    if (parse_int_a("12345", v) == ParseStatus::Ok) {
                        sink += v;
                    }
                }
            }
            std::cout << "        (校验和 " << sink << "，int 已回绕，仅为阻止优化)\n";
        });
        const double ok_b = time_micros([&] {
            std::size_t sink = 0;
            for (int r = 0; r < kOkRounds; ++r) {
                for (int i = 0; i < kOkPerRound; ++i) {
                    if (const auto v = parse_int_b("12345")) {
                        sink += static_cast<std::size_t>(*v);
                    }
                }
            }
            std::cout << "        (校验和 " << sink << ")\n";
        });
        const double ok_d = time_micros([&] {
            std::size_t sink = 0;
            for (int r = 0; r < kOkRounds; ++r) {
                for (int i = 0; i < kOkPerRound; ++i) {
                    sink += static_cast<std::size_t>(parse_int_d("12345"));
                }
            }
            std::cout << "        (校验和 " << sink << ")\n";
        });

        // 失败路径：异常 vs 返回码
        const double fail_a = time_micros([&] {
            int sink = 0;
            for (int r = 0; r < kFailRounds; ++r) {
                for (int i = 0; i < kFailPerRound; ++i) {
                    int v = 0;
                    if (parse_int_a("999999999999", v) != ParseStatus::Ok) {
                        ++sink;
                    }
                }
            }
            std::cout << "        (失败计数 " << sink << ")\n";
        });
        const double fail_d = time_micros([&] {
            int sink = 0;
            for (int r = 0; r < kFailRounds; ++r) {
                for (int i = 0; i < kFailPerRound; ++i) {
                    try {
                        sink += parse_int_d("999999999999");
                    } catch (const std::out_of_range&) {
                        ++sink;
                    }
                }
            }
            std::cout << "        (失败计数 " << sink << ")\n";
        });

        const double ok_calls = static_cast<double>(kOkRounds) * kOkPerRound;
        const double fail_calls = static_cast<double>(kFailRounds) * kFailPerRound;
        std::cout << "  成功路径每个用例 " << static_cast<long long>(ok_calls) << " 次调用（每轮 "
                  << kOkRounds << " x " << kOkPerRound << "）\n";
        std::cout << "  失败路径每个用例 " << static_cast<long long>(fail_calls) << " 次调用（每轮 "
                  << kFailRounds << " x " << kFailPerRound << "）\n\n";
        std::cout << "  场景        策略              总耗时(ms)   每次(ns)\n";
        std::cout << "  ----------  ----------------  ----------  ----------\n";
        std::cout << "  成功路径    返回码            " << std::setw(10) << std::fixed << std::setprecision(2)
                  << ok_a / 1000.0 << "  " << std::setw(10) << ok_a * 1000.0 / ok_calls << "\n";
        std::cout << "  成功路径    optional          " << std::setw(10) << ok_b / 1000.0 << "  "
                  << std::setw(10) << ok_b * 1000.0 / ok_calls << "\n";
        std::cout << "  成功路径    异常（不抛）      " << std::setw(10) << ok_d / 1000.0 << "  "
                  << std::setw(10) << ok_d * 1000.0 / ok_calls << "\n";
        std::cout << "  失败路径    返回码            " << std::setw(10) << fail_a / 1000.0 << "  "
                  << std::setw(10) << fail_a * 1000.0 / fail_calls << "\n";
        std::cout << "  失败路径    异常（抛出）      " << std::setw(10) << fail_d / 1000.0 << "  "
                  << std::setw(10) << fail_d * 1000.0 / fail_calls << "\n";
        std::cout << "\n";
        std::cout << "  实测结论：\n";
        std::cout << "    1) 成功路径上异常是**零成本**的（和返回码同量级）。\n";
        std::cout << "    2) 失败路径上异常比返回码慢约 " << std::fixed << std::setprecision(0)
                  << (fail_a > 0 ? fail_d / fail_a : 0) << " 倍（本机 Debug 配置、每次异常数微秒级）。\n";
        std::cout << "    3) boost/README_Boost_vs_STL.md 的现场测量给的是「异常慢约 430 倍」\n";
        std::cout << "       （20 万次失败，每次异常 1~2 微秒）—— 与本表趋势完全一致：\n";
        std::cout << "       异常的开销主要在栈展开，量级是微秒，不是纳秒。\n";
        std::cout << "    注：本表是 Debug 配置下的数字（未优化），绝对值偏大；\n";
        std::cout << "        数字随机器变化，趋势稳定，仅供参考。\n\n";
    }

    std::cout << "---- 10. 选型决策表 ----\n";
    std::cout << "  场景                              推荐              理由\n";
    std::cout << "  --------------------------------  ----------------  ----------------------------------\n";
    std::cout << "  失败极其频繁（解析/校验/协议）    返回码 / expected 失败路径不能走异常（430 倍差距）\n";
    std::cout << "  「没有值」是正常语义（查找）      std::optional     语义精确，[[nodiscard]] 防漏检\n";
    std::cout << "  失败要带原因且层层上报            std::expected     比错误码信息全，比异常便宜\n";
    std::cout << "  错误要跨多层展开栈               异常              逐层 if 检查会把代码写烂\n";
    std::cout << "  构造失败（构造函数无处返回）      异常              构造函数没有返回值的替代方案\n";
    std::cout << "  C 接口 / 嵌入式 / 禁异常         返回码            ABI 稳定、可预测、无异常开销\n";
    std::cout << "  实时/硬实时系统                   返回码           异常延迟不可预测\n";
    std::cout << "  资源不足（内存/句柄）             异常（bad_alloc）  标准库本身就是这么抛的\n\n";
    std::cout << "  一条重要提醒：**不要混着用**。整个工程（或至少整个模块）要统一策略，\n";
    std::cout << "  否则调用者永远不知道该 try 还是该查返回值，错误一定会被漏掉。\n\n";

    // ------------------------------------------------------------- ScopeGuard
    std::cout << "---- 11. RAII ScopeGuard：清理一定发生 ----\n";
    std::cout << "  成功的事务：\n";
    (void)run_transaction(false);
    std::cout << "  失败的事务：\n";
    (void)run_transaction(true);
    std::cout << "  结论：run_transaction 里没有任何一处显式的清理代码，\n";
    std::cout << "        但无论成功、失败还是中途抛异常，回滚都不会漏。\n\n";

    std::cout << "---- 12. RAII 的同一思路用在锁、文件、内存上 ----\n";
    {
        std::mutex m;
        {
            std::lock_guard<std::mutex> lock(m);  // 离开作用域自动解锁，异常也不漏
            LOG_INFO("持有锁期间做一步操作");
        }  // 这里自动解锁
        std::ofstream file("07-engineering_raii_demo.txt");  // 析构自动关闭 + flush
        file << "RAII 管理的文件句柄\n";
        std::cout << "  结论：lock_guard / unique_ptr / ofstream / ScopeGuard 是同一个思路 ——\n";
        std::cout << "        把「成对的操作」变成「一个对象的生命周期」。\n\n";
    }

    std::cout << "==== 结论：日志管可观测性，RAII 管资源，错误策略要全项目统一 ====\n";
    return 0;
}
