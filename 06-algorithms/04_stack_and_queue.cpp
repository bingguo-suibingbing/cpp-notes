// ============================================================================
// 04_stack_and_queue.cpp
// 演示主题：
//   1. 栈与队列的两种底层：连续数组（缓存友好）与链表（节点指针）
//   2. 循环队列（环形缓冲）的「空 / 满」判断三种方案
//   3. 下标环绕的两种写法：取模 % 与位运算 & (cap-1)，以及后者的前提
//   4. MinStack：辅助栈法 O(1) 取最小值，以及差值编码的进阶技巧
//   5. 用两个栈实现队列：为什么是均摊 O(1)
//   6. std::stack / std::queue 是容器适配器（默认底层 std::deque）
//   7. 栈的经典应用：括号匹配、中缀转后缀（调度场算法的简化版）、后缀求值
//
// 关键结论：
//   1. 数组栈几乎总是快过链表栈：连续内存让 CPU 预取器有效，还省掉每个节点的
//      指针开销与一次堆分配；链表的价值在于「不会因扩容而整体搬移」；
//   2. 循环队列的「满」判断里，维护 size_ 计数器是最简单也最不容易写错的方案；
//      「浪费一个槽位」不花额外变量但要牺牲一个槽位；full_ 标志位等价但多一个分支状态；
//   3. 位运算环绕 index & (cap-1) 比取模 % 略快，前提是容量必须是 2 的幂；
//   4. MinStack 辅助栈最坏 O(n)、最好 O(1) 额外空间；差值编码法只要 O(1) 额外空间，
//      但差值一旦溢出就全错，是「面试加分、生产减分」的技巧；
//   5. 两个栈实现队列是均摊 O(1)：每个元素一生最多被从 in 栈搬到 out 栈一次；
//   6. 栈 / 队列这种「持有资源」的容器必须写完整的 Rule of Five，
//      否则拷贝一次就会 double free（编译器生成的浅拷贝只复制指针）；
//   7. 中缀转后缀只要「运算符栈 + 优先级表」，一元负号靠「当前位置是否期待操作数」识别。
//
// 说明：本文件在 Debug（/Od）下编译运行，绝对耗时比 Release 慢很多，
//       所有数字仅供参考，看「趋势」和「数量级差异」才有意义。
//       教学用途，生产请用 std::stack / std::queue / std::deque。
// ============================================================================

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <iostream>
#include <list>
#include <memory>
#include <new>
#include <queue>
#include <random>
#include <stack>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// 防止测量结果被优化掉
// ---------------------------------------------------------------------------
// 编译器（尤其 Release 的 /O2）发现一个纯计算的返回值没人用，会直接把整段代码删掉，
// 于是你测出来的是「0 毫秒」——这不是算法快，是你的代码根本没跑。
// 解决办法：把结果写进一个 volatile 全局变量。volatile 强制产生真实的读写副作用，
// 编译器不能假设它没用，也就不能删掉计算过程。
//
// 注意 C++20 起 volatile 的复合赋值（g_sink += r）和 ++/-- 已被弃用（P1152），
// 所以下面坚持写成「先读、再算、再写」的展开形式。
volatile std::uint64_t g_sink = 0;

// ---------------------------------------------------------------------------
// 打印小工具
// ---------------------------------------------------------------------------
void Section(const std::string& title) {
    std::cout << "\n============================================================\n";
    std::cout << title << "\n";
    std::cout << "============================================================\n";
}

void Note(const std::string& text) { std::cout << "  " << text << "\n"; }

// 估算字符串在终端里占的「显示列数」。
// 这里有个必踩的坑：std::setw 数的是 char 的个数，而一个中文字符在 UTF-8 里占 3 个
// 字节、在终端里显示为 2 列。所以直接用 setw 去对齐含中文的表头和数字，结果一定是歪的。
// 自己算显示宽度、自己补空格，表格才是齐的。
std::size_t DisplayWidth(const std::string& s) {
    std::size_t width = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {                 // ASCII：1 字节 1 列
            width += 1;
            i += 1;
        } else if ((c >> 5) == 0x06) {  // 2 字节序列
            width += 2;
            i += 2;
        } else if ((c >> 4) == 0x0E) {  // 3 字节序列（中文、中文标点）
            width += 2;
            i += 3;
        } else {                        // 4 字节序列
            width += 2;
            i += 4;
        }
    }
    return width;
}

// 左对齐打印标签并补空格到指定显示宽度，这样后面的数字就能对齐到同一列。
void Label(const std::string& text, std::size_t total_width) {
    std::cout << text;
    const std::size_t w = DisplayWidth(text);
    if (w < total_width) {
        std::cout << std::string(total_width - w, ' ');
    }
}

// ---------------------------------------------------------------------------
// 计时核心：重复多次，取中位数
// ---------------------------------------------------------------------------
// 为什么是「中位数」而不是「平均值」？
//   操作系统调度、其他进程抢 CPU、缓存被换出，都会让某一次测量突然变得很慢。
//   平均值会被这些异常值拉高，中位数则对异常值免疫——它回答的是
//   「典型的一次到底要多久」。
//
// 为什么用 steady_clock 而不是 system_clock？
//   steady_clock 是单调时钟，不会被 NTP 校时或用户改系统时间影响。
//   测「经过了多少时间」永远用 steady_clock。
//
// 返回值单位：微秒（us）。
template <typename Fn>
double BenchMedianUs(Fn&& fn, int repeats) {
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(repeats));

    for (int i = 0; i < repeats; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        const std::uint64_t result = fn();
        const auto t1 = std::chrono::steady_clock::now();

        // 这一行是「防止被优化掉」的落点：结果必须真正被用掉。
        g_sink = g_sink + result;

        const std::chrono::duration<double, std::micro> dt = t1 - t0;
        samples.push_back(dt.count());
    }

    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];  // 中位数
}

// 预热：第一次调用会带上「缺页中断 + 缓存冷启动 + 分支预测器未训练」的开销。
template <typename Fn>
void WarmUp(Fn&& fn, int times) {
    for (int i = 0; i < times; ++i) {
        g_sink = g_sink + fn();
    }
}

// 把「一次操作」重复 repeat 次并累加结果。
// O(1) 的操作只要几纳秒，而计时器分辨率在百纳秒量级，直接测一次测到的全是噪声。
// 重复 repeat 次只是给总耗时乘一个常数，不改变增长阶，却能把测量抬离噪声区。
template <typename Fn>
std::uint64_t Repeat(Fn&& fn, int repeat) {
    std::uint64_t acc = 0;
    for (int r = 0; r < repeat; ++r) {
        acc += fn();
    }
    return acc;
}

// ===========================================================================
// 第 0 部分：底层内存工具
// ===========================================================================
// RawStorage 只做一件事：分配 / 释放「未构造对象」的原始内存。
// 它不构造对象、不析构对象、不记录 size——对象的生命周期由各容器自己用
// std::construct_at / std::destroy_at 管。
//
// 为什么不直接 new T[n]？因为 new T[n] 要求 T 可默认构造，而且会立刻构造 n 个对象。
// 一个栈只需要构造「已经压进去的那几个」，数组里剩下的部分必须是「未初始化内存」。
// 这就是标准库容器「容量（capacity）和大小（size）分开算」的根本原因。
template <typename T>
class RawStorage {
public:
    RawStorage() noexcept = default;
    explicit RawStorage(std::size_t capacity) : data_(Allocate(capacity)) {}

    ~RawStorage() { ::operator delete(data_); }

    // 原始内存的拥有者，禁止拷贝（拷贝一块「不知道构造了几个对象」的内存毫无意义）。
    RawStorage(const RawStorage&) = delete;
    RawStorage& operator=(const RawStorage&) = delete;
    RawStorage(RawStorage&& other) noexcept : data_(other.data_) { other.data_ = nullptr; }
    RawStorage& operator=(RawStorage&& other) noexcept {
        if (this != &other) {
            ::operator delete(data_);
            data_ = other.data_;
            other.data_ = nullptr;
        }
        return *this;
    }

    T* data() const noexcept { return data_; }

    // 注意 ::operator new 只保证 max_align_t 对齐；对 over-aligned 类型（如 alignas(64)）
    // 需要改用带 std::align_val_t 的重载，这里为了教学简洁不做处理。
    static T* Allocate(std::size_t capacity) {
        if (capacity == 0) {
            return nullptr;
        }
        return static_cast<T*>(::operator new(capacity * sizeof(T)));
    }

private:
    T* data_ = nullptr;
};

// ===========================================================================
// 1. 栈：数组实现
// ===========================================================================
// 时间复杂度：push / pop / top 都是 O(1)（扩容那一次是 O(n)，均摊到每次仍为 O(1)）。
// 空间复杂度：O(n)，另有最多到 2 倍的容量冗余（和 std::vector 一样按倍数扩容）。
// 工程要点：完整 Rule of Five、强异常安全（拷贝赋值用 copy-and-swap）、
//           移动后不抛异常（noexcept，便于放进标准容器）。
// 教学用途，生产请用 std::stack。
template <typename T>
class ArrayStack {
public:
    ArrayStack() noexcept = default;
    explicit ArrayStack(std::size_t initial_capacity) { Reserve(initial_capacity); }

    ~ArrayStack() { Clear(); }

    // 拷贝构造：深拷贝，只拷贝「有对象的那些槽位」，其余保持未初始化。
    ArrayStack(const ArrayStack& other) { CopyFrom(other); }

    // 拷贝赋值：copy-and-swap。先在临时对象里完整拷贝，成功了再交换，
    // 这样即使中途抛异常（比如 T 的拷贝构造抛了），*this 也保持原样。
    ArrayStack& operator=(const ArrayStack& other) {
        if (this != &other) {
            ArrayStack tmp(other);
            Swap(tmp);
        }
        return *this;
    }

    // 移动构造：只偷指针，O(1)，不抛异常。被移动的对象变成空栈。
    ArrayStack(ArrayStack&& other) noexcept
        : buf_(std::move(other.buf_)), size_(other.size_), capacity_(other.capacity_) {
        other.size_ = 0;
        other.capacity_ = 0;
    }

