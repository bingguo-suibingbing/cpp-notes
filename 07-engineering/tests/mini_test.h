// ============================================================================
//  tests/mini_test.h —— 30 行以内手写的极简断言框架
// ----------------------------------------------------------------------------
//  为什么不用第三方框架（GoogleTest / Catch2）：
//    本目录的目标是「不引任何依赖就能跑起来的真实单元测试」。
//    真实项目里当然应该用 GoogleTest（功能更全、CI 集成更好），
//    但先用这 30 行体会一下测试框架的本质：
//      1) 一个计数器（通过 / 失败）
//      2) 一组断言宏（失败时打印文件、行号、表达式，然后累加失败数）
//      3) main 里返回「失败数 == 0 ? 0 : 1」
//    理解了这三点，看任何测试框架都是同一套东西。
//
//  设计取舍：
//    - 断言宏用 do { ... } while (false) 包裹：这样它整体是一条语句，
//      可以在 if/else 后面安全使用，也不会和后续分号打架。
//    - 只提供最常用的三种：CHECK_TRUE / CHECK_FALSE / CHECK_EQ。
//      真实框架还有 CHECK_NEAR（浮点）、CHECK_THROWS（异常）等。
//    - CHECK_EQ 打印两个值：失败时能直接看出「期望 vs 实际」，
//      省掉一次「加打印再跑一遍」的循环。
//    - 故意不用模板化的万能打印：保持简单，够用就行。
// ============================================================================
#pragma once  // 头文件只被包含一次（比 include guard 简洁，主流编译器都支持）

#include <iostream>
#include <string>

namespace mini_test {

// 全局计数器：用函数内静态变量，避免静态初始化顺序问题（同 03 文件的日志单例思路）
inline int& passed_count() {
    static int value = 0;
    return value;
}
inline int& failed_count() {
    static int value = 0;
    return value;
}

// 把值转成字符串用于失败信息。
// 为什么要这个重载对，而不是到处写 std::to_string：
//   std::to_string 不支持 std::string / const char*，
//   而 CHECK_EQ 需要能打印任意可比较类型（本目录里就有 string 比较）。
//   std::string 优先，其它类型交给 std::to_string（数值型都能用）。
inline std::string to_display(const std::string& value) { return value; }
inline std::string to_display(const char* value) { return value == nullptr ? "(null)" : value; }

template <typename T>
std::string to_display(const T& value) {
    return std::to_string(value);
}

// 断言失败时的统一输出：文件、行号、表达式、期望值、实际值
inline void report_failure(const char* file, int line, const std::string& what,
                           const std::string& detail = {}) {
    ++failed_count();
    std::cout << "  [FAIL] " << file << ":" << line << "  " << what;
    if (!detail.empty()) {
        std::cout << "  -> " << detail;
    }
    std::cout << "\n";
}

// 测试收尾：打印统计并返回进程退出码（0 = 全部通过）
inline int summary(const char* suite_name) {
    const int passed = passed_count();
    const int failed = failed_count();
    std::cout << "[" << suite_name << "] 通过 " << passed << " 项，失败 " << failed << " 项\n";
    if (failed == 0) {
        std::cout << "[" << suite_name << "] 全部通过\n";
        return 0;
    }
    std::cout << "[" << suite_name << "] 有失败项\n";
    return 1;  // 让 CTest / CI 通过退出码判定失败
}

}  // namespace mini_test

// ---------------------------------------------------------------------------
//  断言宏。do { } while (false) 的理由见文件头。
//  CHECK_EQ 要求 a、b 可以流式输出；用一次求值，避免副作用被执行两次。
// ---------------------------------------------------------------------------
#define CHECK_TRUE(expr)                                                                  \
    do {                                                                                  \
        if (expr) {                                                                       \
            ++::mini_test::passed_count();                                                \
        } else {                                                                          \
            ::mini_test::report_failure(__FILE__, __LINE__, "CHECK_TRUE(" #expr ")");      \
        }                                                                                 \
    } while (false)

#define CHECK_FALSE(expr)                                                                 \
    do {                                                                                  \
        if (!(expr)) {                                                                    \
            ++::mini_test::passed_count();                                                \
        } else {                                                                          \
            ::mini_test::report_failure(__FILE__, __LINE__, "CHECK_FALSE(" #expr ")");     \
        }                                                                                 \
    } while (false)

#define CHECK_EQ(a, b)                                                                    \
    do {                                                                                  \
        const auto lhs_value = (a);                                                       \
        const auto rhs_value = (b);                                                       \
        if (lhs_value == rhs_value) {                                                     \
            ++::mini_test::passed_count();                                                \
        } else {                                                                          \
            ::mini_test::report_failure(__FILE__, __LINE__, "CHECK_EQ(" #a ", " #b ")",   \
                                        ::mini_test::to_display(lhs_value) + " != " +     \
                                            ::mini_test::to_display(rhs_value));          \
        }                                                                                 \
    } while (false)

// 统计项数 = 通过 + 失败，供使用者在 main 里做「至少跑了 N 项」的兜底检查
#define CHECK_TOTAL() (::mini_test::passed_count() + ::mini_test::failed_count())
