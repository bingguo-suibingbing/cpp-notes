// ============================================================================
//  tests/ring_buffer.h —— 定长环形缓冲区（固定容量，不分配内存）
// ----------------------------------------------------------------------------
//  用途（真实工程里非常常见）：
//    - 采集数据的滑动窗口（最近 N 个采样）
//    - 串口/网络收包缓冲
//    - 日志的「最近 N 条」缓存
//    - 音频/视频的抖动缓冲
//  特点：
//    - 容量在构造时固定，之后**不再分配内存**（实时系统最看重这一点）；
//    - push 覆盖最旧的数据（overwrite 语义），不会失败、不会抛异常；
//    - 随机访问是 O(1)（用取模把逻辑下标映射到物理下标）。
//
//  本类演示「不变量由类保证」：
//    - size_ <= capacity_ 永远成立；
//    - head_ 始终指向「下一个要写入的位置」；
//    - 只有 size_ == capacity_ 时才需要覆盖，覆盖时同步推进 head_。
//  每次修改后都调用 check_invariant()（Debug 下断言），一旦破坏立刻暴露。
// ============================================================================
#pragma once

#include <cassert>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace eng07 {

template <typename T>
class RingBuffer {
public:
    // explicit：避免 `RingBuffer<int> b = 8;` 这种意图不明的隐式转换
    explicit RingBuffer(std::size_t capacity) : data_(capacity), head_(0), size_(0) {
        if (capacity == 0) {
            // 容量为 0 的环形缓冲区没有意义 -> 这是调用者的编程错误，
            // 但构造函数抛异常比断言更安全（对象根本不该被创建出来）。
            throw std::invalid_argument("RingBuffer：容量必须大于 0");
        }
        check_invariant();
    }

    // push：满了就覆盖最旧的元素（不失败、不抛异常、不分配）
    void push(const T& value) {
        data_[head_] = value;
        head_ = (head_ + 1) % data_.size();
        if (size_ < data_.size()) {
            ++size_;
        }
        check_invariant();
    }

    // 移动版：对于昂贵类型（std::string）能省一次拷贝
    void push(T&& value) {
        data_[head_] = std::move(value);
        head_ = (head_ + 1) % data_.size();
        if (size_ < data_.size()) {
            ++size_;
        }
        check_invariant();
    }

    // 按逻辑下标访问：0 = 最旧的元素，size()-1 = 最新的元素
    // 返回 optional 而不是抛异常：越界是「调用者没查 size()」，属于编程错误，
    // 但这里选择「明确失败」而不是断言崩溃 —— 两种做法都可以，关键是文档写清楚。
    [[nodiscard]] std::optional<T> at(std::size_t logical_index) const {
        if (logical_index >= size_) {
            return std::nullopt;
        }
        // 逻辑下标 -> 物理下标：最旧的元素在 (head_ - size_ + capacity) % capacity
        const std::size_t oldest = (head_ + data_.size() - size_) % data_.size();
        return data_[(oldest + logical_index) % data_.size()];
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return data_.size(); }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] bool full() const noexcept { return size_ == data_.size(); }

    // 最新写入的元素（空缓冲区返回 nullopt）
    [[nodiscard]] std::optional<T> back() const {
        if (size_ == 0) {
            return std::nullopt;
        }
        return data_[(head_ + data_.size() - 1) % data_.size()];
    }

private:
    // 类不变量：size_ 不超过容量；head_ 落在 [0, capacity)
    void check_invariant() const {
        assert(size_ <= data_.size() && "RingBuffer 不变量被破坏：size_ 超过容量");
        assert(head_ < data_.size() && "RingBuffer 不变量被破坏：head_ 越界");
    }

    std::vector<T> data_;  // 一次性分配，之后不再分配
    std::size_t head_;     // 下一个写入位置
    std::size_t size_;     // 当前元素个数
};

}  // namespace eng07
