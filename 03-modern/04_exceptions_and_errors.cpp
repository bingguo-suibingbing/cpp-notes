// ============================================================================
//  04_exceptions_and_errors.cpp
// ----------------------------------------------------------------------------
//  演示主题：
//    1. try / catch / throw 基本形态与「按引用捕获」
//    2. 为什么 catch (const std::exception& e) 不能按值捕获（对象切片）
//    3. std::exception 继承层次与自定义异常
//    4. 栈展开（stack unwinding）与 RAII：异常传播时析构函数一定执行
//    5. noexcept 的意义、何时该标、以及为什么析构函数不该抛异常
//    6. 异常安全的三个等级：基本保证 / 强保证 / 不抛保证 + copy-and-swap
//    7. 错误处理的工程取舍：异常 vs error_code vs optional vs expected
//    8. 性能实测：异常路径 vs 错误码路径（成功率不同时的对比）
//
//  关键结论：
//    - 异常是「异常情况」的通道，不是控制流工具；正常路径上的错误用返回值表达。
//    - catch 一定要按引用或 const 引用；按值捕获会切片，丢失派生类的行为。
//    - noexcept 不是装饰：它影响容器是否敢用移动、影响调用方的优化与异常安全推理。
//    - 析构函数抛异常会在栈展开中直接 terminate，所以析构默认应是 noexcept。
//    - 强保证的标准实现套路就是 copy-and-swap：先做可能抛的操作，最后用
//      一个 noexcept 的 swap 提交，失败则原对象毫发无损。
// ============================================================================

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// ---------------------------------------------------------------- 自定义异常层次
// 继承 std::runtime_error 可以复用 what() 的实现与统一捕获点
class AppError : public std::runtime_error {
public:
    explicit AppError(const std::string& message) : std::runtime_error(message) {}
};

class ValidationError : public AppError {
public:
    ValidationError(int code, const std::string& message)
        : AppError("校验失败[" + std::to_string(code) + "]: " + message), code_(code) {}
    int code() const noexcept { return code_; }

private:
    int code_;
};

class IoError : public AppError {
public:
    explicit IoError(const std::string& path) : AppError("无法读取: " + path), path_(path) {}
    const std::string& path() const noexcept { return path_; }

private:
    std::string path_;
};

// ---------------------------------------------------------------- 切片演示
class Base {
public:
    virtual ~Base() = default;
    virtual std::string describe() const { return "Base（基类）"; }
};

class Derived : public Base {
public:
    std::string describe() const override { return "Derived（派生类）"; }
    std::string extra() const { return "派生类独有信息"; }
};

// ---------------------------------------------------------------- RAII 资源
class Session {
public:
    explicit Session(std::string name) : name_(std::move(name)) {
        std::cout << "      [Session] 打开 " << name_ << "\n";
    }
    ~Session() {
        // 析构必须不抛异常：它在栈展开过程中被调用，再抛就 terminate
        std::cout << "      [Session] 关闭 " << name_ << "（异常路径同样会执行）\n";
    }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

private:
    std::string name_;
};

// 抛出点：注意 Session 是栈上对象，异常一抛就会被析构
void deep_call(int depth) {
    Session session("level-" + std::to_string(depth));
    if (depth == 0) {
        throw ValidationError(42, "深度到底了");
    }
    deep_call(depth - 1);
}

// ---------------------------------------------------------------- copy-and-swap
// 一个提供「强异常保证」的容器：赋值要么完全成功，要么原对象不变
class IntBuffer {
public:
    IntBuffer() = default;

    explicit IntBuffer(std::size_t n) : data_(new int[n]), size_(n) {}

    // 拷贝构造：可能抛（bad_alloc），此时原对象还没被碰
    IntBuffer(const IntBuffer& other)
        : data_(other.size_ > 0 ? new int[other.size_] : nullptr), size_(other.size_) {
        for (std::size_t i = 0; i < size_; ++i) {
            data_[i] = other.data_[i];
        }
        if (size_ > 0) {
            throw_on_demand();  // 用一个可控的失败点演示强保证
        }
    }

