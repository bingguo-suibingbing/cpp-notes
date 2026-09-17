# 第 05 章 · 标准库 STL —— 实际工程项目里用得最多的部分

> 环境：Visual Studio 18 Community / MSVC 14.51，`/std:c++20`，`/W4 /WX /permissive- /utf-8`
> 构建：在 `cpp-notes` 目录下执行 `.\build.ps1 -Chapter 05-stl -WX`
> 约定：**一个 `.cpp` = 一个独立可执行文件**，每个文件自带 `int main()`，不写头文件/实现分离，**不使用 `using namespace std;`**（一律写 `std::`）。

---

## 1. 本章地图

| 文件 | 主题 | 一句话结论 |
|---|---|---|
| `01_cstdio.cpp` | `<cstdio>` printf/scanf/文件读写 | `printf` 的 `%d`/`%lld`/`%zu` 必须与实参类型严格匹配；`scanf` 的返回值是「成功赋值项数」；清行必须同时处理 `'\n'` 和 `EOF` |
| `02_cstdlib.cpp` | `<cstdlib>` 随机数/内存/转换/排序 | `rand()` 应该被淘汰（范围小、有取模偏置）；`atoi` 无法报告错误；`std::sort` 全面优于 `qsort`；`from_chars` 是 C++17 起的正解 |
| `03_cstring_and_memory.cpp` | `<cstring>` 字符串与内存 | `strtok` 改原串且有内部静态状态，新代码改用 `string_view` 切分；`memcpy` 只对平凡可拷贝类型成立，对 `std::string` 用是 UB |
| `04_cmath.cpp` | `<cmath>` 数学函数 | 三角函数只认弧度；`acos` 前必须 clamp；浮点判等必须用容差；平方写 `x*x` 而不是 `pow(x,2)`（实测快 33 倍） |
| `05_cctype_and_ctime.cpp` | `<cctype>` + `<ctime>`/`<chrono>` | ctype 参数必须先转 `unsigned char`（否则是 UB）；`tm_year` 要 +1900、`tm_mon` 要 +1；C++20 的 `year_month_day` 不再需要这些偏移 |
| `06_cpp_string.cpp` | `std::string` / `string_view` / `format` | SSO 容量 15；`reserve` 实测快 20 倍；`from_chars` 比 `stoi` 快 16 倍；`string_view` 零拷贝但绝不能指向临时对象 |
| `07_sequence_containers.cpp` | `vector` / `array` / `deque` / `list` | 默认用 `vector`；实测 `vector` 遍历比 `list` 快 91 倍、中间插入快 176 倍；`vector<bool>` 不是容器 |
| `08_associative_containers.cpp` | `map` / `set` / `unordered_*` | 要排序或范围查询用 `map`，只要快速查找用 `unordered_map`；`operator[]` 会插入默认值这个坑必须记住 |
| `09_container_adaptors_and_algorithms.cpp` | `stack`/`queue`/`priority_queue` + `pair`/`tuple` + 算法串讲 | 适配器没有迭代器；`priority_queue` 默认大顶堆；`remove` 不删元素；`accumulate` 的初值类型决定结果类型 |
| `10_iostream_and_files.cpp` | `iostream` + 文件 + `filesystem` | `cin` 失败后必须 `clear()` + `ignore()`；用 `'\n'` 而不是 `std::endl`（实测快 3 倍）；文件流是 RAII 的 |

---

## 2. 容器选型决策表（本章最有工程价值的部分）

### 2.1 第一步：按「需要什么操作」查表

| 你的需求 | 该用 | 关键复杂度 | 备注 |
|---|---|---|---|
| 顺序访问 + 尾部增删 | `std::vector` | 尾部均摊 O(1)，随机访问 O(1) | **默认选择，90% 场景都是它** |
| 顺序访问 + 两端增删 | `std::deque` | 两端 O(1)，随机访问 O(1) | 两端操作的引用/指针稳定，迭代器不稳定 |
| 定长、编译期已知大小 | `std::array` | 全部 O(1) | 栈上零开销，可拷贝、可按值传 |
| 频繁在中间插入/删除，且手持迭代器 | `std::list` | 插入/删除 O(1)，定位 O(n) | **实测通常比 vector 慢**，理由见 §4.2 |
| 只要头部的单向遍历、内存极紧 | `std::forward_list` | 同上 | 没有 `size()`、没有 `push_back` |
| 去重 + 有序 | `std::set` | 插入/查找 O(log n) | 迭代器稳定 |
| 去重、不要求有序 | `std::unordered_set` | 平均 O(1)，最坏 O(n) | rehash 时迭代器全失效 |
| key → value + 有序遍历 | `std::map` | O(log n) | 支持 `lower_bound` 范围查询 |
| key → value + 范围查询 | `std::map` | O(log n) | 哈希容器做不到范围查询 |
| key → value + 只要快速查找 | `std::unordered_map` | 平均 O(1)，最坏 O(n) | 元素个数已知时先 `reserve` |
| 允许重复 key（有序） | `std::multimap` | O(log n) | 用 `equal_range` 取全部同 key 值；**没有 `operator[]`** |
| 允许重复 key（无序） | `std::unordered_multimap` | 平均 O(1) | 1 对多场景 |
| LIFO | `std::stack` | 全部 O(1) | 无迭代器 |
| FIFO | `std::queue` | 全部 O(1) | 无迭代器 |
| 每次取最大/最小 | `std::priority_queue` | push/pop O(log n)，top O(1) | 默认大顶堆；小顶堆传 `std::greater<>` |
| 元素很少（< 50）又不常改 | 排序的 `std::vector` | 查找 O(log n) | 内存连续、无节点开销，实测常常更快 |
| 小整数连续 key（0..N） | `std::vector<T>` | O(1) | 直接用下标，零哈希开销 |

### 2.2 第二步：四个问题帮你定下来

```
Q1  需要「按 key 有序」的输出吗？
        需要 ──────────────► map / set
        不需要 ────► Q2
Q2  需要「范围查询」（lower_bound / upper_bound / 区间统计）吗？
        需要 ──────────────► map / set      （哈希容器根本做不到）
        不需要 ────► Q3
Q3  key 类型有「质量好、稳定」的哈希吗？
        有（int / string / 自己写好的特化）──► unordered_map / unordered_set
        没有，或 key 来自不可信输入       ──► map / set（防 HashDoS）
Q4  需要「插入后已有迭代器依然有效」吗？
        需要 ──────────────► map / set      （unordered 的 rehash 会让迭代器全失效）
```

### 2.3 第三步：复杂度与迭代器失效对照

| 操作 | `map` / `set` | `unordered_map` / `unordered_set` |
|---|---|---|
| 查找 `find` | O(log n) | 平均 O(1)，**最坏 O(n)** |
| 插入 `insert` | O(log n) | 平均 O(1)，最坏 O(n) |
| 删除 `erase` | O(log n) | 平均 O(1)，最坏 O(n) |
| 范围查询 | O(log n) + k | **不支持** |
| 有序遍历 | O(n)，天然有序 | 无序（要排序得额外 O(n log n)） |
| 插入后迭代器 | 全部有效 | **rehash 则全部失效** |
| 删除后迭代器 | 只有被删的那个失效 | 只有被删的那个失效 |
| 插入后指针/引用 | 全部有效 | 全部有效（rehash 也不失效） |
| 每元素内存 | 3 个指针 + 颜色位 | 1 个指针 + 桶数组 |
| 额外内存 | 无 | 桶数组（通常 size 的 1~2 倍） |
| 元素顺序 | 按 key 排序 | 实现相关，**换个编译器就变** |

### 2.4 序列容器对照（含实测）

| | `vector` | `deque` | `list` | `array` |
|---|---|---|---|---|
| 内存布局 | 单块连续 | 分块 + 索引表 | 每节点独立 malloc | 单块连续（栈上） |
| 随机访问 | O(1) 最快 | O(1) 两次间接 | **不支持** | O(1) 最快 |
| 头部插入/删除 | O(n) | **O(1)** | O(1)（需先定位） | 不适用 |
| 尾部插入/删除 | 均摊 O(1) | O(1) | O(1) | 不适用 |
| 中间插入/删除 | O(n) 搬移 | O(n) | O(1) + 定位 O(n) | 不适用 |
| 遍历 10 万 int | 基准 | 约 3~9 倍 | **约 25~91 倍** | 同 vector |
| 每元素额外开销 | 0 | 少量（块内连续） | 16 字节指针 + 堆块头 | 0 |
| 迭代器失效 | 扩容全失效 | 两端操作后全失效 | 只影响被删元素 | 不适用 |
| 引用/指针失效 | 扩容全失效 | **两端操作后仍有效** | 只影响被删元素 | 不适用 |
| 与 C 数组互操作 | `data()` 保证连续 | 不保证 | 不保证 | `data()` 保证连续 |

---

## 3. `vector` 专项

### 3.1 扩容策略

实测（`07_sequence_containers.cpp` 第 1 节）：从空 vector 开始逐个 `push_back`，capacity 的变化点是

