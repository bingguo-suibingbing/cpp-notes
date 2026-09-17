// ============================================================================
// 07_heap_and_priority_queue.cpp
// 演示主题：
//   1. 手写二叉堆（模板 BinaryHeap）：数组表示、0 基下标的父子下标公式、
//      sift_up / sift_down 的实现与 O(log n) 复杂度
//   2. build_heap 为什么是 O(n) 而不是 O(n log n)：完整数学推导 + 实测对比
//   3. 堆排序：原地、O(n log n)、不稳定；并给出「为什么不稳定」的具体证据
//   4. std::priority_queue 的用法：默认大顶堆、std::greater 小顶堆、自定义比较器
//   5. 最重要的坑：priority_queue 的比较器语义与 std::sort 的比较器【方向相反】
//   6. 堆的实战场景：Top-K 三方案对比、K 路归并、数据流中位数（双堆法）
//   7. 正确性验证：手写堆与 std::priority_queue 对拍、手写堆排与 std::sort 对拍、
//      Top-K 三种方案结果集合一致、双堆中位数与排序结果一致
//
// 关键结论：
//   1. build_heap 的代价不是「n 次 O(log n)」，而是「每个节点的代价正比于它的高度」：
//      高度为 h 的节点最多约有 n / 2^(h+1) 个，于是总代价 = Σ h*n/2^(h+1)
//      = n/2 * Σ h/2^h = n/2 * 2 = O(n)。
//      直觉：绝大多数节点都在底部附近（高度 0 或 1），它们几乎不需要下沉；
//      只有根附近极少数节点才需要走满 log n 层。n 次 sift_up 则是反过来的，吃亏。
//   2. 堆排序是 O(n log n)、原地（额外空间 O(1)）、但【不稳定】：根与末尾元素交换
//      会把相同键的相对顺序彻底打乱（本文件有可运行的证据）。
//   3. std::priority_queue 的 Compare 参数不是「谁排前面」，而是「谁【优先级更低】」：
//      Compare(a, b) 返回 true 表示 a 应该排在 b 后面（更晚被弹出）。
//      所以同一个 std::less<int>，在 std::sort 里得到升序，在 priority_queue 里
//      得到「大顶堆、弹出降序」——两者方向完全相反，这是最经典的坑。
//   4. Top-K 三方案：全排序 O(n log n)、大小 K 的小顶堆 O(n log k)、
//      nth_element 平均 O(n)。n 很大而 k 很小时堆完胜；k 接近 n 时全排序反而不亏。
//   5. 数据流中位数用双堆（大顶堆存较小一半、小顶堆存较大一半）：
//      插入 O(log n)、取中位数 O(1)，两个堆的大小差永远不超过 1。
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
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <queue>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// 防止测量结果被优化掉
// ---------------------------------------------------------------------------
// Release 下如果计算结果没人用，编译器会把整段代码删掉，于是你测出「0 毫秒」。
// 把结果写进 volatile 全局变量，强制产生真实的读写副作用。
// C++20 起 volatile 的复合赋值（g_sink += r）已被弃用（P1152），
// 所以一律写成「先读、再算、再写」的展开形式。
volatile std::uint64_t g_sink = 0;

// ---------------------------------------------------------------------------
// 打印小工具（本文件自带一份，因为「一个 .cpp = 一个独立可执行文件」）
// ---------------------------------------------------------------------------
void Section(const std::string& title) {
    std::cout << "\n============================================================\n";
    std::cout << title << "\n";
    std::cout << "============================================================\n";
}

void Note(const std::string& text) { std::cout << "  " << text << "\n"; }

// 估算字符串在终端里占的「显示列数」。
// 坑：std::setw 数的是 char 个数，而一个中文字符在 UTF-8 里占 3 字节、显示 2 列，
// 直接用 setw 对齐含中文的表头，表格一定是歪的。
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
// 用中位数而不是平均值：操作系统调度、其他进程抢 CPU 会让某次测量突然变慢，
// 平均值会被异常值拉高，中位数对异常值免疫。
// 用 steady_clock 而不是 system_clock：前者单调，不会被 NTP 校时影响。
// 返回值单位：微秒（us）。
template <typename Fn>
double BenchMedianUs(Fn&& fn, int repeats) {
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(repeats));

    for (int i = 0; i < repeats; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        const std::uint64_t result = fn();
        const auto t1 = std::chrono::steady_clock::now();

        g_sink = g_sink + result;  // 防止被优化掉的落点

        const std::chrono::duration<double, std::micro> dt = t1 - t0;
        samples.push_back(dt.count());
    }

    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

// 预热：把「缺页中断 + 缓存冷启动 + 分支预测器未训练」的一次性成本空跑掉。
template <typename Fn>
void WarmUp(Fn&& fn, int times) {
    for (int i = 0; i < times; ++i) {
        g_sink = g_sink + fn();
    }
}

// ===========================================================================
// 第 1 部分：手写二叉堆
// ===========================================================================
// 二叉堆是什么：一棵【完全二叉树】，用数组存，不需要任何指针。
// 数组表示的两个前提：
//   1) 完全二叉树只有最后一层可能不满，且节点都靠左 -> 数组里没有空洞；
//   2) 父子关系可以用纯算术算出来，不需要存指针。
//
// 父子下标公式的由来（这是一切的关键，值得自己推一遍）：
//   1 基下标（把根放在下标 1）：
//     节点 i 的左孩子在 2i，右孩子在 2i+1，父亲是 floor(i/2)。
//     推导：完全二叉树第 h 层（根为第 0 层）有 2^h 个节点，
//     第 0 到 h-1 层一共 2^h - 1 个节点，所以第 h 层第一个节点的 1 基下标是 2^h。
//     第 h 层第 j 个节点（j 从 0 开始）下标为 2^h + j，它的两个孩子在下一层的
//     位置是 2^(h+1) + 2j 和 2^(h+1) + 2j + 1 = 2*(2^h + j) 和 2*(2^h + j) + 1，
//     也就是 2i 和 2i+1。公式由此而来。
//   0 基下标（C++ 数组天然从 0 开始）：
//     把 1 基公式做变量替换 i' = i - 1，得到：
//       左孩子 = 2*i + 1，右孩子 = 2*i + 2，父亲 = (i - 1) / 2（整数除法）。
//     验证：根 i=0 的孩子是 1 和 2；节点 1 的孩子是 3 和 4；节点 2 的父亲是 (2-1)/2 = 0。
//
// 为什么这里选 0 基：直接和 std::vector / std::sort / 原生数组共用同一套下标，
// 不需要「在下标 0 处放一个哨兵」这种别扭写法，也不需要到处写 i-1。
// 代价是公式里多一个 -1 和 +1，而且乘法有 2*i 的溢出风险（n 上千万时要注意）。
// 如果选 1 基（比如 std::priority_queue 内部、或者某些教材的实现），
// 公式会变成 2i / 2i+1 / i/2，看起来更漂亮，也能用 i/2 直接判断叶子，
// 但 vector 的第 0 个槽位就浪费了，而且和 STL 算法的下标习惯不一致。
//
// 堆序性质：
//   大顶堆（默认）：每个节点的值 >= 它的两个孩子；
//   小顶堆：每个节点的值 <= 它的两个孩子。
//   注意堆序【不要求】左右孩子之间有任何关系，所以堆不是排序结构，
//   它的能力是 O(1) 取最值、O(log n) 插入删除，而不是 O(log n) 查找任意元素。
//
// 复杂度一览（n = 元素个数）：
//   Top     O(1)
//   Push    O(log n)：先放到数组末尾，再 sift_up
//   Pop     O(log n)：把末尾元素搬到根，再 sift_down
//   BuildHeap O(n)：从最后一个非叶节点倒着 sift_down（见第 3 节的完整推导）
//   HeapSort  O(n log n) 时间、O(1) 额外空间、不稳定
//
// 教学用途，生产请用 std::priority_queue（以及 std::push_heap / std::pop_heap /
// std::make_heap / std::sort_heap 这组算法）。
template <typename T, typename Compare = std::less<T>>
class BinaryHeap {
public:
    BinaryHeap() = default;
    explicit BinaryHeap(const Compare& comp) : comp_(comp) {}

    // 从一段数据直接建堆：O(n)，比逐个 Push 快。
    static BinaryHeap FromVector(std::vector<T> data, const Compare& comp = Compare()) {
        BinaryHeap h(comp);
        h.data_ = std::move(data);
        h.BuildHeap();
        return h;
    }

    void Reserve(std::size_t n) { data_.reserve(n); }

    std::size_t Size() const { return data_.size(); }
    bool Empty() const { return data_.empty(); }
    void Clear() { data_.clear(); }