    ArrayStack& operator=(ArrayStack&& other) noexcept {
        if (this != &other) {
            Clear();  // 先析构自己的元素
            buf_ = std::move(other.buf_);  // 再接管对方的内存（旧内存由 buf_ 的移动赋值释放）
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }

    void push(const T& value) {
        EnsureCapacity(size_ + 1);
        std::construct_at(Data() + size_, value);
        ++size_;
    }

    void push(T&& value) {
        EnsureCapacity(size_ + 1);
        std::construct_at(Data() + size_, std::move(value));
        ++size_;
    }

    void pop() {
        if (size_ == 0) {
            throw std::out_of_range("ArrayStack::pop：栈为空");
        }
        --size_;
        std::destroy_at(Data() + size_);  // 先缩小 size 再析构，异常安全
    }

    T& top() {
        if (size_ == 0) {
            throw std::out_of_range("ArrayStack::top：栈为空");
        }
        return Data()[size_ - 1];
    }

    const T& top() const {
        if (size_ == 0) {
            throw std::out_of_range("ArrayStack::top：栈为空");
        }
        return Data()[size_ - 1];
    }

    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }
    std::size_t capacity() const noexcept { return capacity_; }

    void clear() noexcept { Clear(); }

    void reserve(std::size_t new_capacity) { Reserve(new_capacity); }

private:
    T* Data() const noexcept { return buf_.data(); }

    void Clear() noexcept {
        for (std::size_t i = 0; i < size_; ++i) {
            std::destroy_at(Data() + i);
        }
        size_ = 0;
    }

    void Swap(ArrayStack& other) noexcept {
        std::swap(buf_, other.buf_);
        std::swap(size_, other.size_);
        std::swap(capacity_, other.capacity_);
    }

    void CopyFrom(const ArrayStack& other) {
        if (other.size_ == 0) {
            return;
        }
        buf_ = RawStorage<T>(other.size_);
        capacity_ = other.size_;
        std::size_t constructed = 0;
        try {
            for (; constructed < other.size_; ++constructed) {
                std::construct_at(Data() + constructed, other.Data()[constructed]);
            }
        } catch (...) {
            // 拷贝到一半抛异常：把自己已经构造出来的部分析构掉再往外抛。
            for (std::size_t j = 0; j < constructed; ++j) {
                std::destroy_at(Data() + j);
            }
            size_ = 0;
            throw;
        }
        size_ = other.size_;
    }

    void EnsureCapacity(std::size_t needed) {
        if (needed <= capacity_) {
            return;
        }
        std::size_t new_capacity = capacity_ == 0 ? 4 : capacity_;
        while (new_capacity < needed) {
            new_capacity *= 2;
        }
        Reserve(new_capacity);
    }

    // 扩容：申请新内存 -> 把元素搬过去 -> 释放旧内存。
    // 中途抛异常时旧数据完好无损，这是「强异常安全」的关键。
    void Reserve(std::size_t new_capacity) {
        if (new_capacity <= capacity_) {
            return;
        }
        RawStorage<T> fresh(new_capacity);
        std::size_t moved = 0;
        try {
            for (; moved < size_; ++moved) {
                // 栈顶附近的元素很快会被 pop，搬移用拷贝即可；这里优先保证异常安全，
                // 所以不用 std::move_if_noexcept 之外的激进策略。
                std::construct_at(fresh.data() + moved, Data()[moved]);
            }
        } catch (...) {
            for (std::size_t j = 0; j < moved; ++j) {
                std::destroy_at(fresh.data() + j);
            }
            throw;
        }
        // 注意：这里不能调用 Clear()，因为它会把 size_ 清零。
        // 扩容的过程中元素个数没有变化，必须只析构旧内存上的对象、保留 size_。
        for (std::size_t i = 0; i < size_; ++i) {
            std::destroy_at(Data() + i);
        }
        buf_ = std::move(fresh);
        capacity_ = new_capacity;
    }

    RawStorage<T> buf_;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
};

// ===========================================================================
// 2. 栈：单链表实现
// ===========================================================================
// 时间复杂度：push / pop / top 都是 O(1)（严格 O(1)，没有均摊，不搬移任何元素）。
// 空间复杂度：O(n)，但每个元素额外背一个指针（8 字节），且节点散落在堆上。
// 对比要点：没有「扩容」概念，因此不受「扩容瞬间 O(n)」影响；
//           代价是每次 push 一次 new、每次 pop 一次 delete，缓存局部性极差。
// 教学用途，生产请用 std::stack。
template <typename T>
class LinkedStack {
private:
    struct Node {
        T value;
        Node* next;
        Node(const T& v, Node* n) : value(v), next(n) {}
        Node(T&& v, Node* n) : value(std::move(v)), next(n) {}
    };

public:
    LinkedStack() noexcept = default;

    ~LinkedStack() { Clear(); }

    LinkedStack(const LinkedStack& other) { CopyFrom(other); }

    LinkedStack& operator=(const LinkedStack& other) {
        if (this != &other) {
            LinkedStack tmp(other);
            Swap(tmp);
        }
        return *this;
    }

    LinkedStack(LinkedStack&& other) noexcept : head_(other.head_), size_(other.size_) {
        other.head_ = nullptr;
        other.size_ = 0;
    }

    LinkedStack& operator=(LinkedStack&& other) noexcept {
        if (this != &other) {
            Clear();
            head_ = other.head_;
            size_ = other.size_;
            other.head_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    void push(const T& value) {
        // 先 new 成功再改 head_：new 抛异常时栈保持原样（强异常安全）。
        head_ = new Node(value, head_);
        ++size_;
    }

    void push(T&& value) {
        head_ = new Node(std::move(value), head_);
        ++size_;
    }

    void pop() {
        if (head_ == nullptr) {
            throw std::out_of_range("LinkedStack::pop：栈为空");
        }
        Node* victim = head_;
        head_ = head_->next;
        delete victim;
        --size_;
    }

    T& top() {
        if (head_ == nullptr) {
            throw std::out_of_range("LinkedStack::top：栈为空");
        }
        return head_->value;
    }

    const T& top() const {
        if (head_ == nullptr) {
            throw std::out_of_range("LinkedStack::top：栈为空");
        }
        return head_->value;
    }

    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return head_ == nullptr; }

    void clear() noexcept { Clear(); }

private:
    void Swap(LinkedStack& other) noexcept {
        std::swap(head_, other.head_);
        std::swap(size_, other.size_);
    }

    void Clear() noexcept {
        while (head_ != nullptr) {
            Node* victim = head_;
            head_ = head_->next;
            delete victim;
        }
        size_ = 0;
    }

    void CopyFrom(const LinkedStack& other) {
        Node* tail = nullptr;
        try {
            for (Node* p = other.head_; p != nullptr; p = p->next) {
                Node* fresh = new Node(p->value, nullptr);
                if (tail == nullptr) {
                    head_ = fresh;
                } else {
                    tail->next = fresh;
                }
                tail = fresh;
                ++size_;
            }
        } catch (...) {
            Clear();
            throw;
        }
    }

    Node* head_ = nullptr;
    std::size_t size_ = 0;
};

// ===========================================================================
// 3. 循环队列：size_ 计数器法 + 两种下标环绕
// ===========================================================================
// 方案二（本类采用）：额外维护 size_，满 = (size_ == capacity_)，空 = (size_ == 0)。
//   优点：语义最直白，不用为「满」和「空」的歧义做任何妥协，也不会浪费槽位；
//   缺点：多一个计数器（要跟着 push / pop 维护，别漏改）。
//
// kMaskWrap = true 时刻意要求容量向上取整到 2 的幂，于是：
//   index & (capacity_ - 1)  等价于  index % capacity_
//   为什么等价：capacity_ 是 2 的幂时 (capacity_-1) 的低 k 位全是 1，
//   与运算相当于「只保留低 k 位」，正好就是模 2^k 的语义。
//   为什么更快：位运算是单周期指令，取模要跑除法器（x86 上 div 是几十个周期）。
//   前提：容量必须是 2 的幂！否则 & 得到的结果根本不是正确的下标，会静默写坏数据。
//
// 时间复杂度：push / pop / front / back 全部 O(1)，无均摊、无分配。
// 空间复杂度：O(capacity)，预先分配固定容量（这是环形缓冲的取舍：
//             用「容量写死」换「零分配 + 稳定 O(1)」）。
// 教学用途，生产请用 std::queue（或环形缓冲库）。
template <typename T, bool kMaskWrap>
class RingQueue {
public:
    explicit RingQueue(std::size_t requested_capacity)
        : buf_(Normalize(requested_capacity)), capacity_(Normalize(requested_capacity)) {}

    ~RingQueue() { Clear(); }

    RingQueue(const RingQueue& other) : buf_(other.capacity_), capacity_(other.capacity_) {
        std::size_t constructed = 0;
        try {
            for (; constructed < other.size_; ++constructed) {
                std::construct_at(Data() + constructed, other.LogicalAt(constructed));
            }
        } catch (...) {
            for (std::size_t j = 0; j < constructed; ++j) {
                std::destroy_at(Data() + j);
            }
            size_ = 0;
            throw;
        }
        size_ = other.size_;
    }

    RingQueue& operator=(const RingQueue& other) {
        if (this != &other) {
            RingQueue tmp(other);
            Swap(tmp);
        }
        return *this;
    }

    RingQueue(RingQueue&& other) noexcept
        : buf_(std::move(other.buf_)),
          capacity_(other.capacity_),
          head_(other.head_),
          size_(other.size_) {
        other.capacity_ = 0;
        other.head_ = 0;
        other.size_ = 0;
    }

    RingQueue& operator=(RingQueue&& other) noexcept {
        if (this != &other) {
            Clear();
            buf_ = std::move(other.buf_);
            capacity_ = other.capacity_;
            head_ = other.head_;
            size_ = other.size_;
            other.capacity_ = 0;
            other.head_ = 0;
            other.size_ = 0;
        }
        return *this;
    }

    void push(const T& value) {
        if (size_ == capacity_) {
            throw std::out_of_range("RingQueue::push：队列已满");
        }
        std::construct_at(Data() + Physical(size_), value);
        ++size_;
    }

    void push(T&& value) {
        if (size_ == capacity_) {
            throw std::out_of_range("RingQueue::push：队列已满");
        }
        std::construct_at(Data() + Physical(size_), std::move(value));
        ++size_;
    }