```
初始               capacity = 0
size=1             capacity -> 1      （第一次分配）
size=2             capacity -> 2
size=3             capacity -> 3
size=4             capacity -> 4
size=5             capacity -> 6
size=7             capacity -> 9
size=10            capacity -> 13     （增长比例 1.44）
size=14            capacity -> 19     （1.46）
size=20            capacity -> 28     （1.47）
size=29            capacity -> 42     （1.50）
...
```

结论：

1. **按倍数增长**，不是每次 `+1`。MSVC 的 `vector` 是**约 1.5 倍**，libstdc++ 是 2 倍。
2. 增长比例逐渐收敛到 1.5，说明实现用的是「`old_capacity * 3 / 2`」这类策略。
3. 元素大小会影响单次扩容的字节数，但**不影响增长策略**。

### 3.2 均摊复杂度 O(1) 的证明思路

设增长因子为 k（k > 1），第 i 次扩容发生在 size 达到 `k^i` 时，搬移 `k^i` 个元素。

n 次 `push_back` 的总搬移次数是等比级数：

```
1 + k + k² + ... + k^m   （其中 k^m ≤ n）
= (k^(m+1) - 1) / (k - 1)
≈ k·n / (k - 1)
= O(n)
```

所以 n 次操作总共搬移 O(n) 次，**平均每次 O(1)**，即「均摊 O(1)」。

> 关键点：均摊 O(1) **不等于**每次都是 O(1)。扩容那一次的代价是 O(n)（分配新块 + 逐元素搬移 + 释放旧块）。

### 3.3 `reserve` 的使用时机

**该 reserve 的场景：**

- 已知或能估算最终元素个数（读文件前先 `stat` 拿行数、协议头里有 length 字段）
- 在对延迟敏感的代码里（游戏帧循环、交易撮合、音视频处理）——reserve 消掉的是**延迟尖峰**
- 要在容器里**保存指针/引用/迭代器**，且希望它们在插入过程中保持有效

**实测数据**（`07_sequence_containers.cpp`）：

| 场景 | 不 reserve | 先 reserve | 差距 |
|---|---|---|---|
| `push_back` 100 万个 `int` | 18924 us | 15650 us | 1.2 倍 |
| `+=` 分 20000 次追加、每次 32 字节（`std::string`） | 694 us | 34 us | **20.4 倍** |

**为什么 int 的差距只有 1.2 倍，而 string 有 20 倍？**
`int` 的搬移就是 `memcpy`，受内存带宽约束，很快；`std::string` 的搬移要逐个调用移动构造、修改指针、可能涉及分配，成本高得多。

**不该 reserve 的场景：**

- 元素个数完全未知且很少（比如只有 3~5 个）——多一次 `malloc` 反而更慢
- 一次性用 `vector` 的区间构造函数（`vector<T> v(first, last)`）——它在内部已经预留好了

### 3.4 迭代器失效的三种情况

| 操作 | 失效范围 | 原因 |
|---|---|---|
| **扩容**（`push_back` / `insert` / `emplace_back` 超出 capacity，或 `reserve` / `shrink_to_fit` / `resize` 变大） | **全部**指针、引用、迭代器 | 元素被搬到新的内存块 |
| **未扩容的尾部插入** | 只有 `end()` | 元素没搬家，`end()` 指向的位置变了 |
| **中间 `insert` / `erase`** | 插入/删除点**之后**的全部 | 后面的元素整体前移/后移 |
| `clear()` | 全部 | 元素被析构（但 capacity 不变） |

实测证据（`07_sequence_containers.cpp` 第 5 节）：

```cpp
std::vector<int> v = {1, 2, 3, 4, 5};
const int* pBefore = v.data();
v.reserve(100);                       // 触发重新分配
// 实测输出：★ 地址变了，之前所有指针/迭代器/引用全部失效

std::vector<int> w;
w.reserve(10); w.push_back(1); w.push_back(2);
auto itW = w.begin(); const int* pW = w.data();
w.push_back(3);                       // capacity 足够，不搬移
// 实测输出：地址未变，itW 仍有效  →  *itW = 1
```

**最实用的一条规则**：不要在「持有迭代器/引用/指针」的同时做可能改变容器结构的操作。

```cpp
// ✗ 崩溃写法：push_back 可能扩容，it 和 end() 立刻失效
for (auto it = v.begin(); it != v.end(); ++it) {
    if (cond(*it)) v.push_back(0);
}

// ✓ 安全写法：两阶段（读一轮 → 收集 → 一次性写）
std::vector<int> toAdd;
for (const auto& x : v) if (cond(x)) toAdd.push_back(x * 100);
v.insert(v.end(), toAdd.begin(), toAdd.end());
```

### 3.5 `vector` 与 C 数组互操作

```cpp
std::vector<int> v{1, 2, 3, 4, 5};

const int* p = v.data();          // 指向首元素，保证连续
c_function(p, v.size());          // 可以直接传给只接受 C 数组的接口

// 反过来：从 C 数组构造 vector
const int raw[] = {1, 2, 3};
std::vector<int> w(std::begin(raw), std::end(raw));

// 或者直接原地用，不拷贝
std::span<const int> view(raw, std::size(raw));   // C++20，见 §6.6
```

三个必须记住的点：

1. **只有 `vector` / `array` / `string` 保证 `data()` 连续**；`deque`、`list`、`map` 都不保证。用 `deque` 的 `data()` 编译都不会通过。
2. `v.data()` 在 `v` 为空时**可能返回 `nullptr`**，别直接传给要求非空的 C 函数。
3. `v.data()` 的有效期和迭代器完全一样（扩容即失效）。

---

## 4. 重点难点详解（错误直觉 → 正确模型 → 代码证据 → 工程建议）

### 4.1 `vector<bool>` 是特化，不是容器

**错误直觉**：`vector<bool>` 就是「元素是 bool 的 vector」，和 `vector<int>` 一样用。

**正确模型**：标准为了保证 `vector<bool>` 只占 1 bit/元素，把它**特化**了。元素不再是独立的 `bool` 对象，而是被压进位。于是：

- `operator[]` 返回的不是 `bool&`，而是一个**代理对象**（`std::vector<bool>::reference`）
- 没有 `data()`，`&v[0]` 和 `bool& r = v[0]` 都是**编译错误**
- 迭代器是「代理迭代器」，`std::sort` 这类需要真正可交换引用的算法**编译失败**
- 任何泛型代码（`T* p = v.data();`、`for (T& x : v)`）遇到 `T = bool` 都会炸

**代码证据**（`07_sequence_containers.cpp` 第 10 节实测）：

```
std::vector<bool> vb = {true,false,true,true,false}
  vb.size()                 = 5
  sizeof(vb)                = 48 字节     ← 容器对象本身（含 Debug 分配器信息）
  sizeof(std::vector<char>) = 32 字节
  5 个 bool 被压进 1 个字节里的 5 个 bit

  auto proxy = vb[0];  -> 推导出的类型是 std::vector<bool>::reference
  proxy = false; 会真的改到 vb[0]
```

**工程建议**：

| 需求 | 用什么 |
|---|---|
| 普通的 bool 容器（可取地址、可泛型、可对接 C 接口） | `std::vector<char>` 或 `std::vector<std::uint8_t>` |
| 位压缩 + 大小编译期固定 | `std::bitset<N>`（接口干净，支持 `&` / `\|` / `^` / `~` / `<<` / `>>`、`count()`、`to_string()`） |
| 位压缩 + 大小运行期决定 | 自己按 `uint64_t` 分块，或 `boost::dynamic_bitset` |

还有一个陷阱：**不要用 `vector<bool>` 做「标志位数组」的返回值**，比如 `std::vector<bool> GetFlags()`。跨模块传递时，调用方看到 `vector<bool>` 很容易写出 `auto* p = v.data()` 然后编译不过。

---

### 4.2 `list` 比 `vector` 慢（经典反直觉结论）

**错误直觉**：「链表插入是 O(1)，vector 中间插入是 O(n)，所以频繁插入要用 `list`」。

**正确模型**：这个说法只考虑了**搬移元素的次数**，完全忽略了**内存访问模式**。真实的成本模型是：

```
总耗时 ≈ 元素搬移次数 × 单次搬移成本 + 内存访问次数 × 单次缓存未命中成本
```

`list` 的每个节点都是**独立 `malloc` 出来的**，地址随机分布。遍历时几乎每次访问都是 CPU 缓存未命中（一次未命中 ≈ 上百个时钟周期）；而 `vector` 连续存储，预取器能一次抓一整条缓存行（64 字节 = 16 个 int）。

**代码证据**（10 万个 `int`，同一台机器，`07_sequence_containers.cpp` 第 3 节）：

| 操作 | `vector` | `list` | 倍数 |
|---|---|---|---|
| 尾部插入（vector 已 reserve） | 1491 us | 7631 us | **5.1 倍** |
| 从头到尾遍历求和 | 51 us | 4654 us | **91.3 倍** |
| 中间插入 2 万次（list 每次自己 `advance` 定位） | 62813 us | 11038480 us | **175.7 倍** |
| 手持迭代器的中间插入 20 万次 | 做不到 O(1) | 28502 us（0.14 us/次） | list 唯一赢的场景 |

