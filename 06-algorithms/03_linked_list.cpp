// ============================================================================
// 03_linked_list.cpp
// 演示主题：
//   1. 哨兵节点（sentinel / dummy head）：为什么它能消掉插入删除里所有的边界特判
//   2. 手写单链表 SinglyList<T>：插入、删除、查找、反转（迭代 + 递归）、找中点、判圈、合并
//   3. 手写双向链表 DoublyList<T>：头哨兵 + 尾哨兵，O(1) 的中间插入删除
//   4. 快慢指针三件套：找中点、Floyd 判圈（并证明为什么一定相遇）、找环入口
//   5. 与 std::forward_list / std::list / std::vector 对拍（assert 逐元素相等）
//   6. 实测缓存局部性：顺序遍历、随机访问、中间插入删除（中小元素 / 大元素）谁赢谁输
//
// 关键结论：
//   1. 链表唯一真正的优势是「已知位置时插入删除是 O(1)，且不需要搬移其它元素」；
//      一旦还要「先找到位置」，从头走一遍的 O(n) 就把它打回和 vector 同阶，
//      而 vector 的搬移是连续内存 memmove，常数小得多；
//   2. 顺序遍历和随机访问都是 vector 完胜：vector 的元素连续，一次 cache line 能带回
//      16 个 int，CPU 预取器还能提前预取；链表每走一步都是一次潜在的 cache miss。
//      实测顺序遍历 20 万个 int，链表比 vector 慢一到两个数量级（多次运行落在 25 到
//      70 倍之间：vector 那一侧只有 120 到 320 微秒，极易受缓存状态影响，倍数波动很大，
//      但「链表遍历比 vector 慢一个数量级以上」这个结论非常稳定）；
//   3. 反直觉的实测：在「中小元素 + 2 万元素」的中间插入删除场景里，vector 依然赢链表
//      一个数量级——因为后半段只有 40KB，全在缓存里，而链表每个节点都要付一次
//      malloc / free（Debug 的调试堆一次要十几微秒）。只有把元素放大到 4KB，
//      让 vector 每次搬移 10MB 时，链表才真正反超；
//   4. 所以「中间插入删除就用链表」是有条件的：n 和元素大小要够大，
//      并且你必须能长期持有位置句柄（迭代器）。真实项目里元素通常很小、分配又贵，
//      因此默认仍然是 vector，需要两端操作时用 deque，list 真正落地的场景并不多；
//   5. 判圈快慢指针一定相遇的原因：进入环之后快指针相对慢指针的速度恒为 1，
//      两者在环上的距离每一步减 1，所以最多 L 步必然追上（L 是环长）。
//
// 说明：本文件是教学用途，生产请用 std::vector / std::list / std::forward_list。
//       所有数字来自 Debug（/Od）构建，绝对耗时比 Release 慢很多，请只看趋势与数量级。
// ============================================================================

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <forward_list>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <list>
#include <random>
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
// 解决办法：把结果写进一个 volatile 全局变量。volatile 强制产生真实的读写副作用。
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
// 先空跑几遍，让这些一次性成本落在预热里，而不是落在正式测量里。
template <typename Fn>
void WarmUp(Fn&& fn, int times) {
    for (int i = 0; i < times; ++i) {
        g_sink = g_sink + fn();
    }
}

// 把「一次操作」重复 repeat 次并累加结果。
// 为什么需要它：随机访问这种「一次只要几微秒」的工作负载，直接测一次会被计时器
// 噪声淹没。重复 repeat 次只是给总耗时乘一个常数，不改变增长阶，却能提高信噪比。
template <typename Fn>
std::uint64_t Repeat(Fn&& fn, int repeat) {
    std::uint64_t acc = 0;
    for (int r = 0; r < repeat; ++r) {
        acc += fn();
    }
    return acc;
}

// ===========================================================================
// 第 1 部分：节点、哨兵、迭代器
// ===========================================================================
// 为什么把节点拆成「基类 + 派生」两层？
//   哨兵节点本身不需要存 value（它不是一个真元素）。如果节点类型里硬塞一个 T，
//   哨兵就要求 T 必须能默认构造——这是一个完全没必要的约束。
//   所以基类只放指针（next，双向链表再加 prev），派生类才加 value：
//   哨兵用基类对象，真节点用派生类对象，两者可以互相串联。
//   libstdc++ 和 MSVC 的 list 也都用了这个「基类指针 + 派生数据」的手法。
struct SNodeBase {
    SNodeBase* next = nullptr;
};

template <typename T>
struct SNode : SNodeBase {
    T value;
    template <typename... Args>
    explicit SNode(Args&&... args) : value(std::forward<Args>(args)...) {}
};

struct DNodeBase {
    DNodeBase* prev = nullptr;
    DNodeBase* next = nullptr;
};

template <typename T>
struct DNode : DNodeBase {
    T value;
    template <typename... Args>
    explicit DNode(Args&&... args) : value(std::forward<Args>(args)...) {}
};

// 前向迭代器。T 是元素类型，IsConst 决定它是 iterator 还是 const_iterator。
// 用「一个模板 + 两个别名」而不是写两遍，是为了避免两份几乎一样的代码走样。
template <typename T, bool IsConst>
class SListIterator {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = std::conditional_t<IsConst, const T*, T*>;
    using reference = std::conditional_t<IsConst, const T&, T&>;
    using node_pointer = std::conditional_t<IsConst, const SNodeBase*, SNodeBase*>;

    SListIterator() = default;
    explicit SListIterator(node_pointer node) noexcept : node_(node) {}

    // iterator -> const_iterator 的隐式转换是允许的（加 const 永远安全），
    // 反过来不允许。这条规则对所有标准库容器都成立。
    template <bool OtherConst, std::enable_if_t<IsConst && !OtherConst, int> = 0>
    SListIterator(const SListIterator<T, OtherConst>& other) noexcept : node_(other.RawNode()) {}

    reference operator*() const {
        return static_cast<SNode<T>*>(const_cast<SNodeBase*>(node_))->value;
    }
    pointer operator->() const {
        return &static_cast<SNode<T>*>(const_cast<SNodeBase*>(node_))->value;
    }
    SListIterator& operator++() noexcept {
        node_ = node_->next;
        return *this;
    }
    SListIterator operator++(int) noexcept {
        SListIterator tmp = *this;
        node_ = node_->next;
        return tmp;
    }
    friend bool operator==(const SListIterator& a, const SListIterator& b) noexcept {
        return a.node_ == b.node_;
    }
    friend bool operator!=(const SListIterator& a, const SListIterator& b) noexcept {
        return a.node_ != b.node_;
    }

    // 给容器用的「逃生口」：insert_after / erase_after 需要拿到裸节点指针。
    // 生产代码不应该暴露它，这里为了把实现写清楚而保留。
    node_pointer RawNode() const noexcept { return node_; }

private:
    node_pointer node_ = nullptr;
};

// 双向迭代器：多了 operator--。
template <typename T, bool IsConst>
class DListIterator {
public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = std::conditional_t<IsConst, const T*, T*>;
    using reference = std::conditional_t<IsConst, const T&, T&>;
    using node_pointer = std::conditional_t<IsConst, const DNodeBase*, DNodeBase*>;

    DListIterator() = default;
    explicit DListIterator(node_pointer node) noexcept : node_(node) {}

    template <bool OtherConst, std::enable_if_t<IsConst && !OtherConst, int> = 0>
    DListIterator(const DListIterator<T, OtherConst>& other) noexcept : node_(other.RawNode()) {}

    reference operator*() const {
        return static_cast<DNode<T>*>(const_cast<DNodeBase*>(node_))->value;
    }
    pointer operator->() const {
        return &static_cast<DNode<T>*>(const_cast<DNodeBase*>(node_))->value;
    }
    DListIterator& operator++() noexcept {
        node_ = node_->next;
        return *this;
    }
    DListIterator operator++(int) noexcept {
        DListIterator tmp = *this;
        node_ = node_->next;
        return tmp;
    }
    DListIterator& operator--() noexcept {
        node_ = node_->prev;
        return *this;
    }
    DListIterator operator--(int) noexcept {
        DListIterator tmp = *this;
        node_ = node_->prev;
        return tmp;
    }
    friend bool operator==(const DListIterator& a, const DListIterator& b) noexcept {
        return a.node_ == b.node_;
    }
    friend bool operator!=(const DListIterator& a, const DListIterator& b) noexcept {
        return a.node_ != b.node_;
    }

    node_pointer RawNode() const noexcept { return node_; }

private:
    node_pointer node_ = nullptr;
};

// ===========================================================================
// 第 2 部分：手写单链表（头哨兵 + 尾指针）
// ===========================================================================
// 复杂度：
//   PushFront / PushBack / PopFront / InsertAfter / EraseAfter   -> O(1)
//   PopBack                                                     -> O(n)（单链表找不到前驱，只能从头走）
//   InsertAt / EraseAt / At / Find / Remove                     -> O(index) 或 O(n)
//   Reverse / ReverseRecursive / Middle / HasCycle              -> O(n)
//   Size / Empty / Front / Back                                 -> O(1)
//   空间：每个元素额外背 1 个指针（sizeof(SNode<T>)），没有容量冗余。
//
// 教学用途，生产请用 std::forward_list（不需要尾插）或 std::list（需要双向遍历）。
template <typename T>
class SinglyList {
public:
    using size_type = std::size_t;
    using value_type = T;
    using iterator = SListIterator<T, false>;
    using const_iterator = SListIterator<T, true>;

    static constexpr size_type npos = static_cast<size_type>(-1);

    SinglyList() noexcept = default;

