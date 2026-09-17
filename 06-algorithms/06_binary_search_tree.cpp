// ============================================================================
// 06_binary_search_tree.cpp
// 演示主题：
//   1. 手写二叉搜索树（BST，Binary Search Tree）模板：insert / find / contains /
//      erase / size / empty / clear / height，完整的 Rule of Five（深拷贝 + 移动）
//   2. 删除节点的三种情况：叶子、单孩子、双孩子（用中序后继替换值再删后继）
//   3. 中序遍历得到有序序列：递归版与显式栈迭代版，并用 assert 验证严格升序
//   4. 高度定义（约定空树高度 = -1，叶子高度 = 0）与「退化」问题
//   5. 平衡树概念：AVL 的平衡因子与 LL / RR / LR / RL 四种旋转（有可运行的旋转代码）
//   6. 为什么 std::map 选红黑树而不是 AVL：旋转次数与工程取舍
//   7. 与 std::map / std::set 的 API 对照，以及「不要自己写平衡树」的实测证据
//
// 关键结论：
//   1. 朴素 BST 的 O(log n) 只是「期望」，不是「保证」：按升序插入 10 万条数据，
//      树会退化成一条 10 万层深的链表，查找从 O(log n) 变成 O(n)；
//   2. 退化树不只是「慢」，它还会让所有递归版本（查找 / 插入 / 删除 / 析构）栈溢出，
//      所以本文件的查找、高度统计、析构都额外提供迭代实现——这是真实的工程坑；
//   3. 删除双孩子节点时，「复制中序后继的键值再递归删除后继」比「真的搬运节点」
//      简单得多，因为搬运节点要处理父指针、左右孩子、以及根节点的特殊情况；
//   4. AVL 更严格平衡（高度差 <= 1）所以查找略快，但删除时的旋转次数是 O(log n)；
//      红黑树只要求「最长路径 <= 最短路径的 2 倍」，插入最多 2 次旋转、删除最多 3 次，
//      而 std::map 是通用容器、插入删除频繁，所以标准库选红黑树；
//   5. 教学用途，生产请用 std::map / std::set / std::multimap，不要自己写平衡树。
//
// 说明：本文件在 Debug（/Od）下编译运行，绝对耗时比 Release 慢很多，
//       这里所有数字仅供参考，看「趋势」和「数量级差异」才有意义。
// ============================================================================

#include <algorithm>
#include <assert.h>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// 防止测量结果被优化掉
// ---------------------------------------------------------------------------
// 编译器（尤其 Release 的 /O2）发现一个纯计算的返回值没人用，会直接把整段代码删掉，
// 于是你测出来的是「0 毫秒」——这不是算法快，是你的代码根本没跑。
// 解决办法：把结果写进一个 volatile 全局变量，强制产生真实的读写副作用。
// 注意 C++20 起 volatile 的复合赋值（g_sink += r）已被弃用（P1152），
// 所以下面一律写成「先读、再算、再写」的展开形式。
volatile std::uint64_t g_sink = 0;

// ---------------------------------------------------------------------------
// 打印小工具（本文件自带一份，因为「一个 .cpp = 一个独立可执行文件」）
// ---------------------------------------------------------------------------
void Section(const std::string& title) {
    std::cout << "\n============================================================\n";
    std::cout << title << "\n";
    std::cout << "============================================================\n";
}

void Note(const std::string& text) {
    std::cout << "  " << text << "\n";
}

// 估算字符串在终端里占的「显示列数」。
// 坑：std::setw 数的是 char 的个数，而一个中文字符在 UTF-8 里占 3 个字节、
// 终端里显示 2 列。所以直接用 setw 对齐含中文的表头，表格一定是歪的。
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

// 左对齐打印并补空格到指定显示宽度，后面的数字才能对齐到同一列。
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
// 为什么用中位数而不是平均值：操作系统调度、其他进程抢 CPU、缓存被换出都会让某一次
// 测量突然变慢，平均值会被异常值拉高，中位数对异常值免疫——它回答的是
// 「典型的一次到底要多久」。
// 为什么用 steady_clock 而不是 system_clock：前者是单调时钟，不会被 NTP 校时
// 或用户改系统时间影响。测「经过了多少时间」永远用 steady_clock。
// 返回值单位：微秒（us）。
template <typename Fn>
double BenchMedianUs(Fn&& fn, int repeats) {
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(repeats));

    for (int i = 0; i < repeats; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        const std::uint64_t result = fn();
        const auto t1 = std::chrono::steady_clock::now();

        // 防止被优化掉的落点：结果必须真正被用掉。
        g_sink = g_sink + result;

        const std::chrono::duration<double, std::micro> dt = t1 - t0;
        samples.push_back(dt.count());
    }

    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

// 预热：第一次调用带着「缺页中断 + 缓存冷启动 + 分支预测器未训练」的开销，
// 先空跑几遍，让这些一次性成本落在预热里。
template <typename Fn>
void WarmUp(Fn&& fn, int times) {
    for (int i = 0; i < times; ++i) {
        g_sink = g_sink + fn();
    }
}

// 把「一次操作」重复 repeat 次并累加结果。
// O(1) / O(log n) 的操作只要几纳秒到几十纳秒，直接计时测到的全是噪声。
// 重复 repeat 次只是给总耗时乘一个常数，不改变增长阶，却能提高信噪比。
template <typename Fn>
std::uint64_t Repeat(Fn&& fn, int repeat) {
    std::uint64_t acc = 0;
    for (int r = 0; r < repeat; ++r) {
        acc += fn();
    }
    return acc;
}

// ===========================================================================
// 第 1 部分：手写二叉搜索树（key-value，语义对齐 std::map）
// ===========================================================================
// 不变量（invariant，整个类都靠它活着）：
//   对任意节点 x，x 左子树里所有键 < x.key < x 右子树里所有键。
// 由此推出两条最重要的性质：
//   - 中序遍历（左 -> 根 -> 右）一定得到键的升序序列；
//   - 查找时每比较一次就能砍掉一棵子树，所以高度为 h 时查找是 O(h)。
//
// 复杂度（n = 节点数，h = 树高）：
//   平均（随机插入）h = O(log n)：insert / find / erase 都是 O(log n)；
//   最坏（有序插入）h = O(n)：三者都退化成 O(n)，而且树变成链表。
//   空间：每个节点 2 个指针 + 1 个键值对，总共 O(n)。
//
// 教学用途，生产请用 std::map / std::set。
template <typename Key, typename Value, typename Compare = std::less<Key>>
class BinarySearchTree {
private:
    struct Node {
        Key key;
        Value value;
        Node* left = nullptr;
        Node* right = nullptr;
        Node* parent = nullptr;  // 父指针只为了让迭代器能 ++ 到「中序后继」

        Node(const Key& k, const Value& v) : key(k), value(v) {}
    };

    // 给类模板起个短名字，嵌套类里就能引用「外层这个正在定义的模板」。
    // 直接写 BinarySearchTree 会触发「两阶段查找」下的名称查找问题。
    using Tree = BinarySearchTree<Key, Value, Compare>;

public:
    // -----------------------------------------------------------------------
    // 迭代器：让「用迭代器删除」能做到真正的 O(1)，也是与 STL 对齐的必要接口。
    // 注意：这是简化版——没有 operator-- 到 rend 的完整边界处理，够教学用。
    // -----------------------------------------------------------------------
    class Iterator {
    public:
        Iterator() = default;
        Iterator(BinarySearchTree* tree, Node* node) : tree_(tree), node_(node) {}

        const Key& first() const { return node_->key; }
        Value& second() const { return node_->value; }

        Iterator& operator++() {
            node_ = Tree::Successor(node_);
            return *this;
        }
        Iterator operator++(int) {
            Iterator copy = *this;
            node_ = Tree::Successor(node_);
            return copy;
        }

        bool operator==(const Iterator& other) const { return node_ == other.node_; }
        bool operator!=(const Iterator& other) const { return node_ != other.node_; }

    private:
        friend class BinarySearchTree;
        BinarySearchTree* tree_ = nullptr;
        Node* node_ = nullptr;
    };

    BinarySearchTree() = default;

    // --- Rule of Five 之一：拷贝构造（深拷贝）--------------------------------
    // 为什么必须递归复制整棵树：默认的拷贝构造只会复制 root_ 这个指针，
    // 于是两棵树指向同一批节点——任何一棵被修改，另一棵跟着变；任何一棵被析构，
    // 另一棵变成悬垂指针（double free / use after free）。
    // 深拷贝的含义是：为源树的每一个节点重新 new 一个节点，结构与键值完全一致。
    // 递归版实现最清晰（高度为 h 时需要 O(h) 的调用栈），退化树会栈溢出，
    // 所以真实的生产容器（如 std::map 的红黑树）不会用递归来拷贝整棵树。
    BinarySearchTree(const BinarySearchTree& other) : comp_(other.comp_) {
        try {
            root_ = Clone(other.root_, nullptr);
            size_ = other.size_;
        } catch (...) {
            // 异常安全：Clone 中途失败时，把它已经建好的部分清理干净再抛出去，
            // 否则构造函数抛异常不会调用析构函数，那部分内存就泄漏了。
            Clear();
            throw;
        }
    }