内存占用差距同样夸张：100 万个 `int`

```
vector : capacity 1000000 个元素 = 3.8 MB
list   : 1000000 个节点，每个至少 20 字节（2 指针 + int）= 19.1 MB
         （还没算 malloc 的块头和对齐填充，实测常达 40~60 MB）
```

**工程建议**：默认选 `vector`。只有同时满足下面两条时才考虑 `list`：

1. 已经**手持迭代器**（不需要再定位），并且
2. 要么元素很大、移动成本极高，要么需要 `splice` 的常数时间接合。

`list` 真正正当的场景是 **LRU 缓存**：

```cpp
std::list<Node> lru;                                        // 按「最近使用」排序
std::unordered_map<Key, std::list<Node>::iterator> index;   // key -> 链表位置
// 命中：lru.splice(lru.begin(), lru, index[key]);  // O(1)，不移动元素
```

这个例子同时用到了「插入后迭代器稳定」和 `splice` 两个只有 `list` 才有的特性。

---

### 4.3 `operator[]` 会插入默认值

**错误直觉**：`m[key]` 是「查一下 key 对应的值」。

**正确模型**：`std::map::operator[]` 的语义是「**返回 key 对应的值的引用，如果 key 不存在就先插入 `Value{}` 再返回**」。它有副作用。

**代码证据**（`08_associative_containers.cpp` 第 5 节实测）：

```cpp
std::map<std::string, int> m = {{"a", 1}, {"b", 2}};   // size = 2
int val = m["zoe"];                                     // 只是想「查一下」
// 实测：执行后 size = 3，内容变成 (a,1) (b,2) (zoe,0)
```

两个后果：

1. 容器被意外修改，`size()` 变大 —— 循环里这样写还会让 `size()` 不断增长
2. **`Value` 必须可默认构造**，否则 `m[key]` 直接编译不过（比如 `std::map<K, std::mutex>` 就用不了 `[]`）

**正确写法对照**：

| 目的 | 错误写法 | 正确写法 |
|---|---|---|
| 判断 key 是否存在 | `if (m[k])` | `m.contains(k)`（C++20）/ `m.find(k) != m.end()` / `m.count(k) != 0` |
| 读取（不存在时报错） | `int v = m[k];` | `int v = m.at(k);`（抛 `std::out_of_range`） |
| 读取（不存在时用默认值） | `int v = m[k];` | `find` + 判断（或用 §5 的 `Lookup` 函数） |
| 读取（不存在时插入**指定**默认值） | `if (!m.count(k)) m[k] = d;` | `m.try_emplace(k, d)` |
| 插入或覆盖 | `m[k] = v;` | `m.insert_or_assign(k, v)` |
| **统计计数 / 分组** | —— | `++counts[word];` / `groups[cat].push_back(x);`（**这才是 `[]` 该用的地方**） |

**最危险的场景**：把 `[]` 放在循环条件里

```cpp
while (m[someKey] < threshold) { ... }   // 每次判断都插入一个元素 → 死循环 / 内存爆掉
for (const auto& k : keys) total += m[k];  // 不存在的 key 全被插进来，size 悄悄变大
```

---

### 4.4 `unordered_map` 的最坏情况与哈希质量

**错误直觉**：`unordered_map` 查找是 O(1)。

**正确模型**：**平均** O(1)，**最坏** O(n)。当所有 key 都落进同一个桶时，哈希表退化成一条链表。

**代码证据**（`08_associative_containers.cpp` 第 3 节 D）：

```cpp
struct BadHash { std::size_t operator()(int) const noexcept { return 0; } };  // 全碰撞
std::unordered_map<int, int, BadHash> bad;
// 实测：20000 个元素各查一遍
//   哈希全碰撞 : 748748 us   ← 退化成线性查找 O(n)
//   正常哈希   :   1830 us
//   -> 坏哈希约为好哈希的 409 倍耗时（Release 下实测 623 倍）
```

**安全影响（HashDoS）**：如果 key 来自用户输入（HTTP 头、JSON 字段名、查询参数），攻击者可以构造一批「在你的哈希函数下全部碰撞」的 key，让服务器 CPU 打满。这叫**哈希碰撞攻击 / HashDoS**。

**防御手段**：

1. 用带**随机种子**的哈希。libstdc++ / libc++ 默认对字符串哈希加了随机种子；**MSVC 的 `std::hash<int>` 是恒等映射且没有随机种子**，处理不可信整数 key 时要特别注意。
2. 限制单个请求能产生的 key 数量。
3. 对完全不可信的输入改用 `std::map`（O(log n) 的确定性上界比「平均快但可被打崩」更适合暴露在公网的接口）。

**自己写哈希函数的三条要求**：

```cpp
// 要求 1：相等的 key 必须给出相同的哈希（== 为 true ⇒ hash 必须相等）
// 要求 2：不同的 key 尽量给出不同的值（减少冲突）
// 要求 3：要快（每次查找都会调用）
template <> struct std::hash<Point> {
    std::size_t operator()(const Point& p) const noexcept {
        const std::size_t h1 = std::hash<int>{}(p.x);
        const std::size_t h2 = std::hash<int>{}(p.y);
        // 组合多个字段：乘黄金比例魔数再异或，能让相邻输入散得更开
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};
```

**反例**：`return x ^ y;` 会让 `(1,2)` 和 `(2,1)` 碰撞；`return x;` 会让所有 `(x, *)` 挤在一起。

**注意**：`std::hash` 的显式特化**不能放在匿名命名空间里**（会报 `error C2888`，因为匿名命名空间里的类型有内部链接）。本文件的做法是先把 `Point` 放进具名命名空间 `demo_point`，特化写在全局命名空间，然后再重新打开匿名命名空间。

---

### 4.5 迭代器失效（一张表 + 一个真实事故）

完整规则见 §2.3 与 §3.4。这里只强调三种最容易出事的组合：

```cpp
// 事故 1：vector 扩容后继续用老迭代器
auto it = v.begin();
v.push_back(x);          // 可能扩容
*it = 42;                // ✗ UB

// 事故 2：unordered_map rehash 后用老迭代器
auto it = um.find(k);
um[k2] = v;              // 可能触发 rehash
um.erase(it);            // ✗ UB

// 事故 3：范围 for 里修改容器
for (const auto& x : v) {
    if (cond(x)) v.push_back(x);   // ✗ push_back 可能扩容，范围 for 内部保存的 end() 失效
}
```

**跨容器的统一规则**：

| 容器 | 插入 | 删除 | 扩容/rehash |
|---|---|---|---|
| `vector` / `string` | 未扩容：只有 `end()` 失效；扩容：全失效 | 删除点之后全失效 | 全失效 |
| `deque` | 两端：迭代器全失效（**引用/指针有效**）；中间：全失效 | 同左 | 迭代器全失效 |
| `list` / `forward_list` | 全部有效 | 只有被删的失效 | 不适用 |
| `map` / `set` | 全部有效 | 只有被删的失效 | 不适用 |
| `unordered_*` | rehash 则迭代器全失效（**引用/指针有效**） | 只有被删的失效 | 迭代器全失效 |

---

### 4.6 `erase` 循环里怎么删元素

**错误写法**：

```cpp
for (auto it = v.begin(); it != v.end(); ++it) {
    if (cond(*it)) v.erase(it);   // ✗ erase 后 it 已失效，++it 是 UB
}
```

**写法 1：迭代器版（最通用）**

```cpp
for (auto it = v.begin(); it != v.end(); ) {
    if (cond(*it)) it = v.erase(it);   // ★ erase 返回下一个有效位置，不要再 ++
    else           ++it;
}
```

**写法 2：下标版（注意删完不要 `++`）**

```cpp
for (std::size_t i = 0; i < v.size(); ) {
    if (cond(v[i])) v.erase(v.begin() + static_cast<std::ptrdiff_t>(i));
    else            ++i;
}
```

**写法 3：erase-remove 惯用法（推荐，快得多）**

```cpp
v.erase(std::remove_if(v.begin(), v.end(), pred), v.end());
```

为什么快：`remove_if` 只做**一次前移扫描**（O(n)）；而循环 `erase` 每删一个就要搬一次尾巴，最坏 O(n²)。

**写法 4：C++20 一行搞定**

```cpp
std::erase_if(v, pred);      // 返回删除的个数
std::erase(v, value);        // 按值删
```

`std::erase` / `std::erase_if` 是**非成员函数**，参数是容器本身而不是迭代器对 —— 这是它和算法版最大的区别。

**关于 `remove` 的经典误解**：

```cpp
std::vector<int> v{1,2,3,4,5,6};
auto newEnd = std::remove_if(v.begin(), v.end(), [](int x){ return x % 2 == 0; });
// 实测：物理内容变成 [1 3 5 4 5 6]，size 仍然是 6！
v.erase(newEnd, v.end());   // 必须补这一步，size 才变成 3
```

`remove` 系列**不删除元素**，只是把要保留的元素前移并返回新的逻辑结尾。原因：算法只操作迭代器区间，不知道容器是什么，无法调用容器的 `erase`。