    // Top 是 O(1)：堆序保证「最好的元素永远在根上」，取它就是读 data_[0]。
    const T& Top() const {
        assert(!data_.empty());
        return data_[0];
    }

    // 插入：O(log n)。
    // 为什么放到末尾：完全二叉树要保持「最后一层靠左填满」，新节点只能接在数组尾部，
    // 否则数组出现空洞，父子下标公式就失效了。
    void Push(const T& value) {
        data_.push_back(value);
        SiftUpAt(data_.size() - 1);
    }

    void Push(T&& value) {
        data_.push_back(std::move(value));
        SiftUpAt(data_.size() - 1);
    }

    // 删除堆顶：O(log n)。
    // 做法：把末尾元素搬到根（直接删根会在数组中间挖个洞，结构就毁了），
    // 然后让它一路 sift_down 找到自己的位置。
    T Pop() {
        assert(!data_.empty());
        T best = std::move(data_[0]);
        data_[0] = std::move(data_.back());
        data_.pop_back();
        if (!data_.empty()) {
            SiftDownAt(0);
        }
        return best;
    }

    // 建堆（Floyd 算法）：O(n)，不是 O(n log n)，证明见第 3 节。
    void BuildHeap() {
        if (data_.size() < 2) {
            return;
        }
        // 最后一个非叶节点的下标 = (n - 1 - 1) / 2 = n/2 - 1，也就是最后一个节点的父亲。
        // 从它开始【倒着】往前处理。叶节点（下标 > n/2 - 1）天然满足堆序，不用管。
        for (std::size_t i = data_.size() / 2; i-- > 0;) {
            SiftDownAt(i);
        }
    }

    // 暴露单个 sift 操作，方便逐步骤验证。
    void SiftUpAt(std::size_t index) {
        // 上浮：新元素的优先级可能比父亲高，所以一路上移直到父亲比它更好。
        while (index > 0) {
            const std::size_t parent = (index - 1) / 2;  // 0 基的父亲公式
            if (!Better(data_[index], data_[parent])) {
                break;  // 已经不比父亲好，位置正确，停
            }
            std::swap(data_[index], data_[parent]);
            index = parent;
        }
    }

    void SiftDownAt(std::size_t index) {
        // 下沉：把 index 处的元素和「两个孩子里更好的那个」比较，
        // 如果孩子更好就交换、继续往下；否则停。
        const std::size_t n = data_.size();
        for (;;) {
            const std::size_t left = 2 * index + 1;   // 0 基的左孩子
            const std::size_t right = 2 * index + 2;  // 0 基的右孩子
            if (left >= n) {
                break;  // 连左孩子都没有 -> 自己是叶子，停
            }
            // 只在存在的孩子里选「更好的那个」；右孩子可能不存在。
            const std::size_t pick = (right < n && Better(data_[right], data_[left])) ? right : left;
            if (!Better(data_[pick], data_[index])) {
                break;  // 两个孩子都不比自己好，堆序已满足
            }
            std::swap(data_[index], data_[pick]);
            index = pick;
        }
    }

    // -----------------------------------------------------------------------
    // 堆排序：原地、O(n log n)、额外空间 O(1)、【不稳定】
    // -----------------------------------------------------------------------
    // 原理分两步：
    //   1) 先 BuildHeap，把数组变成一个堆，于是最大值跑到 data_[0]；
    //   2) 反复把 data_[0]（当前最大值）和「当前堆的最后一个位置」交换，堆大小减 1，
    //      再对新根 sift_down。每轮都把当时的最大值「钉」到数组末尾，
    //      所以最后数组是从小到大。
    // 为什么必须用 sift_down 而不是 sift_up：每轮只需要修复根这一个位置，
    // sift_down 恰好是 O(log n)；用 sift_up 会把刚放上去的小元素留在根上，排序就错了。
    void Sort() {
        const std::size_t n = data_.size();
        if (n < 2) {
            return;
        }
        BuildHeap();
        for (std::size_t end = n; end > 1; --end) {
            // 交换根（当前最大值）到 end-1，然后缩小堆的范围（heap_size = end - 1）。
            std::swap(data_[0], data_[end - 1]);
            SiftDownRange(0, end - 1);
        }
    }

    const std::vector<T>& Data() const { return data_; }

    // 校验堆序性质：对每个非根节点，它不能比父亲更好。
    // 注意这是【验证器】，正常使用时不需要，但它让「手写堆和 STL 对拍」变得可信。
    bool Validate() const {
        for (std::size_t i = 1; i < data_.size(); ++i) {
            const std::size_t parent = (i - 1) / 2;
            if (Better(data_[i], data_[parent])) {
                return false;  // i 比父亲更好 -> 违反堆序
            }
        }
        return true;
    }

private:
    // 统一的优先级判断：Better(a, b) 为 true 表示 a 应该更靠近堆顶。
    // 这里是全文件最容易读错的地方：不管是 less 还是 greater，我们关心的都是
    // 「谁先出队」，而 comp_(a, b) 的原始含义是「a 排在 b 前面」，
    // 对它取反正好得到「谁更应该靠前（更靠近根）」。
    bool Better(const T& a, const T& b) const { return comp_(b, a); }

    // 带 heap_size 的 sift_down：堆排序时「已排好的尾巴」不属于堆，
    // 所以不能让下沉越过 heap_size 这条线。
    void SiftDownRange(std::size_t index, std::size_t heap_size) {
        for (;;) {
            const std::size_t left = 2 * index + 1;
            const std::size_t right = 2 * index + 2;
            if (left >= heap_size) {
                break;
            }
            const std::size_t pick =
                (right < heap_size && Better(data_[right], data_[left])) ? right : left;
            if (!Better(data_[pick], data_[index])) {
                break;
            }
            std::swap(data_[index], data_[pick]);
            index = pick;
        }
    }

    std::vector<T> data_;
    Compare comp_{};
};

// ===========================================================================
// 第 2 部分：堆排序的「不稳定」到底是什么
// ===========================================================================
// 稳定排序的定义：键相同的两个元素，排序后相对顺序不变。
// 堆排序为什么做不到？看排序的那一步：std::swap(data_[0], data_[end-1])。
// 根上放的是【堆里最大的那个元素】，但它不一定是「原序列里最靠前的那个最大值」；
// 堆的结构只保证父子关系，同一层的兄弟之间、以及不同子树之间没有任何顺序约束。
// 于是把根和末尾一交换，键相同的元素就被扔到了彼此后面，相对顺序被打乱。
//
// 上浮/下沉阶段的交换同样会产生这种效果：sift_down 时和「更好的那个孩子」交换，
// 而两个孩子之间谁更好完全取决于它们的值，不看它们原来谁在前。
//
// 结论：任何基于「远距离交换」的原地排序（堆排序、选择排序、快速排序）都天然不稳定；
// 想稳定就得付出额外空间（归并排序）或者限制在相邻交换（插入排序、冒泡排序）。
struct Item {
    int key = 0;
    int seq = 0;  // 输入时的序号，用来观察稳定性
};

// 只比较 key：这样「key 相同的两个元素谁在前面」完全由排序算法的行为决定。
struct ItemKeyLess {
    bool operator()(const Item& a, const Item& b) const { return a.key < b.key; }
};

// ===========================================================================
// 第 3 部分：Top-K 的三种做法
// ===========================================================================
// 输入 n 个数，输出最大的 K 个（顺序不限）。
//
// 方案 A：全排序后取前 K 个      O(n log n) 时间、O(n) 空间
//   优点：代码最短，K 接近 n 时最合适；缺点：为了 K 个元素把 n 个都排了，浪费。
//
// 方案 B：维护一个大小为 K 的【小顶堆】  O(n log K) 时间、O(K) 空间
//   为什么用小顶堆：堆顶是「当前这 K 个候选里最小的」。新元素只要比堆顶大，
//   就说明它够格进候选，把堆顶挤掉即可。若用大顶堆，堆顶是候选里最大的，
//   你根本不知道谁该被淘汰，就得遍历整个堆。
//   优点：K 很小时几乎就是线性；缺点：K 接近 n 时 log K 接近 log n，优势消失。
//
// 方案 C：std::nth_element     平均 O(n) 时间、O(1) 额外空间（原地打乱输入）
//   原理是快速选择（quickselect）：把第 K 大的元素放到它最终该在的位置，
//   左边都比它小、右边都比它大。它不保证左右两侧内部有序。
//   优点：平均最快；缺点：最坏 O(n^2)（虽然工程实现有中位数近似保证），
//   而且会打乱输入数组，也不适合「数据流式到达」的场景（必须一次性拿到全部数据）。
std::vector<int> TopKByFullSort(std::vector<int> values, std::size_t k) {
    std::sort(values.begin(), values.end(), std::greater<int>());  // 降序
    values.resize(k);
    return values;
}