    // ---- Rule of Five：五个都写齐，不依赖隐式定义 ----
    // 为什么拷贝构造必须手写？因为默认的拷贝构造只会「浅拷贝 head_.next 这个指针」，
    // 两个链表会共享同一批节点，析构时 double free。链表这种「拥有裸指针」的类，
    // 编译器生成的拷贝语义永远是错的。
    SinglyList(const SinglyList& other) : SinglyList() {
        for (const T& x : other) {
            PushBack(x);
        }
    }

    // 移动构造：把对方的整条链摘过来。这里不用逐个节点搬，O(1)。
    // 摘完必须把源对象恢复成「合法的空链表」——源对象的哨兵是它自己的成员，
    // 不能把它的地址留下来，否则源对象析构时会去删一堆已经不属于它的节点。
    SinglyList(SinglyList&& other) noexcept : SinglyList() { TakeFrom(other); }

    // 拷贝赋值用 copy-and-swap：先在临时对象里把拷贝做完，
    // 抛异常时 *this 一点没变（强异常保证），成功后再 O(1) 交换。
    SinglyList& operator=(const SinglyList& other) {
        if (this != &other) {
            SinglyList tmp(other);
            Swap(tmp);
        }
        return *this;
    }

    SinglyList& operator=(SinglyList&& other) noexcept {
        if (this != &other) {
            Clear();
            TakeFrom(other);
        }
        return *this;
    }

    // 析构：逐个 delete 节点。注意节点是一个个 new 出来的，没有连续内存可以整块释放。
    ~SinglyList() { Clear(); }

    // ---- 状态 ----
    size_type Size() const noexcept { return size_; }
    bool Empty() const noexcept { return size_ == 0; }

    iterator begin() noexcept { return iterator(head_.next); }
    iterator end() noexcept { return iterator(nullptr); }
    const_iterator begin() const noexcept { return const_iterator(head_.next); }
    const_iterator end() const noexcept { return const_iterator(nullptr); }

    T& Front() noexcept { return static_cast<SNode<T>*>(head_.next)->value; }
    const T& Front() const noexcept { return static_cast<const SNode<T>*>(head_.next)->value; }
    T& Back() noexcept { return static_cast<SNode<T>*>(tail_)->value; }
    const T& Back() const noexcept { return static_cast<const SNode<T>*>(tail_)->value; }

    // ---- 插入 ----
    // 哨兵的全部价值就在这两个函数里：PushFront 和 PushBack 走的是同一条代码路径，
    // 「插到第一个元素前面」（head_ 之后）和「插到最后一个元素后面」（tail_ 之后）
    // 完全不需要区分，更不需要特判空表。空表的 tail_ 就是 &head_，
    // 于是 PushBack 自动退化成 PushFront。
    void PushFront(const T& value) { InsertAfter(iterator(&head_), value); }
    void PushFront(T&& value) { InsertAfter(iterator(&head_), std::move(value)); }
    void PushBack(const T& value) { InsertAfter(iterator(tail_), value); }
    void PushBack(T&& value) { InsertAfter(iterator(tail_), std::move(value)); }

    iterator InsertAfter(iterator pos, const T& value) {
        auto* node = new SNode<T>(value);
        LinkAfter(static_cast<SNode<T>*>(pos.RawNode()), node);
        ++size_;
        return iterator(node);
    }
    iterator InsertAfter(iterator pos, T&& value) {
        auto* node = new SNode<T>(std::move(value));
        LinkAfter(static_cast<SNode<T>*>(pos.RawNode()), node);
        ++size_;
        return iterator(node);
    }

    // 在下标 index 处插入（0 表示插到最前面）。O(index)。
    iterator InsertAt(size_type index, const T& value) {
        if (index == 0) {
            return InsertAfter(iterator(&head_), value);
        }
        auto* prev = static_cast<SNode<T>*>(NodeAt(index - 1));
        return InsertAfter(iterator(prev), value);
    }

    // ---- 删除 ----
    // 同样地：删第一个元素就是「删掉哨兵的后继」，没有任何特判。
    void PopFront() noexcept { EraseAfter(iterator(&head_)); }

    // 单链表的软肋：删尾要先找到尾的前驱，而只有 next 指针，只能从头走一遍。
    // 这是「O(1) 尾插」换来「O(n) 删尾」的经典取舍。
    void PopBack() noexcept {
        if (size_ == 0) {
            return;
        }
        SNodeBase* prev = &head_;
        while (prev->next != tail_) {  // O(n)
            prev = prev->next;
        }
        EraseAfter(iterator(prev));
    }

    // 返回被删元素的下一个位置。pos 不能是 end()，也不能是最后一个元素
    // （在最后一个元素后面什么都没有，删不了）。
    iterator EraseAfter(iterator pos) noexcept {
        SNodeBase* node = pos.RawNode();
        SNodeBase* victim = node->next;
        if (victim == nullptr) {
            return end();
        }
        node->next = victim->next;
        if (tail_ == victim) {
            tail_ = node;  // 删掉的正好是尾节点，尾指针要回退
        }
        delete static_cast<SNode<T>*>(victim);
        --size_;
        return iterator(node->next);
    }

    void EraseAt(size_type index) noexcept {
        if (index == 0) {
            PopFront();
            return;
        }
        SNodeBase* prev = NodeAt(index - 1);
        if (prev != nullptr) {
            EraseAfter(iterator(prev));
        }
    }

    // 删除第一个等于 value 的节点，返回是否删掉了。需要 T 支持 ==。
    // 语义提醒：这里只删第一个匹配项，而 std::remove / list::remove 删的是全部匹配项。
    // 这种「看起来一样、细节不一样」的地方，正是对拍能抓出来的 bug。
    bool Remove(const T& value) {
        SNodeBase* prev = &head_;
        while (prev->next != nullptr &&
               !(static_cast<SNode<T>*>(prev->next)->value == value)) {
            prev = prev->next;
        }
        if (prev->next == nullptr) {
            return false;
        }
        EraseAfter(iterator(prev));
        return true;
    }

    void Clear() noexcept {
        SNodeBase* cur = head_.next;
        while (cur != nullptr) {
            SNodeBase* next = cur->next;
            delete static_cast<SNode<T>*>(cur);
            cur = next;
        }
        head_.next = nullptr;
        tail_ = &head_;
        size_ = 0;
    }

    // ---- 访问 ----
    // At(index) 是 O(index)：链表没有「按下标直接跳」的能力，必须一步一步走。
    // 这就是为什么链表不能做二分查找，也是它随机访问慢的根源。
    T& At(size_type index) {
        return static_cast<SNode<T>*>(NodeAt(index))->value;
    }
    const T& At(size_type index) const {
        return static_cast<const SNode<T>*>(NodeAt(index))->value;
    }

    iterator Find(const T& value) noexcept {
        SNodeBase* cur = head_.next;
        while (cur != nullptr) {
            if (static_cast<SNode<T>*>(cur)->value == value) {
                return iterator(cur);
            }
            cur = cur->next;
        }
        return end();
    }

    // ---- 反转 ----
    // 迭代版：边走边把 next 指针掉头。时间 O(n)，额外空间 O(1)。
    // 三个指针 prev / cur / next 的滚动是链表题的经典模板，值得背下来。
    void Reverse() noexcept {
        SNodeBase* prev = nullptr;
        SNodeBase* cur = head_.next;
        SNodeBase* old_head = cur;  // 反转后它会变成尾节点
        while (cur != nullptr) {
            SNodeBase* next = cur->next;  // 先存住后继，否则掉头后就找不到了
            cur->next = prev;             // 掉头
            prev = cur;
            cur = next;
        }
        head_.next = prev;
        tail_ = (old_head != nullptr) ? old_head : &head_;
    }

    // 递归版：思路是「先反转后面的，再让后一个节点指回自己」。
    // 风险：递归深度 = 链表长度，每层都在栈上占一个栈帧，空间复杂度 O(n)。
    //       10 万个节点就可能撑爆默认 1MB 的线程栈（下面有实测数字）。
    //       生产代码请用迭代版：同样是 O(n) 时间，但空间是 O(1)。
    void ReverseRecursive() {
        if (head_.next == nullptr) {
            return;
        }
        auto* old_head = static_cast<SNode<T>*>(head_.next);
        head_.next = ReverseRecursiveImpl(old_head);
        tail_ = old_head;  // 老的头在新链表里是尾
    }

    // 带栈探测的递归版：把每一层栈帧里「参数 node 的地址」记下来，
    // 用来实测递归到底吃了多少栈空间。逻辑和 ReverseRecursiveImpl 完全一样。
    void ReverseRecursiveMeasured(std::size_t& depth_out, std::size_t& stack_bytes_out) {
        depth_out = 0;
        stack_bytes_out = 0;
        if (head_.next == nullptr) {
            return;
        }
        auto* old_head = static_cast<SNode<T>*>(head_.next);
        StackProbe probe;
        head_.next = ReverseRecursiveProbe(old_head, probe);
        tail_ = old_head;
        depth_out = probe.frames;
        stack_bytes_out = probe.SpanBytes();
    }

    // ---- 快慢指针 ----
    // 找中点：慢指针每次走 1 步，快指针每次走 2 步，快指针走完时慢指针正好在中间。
    // 偶数个元素时返回「靠后」的那个中点（下标 n/2），这一点要和使用方约定清楚。
    iterator Middle() noexcept {
        SNodeBase* slow = head_.next;
        SNodeBase* fast = head_.next;
        while (fast != nullptr && fast->next != nullptr) {
            slow = slow->next;
            fast = fast->next->next;
        }
        return iterator(slow);  // 空表时是 end()
    }

