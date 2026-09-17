// ============================================================================
//  tests/test_ring_buffer.cpp —— 环形缓冲区的单元测试
// ----------------------------------------------------------------------------
//  测试组织方式同 test_checked_int.cpp：一个函数 = 一组断言。
//
//  覆盖策略：
//    1) 空缓冲区：size/full/back/at 的行为
//    2) 未满时：push 后 size 增长，逻辑顺序 = 插入顺序
//    3) 写满后：push 覆盖最旧的元素（这是最容易写错的地方）
//    4) 环绕多圈：连续 push 远超容量的数据，检查顺序仍然正确
//    5) 边界：容量为 1、容量为 0（构造函数抛异常）
//    6) 昂贵类型：std::string 的移动入队（配合 04 文件讲的减少拷贝）
// ============================================================================
#include "ring_buffer.h"

#include "mini_test.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using eng07::RingBuffer;

void test_empty() {
    const RingBuffer<int> buffer(4);
    CHECK_EQ(buffer.size(), std::size_t{0});
    CHECK_EQ(buffer.capacity(), std::size_t{4});
    CHECK_TRUE(buffer.empty());
    CHECK_FALSE(buffer.full());
    CHECK_FALSE(buffer.at(0).has_value());
    CHECK_FALSE(buffer.back().has_value());
}

void test_partially_filled() {
    RingBuffer<int> buffer(4);
    buffer.push(10);
    buffer.push(20);
    buffer.push(30);

    CHECK_EQ(buffer.size(), std::size_t{3});
    CHECK_TRUE(buffer.full() == false);
    CHECK_EQ(buffer.at(0).value(), 10);  // 0 = 最旧
    CHECK_EQ(buffer.at(1).value(), 20);
    CHECK_EQ(buffer.at(2).value(), 30);  // size-1 = 最新
    CHECK_EQ(buffer.back().value(), 30);
    CHECK_FALSE(buffer.at(3).has_value());  // 逻辑下标 3 还不存在
}

void test_overwrite_oldest() {
    RingBuffer<int> buffer(3);
    buffer.push(1);
    buffer.push(2);
    buffer.push(3);
    CHECK_TRUE(buffer.full());

    buffer.push(4);  // 覆盖最旧的 1
    CHECK_EQ(buffer.size(), std::size_t{3});
    CHECK_EQ(buffer.at(0).value(), 2);
    CHECK_EQ(buffer.at(1).value(), 3);
    CHECK_EQ(buffer.at(2).value(), 4);
    CHECK_EQ(buffer.back().value(), 4);

    buffer.push(5);  // 覆盖 2
    CHECK_EQ(buffer.at(0).value(), 3);
    CHECK_EQ(buffer.at(1).value(), 4);
    CHECK_EQ(buffer.at(2).value(), 5);
}

void test_wrap_around_many_times() {
    // 连续写 10 个数据到容量 3 的缓冲区：最后应该是 8、9、10
    RingBuffer<int> buffer(3);
    for (int i = 1; i <= 10; ++i) {
        buffer.push(i);
    }
    CHECK_EQ(buffer.size(), std::size_t{3});
    CHECK_EQ(buffer.at(0).value(), 8);
    CHECK_EQ(buffer.at(1).value(), 9);
    CHECK_EQ(buffer.at(2).value(), 10);
    CHECK_EQ(buffer.back().value(), 10);
    // 越界访问依然安全（返回 nullopt，而不是崩溃）
    CHECK_FALSE(buffer.at(3).has_value());
    CHECK_FALSE(buffer.at(100).has_value());
}

void test_capacity_one() {
    // 容量 1 是最容易出取模错误的边界情况（(0+1)%1 == 0）
    RingBuffer<int> buffer(1);
    buffer.push(42);
    CHECK_TRUE(buffer.full());
    CHECK_EQ(buffer.back().value(), 42);
    buffer.push(43);
    CHECK_EQ(buffer.size(), std::size_t{1});
    CHECK_EQ(buffer.at(0).value(), 43);
    CHECK_EQ(buffer.back().value(), 43);
}

void test_capacity_zero_throws() {
    bool threw = false;
    try {
        const RingBuffer<int> invalid(0);
        (void)invalid;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK_TRUE(threw);  // 容量 0 是编程错误，必须在构造时就被拒绝
}

void test_string_payload() {
    // 用 string 作为元素类型：既测了「非 POD 类型」，也顺便演示移动入队
    RingBuffer<std::string> buffer(2);
    buffer.push(std::string("first"));
    buffer.push(std::string("second"));
    CHECK_EQ(buffer.at(0).value(), std::string("first"));
    CHECK_EQ(buffer.back().value(), std::string("second"));

    std::string third = "third";
    buffer.push(std::move(third));  // 移动入队：third 被搬空，避免一次深拷贝
    CHECK_EQ(buffer.at(0).value(), std::string("second"));
    CHECK_EQ(buffer.at(1).value(), std::string("third"));
}

}  // namespace

int main() {
    std::cout << "==== 测试：RingBuffer（定长环形缓冲区）====\n";

    test_empty();
    test_partially_filled();
    test_overwrite_oldest();
    test_wrap_around_many_times();
    test_capacity_one();
    test_capacity_zero_throws();
    test_string_payload();

    if (CHECK_TOTAL() < 20) {
        std::cout << "  [FAIL] 执行的检查项太少（" << CHECK_TOTAL() << "）\n";
        return 1;
    }
    return mini_test::summary("RingBuffer");
}