---

### 4.7 `from_chars` vs `stoi`

**错误直觉**：`stoi` 更简单，性能差别不大。

**正确模型**：`std::stoi` / `std::stod` 内部要**构造临时 `std::string`**、调用 `strtol` / `strtod`、**查 locale**，并用**异常**报告越界。`std::from_chars` 是一条直路：直接读 `[first, last)` 区间，错误通过返回值报告。

**代码证据**（`06_cpp_string.cpp` 第 7 节，10 万次解析）：

| 解析 | `stoi` / `stod` | `from_chars` | 倍数 |
|---|---|---|---|
| 整数（Release） | 10412 us | 639 us | **16.3 倍** |
| 浮点（Release） | 21792 us | 4946 us | **4.4 倍** |
| 整数（Debug） | 12145 us | 4257 us | 2.9 倍 |
| 浮点（Debug） | 22892 us | 12621 us | 1.8 倍 |

**行为差异对照**：

| 输入 | `stoi` 结果 | `from_chars` 结果 |
|---|---|---|
| `"2026"` | 2026，消费 4 字符 | 2026，`ec == {}`，ptr 到末尾 |
| `"  -42"` | **-42**（自动跳过前导空白） | `errc::invalid_argument`（不跳空白） |
| `"12abc"` | **12**（停在非数字处，不报错） | 12，`ec == {}`，但 `ptr` 没到末尾 → 调用方自己判断 |
| `"abc"` | 抛 `std::invalid_argument` | `errc::invalid_argument` |
| `""` | 抛 `std::invalid_argument` | `errc::invalid_argument` |
| `"99999999999999999999"` | 抛 `std::out_of_range` | `errc::result_out_of_range` |
| `"3.14"`（作为 int） | **3**（静默截断小数部分！） | 3，`ptr` 停在 `'.'` |

**`from_chars` 的五个优点**：

1. **不抛异常**：错误走返回值，可用于 `noexcept` 函数和热路径
2. **完全不受 locale 影响**：`std::stod` 在德语等 locale 下小数点会变成逗号，协议解析会直接崩；`from_chars` 永远是 `'.'`
3. **不需要构造 `std::string` / `istringstream`**，没有内存分配
4. **接受 `[first, last)` 区间**，不需要 `'\0'` 结尾，天然配合 `string_view`
5. **不做任何隐式操作**（不跳空白、不多消费字符）—— 行为就是字面上的行为

**唯一的限制**：C++17 的 `from_chars` 对浮点支持是「可选」的。MSVC / libstdc++ / libc++ 现在都支持，但老编译器可能只有整数版。

**工程建议**：解析配置文件 / 协议报文 / CSV 一律用 `from_chars`；只在「输入来自人类、希望拿到异常」的一次性交互代码里用 `stoi` / `stod`。

---

### 4.8 `cin` 的失败状态

**错误直觉**：`cin >> x` 失败就是「输入不对」，重试一下就好。

**正确模型**：`cin` 有四个状态位，一旦 `failbit` 被置位，**之后所有 `>>` 操作都立即失败且不消费任何字符**。这就是「读失败就 `continue` 重试」会变成死循环的根本原因。

**代码证据**（`10_iostream_and_files.cpp` 第 3 节，逐步打印状态）：

```
输入流内容是 "abc" / "42" 两行
第一次 cin >> n（期望 int）：
  操作成功？否
  n 的值      = 0        ← 失败的读取不写值
  good()=0 eof()=0 fail()=1 bad()=0
第二次 cin >> n：
  操作成功？否            ← ★ 'a' 还在流里没被消费

修复第一步 input.clear()：fail()=0，下一个字符 peek()='a'   ← 状态清了，垃圾还在
修复第二步 input.ignore(max, '\n')：下一个字符 peek()='4'   ← 现在流指针指向下一行
现在再 >> n：成功，n = 42   ← 正确读到了下一行的 42
```

**`clear()` 修的是「状态位」，`ignore()` 修的是「流里的垃圾字符」，两件事都要做。**

另一个常见误解：**读到结尾时 `eofbit` 和 `failbit` 同时被置位**。

```cpp
// ✗ 错误写法：最后一次失败后还会进循环，用未更新的 x 处理一遍数据
while (!cin.eof()) { cin >> x; process(x); }

// ✓ 正确写法：让 >> 的返回值当条件
while (cin >> x) { process(x); }
// 或者用 getline
while (std::getline(in, line)) { process(line); }
```

本文件给出了 5 个可直接复用的函数模板：`ReadInt` / `ReadIntInRange` / `ReadLineSafe` / `ReadNonEmptyLine` / `ReadDouble` / `ReadMenuChoice`（后者的输入循环能区分「格式错误」「范围错误」「输入结束」三种情况，永远不死循环）。

---

## 5. C 标准库 vs C++ 标准库对照

| C 头文件 | C++ 头文件 | 推荐替代（新项目该用哪个） | 为什么 |
|---|---|---|---|
| `<stdio.h>` | `<cstdio>` | **`<iostream>`（交互） + `std::format`（格式化） + `<fstream>`/`<filesystem>`（文件）** | 类型安全、RAII 自动关闭文件、格式串编译期检查 |
| `<stdlib.h>` 随机数 | `<cstdlib>` | **`<random>`（`std::mt19937` + `distribution`）** | `rand()` 范围只有 32767、低位随机性差、有取模偏置、全局状态 |
| `<stdlib.h>` 内存 | `<cstdlib>` | **容器 + 智能指针（`vector` / `unique_ptr`）** | 自动释放、异常安全；`malloc/free` 只留在对接 C 库的边界 |
| `<stdlib.h>` 排序 | `<cstdlib>` | **`<algorithm>` 的 `std::sort`** | 可内联比较器、保证 O(n log n) 上界、类型安全（`qsort` 通过函数指针调用，最坏 O(n²)） |
| `<stdlib.h>` 数值转换 | `<cstdlib>` | **`<charconv>` 的 `std::from_chars` / `to_chars`** | 不抛异常、不受 locale 影响、更快（实测整数快 16 倍） |
| `<string.h>` | `<cstring>` | **`std::string` / `std::string_view`** | 自动管理内存、零拷贝视图、不会缓冲区溢出 |
| `<math.h>` | `<cmath>` | **`<cmath>` + `std::` 前缀；常量用 `<numbers>`** | `std::numbers::pi` 是标准（`M_PI` 需要 `_USE_MATH_DEFINES` 宏，非标准） |
| `<time.h>` | `<ctime>` | **`<chrono>`（`steady_clock` 计时 / `year_month_day` 日期） + `std::format` 格式化** | 类型安全、无 `+1900`/`+1` 陷阱、`steady_clock` 单调不受系统时间调整影响 |
| `<ctype.h>` | `<cctype>` | **`<cctype>` 仍然可用**；判数字等用 `<charconv>` 或 `std::all_of` | 处理多字节/Unicode 必须用 ICU 或 C++23 `<text>`，ctype 只认单字节 |
| `<limits.h>` | `<climits>` | **`<limits>` 的 `std::numeric_limits<T>`** | 模板化，可按类型参数化，不用为每个类型记一个宏名 |
| `<float.h>` | `<cfloat>` | **`<limits>`** | 同上 |
| `<assert.h>` | `<cassert>` | `<cassert>`（仍推荐）；需要更详细信息用 `std::source_location`（C++20） | `assert` 在 `NDEBUG` 下被完全移除，不能放有副作用的代码 |
| `<errno.h>` | `<cerrno>` | 视场景；C++ 更推荐异常或 `std::error_code` | `errno` 是全局的，调用前容易忘清零 |
| `<conio.h>`（Windows 专有） | 不存在 | **`std::cin.get()`（跨平台）/ `_getch()`（Windows 读密码）/ ANSI 转义序列或第三方库（终端控制）** | `conio.h` 是 MSVC/Turbo C 扩展，Linux/macOS 完全没有 |

### 明确建议：新项目

- **输入输出**：`std::cout` / `std::format`。只有在对接 C 库或极致性能时才用 `printf`。
- **字符串**：`std::string`；函数参数用 `std::string_view`。
- **容器**：`std::vector` / `std::unordered_map` / `std::map`（按 §2 决策表）。
- **算法**：`<algorithm>` / `<numeric>` 里的算法 + lambda，不要手写循环。
- **随机数**：`<random>` 的 `mt19937` + distribution。
- **时间**：`std::chrono`（`steady_clock` 测耗时，`system_clock` + `year_month_day` 取日历）。
- **查找表**：`std::array` + `constexpr`，或 `constexpr` 的 `std::array<std::pair<...>>` + `std::lower_bound`。
- **错误处理**：`std::optional`（可能没有值）/ `std::variant`（多种互斥情况）/ 异常（真正的错误）。

---

## 6. 实用开发习惯

### 6.1 `reserve` 习惯

```cpp
// 读文件前先估算行数
std::vector<std::string> lines;
lines.reserve(estimatedCount);

// 循环追加已知个数
std::vector<Result> results;
results.reserve(input.size());     // 上界
for (const auto& x : input) results.push_back(process(x));

// 拼接字符串
std::string out;
out.reserve(totalSize);
```