    void pop() {
        if (size_ == 0) {
            throw std::out_of_range("RingQueue::pop：队列为空");
        }
        std::destroy_at(Data() + head_);
        head_ = Wrap(head_ + 1);
        --size_;
    }

    T& front() {
        if (size_ == 0) {
            throw std::out_of_range("RingQueue::front：队列为空");
        }
        return Data()[head_];
    }

    const T& front() const {
        if (size_ == 0) {
            throw std::out_of_range("RingQueue::front：队列为空");
        }
        return Data()[head_];
    }

    T& back() {
        if (size_ == 0) {
            throw std::out_of_range("RingQueue::back：队列为空");
        }
        return Data()[Physical(size_ - 1)];
    }

    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }
    bool full() const noexcept { return size_ == capacity_; }
    std::size_t capacity() const noexcept { return capacity_; }

    void clear() noexcept { Clear(); }

private:
    T* Data() const noexcept { return buf_.data(); }

    // 逻辑下标 -> 物理下标。head_ 是队头所在的物理位置。
    T& LogicalAt(std::size_t logical) const { return buf_.data()[Physical(logical)]; }

    std::size_t Physical(std::size_t logical) const noexcept { return Wrap(head_ + logical); }

    std::size_t Wrap(std::size_t index) const noexcept {
        if constexpr (kMaskWrap) {
            return index & (capacity_ - 1);  // 容量是 2 的幂，等价于取模且更快
        } else {
            return index % capacity_;
        }
    }

    static std::size_t Normalize(std::size_t requested) noexcept {
        std::size_t cap = requested < 2 ? 2 : requested;
        if constexpr (kMaskWrap) {
            std::size_t power = 1;
            while (power < cap) {
                power *= 2;
            }
            cap = power;
        }
        return cap;
    }

    void Clear() noexcept {
        for (std::size_t i = 0; i < size_; ++i) {
            std::destroy_at(Data() + Physical(i));
        }
        size_ = 0;
        head_ = 0;
    }

    void Swap(RingQueue& other) noexcept {
        std::swap(buf_, other.buf_);
        std::swap(capacity_, other.capacity_);
        std::swap(head_, other.head_);
        std::swap(size_, other.size_);
    }

    RawStorage<T> buf_;
    std::size_t capacity_ = 0;
    std::size_t head_ = 0;  // 队头下标
    std::size_t size_ = 0;  // 元素个数（空 / 满判断全靠它）
};

// ===========================================================================
// 4. 循环队列：浪费一个槽位法
// ===========================================================================
// 方案一：不额外记录 size，只用 head_ / tail_ 两个下标（tail_ 指向「下一个可写的空位」）。
//   空：head_ == tail_
//   满：(tail_ + 1) % capacity_ == head_
// 代价是永远有一个槽位空着，实际最多存 capacity_ - 1 个元素。
//
// 为什么必须浪费一个？因为如果允许塞满，那么「塞满」时 tail_ 绕一圈回来正好等于
// head_，此时「满」和「空」的判定条件完全一样，两者无法区分。
// 三种解法就是在「怎么区分这两种状态」上做文章：
//   1) 浪费一个槽位（本类）：不动用额外变量，改用 (tail_+1)%cap == head_ 判满；
//   2) size_ 计数器（上一类）：额外一个整数，语义最清晰，推荐；
//   3) full_ 布尔标志：push 到 head_ == tail_ 时置 full_ = true，pop 后清掉；
//      它能省掉计数器、也不浪费槽位，但状态从一个「数值不变量」变成两个变量共同描述，
//      忘记维护标志位会静默出错，可读性最差。
//
// 时间复杂度：全部 O(1)。空间复杂度：O(capacity)，其中可用的只有 capacity_ - 1 个。
template <typename T>
class WasteOneRingQueue {
public:
    explicit WasteOneRingQueue(std::size_t requested_capacity)
        : buf_(AtLeastTwo(requested_capacity)), capacity_(AtLeastTwo(requested_capacity)) {}

    ~WasteOneRingQueue() { Clear(); }

    WasteOneRingQueue(const WasteOneRingQueue& other)
        : buf_(other.capacity_), capacity_(other.capacity_) {
        const std::size_t count = other.size();
        std::size_t constructed = 0;
        try {
            for (; constructed < count; ++constructed) {
                std::construct_at(Data() + constructed, other.buf_.data()[
                    other.Wrap(other.head_ + constructed)]);
            }
        } catch (...) {
            for (std::size_t j = 0; j < constructed; ++j) {
                std::destroy_at(Data() + j);
            }
            tail_ = 0;
            throw;
        }
        tail_ = count;  // 拷贝后压紧到头部：head_ = 0，tail_ = size
    }

    WasteOneRingQueue& operator=(const WasteOneRingQueue& other) {
        if (this != &other) {
            WasteOneRingQueue tmp(other);
            Swap(tmp);
        }
        return *this;
    }

    WasteOneRingQueue(WasteOneRingQueue&& other) noexcept
        : buf_(std::move(other.buf_)),
          capacity_(other.capacity_),
          head_(other.head_),
          tail_(other.tail_) {
        other.capacity_ = 0;
        other.head_ = 0;
        other.tail_ = 0;
    }

    WasteOneRingQueue& operator=(WasteOneRingQueue&& other) noexcept {
        if (this != &other) {
            Clear();
            buf_ = std::move(other.buf_);
            capacity_ = other.capacity_;
            head_ = other.head_;
            tail_ = other.tail_;
            other.capacity_ = 0;
            other.head_ = 0;
            other.tail_ = 0;
        }
        return *this;
    }

    void push(const T& value) {
        if (full()) {
            throw std::out_of_range("WasteOneRingQueue::push：队列已满（可用槽位是 capacity - 1）");
        }
        std::construct_at(Data() + tail_, value);
        tail_ = Wrap(tail_ + 1);
    }

    void pop() {
        if (empty()) {
            throw std::out_of_range("WasteOneRingQueue::pop：队列为空");
        }
        std::destroy_at(Data() + head_);
        head_ = Wrap(head_ + 1);
    }

    T& front() {
        if (empty()) {
            throw std::out_of_range("WasteOneRingQueue::front：队列为空");
        }
        return Data()[head_];
    }

    T& back() {
        if (empty()) {
            throw std::out_of_range("WasteOneRingQueue::back：队列为空");
        }
        return Data()[Wrap(tail_ + capacity_ - 1)];
    }

    bool empty() const noexcept { return head_ == tail_; }
    bool full() const noexcept { return Wrap(tail_ + 1) == head_; }

    std::size_t size() const noexcept { return Wrap(tail_ + capacity_ - head_); }
    std::size_t capacity() const noexcept { return capacity_; }

    void clear() noexcept { Clear(); }

private:
    T* Data() const noexcept { return buf_.data(); }

    std::size_t Wrap(std::size_t index) const noexcept {
        // capacity_ 是构造时固定的非零值，% 一定安全。这里不做 2 的幂假设，
        // 因为「浪费一个槽位」这种方案用户指定的容量往往就是有意义的业务容量。
        return index % capacity_;
    }

    static std::size_t AtLeastTwo(std::size_t requested) noexcept {
        return requested < 2 ? 2 : requested;
    }

    void Clear() noexcept {
        while (!empty()) {
            std::destroy_at(Data() + head_);
            head_ = Wrap(head_ + 1);
        }
        head_ = 0;
        tail_ = 0;
    }

    void Swap(WasteOneRingQueue& other) noexcept {
        std::swap(buf_, other.buf_);
        std::swap(capacity_, other.capacity_);
        std::swap(head_, other.head_);
        std::swap(tail_, other.tail_);
    }

    RawStorage<T> buf_;
    std::size_t capacity_ = 0;
    std::size_t head_ = 0;  // 队头
    std::size_t tail_ = 0;  // 下一个可写位置
};

// ===========================================================================
// 5. 队列：链表实现（head_ / tail_ 双指针）
// ===========================================================================
// 时间复杂度：push / pop / front / back 全部严格 O(1)——因为同时持有队头和队尾指针。
//             （只持有 head 的链表队列，push 会退化成 O(n)，这是经典陷阱。）
// 空间复杂度：O(n)，每元素额外一个指针。
// 教学用途，生产请用 std::queue。
template <typename T>
class LinkedQueue {
private:
    struct Node {
        T value;
        Node* next;
        explicit Node(const T& v) : value(v), next(nullptr) {}
        explicit Node(T&& v) : value(std::move(v)), next(nullptr) {}
    };

public:
    LinkedQueue() noexcept = default;

    ~LinkedQueue() { Clear(); }

    LinkedQueue(const LinkedQueue& other) {
        for (Node* p = other.head_; p != nullptr; p = p->next) {
            AppendCopy(p->value);
        }
    }

    LinkedQueue& operator=(const LinkedQueue& other) {
        if (this != &other) {
            LinkedQueue tmp(other);
            Swap(tmp);
        }
        return *this;
    }

    LinkedQueue(LinkedQueue&& other) noexcept
        : head_(other.head_), tail_(other.tail_), size_(other.size_) {
        other.head_ = nullptr;
        other.tail_ = nullptr;
        other.size_ = 0;
    }