    // Floyd 判圈。为什么快慢指针一定会相遇？
    //   设环长为 L。两个指针都进环之后，快指针相对慢指针的速度恒为 1（每轮追近 1 步），
    //   所以它们之间的「环上距离」每轮减 1，最多 L 轮就变成 0，也就是相遇。
    //   关键点是「相对速度恒为 1」——如果快指针每次走 3 步、相对速度是 2，
    //   而环长恰好是偶数，就可能永远在对侧错过（这也是为什么快指针取 2 倍速最稳）。
    // 时间 O(n)，额外空间 O(1)。这是判断链表有环的「标准答案」，
    // 哈希表法虽然也 O(n)，但要 O(n) 额外空间。
    bool HasCycle() const noexcept {
        const SNodeBase* slow = head_.next;
        const SNodeBase* fast = head_.next;
        while (fast != nullptr && fast->next != nullptr) {
            slow = slow->next;
            fast = fast->next->next;
            if (slow == fast) {
                return true;
            }
        }
        return false;
    }

    // 环的入口下标；无环返回 npos。
    // 数学推导：设 head 到入口距离 a，入口到相遇点距离 b，环长 L。
    //   相遇时慢走了 a + b，快走了 a + b + kL，而快是慢的两倍：
    //       2(a + b) = a + b + kL  ->  a + b = kL  ->  a = (k-1)L + (L - b)
    //   也就是「从头出发走 a 步」和「从相遇点出发走 a 步」都会停在入口上：
    //   后者先走完 L - b 步回到入口，再绕 k-1 圈回到入口。
    //   所以让一个指针从头、另一个从相遇点同速前进，它们必然在入口相遇。
    size_type CycleStartIndex() const noexcept {
        const SNodeBase* slow = head_.next;
        const SNodeBase* fast = head_.next;
        while (fast != nullptr && fast->next != nullptr) {
            slow = slow->next;
            fast = fast->next->next;
            if (slow == fast) {
                // 第二阶段：一个指针从头出发，另一个从相遇点出发，同速（每次 1 步）前进，
                // 由上面的推导 a = (k-1)L + (L - b) 可知它们必然在环入口相遇。
                // 注意不能「让从相遇点出发的指针原地不动、只从头走」——那样找到的是相遇点。
                const SNodeBase* from_head = head_.next;
                const SNodeBase* from_meet = slow;
                while (from_head != from_meet) {
                    from_head = from_head->next;
                    from_meet = from_meet->next;
                }
                size_type index = 0;
                for (const SNodeBase* p = head_.next; p != from_head; p = p->next) {
                    ++index;
                }
                return index;
            }
        }
        return npos;
    }

    // 环长；无环返回 0。从相遇点出发绕一圈回到相遇点即可。
    size_type CycleLength() const noexcept {
        const SNodeBase* slow = head_.next;
        const SNodeBase* fast = head_.next;
        while (fast != nullptr && fast->next != nullptr) {
            slow = slow->next;
            fast = fast->next->next;
            if (slow == fast) {
                size_type length = 1;
                const SNodeBase* p = slow->next;
                while (p != slow) {
                    p = p->next;
                    ++length;
                }
                return length;
            }
        }
        return 0;
    }

    // ---- 合并两个有序链表 ----
    // 时间 O(n + m)，空间 O(n + m)（这里用新建节点的方式实现，最直白）。
    // 工程上可以做到空间 O(1)：不复制 value，直接把两个链表的节点「接」到一条链上
    // （splice），这也是 std::list::merge 的做法。这里为了不引入额外的所有权复杂度，
    // 选了更好懂的复制版本。
    static SinglyList Merge(const SinglyList& a, const SinglyList& b) {
        SinglyList out;
        const_iterator ia = a.begin();
        const_iterator ib = b.begin();
        while (ia != a.end() && ib != b.end()) {
            // 相等时先取 a 的，保证「稳定性」：相等元素的相对顺序不变。
            if (*ib < *ia) {
                out.PushBack(*ib);
                ++ib;
            } else {
                out.PushBack(*ia);
                ++ia;
            }
        }
        for (; ia != a.end(); ++ia) {
            out.PushBack(*ia);
        }
        for (; ib != b.end(); ++ib) {
            out.PushBack(*ib);
        }
        return out;
    }

    // ---- 测试与自检用的钩子 ----
    // 把尾节点的 next 指到下标 index 的节点上，制造一个环。
    // 仅供教学演示判圈算法：有环的链表析构会死循环，所以用完必须 BreakCycleForTest()。
    void MakeCycleForTest(size_type index) noexcept {
        SNodeBase* target = NodeAt(index);
        if (target != nullptr && tail_ != &head_) {
            tail_->next = target;
        }
    }
    void BreakCycleForTest() noexcept {
        if (tail_ != &head_) {
            tail_->next = nullptr;
        }
    }

    // 检查内部不变量：size_ 与真实节点数是否一致、tail_ 是否真的指向最后一个节点。
    // 这种「自己检查自己」的函数在 Debug 里极其好用，assert 挂掉时能立刻定位。
    bool CheckInvariants() const noexcept {
        size_type counted = 0;
        const SNodeBase* last = &head_;
        for (const SNodeBase* p = head_.next; p != nullptr; p = p->next) {
            last = p;
            ++counted;
        }
        return counted == size_ && last == tail_;
    }

    void Swap(SinglyList& other) noexcept {
        // 借助一个空链表做中转：三次「整体摘链接链」，全程 O(1) 且不分配内存。
        SinglyList tmp;
        tmp.TakeFrom(*this);
        this->TakeFrom(other);
        other.TakeFrom(tmp);
    }

private:
    // StackProbe 要在 ReverseRecursiveMeasured 里用，所以定义在类内。
    struct StackProbe {
        std::uintptr_t high = 0;  // 栈上最高地址（最外层栈帧）
        std::uintptr_t low = 0;   // 栈上最低地址（最深层栈帧）
        std::size_t frames = 0;

        void Enter(std::uintptr_t addr) noexcept {
            if (frames == 0) {
                high = addr;
                low = addr;
            }
            if (addr > high) {
                high = addr;
            }
            if (addr < low) {
                low = addr;
            }
            ++frames;
        }
        std::size_t SpanBytes() const noexcept {
            return static_cast<std::size_t>(high - low);
        }
    };

    static SNode<T>* ReverseRecursiveImpl(SNode<T>* node) {
        auto* next = static_cast<SNode<T>*>(node->next);
        if (next == nullptr) {
            return node;  // 只剩一个节点：它就是反转后的头
        }
        SNode<T>* new_head = ReverseRecursiveImpl(next);
        next->next = node;  // 让后一个节点指回自己
        node->next = nullptr;
        return new_head;
    }

    static SNode<T>* ReverseRecursiveProbe(SNode<T>* node, StackProbe& probe) {
        probe.Enter(reinterpret_cast<std::uintptr_t>(&node));
        auto* next = static_cast<SNode<T>*>(node->next);
        if (next == nullptr) {
            return node;
        }
        SNode<T>* new_head = ReverseRecursiveProbe(next, probe);
        next->next = node;
        node->next = nullptr;
        return new_head;
    }

    void LinkAfter(SNode<T>* pos, SNode<T>* node) noexcept {
        node->next = pos->next;
        pos->next = node;
        if (tail_ == pos) {
            tail_ = node;  // 挂在尾部后面时要同步尾指针
        }
    }

    // 按下标找节点，越界返回 nullptr。
    SNodeBase* NodeAt(size_type index) noexcept {
        SNodeBase* cur = head_.next;
        for (size_type i = 0; i < index && cur != nullptr; ++i) {
            cur = cur->next;
        }
        return cur;
    }
    const SNodeBase* NodeAt(size_type index) const noexcept {
        const SNodeBase* cur = head_.next;
        for (size_type i = 0; i < index && cur != nullptr; ++i) {
            cur = cur->next;
        }
        return cur;
    }

    // 把 other 的整条链摘到自己身上，并把 other 复位成合法的空表。
    // 前置条件：*this 必须是空的（Swap / 移动构造 / 移动赋值都满足）。
    void TakeFrom(SinglyList& other) noexcept {
        if (other.size_ == 0) {
            return;
        }
        head_.next = other.head_.next;
        tail_ = other.tail_;
        size_ = other.size_;
        other.head_.next = nullptr;
        other.tail_ = &other.head_;
        other.size_ = 0;
    }

    // 头哨兵：它本身不存数据，head_.next 才指向第一个真元素。
    // 有了它，「在第一个元素之前插入」和「在任意元素之后插入」是同一个操作，
    // 于是 PushFront / PushBack / InsertAfter 可以共用一套没有分支的实现。
    SNodeBase head_;
    // 尾指针：指向最后一个真元素；空表时指向 head_ 自己。
    // 这个「空表时指向哨兵」的约定，让 PushBack 在空表上也不需要特判。
    SNodeBase* tail_ = &head_;
    size_type size_ = 0;
};

// ===========================================================================
// 第 3 部分：手写双向链表（头哨兵 + 尾哨兵）
// ===========================================================================
// 复杂度：
//   PushFront / PushBack / PopFront / PopBack / Insert / Erase  -> O(1)
//   Find / At / Reverse / Middle                               -> O(n)
//   Size / Empty / Front / Back                                -> O(1)
//   空间：每个元素额外背 2 个指针（sizeof(DNode<T>)），是 vector 的若干倍。
//
// 两个哨兵的不变式：
//   head_.next 指向第一个元素（空表时是 &tail_）
//   tail_.prev 指向最后一个元素（空表时是 &head_）
// 于是「在 pos 之前插入」这一个函数就覆盖了头插、尾插、中间插入三种情况。
//
// 教学用途，生产请用 std::list。
template <typename T>
class DoublyList {
public:
    using size_type = std::size_t;
    using value_type = T;
    using iterator = DListIterator<T, false>;
    using const_iterator = DListIterator<T, true>;