**判断标准**：能在 O(1) 时间内算出（或合理上界）元素个数 → 就 `reserve`。

### 6.2 `const auto&` 遍历

```cpp
// ✓ 不拷贝元素，也不允许误改
for (const auto& item : container) { use(item); }

// ✓ 需要修改
for (auto& item : container) { item.mutate(); }

// ✓ 结构化绑定 + const 引用（map 遍历的标配）
for (const auto& [key, value] : map) { use(key, value); }

// ✗ 每次迭代都拷贝一个元素
for (auto item : container) { ... }

// ✗ 想改容器结构的循环里用引用
for (auto& x : v) v.push_back(x);   // 迭代器失效，UB
```

**注意**：`vector<bool>` 不能用 `auto&`（代理对象），用 `auto` 或 `auto&&`。

**注意**：范围 for 会把范围表达式的结果绑定到一个隐藏变量，所以 `for (const auto& x : MakeVector())` 在循环期间是安全的（C++ 保证临时对象活到循环结束），但**出了循环就不能再用那个容器里的任何引用**。

### 6.3 `erase-remove` 惯用法

```cpp
// C++20 之前
v.erase(std::remove(v.begin(), v.end(), value), v.end());
v.erase(std::remove_if(v.begin(), v.end(), pred), v.end());

// C++20 起（一行，语义更清楚）
std::erase(v, value);
std::erase_if(v, pred);

// map / unordered_map 也能用 erase_if
std::erase_if(m, [](const auto& kv) { return kv.second < 0; });
```

**性能**：erase-remove 是 O(n) 单遍扫描；循环 `erase` 最坏 O(n²)。

### 6.4 `contains` 而非 `count`

```cpp
// C++20 起（推荐，语义最清楚）
if (m.contains(key)) { ... }

// C++17 及以前（两者等价，count 对 map 有轻微误导）
if (m.find(key) != m.end()) { ... }
if (m.count(key) != 0) { ... }
```

对 `vector` 不要用 `count` 当「存在性判断」——`count` 会扫完整个区间，`find` 找到就能停。

**注意**：`std::string::contains` 是 **C++23** 才有的（本机 `/std:c++20` 下编译失败）。C++20 判「包含子串」仍要写 `s.find(sub) != std::string::npos`。

### 6.5 `emplace` 的取舍

| 场景 | 用哪个 | 理由 |
|---|---|---|
| 传已存在的对象 | `push_back(obj)` / `push_back(std::move(obj))` | 意图明确：是拷贝还是移动 |
| 就地构造 | `emplace_back(args...)` | 少一次移动构造 |
| 「存在就不动」的 map 插入 | `try_emplace(k, args...)` | key 已存在时连参数都不构造 |
| 「都要覆盖」 | `insert_or_assign(k, v)` | 语义明确 |

**`emplace` 的陷阱**：

```cpp
std::vector<std::string> v;
// v.push_back(nullptr);      // 编译错误（好）
v.emplace_back(nullptr);      // ★ 编译通过，运行期 UB（坏！）
```

因为 `emplace_back` 会把参数**完美转发**给 `std::string(const char*)` 构造函数，而 `nullptr` 对它是合法实参。同理，`emplace_back` 还能绕过 `explicit` 构造函数，导致意外的隐式转换。

**实测数据**（`07_sequence_containers.cpp` 第 4 节）：

| 元素类型 | `push_back` | `emplace_back` | 倍数 |
|---|---|---|---|
| `int`（平凡类型） | 15620 us | 15949 us | 1.02 倍（**生成的机器码完全相同**） |
| `Heavy`（含 `std::string` + `std::vector<int>`） | 119001 us | 56839 us | 2.09 倍 |

结论：**「为了性能必须 emplace」是误区**。真正影响性能的是元素大小、是否 `noexcept` 移动、有没有 `reserve`。按可读性选择即可。

### 6.6 容器传参用 `std::span`（C++20）

```cpp
// ✗ 老式：只能收 vector
void Process(const std::vector<int>& data);

// ✗ 更差：退化成指针，丢失长度信息
void Process(const int* data, std::size_t n);

// ✓ 推荐：收「任意连续区间」，零拷贝
void Process(std::span<const int> data) {
    for (int x : data) { ... }
    // data.size() / data.data() 都有
}

// 调用方都可以直接传
std::vector<int> v;
std::array<int, 5> a;
int raw[10];
Process(v);      // vector 隐式转换成 span
Process(a);
Process(raw);    // C 数组也行
```

**注意**：`span` **不拥有数据**，只是 `(指针, 长度)`，所以它和 `string_view` 有完全一样的生命周期陷阱 —— 不能存指向临时对象的 `span`。

`std::span<std::byte>` 还是「任意二进制缓冲区」的标准表示方式，比 `void* + size` 类型安全得多。

### 6.7 避免在循环里 `map[key]`

```cpp
// ✗ 每次循环都可能插入新元素：size 悄悄变大、value 被默认构造
for (const auto& k : keys) total += m[k];

// ✓ 只读查找
for (const auto& k : keys) {
    const auto it = m.find(k);
    if (it != m.end()) total += it->second;
}

// ✓ 或者用 at()（不存在就抛异常，能立刻暴露问题）
for (const auto& k : keys) total += m.at(k);
```

**另外**：`m[k]` 会先默认构造再赋值，对 `Value` 是大对象时比 `try_emplace` / `insert_or_assign` 慢。

### 6.8 其它值得养成的习惯

```cpp
// 1. 用 '\n' 而不是 std::endl（实测快 3 倍）
std::cout << "hello\n";                     // ✓
std::cout << "hello" << std::endl;          // 只在需要立即刷新时用

// 2. 需要「非拥有视图」时首选 string_view / span，但要管好生命周期
void Log(std::string_view msg);             // ✓ 调用方传字面量也不分配内存

// 3. 遍历 map 用结构化绑定 + const auto&
for (const auto& [k, v] : m) { ... }

// 4. 排序键多于一列时用 pair / tuple 的字典序比较，不要手写比较器
std::sort(v.begin(), v.end());   // vector<pair<int,int>> 天然先按 first 再按 second

// 5. 二分查找前确认数据已排序，排序和查找用同一个比较关系
std::sort(v.begin(), v.end(), std::greater<int>{});
auto it = std::lower_bound(v.begin(), v.end(), 30, std::greater<int>{});   // ← 必须一致

// 6. accumulate 的初值类型决定结果类型
auto sum = std::accumulate(v.begin(), v.end(), 0LL);   // 防 int 溢出
auto avg = std::accumulate(v.begin(), v.end(), 0.0) / v.size();

// 7. 判断「全部满足 / 存在 / 都不满足」用 all_of / any_of / none_of
if (std::all_of(id.begin(), id.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0; })) { ... }
```

---

## 7. 「原素材纠错 / 改动清单」

