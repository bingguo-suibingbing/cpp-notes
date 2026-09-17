// ============================================================================
//  tests/test_checked_int.cpp —— 整数解析器的单元测试
// ----------------------------------------------------------------------------
//  测试组织方式：一个「测试函数」= 一组相关的断言。
//  真正的测试框架会用 TEST(Suite, Name) 宏自动注册；这里手工调用，
//  但「一个失败不影响其它检查继续跑」这一点是一样的（比 assert 好在不会中止）。
//
//  覆盖策略（测试用例怎么想）：
//    1) 正常路径：正数、负数、带 + 号、零、边界值（INT_MAX / INT_MIN）
//    2) 边界：单个数字、超长数字（溢出）、刚好越界一位
//    3) 非法输入：空串、只有符号、含字母、含空格、含小数点、前导零（允许）
//    4) 不变式：失败时不能改 out（调用者的变量不该被污染）
// ============================================================================
#include "checked_int.h"

#include "mini_test.h"

#include <iostream>
#include <string>

namespace {

using eng07::parse_int_ok;
using eng07::parse_int_strict;
using eng07::ParseError;

void test_normal_cases() {
    int value = 0;
    CHECK_TRUE(parse_int_strict("0", value) == ParseError::Ok);
    CHECK_EQ(value, 0);

    CHECK_TRUE(parse_int_strict("42", value) == ParseError::Ok);
    CHECK_EQ(value, 42);

    CHECK_TRUE(parse_int_strict("-42", value) == ParseError::Ok);
    CHECK_EQ(value, -42);

    CHECK_TRUE(parse_int_strict("+42", value) == ParseError::Ok);
    CHECK_EQ(value, 42);

    CHECK_TRUE(parse_int_strict("0007", value) == ParseError::Ok);
    CHECK_EQ(value, 7);  // 前导零是允许的，值仍然是 7

    CHECK_TRUE(parse_int_ok("123456", value));
    CHECK_EQ(value, 123456);
}

void test_boundaries() {
    int value = 0;
    // 注意：这里用 INT32_MAX/INT32_MIN（int64 字面量再转 int）而不是
    // std::numeric_limits<int>::max()，是为了让断言打印出来的期望值一眼可读；
    // 真实项目里用 numeric_limits 更通用。
    constexpr int kIntMax = 2147483647;
    constexpr int kIntMin = -2147483647 - 1;

    // 刚好在范围内的最大值 / 最小值：必须成功
    CHECK_TRUE(parse_int_strict("2147483647", value) == ParseError::Ok);
    CHECK_EQ(value, kIntMax);

    CHECK_TRUE(parse_int_strict("-2147483648", value) == ParseError::Ok);
    CHECK_EQ(value, kIntMin);

    // 越界一位：必须报 Overflow，而不是回绕
    CHECK_TRUE(parse_int_strict("2147483648", value) == ParseError::Overflow);
    CHECK_TRUE(parse_int_strict("-2147483649", value) == ParseError::Overflow);

    // 远超范围的超长输入：也必须报 Overflow（不能因为中间量溢出而误判为成功）
    CHECK_TRUE(parse_int_strict("99999999999999999999999999", value) == ParseError::Overflow);
    CHECK_TRUE(parse_int_strict("-99999999999999999999999999", value) == ParseError::Overflow);
}

void test_invalid_input() {
    int value = 0;

    CHECK_TRUE(parse_int_strict("", value) == ParseError::Empty);
    CHECK_TRUE(parse_int_strict("-", value) == ParseError::Empty);
    CHECK_TRUE(parse_int_strict("+", value) == ParseError::Empty);

    CHECK_TRUE(parse_int_strict("12a", value) == ParseError::BadChar);
    CHECK_TRUE(parse_int_strict("a12", value) == ParseError::BadChar);
    CHECK_TRUE(parse_int_strict("1 2", value) == ParseError::BadChar);
    CHECK_TRUE(parse_int_strict(" 12", value) == ParseError::BadChar);  // 不 trim，交给调用者
    CHECK_TRUE(parse_int_strict("3.14", value) == ParseError::BadChar);
    CHECK_TRUE(parse_int_strict("0x1F", value) == ParseError::BadChar);
    CHECK_TRUE(parse_int_strict("1e3", value) == ParseError::BadChar);
}

void test_output_untouched_on_failure() {
    // 重要契约：解析失败时不应该修改 out。
    // 这条测试能挡住「先写一半再发现错误」的实现（那种实现会让调用者
    // 在失败路径上读到半成品，是最难查的一类 bug）。
    int value = 777;
    (void)parse_int_strict("abc", value);
    CHECK_EQ(value, 777);

    (void)parse_int_strict("999999999999", value);
    CHECK_EQ(value, 777);

    (void)parse_int_strict("", value);
    CHECK_EQ(value, 777);
}

}  // namespace

int main() {
    std::cout << "==== 测试：checked_int（严格整数解析）====\n";

    test_normal_cases();
    test_boundaries();
    test_invalid_input();
    test_output_untouched_on_failure();

    // 兜底：如果断言宏写错导致一个都没跑，这里会暴露出来
    if (CHECK_TOTAL() < 20) {
        std::cout << "  [FAIL] 执行的检查项太少（" << CHECK_TOTAL() << "），测试可能没被正确调用\n";
        return 1;
    }
    return mini_test::summary("checked_int");
}