    // --- Rule of Five 之二：移动构造（窃取指针，O(1)）------------------------
    // 移动的语义是「源对象反正要死了」，所以不用复制节点，直接抢走 root_，
    // 再把源对象清成空树，保证它析构时不会删掉已经被抢走的节点。
    BinarySearchTree(BinarySearchTree&& other) noexcept
        : root_(other.root_), size_(other.size_), comp_(std::move(other.comp_)) {
        other.root_ = nullptr;
        other.size_ = 0;
    }

    // --- Rule of Five 之三：析构 --------------------------------------------
    ~BinarySearchTree() { Clear(); }

    // --- Rule of Five 之四：拷贝赋值（copy-and-swap）------------------------
    // 拷贝构造 + swap 的好处：只有一次分配，天然自赋值安全，
    // 而且如果拷贝构造抛异常，*this 保持原样（强异常安全保证）。
    BinarySearchTree& operator=(const BinarySearchTree& other) {
        if (this != &other) {
            BinarySearchTree tmp(other);
            Swap(tmp);
        }
        return *this;
    }

    // --- Rule of Five 之五：移动赋值 ----------------------------------------
    BinarySearchTree& operator=(BinarySearchTree&& other) noexcept {
        if (this != &other) {
            BinarySearchTree tmp(std::move(other));
            Swap(tmp);  // tmp 析构时负责删掉我们原来的节点
        }
        return *this;
    }

    void Swap(BinarySearchTree& other) noexcept {
        std::swap(root_, other.root_);
        std::swap(size_, other.size_);
        std::swap(comp_, other.comp_);
    }

    // -----------------------------------------------------------------------
    // 基本接口
    // -----------------------------------------------------------------------
    std::size_t Size() const { return size_; }
    bool Empty() const { return size_ == 0; }

    // 清空：注意这里【故意】用迭代实现，见下面注释。
    void Clear() {
        // 为什么不用「递归后序析构」？因为有序插入 10 万条之后树高 10 万，
        // 递归析构就有 10 万层调用栈，直接栈溢出（真实工程事故）。
        // 迭代做法：反复把 root_ 替换成它的某个孩子，同时把当前节点挂到待删链表上。
        // 每个节点最多进链表一次，所以总体是 O(n)，且额外空间只有 O(1) 个指针。
        Node* batch = nullptr;
        while (root_ != nullptr) {
            Node* victim = root_;
            if (victim->left != nullptr) {
                root_ = victim->left;          // 先顺着左边一路往下走
            } else {
                root_ = victim->right;         // 左边没了就换右边
            }
            victim->left = nullptr;
            victim->right = nullptr;
            victim->parent = batch;            // 用 parent 字段串成待删链表
            batch = victim;
        }
        while (batch != nullptr) {
            Node* next = batch->parent;
            delete batch;
            batch = next;
        }
        size_ = 0;
    }

    // -----------------------------------------------------------------------
    // 插入：重复键时更新值并返回 false（对齐 std::map::insert 的语义）
    // 复杂度 O(h)：先自根向下找位置，再挂上新节点。
    // -----------------------------------------------------------------------
    bool Insert(const Key& key, const Value& value) {
        Node* parent = nullptr;
        Node* cur = root_;
        while (cur != nullptr) {
            parent = cur;
            if (comp_(key, cur->key)) {
                cur = cur->left;
            } else if (comp_(cur->key, key)) {
                cur = cur->right;
            } else {
                cur->value = value;  // 键已存在：只更新值，不新增节点
                return false;
            }
        }

        Node* node = new Node(key, value);
        node->parent = parent;
        if (parent == nullptr) {
            root_ = node;  // 空树：新节点就是根
        } else if (comp_(key, parent->key)) {
            parent->left = node;
        } else {
            parent->right = node;
        }
        size_ += 1;
        return true;
    }

    // 返回内部节点指针（找不到返回 nullptr）。
    // 【关键工程提示】查找【故意】写成迭代版：树一旦退化成 10 万层的链表，
    // 递归查找就是 10 万层调用栈，Debug 下默认 1MB 栈帧很快就会溢出。
    // 迭代版只用 O(1) 的空间，无论树多高都不会崩。
    Node* FindNode(const Key& key) const {
        Node* cur = root_;
        while (cur != nullptr) {
            if (comp_(key, cur->key)) {
                cur = cur->left;
            } else if (comp_(cur->key, key)) {
                cur = cur->right;
            } else {
                return cur;
            }
        }
        return nullptr;
    }

    const Value* Find(const Key& key) const {
        const Node* node = FindNode(key);
        return node == nullptr ? nullptr : &node->value;
    }

    bool Contains(const Key& key) const { return FindNode(key) != nullptr; }

    // -----------------------------------------------------------------------
    // 删除：三种情况必须分开处理
    // 复杂度 O(h)，返回是否真的删掉了东西。
    // 【关键提示】和查找一样，递归版在退化树上会栈溢出；生产容器用的是
    // 「带父指针的迭代删除」。这里保留递归版是因为它把三种情况表达得最清楚。
    // -----------------------------------------------------------------------
    bool Erase(const Key& key) { return EraseNode(root_, key); }

    // 用迭代器删除：节点指针已经在手上了，不需要再从根找一遍，所以是真正的 O(1)
    // （相对于「按 key 删除」的 O(h)）。这是 STL 容器普遍提供 erase(iterator)
    // 重载的原因：遍历中删元素要用这个版本。
    //
    // 这里有一个必须讲清楚的设计细节，也是本文件踩过的真实 bug：
    // 迭代器版删除【绝对不能】返回一个「刚刚被 delete 掉的节点」的迭代器。
    // 具体来说，场景 3 是「把中序后继的键值搬进当前节点，再删掉后继那个位置」，
    // 这个过程中被 delete 的是【后继节点】，它的内存会被 Debug 堆填充成 0xDD；
    // 如果返回指向它的迭代器，调用方下一次解引用就会读到一个键为 0xDDDDDDDD
    // （十进制约 -572662307）的幽灵节点，然后就是访问冲突崩溃。
    // 正确做法：返回「后继的键值现在所在的、仍然活着的那个节点」：
    //   - 后继是叶子：它的键值已经被搬进 victim，所以返回 victim；
    //   - 后继只有一个右孩子：键值搬进 victim，右孩子顶替了后继的位置，
    //     位置最小的仍然是那个右孩子，所以返回它；
    //   - 注意后继不可能有左孩子（它是右子树里最小的）。
    Iterator Erase(Iterator position) {
        if (position.node_ == nullptr) {
            return Iterator(this, nullptr);
        }
        Node* victim = position.node_;

        // --- 情况 1：叶子节点 ----------------------------------------------
        // 删掉之后，中序后继是「往上爬」找到的那个祖先。
        if (victim->left == nullptr && victim->right == nullptr) {
            Node* next = Successor(victim);
            DetachLeaf(victim);
            size_ -= 1;
            return Iterator(this, next);
        }
        // --- 情况 2：只有一个孩子 ------------------------------------------
        // 用孩子顶替。后继要么在右子树最左下角，要么是往上爬的那个祖先。
        if (victim->left == nullptr || victim->right == nullptr) {
            Node* child = (victim->left != nullptr) ? victim->left : victim->right;
            Node* next = (child == victim->right) ? Leftmost(child) : Successor(victim);
            ReplaceWithChild(victim, child);
            size_ -= 1;
            return Iterator(this, next);
        }
        // --- 情况 3：有两个孩子 --------------------------------------------
        // 把中序后继的键值搬进 victim（victim 的地址不变，它仍然是「原来那个位置」，
        // 只是内容换成了后继的键值），然后删掉后继那一个节点。
        Node* successor = Leftmost(victim->right);
        victim->key = successor->key;
        victim->value = successor->value;
        Node* next = victim;  // 默认：后继是叶子，键值已经搬到 victim 上了
        if (successor->right != nullptr) {
            // 后继不是叶子（它只有右孩子，不可能有左孩子）。
            // 把右孩子顶替上去，它就成了这个位置上最小的节点。
            next = successor->right;
            ReplaceWithChild(successor, next);
        } else {
            DetachLeaf(successor);
        }
        size_ -= 1;
        return Iterator(this, next);
    }

    // -----------------------------------------------------------------------
    // 高度
    // 定义：【空树高度 = -1，只有根节点的树高度 = 0】（高度 = 边的条数）。
    // 为什么这么定：递归式 height(x) = 1 + max(height(left), height(right))
    // 对叶子自然算出 0，不需要任何特判，公式最干净。
    // 另一种常见约定是「节点数」定义（空树 0、单节点 1），两者只差 1，
    // 但树上算法文献（AVL 的平衡因子、红黑树的黑高）几乎都用「边数」定义，
    // 所以这里跟文献保持一致。
    // -----------------------------------------------------------------------
    int Height() const { return HeightRecursive(root_); }