    DoublyList() noexcept {
        // 空表：头哨兵的 next 直接指向尾哨兵，尾哨兵的 prev 指向头哨兵。
        // 有了这一对「互相指着对方」的哨兵，插入删除永远不需要问「我是不是第一个/最后一个」。
        head_.next = &tail_;
        tail_.prev = &head_;
    }

    // Rule of Five：理由和单链表一样——类拥有裸指针，编译器生成的浅拷贝语义是错的。
    DoublyList(const DoublyList& other) : DoublyList() {
        for (const T& x : other) {
            PushBack(x);
        }
    }
    DoublyList(DoublyList&& other) noexcept : DoublyList() { TakeFrom(other); }

    DoublyList& operator=(const DoublyList& other) {
        if (this != &other) {
            DoublyList tmp(other);
            Swap(tmp);
        }
        return *this;
    }
    DoublyList& operator=(DoublyList&& other) noexcept {
        if (this != &other) {
            Clear();
            TakeFrom(other);
        }
        return *this;
    }
    ~DoublyList() { Clear(); }

    size_type Size() const noexcept { return size_; }
    bool Empty() const noexcept { return size_ == 0; }

    iterator begin() noexcept { return iterator(head_.next); }
    iterator end() noexcept { return iterator(&tail_); }
    const_iterator begin() const noexcept { return const_iterator(head_.next); }
    const_iterator end() const noexcept { return const_iterator(&tail_); }

    T& Front() noexcept { return static_cast<DNode<T>*>(head_.next)->value; }
    const T& Front() const noexcept { return static_cast<const DNode<T>*>(head_.next)->value; }
    T& Back() noexcept { return static_cast<DNode<T>*>(tail_.prev)->value; }
    const T& Back() const noexcept { return static_cast<const DNode<T>*>(tail_.prev)->value; }

    void PushFront(const T& value) { Insert(begin(), value); }
    void PushFront(T&& value) { Insert(begin(), std::move(value)); }
    void PushBack(const T& value) { Insert(end(), value); }
    void PushBack(T&& value) { Insert(end(), std::move(value)); }

    void PopFront() noexcept {
        if (size_ > 0) {
            Erase(begin());
        }
    }
    void PopBack() noexcept {
        if (size_ > 0) {
            Erase(iterator(tail_.prev));
        }
    }

    // 在 pos 之前插入一个节点。这就是双向链表 + 双哨兵的全部威力：
    // 头插（pos = begin()）、尾插（pos = end()）、中间插入用的是同一段代码。
    // 单链表要在「某节点之前」插入，还得先从头找它的前驱，O(n)——这就是 prev 指针买来的东西。
    iterator Insert(iterator pos, const T& value) {
        auto* node = new DNode<T>(value);
        SpliceBefore(pos.RawNode(), node);
        return iterator(node);
    }
    iterator Insert(iterator pos, T&& value) {
        auto* node = new DNode<T>(std::move(value));
        SpliceBefore(pos.RawNode(), node);
        return iterator(node);
    }

    // 删除 pos 处的元素，返回它的下一个位置。pos 不能是 end()。
    // O(1)：改两个指针 + delete，跟链表有多长完全无关，也不需要搬移任何其它元素。
    iterator Erase(iterator pos) noexcept {
        DNodeBase* node = pos.RawNode();
        DNodeBase* prev_node = node->prev;
        DNodeBase* next_node = node->next;
        prev_node->next = next_node;
        next_node->prev = prev_node;
        delete static_cast<DNode<T>*>(node);
        --size_;
        return iterator(next_node);
    }

    void Clear() noexcept {
        DNodeBase* cur = head_.next;
        while (cur != &tail_) {
            DNodeBase* next = cur->next;
            delete static_cast<DNode<T>*>(cur);
            cur = next;
        }
        head_.next = &tail_;
        tail_.prev = &head_;
        size_ = 0;
    }

    // O(index)：双向链表也不能按下标直接跳，仍然要一步一步走。
    // 「O(1) 插入删除」的前提是「你已经拿着那个位置的迭代器」。
    T& At(size_type index) {
        iterator it = begin();
        for (size_type i = 0; i < index; ++i) {
            ++it;
        }
        return *it;
    }
    const T& At(size_type index) const {
        const_iterator it = begin();
        for (size_type i = 0; i < index; ++i) {
            ++it;
        }
        return *it;
    }

    iterator Find(const T& value) noexcept {
        for (iterator it = begin(); it != end(); ++it) {
            if (*it == value) {
                return it;
            }
        }
        return end();
    }

    // 反转：做法是「逐节点交换 prev / next」。
    // 交换完之后，老的首节点的 next 会错误地指向头哨兵（它现在是尾节点），
    // 老的尾节点的 prev 会错误地指向尾哨兵（它现在是首节点），
    // 所以最后只要修这两个端点即可。中间节点的指针在交换后天然就是对的。
    void Reverse() noexcept {
        if (size_ < 2) {
            return;
        }
        DNodeBase* first = head_.next;
        DNodeBase* last = tail_.prev;
        // 交换后 p->prev 里存的正是「原来的 next」，所以沿 prev 走 = 按原顺序前进。
        for (DNodeBase* p = first; p != &tail_; p = p->prev) {
            std::swap(p->prev, p->next);
        }
        head_.next = last;
        tail_.prev = first;
        first->next = &tail_;
        last->prev = &head_;
    }

    // 快慢指针找中点：偶数时返回下标 n/2 的那个（靠后）。
    iterator Middle() noexcept {
        DNodeBase* slow = head_.next;
        DNodeBase* fast = head_.next;
        while (fast != &tail_ && fast->next != &tail_) {
            slow = slow->next;
            fast = fast->next->next;
        }
        return iterator(slow);
    }

    bool CheckInvariants() const noexcept {
        size_type counted = 0;
        const DNodeBase* prev = &head_;
        for (const DNodeBase* p = head_.next; p != &tail_; p = p->next) {
            if (p->prev != prev) {
                return false;
            }
            prev = p;
            ++counted;
        }
        return counted == size_ && tail_.prev == prev;
    }

    void Swap(DoublyList& other) noexcept {
        // 和单链表同一套思路：三次整体摘链接链。这里额外要看住两个哨兵的指针，
        // 但因为全部是「整条链的端点重接」，依然是 O(1)。
        DoublyList tmp;
        tmp.TakeFrom(*this);
        this->TakeFrom(other);
        other.TakeFrom(tmp);
    }

private:
    void SpliceBefore(DNodeBase* pos, DNode<T>* node) noexcept {
        DNodeBase* prev_node = pos->prev;
        node->prev = prev_node;
        node->next = pos;
        prev_node->next = node;
        pos->prev = node;
        ++size_;
    }

    // 前置条件：*this 必须是空的。
    void TakeFrom(DoublyList& other) noexcept {
        if (other.size_ == 0) {
            return;
        }
        DNodeBase* first = other.head_.next;
        DNodeBase* last = other.tail_.prev;
        head_.next = first;
        first->prev = &head_;
        tail_.prev = last;
        last->next = &tail_;
        size_ = other.size_;
        other.head_.next = &other.tail_;
        other.tail_.prev = &other.head_;
        other.size_ = 0;
    }

    DNodeBase head_;  // 头哨兵（不存数据）
    DNodeBase tail_;  // 尾哨兵（不存数据）
    size_type size_ = 0;
};

// ===========================================================================
// 第 4 部分：对拍与测量用的小工具
// ===========================================================================
// 逐元素比对：手写链表 vs STL 容器。对拍是验证数据结构实现最有效的手段——
// 你不需要手工推演每一处指针，只要让两边跑同一串操作，然后问「结果一样吗」。
bool SameElements(const SinglyList<int>& mine, const std::forward_list<int>& ref) {
    return std::equal(mine.begin(), mine.end(), ref.begin(), ref.end());
}

bool SameElements(const DoublyList<int>& mine, const std::list<int>& ref) {
    return std::equal(mine.begin(), mine.end(), ref.begin(), ref.end());
}

bool SameValues(const SinglyList<int>& mine, const std::vector<int>& ref) {
    return std::equal(mine.begin(), mine.end(), ref.begin(), ref.end());
}

std::vector<int> MakeRandom(std::size_t n, unsigned int seed) {
    std::mt19937 rng(seed);
    std::vector<int> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = static_cast<int>(rng() % 1000000u);
    }
    return v;
}

SinglyList<int> MakeSingly(const std::vector<int>& data) {
    SinglyList<int> list;
    for (const int x : data) {
        list.PushBack(x);
    }
    return list;
}

DoublyList<int> MakeDoubly(const std::vector<int>& data) {
    DoublyList<int> list;
    for (const int x : data) {
        list.PushBack(x);
    }
    return list;
}

// ---- 测量用的工作负载 ----
std::uint64_t SumVector(const std::vector<int>& v) {
    std::uint64_t sum = 0;
    for (const int x : v) {
        sum += static_cast<std::uint64_t>(x);
    }
    return sum;
}

std::uint64_t SumSingly(const SinglyList<int>& l) {
    std::uint64_t sum = 0;
    for (const int x : l) {
        sum += static_cast<std::uint64_t>(x);
    }
    return sum;
}