    // 移动构造：不抛，标 noexcept
    IntBuffer(IntBuffer&& other) noexcept
        : data_(other.data_), size_(other.size_) {
        other.data_ = nullptr;
        other.size_ = 0;
    }

    // copy-and-swap：拷贝交换法，天然提供强异常保证
    IntBuffer& operator=(IntBuffer other) noexcept {  // 注意：按值接收，拷贝发生在进入函数前
        swap(other);
        return *this;
    }

    ~IntBuffer() { delete[] data_; }

    void swap(IntBuffer& other) noexcept {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
    }

    std::size_t size() const noexcept { return size_; }
    int at(std::size_t i) const { return data_[i]; }
    bool valid() const noexcept { return size_ == 0 || data_ != nullptr; }

    // 下面两个是演示用的钩子
    static void set_on_demand(bool on) noexcept { on_demand_ = on; }

private:
    void throw_on_demand() {
        if (on_demand_) {
            throw std::runtime_error("模拟拷贝中途失败");
        }
    }

    int* data_{nullptr};
    std::size_t size_{0};

    // 静态成员当作「测试开关」，避免污染对象状态
    inline static bool on_demand_{false};
};

// ---------------------------------------------------------------- 错误码 / optional
enum class ParseError : int { none = 0, empty_input = 1, bad_char = 2, overflow = 3 };

const char* to_string(ParseError e) noexcept {
    switch (e) {
        case ParseError::none:        return "ok";
        case ParseError::empty_input: return "输入为空";
        case ParseError::bad_char:    return "包含非法字符";
        case ParseError::overflow:    return "数值溢出";
    }
    return "未知错误";
}

// C 风格 + 枚举错误码：显式、零开销、可在 noexcept / 跨 ABI 边界使用
int parse_int_errorcode(const std::string& text, int& out, ParseError& err) noexcept {
    if (text.empty()) {
        err = ParseError::empty_input;
        return -1;
    }
    long long value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') {
            err = ParseError::bad_char;
            return -1;
        }
        value = value * 10 + (c - '0');
        if (value > 2147483647LL) {
            err = ParseError::overflow;
            return -1;
        }
    }
    out = static_cast<int>(value);
    err = ParseError::none;
    return 0;
}

// optional：只关心「有 / 没有」，不需要错误原因
std::optional<int> parse_int_optional(const std::string& text) {
    int value = 0;
    ParseError err = ParseError::none;
    if (parse_int_errorcode(text, value, err) != 0) {
        return std::nullopt;
    }
    return value;
}

// C++23 的 std::expected 在 MSVC 19.4x 之前不可用（本环境未提供），
// 这里手写一个最小版本展示同一种「返回值携带错误」的模型。
// 生产代码请优先使用标准库版本，接口语义更容易与生态对接。
template <typename T, typename E>
class Expected {
public:
    Expected(T value) : value_(std::move(value)), has_value_(true) {}
    Expected(E error) : error_(std::move(error)), has_value_(false) {}

    explicit operator bool() const noexcept { return has_value_; }
    const T& operator*() const { return value_; }
    const T* operator->() const { return &value_; }
    const E& error() const { return error_; }

private:
    T value_{};
    E error_{};
    bool has_value_{false};
};

Expected<int, ParseError> parse_int_expected(const std::string& text) {
    int value = 0;
    ParseError err = ParseError::none;
    if (parse_int_errorcode(text, value, err) != 0) {
        return Expected<int, ParseError>(err);
    }
    return Expected<int, ParseError>(value);
}

// ---------------------------------------------------------------- noexcept
void declared_noexcept() noexcept {
    // 这里绝不能抛；抛了就是 std::terminate（见下文演示）
}