    LinkedQueue& operator=(LinkedQueue&& other) noexcept {
        if (this != &other) {
            Clear();
            head_ = other.head_;
            tail_ = other.tail_;
            size_ = other.size_;
            other.head_ = nullptr;
            other.tail_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    void push(const T& value) { AppendCopy(value); }

    void push(T&& value) {
        Node* fresh = new Node(std::move(value));
        Link(fresh);
    }

    void pop() {
        if (head_ == nullptr) {
            throw std::out_of_range("LinkedQueue::pop：队列为空");
        }
        Node* victim = head_;
        head_ = head_->next;
        if (head_ == nullptr) {
            tail_ = nullptr;  // 队列变空时必须把 tail_ 也清掉，否则 tail_ 悬空
        }
        delete victim;
        --size_;
    }

    T& front() {
        if (head_ == nullptr) {
            throw std::out_of_range("LinkedQueue::front：队列为空");
        }
        return head_->value;
    }

    T& back() {
        if (tail_ == nullptr) {
            throw std::out_of_range("LinkedQueue::back：队列为空");
        }
        return tail_->value;
    }

    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return head_ == nullptr; }

    void clear() noexcept { Clear(); }

private:
    void Link(Node* fresh) noexcept {
        if (tail_ == nullptr) {
            head_ = fresh;
            tail_ = fresh;
        } else {
            tail_->next = fresh;
            tail_ = fresh;
        }
        ++size_;
    }

    void AppendCopy(const T& value) {
        Node* fresh = new Node(value);  // 先分配，成功了再挂链（强异常安全）
        Link(fresh);
    }

    void Clear() noexcept {
        while (head_ != nullptr) {
            Node* victim = head_;
            head_ = head_->next;
            delete victim;
        }
        tail_ = nullptr;
        size_ = 0;
    }

    void Swap(LinkedQueue& other) noexcept {
        std::swap(head_, other.head_);
        std::swap(tail_, other.tail_);
        std::swap(size_, other.size_);
    }

    Node* head_ = nullptr;
    Node* tail_ = nullptr;
    std::size_t size_ = 0;
};

// ===========================================================================
// 6. MinStack：辅助栈法，O(1) 取最小值
// ===========================================================================
// 核心思想：再拿一个栈，专门记录「到当前为止的最小值」。两个栈同步增长、同步收缩。
//
// 为什么可以同步？因为整个过程中最小值的变化是「单调」的：
//   - 只有当新压入的元素 <= 当前最小值时，最小值才会变，此时往辅助栈压一份副本；
//   - 出栈时，如果弹出的元素恰好等于辅助栈栈顶（说明它当年就是最小值），
//     就一起弹掉辅助栈栈顶，此时辅助栈新的栈顶恰好就是「那个元素入栈之前的最小值」。
//   - 必须用 <=（相等也压），否则连续压入两个相同的最小值时，
//     弹掉一个就会把最小值一起弹掉，剩下的那个就再也找不回最小值了。
//
// 空间复杂度：O(n) 最坏（比如依次压入 n, n-1, ..., 1，每次都刷新最小值），
//             O(1) 最好（依次压入 1, 2, ..., n，辅助栈只存了 1 个）。
//             如果改成「每个元素都往辅助栈压一份当前最小值」，那就是稳定的 O(n)。
// 时间复杂度：push / pop / top / min 全部 O(1)。
// 教学用途，生产里也可以用这个套路（但要考虑 T 的比较代价）。
template <typename T>
class MinStack {
public:
    MinStack() = default;

    // 成员 data_ / min_ 都是 ArrayStack，而 ArrayStack 自己已经正确实现了 Rule of Five，
    // 所以编译器生成的版本（逐成员深拷贝 / 移动）就是完全正确的。
    // 这里显式写出来，是为了向读者表明「我已经确认过所有权语义」，而不是听天由命。
    MinStack(const MinStack&) = default;
    MinStack& operator=(const MinStack&) = default;
    MinStack(MinStack&&) noexcept = default;
    MinStack& operator=(MinStack&&) noexcept = default;
    ~MinStack() = default;

    void push(const T& value) {
        data_.push(value);
        if (min_.empty() || value <= min_.top()) {
            min_.push(value);
        }
    }

    void pop() {
        if (data_.empty()) {
            throw std::out_of_range("MinStack::pop：栈为空");
        }
        if (data_.top() == min_.top()) {
            min_.pop();
        }
        data_.pop();
    }

    T& top() { return data_.top(); }

    // O(1) 取最小值——这就是整个数据结构存在的理由。
    const T& min() const { return min_.top(); }

    std::size_t size() const noexcept { return data_.size(); }
    bool empty() const noexcept { return data_.empty(); }

private:
    ArrayStack<T> data_;
    ArrayStack<T> min_;
};

// ---------------------------------------------------------------------------
// 进阶：差值编码法——只用一个栈，额外空间 O(1)
// ---------------------------------------------------------------------------
// 思路：栈里不存元素本身，而存「元素与当时最小值的差」。
//   push x：d = x - cur_min；压入 d；若 d < 0（说明 x 是新的最小值）则 cur_min = x。
//   top   ：若栈顶 d < 0，真实值就是 cur_min；否则真实值 = cur_min + d。
//   pop   ：若弹出的 d < 0，说明被弹掉的是当年的最小值，
//           上一个最小值 = cur_min - d（因为 d = x - prev_min 且 x = cur_min）。
//   为什么这样能还原：d < 0 时栈顶记录的是「新最小值与旧最小值的差」，
//   只靠 cur_min 和 d 就能反推出旧最小值，不需要额外的栈。
//
// 代价（为什么生产里慎用）：
//   1) 差值的类型必须比 T 宽（int 的差要用 long long 存），否则 x - min 会溢出；
//   2) 一旦 T 是 64 位整数就没有更宽的类型可以装差值了，直接失效；
//   3) 浮点数的差值会丢精度，T 是 double 时结果可能对不上；
//   4) 代码可读性差：想取栈顶元素还要先判断符号再反算。
// 所以它是「面试加分项、生产减分项」。这里实现出来只为把原理讲透。
template <typename T>
class MinStackDelta {
    static_assert(std::is_integral_v<T>, "差值编码法只适用于整数类型");
    static_assert(sizeof(T) <= sizeof(long long), "差值必须能被 long long 精确容纳");

public:
    MinStackDelta() = default;

    // 成员是 std::vector<long long> 和 T，都是值语义，Rule of Five 默认版本即正确。
    MinStackDelta(const MinStackDelta&) = default;
    MinStackDelta& operator=(const MinStackDelta&) = default;
    MinStackDelta(MinStackDelta&&) noexcept = default;
    MinStackDelta& operator=(MinStackDelta&&) noexcept = default;
    ~MinStackDelta() = default;

    void push(T value) {
        const long long wide = static_cast<long long>(value);
        if (diff_.empty()) {
            cur_min_ = value;
            diff_.push_back(0);
            return;
        }
        const long long delta = wide - static_cast<long long>(cur_min_);
        diff_.push_back(delta);
        if (delta < 0) {
            cur_min_ = value;  // 出现了新的最小值
        }
    }

    void pop() {
        if (diff_.empty()) {
            throw std::out_of_range("MinStackDelta::pop：栈为空");
        }
        const long long delta = diff_.back();
        diff_.pop_back();
        if (delta < 0) {
            // 被弹掉的就是当年那个最小值，反推回上一个最小值。
            cur_min_ = static_cast<T>(static_cast<long long>(cur_min_) - delta);
        }
    }

    T top() const {
        if (diff_.empty()) {
            throw std::out_of_range("MinStackDelta::top：栈为空");
        }
        const long long delta = diff_.back();
        if (delta < 0) {
            return cur_min_;  // 栈顶就是最小值本身
        }
        return static_cast<T>(static_cast<long long>(cur_min_) + delta);
    }

    T min() const {
        if (diff_.empty()) {
            throw std::out_of_range("MinStackDelta::min：栈为空");
        }
        return cur_min_;
    }

    std::size_t size() const noexcept { return diff_.size(); }
    bool empty() const noexcept { return diff_.empty(); }

private:
    std::vector<long long> diff_;  // 只存差值，不存原始元素
    T cur_min_ = 0;
};

// ===========================================================================
// 7. 用两个栈实现队列
// ===========================================================================
// 不变量：in_ 存「刚入队的元素」（栈顶是最新的），out_ 存「准备出队的元素」（栈顶是最早的）。
//   push：永远压进 in_，O(1)。
//   pop / front：若 out_ 为空，把 in_ 里的元素全部倒进 out_（倒一次顺序就正过来了），
//               然后从 out_ 出栈。
//
// 均摊 O(1) 的推导（关键！）：
//   每个元素的生命周期里，最多被「从 in_ 弹出并压入 out_」一次，也就是最多被搬一次。
//   所以 n 次 push 引发的总搬移次数 <= n，摊到每次出队操作上是 O(1)。
//   单次 pop 最坏是 O(n)（那一次恰好要倒整个 in_），但均摊之后是 O(1)。
//   下面的 moves_ 计数器就是用来实测这个「每个元素恰好被搬一次」的。
// 空间复杂度：O(n)（两个栈加起来装 n 个元素）。
// 教学用途，生产请用 std::queue（它在 std::deque 上做这件事更高效）。
template <typename T>
class TwoStackQueue {
public:
    TwoStackQueue() = default;

    // 两个成员都是 ArrayStack，值语义正确，Rule of Five 默认版本即正确。
    TwoStackQueue(const TwoStackQueue&) = default;
    TwoStackQueue& operator=(const TwoStackQueue&) = default;
    TwoStackQueue(TwoStackQueue&&) noexcept = default;
    TwoStackQueue& operator=(TwoStackQueue&&) noexcept = default;
    ~TwoStackQueue() = default;

    void push(const T& value) { in_.push(value); }

    void pop() {
        PrepareOut();
        out_.pop();
    }

    T& front() {
        PrepareOut();
        return out_.top();
    }

    T& back() {
        if (!in_.empty()) {
            return in_.top();  // 最新的元素永远在 in_ 的栈顶
        }
        if (out_.empty()) {
            throw std::out_of_range("TwoStackQueue::back：队列为空");
        }
        // in_ 空时，最早的元素在 out_ 栈顶，最晚的沉在 out_ 栈底。
        // 这里为了教学不提供「扫到底」的接口，直接抛异常说明不适用。
        throw std::out_of_range("TwoStackQueue::back：in 栈为空时无法 O(1) 取队尾");
    }

    std::size_t size() const noexcept { return in_.size() + out_.size(); }
    bool empty() const noexcept { return in_.empty() && out_.empty(); }

    // 实测用：累计发生的「栈间搬移」次数。
    std::uint64_t moves() const noexcept { return moves_; }

private:
    void PrepareOut() {
        if (!out_.empty()) {
            return;
        }
        if (in_.empty()) {
            throw std::out_of_range("TwoStackQueue：队列为空");
        }
        while (!in_.empty()) {
            out_.push(in_.top());
            in_.pop();
            ++moves_;
        }
    }