std::uint64_t SumList(const std::list<int>& l) {
    std::uint64_t sum = 0;
    for (const int x : l) {
        sum += static_cast<std::uint64_t>(x);
    }
    return sum;
}

// 一次「随机访问」 = 按下标取一个元素。三者的接口形式不同，但语义完全一样。
std::uint64_t AccessVector(const std::vector<int>& v, const std::vector<std::size_t>& idx) {
    std::uint64_t sum = 0;
    for (const std::size_t i : idx) {
        sum += static_cast<std::uint64_t>(v[i]);
    }
    return sum;
}

std::uint64_t AccessSingly(const SinglyList<int>& l, const std::vector<std::size_t>& idx) {
    std::uint64_t sum = 0;
    for (const std::size_t i : idx) {
        sum += static_cast<std::uint64_t>(l.At(i));
    }
    return sum;
}

std::uint64_t AccessList(const std::list<int>& l, const std::vector<std::size_t>& idx) {
    std::uint64_t sum = 0;
    for (const std::size_t i : idx) {
        sum += static_cast<std::uint64_t>(*std::next(l.begin(), static_cast<std::ptrdiff_t>(i)));
    }
    return sum;
}

// 中间插入 + 删除一轮。三者都插在「中间位置」再把它删掉，规模保持不变，
// 这样比较的才是「单纯的位置改动成本」，而不是容器长度的差异。
// vector：每次都要把后半段元素整体搬移；
// list  ：只改两个指针（前提是手里已经握着中间位置的迭代器）。
std::uint64_t MiddleChurnVector(std::size_t n, int k) {
    std::vector<int> v(n, 1);
    const std::ptrdiff_t mid = static_cast<std::ptrdiff_t>(n / 2);
    for (int i = 0; i < k; ++i) {
        auto it = v.insert(v.begin() + mid, i);
        v.erase(it);
    }
    return v.size();
}

std::uint64_t MiddleChurnList(std::size_t n, int k) {
    std::list<int> l(n, 1);
    auto mid = std::next(l.begin(), static_cast<std::ptrdiff_t>(n / 2));
    for (int i = 0; i < k; ++i) {
        auto it = l.insert(mid, i);
        l.erase(it);
    }
    return l.size();
}

std::uint64_t MiddleChurnDoubly(std::size_t n, int k) {
    DoublyList<int> l;
    for (std::size_t i = 0; i < n; ++i) {
        l.PushBack(1);
    }
    auto mid = l.begin();
    for (std::size_t i = 0; i < n / 2; ++i) {
        ++mid;
    }
    for (int i = 0; i < k; ++i) {
        auto it = l.Insert(mid, i);
        l.Erase(it);
    }
    return l.Size();
}

// 单链表没有 prev 指针，插到中间之前只能每次都从头走一遍 O(n)：
// 这一项它注定要输，因为「找位置」的代价盖过了「插入本身 O(1)」的优势。
std::uint64_t MiddleChurnSingly(std::size_t n, int k) {
    SinglyList<int> l;
    for (std::size_t i = 0; i < n; ++i) {
        l.PushBack(1);
    }
    for (int i = 0; i < k; ++i) {
        l.InsertAt(n / 2, i);
        l.EraseAt(n / 2);
    }
    return l.Size();
}

// 大元素（4KB，比如一个图像行缓存、一个网络包缓冲）。用同样的「中间插入 + 删除」
// 负载，把元素放大到让 vector 的搬移量真正成为瓶颈，看链表什么时候反超。
struct BigPayload {
    int id;
    char data[4092];  // 凑成正好 4096 字节
};

std::uint64_t MiddleChurnVectorBig(std::size_t n, int k) {
    std::vector<BigPayload> v(n);
    const std::ptrdiff_t mid = static_cast<std::ptrdiff_t>(n / 2);
    BigPayload item{};
    for (int i = 0; i < k; ++i) {
        item.id = i;
        auto it = v.insert(v.begin() + mid, item);
        v.erase(it);
    }
    return v.size();
}

std::uint64_t MiddleChurnListBig(std::size_t n, int k) {
    std::list<BigPayload> l(n);
    auto mid = std::next(l.begin(), static_cast<std::ptrdiff_t>(n / 2));
    BigPayload item{};
    for (int i = 0; i < k; ++i) {
        item.id = i;
        auto it = l.insert(mid, item);
        l.erase(it);
    }
    return l.size();
}

std::uint64_t MiddleChurnDoublyBig(std::size_t n, int k) {
    DoublyList<BigPayload> l;
    const BigPayload fill{};
    for (std::size_t i = 0; i < n; ++i) {
        l.PushBack(fill);
    }
    auto mid = l.begin();
    for (std::size_t i = 0; i < n / 2; ++i) {
        ++mid;
    }
    BigPayload item{};
    for (int i = 0; i < k; ++i) {
        item.id = i;
        auto it = l.Insert(mid, item);
        l.Erase(it);
    }
    return l.Size();
}

}  // namespace

