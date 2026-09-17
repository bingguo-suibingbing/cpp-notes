// ============================================================================
//  tests/checked_int.cpp —— checked_int.h 的实现（+ 一个健壮性小实验）
// ============================================================================
#include "checked_int.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace eng07 {

namespace {

// 溢出判断的策略：用 64 位中间量累加，并设置一个「早退上限」。
// 为什么不直接比较 INT32_MAX：如果输入是 100 位数字，中间量自己就会溢出，
// 那就变成「用溢出检测溢出」了 —— 所以在中间量增长到一定规模时提前退出。
constexpr std::int64_t kEarlyExitLimit = 1'000'000'000'000'000'000LL;  // 10^18，远小于 int64 上限

}  // namespace

ParseError parse_int_strict(std::string_view text, int& out) noexcept {
    if (text.empty()) {
        return ParseError::Empty;
    }

    bool negative = false;
    std::size_t index = 0;
    if (text[0] == '+' || text[0] == '-') {
        negative = (text[0] == '-');
        index = 1;
        if (text.size() == 1) {
            return ParseError::Empty;  // 只有一个符号：不是合法整数
        }
    }

    std::int64_t accumulator = 0;
    for (; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch < '0' || ch > '9') {
            return ParseError::BadChar;
        }
        accumulator = accumulator * 10 + static_cast<std::int64_t>(ch - '0');
        if (accumulator > kEarlyExitLimit) {
            return ParseError::Overflow;  // 早退：避免中间量自己溢出
        }
    }

    if (negative) {
        accumulator = -accumulator;
    }

    // 真正的范围检查：用 int64 与 int 的上下限比较（不用 INT32_MIN 之类的宏，
    // 因为宏在 <climits> 里且类型不明确；numeric_limits 更安全）
    constexpr std::int64_t kIntMin = static_cast<std::int64_t>(-2147483647 - 1);  // INT32_MIN
    constexpr std::int64_t kIntMax = static_cast<std::int64_t>(2147483647);       // INT32_MAX
    if (accumulator < kIntMin || accumulator > kIntMax) {
        return ParseError::Overflow;
    }

    out = static_cast<int>(accumulator);
    return ParseError::Ok;
}

bool parse_int_ok(std::string_view text, int& out) noexcept {
    return parse_int_strict(text, out) == ParseError::Ok;
}

}  // namespace eng07