    ArrayStack<T> in_;   // 入队栈
    ArrayStack<T> out_;  // 出队栈
    std::uint64_t moves_ = 0;
};

// ===========================================================================
// 8. 栈的经典应用之一：括号匹配
// ===========================================================================
// 思路：遇到左括号就把「它期待的那个右括号」和「它的位置」一起压栈；
//       遇到右括号就看栈顶期待的是不是它——是就弹出，不是就立刻能报出出错位置。
// 时间复杂度 O(n)，空间复杂度 O(n)（最坏全是左括号）。
struct BracketCheck {
    bool ok = true;
    std::size_t position = 0;  // 出错下标（ok 为 true 时无意义）
    std::string message;
};

BracketCheck CheckBrackets(const std::string& text) {
    const std::string openers = "([{";
    const std::string closers = ")]}";

    // 栈里存 pair<期待的右括号, 左括号所在下标>，这样报错时能指出「是哪个左括号」。
    ArrayStack<std::pair<char, std::size_t>> pending;

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const std::size_t open_index = openers.find(c);
        if (open_index != std::string::npos) {
            pending.push({closers[open_index], i});
            continue;
        }
        if (closers.find(c) == std::string::npos) {
            continue;  // 只关心括号，其他字符一律跳过
        }
        if (pending.empty()) {
            return {false, i, std::string("第 ") + std::to_string(i) + " 个字符是多余的右括号 '" +
                                     c + "'，它没有对应的左括号"};
        }
        const std::pair<char, std::size_t> expected = pending.top();
        if (expected.first != c) {
            return {false,
                    i,
                    std::string("第 ") + std::to_string(i) + " 个字符是 '" + c +
                        "'，但这里需要 '" + expected.first + "'（它在第 " +
                        std::to_string(expected.second) + " 个字符打开，至今没有闭合）"};
        }
        pending.pop();
    }

    if (!pending.empty()) {
        // 栈顶是「最后一个」未闭合的左括号；把栈倒空就能拿到「最早」那个，
        // 报最早的位置对使用者更有用（它才是第一个出问题的地方）。
        std::size_t earliest = 0;
        char earliest_char = 0;
        while (!pending.empty()) {
            earliest = pending.top().second;
            earliest_char = text[earliest];
            pending.pop();
        }
        return {false,
                earliest,
                std::string("第 ") + std::to_string(earliest) + " 个字符 '" +
                    std::string(1, earliest_char) + "' 打开后没有闭合"};
    }
    return {};
}

// ===========================================================================
// 9. 栈的经典应用之二：中缀 -> 后缀（调度场算法简化版）-> 后缀求值
// ===========================================================================
// 本节简化了什么（工程上要补的坑）：
//   - 只支持非负整数字面量，不支持小数、科学计数法、十六进制；
//   - 只支持 + - * / 和括号，以及一元负号；不支持 % ^ 幂运算、函数调用、逗号；
//   - 不做静态类型检查（比如 "1 2 +" 这种操作数多余的情况只在求值阶段被兜住）；
//   - 除法是 C++ 的整数除法（向零取整），不是数学除法。
//
// 调度场（shunting-yard）的核心只有两条规则：
//   1) 操作数直接输出到后缀串；
//   2) 运算符进栈之前，把栈里「优先级不低于它（左结合时含相等）」的运算符先弹出来；
//      遇到右括号就一直弹到左括号为止。
// 一元负号的处理技巧：多带一个状态「当前位置是否期待操作数」。
//   刚开始、左括号后、运算符后 => 期待操作数 => 此时的 '-' 是一元负号（给最高优先级）；
//   操作数后、右括号后 => 期待运算符 => 此时的 '-' 是二元减法。
//   为了和后缀求值约定一致，一元负号在 token 里写作 "u-"，优先级 3（高于乘除）。

std::vector<std::string> TokenizeInfix(const std::string& expr) {
    std::vector<std::string> tokens;
    std::size_t i = 0;
    while (i < expr.size()) {
        const char c = expr[i];
        if (c == ' ') {
            ++i;
            continue;
        }
        if (c >= '0' && c <= '9') {
            // 多位数：一口气把连续数字吃完，作为一个 token。
            const std::size_t start = i;
            while (i < expr.size() && expr[i] >= '0' && expr[i] <= '9') {
                ++i;
            }
            tokens.push_back(expr.substr(start, i - start));
            continue;
        }
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '(' || c == ')') {
            tokens.emplace_back(1, c);
            ++i;
            continue;
        }
        throw std::invalid_argument(std::string("无法识别的字符：") + c);
    }
    return tokens;
}

int Precedence(const std::string& op) {
    if (op == "u-") {
        return 3;
    }
    if (op == "*" || op == "/") {
        return 2;
    }
    if (op == "+" || op == "-") {
        return 1;
    }
    return 0;  // 左括号占位用，永远不参与比较
}

bool IsRightAssociative(const std::string& op) { return op == "u-"; }

std::vector<std::string> InfixToPostfix(const std::string& expr) {
    const std::vector<std::string> tokens = TokenizeInfix(expr);
    std::vector<std::string> output;
    ArrayStack<std::string> operators;

    bool expect_operand = true;  // 状态机：true 表示下一个 token 应该是操作数

    for (const std::string& token : tokens) {
        const char c = token[0];
        const bool is_number = token.size() > 1 || (c >= '0' && c <= '9');
        if (is_number) {
            if (!expect_operand) {
                throw std::invalid_argument("表达式语法错误：两个操作数之间缺少运算符");
            }
            output.push_back(token);
            expect_operand = false;
            continue;
        }
        if (c == '(') {
            if (!expect_operand) {
                throw std::invalid_argument("表达式语法错误：操作数与左括号之间缺少运算符");
            }
            operators.push(token);
            expect_operand = true;
            continue;
        }
        if (c == ')') {
            while (!operators.empty() && operators.top() != "(") {
                output.push_back(operators.top());
                operators.pop();
            }
            if (operators.empty()) {
                throw std::invalid_argument("括号不匹配：多了一个右括号");
            }
            operators.pop();  // 丢掉左括号
            expect_operand = false;
            continue;
        }

        std::string op(1, c);
        if (c == '-' && expect_operand) {
            op = "u-";  // 一元负号
        }
        // 栈顶优先级更高、或者优先级相同但当前运算符是左结合的，就先弹出栈顶。
        while (!operators.empty() && operators.top() != "(") {
            const std::string& top_op = operators.top();
            const int top_prec = Precedence(top_op);
            const int cur_prec = Precedence(op);
            if (top_prec > cur_prec || (top_prec == cur_prec && !IsRightAssociative(op))) {
                output.push_back(top_op);
                operators.pop();
            } else {
                break;
            }
        }
        operators.push(op);
        expect_operand = true;
    }

    while (!operators.empty()) {
        if (operators.top() == "(") {
            throw std::invalid_argument("括号不匹配：多了一个左括号");
        }
        output.push_back(operators.top());
        operators.pop();
    }
    return output;
}

std::string JoinTokens(const std::vector<std::string>& tokens) {
    std::string joined;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i != 0) {
            joined += ' ';
        }
        joined += tokens[i];
    }
    return joined;
}

// 后缀求值：遇到操作数压栈，遇到运算符弹出两个操作数算完再压回去。
// 时间复杂度 O(n)，空间复杂度 O(n)。
long long EvaluatePostfix(const std::vector<std::string>& postfix) {
    ArrayStack<long long> values;

    for (const std::string& token : postfix) {
        if (token == "u-") {
            if (values.empty()) {
                throw std::invalid_argument("后缀表达式缺少操作数");
            }
            const long long operand = values.top();
            values.pop();
            values.push(-operand);
            continue;
        }
        if (token.size() == 1 && (token[0] < '0' || token[0] > '9')) {
            if (values.size() < 2) {
                throw std::invalid_argument("后缀表达式缺少操作数");
            }
            const long long rhs = values.top();
            values.pop();
            const long long lhs = values.top();
            values.pop();
            switch (token[0]) {
                case '+': values.push(lhs + rhs); break;
                case '-': values.push(lhs - rhs); break;
                case '*': values.push(lhs * rhs); break;
                case '/':
                    if (rhs == 0) {
                        throw std::domain_error("除数为 0");
                    }
                    values.push(lhs / rhs);
                    break;
                default: throw std::invalid_argument("后缀表达式里出现了未知运算符");
            }
            continue;
        }
        values.push(std::stoll(token));
    }

    if (values.size() != 1) {
        throw std::invalid_argument("后缀表达式不合法：最后栈里不是恰好一个值");
    }
    return values.top();
}

long long EvaluateInfix(const std::string& expr) {
    return EvaluatePostfix(InfixToPostfix(expr));
}

// ---------------------------------------------------------------------------
// 小工具：把容器内容导出成 vector，便于和 std::vector 逐项对拍
// ---------------------------------------------------------------------------
template <typename Stack>
std::vector<int> DrainStack(Stack& st) {
    std::vector<int> out;
    while (!st.empty()) {
        out.push_back(st.top());
        st.pop();
    }
    return out;
}

}  // namespace