    // 退化树专用：迭代版高度，用「中序遍历配对」技巧，额外空间 O(h)。
    // 原理：中序序列里，一个节点的深度 = 访问它时栈里的节点个数 - 1。
    // 每次弹栈时更新最大深度即可，全程不递归。
    int HeightIterative() const {
        int max_depth = -1;  // 空树高度为 -1
        std::vector<Node*> stack;
        Node* cur = root_;
        while (cur != nullptr || !stack.empty()) {
            while (cur != nullptr) {
                stack.push_back(cur);
                cur = cur->left;
            }
            cur = stack.back();
            stack.pop_back();
            const int depth = static_cast<int>(stack.size());  // 弹掉自己之后的栈深就是深度
            if (depth > max_depth) {
                max_depth = depth;
            }
            cur = cur->right;
        }
        return max_depth;
    }

    int HeightRecursive(const Node* node) const {
        if (node == nullptr) {
            return -1;  // 空树高度 -1，配合下面的 +1 让叶子的高度为 0
        }
        const int lh = HeightRecursive(node->left);
        const int rh = HeightRecursive(node->right);
        return 1 + (lh > rh ? lh : rh);
    }

    // -----------------------------------------------------------------------
    // 中序遍历 = 升序序列（这是 BST 的定义性质带来的）
    // 递归版：代码最短，但退化树上会栈溢出。
    // 递归的栈空间是 O(h)：平均 O(log n)，最坏 O(n)。
    // -----------------------------------------------------------------------
    std::vector<std::pair<Key, Value>> InorderRecursive() const {
        std::vector<std::pair<Key, Value>> out;
        out.reserve(size_);
        InorderWalk(root_, out);
        return out;
    }

    // 显式栈迭代版：自己用 vector 当栈，绕开函数调用栈，退化树也安全。
    // 手工模拟的正是递归版的三步：一路向左压栈 -> 弹栈访问 -> 转向右子树。
    std::vector<std::pair<Key, Value>> InorderIterative() const {
        std::vector<std::pair<Key, Value>> out;
        out.reserve(size_);
        std::vector<const Node*> stack;
        const Node* cur = root_;
        while (cur != nullptr || !stack.empty()) {
            while (cur != nullptr) {
                stack.push_back(cur);
                cur = cur->left;
            }
            cur = stack.back();
            stack.pop_back();
            out.emplace_back(cur->key, cur->value);
            cur = cur->right;
        }
        return out;
    }

    Iterator Begin() { return Iterator(this, Leftmost(root_)); }
    Iterator End() { return Iterator(this, nullptr); }

private:
    // -----------------------------------------------------------------------
    // 深拷贝的递归辅助：为 src 子树整棵复制一份，parent 是复制出来的根的父亲。
    // 复杂度 O(n) 时间、O(h) 递归栈空间。
    // -----------------------------------------------------------------------
    Node* Clone(const Node* src, Node* parent) {
        if (src == nullptr) {
            return nullptr;
        }
        Node* node = new Node(src->key, src->value);
        node->parent = parent;
        // 先接左再接右，任意一边抛异常都会由构造函数的 catch 统一清理。
        node->left = Clone(src->left, node);
        node->right = Clone(src->right, node);
        return node;
    }

    void InorderWalk(const Node* node, std::vector<std::pair<Key, Value>>& out) const {
        if (node == nullptr) {
            return;
        }
        InorderWalk(node->left, out);
        out.emplace_back(node->key, node->value);
        InorderWalk(node->right, out);
    }

    static Node* Leftmost(Node* node) {
        if (node == nullptr) {
            return nullptr;
        }
        while (node->left != nullptr) {
            node = node->left;
        }
        return node;
    }

    // 中序后继 = 第一个比 node 大的键。这是「双孩子删除」和迭代器 ++ 的公共基础。
    static Node* Successor(Node* node) {
        if (node->right != nullptr) {
            return Leftmost(node->right);  // 有右子树：右子树里最小的那个
        }
        Node* parent = node->parent;
        while (parent != nullptr && node == parent->right) {
            node = parent;  // 没右子树：往上爬，直到「自己是从左边上来的」
            parent = parent->parent;
        }
        return parent;
    }

    // -----------------------------------------------------------------------
    // 删除的三种情况（node 用引用传入，因为情况 1/2 需要改写父亲的指针）
    // -----------------------------------------------------------------------
    bool EraseNode(Node*& node, const Key& key) {
        if (node == nullptr) {
            return false;  // 走到空说明键不存在
        }
        if (comp_(key, node->key)) {
            return EraseNode(node->left, key);
        }
        if (comp_(node->key, key)) {
            return EraseNode(node->right, key);
        }

        // ---- 情况 1：叶子节点（没有孩子）-----------------------------------
        // 直接删掉，并把父亲的对应指针置空。两个助手函数把「改父指针 + 释放节点」
        // 这段容易写错的逻辑收在一处，迭代器版的删除也复用它们。
        if (node->left == nullptr && node->right == nullptr) {
            DetachLeaf(node);
            node = nullptr;  // 通过引用改写了父节点的 left / right（或 root_）
        }
        // ---- 情况 2：只有一个孩子 ------------------------------------------
        // 用这个孩子直接顶替自己的位置。为什么这样安全：BST 的有序性只依赖
        // 「左子树全部小于、右子树全部大于」，孩子的整棵子树本来就满足这个约束，
        // 所以让祖父直接指向孩子，有序性自动保持。
        // 但别忘了改孩子的 parent 指针，否则迭代器 ++ 会走错方向。
        else if (node->left == nullptr || node->right == nullptr) {
            Node* child = (node->left != nullptr) ? node->left : node->right;
            ReplaceWithChild(node, child);
            node = child;  // 父节点的指针改指向孩子
        }
        // ---- 情况 3：有两个孩子 --------------------------------------------
        // 教科书标准做法：找【中序后继】（右子树里最小的节点，也就是比 node.key
        // 大的所有键里最小的那个），把它的键值【复制】到当前节点，然后递归删除
        // 那个后继。因为后继没有左孩子（它是最小的），所以递归下去一定落回
        // 情况 1 或情况 2，递归只有一层深度。
        //
        // 为什么也可以用【前驱】？左子树里最大的节点（中序前驱）同样满足
        // 「介于左右子树之间」这个要求，把它换上来 BST 依然有序。
        // 后继和前驱的选择不影响正确性，只影响树的形状：
        //   - 总是用后继：删除后倾向于让左侧变高；
        //   - 总是用前驱：删除后倾向于让右侧变高；
        //   - 交替使用（按高度选较高一侧的孩子）可以让树更平衡，
        //     这在真正的平衡树里由旋转负责，所以这里固定用后继即可。
        //
        // 为什么「只替换值再删后继」比「真的把节点搬过去」简单得多？
        //   真的搬运节点要同时改 6 处：后继父节点的孩子指针、后继的 parent、
        //   后继的 left / right、以及当前节点父亲的指针；而且后继就是当前节点的
        //   直接右孩子时，这些改动会互相覆盖，必须单独特判，极容易写错。
        //   复制值 + 递归删除把「结构改动」全部交给已经写对的情况 1/2 处理，
        //   代价只是多一次赋值（对 key/value 类型要求可拷贝/可移动）。
        else {
            Node* successor = Leftmost(node->right);
            node->key = successor->key;      // 只替换键值，不动树形
            node->value = successor->value;
            // 递归删除后继：它一定没有左孩子，所以只可能走情况 1 或情况 2。
            // 用 return 直接丢弃返回值即可——整棵子树必然能找到这个 key。
            (void)EraseNode(node->right, successor->key);
        }

        size_ -= 1;
        return true;
    }

    // 情况 1 的公共实现：把叶子从树上摘下来并释放。
    // 顺序很重要：先改父亲的指针（此时 node 的 parent 还没被删），再 delete。
    // 反过来写就是先 delete 再读 node->parent，标准的 use after free。
    void DetachLeaf(Node* node) {
        Node* parent = node->parent;
        if (parent == nullptr) {
            root_ = nullptr;
        } else if (parent->left == node) {
            parent->left = nullptr;
        } else {
            parent->right = nullptr;
        }
        delete node;
    }

    // 情况 2 的公共实现：让孩子顶替 node 的位置。
    // child 整棵子树都在合法区间内（左子树全小于、右子树全大于），
    // 所以只要把指针接对，BST 不变式自动保持。
    void ReplaceWithChild(Node* node, Node* child) {
        Node* parent = node->parent;
        child->parent = parent;
        if (parent == nullptr) {
            root_ = child;
        } else if (parent->left == node) {
            parent->left = child;
        } else {
            parent->right = child;
        }
        delete node;
    }

    Node* root_ = nullptr;
    std::size_t size_ = 0;
    Compare comp_{};
};