std::vector<int> TopKByMinHeap(const std::vector<int>& values, std::size_t k) {
    // 手写的小顶堆：BinaryHeap 的默认比较器是 less，得到的是大顶堆，
    // 所以这里显式传 std::greater<int> 变成小顶堆。
    BinaryHeap<int, std::greater<int>> heap;
    heap.Reserve(k + 1);
    for (const int x : values) {
        if (heap.Size() < k) {
            heap.Push(x);
        } else if (x > heap.Top()) {
            heap.Pop();      // 堆顶是候选里最小的，先把它挤出去
            heap.Push(x);
        }
    }
    std::vector<int> out;
    out.reserve(heap.Size());
    while (!heap.Empty()) {
        out.push_back(heap.Pop());
    }
    return out;
}

std::vector<int> TopKByNthElement(std::vector<int> values, std::size_t k) {
    // nth_element 之后，[0, k) 一定是「最大的 k 个」（顺序任意）。
    // 注意 nth_element 是修改输入的原地算法，所以这里按值传参（拷贝一份）。
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(k),
                     values.end(), std::greater<int>());
    values.resize(k);
    return values;
}

// ===========================================================================
// 第 4 部分：K 路归并用的单链表
// ===========================================================================
struct ListNode {
    int value = 0;
    ListNode* next = nullptr;

    explicit ListNode(int v) : value(v) {}
};

ListNode* BuildList(const std::vector<int>& sorted_values) {
    ListNode* head = nullptr;
    ListNode* tail = nullptr;
    for (const int v : sorted_values) {
        ListNode* node = new ListNode(v);
        if (head == nullptr) {
            head = node;
            tail = node;
        } else {
            tail->next = node;
            tail = node;
        }
    }
    return head;
}

void FreeList(ListNode* head) {
    while (head != nullptr) {
        ListNode* next = head->next;
        delete head;
        head = next;
    }
}

// K 路归并：复杂度 O(N log K)，N 是总元素数。
// 为什么是 log K 而不是 log N：堆里【永远只有 K 个元素】（每路一个当前游标），
// 所以每次堆操作的代价是 O(log K)。总比较次数约 N*log K。
// 对照：把所有元素倒进一个 vector 再排序是 O(N log N)，K 小时明显更差。
ListNode* MergeKSortedLists(const std::vector<ListNode*>& lists) {
    // 关键：priority_queue 里存的是【节点指针】，所以比较器要作用在指针上，
    // 自己按 node->value 比较，把值小的排成更高优先级（小顶堆语义）。
    // 这就是「自定义比较器」最典型的用法：容器元素类型和比较依据不是同一个东西。
    struct NodeGreater {
        bool operator()(const ListNode* a, const ListNode* b) const {
            return a->value > b->value;  // a 的值更大 -> a 优先级更低（更晚出队）
        }
    };

    std::priority_queue<ListNode*, std::vector<ListNode*>, NodeGreater> pq;
    for (ListNode* head : lists) {
        if (head != nullptr) {
            pq.push(head);  // 每一路只把第一个元素放进堆
        }
    }

    ListNode* merged_head = nullptr;
    ListNode* merged_tail = nullptr;
    while (!pq.empty()) {
        ListNode* node = pq.top();
        pq.pop();
        if (node->next != nullptr) {
            pq.push(node->next);  // 这一路往前推一格，把新游标放回堆里
        }
        // 用尾指针串结果，避免每次都从头找尾巴（否则会退化成 O(N^2)）。
        if (merged_head == nullptr) {
            merged_head = node;
            merged_tail = node;
        } else {
            merged_tail->next = node;
            merged_tail = node;
        }
    }
    if (merged_tail != nullptr) {
        merged_tail->next = nullptr;  // 收尾，防止把原链表的残余接上来
    }
    return merged_head;
}

// ===========================================================================
// 第 5 部分：数据流中位数（双堆法）
// ===========================================================================
// 目标：数据一个一个到来，随时能 O(1) 回答「当前所有数的中位数」。
//
// 结构：两个堆，把数据从中间劈成两半。
//   low_ ：大顶堆，存【较小的一半】（堆顶是这一半里最大的，也就是中位数的下界）；
//   high_：小顶堆，存【较大的一半】（堆顶是这一半里最小的，也就是中位数的上界）。
//
// 平衡规则（必须严格遵守，否则中位数就取错了）：
//   |low_.size() - high_.size()| <= 1，且我们约定 low_.size() >= high_.size()。
//   也就是：奇数个元素时，多出来的那个一定在 low_ 里，中位数就是 low_.Top()。
//
// 插入流程（插入 O(log n)）：
//   1) 新元素先无条件进 low_（保证 low_ 存的是「较小的一半」——先把可能偏大的
//      元素暂时放进来，下一步再纠正）；
//   2) 把 low_ 的堆顶（low_ 里最大的那个）挪到 high_。这一步是精髓：
//      它保证了「low_ 的所有元素 <= high_ 的所有元素」这条跨堆不变式，
//      因为 low_ 的最大值就是这两个堆的分界线；
//   3) 如果 high_ 比 low_ 大（说明刚才多倒了一个），就把 high_ 的堆顶挪回 low_，
//      恢复大小平衡。
//   每次操作最多 2 次堆操作，所以插入是 O(log n)。
class MedianFinder {
public:
    void Add(int value) {
        low_.push(value);              // 1) 先塞进较小的一半
        high_.push(low_.top());        // 2) 把分界线元素挪到较大的一半
        low_.pop();
        if (high_.size() > low_.size()) {  // 3) 恢复「low_ 不少于 high_」
            low_.push(high_.top());
            high_.pop();
        }
        assert(low_.size() >= high_.size());
        assert(low_.size() - high_.size() <= 1);
    }

    // 取中位数 O(1)：奇数个就是 low_ 的堆顶；偶数个是两堆堆顶的平均值。
    double Median() const {
        assert(!low_.empty());
        if (low_.size() > high_.size()) {
            return static_cast<double>(low_.top());
        }
        return (static_cast<double>(low_.top()) + static_cast<double>(high_.top())) / 2.0;
    }

    std::size_t Size() const { return low_.size() + high_.size(); }

private:
    std::priority_queue<int> low_;                              // 默认大顶堆
    std::priority_queue<int, std::vector<int>, std::greater<int>> high_;  // 小顶堆
};

// ===========================================================================
// 第 6 部分：造数据与小工具
// ===========================================================================
std::vector<int> MakeRandomInts(std::size_t n, int bound, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<int> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = static_cast<int>(rng() % static_cast<unsigned>(bound));
    }
    return v;
}

void PrintIntsHead(const std::vector<int>& v, std::size_t count) {
    std::cout << "[";
    const std::size_t limit = (v.size() < count) ? v.size() : count;
    for (std::size_t i = 0; i < limit; ++i) {
        std::cout << v[i] << (i + 1 == limit ? "" : ", ");
    }
    if (v.size() > limit) {
        std::cout << ", ... 共 " << v.size() << " 个";
    }
    std::cout << "]";
}

// 打印 0 基下标下的父子关系，把公式「看见」。
void PrintIndexRelations(std::size_t index, std::size_t n) {
    Label("    下标 " + std::to_string(index), 16);
    std::cout << ": 父亲 = " << ((index == 0) ? std::string("无（它就是根）")
                                              : std::to_string((index - 1) / 2));
    std::cout << "，左孩子 = ";
    if (2 * index + 1 < n) {
        std::cout << 2 * index + 1;
    } else {
        std::cout << "无";
    }
    std::cout << "，右孩子 = ";
    if (2 * index + 2 < n) {
        std::cout << 2 * index + 2;
    } else {
        std::cout << "无";
    }
    std::cout << "\n";
}

}  // namespace