// 演示 noexcept 对「移动 vs 拷贝」选择的影响：
// vector 扩容时，只有移动构造是 noexcept 才会用移动，否则退化成拷贝
struct MoveNoexcept {
    MoveNoexcept() = default;
    MoveNoexcept(const MoveNoexcept&) {}
    MoveNoexcept(MoveNoexcept&&) noexcept {}
};
struct MoveThrowing {
    MoveThrowing() = default;
    MoveThrowing(const MoveThrowing&) {}
    MoveThrowing(MoveThrowing&&) {}  // 没标 noexcept
};

static_assert(std::is_nothrow_move_constructible_v<MoveNoexcept>, "应当是 noexcept 移动");
static_assert(!std::is_nothrow_move_constructible_v<MoveThrowing>, "未标 noexcept");

// ---------------------------------------------------------------- 性能实测
// 对比「异常路径」与「错误码路径」在两种成功率下的开销
struct ParseResult {
    int value{0};
    int error{0};
};

ParseResult parse_with_errorcode(const std::string& text) noexcept {
    ParseResult r;
    int value = 0;
    ParseError err = ParseError::none;
    if (parse_int_errorcode(text, value, err) != 0) {
        r.error = static_cast<int>(err);
        return r;
    }
    r.value = value;
    return r;
}

int parse_with_exception(const std::string& text) {
    int value = 0;
    ParseError err = ParseError::none;
    if (parse_int_errorcode(text, value, err) != 0) {
        throw std::invalid_argument(to_string(err));
    }
    return value;
}

template <typename Fn>
double time_ms(Fn&& fn, int repeats) {
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < repeats; ++i) {
        volatile int sink = fn(i);
        (void)sink;
    }
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

