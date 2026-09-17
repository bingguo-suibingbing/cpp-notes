// ============================================================================
//  tests/checked_int.h —— 带完整边界检查的整数解析器
// ----------------------------------------------------------------------------
//  这是被测试的「被测代码」（SUT, System Under Test）。
//  放在 tests 目录内是为了让本章自成一体；真实项目里它应该在 src/。
//
//  为什么不用 std::stoi / atoi / sscanf：
//    - atoi   ：无法区分「0」和「非法输入」，溢出是未定义行为；
//    - stoi   ：会抛异常（可用），但「整个字符串必须是数字」这件事要自己查
//               （它只解析前缀，比如 "12abc" 会返回 12 而不报错）；
//    - sscanf ：返回值和边界都很容易用错，且不能从 std::string_view 直接读。
//    要一个「严格、明确、可预期」的版本，自己写反而最短。
//
//  接口契约：
//    - 接受可选的正负号；
//    - 除符号外必须全是十进制数字，且至少有一位；
//    - 不接受前导/尾随空白（调用者要先 trim —— 保持函数单一职责）；
//    - 结果必须落在 [INT32_MIN, INT32_MAX]，越界返回 Overflow，绝不回绕。
// ============================================================================
#pragma once

#include <cstdint>
#include <string_view>

namespace eng07 {

enum class ParseError {
    Ok = 0,
    Empty,       // 空字符串 / 只有符号
    BadChar,     // 含非数字字符
    Overflow,    // 数值超出 int 范围
};

// 把错误码放在返回值里，并加 [[nodiscard]]：
// 这样「忘记检查返回值」会在 /W4 下变成编译警告 C4834 ——
// 把「接口约定」交给编译器强制，是本章反复强调的思路。
[[nodiscard]] ParseError parse_int_strict(std::string_view text, int& out) noexcept;

// 只要「成功 / 失败」时用的简化版：语义更清楚，代价是丢掉具体原因。
[[nodiscard]] bool parse_int_ok(std::string_view text, int& out) noexcept;

}  // namespace eng07