原素材指 `C:\Users\Waj07\Desktop\CPPstudy\模块\` 与 `C:\Users\Waj07\Desktop\CPPstudy\math.h\` 下的代码（这些目录**已原样保留、未做任何修改**，作为历史素材）。

### 7.1 技术性错误 / 有风险写法

| 原写法 | 问题 | 新写法 | 为什么 |
|---|---|---|---|
| `while (getchar() != '\n') {}` | **遇到 EOF 会死循环** | `int c; while ((c = std::getchar()) != '\n' && c != EOF) {}` | EOF 时 `getchar()` 永远返回 -1，条件永远为真。用管道喂空输入或按 Ctrl+Z 就会触发 |
| `while (cin.get() != '\n') {}`（`stl_demo.cpp`） | 同上 | 同上，或直接用 `ignore(numeric_limits<streamsize>::max(), '\n')` | 同样的 EOF 死循环问题 |
| `if (scanf("%d", &choice) != 1)` | 返回值语义不完整 | 和期望项数比较，并区分 `EOF`（-1）与格式错（0） | `scanf` 返回「成功赋值项数」；只判 `!= 1` 会把两种失败混在一起 |
| `std::strtok` + `nullptr` 续切 | 改原串 + 内部静态状态 + 非线程安全 | 主推 `std::string_view` 手写切分；`getline` + `istringstream` 作为备选 | `strtok` 把分隔符原地改成 `'\0'`，切分位置存在函数内部静态变量里，不能嵌套、多线程会互相破坏。原注释说的「会改原串、非线程安全」是对的，补充了「内部静态状态」这一点 |
| 想用 `std::strtok_s` | **MSVC 上不存在**，报 `error C2039: 'strtok_s': is not a member of 'std'` | 用全局 `strtok_s`（MSVC / Annex K 扩展，非标准）或干脆改用 `string_view` | MSVC 的安全版在全局命名空间；这本身也说明「`_s` 版本不是可移植方案」 |
| `strcpy` / `strcat` / `strncpy` 直接调用 | 缓冲区溢出风险；`strncpy` 长源时不补 `'\0'` | `strcpy_s` / `strcat_s` / `strncpy_s`，或（推荐）`std::string` | `strncpy` 在源串比 n 长时**不补结尾 `'\0'`**，结果不是合法 C 字符串，`printf` 会越界读 |
| 未提及 `*_s` 函数的 Debug 行为 | 演示 `strcpy_s` 溢出时会**直接弹断言中止进程**（exit code 3），拿不到错误码 | 先装 `_set_invalid_parameter_handler` + `_CrtSetReportMode(_CRT_ASSERT, 0)`，再演示返回 `ERANGE`（实测得到 34） | 默认的无效参数处理器会中止进程；生产环境里「错误码路径」比「弹窗 + 进程消失」有用 |
| `malloc` 后只写 `free(arr)` | 没置空，可能 double free | `free(arr); arr = nullptr;` | 释放后置空避免野指针 |
| `std::rand() % 100` | 范围小（RAND_MAX=32767）+ 取模偏置 | `<random>` 的 `mt19937` + `uniform_int_distribution` | 原素材的 `math.h` 手册第 5 条已经指出「不要用 rand()」，但 `stdlib_demo.cpp` 仍在用 —— 现在代码和文档一致了 |
| `atoi("12ab")` 当合法解析 | 无法报告错误，越界是 UB | `strtol` + `errno` + `endptr`，或 `from_chars` | `atoi` 对 `"abc"` 和 `"0"` 都返回 0，调用方无法区分；越界完全无定义 |
| `qsort` 比较函数写 `return x - y;` | **整数溢出 UB** | `return (x > y) - (x < y);` | 两数差距大时 `x - y` 超出 `int` 范围；原素材的 `cmpInt` 是对的，但没说明为什么不能写 `x - y`，本文件补了反例演示 |
| `abs(INT_MIN)`、`abs(-5.0)` | 前者是 UB，后者在 C 里会截断成 `abs(-5)` | `llabs((long long)INT_MIN)`；浮点用 `std::fabs` | `-INT_MIN` 超出 `int` 范围；`abs` 是整数版。原素材手册第 8 条讲对了，代码里没演示 |
| `M_PI` + `_USE_MATH_DEFINES` | 非标准宏，且必须先 define 后 include，容易被别的头文件抢先 | `std::numbers::pi`（C++20，`<numbers>`） | 标准、无宏、无顺序要求 |
| `1.0 / 0.0` 直接写在代码里 | MSVC 报 **C2124（divide or mod by zero）**，`/WX` 下编译失败 | 用 `volatile` 变量挡住编译期常量折叠 | 编译器把常量表达式里的除零当错误，但运行期的浮点除零只会得到 `inf` |
| 原素材未区分 `clock()` 与墙钟时间 | `clock()` 测的是**进程 CPU 时间**，多线程下会远大于墙钟 | 测耗时用 `std::chrono::steady_clock`；`clock()` 只在需要 CPU 时间时用 | 原 `time_demo.cpp` 用 `clock()` 测循环耗时，语义不精确 |
| 时间戳算术的类型 | 原素材已用 `100LL`（正确），但没解释为什么 | 强化说明：时间戳算术一律显式用 64 位，并给出 int 溢出的实测反例 | 32 位 `int` 上算 36524 天 × 86400 秒会溢出（实测得到 -1139293696） |
| 原素材未提及 `tm_isdst` | 手写 `tm` 时若把 `tm_isdst` 留成 0（而非 -1），`mktime` 可能算错一小时 | 显式写 `tm.tm_isdst = -1;` 让 `mktime` 自己判断 | `mktime` 依赖这个字段判断夏令时；`{}` 初始化会给 0（= 非夏令时） |
| `putchar(std::tolower(c))` 里 `c` 是 `char` | **负值传进 ctype 是 UB** | `std::tolower(static_cast<unsigned char>(c))` | MSVC 的 `char` 有符号，UTF-8 中文/重音字母的字节会变成负数。原素材的 `ctype_demo.cpp` **已经做对了**，但没解释这个坑，现补充完整说明与实测（`"中文"` 的字节作为 char 是 -28/-72/-83…，作为 unsigned char 是 228/184/173…） |
| 原素材用 `localtime` | 返回指向**函数内部静态缓冲区**的指针，非线程安全 | `localtime_s`（MSVC）/ `localtime_r`（POSIX）/ C++20 `year_month_day` | 静态缓冲区会被下次调用覆盖，多线程下直接数据竞争 |
| 原素材在 `main.cpp` 用 `SetConsoleOutputCP(CP_UTF8)` | 正确，但只在一个文件里做了 | 每个 `.cpp` 都有 `EnableUtf8Console()` | 单文件独立可执行后没有统一的 `main.cpp`，每个文件都得自己处理 |
| `strftime` 的 `%Z` 时区名 | 按**活动代码页**编码，UTF-8 控制台下显示乱码（本机实测） | 不依赖 `%Z`，自己维护时区名字符串 | 跨平台稳定输出要考虑编码 |
| 删除仍被打开的文件（Windows） | `std::ifstream` 还活着时 `std::remove` 会失败；Linux 却允许 | 把流放进内层作用域，析构（句柄释放）之后再删 | Windows 不允许删除已打开的文件，Linux 允许 `unlink` —— 跨平台代码要按严格的那一边写 |

### 7.2 结构性 / 风格改动（有意为之）

| 原写法 | 问题 | 新写法 | 为什么 |
|---|---|---|---|
| 1 个头文件 + 1 个 cpp，`void demoXxx()` 由 `main.cpp` 菜单调度 | 与「一个 `.cpp` = 一个独立可执行文件」的新构建体系冲突 | 每个模块的实现搬进自己的 `main()`，`demoXxx()` 的函数体留在匿名命名空间里被 `main` 调用 | 新 `build.ps1` 是「一个 `.cpp` 一个 exe」，多文件共用 main 会链接冲突 |
| `using namespace std;`（`stl_demo.cpp`） | 全局引入 std，真实项目里会造成名字冲突 | 全部改成 `std::` 前缀 | 原素材自己也注释了「生产建议限定 `std::`」；本仓库统一风格 |
| `#include <math.h>` + 全局 `sqrt()` | 把 C 函数暴露在全局命名空间 | `#include <cmath>` + `std::sqrt()` | 统一命名空间，也是本仓库的既有约定 |
| 原素材的 `ConsoleBuf` 类（接管 `cout` 用 `WriteConsoleW` 输出） | 是一个 50 行的「进阶技巧」，对初学者是噪声 | 换成 4 行的 `EnableUtf8Console()`（`chcp 65001`） | 达到相同效果（中文不乱码），但概念负担小得多；`ConsoleBuf` 的做法仍保留在原目录作为进阶阅读材料 |
| 菜单式交互（9 个模块靠键盘选） | 构建脚本用空 stdin 运行时会卡住或行为不确定 | 去掉菜单，直接顺序跑完所有分节 | 让每个示例都能在无人值守下完整运行、可自动化验证 |
| `void demoMath()` 里的 C4996 说明「VS 中对 math 函数做 C4996 警告少见」 | 含糊 | 明确说明 `_CRT_SECURE_NO_WARNINGS` 的作用与生产替代方案 | 教学要给出确定的规则：「`_CRT_SECURE_NO_WARNINGS` 只是让教学对比能编译；生产代码用 `_s` 版本或 C++ 替代品」 |
| 参考手册里「`fmin`/`fmax` 比 `std::min` 安全」 | 正确但不够精确 | 补充：`std::max(a,b)` 的实现是 `(a < b) ? b : a`，`NaN < 5.0` 为 false，所以返回第一个参数（NaN）—— 实测验证 | 把「为什么」补上，读者才能真正判断什么时候该用哪个 |

---

## 8. 本章全部实测数据汇总

> 测量方式：`std::chrono::steady_clock`。同一台机器，MSVC 14.51，`/std:c++20`。
> Debug = `/MDd /Zi`（无优化），Release = 加 `/O2 /DNDEBUG`。
> **注意：不同运行之间抖动可达 ±30%，以下数字看量级和趋势，不要当精确值。**

### 8.1 C 标准库部分

| 对比 | 结果 |
|---|---|
| `std::sort` vs `qsort`（30 万 int） | Debug 下两者接近（`std::sort` 的迭代器检查在无优化时开销显现）；`std::sort` 保证 O(n log n) 上界、可内联比较器 |
| `pow(x, 2.0)` vs `x * x`（200 万次） | Release **33.3 倍**（14032 vs 421 us），Debug 16.0 倍 |
| `log(1+x)` vs `log1p(x)`，x = 1e-16 | `log(1+x)` 返回 **0**（相对误差 100%），`log1p(x)` 返回 1e-16 |
| `exp(x)-1` vs `expm1(x)`，x = 1e-16 | 前者返回 **0**，后者返回 1e-16 |
| `std::fma(1e16, 3.0000000000000004, -3e16)` | **4.4408920985006262**（正确）；`a*b + c` 得到 **4**（精度丢失） |
| `hypot(1e200, 1e200)` | 1.414214e+200；`sqrt(x*x+x*x)` 得到 **inf** |
| `time_t` 用 int 算 36524 天 | 得到 **-1139293696**（溢出）；64 位得到 3155673600 |
| `strftime` 的 `%Z` | UTF-8 控制台下显示乱码 |