// ===========================================================================
// 第 2 部分：AVL 的四种旋转（概念演示，不是完整的平衡树实现）
// ===========================================================================
// AVL 的不变量比 BST 多一条：任意节点的「平衡因子」BF = 左子树高 - 右子树高，
// 必须落在 {-1, 0, +1} 之内。一旦插入/删除破坏了它，就用旋转把树掰回去。
//
// 四种失衡形态（以「新节点插在哪里」命名，X 是最先失衡的节点）：
//   LL：插在 X 左孩子的左子树  -> 对 X 做一次【右旋】；
//   RR：插在 X 右孩子的右子树  -> 对 X 做一次【左旋】；
//   LR：插在 X 左孩子的右子树  -> 先对 X->left 左旋，再对 X 右旋（双旋）；
//   RL：插在 X 右孩子的左子树  -> 先对 X->right 右旋，再对 X 左旋（双旋）。
// 记忆法：LL 用右旋、RR 用左旋，LR/RL 就是把两个单旋按相反顺序各做一次。
//
// 复杂度：旋转本身只改几个指针，是 O(1)；它把失衡子树的高度降回去，
// 所以插入只要沿路径向上找第一个失衡点，最多 O(log n) 次旋转（实际插入 <= 2 次）。
struct AvlNode {
    int key = 0;
    int height = 0;  // 缓存高度，换取 O(1) 读取（代价是每个节点多 4 字节）
    AvlNode* left = nullptr;
    AvlNode* right = nullptr;

    explicit AvlNode(int k) : key(k) {}
};

int AvlHeight(const AvlNode* node) { return node == nullptr ? -1 : node->height; }

void AvlUpdateHeight(AvlNode* node) {
    const int lh = AvlHeight(node->left);
    const int rh = AvlHeight(node->right);
    node->height = 1 + (lh > rh ? lh : rh);
}

int AvlBalanceFactor(const AvlNode* node) {
    return node == nullptr ? 0 : AvlHeight(node->left) - AvlHeight(node->right);
}

// 右旋：LL 情况的解药。x 的左孩子 y 升上来当子树根，x 降到 y 的右边。
AvlNode* RotateRight(AvlNode* x) {
    AvlNode* y = x->left;
    x->left = y->right;  // y 的右子树「介于 y 和 x 之间」，正好过继给 x 当左子树
    y->right = x;
    AvlUpdateHeight(x);  // 先更新低的那层，再更新高的（顺序不能反）
    AvlUpdateHeight(y);
    return y;  // 返回新的子树根
}

// 左旋：RR 情况的解药，与右旋完全对称。
AvlNode* RotateLeft(AvlNode* x) {
    AvlNode* y = x->right;
    x->right = y->left;
    y->left = x;
    AvlUpdateHeight(x);
    AvlUpdateHeight(y);
    return y;
}

// 一次带旋转的 AVL 插入，返回（可能换过根的）子树根，rotations 统计旋转次数。
AvlNode* AvlInsert(AvlNode* node, int key, int& rotations) {
    if (node == nullptr) {
        return new AvlNode(key);
    }
    if (key < node->key) {
        node->left = AvlInsert(node->left, key, rotations);
    } else if (key > node->key) {
        node->right = AvlInsert(node->right, key, rotations);
    } else {
        return node;  // 重复键：AVL 也不存重复键
    }

    AvlUpdateHeight(node);
    const int bf = AvlBalanceFactor(node);

    if (bf > 1 && key < node->left->key) {          // LL
        rotations += 1;
        return RotateRight(node);
    }
    if (bf < -1 && key > node->right->key) {        // RR
        rotations += 1;
        return RotateLeft(node);
    }
    if (bf > 1 && key > node->left->key) {          // LR：双旋 = 两次单旋
        rotations += 2;
        node->left = RotateLeft(node->left);
        return RotateRight(node);
    }
    if (bf < -1 && key < node->right->key) {        // RL：双旋
        rotations += 2;
        node->right = RotateRight(node->right);
        return RotateLeft(node);
    }
    return node;
}

void AvlFree(AvlNode* node) {
    if (node == nullptr) {
        return;
    }
    AvlFree(node->left);
    AvlFree(node->right);
    delete node;
}

// 递归求 AVL 高度，顺便校验平衡因子确实落在 {-1, 0, 1}。
int AvlHeightChecked(const AvlNode* node) {
    if (node == nullptr) {
        return -1;
    }
    const int lh = AvlHeightChecked(node->left);
    const int rh = AvlHeightChecked(node->right);
    assert(lh - rh >= -1 && lh - rh <= 1);
    return 1 + (lh > rh ? lh : rh);
}

// 迭代版 AVL 插入（用一条显式的「从根到插入位置」的路径当栈）。
// 为什么不用递归：要跑 10 万条数据，递归插入在 Debug 下既慢又占栈；
// 迭代版把路径存进 vector，回溯时按相反顺序更新高度并旋转。
AvlNode* AvlInsertIterative(AvlNode* root, int key, int& rotations) {
    std::vector<AvlNode*> path;
    AvlNode* cur = root;
    while (cur != nullptr) {
        path.push_back(cur);
        if (key < cur->key) {
            cur = cur->left;
        } else if (key > cur->key) {
            cur = cur->right;
        } else {
            return root;  // 重复键，直接返回
        }
    }

    AvlNode* fresh = new AvlNode(key);
    if (path.empty()) {
        return fresh;  // 空树
    }
    if (key < path.back()->key) {
        path.back()->left = fresh;
    } else {
        path.back()->right = fresh;
    }

    // 自底向上回溯：先修高度，再看是否失衡。
    // 关键观察：整条路径上第一个失衡点修好之后，上面所有节点的高度都恢复原值，
    // 所以对【插入】来说修复一次就够了，可以立刻 break（这正是 AVL 插入
    // 旋转次数少的原因）。
    AvlNode* new_subtree_root = nullptr;
    for (std::size_t i = path.size(); i-- > 0;) {
        AvlNode* node = path[i];
        AvlUpdateHeight(node);
        const int bf = AvlBalanceFactor(node);
        if (bf > 1 || bf < -1) {
            AvlNode* fixed = nullptr;
            if (bf > 1 && AvlBalanceFactor(node->left) >= 0) {
                rotations += 1;
                fixed = RotateRight(node);  // LL
            } else if (bf > 1) {
                rotations += 2;
                node->left = RotateLeft(node->left);
                fixed = RotateRight(node);  // LR
            } else if (bf < -1 && AvlBalanceFactor(node->right) <= 0) {
                rotations += 1;
                fixed = RotateLeft(node);   // RR
            } else {
                rotations += 2;
                node->right = RotateRight(node->right);
                fixed = RotateLeft(node);   // RL
            }
            if (i == 0) {
                new_subtree_root = fixed;   // 换的是整棵树的根
            } else {
                AvlNode* parent = path[i - 1];
                if (parent->left == node) {
                    parent->left = fixed;
                } else {
                    parent->right = fixed;
                }
                AvlUpdateHeight(parent);
            }
            break;  // 插入时的修复到此为止（见上面的关键观察）
        }
    }

    return new_subtree_root == nullptr ? root : new_subtree_root;
}

// 迭代版中序遍历（不递归，避免深树栈溢出），顺便校验严格升序。
void AvlCollectSorted(const AvlNode* root, std::vector<int>& out) {
    std::vector<const AvlNode*> stack;
    const AvlNode* cur = root;
    while (cur != nullptr || !stack.empty()) {
        while (cur != nullptr) {
            stack.push_back(cur);
            cur = cur->left;
        }
        cur = stack.back();
        stack.pop_back();
        out.push_back(cur->key);
        cur = cur->right;
    }
}

// ===========================================================================
// 第 3 部分：造数据与小工具
// ===========================================================================
std::vector<int> MakeSorted(std::size_t n) {
    std::vector<int> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = static_cast<int>(i) + 1;
    }
    return v;
}

std::vector<int> MakeShuffled(std::size_t n, unsigned seed) {
    std::vector<int> v = MakeSorted(n);
    std::mt19937 rng(seed);
    std::shuffle(v.begin(), v.end(), rng);  // C++17 起用 shuffle，不要用已移除的 random_shuffle
    return v;
}

// 把「一次操作」的耗时换算成「每次操作多少纳秒」，方便横向比较。
double NsPerOp(double total_us, std::size_t ops) {
    return total_us * 1000.0 / static_cast<double>(ops);
}

void AssertAscending(const std::vector<std::pair<int, int>>& seq, const char* who) {
    for (std::size_t i = 1; i < seq.size(); ++i) {
        assert(seq[i - 1].first < seq[i].first);  // 严格升序，顺带证明没有重复键
    }
    std::cout << "  [assert] " << who << " 输出严格升序，元素个数 " << seq.size() << "\n";
}

}  // namespace