int main() {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "本文件在 Debug 配置下运行，绝对耗时比 Release 慢，请只看趋势与数量级。\n";

    // =======================================================================
    Section("1. 手写二叉堆：数组表示与父子下标公式");
    // =======================================================================
    Note("堆是完全二叉树 + 数组存储 + 堆序性质，不需要任何指针。");
    Note("0 基下标公式（C++ 的天然选择）：");
    Note("  parent(i) = (i - 1) / 2（整数除法）");
    Note("  left(i)   = 2 * i + 1");
    Note("  right(i)  = 2 * i + 2");
    Note("1 基下标公式（教材常见，把根放在下标 1）：");
    Note("  parent(i) = i / 2");
    Note("  left(i)   = 2 * i");
    Note("  right(i)  = 2 * i + 1");
    Note("推导：完全二叉树第 h 层有 2^h 个节点，第 0 到 h-1 层共 2^h - 1 个。");
    Note("      所以第 h 层第一个节点的 1 基下标是 2^h，第 j 个节点在 2^h + j，");
    Note("      它的孩子在下一层位于 2^(h+1) + 2j 和 2^(h+1) + 2j + 1，");
    Note("      恰好是 2*(2^h + j) 与 2*(2^h + j) + 1，也就是 2i 和 2i+1。");
    Note("      把 i 换成 i+1（0 基）就得到 2i+1 / 2i+2 / (i-1)/2。");
    std::cout << "\n";

    const std::vector<int> demo_values = {50, 30, 70, 20, 40, 60, 80, 15, 25, 65};
    {
        std::cout << "  示例数组: ";
        PrintIntsHead(demo_values, demo_values.size());
        std::cout << "\n";
        for (std::size_t i = 0; i < demo_values.size(); i += 3) {
            PrintIndexRelations(i, demo_values.size());
        }
        Note("");
        Note("为什么选 0 基：可以直接和 std::vector / std::sort / 原生数组共用一套下标，");
        Note("           不需要在下标 0 处放哨兵，也不用到处写 i-1；代价是公式里多个 +1。");
        Note("           选 1 基的公式更漂亮（2i / 2i+1 / i/2），但浪费一个槽位，");
        Note("           而且和 STL 的下标习惯不一致。本文件统一用 0 基。");
    }

    // =======================================================================
    Section("2. 堆的核心操作：sift_up / sift_down 与对拍验证");
    // =======================================================================
    Note("sift_up：新元素放在数组末尾，然后一路上浮。代价 O(log n)（最多走满树高）。");
    Note("sift_down：根上的元素一路下沉，每次与两个孩子中更好的那个交换。也是 O(log n)。");
    Note("sift_up 和 sift_down 的代价都正比于「走过的层数」，最多是树高 = floor(log2(n))。");
    std::cout << "\n";

    {
        // --- 大顶堆：逐个 Push，每一步都和 std::priority_queue 对拍 ---
        BinaryHeap<int> heap;
        std::priority_queue<int> ref;
        std::mt19937 rng(777u);
        bool push_ok = true;

        for (int step = 0; step < 3000; ++step) {
            const int value = static_cast<int>(rng() % 100000u);
            heap.Push(value);
            ref.push(value);
            if (heap.Top() != ref.top() || heap.Size() != ref.size()) {
                push_ok = false;
                break;
            }
        }
        assert(push_ok && heap.Validate());
        std::cout << "  Push 3000 次：每一步的 Top 与 Size 都与 std::priority_queue 一致 -> "
                  << (push_ok ? "OK" : "FAIL") << "\n";
        std::cout << "  手写堆 Validate()（每个节点都不比父亲更好）-> "
                  << (heap.Validate() ? "OK" : "FAIL") << "\n";

        // --- 连续 Pop，弹出序列必须是严格不升的（大顶堆 -> 降序） ---
        bool pop_ok = true;
        int prev = 0;
        bool first = true;
        while (!ref.empty()) {
            const int mine = heap.Pop();
            const int theirs = ref.top();
            ref.pop();
            if (mine != theirs) {
                pop_ok = false;
                break;
            }
            if (!first && mine > prev) {
                pop_ok = false;  // 大顶堆弹出必须不升
                break;
            }
            prev = mine;
            first = false;
        }
        assert(pop_ok && heap.Empty());
        std::cout << "  Pop  3000 次：弹出序列与 std::priority_queue 完全一致，且不升 -> "
                  << (pop_ok ? "OK" : "FAIL") << "\n";
        Note("这就是「对拍」：不是看一眼觉得对，而是让两个独立实现逐元素比对。");

        // --- 小顶堆：显式传 std::greater ---
        BinaryHeap<int, std::greater<int>> min_heap;
        std::priority_queue<int, std::vector<int>, std::greater<int>> min_ref;
        std::vector<int> sample = MakeRandomInts(2000, 5000, 4242u);
        for (const int x : sample) {
            min_heap.Push(x);
            min_ref.push(x);
        }
        bool min_ok = min_heap.Validate();
        while (!min_ref.empty()) {
            if (min_heap.Pop() != min_ref.top()) {
                min_ok = false;
                break;
            }
            min_ref.pop();
        }
        assert(min_ok);
        std::cout << "  小顶堆（std::greater）：2000 次 Push + Pop 与 STL 小顶堆一致 -> "
                  << (min_ok ? "OK" : "FAIL") << "\n";
        Note("注意：同一个 BinaryHeap 模板，只换比较器就从大顶堆变成小顶堆，");
        Note("      所以堆的「大顶/小顶」是策略（比较器），不是两份代码。");
    }

    // =======================================================================
    Section("3. build_heap 为什么是 O(n) 而不是 O(n log n)");
    // =======================================================================
    Note("先看错误的直觉：建堆要处理 n 个节点，每个 sift_down 是 O(log n)，");
    Note("             所以是 O(n log n)——错在「每个节点都走满 log n 层」这个假设。");
    Note("");
    Note("正确推导（完整版）：");
    Note("  设树高 H = floor(log2(n))，h(v) 表示节点 v 的高度（到最远叶子的边数）。");
    Note("  1) sift_down 一个节点 v 的代价正比于 h(v)，不是 H。因为它一旦沉到底就停，");
    Note("     而底部的节点高度很小。总代价 = Σ over v of h(v)。");
    Note("  2) 完全二叉树里，高度恰好为 h 的节点最多有 ceil(n / 2^(h+1)) 个。");
    Note("     直觉估算：第 (H-h) 层有 2^(H-h) 个节点；高度为 h 的节点都在这一层，");
    Note("     而完全二叉树的总节点数 n >= 2^H（前 H 层全满），所以");
    Note("     2^(H-h) = 2^H / 2^h <= n / 2^h，再除以 2 得到更细的界 n / 2^(h+1)。");
    Note("  3) 于是总代价 = Σ(h 从 0 到 H) h * ceil(n / 2^(h+1))");
    Note("                  <= (n/2) * Σ(h 从 0 到无穷) h / 2^h");
    Note("  4) 无穷级数 Σ h/2^h = 1/2 + 2/4 + 3/8 + 4/16 + ... = 2（下面是初等证明）。");
    Note("     所以总代价 <= (n/2) * 2 = n，即 O(n)。");
    Note("");
    Note("级数 Σ h*x^h = x / (1-x)^2 的证明（|x| < 1）：");
    Note("  因为 Σ x^h = 1/(1-x)，两边求导得 Σ h*x^(h-1) = 1/(1-x)^2，");
    Note("  再乘 x 得 Σ h*x^h = x/(1-x)^2。代入 x = 1/2：");
    Note("  Σ h/2^h = (1/2) / (1/2)^2 = (1/2) / (1/4) = 2。得证。");
    Note("");
    Note("直觉解释：堆里大约一半的节点是叶子（高度 0，代价 0 层），");
    Note("          四分之一高度为 1（最多沉 1 层），八分之一高度为 2……");
    Note("          绝大多数节点几乎不需要下沉，所以总量是线性的。");
    Note("对比「逐个 Push + sift_up」：那种做法让【每一个】新节点都从最底部往上爬，");
    Note("          而完全二叉树里绝大多数节点都在底部，它们要爬的距离接近 H。");
    Note("          总代价 = Σ(第 i 个节点插入时的树高) ≈ Σ log i = log(n!) = Θ(n log n)。");
    std::cout << "\n";

    // --- (a) 用节点计数验证「高度为 h 的节点数 ≈ n / 2^(h+1)」和级数收敛 ---
    {
        constexpr std::size_t kCountN = 100000;
        std::vector<std::size_t> per_height;
        std::size_t index = 0;
        while (index < kCountN) {
            const std::size_t level_end = 2 * index + 2;  // 这一层结束后的下标
            const std::size_t level_count =
                ((level_end + 1 < kCountN) ? (level_end + 1) : kCountN) - index;
            per_height.push_back(level_count);
            index = level_end + 1;
        }

        Label("    高度 h", 14);
        Label("节点数", 14);
        Label("理论上界 ceil(n/2^(h+1))", 26);
        Label("该层总代价 = h*个数", 22);
        std::cout << "\n" << std::string(80, '-') << "\n";

        std::size_t total_cost = 0;
        for (std::size_t h = 0; h < per_height.size(); ++h) {
            const std::size_t count = per_height[h];
            const double bound = std::ceil(static_cast<double>(kCountN) /
                                           std::pow(2.0, static_cast<double>(h) + 1.0));
            total_cost += h * count;
            std::cout << std::setw(14) << h << std::setw(14) << count << std::setw(26)
                      << static_cast<std::size_t>(bound) << std::setw(22) << (h * count)
                      << "\n";
        }
        std::cout << std::string(80, '-') << "\n";
        std::cout << "    n = " << kCountN << "，Σ h*count = " << total_cost
                  << "，即总下沉步数约 " << static_cast<double>(total_cost) /
                         static_cast<double>(kCountN)
                  << " * n 步 -> O(n)\n";
        std::cout << "    作为对比：n log2(n) = "
                  << static_cast<std::size_t>(static_cast<double>(kCountN) *
                                              std::log2(static_cast<double>(kCountN)))
                  << " 步，实测总步数远小于它。\n";
        Note("    级数收敛的证据：Σ h/2^h 的每一项是 1/2, 2/4, 3/8, 4/16... 前几项和");
        Note("    已经接近 2（1/2 + 1/2 + 3/8 + 1/4 = 1.625），后面越来越小，总和正好 2。");
    }

    // --- (b) 实测：逐个 sift_up 建堆 vs 倒着 sift_down 建堆 ---
    std::cout << "\n";
    {
        constexpr std::size_t kBuildN = 100000;
        constexpr int kBuildRepeats = 3;
        const std::vector<int> build_input = MakeRandomInts(kBuildN, 10000000, 31415u);

        std::uint64_t sum_slow = 0;
        std::uint64_t sum_fast = 0;

        const double t_slow = BenchMedianUs(
            [&] {
                BinaryHeap<int> h;
                h.Reserve(kBuildN);
                // 方案一：逐个插入（每次 sift_up）。这是 Θ(n log n) 的做法。
                for (const int x : build_input) {
                    h.Push(x);
                }
                assert(h.Validate() && h.Size() == kBuildN);
                sum_slow = static_cast<std::uint64_t>(h.Top());
                return h.Size();
            },
            kBuildRepeats);

        const double t_fast = BenchMedianUs(
            [&] {
                // 方案二：先把数据全放进数组（O(n)），再从最后一个非叶节点倒着 sift_down。
                BinaryHeap<int> h = BinaryHeap<int>::FromVector(build_input);
                assert(h.Validate() && h.Size() == kBuildN);
                sum_fast = static_cast<std::uint64_t>(h.Top());
                return h.Size();
            },
            kBuildRepeats);

        assert(sum_slow == sum_fast);  // 两种建堆方式得到的堆顶必须一致

        Label("    逐个 Push（n 次 sift_up）", 36);
        std::cout << ": " << t_slow << " us\n";
        Label("    BuildHeap（倒着 n/2 次 sift_down）", 36);
        std::cout << ": " << t_fast << " us\n";
        Label("    倍数（慢 / 快）", 36);
        std::cout << ": " << ((t_fast > 0.0) ? (t_slow / t_fast) : 0.0) << " x\n";
        std::cout << "    两种方式堆顶都是 " << sum_slow << "（结果一致）\n";
        Note("");
        Note("结论：BuildHeap 明显更快，而且 n 越大差距越明显——因为一个是 O(n)、");
        Note("      一个是 O(n log n)，差的是 log2(n) 约 17 倍这个因子。");
        Note("      Debug 构建下函数调用开销被放大，实测倍数往往比理论值更大。");
        Note("      工程结论：批量数据建堆永远用 std::make_heap / 带范围的构造函数，");
        Note("      不要写 for (...) pq.push(x);。");
    }

    // =======================================================================
    Section("4. 堆排序：O(n log n) 原地，但【不稳定】");
    // =======================================================================
    Note("堆排序 = 反复「把堆顶换到末尾 + 缩小堆 + sift_down」。");
    Note("复杂度 O(n log n)，额外空间 O(1)（不需要归并排序那样的辅助数组）。");
    std::cout << "\n";

    {
        // --- (a) 与 std::sort 对拍 ---
        const std::vector<int> raw = MakeRandomInts(200000, 1000000, 2024u);
        std::vector<int> sorted_by_stl = raw;
        std::sort(sorted_by_stl.begin(), sorted_by_stl.end());

        BinaryHeap<int> sorter = BinaryHeap<int>::FromVector(raw);
        sorter.Sort();
        assert(sorter.Data() == sorted_by_stl);
        std::cout << "  200000 个随机整数：手写堆排序结果与 std::sort 完全一致 -> OK\n";

        // 有重复键的数据也要对拍（重复键最容易暴露实现错误）。
        const std::vector<int> dup = MakeRandomInts(100000, 1000, 555u);
        std::vector<int> dup_stl = dup;
        std::sort(dup_stl.begin(), dup_stl.end());
        BinaryHeap<int> dup_sorter = BinaryHeap<int>::FromVector(dup);
        dup_sorter.Sort();
        assert(dup_sorter.Data() == dup_stl);
        std::cout << "  100000 个「只有 1000 种取值」的重复数据：同样一致 -> OK\n";

        // --- (b) 稳定性：同一份数据，只比较 key，观察 seq 的顺序 ---
        std::vector<int> keys = MakeRandomInts(60, 10, 9u);  // 只有 10 种键，必然大量重复
        {
            // 先构造「稳定排序」的参照：std::stable_sort 保序。
            std::vector<Item> items;
            for (std::size_t i = 0; i < keys.size(); ++i) {
                items.push_back(Item{keys[i], static_cast<int>(i)});
            }
            std::vector<Item> stable = items;
            std::stable_sort(stable.begin(), stable.end(), ItemKeyLess());
            for (std::size_t i = 1; i < stable.size(); ++i) {
                if (stable[i - 1].key == stable[i].key) {
                    assert(stable[i - 1].seq < stable[i].seq);  // 稳定：seq 一定递增
                }
            }
            std::cout << "  参照：std::stable_sort 对相同 key 保持原顺序（assert 通过）\n";
        }

        {
            // 堆排序：直接复用 BinaryHeap 的 Sort，只比较 key。
            std::vector<Item> items;
            for (std::size_t i = 0; i < keys.size(); ++i) {
                items.push_back(Item{keys[i], static_cast<int>(i)});
            }
            BinaryHeap<Item, ItemKeyLess> heap_sorter = BinaryHeap<Item, ItemKeyLess>::FromVector(items);
            heap_sorter.Sort();
            const std::vector<Item>& out = heap_sorter.Data();

            // 先验证它确实按 key 排好序了。
            for (std::size_t i = 1; i < out.size(); ++i) {
                assert(!(out[i].key < out[i - 1].key));
            }

            // 再统计「相同 key 里 seq 出现逆序」的次数：大于 0 就证明不稳定。
            std::size_t inversions = 0;
            std::size_t compared_pairs = 0;
            for (std::size_t i = 0; i < out.size(); ++i) {
                for (std::size_t j = i + 1; j < out.size() && out[j].key == out[i].key; ++j) {
                    compared_pairs += 1;
                    if (out[j].seq < out[i].seq) {
                        inversions += 1;
                    }
                }
            }
            std::cout << "  堆排序：键相同的相邻元素对共 " << compared_pairs
                      << " 对，其中顺序被颠倒的有 " << inversions << " 对\n";
            assert(inversions > 0);  // 堆排序不稳定 —— 这就是可运行的证据
            std::cout << "  -> 堆排序【不稳定】（reverse 对数 > 0），"
                      << "std::sort 也不稳定，"
                      << "需要稳定时用 std::stable_sort\n";
            Note("");
            Note("为什么不稳定：排序时反复做 swap(data_[0], data_[end-1])，");
            Note("  而堆顶的「最大值」和末尾元素在原序列里可能隔着十万八千里，");
            Note("  一次远距离交换就把相同键元素的相对顺序打乱了。");
            Note("  下沉阶段的 swap 也一样：它只在值和值之间做选择，从不看谁原来在前。");
        }

        // --- (c) 耗时对比：堆排序 vs std::sort ---
        std::cout << "\n";
        constexpr int kSortRepeats = 3;
        const double t_stl_sort = BenchMedianUs(
            [&] {
                std::vector<int> buf = raw;
                std::sort(buf.begin(), buf.end());
                return static_cast<std::uint64_t>(buf[buf.size() / 2]);
            },
            kSortRepeats);

        const double t_heap_sort = BenchMedianUs(
            [&] {
                BinaryHeap<int> h = BinaryHeap<int>::FromVector(raw);
                h.Sort();
                return static_cast<std::uint64_t>(h.Data()[h.Size() / 2]);
            },
            kSortRepeats);

        Label("    std::sort（内省排序）", 34);
        std::cout << ": " << t_stl_sort << " us\n";
        Label("    手写堆排序", 34);
        std::cout << ": " << t_heap_sort << " us\n";
        Label("    倍数", 34);
        std::cout << ": " << ((t_stl_sort > 0.0) ? (t_heap_sort / t_stl_sort) : 0.0) << " x\n";
        Note("");
        Note("结论：同样的 O(n log n)，std::sort 快得多。原因是常数因子：");
        Note("  - std::sort 用内省排序（快排 + 堆排兜底 + 小区间插入排序），");
        Note("    顺序访问数组，缓存命中率高，分支预测友好；");
        Note("  - 堆排序每次比较都要跳着访问 data_[2i+1] / data_[2i+2]，");
        Note("    是典型的「指针追逐式」访问，缓存不友好，而且分支难预测。");
        Note("  这是「复杂度相同 != 性能相同」的又一个例子。");
        Note("  堆排序的真正价值在于 O(1) 额外空间 + O(n log n) 最坏情况保证（实时系统用）。");
    }

    // =======================================================================
    Section("5. std::priority_queue：默认大顶堆与 std::greater 小顶堆");
    // =======================================================================
    Note("priority_queue 是【容器适配器】，不是容器：它内部默认用一个 std::vector");
    Note("  当存储，用堆算法维护顺序，对外只暴露 top / push / pop。");
    Note("  所以它没有迭代器、不能遍历、不能随机访问——因为你只该关心最值。");
    std::cout << "\n";

    {
        const std::vector<int> nums = {3, 1, 4, 1, 5, 9, 2, 6};

        std::priority_queue<int> max_pq;  // 默认大顶堆
        for (const int x : nums) {
            max_pq.push(x);
        }
        std::cout << "  默认（大顶堆）弹出顺序: ";
        while (!max_pq.empty()) {
            std::cout << max_pq.top() << " ";
            max_pq.pop();
        }
        std::cout << "\n";

        std::priority_queue<int, std::vector<int>, std::greater<int>> min_pq;  // 小顶堆
        for (const int x : nums) {
            min_pq.push(x);
        }
        std::cout << "  std::greater（小顶堆）: ";
        while (!min_pq.empty()) {
            std::cout << min_pq.top() << " ";
            min_pq.pop();
        }
        std::cout << "\n";

        Note("");
        Note("写法拆解：std::priority_queue<T, Container, Compare>");
        Note("  T        = 元素类型；");
        Note("  Container= 底层容器，默认 std::vector<T>，必须支持随机访问；");
        Note("  Compare  = 比较器，默认 std::less<T>。");
        Note("  想变成小顶堆就必须写全三个模板参数，因为 C++ 不允许「只指定第三个」。");
    }

    // =======================================================================
    Section("6. 最经典的坑：priority_queue 的比较器方向与 sort 相反");
    // =======================================================================
    Note("先说结论（背下来）：");
    Note("  std::sort 的 comp(a, b) 返回 true 表示【a 应该排在 b 前面】；");
    Note("  std::priority_queue 的 comp(a, b) 返回 true 表示【a 的优先级低于 b】，");
    Note("  也就是 a 应该更晚被 top() 弹出、在堆里排得更靠后。");
    Note("  两者的方向【完全相反】：priority_queue 认为 comp 为 true 的那一对，");
    Note("  a 得「让位」。");
    std::cout << "\n";

    {
        const std::vector<int> nums = {3, 1, 4, 1, 5, 9, 2, 6};

        std::vector<int> sorted_less = nums;
        std::sort(sorted_less.begin(), sorted_less.end(), std::less<int>());
        std::vector<int> sorted_greater = nums;
        std::sort(sorted_greater.begin(), sorted_greater.end(), std::greater<int>());

        std::priority_queue<int, std::vector<int>, std::less<int>> pq_less;
        std::priority_queue<int, std::vector<int>, std::greater<int>> pq_greater;
        for (const int x : nums) {
            pq_less.push(x);
            pq_greater.push(x);
        }
        std::vector<int> pop_less;
        std::vector<int> pop_greater;
        while (!pq_less.empty()) {
            pop_less.push_back(pq_less.top());
            pq_less.pop();
        }
        while (!pq_greater.empty()) {
            pop_greater.push_back(pq_greater.top());
            pq_greater.pop();
        }

        Label("    std::sort + std::less", 30);
        std::cout << ": ";
        PrintIntsHead(sorted_less, sorted_less.size());
        std::cout << "\n";
        Label("    priority_queue + less", 30);
        std::cout << ": ";
        PrintIntsHead(pop_less, pop_less.size());
        std::cout << "   <- 方向相反\n";
        Label("    std::sort + std::greater", 30);
        std::cout << ": ";
        PrintIntsHead(sorted_greater, sorted_greater.size());
        std::cout << "\n";
        Label("    priority_queue + greater", 30);
        std::cout << ": ";
        PrintIntsHead(pop_greater, pop_greater.size());
        std::cout << "   <- 方向相反\n";

        assert(sorted_less.front() < sorted_less.back());      // sort+less  -> 升序
        assert(pop_less.front() > pop_less.back());            // pq+less    -> 降序
        assert(sorted_greater.front() > sorted_greater.back()); // sort+greater -> 降序
        assert(pop_greater.front() < pop_greater.back());       // pq+greater  -> 升序
        Note("");
        Note("同一个 std::less<int>：sort 得到升序，priority_queue 得到「弹出降序」。");
        Note("同一个 std::greater<int>：sort 得到降序，priority_queue 得到「弹出升序」。");
        Note("记法：priority_queue 要的是「谁更不重要」，而 sort 要的是「谁排前面」。");
        Note("     默认 less 让「大」的元素更优先，所以默认是大顶堆——这才是它的直觉来源。");
    }

    // --- 自定义比较器：解决「比较依据不是元素本身」的问题（比 second 排序）---
    std::cout << "\n";
    {
        using Pair = std::pair<int, int>;
        const std::vector<Pair> pairs = {{1, 5}, {2, 3}, {3, 9}, {4, 1}, {5, 7}};

        // 目标：按 second 升序弹出。也就是「second 小的优先级高」= 小顶堆语义。
        // comp(a, b) 必须返回「a 是不是比 b 更晚被弹出」，即 a 的 second 更大时返回 true。
        struct BySecondDescending {
            bool operator()(const Pair& a, const Pair& b) const {
                return a.second > b.second;  // a 的 second 更大 -> a 优先级更低（更晚弹出）
            }
        };

        std::priority_queue<Pair, std::vector<Pair>, BySecondDescending> pq;
        for (const Pair& p : pairs) {
            pq.push(p);
        }
        std::cout << "  按 second 升序弹出（自定义比较器）: ";
        while (!pq.empty()) {
            std::cout << "(" << pq.top().first << "," << pq.top().second << ") ";
            pq.pop();
        }
        std::cout << "\n";

        // 同样的意图用 sort 写，比较器方向正好相反：second 小的排前面 -> a.second < b.second。
        std::vector<Pair> sorted = pairs;
        std::sort(sorted.begin(), sorted.end(),
                  [](const Pair& a, const Pair& b) { return a.second < b.second; });
        std::cout << "  用 std::sort 做同一件事（注意比较器写的是 <）: ";
        for (const Pair& p : sorted) {
            std::cout << "(" << p.first << "," << p.second << ") ";
        }
        std::cout << "\n";
        assert(sorted.front().first == 4 && sorted.back().first == 3);
        Note("");
        Note("对照着看：同样是「按 second 升序」，");
        Note("  std::sort 的比较器写 a.second < b.second ;");
        Note("  priority_queue 的比较器写 a.second > b.second 。");
        Note("  凡是要给 priority_queue 写比较器，先在心里问一句：");
        Note("  「comp 返回 true 的时候，a 应该更早还是更晚被 top() 取出来？」");
    }

    // --- 自定义优先级：结构体 + 运行期优先级（真实工程写法）---
    std::cout << "\n";
    {
        struct Task {
            int priority = 0;  // 约定：数字越小越紧急
            int id = 0;
        };
        // 陷阱提醒：priority_queue 的比较器必须满足严格弱序，而且【不能捕获运行期状态】，
        // 因为它被容器按值保存了一份。想让优先级依赖运行期数据，只能写进元素里
        // （就像这里的 Task::priority），不要试图在比较器里用外部变量。
        struct TaskLess {
            bool operator()(const Task& a, const Task& b) const {
                return a.priority > b.priority;  // 数字大的优先级更低 -> 数字小的先出队
            }
        };

        std::priority_queue<Task, std::vector<Task>, TaskLess> tasks;
        tasks.push(Task{5, 100});
        tasks.push(Task{1, 101});
        tasks.push(Task{3, 102});
        tasks.push(Task{1, 103});
        std::cout << "  任务调度（priority 小 = 更紧急）出队顺序: ";
        while (!tasks.empty()) {
            const Task t = tasks.top();
            tasks.pop();
            std::cout << "task" << t.id << "(pri=" << t.priority << ") ";
        }
        std::cout << "\n";
        Note("注意输出的前两个是 pri=1 的 task101 和 task103：它们优先级相同，");
        Note("  谁先出来【不确定】（堆排序不稳定），所以关键业务不能依赖这个顺序——");
        Note("  要稳定就用 (priority, 到达时间) 组成复合键一起比较。");
    }

    // =======================================================================
    Section("7. 场景一：Top-K 三方案实测对比");
    // =======================================================================
    Note("任务：从 n 个数里取出最大的 K 个。三种做法的复杂度：");
    Note("  (a) 全排序后取前 K      O(n log n)  时间、O(n) 空间");
    Note("  (b) 大小 K 的小顶堆      O(n log K)  时间、O(K) 空间");
    Note("  (c) std::nth_element    平均 O(n)    时间、O(1) 额外空间（原地）");
    std::cout << "\n";

    {
        constexpr std::size_t kTopN = 200000;
        constexpr int kTopRepeats = 3;
        const std::vector<int> pool = MakeRandomInts(kTopN, 10000000, 8888u);

        auto run_with_k = [&](std::size_t k) {
            const std::vector<int> by_sort = TopKByFullSort(pool, k);
            const std::vector<int> by_heap = TopKByMinHeap(pool, k);
            const std::vector<int> by_nth = TopKByNthElement(pool, k);

            // --- 正确性：三种方案取到的【集合】必须完全一致 ---
            std::vector<int> a = by_sort;
            std::vector<int> b = by_heap;
            std::vector<int> c = by_nth;
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            std::sort(c.begin(), c.end());
            assert(a == b);
            assert(a == c);

            const double t_full = BenchMedianUs(
                [&] {
                    const std::vector<int> r = TopKByFullSort(pool, k);
                    return static_cast<std::uint64_t>(r.front());
                },
                kTopRepeats);
            const double t_heap = BenchMedianUs(
                [&] {
                    const std::vector<int> r = TopKByMinHeap(pool, k);
                    return static_cast<std::uint64_t>(r.front());
                },
                kTopRepeats);
            const double t_nth = BenchMedianUs(
                [&] {
                    const std::vector<int> r = TopKByNthElement(pool, k);
                    return static_cast<std::uint64_t>(r.front());
                },
                kTopRepeats);

            std::cout << "  K = " << k << "（n = " << kTopN
                      << "）：三方案结果集合一致 -> OK\n";
            Label("    (a) 全排序后取前 K", 30);
            std::cout << ": " << t_full << " us\n";
            Label("    (b) 大小 K 的小顶堆", 30);
            std::cout << ": " << t_heap << " us\n";
            Label("    (c) nth_element", 30);
            std::cout << ": " << t_nth << " us\n";
            if (t_heap > 0.0 && t_full > 0.0) {
                Label("    全排序 / 最快方案", 30);
                std::cout << ": " << (t_full / ((t_heap < t_nth) ? t_heap : t_nth))
                          << " x（越大说明全排序越亏）\n";
            }
            std::cout << "\n";
        };

        run_with_k(10);
        run_with_k(1000);
        run_with_k(100000);  // K 接近 n，看趋势怎么反转

        Note("读表结论：");
        Note("  K = 10 时小顶堆和 nth_element 都远快于全排序（我们把 n 个元素全排了，");
        Note("  却只用到了前 10 个）。K = 1000 时优势缩小（log K 变大）。");
        Note("  K = 100000（n 的一半）时全排序反而不亏了：因为 log K 已经接近 log n，");
        Note("  而堆方案还要多出 n 次「和堆顶比较」的开销。");
        Note("");
        Note("怎么选：");
        Note("  - K 远小于 n，且数据是【流式到达】的（无法二次访问）-> 小顶堆，");
        Note("    这也是唯一能用在数据流上的方案，空间只要 O(K)；");
        Note("  - K 远小于 n，且数据一次性都在内存里 -> nth_element（平均最快）；");
        Note("  - K 接近 n，或者你顺便还需要整体有序 -> 直接全排序；");
        Note("  - 面试里最常考的是小顶堆方案的 O(n log K)，因为它同时体现了堆的用法和空间意识。");
    }

    // =======================================================================
    Section("8. 场景二：K 路归并（合并 K 个有序链表）");
    // =======================================================================
    Note("用一个小顶堆放「每一路的当前游标」，每次取出最小者，再把该路的下一格放回去。");
    Note("堆里永远只有 K 个元素，所以每次堆操作 O(log K)，总复杂度 O(N log K)。");
    Note("对照：把所有元素收集到一个 vector 再排序是 O(N log N)；K 远小于 N 时明显更差。");
    std::cout << "\n";

    {
        constexpr std::size_t kListCount = 8;
        constexpr std::size_t kPerList = 5000;
        constexpr std::size_t kTotal = kListCount * kPerList;

        constexpr int kMergeRepeats = 3;

        // 每一路内部先排好序（这是 K 路归并的前提）。
        std::vector<std::vector<int>> sorted_parts;
        std::vector<int> everything;
        everything.reserve(kTotal);
        for (std::size_t li = 0; li < kListCount; ++li) {
            std::vector<int> one = MakeRandomInts(kPerList, 1000000, static_cast<unsigned>(900 + li));
            std::sort(one.begin(), one.end());
            everything.insert(everything.end(), one.begin(), one.end());
            sorted_parts.push_back(std::move(one));
        }

        // 参照答案：全部元素排序。
        std::vector<int> expected = everything;
        std::sort(expected.begin(), expected.end());

        // 【一个真实的测量陷阱，值得单独记住】
        // MergeKSortedLists 是【破坏性】的：它不复刻节点，而是把输入链表的节点直接
        // 重新串成一条新链；测完我们又会 FreeList 掉整条结果链。
        // 所以同一批链表只能归并一次。如果像普通函数那样让计时器重复调用 3 次，
        // 第 2 次就会去解引用已经 delete 掉的节点——实测直接 access violation 崩溃
        // （0xC0000005），而且崩得毫无提示。
        // 正确做法：给每一轮预先把输入建好，把构造成本留在计时之外，
        // 这样每次测的都是「纯粹的归并」。
        std::vector<std::vector<ListNode*>> rounds;
        rounds.reserve(static_cast<std::size_t>(kMergeRepeats));
        for (int r = 0; r < kMergeRepeats; ++r) {
            std::vector<ListNode*> ls;
            ls.reserve(sorted_parts.size());
            for (const std::vector<int>& part : sorted_parts) {
                ls.push_back(BuildList(part));
            }
            rounds.push_back(std::move(ls));
        }

        std::size_t round = 0;
        const double t_merge = BenchMedianUs(
            [&] {
                // 每一轮取一套全新的链表；归并会消费它，随后由 FreeList 统一释放，
                // 所以这里不需要（也绝不能）再对输入做第二次释放。
                std::vector<ListNode*>& ls = rounds[round];
                round += 1;
                ListNode* merged = MergeKSortedLists(ls);
                std::uint64_t acc = 0;
                std::size_t count = 0;
                for (ListNode* p = merged; p != nullptr; p = p->next) {
                    acc += static_cast<std::uint64_t>(p->value);
                    count += 1;
                }
                assert(count == kTotal);
                // 顺手验证归并结果确实有序。
                ListNode* prev = merged;
                for (ListNode* p = (merged == nullptr ? nullptr : merged->next); p != nullptr;
                     p = p->next) {
                    assert(prev->value <= p->value);
                    prev = p;
                }
                FreeList(merged);
                return acc;
            },
            kMergeRepeats);

        std::vector<int> flat = everything;
        const double t_flat_sort = BenchMedianUs(
            [&] {
                std::vector<int> buf = flat;
                std::sort(buf.begin(), buf.end());
                std::uint64_t acc = 0;
                for (const int x : buf) {
                    acc += static_cast<std::uint64_t>(x);
                }
                assert(buf == expected);
                return acc;
            },
            3);

        std::cout << "  K = " << kListCount << " 路，每路 " << kPerList << " 个，总计 N = "
                  << kTotal << "\n";
        Label("    priority_queue K 路归并 O(N log K)", 40);
        std::cout << ": " << t_merge << " us（结果与排序一致，且有序）\n";
        Label("    收集到 vector 后 std::sort O(N log N)", 40);
        std::cout << ": " << t_flat_sort << " us\n";
        Label("    倍数", 40);
        std::cout << ": " << ((t_merge > 0.0) ? (t_flat_sort / t_merge) : 0.0) << " x\n";
        Note("");
        Note("这里 K = 8 还太小，log K = 3 对 log N = 17，理论上归并应该快好几倍；");
        Note("但归并方案有大量【指针追逐】（链表节点散落在堆上），缓存不友好，");
        Note("而 std::sort 顺着一块连续内存跑，常数因子小得多——所以实测可能打平甚至落后。");
        Note("这就是「复杂度只是起点」的又一个例子：数据结构的内存布局经常比增长阶更能决定胜负。");
        Note("K 越大（比如 K = 1000 路日志文件归并），归并方案的复杂度优势才会真正体现出来。");

        // 注意：这里【不需要】再释放输入链表。
        // 所有节点都已经随 merged 一起被 FreeList 释放过了；
        // 再释放一次就是 double free，同样是崩溃。
    }

    // =======================================================================
    Section("9. 场景三：数据流中位数（双堆法）");
    // =======================================================================
    Note("两个堆把数据从中间劈开：");
    Note("  low_ ：大顶堆，存较小的一半，堆顶是这一半的最大值（中位数下界）；");
    Note("  high_：小顶堆，存较大的一半，堆顶是这一半的最小值（中位数上界）。");
    Note("平衡规则：|low_.size() - high_.size()| <= 1，且约定 low_.size() >= high_.size()。");
    Note("插入 O(log n)（最多两次堆操作），取中位数 O(1)。");
    std::cout << "\n";

    {
        constexpr std::size_t kStreamN = 20000;
        const std::vector<int> stream = MakeRandomInts(kStreamN, 1000000, 12321u);

        // 参照答案：把所有元素排好序，随时能算出真正的中位数（离线做法，O(n log n)）。
        std::vector<int> reference = stream;
        std::sort(reference.begin(), reference.end());

        MedianFinder finder;
        const double t_median_stream = BenchMedianUs(
            [&] {
                finder = MedianFinder();
                double last = 0.0;
                for (std::size_t i = 0; i < stream.size(); ++i) {
                    finder.Add(stream[i]);
                    // 每插入一个就取一次中位数（这是流式场景的核心需求）。
                    last = finder.Median();
                }
                std::cout << "    流式插入 " << kStreamN << " 个元素后中位数 = " << last << "\n";
                return static_cast<std::uint64_t>(last);
            },
            1);

        // 逐点校验：每个前缀的中位数都必须与「把该前缀单独排序」的离线答案一致。
        //
        // 【这里有个很容易踩的验证陷阱】
        // 不能拿「全局排序结果的头部」去当某个前缀的答案：
        // reference 的前 i+1 个元素是【全局最小的 i+1 个数】，
        // 而流的前 i+1 个元素是【最早到达的 i+1 个数】——两者是完全不同的集合。
        // 第一版就是这么写错的，结果 assert 在中间某个前缀上失败，
        // 看起来像 MedianFinder 有 bug，实际是参照答案本身错了。
        // 教训：对拍时，参照实现必须和被测实现【输入完全一致】。
        //
        // 逐点校验的参照代价是 O(n^2 log n)，所以只在小规模上做。
        {
            constexpr std::size_t kCheckN = 2000;
            MedianFinder check_finder;
            bool all_ok = true;
            for (std::size_t i = 0; i < kCheckN; ++i) {
                check_finder.Add(stream[i]);
                // 关键：拷贝【流的前缀】，然后对这个前缀自己排序。
                std::vector<int> prefix(stream.begin(),
                                        stream.begin() + static_cast<std::ptrdiff_t>(i + 1));
                std::sort(prefix.begin(), prefix.end());
                const std::size_t m = prefix.size();
                double expected_median = 0.0;
                if (m % 2 == 1) {
                    expected_median = static_cast<double>(prefix[m / 2]);
                } else {
                    expected_median = (static_cast<double>(prefix[m / 2 - 1]) +
                                       static_cast<double>(prefix[m / 2])) / 2.0;
                }
                if (std::fabs(check_finder.Median() - expected_median) > 1e-9) {
                    all_ok = false;
                    break;
                }
            }
            assert(all_ok);
            assert(check_finder.Size() == kCheckN);
            std::cout << "  逐点校验：前 " << kCheckN
                      << " 个前缀的中位数全部与「把该前缀单独排序」的结果一致 -> OK\n";
        }

        // 全量校验：当处理完全部元素时，「流的前缀」就是整个 stream，
        // 此时全局排序结果才真正等于前缀的排序结果，可以直接用 reference。
        MedianFinder tail_finder;
        for (std::size_t i = 0; i < kStreamN; ++i) {
            tail_finder.Add(stream[i]);
        }
        {
            const std::size_t m = reference.size();
            const double final_expected =
                (m % 2 == 1)
                    ? static_cast<double>(reference[m / 2])
                    : (static_cast<double>(reference[m / 2 - 1]) +
                       static_cast<double>(reference[m / 2])) / 2.0;
            assert(std::fabs(tail_finder.Median() - final_expected) < 1e-9);
            assert(tail_finder.Size() == kStreamN);
            std::cout << "  全量校验：" << kStreamN
                      << " 个元素全部插入后的中位数与排序结果一致 -> OK\n";
        }

        Label("    双堆：插入 n 个 + 每次取中位数", 40);
        std::cout << ": " << t_median_stream << " us\n";

        // 对照：每个前缀都重新排序（真·暴力），复杂度 O(n^2 log n)。
        constexpr std::size_t kBruteN = 2000;
        const double t_brute = BenchMedianUs(
            [&] {
                std::vector<int> buf;
                buf.reserve(kBruteN);
                std::uint64_t acc = 0;
                for (std::size_t i = 0; i < kBruteN; ++i) {
                    buf.push_back(reference[i]);
                    std::sort(buf.begin(), buf.end());
                    acc += static_cast<std::uint64_t>(buf[buf.size() / 2]);
                }
                return acc;
            },
            1);

        Label("    暴力：每个前缀都重新排序", 40);
        std::cout << ": " << t_brute << " us（只跑 " << kBruteN << " 个元素）\n";
        Note("");
        Note("对比要点：暴力做法每来一个元素就重排一次，总体是 O(n^2 log n)；");
        Note("双堆法总代价 O(n log n)，而且【取中位数是 O(1)】——在数据流场景里");
        Note("中位数的查询次数可能远多于插入次数，这个 O(1) 才是关键。");
        Note("实测里暴力只跑了 1/10 的数据量，耗时却已经明显更高，趋势一目了然。");
        Note("");

        // 边界情况演示：有序输入（最坏情况也必须是 O(log n)）
        MedianFinder ascending;
        ascending.Add(1);
        assert(ascending.Median() == 1.0);
        ascending.Add(2);
        assert(std::fabs(ascending.Median() - 1.5) < 1e-9);  // 偶数个 -> 两堆顶平均
        ascending.Add(3);
        assert(ascending.Median() == 2.0);
        ascending.Add(4);
        assert(std::fabs(ascending.Median() - 2.5) < 1e-9);
        ascending.Add(5);
        assert(ascending.Median() == 3.0);
        std::cout << "  有序输入 1..5 的中位数序列: 1, 1.5, 2, 2.5, 3（assert 全部通过）\n";
        Note("注意偶数个元素时中位数是「两个堆顶的平均值」，所以返回值类型是 double。");
        Note("如果业务想要「两个中间值里较小的那个」，直接返回 low_.top() 即可。");
    }

    // =======================================================================
    Section("10. 总结：什么时候用堆");
    // =======================================================================
    Note("堆的定位：只关心【最值】，不关心整体有序，也不关心任意元素查找。");
    std::cout << "\n";
    Label("    需求", 34);
    Label("该用什么", 30);
    std::cout << "\n" << std::string(70, '-') << "\n";

    Label("    反复取最小值（合并、调度、Dijkstra）", 34);
    Label("std::priority_queue", 30);
    std::cout << "\n";
    Label("    只要 Top-K，K 远小于 n", 34);
    Label("大小 K 的堆 / nth_element", 30);
    std::cout << "\n";
    Label("    流式数据的中位数 / 分位数", 34);
    Label("双堆（大顶堆 + 小顶堆）", 30);
    std::cout << "\n";
    Label("    需要整体有序", 34);
    Label("std::sort（不要用堆排序）", 30);
    std::cout << "\n";
    Label("    需要任意元素查找", 34);
    Label("std::map / std::unordered_map", 30);
    std::cout << "\n";
    Label("    需要在堆中修改/删除任意元素", 34);
    Label("std::set（标准库堆不支持）", 30);
    std::cout << "\n" << std::string(70, '-') << "\n";
    Note("");
    Note("最后一条特别重要：堆【不能】高效地删除或修改任意一个元素，");
    Note("  因为堆里没有「元素 -> 下标」的反查表。手写的话可以用「索引堆」");
    Note("  （额外维护 position 数组）解决，标准库的 priority_queue 做不到，");
    Note("  这种需求要用 std::set / std::multiset 或者自己写索引堆。");
    Note("");
    Note("本文件所有实现均为教学用途，生产请用 std::priority_queue、");
    Note("  std::make_heap / std::push_heap / std::pop_heap / std::sort_heap、");
    Note("  以及 std::nth_element。");

    std::cout << "\n[校验] 累加器非零，说明所有测量结果都真实参与了运算: "
              << (g_sink != 0 ? "是" : "否") << "\n";

    return 0;
}