### 8.2 `std::string`

| 对比 | Debug | Release |
|---|---|---|
| 分 20000 次追加 32 字节：不 reserve vs reserve | 930 vs 308 us（3.0 倍） | **694 vs 34 us（20.4 倍）** |
| 往头部 `insert(0, ...)` 2 万次 vs `push_back` + `reverse` | 2127 vs 97 us（22 倍） | **47.1 倍** |
| `stoi` vs `from_chars`（10 万次整数） | 12145 vs 4257 us（2.9 倍） | **10412 vs 639 us（16.3 倍）** |
| `stod` vs `from_chars`（10 万次浮点） | 22892 vs 12621 us（1.8 倍） | **21792 vs 4946 us（4.4 倍）** |
| 拼 2 万次格式化串：`format` / `snprintf` / `ostringstream` | 59653 / 19444 / 123206 us | **14434 / 19894 / 102219 us** |

`std::string` 的 SSO 实测（MSVC）：容量 **15** 字节。长度 ≤ 15 时 `data()` 落在 `string` 对象内部，长度 16 起 capacity 跳到 31（走堆）。

### 8.3 序列容器（10 万个 int）

| 操作 | Debug | Release |
|---|---|---|
| `vector` 尾部插入（已 reserve） | 2178 us | 1491 us |
| `list` 尾部插入 | 14042 us（**6.4 倍**） | 7631 us（**5.1 倍**） |
| `vector` 遍历求和 | 94 us | 51 us |
| `list` 遍历求和 | 3943 us（**41.9 倍**） | 4654 us（**91.3 倍**） |
| `vector` 中间 insert 2 万次 | 74817 us | 62813 us |
| `list` 定位 + insert 2 万次 | 9806534 us（**131 倍**） | 11038480 us（**176 倍**） |
| `list` 手持迭代器 insert 20 万次 | 47235 us（0.24 us/次） | 28502 us（0.14 us/次） |

| 其它 | Debug | Release |
|---|---|---|
| `push_back` 100 万 int：不 reserve vs reserve | 25865 vs 22096 us（1.2 倍） | 18924 vs 15650 us（1.2 倍） |
| `push_back(Heavy)` vs `emplace_back(args)` 20 万次 | 224498 vs 111184 us（2.0 倍） | 119001 vs 56839 us（2.1 倍） |
| `int` 类型：`push_back` vs `emplace_back` | 24545 vs 21514 us | 15620 vs 15949 us（**无差别**） |
| 200 万元素：下标遍历 vector vs deque | 3237 vs 12252 us | 2101 vs 6819 us |
| 200 万元素：迭代遍历 vector vs deque | 2262 vs 12268 us | 737 vs 6248 us |
| 头部插入 10 万次：deque `push_front` vs vector `insert(0)` | 8698 vs 308419 us（**35 倍**） | 3791 vs 309801 us（**82 倍**） |

### 8.4 关联容器

| 对比 | Debug | Release |
|---|---|---|
| 插入 20 万个 int：`map` vs `unordered_map`（已 reserve） | 213591 vs 78542 us（2.7 倍） | 104779 vs 35991 us（**2.9 倍**） |
| 查找 50 万次：`map` vs `unordered_map` | 676434 vs 288682 us（2.3 倍） | 532218 vs 246031 us（**2.2 倍**） |
| 遍历全部元素：`map` vs `unordered_map` | 29369 vs 19131 us | 23086 vs 17503 us |
| 恶意哈希（全碰撞）vs 正常哈希 | 748748 vs 1830 us（**409 倍**） | 约 **623 倍** |
| `map<string,int>` vs `map<string,int,less<>>`（20 万次字面量查找） | 123703 vs 26455 us（**4.7 倍**） | 56401 us（**4.7 倍**） |
| MiniFlatMap vs `std::map`：构建 2 万元素 | 37658 vs 12238 us（flat 慢 3.1 倍） | 19924 vs 4382 us（flat 慢 4.6 倍） |
| MiniFlatMap vs `std::map`：查找 50 万次 | 138276 vs 135679 us（基本持平） | 63067 vs 55730 us（flat 快 1.13 倍） |

### 8.5 适配器与算法

| 对比 | Debug | Release |
|---|---|---|
| 30 万元素建堆：逐个 push vs `make_heap` | 37125 vs 4821 us（**7.7 倍**） | 同一量级 |
| 30 万元素：`sort` / `partial_sort(前10)` / `nth_element(第10)` | 51532 / 797 / 4112 us | 同一量级（`partial_sort` 快 60+ 倍） |

### 8.6 iostream

| 对比 | 结果 |
|---|---|
| `'\n'` vs `std::endl`（2 万次写文件） | **3.0 倍**（Debug 25632 vs 75957 us；Release 约 3.1 倍） |
| `std::format` vs `ostringstream` | 2.1 倍（Debug）/ **7.1 倍**（Release） |
| `std::format` vs `snprintf` | Debug 下 snprintf 更快；**Release 下 format 反而略快**（14434 vs 19894 us / 2 万次） |

---

## 9. 自测题（8 道，含答案）

### 题 1：为什么 `while (getchar() != '\n') {}` 有 bug？

<details>
<summary>答案</summary>

**遇到 EOF 时 `getchar()` 永远返回 -1（EOF），条件 `-1 != '\n'` 恒为真 → 死循环。**

触发场景：Windows 控制台按 `Ctrl+Z` 回车、程序被管道喂入空 stdin、输入被重定向到一个已读完的文件。

正确写法：

```cpp
int c;
while ((c = std::getchar()) != '\n' && c != EOF) { /* 丢弃 */ }
```

或者对 C++ 流用：

```cpp
std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
```
</details>

---

### 题 2：`std::vector` 的 `push_back` 是 O(1) 吗？为什么？

<details>
<summary>答案</summary>

**是「均摊 O(1)」，不是「每次 O(1)」。**

实现按**倍数**增长（MSVC 约 1.5 倍）。设增长因子为 k > 1，n 次 `push_back` 的总搬移量是等比级数

```
1 + k + k² + ... + k^m  ≈ k·n/(k−1) = O(n)
```

所以平均每次是 O(1)。但**扩容那一次的代价是 O(n)**（分配新块 + 逐元素搬移 + 释放旧块）。

实测（`07_sequence_containers.cpp`）：capacity 的变化点是 1 → 2 → 3 → 4 → 6 → 9 → 13 → 19 → 28 → 42，增长比逐渐收敛到 1.5。

工程含义：对延迟敏感的系统要 `reserve`，消掉的是**延迟尖峰**而不只是平均耗时。
</details>

---

### 题 3：为什么实测 `std::list` 比 `std::vector` 慢，连「中间插入」都慢？

<details>
<summary>答案</summary>

因为 `list` 的每个节点是**独立 `malloc` 的**，地址随机分布：

1. **遍历成本**：`list` 几乎每次访问节点都是 CPU 缓存未命中（~100+ 时钟周期）；`vector` 连续存储，预取器能一次抓一整条缓存行。实测遍历差距 **91 倍**（Release，10 万 int）。
2. **插入前必须定位**：`list` 的「O(1) 插入」前提是**已经持有迭代器**。如果要先 `std::advance` 走到位置，那一步是 O(n) 的指针追逐，比 `vector` 的连续内存搬移慢得多。实测中间插入 2 万次差距 **176 倍**。
3. **内存开销**：每个节点至少 2 个指针（16 字节）+ `malloc` 块头 + 对齐填充。100 万个 `int`：`vector` 约 3.8 MB，`list` 至少 19 MB（实测常达 40~60 MB）。

`list` 只在「已持有迭代器 + 元素移动成本极高」或「需要 `splice` 常数时间接合」时才有优势 —— 典型场景是 LRU 缓存。
</details>

---

### 题 4：这段代码有什么问题？

```cpp
std::map<std::string, int> counts;
for (const auto& w : words) {
    if (counts[w] == 0) {
        std::cout << "new word: " << w << "\n";
    }
}
```

<details>
<summary>答案</summary>

**`counts[w]` 会在 `w` 不存在时插入 `{w, 0}`**，所以：

1. 循环结束后 `counts.size()` 会变成「所有出现过的词」的数量 —— 即使本意只是「检查一下」。如果后面的逻辑依赖 `size()`，就全错了。
2. `Value` 类型必须可默认构造（这里是 `int`，没问题；如果换成不可默认构造的类型就直接编译失败）。
3. 在**循环条件**里用 `[]` 更危险：`while (m[k] < threshold)` 会不断插入新元素。

正确写法：

```cpp
if (counts.contains(w)) {                    // C++20
// 或者
if (counts.find(w) != counts.end()) {
// 或者（C++17）
if (counts.count(w) != 0) {
    std::cout << "new word: " << w << "\n";
}
```

**`operator[]` 的正当用途**是「统计计数 / 分组」：

```cpp
++counts[w];                    // 不存在就插入 0 再自增 —— 这才是它该被用的地方
groups[category].push_back(x);  // 不存在就建一个空组
```
</details>