// ===========================================================================
// main
// ===========================================================================
int main() {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "本文件在 Debug 配置下运行，绝对耗时比 Release 慢，数字仅供参考，趋势才重要。\n";
    Note("教学用途，生产请用 std::vector / std::list / std::forward_list。");

    // =======================================================================
    Section("1. 哨兵节点：为什么它能消掉所有的边界特判");
    // =======================================================================
    Note("没有哨兵的写法，光是「插入」就要写四种分支：");
    Note("  空表插入、头部插入（要改 head 指针）、尾部插入（要改 tail 指针）、中间插入；");
    Note("  「删除」还要再加一条：删掉最后一个元素时要把尾指针置空。");
    Note("  每多一条分支就多一个忘记写的可能，而漏掉的后果是野指针或死循环。");
    Note("");
    Note("哨兵写法：让一个不存数据的「假节点」永远站在第一个真元素前面。");
    Note("  单链表：head_ 是哨兵，head_.next 才是首元素；尾指针在空表时指向 head_ 自己。");
    Note("  双向链表：head_ 和 tail_ 两个哨兵互相指着对方（空表时 head_.next == &tail_）。");
    Note("于是「插入」只剩一个动作：挂到某个节点后面（或前面）。");
    Note("  PushFront == InsertAfter(&head_, v)；PushBack == InsertAfter(tail_, v)，");
    Note("  空表时 tail_ 等于 &head_，两条路径自动合流，一行特判都不需要。");
    std::cout << "\n";

    {
        // 用同一段代码演示「空表 -> 头插 -> 尾插 -> 删头 -> 删尾」，全程无特判。
        SinglyList<int> s;
        assert(s.Empty() && s.CheckInvariants());

        s.PushBack(10);  // 空表尾插：tail_ == &head_，等价于头插
        s.PushFront(20); // 头插
        s.PushBack(30);  // 尾插（此时 tail_ 已指向 10）
        assert(s.CheckInvariants());
        assert(s.Size() == 3 && s.Front() == 20 && s.Back() == 30);
        std::cout << "  单链表 20 -> 10 -> 30: ";
        for (const int x : s) {
            std::cout << x << " ";
        }
        std::cout << "\n";

        s.PopFront();  // == EraseAfter(&head_)，删首元素不需要特判
        s.PopBack();   // 需要走一遍找前驱（单链表的代价），但仍然不用特判
        assert(s.CheckInvariants());
        assert(s.Size() == 1 && s.Front() == 10);

        DoublyList<int> d;
        assert(d.Empty() && d.CheckInvariants());
        d.PushBack(1);  // pos == end() == &tail_，插入函数把它当成「插到尾哨兵之前」
        d.PushFront(0); // pos == begin()，同一个函数
        d.PushBack(2);
        assert(d.CheckInvariants());
        assert(d.Size() == 3 && d.Front() == 0 && d.Back() == 2);
        std::cout << "  双向链表 0 <-> 1 <-> 2: ";
        for (const int x : d) {
            std::cout << x << " ";
        }
        std::cout << "（--end() = " << *(--d.end()) << "，双向迭代器可用）\n";

        d.PopFront();
        d.PopBack();
        assert(d.CheckInvariants());
        assert(d.Size() == 1 && d.Front() == 1);
        Note("");
        Note("CheckInvariants() 会验证「size 计数」和「尾指针」和真实节点链一致，");
        Note("本文件每一次改动链表之后都会 assert 一次——这是链表实现最实用的自检手段。");
    }

    // =======================================================================
    Section("2. 单链表对拍：SinglyList vs std::vector / std::forward_list");
    // =======================================================================
    Note("做法：把同一串随机操作同时作用在手写单链表和参考容器上，");
    Note("      每一步都比对 size 和全部元素。对拍比手工推演指针可靠得多。");
    std::cout << "\n";

    {
        std::mt19937 rng(2024u);
        SinglyList<int> mine;
        std::vector<int> ref;

        for (int step = 0; step < 4000; ++step) {
            const unsigned int op = rng() % 100u;
            const int value = static_cast<int>(rng() % 1000u);
            if (op < 30u) {
                mine.PushBack(value);
                ref.push_back(value);
            } else if (op < 45u) {
                mine.PushFront(value);
                ref.insert(ref.begin(), value);
            } else if (op < 55u) {
                if (!ref.empty()) {
                    mine.PopBack();
                    ref.pop_back();
                }
            } else if (op < 65u) {
                if (!ref.empty()) {
                    mine.PopFront();
                    ref.erase(ref.begin());
                }
            } else if (op < 78u) {
                const std::size_t pos =
                    ref.empty() ? 0 : (static_cast<std::size_t>(rng()) % (ref.size() + 1));
                mine.InsertAt(pos, value);
                ref.insert(ref.begin() + static_cast<std::ptrdiff_t>(pos), value);
            } else if (op < 88u) {
                if (!ref.empty()) {
                    const std::size_t pos = static_cast<std::size_t>(rng()) % ref.size();
                    mine.EraseAt(pos);
                    ref.erase(ref.begin() + static_cast<std::ptrdiff_t>(pos));
                }
            } else if (op < 94u) {
                // 注意语义差异：本文件的 Remove 只删「第一个」匹配项，
                // 而 std::remove 会删掉「全部」匹配项。对拍时必须让两边语义一致，
                // 所以参考侧用 find + erase，而不是 std::remove。
                const bool removed_mine = mine.Remove(value);
                const auto it = std::find(ref.begin(), ref.end(), value);
                const bool removed_ref = (it != ref.end());
                if (removed_ref) {
                    ref.erase(it);
                }
                assert(removed_mine == removed_ref);
            } else {
                mine.Reverse();
                std::reverse(ref.begin(), ref.end());
            }

            assert(mine.Size() == ref.size());
            assert(SameValues(mine, ref));
            assert(mine.CheckInvariants());
        }
        std::cout << "  与 std::vector 对拍 4000 步通过，最终 size = " << mine.Size() << "\n";

        // 专门和 std::forward_list 对拍 insert_after / erase_after 这两个接口：
        // 这是单链表「原生」的操作形态，也是接在哨兵后面的直接体现。
        SinglyList<int> a;
        std::forward_list<int> b;
        std::size_t b_size = 0;
        for (int step = 0; step < 1000; ++step) {
            const unsigned int op = rng() % 100u;
            if (op < 60u) {
                const std::size_t pos = b_size == 0
                                            ? 0
                                            : (static_cast<std::size_t>(rng()) % (b_size + 1));
                const int value = static_cast<int>(rng() % 100u);
                a.InsertAt(pos, value);
                auto it = b.before_begin();
                for (std::size_t i = 0; i < pos; ++i) {
                    ++it;
                }
                b.insert_after(it, value);
                ++b_size;
            } else if (op < 90u) {
                if (b_size > 0) {
                    const std::size_t pos = static_cast<std::size_t>(rng()) % b_size;
                    a.EraseAt(pos);
                    auto it = b.before_begin();
                    for (std::size_t i = 0; i < pos; ++i) {
                        ++it;
                    }
                    b.erase_after(it);
                    --b_size;
                }
            } else {
                a.Reverse();
                b.reverse();
            }
            assert(a.Size() == b_size);
            assert(SameElements(a, b));
            assert(a.CheckInvariants());
        }
        std::cout << "  与 std::forward_list 对拍 1000 步通过，最终 size = " << a.Size() << "\n";

        // 尾插 / 中间插入 / 中点这些 vector 参考版本也一并验证。
        SinglyList<int> m = MakeSingly({1, 2, 3, 4, 5, 6});
        assert(m.Middle() != m.end() && *m.Middle() == 4);  // 偶数个元素取靠后的中点 n/2
        SinglyList<int> merged_probe = SinglyList<int>::Merge(m, SinglyList<int>());
        assert(SameValues(merged_probe, std::vector<int>({1, 2, 3, 4, 5, 6})));
        std::cout << "  6 个元素的中点 = " << *m.Middle() << "（下标 n/2，偶数取靠后者）\n";
    }

    // =======================================================================
    Section("3. 双向链表对拍：DoublyList vs std::list");
    // =======================================================================
    {
        std::mt19937 rng(777u);
        DoublyList<int> mine;
        std::list<int> ref;

        for (int step = 0; step < 3000; ++step) {
            const unsigned int op = rng() % 100u;
            const int value = static_cast<int>(rng() % 1000u);
            if (op < 30u) {
                mine.PushBack(value);
                ref.push_back(value);
            } else if (op < 45u) {
                mine.PushFront(value);
                ref.push_front(value);
            } else if (op < 55u) {
                if (!ref.empty()) {
                    mine.PopBack();
                    ref.pop_back();
                }
            } else if (op < 65u) {
                if (!ref.empty()) {
                    mine.PopFront();
                    ref.pop_front();
                }
            } else if (op < 80u) {
                // 在随机位置插入：两边都要先「走到那个位置」。
                // 链表没有下标，只能一步步 ++；这本身也是链表的一个事实。
                const std::size_t pos =
                    ref.empty() ? 0 : (static_cast<std::size_t>(rng()) % (ref.size() + 1));
                auto it = ref.begin();
                for (std::size_t i = 0; i < pos; ++i) {
                    ++it;
                }
                auto mit = mine.begin();
                for (std::size_t i = 0; i < pos; ++i) {
                    ++mit;
                }
                mine.Insert(mit, value);
                ref.insert(it, value);
            } else if (op < 92u) {
                if (!ref.empty()) {
                    const std::size_t pos = static_cast<std::size_t>(rng()) % ref.size();
                    auto it = ref.begin();
                    for (std::size_t i = 0; i < pos; ++i) {
                        ++it;
                    }
                    auto mit = mine.begin();
                    for (std::size_t i = 0; i < pos; ++i) {
                        ++mit;
                    }
                    mine.Erase(mit);
                    ref.erase(it);
                }
            } else {
                mine.Reverse();
                ref.reverse();
            }

            assert(mine.Size() == ref.size());
            assert(SameElements(mine, ref));
            assert(mine.CheckInvariants());
        }
        std::cout << "  与 std::list 对拍 3000 步通过，最终 size = " << mine.Size() << "\n";
        assert(*mine.begin() == *ref.begin() && mine.Front() == ref.front());
        assert(mine.Back() == ref.back());
        assert(mine.Middle() != mine.end());
        Note("");
        Note("反向迭代器也逐个验证：*--end() 必须等于最后一个元素。");
        {
            DoublyList<int> d = MakeDoubly({5, 6, 7});
            auto it = d.end();
            --it;
            assert(*it == 7);
            --it;
            assert(*it == 6);
            assert(d.CheckInvariants());
            std::cout << "  --end() 连续两次得到 7, 6，双向迭代器正确\n";
        }
    }

    // =======================================================================
    Section("4. 反转：迭代 vs 递归，以及递归的栈代价");
    // =======================================================================
    Note("迭代版：三个指针滚动，时间 O(n)、额外空间 O(1)，是工程上应该用的版本。");
    Note("递归版：代码更短，但递归深度 = 链表长度，空间 O(n)，长链表会栈溢出。");
    std::cout << "\n";

    {
        std::vector<int> data = MakeRandom(2000, 31u);
        std::vector<int> expected = data;
        std::reverse(expected.begin(), expected.end());

        SinglyList<int> iter_list = MakeSingly(data);
        iter_list.Reverse();
        assert(SameValues(iter_list, expected));
        assert(iter_list.CheckInvariants());

        SinglyList<int> rec_list = MakeSingly(data);
        rec_list.ReverseRecursive();
        assert(SameValues(rec_list, expected));
        assert(rec_list.CheckInvariants());

        // 边界：空表和单元素表
        SinglyList<int> empty_list;
        empty_list.Reverse();
        empty_list.ReverseRecursive();
        assert(empty_list.Empty() && empty_list.CheckInvariants());
        SinglyList<int> one;
        one.PushBack(42);
        one.Reverse();
        assert(one.Size() == 1 && one.Front() == 42 && one.CheckInvariants());

        std::size_t depth = 0;
        std::size_t stack_bytes = 0;
        SinglyList<int> probe_list = MakeSingly(data);
        probe_list.ReverseRecursiveMeasured(depth, stack_bytes);
        assert(SameValues(probe_list, expected));
        assert(probe_list.CheckInvariants());

        std::cout << "  n = " << data.size() << "：三种写法结果一致，且不变量都成立\n";
        std::cout << "  递归实测：递归深度 = " << depth << " 层，栈上跨度 = " << stack_bytes
                  << " 字节，约 " << (stack_bytes / (depth > 1 ? depth - 1 : 1))
                  << " 字节/帧\n";
        const double per_frame =
            static_cast<double>(stack_bytes) / static_cast<double>(depth > 1 ? depth - 1 : 1);
        std::cout << "  外推：n = 1000000 时，仅递归就要 " << (per_frame * 1000000.0 / 1048576.0)
                  << " MB 栈空间\n";
        Note("");
        Note("Windows 默认线程栈只有 1MB。按上面的数字，几万个节点的链表用递归反转就会崩。");
        Note("这类「写法优雅但会爆栈」的递归，在工程里必须换成迭代——");
        Note("同样的 O(n) 时间，空间从 O(n) 降到 O(1)。");
        Note("（Debug 构建的栈帧比 Release 更胖，Release 下每帧会小不少，但结论不变。）");
    }

    // =======================================================================
    Section("5. 快慢指针：找中点 + Floyd 判圈");
    // =======================================================================
    Note("快慢指针就是「一个走一步、一个走两步」，一次遍历同时解决多个问题：");
    Note("  找中点 O(n)/O(1)、判环 O(n)/O(1)、找环入口 O(n)/O(1)、找倒数第 k 个 O(n)/O(1)。");
    Note("为什么判圈一定相遇：进入环之后，快指针相对慢指针的速度恒为 1，");
    Note("两者在环上的距离每一步减 1，最多 L 步（L 为环长）必然追上。");
    Note("反例：如果快指针每次走 3 步（相对速度 2），而环长是偶数，就可能永远错过。");
    std::cout << "\n";

    {
        // 找中点：与「长度一半」的朴素做法逐一对照。
        for (std::size_t n = 1; n <= 8; ++n) {
            std::vector<int> data(n);
            for (std::size_t i = 0; i < n; ++i) {
                data[i] = static_cast<int>(i);
            }
            SinglyList<int> l = MakeSingly(data);
            assert(*l.Middle() == data[n / 2]);
        }
        SinglyList<int> even = MakeSingly({1, 2, 3, 4});
        assert(*even.Middle() == 3);
        std::cout << "  找中点: n = 1..8 全部等于 data[n/2]（偶数取靠后的中点）\n";

        // 判圈：在链表的第 3 个节点处制造一个环。
        SinglyList<int> cyc = MakeSingly({0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
        assert(!cyc.HasCycle());
        assert(cyc.CycleStartIndex() == SinglyList<int>::npos);
        assert(cyc.CycleLength() == 0);

        cyc.MakeCycleForTest(3);  // 尾节点指回下标 3 的节点，环长 = 10 - 3 = 7
        assert(cyc.HasCycle());
        assert(cyc.CycleStartIndex() == 3);
        assert(cyc.CycleLength() == 7);
        std::cout << "  判圈: 有条链表 0..9，把尾节点接回下标 3 的节点后：\n";
        std::cout << "        有环 = " << (cyc.HasCycle() ? "是" : "否")
                  << "，环入口下标 = " << cyc.CycleStartIndex()
                  << "，环长 = " << cyc.CycleLength() << "（期望 3 和 7）\n";

        // 必须先把环断开，否则析构函数会沿着环一直走下去，永远不会结束。
        cyc.BreakCycleForTest();
        assert(!cyc.HasCycle());
        std::cout << "        断环后重新检测：有环 = " << (cyc.HasCycle() ? "是" : "否") << "\n";
        Note("");
        Note("注意：有环的链表析构会死循环，所以判圈算法的用途是「防御性检查」——");
        Note("      工程上链表本就不该出现环，出现环说明别处有 bug。");
    }

    // =======================================================================
    Section("6. 合并两个有序链表");
    // =======================================================================
    Note("两条有序链表的归并是 O(n + m)：每次只比较两个队首，取小的那个。");
    Note("这是归并排序的核心步骤（见 04 到 06 的排序文件）。");
    std::cout << "\n";

    {
        const std::vector<int> raw_a = {1, 4, 9, 16, 25};
        const std::vector<int> raw_b = {2, 3, 5, 8, 13, 21, 34};
        SinglyList<int> a = MakeSingly(raw_a);
        SinglyList<int> b = MakeSingly(raw_b);

        SinglyList<int> merged = SinglyList<int>::Merge(a, b);

        std::vector<int> expected;
        std::merge(raw_a.begin(), raw_a.end(), raw_b.begin(), raw_b.end(),
                   std::back_inserter(expected));
        assert(SameValues(merged, expected));

        std::cout << "  A = 1 4 9 16 25，B = 2 3 5 8 13 21 34\n";
        std::cout << "  归并结果 = ";
        for (const int x : merged) {
            std::cout << x << " ";
        }
        std::cout << "\n";
        assert(merged.CheckInvariants() && merged.Size() == expected.size());
        std::cout << "  与 std::merge 的结果逐元素相同，size = " << merged.Size() << "\n";

        // 含重复元素时验证稳定性：相等时先取 A 的，保证相等元素的相对次序不变。
        SinglyList<int> c = MakeSingly({1, 2, 2, 5});
        SinglyList<int> d = MakeSingly({2, 2, 3});
        SinglyList<int> cd = SinglyList<int>::Merge(c, d);
        assert(SameValues(cd, std::vector<int>({1, 2, 2, 2, 2, 3, 5})));
        std::cout << "  含重复元素 1 2 2 5 + 2 2 3 -> 1 2 2 2 2 3 5，顺序稳定\n";
    }

    // =======================================================================
    Section("7. 性能实测：缓存局部性才是链表输掉的根本原因");
    // =======================================================================
    Note("四个场景：顺序遍历、随机访问、中小元素的中间插入删除、大元素的中间插入删除。");
    Note("结论预告：前两项 vector 完胜；第三项 vector 还是赢（这个结果很反直觉，7.3 会解释）；");
    Note("          只有第四项——元素足够大时——链表才真的反超。");
    std::cout << "\n";

    constexpr std::size_t kTraverseN = 200000;
    constexpr std::size_t kAccessN = 20000;
    constexpr std::size_t kAccessLookups = 200;
    constexpr std::size_t kChurnN = 20000;
    constexpr int kChurnOps = 200;

    // ---- 7.1 顺序遍历求和 ----
    {
        std::cout << "7.1 顺序遍历求和（n = " << kTraverseN << "）\n";
        const std::vector<int> data = MakeRandom(kTraverseN, 999u);
        const SinglyList<int> singly = MakeSingly(data);
        const std::list<int> stdlist(data.begin(), data.end());

        WarmUp([&] { return SumVector(data); }, 2);
        WarmUp([&] { return SumSingly(singly); }, 2);
        WarmUp([&] { return SumList(stdlist); }, 2);

        // 三者求和结果必须完全一致——顺便当成一次正确性校验。
        const std::uint64_t s1 = SumVector(data);
        const std::uint64_t s2 = SumSingly(singly);
        const std::uint64_t s3 = SumList(stdlist);
        assert(s1 == s2 && s2 == s3);
        std::cout << "  校验：三者求和结果相同（" << s1 << "）\n\n";

        const double t_vec = BenchMedianUs([&] { return SumVector(data); }, 7);
        const double t_singly = BenchMedianUs([&] { return SumSingly(singly); }, 7);
        const double t_list = BenchMedianUs([&] { return SumList(stdlist); }, 7);

        Label("容器", 34);
        Label("耗时 (us)", 14);
        Label("相对 vector", 14);
        std::cout << "\n" << std::string(66, '-') << "\n";
        auto row = [&](const std::string& name, double us) {
            Label(name, 34);
            std::cout << std::setw(14) << us;
            std::cout << std::setw(14) << (us / t_vec) << "\n";
        };
        row("std::vector<int>", t_vec);
        row("手写单链表 SinglyList<int>", t_singly);
        row("std::list<int>", t_list);
        std::cout << "\n";
        Note("为什么一样是 O(n)，却能差这么多？");
        Note("  vector：元素连续，一个 64 字节 cache line 带回 16 个 int，");
        Note("          硬件预取器看出「顺序访问」后会提前把后面的数据搬进缓存；");
        Note("  链表  ：每个节点都是一次 new，散落在堆的各个角落，");
        Note("          每走一步都是一次「跟着指针去陌生地址」的访问，预取器基本帮不上忙。");
        Note("这就是「复杂度相同，性能可以差一个数量级」最经典的一课。");
        Note("");
        Note("诚实提示：这个倍数在不同次运行之间波动很大（25 到 70 倍），");
        Note("因为分母 vector 只有一两百微秒，很容易被缓存状态和调度噪声影响。");
        Note("所以别记住具体倍数，记住「差一个数量级以上、且方向永远不变」就够了。");
    }

    // ---- 7.2 随机访问第 k 个元素 ----
    {
        std::cout << "\n7.2 随机访问（n = " << kAccessN << "，随机取 " << kAccessLookups
                  << " 次，内部重复 5 轮）\n";
        const std::vector<int> data = MakeRandom(kAccessN, 55u);
        const SinglyList<int> singly = MakeSingly(data);
        const std::list<int> stdlist(data.begin(), data.end());

        std::vector<std::size_t> idx(kAccessLookups);
        std::mt19937 rng(4242u);
        for (std::size_t i = 0; i < kAccessLookups; ++i) {
            idx[i] = static_cast<std::size_t>(rng()) % kAccessN;
        }

        constexpr int kInner = 5;
        const std::uint64_t a1 = AccessVector(data, idx);
        const std::uint64_t a2 = AccessSingly(singly, idx);
        const std::uint64_t a3 = AccessList(stdlist, idx);
        assert(a1 == a2 && a2 == a3);

        WarmUp([&] { return Repeat([&] { return AccessSingly(singly, idx); }, 1); }, 1);

        const double t_vec = BenchMedianUs([&] { return Repeat([&] { return AccessVector(data, idx); }, kInner); }, 5);
        const double t_singly = BenchMedianUs([&] { return Repeat([&] { return AccessSingly(singly, idx); }, kInner); }, 5);
        const double t_list = BenchMedianUs([&] { return Repeat([&] { return AccessList(stdlist, idx); }, kInner); }, 5);

        Label("容器", 34);
        Label("耗时 (us)", 14);
        Label("相对 vector", 14);
        std::cout << "\n" << std::string(66, '-') << "\n";
        auto row = [&](const std::string& name, double us) {
            Label(name, 34);
            std::cout << std::setw(14) << us;
            std::cout << std::setw(14) << (us / t_vec) << "\n";
        };
        row("std::vector<int>（下标直达）", t_vec);
        row("手写单链表（走 i 步）", t_singly);
        row("std::list（std::next 走 i 步）", t_list);
        std::cout << "\n";
        Note("vector 的随机访问是真正的 O(1)：一次乘加算出地址，一次内存读；");
        Note("链表没有「第 k 个」这个概念，只能从头一步步数，是 O(k)。");
        Note("所以链表不能二分查找，任何需要「按下标跳转」的算法都不该用链表。");
        Note("");
        Note("诚实提示：vector 这一行的绝对时间已经落到计时器分辨率附近（几微秒），");
        Note("它的具体数值不太可信；但「两万倍」这个量级差距是真实且稳定的。");
    }

    // ---- 7.3 中间插入 / 删除（小元素） ----
    {
        std::cout << "\n7.3 在中间位置插入 + 删除（小元素 int，n = " << kChurnN << "，"
                  << kChurnOps << " 轮，内部重复 5 轮）\n";
        constexpr int kInner = 5;

        const double t_vec = BenchMedianUs(
            [&] { return Repeat([&] { return MiddleChurnVector(kChurnN, kChurnOps); }, kInner); }, 5);
        const double t_stdlist = BenchMedianUs(
            [&] { return Repeat([&] { return MiddleChurnList(kChurnN, kChurnOps); }, kInner); }, 5);
        const double t_doubly = BenchMedianUs(
            [&] { return Repeat([&] { return MiddleChurnDoubly(kChurnN, kChurnOps); }, kInner); }, 5);
        const double t_singly = BenchMedianUs(
            [&] { return Repeat([&] { return MiddleChurnSingly(kChurnN, kChurnOps); }, kInner); }, 5);

        // 每轮里做了 kInner 次「插入 + 删除」，换算成单次操作对的纳秒数更好比较。
        constexpr int kPairs = kChurnOps * kInner;
        Label("容器（每轮 = 插入 1 次 + 删除 1 次）", 44);
        Label("耗时 (us)", 14);
        Label("相对 vector", 14);
        Label("ns/操作对", 14);
        std::cout << "\n" << std::string(90, '-') << "\n";
        auto row = [&](const std::string& name, double us) {
            Label(name, 44);
            std::cout << std::setw(14) << us;
            std::cout << std::setw(14) << (us / t_vec);
            std::cout << std::setw(14) << (us / static_cast<double>(kPairs) * 1000.0) << "\n";
        };
        row("std::vector<int>（每次搬移后半段）", t_vec);
        row("std::list<int>（持有中间迭代器）", t_stdlist);
        row("手写 DoublyList<int>（持有中间迭代器）", t_doubly);
        row("手写 SinglyList<int>（每次从头找位置）", t_singly);
        std::cout << "\n";
        Note("先看最反直觉的一点：即使在「中间插入删除」这一项，");
        Note("int 这种小元素 + 2 万元素的规模下，vector 依然赢了链表一个数量级。");
        Note("原因是链表付了两笔 vector 不用付的钱：");
        std::cout << "     1) 缓存：vector 后半段只有 " << (kChurnN / 2 * sizeof(int))
                  << " 字节，整个容器都装得进二级缓存，" << "memmove 搬的是缓存内的高速数据；\n";
        Note("  2) 分配：链表每插一个节点都要 malloc 一次。Debug 的调试堆每次分配都要");
        Note("     记录调用点、每次释放都要校验堆——从上面的数字反推，一次 malloc + free");
        Note("     要二十微秒上下，远远盖过了「不用搬移元素」省下来的那点时间。");
        Note("");
        Note("所以「中间插入删除就用链表」这句话是有前提的：");
        Note("必须让 vector 的搬移量真的超过链表的单次分配成本。见 7.4。");
        Note("单链表那一行更是又输一层：它没有 prev 指针，每次都要从头走到中间（O(n)），");
        Note("「找位置」的成本把「插入 O(1)」的优势也吃干净了。");
    }

    // ---- 7.4 中间插入 / 删除（大元素）：链表终于反超 ----
    {
        constexpr std::size_t kBigN = 5000;
        constexpr int kBigOps = 20;
        const std::size_t tail_bytes = (kBigN / 2) * sizeof(BigPayload);

        std::cout << "\n7.4 在中间位置插入 + 删除（大元素 " << sizeof(BigPayload) << " 字节，n = "
                  << kBigN << "，" << kBigOps << " 轮）\n";
        std::cout << "    这时的关键数字：vector 每次插入要搬移后半段 " << (kBigN / 2)
                  << " 个元素 = " << tail_bytes << " 字节 = " << (tail_bytes / 1048576.0)
                  << " MB\n\n";

        const double t_vec = BenchMedianUs([&] { return MiddleChurnVectorBig(kBigN, kBigOps); }, 3);
        const double t_stdlist = BenchMedianUs([&] { return MiddleChurnListBig(kBigN, kBigOps); }, 3);
        const double t_doubly = BenchMedianUs([&] { return MiddleChurnDoublyBig(kBigN, kBigOps); }, 3);

        Label("容器（每轮 = 插入 1 次 + 删除 1 次）", 44);
        Label("耗时 (us)", 14);
        Label("相对 vector", 14);
        Label("ns/操作对", 14);
        std::cout << "\n" << std::string(90, '-') << "\n";
        auto row = [&](const std::string& name, double us) {
            Label(name, 44);
            std::cout << std::setw(14) << us;
            std::cout << std::setw(14) << (us / t_vec);
            std::cout << std::setw(14) << (us / static_cast<double>(kBigOps) * 1000.0) << "\n";
        };
        row("std::vector<BigPayload>（搬移 10MB）", t_vec);
        row("std::list<BigPayload>（持有中间迭代器）", t_stdlist);
        row("手写 DoublyList<BigPayload>（持有中间迭代器）", t_doubly);
        std::cout << "\n";
        std::cout << "  这才是链表的「主场」：元素一大，vector 每次插入都要搬走 "
                  << (tail_bytes / 1048576.0) << " MB，\n";
        Note("memmove 再快也架不住搬的数据量本身变成了瓶颈；");
        Note("而链表仍然是「一次分配 + 改两个指针」，跟元素大小无关。");
        Note("上面的「相对 vector」一列小于 1，就说明链表反超了。");
        Note("");
        Note("把 7.3 和 7.4 放在一起，就得到了判断标准：");
        Note("  链表赢的条件 = (n / 2) * sizeof(T) 的搬移成本 > 单次节点分配成本。");
        Note("  n 越大、元素越大、分配器越快（Release 比 Debug 快一个数量级），链表越占优。");
    }

    // ---- 7.5 每个元素的内存开销 ----
    {
        std::cout << "\n7.5 内存开销对比（" << kTraverseN << " 个 int）\n";
        const std::size_t vec_bytes = kTraverseN * sizeof(int);
        const std::size_t singly_bytes = kTraverseN * sizeof(SNode<int>);
        const std::size_t doubly_bytes = kTraverseN * sizeof(DNode<int>);
        Label("std::vector<int>", 34);
        std::cout << ": " << vec_bytes << " 字节（" << sizeof(int) << " 字节/元素）\n";
        Label("手写单链表 SinglyList<int>", 34);
        std::cout << ": " << singly_bytes << " 字节（" << sizeof(SNode<int>) << " 字节/元素）\n";
        Label("手写双向链表 DoublyList<int>", 34);
        std::cout << ": " << doubly_bytes << " 字节（" << sizeof(DNode<int>) << " 字节/元素）\n";
        Label("std::list<int> 估算", 34);
        std::cout << ": " << (kTraverseN * (sizeof(int) + 2 * sizeof(void*)))
                  << " 字节（" << (sizeof(int) + 2 * sizeof(void*)) << " 字节/元素）\n";
        std::cout << "\n";
        Note("同一份数据，链表要占 4 到 6 倍的内存。这直接造成两个后果：");
        Note("  1) 同样大小的三级缓存能装下的元素少了好几倍，cache miss 更多；");
        Note("  2) 每个节点一次 new/delete，分配器本身的元数据开销还没算进去。");
        Note("内存占用本身就是缓存局部性的另一半原因。");
    }

    // =======================================================================
    Section("8. 工程结论");
    // =======================================================================
    Note("按实测结果排优先级：");
    Note("  1) 需要随机访问、顺序遍历、二分查找、紧凑存储 —— 用 std::vector，没有悬念；");
    Note("  2) 只在两端操作 —— std::deque（比 list 缓存友好得多，还能 O(1) 随机访问）；");
    Note("  3) 需要频繁在中间插入删除，同时满足「元素够大 / 数据量够大」且「能长期持有");
    Note("     位置句柄」—— 才考虑 std::list；");
    Note("  4) 只在头部操作、且真的只需要单向遍历 —— std::forward_list（省一个指针）。");
    Note("");
    Note("把实测数字连起来看，就是这条判断标准：");
    Note("  链表每次插入的固定成本 = 一次节点分配（Debug 实测一次 malloc + free 要");
    Note("  二十到三十微秒，Release 下会小一个数量级左右）；");
    Note("  vector 每次插入的成本 = 搬移后半段 (n/2) * sizeof(T) 字节；");
    Note("  只有后者大于前者时，链表才赢。7.3（int，4 万字节搬移）vector 赢，");
    Note("  7.4（4KB 元素，10MB 搬移）链表赢——交叉点就在这里。");
    Note("");
    Note("而真实项目里，元素往往很小（int、指针、小结构体）、分配又贵（Debug / 多线程 /");
    Note("内存碎片），所以「频繁中间插入删除」这个需求常常改用 vector + 批量重建，");
    Note("或者「数组 + 延迟删除 / 空闲链表」来满足，最后真正落到 std::list 的场景并不多。");
    Note("链表更重要的价值是「它教给你的思维」：哨兵节点简化边界、快慢指针一次遍历、");
    Note("指针改向的 O(1) 插入删除——这些手法在 LRU 缓存、内存池、文件系统、");
    Note("内核链表（Linux 的 list_head）里到处都在用。");
    std::cout << "\n";
    Note("教学用途，生产请用 STL。这些手写版本的价值在于让你知道 STL 为什么这么设计。");

    // 让 g_sink 真正被「读过」一次，确保它不会被整个优化掉。
    std::cout << "\n[校验] 累加器非零，说明所有测量结果都真实参与了运算: "
              << (g_sink != 0 ? "是" : "否") << "\n";

    return 0;
}