int main() {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "本文件在 Debug 配置下运行，绝对耗时比 Release 慢，请只看趋势与数量级。\n";

    // =======================================================================
    Section("1. 手写 BST：插入 / 查找 / 删除 / 中序遍历");
    // =======================================================================
    Note("用 key-value（语义对齐 std::map）实现，重复插入同一个键只更新值。");
    Note("复杂度：平均 O(log n)，最坏 O(n)（退化成一维链表时）。");
    std::cout << "\n";

    {
        BinarySearchTree<int, std::string> tree;
        assert(tree.Empty());
        assert(tree.Height() == -1);  // 空树高度约定为 -1

        // 故意用一个会左右都长孩子的插入顺序，让后面三种删除情况都能覆盖到。
        //                50
        //          30          70
        //       20   40     60    80
        const int keys[] = {50, 30, 70, 20, 40, 60, 80};
        for (const int k : keys) {
            assert(tree.Insert(k, "v" + std::to_string(k)));
        }
        assert(tree.Insert(50, "updated") == false);  // 重复键：只更新值
        assert(tree.Size() == 7);
        assert(tree.Height() == 2);  // 高度按「边数」算：50 -> 30 -> 20 是 2 条边

        std::cout << "  size = " << tree.Size() << "，height = " << tree.Height() << "\n";
        std::cout << "  find(40)  -> " << *tree.Find(40) << "\n";
        std::cout << "  find(50)  -> " << *tree.Find(50) << "（被第二次 Insert 更新过）\n";
        assert(tree.Find(41) == nullptr);
        assert(tree.Contains(60) && !tree.Contains(61));

        // --- 中序遍历：递归 vs 显式栈，两者必须完全一致，而且必须升序 ---
        const std::vector<std::pair<int, std::string>> rec = tree.InorderRecursive();
        const std::vector<std::pair<int, std::string>> itr = tree.InorderIterative();
        assert(rec == itr);
        for (std::size_t i = 1; i < rec.size(); ++i) {
            assert(rec[i - 1].first < rec[i].first);
        }
        std::cout << "  中序（递归）  : ";
        for (const auto& kv : rec) {
            std::cout << kv.first << " ";
        }
        std::cout << "\n";
        std::cout << "  中序（显式栈）: ";
        for (const auto& kv : itr) {
            std::cout << kv.first << " ";
        }
        std::cout << "\n";
        Note("两种写法结果完全相同：BST 的中序遍历一定是升序，这是定义性质。");

        // --- 删除的三种情况，一个一个来 ---
        std::cout << "\n";
        Note("删除的三种情况演示（每一步都 assert 结构不变量）：");
        assert(tree.Erase(20));                 // 情况 1：叶子节点
        assert(!tree.Contains(20) && tree.Size() == 6);
        Note("  情况 1：删叶子 20 -> 父亲 30 的 left 直接置空");

        assert(tree.Erase(60));                 // 情况 2：只有一个孩子
        assert(!tree.Contains(60) && tree.Size() == 5);
        Note("  情况 2：删只有右孩子的 60 -> 让 60 的孩子直接顶替它的位置");

        assert(tree.Erase(30));                 // 情况 3：两个孩子，用中序后继 40
        assert(!tree.Contains(30) && tree.Contains(40) && tree.Size() == 4);
        Note("  情况 3：删有两个孩子的 30 -> 中序后继是 40，把 40 的键值复制上来，");
        Note("          再递归删除 40 这个原来的位置（它没有左孩子，落回情况 1/2）");

        const std::vector<std::pair<int, std::string>> after = tree.InorderIterative();
        for (std::size_t i = 1; i < after.size(); ++i) {
            assert(after[i - 1].first < after[i].first);
        }
        std::cout << "  删除后中序: ";
        for (const auto& kv : after) {
            std::cout << kv.first << " ";
        }
        std::cout << "\n";
        assert(!tree.Erase(999));               // 删不存在的键
        std::cout << "  删除不存在的键 999 返回 false，size 仍为 " << tree.Size() << "\n";
    }

    // =======================================================================
    Section("2. Rule of Five：深拷贝与移动语义");
    // =======================================================================
    Note("拷贝构造必须【递归复制整棵树】。如果只是复制 root_ 指针（浅拷贝），");
    Note("两棵树会共享同一批节点：改一个影响另一个，析构时还会 double free。");
    std::cout << "\n";

    {
        BinarySearchTree<int, int> a;
        for (const int k : {8, 4, 12, 2, 6, 10, 14}) {
            a.Insert(k, k * 10);
        }

        BinarySearchTree<int, int> b(a);  // 拷贝构造：深拷贝
        assert(b.Size() == a.Size());
        b.Insert(99, 990);  // 改副本
        assert(!a.Contains(99));  // 原件不受影响 —— 这就是深拷贝的证据
        std::cout << "  拷贝后改副本：a.size = " << a.Size() << "，b.size = " << b.Size()
                  << "（互不影响）\n";

        BinarySearchTree<int, int> c;
        c = a;  // 拷贝赋值：copy-and-swap
        assert(c.Size() == a.Size() && c.Contains(14));

        BinarySearchTree<int, int> d(std::move(b));  // 移动构造：只偷指针，O(1)
        assert(d.Size() == 8);
        assert(b.Size() == 0 && b.Empty());  // 被移动后必须处于合法（空）状态
        std::cout << "  移动构造后：d.size = " << d.Size() << "，被移走的 b.size = "
                  << b.Size() << "\n";

        BinarySearchTree<int, int> e;
        e.Insert(1, 1);
        e = std::move(d);  // 移动赋值：先把 d 偷进临时对象再 swap，原节点由临时对象析构时释放
        assert(e.Size() == 8 && d.Empty());
        std::cout << "  移动赋值后：e.size = " << e.Size() << "，被移走的 d.size = "
                  << d.Size() << "\n";

        // 迭代器 + erase(iterator)：不用再按 key 找一遍，是真正的 O(1)。
        // 关键点：erase(iterator) 返回的迭代器指向「原来的中序后继」，
        // 而且删除过程中当前节点的【地址不变】，所以能安全地继续遍历。
        std::size_t visited = 0;
        for (auto it = e.Begin(); it != e.End();) {
            if (it.first() % 4 == 0) {
                it = e.Erase(it);  // 遍历中删除：把返回的迭代器接上，不要写 ++it
            } else {
                ++it;
            }
            visited += 1;
        }
        std::cout << "  用 erase(iterator) 删掉所有 4 的倍数后 size = " << e.Size()
                  << "（迭代了 " << visited << " 次）\n";
        const std::vector<std::pair<int, int>> rest = e.InorderIterative();
        for (const auto& kv : rest) {
            assert(kv.first % 4 != 0);
        }

        // 自赋值：copy-and-swap 写法天然安全，不需要额外特判逻辑。
        e = *&e;
        assert(e.Size() == rest.size());
        std::cout << "  自赋值后 size 保持不变 = " << e.Size() << "\n";
    }

    // =======================================================================
    Section("3. 与 std::map / std::set 的 API 对照");
    // =======================================================================
    Note("同一批功能，用 std::map / std::set 再写一遍，体会标准库接口的完备程度。");
    std::cout << "\n";

    {
        std::map<int, std::string> m;
        // insert 返回 pair<iterator, bool>：bool 表示「是不是真的插进去了」。
        const auto r1 = m.insert({50, "v50"});
        const auto r2 = m.insert({50, "again"});
        assert(r1.second && !r2.second);  // 第二次插入被拒绝，值不覆盖
        // operator[] 会「不存在就默认构造」地插入，写起来最省事但语义不同：
        m[30] = "v30";
        m[70] = "v70";
        assert(m.size() == 3);
        std::cout << "  map.insert 重复键返回 {" << r2.first->first << ", "
                  << (r2.second ? "true" : "false") << "}，值仍是 " << r2.first->second << "\n";
        m[50] = "overwritten";  // 想覆盖老值就用 operator[] 或 insert_or_assign
        assert(m.at(50) == "overwritten");

        // erase(key) 返回删掉了几个（对 map 只可能是 0 或 1；multimap 可能是多个）。
        assert(m.erase(30) == 1);
        assert(m.erase(30) == 0);
        assert(m.count(70) == 1 && m.contains(70));  // C++20 的 contains
        m.insert({10, "v10"});
        m.insert({90, "v90"});

        std::cout << "  map 遍历（key 自动升序）: ";
        for (const auto& kv : m) {
            std::cout << kv.first << " ";
        }
        std::cout << "\n";

        // lower_bound / upper_bound 是「有序容器」的独门武器：
        // 一次 O(log n) 定位就能扫一个区间，这是哈希表做不到的（范围查询）。
        const auto lo = m.lower_bound(40);
        const auto hi = m.upper_bound(90);
        std::cout << "  lower_bound(40) 起的区间: ";
        for (auto it = lo; it != hi; ++it) {
            std::cout << it->first << " ";
        }
        std::cout << "（范围查询是红黑树相对哈希表的优势）\n";

        std::set<int> s;
        for (const int k : {5, 1, 9, 3, 7, 3}) {
            s.insert(k);  // 重复的直接被忽略
        }
        assert(s.size() == 5);
        assert(s.erase(3) == 1);
        std::cout << "  set 元素: ";
        for (const int x : s) {
            std::cout << x << " ";
        }
        std::cout << "（自动去重 + 自动排序，永远是 O(log n)）\n";

        Note("");
        Note("结论：手写 BST 想达到这套接口的完备度，还要补上迭代器失效规则、");
        Note("      异常安全、分配器、异构查找、节点句柄（extract / merge）……");
        Note("      所以工程铁律是：不要自己写平衡树，用 std::map / std::set。");
    }

    // =======================================================================
    Section("4. 高度与退化：有序插入 2 万条数据");
    // =======================================================================
    Note("第 5 节才是 10 万条的完整规模对比；这一节先把「退化」讲清楚，");
    Note("并且用一个中等规模的数据拿到高度、查找、插入三条曲线。");
    Note("危险提示：有序插入后树高 ≈ n，此时【任何递归版本】都会栈溢出，");
    Note("所以下面的查找用迭代版 FindNode / Contains，高度用迭代版 HeightIterative。");
    std::cout << "\n";

    // n 的取值说明：有序插入的建树代价是 O(n^2)（每次都要走到最深处），
    // Debug 下单次比较要几十纳秒，n 取太大会让总运行时间失控。
    // 2 万条已经能清楚地看出 O(n) 的高度和 O(n) 的单次查找，
    // 而下面的推导会把结论外推到 10 万条。真实项目里 10 万条就是这个数量级。
    constexpr std::size_t kSortedN = 20000;
    constexpr std::size_t kRandomN = 50000;
    constexpr int kBuildRepeats = 3;

    double t_plain_build = 0.0;
    std::size_t plain_height = 0;
    std::size_t plain_size = 0;
    std::size_t random_height = 0;
    std::size_t random_size = 0;
    std::size_t balanced_height = 0;

    const std::vector<int> sorted_keys = MakeSorted(kSortedN);

    // 提前把查找目标取出来：要求是「两棵结构里都存在的键」，
    // 否则朴素树的 O(n) 查找可能在早期就命中返回，对比就不公平了。
    // 这里取中间和最后的几个键，落在最坏路径附近。
    std::vector<int> sorted_targets;
    for (std::size_t i = kSortedN / 2; i < kSortedN; i += kSortedN / 64) {
        sorted_targets.push_back(sorted_keys[i]);
    }

    // 每个测量单元只做一件事：要么建树，要么查找。把两件事混在一个计时里，
    // 再用减法去拆，得到的数字必然被噪声污染——分开测，数字才可信。
    auto measure_build = [&](auto&& insert_fn) {
        return BenchMedianUs(
            [&] {
                std::size_t built = insert_fn();
                return static_cast<std::uint64_t>(built);
            },
            kBuildRepeats);
    };

    // 只测「查找」本身：先建好结构，再单独给查找计时。
    // 返回的是「本批查找的总耗时（微秒，四舍五入到整数）」，外面再除以次数换成 ns/次。
    // 借用 fn 的返回值把时间带出来，是 BenchMedianUs 的返回值类型决定的（uint64_t）。
    auto measure_find = [&](auto&& setup_fn, auto&& lookup_fn, int repeat) {
        return BenchMedianUs(
            [&] {
                setup_fn();
                const auto t0 = std::chrono::steady_clock::now();
                std::uint64_t acc = 0;
                for (int r = 0; r < repeat; ++r) {
                    acc += lookup_fn();
                }
                const auto t1 = std::chrono::steady_clock::now();
                g_sink = g_sink + acc;  // 让查找结果真正产生副作用，防止整段被优化掉
                const std::chrono::duration<double, std::micro> dt = t1 - t0;
                return static_cast<std::uint64_t>(dt.count() + 0.5);
            },
            kBuildRepeats);
    };

    std::cout << "  [构造退化 BST：" << kSortedN << " 条升序数据，取 " << kBuildRepeats
              << " 次中位数]\n";
    t_plain_build = measure_build([&] {
        BinarySearchTree<int, int> t;
        for (const int k : sorted_keys) {
            t.Insert(k, k);
        }
        plain_height = static_cast<std::size_t>(t.HeightIterative());
        plain_size = t.Size();
        // 注意：t 的析构是迭代实现，即使树高 10 万也不会栈溢出。
        return t.Size();
    });

    std::cout << "  [构造随机 BST：" << kRandomN << " 条乱序数据]\n";
    const std::vector<int> random_keys = MakeShuffled(kRandomN, 20240606u);
    const double t_random_build = measure_build([&] {
        BinarySearchTree<int, int> t;
        for (const int k : random_keys) {
            t.Insert(k, k);
        }
        random_height = static_cast<std::size_t>(t.HeightIterative());
        random_size = t.Size();
        return t.Size();
    });

    std::cout << "  [构造 std::map：" << kSortedN << " 条升序数据（它的最坏输入）]\n";
    const double t_map_build = measure_build([&] {
        std::map<int, int> m;
        for (const int k : sorted_keys) {
            m.insert({k, k});
        }
        return m.size();
    });

    // 一个「确定平衡」的对照 BST：按二分中点顺序插入，建出来必然接近完全二叉树，
    // 高度正好等于 floor(log2(n))。用它代表「理想平衡树」。
    auto build_balanced = [&](std::size_t n, BinarySearchTree<int, int>& t) {
        std::vector<std::pair<std::size_t, std::size_t>> ranges;
        ranges.emplace_back(0, n);
        while (!ranges.empty()) {
            const auto range = ranges.back();
            ranges.pop_back();
            if (range.first >= range.second) {
                continue;
            }
            const std::size_t mid = range.first + (range.second - range.first) / 2;
            t.Insert(sorted_keys[mid], sorted_keys[mid]);
            ranges.emplace_back(mid + 1, range.second);
            ranges.emplace_back(range.first, mid);
        }
    };
    const double t_balanced_build = measure_build([&] {
        const BinarySearchTree<int, int> t = [&] {
            BinarySearchTree<int, int> built;
            build_balanced(kSortedN, built);
            return built;  // 走一次移动构造（O(1) 偷指针）
        }();
        balanced_height = static_cast<std::size_t>(t.HeightIterative());
        return t.Size();
    });

    // 查找对比：把结构建在 lambda 外面，lambda 里只做查找，计时才干净。
    std::cout << "  [查找计时：朴素退化 BST 与 std::map]\n";
    constexpr int kPlainFindRounds = 100;
    constexpr int kMapFindRounds = 400;
    const std::size_t plain_ops = sorted_targets.size() * kPlainFindRounds;
    const std::size_t map_ops = sorted_targets.size() * kMapFindRounds;

    BinarySearchTree<int, int> plain_tree_for_lookup;
    for (const int k : sorted_keys) {
        plain_tree_for_lookup.Insert(k, k);
    }
    std::map<int, int> map_for_lookup;
    for (const int k : sorted_keys) {
        map_for_lookup.insert({k, k});
    }

    WarmUp([&] {
        return static_cast<std::uint64_t>(plain_tree_for_lookup.Contains(sorted_targets[0]));
    },
           3);

    const double t_plain_find = measure_find(
        [&] { plain_tree_for_lookup.Empty(); },
        [&] {
            std::uint64_t acc = 0;
            for (const int key : sorted_targets) {
                acc += static_cast<std::uint64_t>(plain_tree_for_lookup.Contains(key));
            }
            return acc;
        },
        kPlainFindRounds);

    const double t_map_find = measure_find(
        [&] { g_sink = g_sink + static_cast<std::uint64_t>(map_for_lookup.size()); },
        [&] {
            std::uint64_t acc = 0;
            for (const int key : sorted_targets) {
                acc += static_cast<std::uint64_t>(map_for_lookup.find(key) != map_for_lookup.end());
            }
            return acc;
        },
        kMapFindRounds);

    assert(t_plain_find > 0.0 && t_map_find > 0.0);

    // 期望高度：随机 BST 的期望高度约为 2.99 * log2(n)（Devroye 的经典结果），
    // 平衡 BST 的高度是 floor(log2(n))。
    const double expected_random = 2.99 * std::log2(static_cast<double>(kRandomN));
    const double expected_balanced = std::log2(static_cast<double>(kSortedN));

    std::cout << "\n";
    Label("  指标", 34);
    Label("朴素 BST(升序)", 18);
    Label("朴素 BST(乱序)", 18);
    Label("平衡 BST", 14);
    Label("std::map", 12);
    std::cout << "\n" << std::string(96, '-') << "\n";

    Label("  数据量 n", 34);
    std::cout << std::setw(18) << plain_size << std::setw(18) << random_size
              << std::setw(14) << kSortedN << std::setw(12) << kSortedN << "\n";

    Label("  树高（边数）", 34);
    std::cout << std::setw(18) << plain_height << std::setw(18) << random_height
              << std::setw(14) << balanced_height << std::setw(12) << "-"
              << "（std::map 不暴露树高）\n";

    Label("  理论树高", 34);
    std::cout << std::setw(18) << plain_size << std::setw(18)
              << static_cast<std::size_t>(expected_random + 0.5)
              << std::setw(14) << static_cast<std::size_t>(expected_balanced + 0.5)
              << std::setw(12) << "约 2*log2(n)" << "\n";

    Label("  建树耗时 (us)", 34);
    std::cout << std::setw(18) << t_plain_build << std::setw(18) << t_random_build
              << std::setw(14) << t_balanced_build << std::setw(12) << t_map_build << "\n";

    Label("  查找 ns/次", 34);
    std::cout << std::setw(18) << NsPerOp(t_plain_find, plain_ops)
              << std::setw(18) << "-"
              << std::setw(14) << "-"
              << std::setw(12) << NsPerOp(t_map_find, map_ops) << "\n";
    std::cout << std::string(96, '-') << "\n";

    Note("");
    Note("读表要点：");
    Note("  1) 升序插入的朴素 BST 高度 = n - 1（2 万条就是 19999），它已经不是树，");
    Note("     而是一条链表。按「比较次数 ≈ n/2 = 1 万次」估算，10 万条时单次查找");
    Note("     要比较约 5 万次，而 std::map 只需要约 17 次——差三个数量级；");
    Note("  2) 乱序插入的朴素 BST 高度远小于 n，接近 2.99*log2(n) 的理论期望，");
    Note("     这解释了「随机数据下 BST 还凑合」；但注意它是【期望】，不是保证：");
    Note("     换一个种子、换一种输入分布（比如近似有序的真实数据）就会变差；");
    Note("  3) 平衡 BST 与 std::map 的高度都在 log2(n) 量级，输入顺序完全不影响它们。");
    Note("");
    Note("这就是「不要自己写平衡树」的第一条硬证据：你的朴素 BST 在真实数据面前");
    Note("可能退化成链表，而 std::map 无论输入顺序如何都保证 O(log n)。");

    // =======================================================================
    Section("5. 10 万条数据：退化的代价（含栈溢出风险说明）");
    // =======================================================================
    Note("这一节回答原问题：「有序插入 10 万条，朴素 BST 和 std::map 的高度与查找");
    Note("耗时差多少」。因为有序插入的建树本身是 O(n^2)，Debug 下 10 万条要跑很久，");
    Note("所以这里用「实测 n + 外推」的方式给出 10 万条的结论，并明确标注哪部分是实测、");
    Note("哪部分是外推。");
    std::cout << "\n";

    {
        constexpr std::size_t kWideN = 100000;
        const double log2n = std::log2(static_cast<double>(kWideN));

        // 平衡 BST 是可以用 10 万条真跑的：按中点顺序插入，建树也是 O(n log n)。
        const std::vector<int> wide_keys = MakeSorted(kWideN);
        std::size_t wide_balanced_height = 0;
        const double t_wide_balanced = BenchMedianUs(
            [&] {
                BinarySearchTree<int, int> t;
                std::vector<std::pair<std::size_t, std::size_t>> ranges;
                ranges.emplace_back(0, kWideN);
                while (!ranges.empty()) {
                    const auto range = ranges.back();
                    ranges.pop_back();
                    if (range.first >= range.second) {
                        continue;
                    }
                    const std::size_t mid = range.first + (range.second - range.first) / 2;
                    t.Insert(wide_keys[mid], wide_keys[mid]);
                    ranges.emplace_back(mid + 1, range.second);
                    ranges.emplace_back(range.first, mid);
                }
                wide_balanced_height = static_cast<std::size_t>(t.HeightIterative());
                return static_cast<std::uint64_t>(t.Size());
            },
            1);

        // std::map 用 10 万条升序数据（它的最坏输入）真跑一遍：仍然是 O(log n) 建树。
        std::size_t wide_map_probe = 0;
        const double t_wide_map = BenchMedianUs(
            [&] {
                std::map<int, int> m;
                for (const int k : wide_keys) {
                    m.insert({k, k});
                }
                // 顺着 begin() 走一步，证明容器可用（std::map 不暴露内部高度）。
                wide_map_probe = static_cast<std::size_t>(m.begin()->first);
                return static_cast<std::uint64_t>(m.size());
            },
            1);

        std::cout << "  实测（n = " << kWideN << "，Debug）：\n";
        Label("    平衡 BST（按中点顺序插入）", 40);
        std::cout << ": 高度 = " << wide_balanced_height << "，建树 " << t_wide_balanced
                  << " us\n";
        Label("    std::map（升序插入，最坏输入）", 40);
        std::cout << ": 高度不暴露（红黑树保证 O(log n)），建树 " << t_wide_map << " us\n";
        std::cout << "    （std::map 首个元素 = " << wide_map_probe
                  << "，说明升序插入后依然有序可用）\n";
        std::cout << "\n  外推（朴素 BST，升序插入）：\n";
        std::cout << "    n = " << kWideN << " 时高度 = n - 1 = " << (kWideN - 1) << "\n";
        std::cout << "    单次查找平均比较次数 ≈ n/2 = " << (kWideN / 2) << "\n";
        std::cout << "    单次查找最坏比较次数 = n = " << kWideN << "\n";
        std::cout << "    对比 std::map 的查找深度 ≈ 2*log2(n) = "
                  << static_cast<std::size_t>(2.0 * log2n + 0.5) << "\n";
        const double ratio = static_cast<double>(kWideN / 2) / (2.0 * log2n);
        std::cout << "    比较次数之比 ≈ " << ratio << " 倍（三个数量级）\n";

        Note("");
        Note("【必须知道的工程坑】有序插入 10 万条之后，朴素 BST 的高度是 10 万：");
        Note("  - 递归查找 / 递归插入 / 递归删除 / 递归析构都会产生约 10 万层调用栈，");
        Note("    Windows 默认线程栈 1MB，每帧几十到几百字节，必然栈溢出崩溃");
        Note("    （错误码 0xC00000FD: stack overflow，俗称「爆栈」）；");
        Note("  - 所以本文件的 FindNode / Contains / HeightIterative / Clear 全部是迭代实现，");
        Note("    只有 InorderRecursive / HeightRecursive / EraseNode 是递归版，");
        Note("    它们【只能】用在高度可控的树上（本文件用它们的地方树高都不超过 3）；");
        Note("  - 同一个坑在真实项目里的表现形式是：测试数据量小的时候一切正常，");
        Note("    上线遇到用户按时间顺序导入的 10 万条数据，程序直接崩。");
        Note("");
        Note("本文件第 1 节删节点用的 EraseNode 就是递归实现——它在 7 个节点的树上完美工作，");
        Note("但它正是「只能在矮树上用」的典型：真实容器必须用带父指针的迭代删除。");

        // 验证 10 万条平衡树的中序序列确实严格升序（对 10 万个元素做一次）。
        const std::vector<std::pair<int, int>> wide_seq = [&] {
            BinarySearchTree<int, int> t;
            const std::vector<int> keys = MakeSorted(kWideN);
            std::vector<std::pair<std::size_t, std::size_t>> ranges;
            ranges.emplace_back(0, kWideN);
            while (!ranges.empty()) {
                const auto range = ranges.back();
                ranges.pop_back();
                if (range.first >= range.second) {
                    continue;
                }
                const std::size_t mid = range.first + (range.second - range.first) / 2;
                t.Insert(keys[mid], keys[mid]);
                ranges.emplace_back(mid + 1, range.second);
                ranges.emplace_back(range.first, mid);
            }
            return t.InorderIterative();
        }();
        assert(wide_seq.size() == kWideN);
        AssertAscending(wide_seq, "10 万节点平衡 BST 的迭代中序");
    }

    // =======================================================================
    Section("6. AVL：平衡因子与四种旋转");
    // =======================================================================
    Note("AVL 在 BST 之上多加一条约束：每个节点的平衡因子 |BF| <= 1。");
    Note("BF = 左子树高 - 右子树高。插入后一旦 |BF| >= 2，就用旋转把它掰回来。");
    std::cout << "\n";

    {
        // --- LL：一直往左插，最后对失衡点做一次右旋 ---
        AvlNode* ll = nullptr;
        int ll_rot = 0;
        for (const int k : {30, 20, 10}) {
            ll = AvlInsert(ll, k, ll_rot);
        }
        assert(ll->key == 20 && ll->left->key == 10 && ll->right->key == 30);
        assert(AvlHeight(ll) == 1 && AvlHeightChecked(ll) == 1);
        std::cout << "  LL（30,20,10）-> 右旋一次，根变成 " << ll->key
                  << "，高度 = " << AvlHeight(ll) << "，旋转次数 = " << ll_rot << "\n";
        Note("    插入 10 后节点 30 的 BF = 2，属于 LL：对 30 做右旋，20 上位当根。");
        AvlFree(ll);

        // --- RR：一直往右插，最后做一次左旋 ---
        AvlNode* rr = nullptr;
        int rr_rot = 0;
        for (const int k : {10, 20, 30}) {
            rr = AvlInsert(rr, k, rr_rot);
        }
        assert(rr->key == 20 && rr->left->key == 10 && rr->right->key == 30);
        std::cout << "  RR（10,20,30）-> 左旋一次，根变成 " << rr->key
                  << "，高度 = " << AvlHeight(rr) << "，旋转次数 = " << rr_rot << "\n";
        AvlFree(rr);

        // --- LR：先左旋孩子再右旋自己 ---
        AvlNode* lr = nullptr;
        int lr_rot = 0;
        for (const int k : {30, 10, 20}) {
            lr = AvlInsert(lr, k, lr_rot);
        }
        assert(lr->key == 20 && lr->left->key == 10 && lr->right->key == 30);
        std::cout << "  LR（30,10,20）-> 双旋（先对 10 左旋，再对 30 右旋），根变成 "
                  << lr->key << "，旋转次数 = " << lr_rot << "\n";
        Note("    BF 的符号和孩子的 BF 符号【相反】时必须双旋：单旋解决不了折线形失衡。");
        AvlFree(lr);

        // --- RL：先右旋孩子再左旋自己 ---
        AvlNode* rl = nullptr;
        int rl_rot = 0;
        for (const int k : {10, 30, 20}) {
            rl = AvlInsert(rl, k, rl_rot);
        }
        assert(rl->key == 20 && rl->left->key == 10 && rl->right->key == 30);
        std::cout << "  RL（10,30,20）-> 双旋（先对 30 右旋，再对 10 左旋），根变成 "
                  << rl->key << "，旋转次数 = " << rl_rot << "\n";
        AvlFree(rl);

        // --- 关键对比：升序插入 10 万条，AVL 的高度仍然是 log2(n) ---
        std::cout << "\n";
        constexpr std::size_t kAvlN = 100000;
        AvlNode* avl_root = nullptr;
        int avl_rotations = 0;
        const double t_avl_build = BenchMedianUs(
            [&] {
                avl_root = nullptr;
                avl_rotations = 0;
                for (std::size_t i = 1; i <= kAvlN; ++i) {
                    avl_root = AvlInsertIterative(avl_root, static_cast<int>(i),
                                                  avl_rotations);
                }
                return static_cast<std::uint64_t>(kAvlN);
            },
            1);

        std::vector<int> avl_seq;
        avl_seq.reserve(kAvlN);
        AvlCollectSorted(avl_root, avl_seq);
        assert(avl_seq.size() == kAvlN);
        for (std::size_t i = 1; i < avl_seq.size(); ++i) {
            assert(avl_seq[i - 1] < avl_seq[i]);
        }
        const int avl_height = AvlHeightChecked(avl_root);

        std::cout << "  AVL 升序插入 " << kAvlN << " 条（最坏输入）：\n";
        Label("    树高（边数）", 30);
        std::cout << ": " << avl_height << "（同一批数据，朴素 BST 是 "
                  << (kAvlN - 1) << "）\n";
        Label("    理论高度 floor(log2(n))", 30);
        std::cout << ": " << static_cast<std::size_t>(std::log2(static_cast<double>(kAvlN)))
                  << "\n";
        Label("    插入过程中的旋转次数", 30);
        std::cout << ": " << avl_rotations << "\n";
        Label("    建树耗时 (us)", 30);
        std::cout << ": " << t_avl_build << "\n";
        Label("    中序校验", 30);
        std::cout << ": " << (avl_seq.size() == kAvlN ? "10 万元素严格升序 OK" : "FAIL")
                  << "\n";
        AvlFree(avl_root);

        Note("");
        Note("同一个「升序插入」的输入：朴素 BST 高 " + std::to_string(kAvlN - 1)
             + "，AVL 高 " + std::to_string(avl_height) + "。");
        Note("AVL 用旋转把最坏情况压回 O(log n)，代价是每次插入/删除都要维护高度并可能旋转。");
    }

    // =======================================================================
    Section("7. 为什么 std::map 选红黑树而不是 AVL");
    // =======================================================================
    Note("两种树都保证 O(log n)，但「平衡的严格程度」不一样，这决定了旋转次数：");
    std::cout << "\n";

    {
        Label("  对比项", 26);
        Label("AVL 树", 30);
        Label("红黑树（std::map）", 30);
        std::cout << "\n" << std::string(88, '-') << "\n";

        Label("  平衡条件", 26);
        Label("任意节点左右子树高度差 <= 1", 30);
        Label("最长路径 <= 最短路径的 2 倍", 30);
        std::cout << "\n";

        Label("  树高上界", 26);
        Label("约 1.44 * log2(n)", 30);
        Label("约 2 * log2(n)", 30);
        std::cout << "\n";

        Label("  查找性能", 26);
        Label("略快（树更矮）", 30);
        Label("略慢（最多多比较一倍）", 30);
        std::cout << "\n";

        Label("  插入旋转次数", 26);
        Label("最多 2 次（单旋 + 双旋）", 30);
        Label("最多 2 次（且插入后最多调整 2 层）", 30);
        std::cout << "\n";

        Label("  删除旋转次数", 26);
        Label("最坏 O(log n) 次", 30);
        Label("最多 3 次", 30);
        std::cout << "\n";

        Label("  删除的调整动作", 26);
        Label("旋转 + 更新高度，可能一路传上去", 30);
        Label("主要靠【重新着色】，旋转极少", 30);
        std::cout << "\n";

        Label("  维护的额外信息", 26);
        Label("每个节点存高度/平衡因子", 30);
        Label("每个节点存 1 个颜色位", 30);
        std::cout << "\n";
        std::cout << std::string(88, '-') << "\n";

        Note("");
        Note("为什么「插入最多 2 次旋转」这句话要分成两半看：");
        Note("  AVL 的插入虽然最多也只要 2 次旋转，但【每次插入都要沿路径更新高度】；");
        Note("  红黑树的插入在旋转之后，向上只需再做 O(log n) 次【重新着色】就结束，");
        Note("  而着色比旋转便宜得多。删除的差距更大：AVL 删除可能一路旋转到根，");
        Note("  红黑树删除最多 3 次旋转 + O(log n) 次着色。");
        Note("");
        Note("工程结论（这才是选择红黑树的真正原因）：");
        Note("  1) std::map 是【通用容器】，真实负载是「插入删除和查找一样频繁」，");
        Note("     甚至插入删除更多（缓存、索引、订阅表）。AVL 把查找优化到极致，");
        Note("     却把写操作的代价抬高了，属于优化错了方向；");
        Note("  2) 红黑树的平衡条件宽松，写操作需要重排的次数是【常数级】的，");
        Note("     而 AVL 的删除是 O(log n) 次旋转。对于要保证「每次操作延迟稳定」的");
        Note("     场景（比如实时系统、游戏主循环），常数上界比平均值更重要；");
        Note("  3) 红黑树每个节点只需要 1 个颜色位，不需要存高度；AVL 要存高度或平衡因子，");
        Note("     每个节点多几个字节——对内存敏感的容器，这是实实在在的差别；");
        Note("  4) 查找变慢的那一点点（树高从 1.44*log2(n) 到 2*log2(n)）在实际数据量下");
        Note("     只是几次比较的差距，而写操作省下的旋转是实打实的。");
        Note("");
        Note("补充：如果你确定场景是「一次构建、海量查询、几乎不改」，那么 AVL 或");
        Note("      静态的排序数组 + 二分查找都可能更合适。容器选择永远取决于负载特征。");

        // 用可以观测到的现象收尾：AVL 在升序插入 10 万条时旋转了多少次？
        // 这个数字远大于「红黑树插入最多 2 次旋转」的直觉，因为它统计的是整个过程。
        constexpr std::size_t kProbeN = 50000;
        AvlNode* probe_root = nullptr;
        int probe_rotations = 0;
        for (std::size_t i = 1; i <= kProbeN; ++i) {
            probe_root = AvlInsertIterative(probe_root, static_cast<int>(i), probe_rotations);
        }
        std::cout << "\n";
        std::cout << "  实测：AVL 升序插入 " << kProbeN << " 条，共发生 " << probe_rotations
                  << " 次旋转（平均每条 " << (static_cast<double>(probe_rotations)
                                              / static_cast<double>(kProbeN))
                  << " 次）。\n";
        std::cout << "  注意：这里统计的是【整棵树构造过程】的累计旋转次数，不是单次插入的上界。\n";
        Note("单次插入的旋转次数上界才是比较两种树的关键指标（AVL 2 次 / 红黑树 2 次），");
        Note("但删除时的差距是 O(log n) vs O(1)，这才是 std::map 选红黑树的决定性理由。");
        AvlFree(probe_root);
    }

    // =======================================================================
    Section("8. 总结：什么时候能用手写 BST");
    // =======================================================================
    Note("可以：教学、面试、嵌入式里极度受限的场景、或者数据分布已知且随机的只读索引。");
    Note("不可以：任何「数据来自用户 / 文件 / 数据库」的生产代码。因为你无法保证");
    Note("        输入顺序的随机性，而朴素 BST 的最坏情况（O(n)）会真的发生。");
    Note("");
    Note("工程铁律：");
    Note("  - 要有序 + 范围查询 -> std::map / std::set（红黑树，永远 O(log n)）；");
    Note("  - 只要查找、不在乎顺序 -> std::unordered_map / std::unordered_set（平均 O(1)）；");
    Note("  - 要频繁取最值 -> std::priority_queue（见 07_heap_and_priority_queue.cpp）；");
    Note("  - 真的需要更强的有序统计能力 -> 用成熟库（比如 Boost.Intrusive / abseil），");
    Note("    而不是自己写平衡树。");
    Note("");
    Note("本文件所有实现均为教学用途，生产请用 std::map / std::set。");

    // 让 g_sink 真正被读过一次，确保所有测量结果都参与了运算。
    std::cout << "\n[校验] 累加器非零，说明所有测量结果都真实参与了运算: "
              << (g_sink != 0 ? "是" : "否") << "\n";

    return 0;
}