---

### 题 5：什么情况下 `unordered_map` 的查找会退化到 O(n)？工程上怎么防？

<details>
<summary>答案</summary>

**当大量 key 落进同一个桶时**（哈希冲突严重），哈希表退化成链表，查找变成线性扫描。

两个来源：

1. **哈希函数质量差**：比如自定义哈希写成 `return x ^ y;`，会让 `(1,2)` 和 `(2,1)` 碰撞；或者把所有 key 都映射到同一个值。实测全碰撞时比正常哈希慢 **409 倍（Debug）/ 623 倍（Release）**。
2. **恶意构造的 key**：如果 key 来自用户输入（HTTP 头、JSON 字段名、查询参数），攻击者可以离线构造一批「在目标哈希函数下全部碰撞」的 key，让服务器 CPU 打满 —— 这叫 **HashDoS / 哈希碰撞攻击**。

防御：

1. **用带随机种子的哈希**。libstdc++ / libc++ 默认对字符串哈希加随机种子；**MSVC 的 `std::hash<int>` 是恒等映射且无随机种子**，处理不可信整数 key 时要特别注意。
2. **限制**单个请求能产生的 key 数量（比如限制 JSON 对象字段数）。
3. **对完全不可信的输入改用 `std::map`** —— O(log n) 是确定性上界，「平均快但可被打崩」不适合暴露在公网的接口。
4. 已知元素个数时 `reserve`，避免 rehash 的开销和迭代器失效（注意 rehash 不会改变最坏情况的复杂度）。
</details>

---

### 题 6：`erase` 循环的正确写法是什么？为什么 `remove_if` 更值得推荐？

<details>
<summary>答案</summary>

**错误写法**：

```cpp
for (auto it = v.begin(); it != v.end(); ++it) {
    if (cond(*it)) v.erase(it);   // ✗ erase 后 it 失效，++it 是 UB
}
```

**迭代器版**（通用）：

```cpp
for (auto it = v.begin(); it != v.end(); ) {
    if (cond(*it)) it = v.erase(it);   // ★ 用 erase 的返回值，不要再 ++
    else           ++it;
}
```

**erase-remove 惯用法**（推荐）：

```cpp
v.erase(std::remove_if(v.begin(), v.end(), pred), v.end());
```

**为什么更推荐**：

- `remove_if` 是**一次前移扫描**，总复杂度 O(n)；
- 循环 `erase` 每删一个就要把尾巴整体前移一次，最坏 **O(n²)**；
- `remove_if` 只做「移动赋值」不做「多次搬移」，对非平凡类型也更友好。

**必须知道的一点**：`remove_if` **不删除元素**，它只把要保留的元素前移到前面并返回新的逻辑结尾，容器 `size()` 不变。所以必须补 `erase(newEnd, end())`。

**C++20 起**可以一行：

```cpp
std::erase_if(v, pred);   // 返回删除的个数；注意它是非成员函数，参数是容器本身
```
</details>

---

### 题 7：`std::from_chars` 相比 `std::stoi` 有哪些优势？什么场景仍该用 `stoi`？

<details>
<summary>答案</summary>

**优势**：

1. **不抛异常** — 错误通过 `{ptr, ec}` 返回值报告，可用于 `noexcept` 函数和热路径。
2. **完全不受 locale 影响** — `std::stod` 在德语等 locale 下小数点会变成逗号，协议解析会直接崩；`from_chars` 永远是 `'.'`。
3. **更快** — 不需要构造临时 `std::string` / `istringstream`，没有内存分配。实测（Release）：整数 **16.3 倍**，浮点 **4.4 倍**。
4. **接受 `[first, last)` 区间** — 不需要 `'\0'` 结尾，天然配合 `string_view`，解析 CSV 时「值后面紧跟分隔符」是天然合适的。
5. **行为可预测** — 不跳前导空白、不多消费字符，`"12abc"` 会诚实地告诉你「消费了 2 个字符，还剩 3 个」。

**限制**：C++17 的 `from_chars` 对浮点支持是「可选」的（MSVC / libstdc++ / libc++ 都支持，老编译器可能只有整数版）。

**仍该用 `stoi` / `stod` 的场景**：

- 输入来自人类、希望用异常统一处理错误的一次性交互代码
- 已经在一个 `try/catch` 上下文里，不想为转换单独写错误分支
- 需要「跳过前导空白」这个宽松行为（`from_chars` 不跳，得自己 trim）

**注意 `stoi` 的坑**：`stoi("3.14")` 得到 **3** 且不报错（停在 `'.'`）。
</details>

---

### 题 8：`std::endl` 和 `'\n'` 有什么区别？为什么推荐用 `'\n'`？

<details>
<summary>答案</summary>

- `'\n'` 只是往缓冲区里写一个换行符。
- `std::endl` 做**两件事**：写一个 `'\n'`，然后**强制刷新（flush）**缓冲区到设备。

**实测**（写 2 万行到文件，`10_iostream_and_files.cpp`）：`'\n'` 25632 us vs `std::endl` 75957 us，**约 3 倍**（Release 同量级；磁盘越慢差距越大）。

**该用 `std::endl` 的场景**（「需要立刻看到输出」）：

- 崩溃前的最后一条日志 —— 不 flush 可能就丢了
- 交互式提示语（`std::cout << "请输入：" << std::endl;`）—— 否则重定向到管道时用户看不到提示
- 调试时想立刻确认某一步执行到了

**其它情况一律用 `'\n'`**，包括正常日志输出、循环里打印。

**相关提醒**：

- 交互式提示语如果**不以换行结尾**，`std::endl` 也救不了，要用 `std::cout.flush()` 或 `std::cout << ... << std::flush;`
- `std::cerr` 默认是**不缓冲**的（等价于每次都 flush），所以往 `cerr` 写大量日志反而慢
- 需要更高吞吐时可以考虑 `std::ios::sync_with_stdio(false)` + `std::cin.tie(nullptr)`（代价是不能和 C 的 `printf` / `scanf` 混用）
</details>

---

## 10. 附录

### 10.1 原素材保留位置

以下目录**原样保留**，作为本章的原始素材与历史对照，**未做任何修改**：

| 路径 | 内容 |
|---|---|
| `C:\Users\Waj07\Desktop\CPPstudy\模块\` | 最初的模块化「C 标准库头文件用法示例」（9 个模块 + 菜单式 main） |
| `C:\Users\Waj07\Desktop\CPPstudy\math.h\math_h_参考手册.txt` | `<math.h>` 速查手册（函数总表、15 个坑、常用套路、宏与常量） |
| `C:\Users\Waj07\Desktop\CPPstudy\math.h\math_demo.cpp` | 18 个数学函数可运行演示（含 `ConsoleBuf` 中文输出技巧） |

本章与它们的关系：

- **01–05** 是原「模块」素材的整合与纠错（去掉了头文件/实现分离，改成自带 `main()` 的独立文件）
- **04_cmath.cpp** 额外吸收了 `math.h_参考手册.txt` 的要点（`atan2` 判象限、`acos` 的 clamp、`log1p` / `expm1` 的精度优势、`hypot` 的防溢出、浮点判等的两种容差）
- **06–10** 是原素材完全没有的新增内容（`std::string` 深挖、序列/关联容器、适配器与算法串讲、`iostream` 与文件）

### 10.2 本机 MSVC 14.51 / `/std:c++20` 的库特性实测

| 特性 | 可用性 | 说明 |
|---|---|---|
| `std::format`（`<format>`） | **可用**（`__cpp_lib_format = 202304`） | 本仓库用它做格式化演示 |
| `std::format` 的 `{:,}` 千分位、`{:.1%}` 百分号 | **不可用** | 编译报 `C7595`；变通：自己插逗号 / 乘 100 再手工加 `%` |
| `std::ranges`（`<ranges>`） | **可用**（`__cpp_lib_ranges = 202110`） | 支持投影，见 `09` 章 |
| `std::span` / `std::numbers` / `std::from_chars`（含浮点） | **可用** | |
| `std::erase` / `std::erase_if`（容器级 free function） | **可用**（`_HAS_CXX20`） | |
| `std::flat_map`（`<flat_map>`） | **不可用** | 头文件存在，但内容被 `#if !_HAS_CXX23` 挡住，报 `STL4038` + `C2039`。需要 `/std:c++23preview`；本仓库统一 `/std:c++20`，所以 `08` 章用「有序 vector + `lower_bound`」手工演示 flat_map 思想 |
| `std::string::contains` / `string_view::contains` | **不可用**（C++23） | C++20 判包含仍用 `find(...) != npos` |
| `std::print`（`<print>`） | **不可用** | 同 flat_map，需 C++23 |
| `std::strtok_s` | **不存在** | MSVC 的安全版是**全局命名空间**的 `strtok_s`（Annex K 扩展） |
| C++20 日历（`year_month_day` / `sys_days` / `2026y/March/1d`） | **可用** | 见 `05` 章 |
| `std::format` 格式化时间点（`{:%F %T}`） | **可用** | |