int main() {
    std::cout << "==== 1. try / catch / throw 与按引用捕获 ====\n";
    {
        try {
            throw IoError("/etc/config.yaml");
        } catch (const IoError& e) {
            std::cout << "  抓到 IoError: " << e.what() << ", path = " << e.path() << "\n";
        } catch (const AppError& e) {
            std::cout << "  这里不会执行（上一级已经匹配）: " << e.what() << "\n";
        }

        try {
            throw ValidationError(7, "字段缺失");
        } catch (const AppError& e) {  // 用基类引用捕获，能拿到多态行为
            std::cout << "  用 AppError& 捕获到派生类: " << e.what() << "\n";
        }

        std::cout << "  捕获顺序很重要：派生类必须写在基类前面，否则永远匹配不到\n";
    }

    std::cout << "\n==== 2. 按值捕获 = 对象切片 ====\n";
    {
        std::cout << "  (a) 按引用捕获（正确）：\n";
        try {
            throw Derived();
        } catch (const Base& b) {
            std::cout << "      describe() = " << b.describe() << "（多态生效）\n";
        }

        std::cout << "  (b) 按值捕获（错误）：\n";
        try {
            throw Derived();
        } catch (Base b) {  // 切片：Derived 部分被切掉，只剩 Base 子对象
            std::cout << "      describe() = " << b.describe()
                      << "（多态失效，extra() 的信息彻底丢失）\n";
            // b.extra();  // 编译错误：切片后类型就是 Base
        }
        std::cout << "  规则：永远 catch (const T&) 或 catch (T&)，不要按值\n";
    }

    std::cout << "\n==== 3. std::exception 层次与统一捕获 ====\n";
    {
        auto probe = [](int which) {
            if (which == 0) {
                throw std::out_of_range("下标越界");
            }
            if (which == 1) {
                throw std::bad_alloc();
            }
            throw 42;  // 抛非异常类型：能捕获，但工程上禁止
        };

        for (int i = 0; i < 3; ++i) {
            try {
                probe(i);
            } catch (const std::bad_alloc&) {
                std::cout << "  bad_alloc: 内存不足\n";
            } catch (const std::logic_error& e) {  // out_of_range 属于 logic_error
                std::cout << "  logic_error: " << e.what() << "\n";
            } catch (const std::exception& e) {
                std::cout << "  std::exception: " << e.what() << "\n";
            } catch (...) {  // 兜底：只应该出现在「最外层」或「必须继续运行」的边界
                std::cout << "  捕获到非 std::exception 的异常（工程上应当避免）\n";
            }
        }
        std::cout << "  层次：exception -> logic_error -> out_of_range / invalid_argument\n";
        std::cout << "        exception -> runtime_error -> range_error / system_error\n";
    }

    std::cout << "\n==== 4. 栈展开与 RAII ====\n";
    {
        try {
            deep_call(2);
        } catch (const AppError& e) {
            std::cout << "  最终捕获: " << e.what() << "\n";
        }
        std::cout << "  上面每一层的 Session 都被析构了，这是 RAII 能在异常下不泄漏的根据\n";
    }

    std::cout << "\n==== 5. noexcept：语义承诺与性能影响 ====\n";
    {
        declared_noexcept();
        std::cout << "  noexcept 函数里抛异常 -> 直接 std::terminate，没有栈展开、没有 catch 机会\n";
        std::cout << "  所以 noexcept 只标「确实不会失败」的函数：\n";
        std::cout << "    - 移动构造 / 移动赋值 / swap（容器扩容靠它选移动）\n";
        std::cout << "    - 析构函数（默认就是 noexcept）\n";
        std::cout << "    - 简单 getter、size()、对比运算符\n";

        std::cout << "  vector<MoveNoexcept> 扩容会用移动；vector<MoveThrowing> 会退化成拷贝\n";

        // 用 type_traits 直接证明「移动是否 noexcept」影响容器的选择
        static_assert(std::is_nothrow_move_constructible_v<MoveNoexcept>);
        std::cout << "  static_assert 已确认：MoveNoexcept 的移动是 noexcept\n";

        std::cout << "  工程提示：给自定义类型的移动操作补上 noexcept，"
                     "否则放进 vector 会静默变慢\n";
    }

    std::cout << "\n==== 6. 异常安全三等级与 copy-and-swap ====\n";
    {
        IntBuffer::set_on_demand(false);
        IntBuffer a(4);
        IntBuffer b(8);
        std::cout << "  赋值前 a.size() = " << a.size() << ", b.size() = " << b.size() << "\n";
        a = b;  // 正常路径
        std::cout << "  赋值后 a.size() = " << a.size() << "（成功）\n";

        std::cout << "  现在让拷贝中途失败，观察强保证：\n";
        IntBuffer::set_on_demand(true);
        IntBuffer c(16);
        const std::size_t before = c.size();
        try {
            c = a;  // 拷贝构造里抛异常
        } catch (const std::exception& e) {
            std::cout << "    捕获: " << e.what() << "\n";
        }
        IntBuffer::set_on_demand(false);
        std::cout << "    失败后 c.size() = " << c.size() << "（原本是 " << before
                  << "，未被破坏 -> 强异常保证）\n";
        std::cout << "  实现要点：拷贝发生在「函数参数构造」阶段，"
                     "此时还没碰 *this；最后一步 swap 是 noexcept 提交\n";
        std::cout << "  三个等级：\n";
        std::cout << "    基本保证：不泄漏、对象有效，但内容可能已改变\n";
        std::cout << "    强保证  ：要么成功，要么完全回滚（事务语义）\n";
        std::cout << "    不抛保证：承诺绝不失败（noexcept），如 swap / 移动\n";
    }

    std::cout << "\n==== 7. 四种错误处理方式对比 ====\n";
    {
        const std::string good = "12345";
        const std::string bad = "12x45";
        const std::string empty;

        int out = 0;
        ParseError err = ParseError::none;
        if (parse_int_errorcode(good, out, err) == 0) {
            std::cout << "  error_code : \"" << good << "\" -> " << out << "\n";
        }
        parse_int_errorcode(bad, out, err);
        std::cout << "  error_code : \"" << bad << "\" -> " << to_string(err)
                  << "（可 noexcept、零分配、错误原因完整）\n";

        std::cout << "  optional   : \"" << empty << "\" -> "
                  << (parse_int_optional(empty) ? "有值" : "无值（丢失了失败原因）") << "\n";

        auto parsed = parse_int_expected(bad);
        if (parsed) {
            std::cout << "  expected   : 有值 " << *parsed << "\n";
        } else {
            std::cout << "  expected   : 失败 " << to_string(parsed.error())
                      << "（既有值又有原因，且不抛异常）\n";
        }

        try {
            const int parsed_by_exception = parse_with_exception(bad);
            std::cout << "  exception  : " << parsed_by_exception << "\n";
        } catch (const std::invalid_argument& e) {
            std::cout << "  exception  : 抛出 " << e.what() << "\n";
        }

        std::cout << "  选型建议：\n";
        std::cout << "    可预期的业务失败（解析、查找、校验） -> optional / expected / error_code\n";
        std::cout << "    构造函数失败、无法返回错误的地方（运算符重载、begin/end 内部） -> 异常\n";
        std::cout << "    跨模块 / 跨 ABI / 跨语言边界            -> error_code 或 expected\n";
        std::cout << "    高频热路径且失败很常见                    -> 不要用异常（见下方实测）\n";
    }

    std::cout << "\n==== 8. 性能实测：异常路径 vs 错误码路径 ====\n";
    {
        constexpr int kRepeats = 20000;
        const std::vector<std::string> texts(2000, "12345");
        const std::size_t text_count = texts.size();

        // 情况 A：从不失败（只测「有异常机制」是否给正常路径加成本）
        const double t_ec_ok = time_ms(
            [&](int i) {
                const auto& s = texts[static_cast<std::size_t>(i) % text_count];
                return parse_with_errorcode(s).value;
            },
            kRepeats);
        const double t_ex_ok = time_ms(
            [&](int i) {
                const auto& s = texts[static_cast<std::size_t>(i) % text_count];
                return parse_with_exception(s);
            },
            kRepeats);

        // 情况 B：一半失败（异常路径被真正走到的成本）
        const double t_ec_fail = time_ms(
            [&](int i) {
                const std::string s = (i % 2 == 0) ? "12345" : "12x45";
                return parse_with_errorcode(s).value;
            },
            kRepeats);
        const double t_ex_fail = time_ms(
            [&](int i) {
                const std::string s = (i % 2 == 0) ? "12345" : "12x45";
                try {
                    return parse_with_exception(s);
                } catch (const std::invalid_argument&) {
                    return -1;
                }
            },
            kRepeats);

        std::cout << "  A. 永不失败（错误码）: " << t_ec_ok << " ms\n";
        std::cout << "  A. 永不失败（异常）  : " << t_ex_ok << " ms（几乎一样，正常路径零开销）\n";
        std::cout << "  B. 一半失败（错误码）: " << t_ec_fail << " ms\n";
        std::cout << "  B. 一半失败（异常）  : " << t_ex_fail << " ms（明显变慢，抛异常很贵）\n";
        std::cout << "  结论：\n";
        std::cout << "    - 「用异常会让正常代码变慢」是误解：零开销异常模型下正常路径几乎免费\n";
        std::cout << "    - 真正贵的是 throw 本身（构造异常对象、查表、栈展开），"
                     "所以异常不能当控制流用\n";
        std::cout << "    - 数字随机器 / 标准库实现变化，趋势稳定，Debug 下差距会被放大\n";
    }

    std::cout << "\n==== 9. 大型项目 / 跨边界禁用异常的现实原因 ====\n";
    std::cout << "  1) 二进制体积与 ABI：异常表会让二进制膨胀，跨 DLL / 跨语言传播异常要约定 ABI，"
                 "C 接口尤其无法承受\n";
    std::cout << "  2) 确定性实时系统：栈展开的时间不可预测，飞控 / 内核 / 音频回调里不可接受\n";
    std::cout << "  3) 代码审查成本：异常是「隐式控制流」，团队规模大时很难静态推理所有出口\n";
    std::cout << "  4) 工具链多样性：某些嵌入式工具链直接关掉异常支持\n";
    std::cout << "  现实做法：核心库用 error_code / expected 表达失败，"
                 "只在应用层边界用异常做统一兜底\n";
    return 0;
}