int main() {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "本文件在 Debug 配置下运行，绝对耗时比 Release 慢，请只看趋势。\n";

    // =======================================================================
    Section("1. 栈的两种底层：数组 vs 链表（含 Rule of Five 验证）");
    // =======================================================================
    Note("两个实现都支持 push / pop / top / size / empty / clear，都是完整 Rule of Five。");
    Note("下面先验正确性，再验所有权语义（拷贝要深拷贝、移动要置空源对象）。");
    std::cout << "\n";

    {
        // 用同一串操作同时喂给两个实现，结果必须完全一致。
        const std::vector<int> ops = {5, 3, 9, 1, 7, 2, 8};
        ArrayStack<int> array_stack;
        LinkedStack<int> linked_stack;
        for (const int v : ops) {
            array_stack.push(v);
            linked_stack.push(v);
        }
        assert(array_stack.size() == ops.size());
        assert(linked_stack.size() == ops.size());
        assert(array_stack.top() == 8);
        assert(linked_stack.top() == 8);

        // 与「出栈序列 = 入栈逆序」这一手算期望对拍。
        const std::vector<int> got_array = DrainStack(array_stack);
        const std::vector<int> got_linked = DrainStack(linked_stack);
        const std::vector<int> expected(ops.rbegin(), ops.rend());
        assert(got_array == expected);
        assert(got_linked == expected);
        assert(array_stack.empty() && linked_stack.empty());

        // 拷贝构造：深拷贝，改副本不能影响原对象。
        ArrayStack<int> original;
        original.push(1);
        original.push(2);
        ArrayStack<int> copied(original);
        copied.push(3);
        assert(copied.size() == 3);
        assert(original.size() == 2);
        assert(original.top() == 2);
        const std::vector<int> copied_drain = DrainStack(copied);
        const std::vector<int> copied_expected = {3, 2, 1};
        assert(copied_drain == copied_expected);

        // 拷贝赋值：copy-and-swap，自赋值也必须安全。
        ArrayStack<int> assigned;
        assigned.push(99);
        assigned = original;
        assert(assigned.size() == 2);
        assert(assigned.top() == 2);
        ArrayStack<int>& self_alias = assigned;  // 通过别名制造自赋值，避免被误判为笔误
        assigned = self_alias;
        assert(assigned.size() == 2);
        assert(assigned.top() == 2);

        // 移动构造 / 移动赋值：源对象被置空，且不能 double free（析构时见真章）。
        ArrayStack<int> moved(std::move(original));
        assert(moved.size() == 2);
        assert(original.empty());
        assert(original.capacity() == 0);
        ArrayStack<int> move_assigned;
        move_assigned = std::move(moved);
        assert(move_assigned.size() == 2);
        assert(moved.empty());

        // 链表版同样验一遍拷贝 / 移动。
        LinkedStack<int> linked_src;
        linked_src.push(11);
        linked_src.push(22);
        LinkedStack<int> linked_copy(linked_src);
        linked_copy.push(33);
        assert(linked_src.size() == 2);
        assert(linked_copy.size() == 3);
        LinkedStack<int> linked_moved(std::move(linked_src));
        assert(linked_moved.size() == 2);
        assert(linked_src.empty());

        // 空栈访问必须抛异常，而不是返回垃圾值。
        bool threw_top = false;
        try {
            ArrayStack<int> empty_stack;
            g_sink = g_sink + static_cast<std::uint64_t>(empty_stack.top());
        } catch (const std::out_of_range&) {
            threw_top = true;
        }
        assert(threw_top);

        bool threw_pop = false;
        try {
            LinkedStack<int> empty_stack;
            empty_stack.pop();
        } catch (const std::out_of_range&) {
            threw_pop = true;
        }
        assert(threw_pop);

        std::cout << "  数组栈 / 链表栈：7 次 push 后出栈序列 == 入栈逆序，断言通过\n";
        std::cout << "  Rule of Five：深拷贝、移动置空、自赋值、空栈抛异常，断言通过\n";
    }

    // =======================================================================
    Section("2. 循环队列：空 / 满判断的三套方案");
    // =======================================================================
    Note("循环队列必须回答一个问题：head == tail 到底是「空」还是「满」？");
    Note("  方案一 浪费一个槽位：(tail + 1) % cap == head 判满，代价是少用一个槽位；");
    Note("  方案二 size_ 计数器：size == cap 判满、size == 0 判空，最直观，推荐；");
    Note("  方案三 full_ 布尔标志：省去计数器和槽位，但状态由两个变量共同描述，最易写错。");
    Note("下面把方案一和方案二都跑一遍，并验证「环绕」行为确实发生了。");
    std::cout << "\n";

    {
        // 容量 8 的环形缓冲：压 6 个、弹 4 个、再压 5 个，强制下标绕回 0 之后。
        // 注意两边都只能压到 7 个：方案一浪费一个槽位，容量 8 时可用就是 7。
        RingQueue<int, false> counted(8);
        WasteOneRingQueue<int> wasted(8);

        for (int i = 1; i <= 6; ++i) {
            counted.push(i * 10);
            wasted.push(i * 10);
        }
        for (int i = 0; i < 4; ++i) {
            assert(counted.front() == wasted.front());
            counted.pop();
            wasted.pop();
        }
        for (int i = 7; i <= 11; ++i) {
            counted.push(i * 10);
            wasted.push(i * 10);
        }

        // 两者的逻辑内容必须完全一致（方案一是浪费槽位，容量 8 时也只能存 8-1=7 个）。
        assert(counted.size() == 7);
        assert(wasted.size() == 7);
        assert(counted.front() == 50);
        assert(wasted.front() == 50);
        for (int i = 0; i < 7; ++i) {
            assert(counted.front() == wasted.front());
            assert(counted.back() >= counted.front());
            counted.pop();
            wasted.pop();
        }
        assert(counted.empty() && wasted.empty());

        // 方案一的容量语义：用户要 8，实际可用 7。
        WasteOneRingQueue<int> wasted_cap(8);
        for (int i = 0; i < 7; ++i) {
            wasted_cap.push(i);
        }
        assert(wasted_cap.full());
        bool threw_full = false;
        try {
            wasted_cap.push(8);
        } catch (const std::out_of_range&) {
            threw_full = true;
        }
        assert(threw_full);

        // 方案二没有槽位浪费：容量 8 就能装 8 个。
        RingQueue<int, false> counted_cap(8);
        for (int i = 0; i < 8; ++i) {
            counted_cap.push(i);
        }
        assert(counted_cap.full() && counted_cap.size() == 8);

        std::cout << "  方案一（浪费槽位）：capacity = 8，实际可存 " << 7
                  << " 个，第 8 个 push 抛 out_of_range，断言通过\n";
        std::cout << "  方案二（size_ 计数）：capacity = 8，实际可存 " << counted_cap.size()
                  << " 个，无槽位浪费，断言通过\n";
        std::cout << "  环绕正确性：head 绕回 0 之后两者内容仍然逐项一致，断言通过\n";
    }

    // =======================================================================
    Section("3. 下标环绕：取模 % vs 位运算 & (cap-1)");
    // =======================================================================
    Note("位运算更快的原因：x % cap 要走除法器，在现代 x86 上 div 是 20 到 40 个周期；");
    Note("而 x & (cap-1) 是单周期逻辑运算。前提是 cap 必须是 2 的幂：");
    Note("cap = 2^k 时 cap-1 的低 k 位全是 1，与运算恰好只保留低 k 位，等价于模 2^k。");
    Note("如果 cap 不是 2 的幂，& 的结果根本不是合法下标，会静默写坏数据（最难查的那种 bug）。");
    std::cout << "\n";

    {
        // 先验证两者语义等价（容量取 2 的幂时）。
        RingQueue<int, false> modulo_queue(1024);
        RingQueue<int, true> mask_queue(1024);
        assert(modulo_queue.capacity() == mask_queue.capacity());
        for (int round = 0; round < 5; ++round) {
            // 每轮净增 100 个元素，head 会一路向前推进并绕过下标 0，环绕路径因此被覆盖。
            for (int i = 0; i < 400; ++i) {
                modulo_queue.push(round * 1000 + i);
                mask_queue.push(round * 1000 + i);
            }
            for (int i = 0; i < 300; ++i) {
                assert(modulo_queue.front() == mask_queue.front());
                modulo_queue.pop();
                mask_queue.pop();
            }
        }
        std::cout << "  语义等价性：1024 容量下两套环绕写法逐项一致，断言通过\n\n";

        constexpr int kOps = 200000;
        constexpr int kInnerRepeat = 10;

        auto bench_wrap = [&](auto&& queue_factory) {
            return BenchMedianUs(
                [&] {
                    return Repeat(
                        [&] {
                            auto q = queue_factory();
                            std::uint64_t acc = 0;
                            for (int i = 0; i < kOps; ++i) {
                                if (q.full()) {
                                    acc += static_cast<std::uint64_t>(q.front());
                                    q.pop();
                                }
                                q.push(i);
                            }
                            while (!q.empty()) {
                                acc += static_cast<std::uint64_t>(q.front());
                                q.pop();
                            }
                            return acc;
                        },
                        kInnerRepeat);
                },
                5);
        };

        // 预热：先把缺页中断和缓存冷启动的成本跑掉。
        WarmUp(
            [&] {
                RingQueue<int, false> q(4096);
                std::uint64_t acc = 0;
                for (int i = 0; i < 1000; ++i) {
                    q.push(i);
                }
                while (!q.empty()) {
                    acc += static_cast<std::uint64_t>(q.front());
                    q.pop();
                }
                return acc;
            },
            3);

        const double t_modulo = bench_wrap([] { return RingQueue<int, false>(4096); });
        const double t_mask = bench_wrap([] { return RingQueue<int, true>(4096); });

        Label("  端到端：size_ 计数 + 取模环绕（200 万次 push）", 54);
        std::cout << ": " << t_modulo << " us\n";
        Label("  端到端：size_ 计数 + 位运算环绕（200 万次 push）", 54);
        std::cout << ": " << t_mask << " us\n";
        if (t_mask > 0.0) {
            Label("  两者倍数（>1 表示位运算更快）", 54);
            std::cout << ": " << (t_modulo / t_mask) << " x\n";
        }

        // 端到端测量里，环绕只占很小一部分，差别会被 push/pop 本身的开销淹没。
        // 想看清楚「除法器 vs 位运算」，必须把下标递推单独隔离出来，并且保证容量
        // 对编译器是不透明的——否则它会把 % 4096 直接折成 & 4095，测了个寂寞。
        constexpr std::uint64_t kWrapSteps = 1000000;
        const volatile std::size_t kRuntimeCap = 4096;  // volatile：不许编译器折成常量

        const double t_wrap_modulo = BenchMedianUs(
            [&] {
                std::size_t index = 0;
                std::uint64_t acc = 0;
                // 关键：下标递推构成长依赖链，除法的延迟被完整暴露出来。
                for (std::uint64_t i = 0; i < kWrapSteps; ++i) {
                    index = (index + 1) % kRuntimeCap;
                    acc += index;
                }
                return acc;
            },
            7);

        const double t_wrap_mask = BenchMedianUs(
            [&] {
                std::size_t index = 0;
                std::uint64_t acc = 0;
                for (std::uint64_t i = 0; i < kWrapSteps; ++i) {
                    index = (index + 1) & (kRuntimeCap - 1);
                    acc += index;
                }
                return acc;
            },
            7);

        std::cout << "\n";
        Label("  隔离测量：100 万次取模下标递推", 54);
        std::cout << ": " << t_wrap_modulo << " us\n";
        Label("  隔离测量：100 万次位运算下标递推", 54);
        std::cout << ": " << t_wrap_mask << " us\n";
        if (t_wrap_mask > 0.0) {
            Label("  位运算快了多少倍", 54);
            std::cout << ": " << (t_wrap_modulo / t_wrap_mask) << " x\n";
        }

        Note("");
        Note("读表方法（很重要，这是测量方法论的现身说法）：");
        Note("  端到端那一组数字里，环绕只是 push / pop 的一小部分，差异被淹没甚至可能反过来；");
        Note("  只有把下标递推单独隔离出来，「除法器 vs 位运算」的差距才稳定呈现。");
        Note("  这也是为什么 benchmark 一定要「控制变量」：想测 A，就别让 B 混进来。");
        Note("工程结论：位运算环绕是零风险的顺手优化（前提是容量取 2 的幂），但不要指望");
        Note("它带来数量级提升；环形缓冲真正的性能来源是「零分配 + 顺序访问 + 稳定 O(1)」。");
    }

    // =======================================================================
    Section("4. MinStack：O(1) 取最小值");
    // =======================================================================
    Note("辅助栈法：主栈每压一个元素，若它 <= 当前最小值，就往辅助栈压一份副本。");
    Note("为什么辅助栈能和主栈同步 push / pop？因为最小值的变化是单调的：");
    Note("入栈只可能让最小值变小或不变，出栈时一旦弹掉的正是当年那个最小值，");
    Note("辅助栈弹掉栈顶后露出来的恰好就是「它入栈之前的那个最小值」。");
    Note("关键细节：判断必须用 <=，相等也要压，否则两个相同最小值只记一份，");
    Note("弹掉第一个之后最小值就丢了。");
    std::cout << "\n";

    {
        // 对拍方法：朴素做法 —— 每一步都用 std::min_element 在主栈的快照上求最小值。
        // 这正是对拍的价值：一个 O(n) 的笨办法，换一个 O(1) 的聪明办法的正确性保证。
        std::mt19937 rng(20240604u);
        MinStack<int> fast;
        MinStackDelta<long long> delta;
        std::vector<int> snapshot;

        int peak_size = 0;
        for (int step = 0; step < 4000; ++step) {
            const bool do_push = snapshot.empty() || (rng() % 100u) < 60u;
            if (do_push) {
                const int value = static_cast<int>(rng() % 1000u) - 500;
                fast.push(value);
                delta.push(static_cast<long long>(value));
                snapshot.push_back(value);
                if (static_cast<int>(snapshot.size()) > peak_size) {
                    peak_size = static_cast<int>(snapshot.size());
                }
            } else {
                fast.pop();
                delta.pop();
                snapshot.pop_back();
            }

            // 每步都和朴素结果对拍。
            if (!snapshot.empty()) {
                const int naive_min = *std::min_element(snapshot.begin(), snapshot.end());
                assert(fast.min() == naive_min);
                assert(fast.top() == snapshot.back());
                assert(delta.min() == static_cast<long long>(naive_min));
                assert(delta.top() == static_cast<long long>(snapshot.back()));
                assert(fast.size() == snapshot.size());
            }
        }

        std::cout << "  对拍 4000 步（push / pop 随机混合，栈深峰值 " << peak_size << "）：\n";
        std::cout << "  辅助栈 MinStack 的 min() 每一步都等于 std::min_element 的朴素结果，断言通过\n";
        std::cout << "  差值编码 MinStackDelta 同样逐项一致（额外空间恒为 O(1)），断言通过\n\n";

        // 复杂度实测：min() 是 O(1)，朴素 min_element 是 O(n)，差距随栈深线性拉开。
        constexpr int kDeep = 20000;
        MinStack<int> deep;
        std::vector<int> mirror;
        mirror.reserve(static_cast<std::size_t>(kDeep));
        for (int i = 0; i < kDeep; ++i) {
            deep.push(kDeep - i);          // 递减压入 => 辅助栈最坏情况，存了全部元素
            mirror.push_back(kDeep - i);   // 同样的数据给朴素做法当输入
        }
        const double t_fast_min = BenchMedianUs(
            [&] { return Repeat([&] { return static_cast<std::uint64_t>(deep.min()); }, 1000); },
            5);
        const double t_naive_min = BenchMedianUs(
            [&] {
                return Repeat(
                    [&] {
                        // 朴素做法：每次都把整个栈扫一遍求最小值（std::min_element 的等价物）
                        int best = mirror[0];
                        for (const int v : mirror) {
                            best = std::min(best, v);
                        }
                        return static_cast<std::uint64_t>(best + 1);
                    },
                    1000);
            },
            5);

        Label("  O(1) 辅助栈 min() 调用 1000 次", 38);
        std::cout << ": " << t_fast_min << " us\n";
        Label("  O(n) 朴素扫描 1000 次（同量级数据）", 38);
        std::cout << ": " << t_naive_min << " us\n";
        Note("");
        Note("结论：栈深 20000 时，O(1) 的 min() 比 O(n) 的朴素扫描快两到三个数量级，");
        Note("      而且栈越深差距越大——这正是「用空间换时间」的标准收益曲线。");
    }

    // =======================================================================
    Section("5. 用两个栈实现队列：均摊 O(1)");
    // =======================================================================
    Note("入队：压进 in 栈（栈顶是最新元素）。");
    Note("出队：out 栈为空时，把 in 栈整个倒进 out 栈，此时 out 栈顶就是最早的元素。");
    Note("均摊分析：每个元素一生最多被搬一次（in -> out），n 次操作总搬移 <= n，摊到每次是 O(1)。");
    std::cout << "\n";

    {
        // 正确性：和 std::queue 对拍。
        TwoStackQueue<int> mine;
        std::queue<int> reference;
        std::mt19937 rng(777u);
        for (int step = 0; step < 20000; ++step) {
            const bool do_push = reference.empty() || (rng() % 100u) < 55u;
            if (do_push) {
                const int value = static_cast<int>(rng() % 100000u);
                mine.push(value);
                reference.push(value);
            } else {
                assert(mine.front() == reference.front());
                mine.pop();
                reference.pop();
            }
            assert(mine.size() == reference.size());
            if (!reference.empty()) {
                assert(mine.front() == reference.front());
            }
        }

        // 均摊实测：连续 n 次 push 后连续 n 次 pop，统计发生了多少次栈间搬移。
        constexpr int kN = 100000;
        TwoStackQueue<int> amortized;
        for (int i = 0; i < kN; ++i) {
            amortized.push(i);
        }
        const std::uint64_t moves_after_push = amortized.moves();
        long long checksum = 0;
        for (int i = 0; i < kN; ++i) {
            assert(amortized.front() == i);
            checksum += amortized.front();
            amortized.pop();
        }

        std::cout << "  与 std::queue 对拍 20000 步（push / pop 随机混合）：逐项一致，断言通过\n";
        std::cout << "  均摊实测：连续 " << kN << " 次 push 时搬移次数 = " << moves_after_push
                  << "（因为 out 栈还没被触发）\n";
        std::cout << "  连续 " << kN << " 次 pop 后总搬移次数 = " << amortized.moves()
                  << "，即每个元素恰好被搬 1 次，均摊 O(1) 得到实测确认\n";
        std::cout << "  校验和 = " << checksum << "（= n(n-1)/2，说明出队顺序完全正确）\n";
        assert(amortized.moves() == static_cast<std::uint64_t>(kN));
        assert(checksum == static_cast<long long>(kN) * (kN - 1) / 2);
    }

    // =======================================================================
    Section("6. 栈的经典应用：括号匹配");
    // =======================================================================
    Note("遇到左括号，把「它期待的右括号 + 它的位置」压栈；遇到右括号就检查栈顶。");
    Note("这样不仅知道「不匹配」，还能报出究竟是哪个位置出的错。");
    std::cout << "\n";

    {
        struct BracketCase {
            const char* text;
            bool expected_ok;
        };
        const BracketCase cases[] = {
            {"(a[b]{c})", true},
            {"{[()]}", true},
            {"", true},
            {"no brackets at all", true},
            {"((()))", true},
            {"(a[b)]", false},
            {"((a)", false},
            {"a)b(", false},
            {"{[(", false},
            {"func(int a[10], std::vector<int>{1,2})", true},
        };

        for (const BracketCase& item : cases) {
            const BracketCheck result = CheckBrackets(item.text);
            assert(result.ok == item.expected_ok);
            Label(std::string("  \"") + item.text + "\"", 46);
            if (result.ok) {
                std::cout << " -> 匹配\n";
            } else {
                std::cout << " -> 第 " << result.position << " 位出错：" << result.message << "\n";
            }
        }
        Note("");
        Note("所有用例的判定结果都与手算期望一致（assert 通过），且错误位置可定位。");
    }

    // =======================================================================
    Section("7. 栈的经典应用：中缀转后缀 + 后缀求值");
    // =======================================================================
    Note("调度场算法：操作数直接输出；运算符入栈前先弹出栈里优先级不低于它的运算符。");
    Note("一元负号靠「当前位置是否期待操作数」识别，在后缀串里记作 u-（优先级高于乘除）。");
    Note("简化之处：只支持非负整数字面量与 + - * / 和括号，除法是 C++ 的整数除法。");
    std::cout << "\n";

    {
        struct ExprCase {
            const char* expr;
            long long expected;
        };
        // 期望值全部是手算结果，assert 就是和「手算」对拍。
        const ExprCase cases[] = {
            {"1+2*3", 7},
            {"(1+2)*3", 9},
            {"100-20-5", 75},          // 左结合：(100-20)-5，不是 100-(20-5)
            {"100-(20-5)", 85},
            {"-3+5", 2},               // 一元负号
            {"2*-3", -6},              // 运算符后面的一元负号
            {"-(4+6)", -10},           // 一元负号作用于括号
            {"7/2", 3},                // 整数除法，向零取整
            {"-7/2", -3},              // C++ 的除法向零取整，所以是 -3 而不是 -4
            {"((2+3)*(4+5))", 45},
            {"12345+54321", 66666},    // 多位数必须被当成一个 token
            {"2*(3+(4-1))*5", 60},
            {"10--10", 20},            // 连续两个减号：二元减法 + 一元负号
            {"-(-(-5))", -5},          // 嵌套一元负号
        };

        Label("  中缀表达式", 24);
        Label("后缀表达式", 30);
        Label("求值结果", 12);
        Label("手算期望", 10);
        std::cout << "\n  " << std::string(72, '-') << "\n";

        for (const ExprCase& item : cases) {
            const std::vector<std::string> postfix = InfixToPostfix(item.expr);
            const long long got = EvaluatePostfix(postfix);
            assert(got == item.expected);
            Label(std::string("  ") + item.expr, 24);
            Label(JoinTokens(postfix), 30);
            Label(std::to_string(got), 12);
            std::cout << item.expected << "\n";
        }

        // 与「直接用中缀式子求值」的入口保持一致。
        assert(EvaluateInfix("(8-3)*(7+9)/4") == 20);

        // 错误路径也要可预期：括号不匹配、除零、非法字符都必须抛异常。
        auto expect_throw = [](const std::string& expr) {
            bool threw = false;
            try {
                g_sink = g_sink + static_cast<std::uint64_t>(EvaluateInfix(expr));
            } catch (const std::exception&) {
                threw = true;
            }
            return threw;
        };
        assert(expect_throw("(1+2"));
        assert(expect_throw("1+2)"));
        assert(expect_throw("1/0"));
        assert(expect_throw("1+a"));
        assert(expect_throw("1 2 +"));

        Note("");
        Note("14 个用例的求值结果全部等于手算期望值（assert 通过）；");
        Note("括号不匹配、除零、非法字符、多余操作数这四类错误都按预期抛异常。");
    }

    // =======================================================================
    Section("8. std::stack / std::queue 是容器适配器，默认底层是 std::deque");
    // =======================================================================
    Note("它们不是新容器，而是「把某个既有容器裁掉一部分接口」的适配器：");
    Note("  std::stack<T> 需要 back / push_back / pop_back  => deque / vector / list 都行；");
    Note("  std::queue<T> 需要 front / back / push_back / pop_front => deque / list 行，");
    Note("  vector 不行，因为它没有 pop_front（这也是「vector 当队列用」必须自己维护");
    Note("  一个 head 下标的原因）。");
    std::cout << "\n";

    {
        // 用编译期断言把「默认底层是 deque」这件事钉死，而不是靠注释口说无凭。
        static_assert(std::is_same_v<std::stack<int>::container_type, std::deque<int>>);
        static_assert(std::is_same_v<std::queue<int>::container_type, std::deque<int>>);
        static_assert(std::is_same_v<std::stack<int, std::vector<int>>::container_type,
                                     std::vector<int>>);
        static_assert(std::is_same_v<std::queue<int, std::list<int>>::container_type,
                                     std::list<int>>);

        std::cout << "  static_assert 通过：stack/queue 的默认 container_type 都是 std::deque<int>\n";
        std::cout << "  sizeof(std::stack<int>)  = " << sizeof(std::stack<int>)
                  << " 字节（通常就是一个 deque 的大小）\n";
        std::cout << "  sizeof(std::queue<int>)  = " << sizeof(std::queue<int>) << " 字节\n";
        std::cout << "  sizeof(std::deque<int>)  = " << sizeof(std::deque<int>)
                  << " 字节（deque 内部是指针数组 + 若干定长块，不是一整块连续内存）\n";

        // 三种「队列底层」跑同一份工作负载：deque / list / 自己维护 head 的 vector。
        constexpr int kOps = 100000;
        constexpr int kRepeats = 3;

        WarmUp(
            [&] {
                std::queue<int> q;
                std::uint64_t acc = 0;
                for (int i = 0; i < 1000; ++i) {
                    q.push(i);
                }
                while (!q.empty()) {
                    acc += static_cast<std::uint64_t>(q.front());
                    q.pop();
                }
                return acc;
            },
            3);

        const double t_deque = BenchMedianUs(
            [&] {
                std::queue<int> q;  // 默认底层 = std::deque
                std::uint64_t acc = 0;
                for (int i = 0; i < kOps; ++i) {
                    q.push(i);
                }
                while (!q.empty()) {
                    acc += static_cast<std::uint64_t>(q.front());
                    q.pop();
                }
                return acc;
            },
            kRepeats);

        const double t_list = BenchMedianUs(
            [&] {
                std::queue<int, std::list<int>> q;  // 底层换成 list
                std::uint64_t acc = 0;
                for (int i = 0; i < kOps; ++i) {
                    q.push(i);
                }
                while (!q.empty()) {
                    acc += static_cast<std::uint64_t>(q.front());
                    q.pop();
                }
                return acc;
            },
            kRepeats);

        const double t_vector = BenchMedianUs(
            [&] {
                // vector 不能直接当 std::queue 的底层（没有 pop_front），
                // 所以自己维护一个 head 下标：pop 只是把 head 往后挪，不搬移数据。
                std::vector<int> v;
                std::size_t head = 0;
                std::uint64_t acc = 0;
                for (int i = 0; i < kOps; ++i) {
                    v.push_back(i);
                }
                while (head < v.size()) {
                    acc += static_cast<std::uint64_t>(v[head]);
                    ++head;
                }
                return acc;
            },
            kRepeats);

        std::cout << "\n";
        Label("  std::queue<int>（底层 deque）     push+pop 100000 次", 56);
        std::cout << ": " << t_deque << " us\n";
        Label("  std::queue<int, list<int>>        同上", 56);
        std::cout << ": " << t_list << " us\n";
        Label("  vector + head 下标 自维护队列     同上", 56);
        std::cout << ": " << t_vector << " us\n";
        if (t_deque > 0.0 && t_vector > 0.0) {
            Label("  list / deque 的倍数", 56);
            std::cout << ": " << (t_list / t_deque) << " x\n";
            Label("  vector_自维护 / deque 的倍数", 56);
            std::cout << ": " << (t_vector / t_deque) << " x\n";
        }
        Note("");
        Note("怎么读这三个数字：");
        Note("  1) list 版明显慢于 deque 版：它每一次 push / pop 都是一次堆分配加一次释放，");
        Note("     而 deque 一次分配一整块（MSVC 下每块能放很多个 int），均摊下来便宜得多；");
        Note("  2) 这里「vector + head 下标」反而最快，因为它压根不回收内存：");
        Note("     push 就是一次连续写入，pop 只是把 head 往后挪一格，既没有块管理也没有释放；");
        Note("  3) 但它用空间换的：内存随 push 总次数单调增长。一压一弹交替进行的场景下，");
        Note("     size() 一直是 1，占用却一直涨——这是真实项目里会翻车的地方。");
        Note("");
        Note("选择建议：元素数量长期稳定波动、需要真正回收内存，用 std::deque");
        Note("（这也是 std::stack / std::queue 的默认底层，因为它两头操作都不需要搬移）；");
        Note("确定「只追加、不回头」或者是做题/批处理，vector + head 下标最简单也最快；");
        Note("只有在必须保证「每次操作耗时完全确定、绝不允许批量分配」时才考虑 list。");
    }

    // =======================================================================
    Section("9. 性能汇总：数组栈 vs 链表栈 vs std::stack");
    // =======================================================================
    Note("工作负载完全一致：连续 push N 次，再连续 pop N 次，累加所有出栈值。");
    Note("数组栈和链表栈都是手写实现，std::stack 作为工程基准。");
    std::cout << "\n";

    {
        constexpr int kN = 50000;
        constexpr int kRepeats = 5;

        auto bench_stack = [&](auto&& make_stack) {
            return BenchMedianUs(
                [&] {
                    auto st = make_stack();
                    std::uint64_t acc = 0;
                    for (int i = 0; i < kN; ++i) {
                        st.push(i);
                    }
                    while (!st.empty()) {
                        acc += static_cast<std::uint64_t>(st.top());
                        st.pop();
                    }
                    return acc;
                },
                kRepeats);
        };

        WarmUp(
            [&] {
                ArrayStack<int> st;
                std::uint64_t acc = 0;
                for (int i = 0; i < 1000; ++i) {
                    st.push(i);
                }
                while (!st.empty()) {
                    acc += static_cast<std::uint64_t>(st.top());
                    st.pop();
                }
                return acc;
            },
            3);

        const double t_array = bench_stack([] { return ArrayStack<int>(); });
        const double t_linked = bench_stack([] { return LinkedStack<int>(); });
        const double t_std = bench_stack([] { return std::stack<int>(); });

        Label("  手写 ArrayStack（连续内存，按倍数扩容）", 44);
        std::cout << ": " << t_array << " us\n";
        Label("  手写 LinkedStack（每元素一次 new/delete）", 44);
        std::cout << ": " << t_linked << " us\n";
        Label("  std::stack<int>（底层 std::deque）", 44);
        std::cout << ": " << t_std << " us\n";
        if (t_array > 0.0 && t_std > 0.0) {
            Label("  LinkedStack / ArrayStack", 44);
            std::cout << ": " << (t_linked / t_array) << " x\n";
            Label("  ArrayStack / std::stack", 44);
            std::cout << ": " << (t_array / t_std) << " x\n";
        }

        Note("");
        Note("结论（数字是 Debug 下的，看倍数不看绝对值）：");
        Note("  1) 链表栈比数组栈慢好几倍，主要成本是每个节点的 new/delete 加上缓存不命中；");
        Note("  2) 数组栈即使算上扩容搬移，仍然明显快于链表栈——这就是「均摊 + 连续内存」的威力；");
        Note("  3) 这里手写数组栈甚至快过 std::stack，原因不是我们的实现更好，而是 Debug 下");
        Note("     std::deque 的分块管理与多层函数调用完全没有被内联优化，而数组栈只有一次写入。");
        Note("     Release 下这个差距会大幅缩小——所以「谁更快」必须和编译配置一起说，");
        Note("     这也正是本文件所有数字都标注 Debug 的原因。");
        Note("");
        Note("什么时候才该选链表作为栈 / 队列的底层？");
        Note("  当元素非常大（拷贝极贵）、或者你绝对不能接受扩容那一瞬间的 O(n) 停顿");
        Note("  （比如实时音频线程），此时用「预留内存池 + 链表」换确定性的单次耗时。");
    }

    // 让 g_sink 真正被「读过」一次，确保它不会被整个优化掉。
    std::cout << "\n[校验] 累加器非零，说明所有测量结果都真实参与了运算: "
              << (g_sink != 0 ? "是" : "否") << "\n";

    return 0;
}
