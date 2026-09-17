// ============================================================================
// 05_hash_table.cpp
// 演示主题：
//   1. 散列函数：整数（除留余数法 vs Knuth 乘法散列）、字符串（FNV-1a vs djb2）
//   2. 为什么表长 m 要取质数，以及 m 取 2 的幂时低位分布为什么会变差
//   3. 冲突解决：链地址法、开放寻址（线性探测 / 二次探测）
//   4. 负载因子与 rehash：为什么扩容 + 重散列是均摊 O(1)
//   5. 删除：开放寻址必须用墓碑（tombstone），直接置空会截断探测链
//   6. std::hash 的用法、自定义类型的哈希特化、标准哈希组合公式
//   7. 一个可用的 MyHashMap<K,V>（模板、迭代器、完整 Rule of Five）
//   8. 与 std::unordered_map 逐步对拍，以及撞库退化成 O(n) 的实测
//
// 关键结论：
//   1. 散列表的 O(1) 是「假设哈希均匀」换来的概率性结论；哈希一旦退化，
//      所有操作立刻退化成 O(n)——本文最后一节用撞库把这件事量化出来；
//   2. 除留余数法里 m 取质数，是为了让「key 的步长」与 m 互质；
//      若 gcd(步长, m) = g，则 key 只能落进 m/g 个桶。m = 2^k 时步长 2^k 直接退化成 1 个桶；
//   3. 乘法散列取的是乘积的高位，天然回避了「低位有规律」的问题，不挑 m；
//   4. 链地址法删除最省事（改指针），开放寻址的成本几乎全在删除上——
//      必须写墓碑，否则探测链被截断，后面的元素永远找不到；
//   5. 链地址法的 rehash 只需要重新挂链（节点不搬家），所以引用/指针在 rehash 后依然有效；
//      开放寻址的 rehash 会真的搬元素，之前的引用全部失效；
//   6. 「均摊 O(1)」不是空话：实测插入 10 万个元素，rehash 一共搬移的节点数只有 n 的
//      一倍多一点，摊到每次插入就是常数；
//   7. 线性探测会产生一次聚集（primary clustering），二次探测能缓解它，
//      但仍有二次聚集；要让二次探测保证「表没满就一定能插入」，表长得取质数。
//
// 说明：本文件在 Debug（/Od）下编译运行，绝对耗时比 Release 慢很多，
//       所有数字仅供参考，看「趋势」和「数量级差异」才有意义。
//       教学用途，生产请用 std::unordered_map。
// ============================================================================

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
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

void PrintSeparator(std::size_t width) { std::cout << "  " << std::string(width, '-') << "\n"; }

// 固定小数位数的格式化：std::to_string(0.25) 会得到 "0.250000"，表格里太难看。
std::string FormatFixed(double value, int decimals) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(decimals) << value;
    return out.str();
}

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
// 散列函数只要几十纳秒，而计时器分辨率在百纳秒量级，直接测一次测到的全是噪声。
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
// 第 0 部分：散列函数
// ===========================================================================
// 一个好的散列函数要满足三件事：
//   1) 确定性：同一个 key 永远得到同一个值（否则存进去就找不回来了）；
//   2) 均匀性：不同的 key 尽量均匀铺满所有桶，与 key 的分布无关；
//   3) 雪崩性：key 改一位，散列值的变化应该像掷骰子（理想情况下约一半的位翻转）。
// 注意：这些都不是密码学哈希。FNV / djb2 都能被反向构造出碰撞，绝对不能用于
// 密码存储或防篡改校验（那些场景要用 SHA-256、BLAKE3 之类的密码学哈希）。
// ===========================================================================

// ---------------------------------------------------------------------------
// 整数散列之一：除留余数法
// ---------------------------------------------------------------------------
// 这是最简单的散列：hash = key % m。
//
// 为什么 m 要取质数？关键在于 gcd(key 的步长, m)。
//   如果 key 是等差数列 a, a+d, a+2d, ...，那么 key % m 只会取到
//   「模 m 下 gcd(d, m) 的倍数」那些值，也就是说最多只能用到 m / gcd(d, m) 个桶。
//   - d = 1024, m = 1024  => gcd = 1024 => 只能用 1 个桶（灾难）；
//   - d = 1024, m = 1021  => gcd = 1    => 1021 个桶全都用得上（最好）。
//   取质数并不能保证 bucket 分布均匀（那取决于 key），但它保证 m 没有小因子，
//   于是「步长」里常见的那些因子（2、4、8、10、1000）都不可能和 m 有公因数。
//
// m 取 2 的幂（key & (m-1)）虽然快，但等价于「只保留低 k 位」：
//   低位一旦有规律（内存地址按 4/8 对齐、ID 是连续的、时间戳低 10 位不变），
//   高位携带的信息就被彻底丢掉了，分布立刻塌缩。这就是「低位分布变差」的含义。
std::size_t HashDivideModulo(std::uint64_t key, std::size_t bucket_count) {
    return static_cast<std::size_t>(key % bucket_count);
}

// ---------------------------------------------------------------------------
// 整数散列之二：Knuth 乘法散列
// ---------------------------------------------------------------------------
// 公式：h(key) = floor(m * frac(key * A))，其中 A ≈ (sqrt(5) - 1) / 2 = 0.6180339887...
// 工程写法：用 2654435761 / 2^32 当作 A 的定点近似（这就是 Knuth 那个常数的来源），
//   product  = key * 2654435761          // 低 32 位是小数部分 frac(key * A) 的定点表示
//   h        = (product 的低 32 位) * m >> 32   // 取高位
//
// 为什么取高位就能救回低位规律？因为乘法会把 key 的每一位都「搅」进结果的高位里：
//   低位的变化会通过进位传播到高位。所以低位再有规律，高位看起来仍然杂乱。
// 注意乘法的溢出在这里是特性不是 bug——我们故意只用 32 位定点小数，
// 丢弃的是整数部分（floor 那一步），保留的正是小数部分。
std::size_t HashMultiplyKnuth(std::uint64_t key, std::size_t bucket_count) {
    const std::uint64_t kKnuthConstant = 2654435761ULL;  // 2^32 / phi
    const std::uint64_t product = key * kKnuthConstant;
    const std::uint64_t fraction = product & 0xFFFFFFFFULL;  // 小数部分的 32 位定点表示
    return static_cast<std::size_t>((fraction * bucket_count) >> 32);
}

// ---------------------------------------------------------------------------
// 字符串散列之一：FNV-1a
// ---------------------------------------------------------------------------
// FNV-1a 的过程只有两步：先异或、再乘。
//   hash = 14695981039346656037  (64 位 FNV offset basis)
//   对每个字节：hash ^= byte; hash *= 1099511628211  (FNV prime)
//
// 为什么「先异或再乘」比「先乘再异或」好？乘法的进位传播会把刚刚异或进去的那一位
// 扩散到整个字里，所以每个字节都能影响最终结果的全部位——这就是雪崩性的来源。
// 优点是极快（每字节一次异或 + 一次乘法）且分布不错，缺点是完全可逆，
// 属于「非密码学哈希」，适合做散列表和去重指纹，不能用来防篡改。
std::size_t HashFnv1a(const std::string& text) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char ch : text) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ch));
        hash *= 1099511628211ULL;
    }
    return static_cast<std::size_t>(hash);
}

// ---------------------------------------------------------------------------
// 字符串散列之二：djb2
// ---------------------------------------------------------------------------
//   hash = 5381; 对每个字节：hash = hash * 33 + byte;
// 33 = 32 + 1，也就是 (hash << 5) + hash，编译器能把它编成一条移位加一条加法，
// 所以在老机器上非常快，几十年来一直是字符串散列的默认选择之一。
// 它的雪崩性弱于 FNV-1a（低位扩散慢），对短字符串和高度相似的字符串更容易聚集。
std::size_t HashDjb2(const std::string& text) {
    std::uint64_t hash = 5381;
    for (const char ch : text) {
        hash = hash * 33 + static_cast<std::uint64_t>(static_cast<unsigned char>(ch));
    }
    return static_cast<std::size_t>(hash);
}

// ---------------------------------------------------------------------------
// 反面教材：只把字节加起来
// ---------------------------------------------------------------------------
// 这个函数有三个致命问题，正好是「坏哈希」的标准样本：
//   1) 与字节顺序无关 => "ab" 和 "ba" 撞车；所有字母异位词（anagram）全撞；
//   2) 值域太窄 => 8 个字符以内的 ASCII 串，散列值最大也就 8*127 = 1016，
//      桶数一旦超过 1016，高位桶永远是空的；
//   3) 完全没有雪崩性 => 改一个字节只让结果变化 1，相邻 key 全挤在一起。
// 放在这里是为了让「直方图为什么会长成那样」有一个可对比的基准。
std::size_t HashWeakByteSum(const std::string& text) {
    std::uint64_t hash = 0;
    for (const char ch : text) {
        hash += static_cast<std::uint64_t>(static_cast<unsigned char>(ch));
    }
    return static_cast<std::size_t>(hash);
}

// ---------------------------------------------------------------------------
// 哈希组合：把多个字段的哈希拼成一个
// ---------------------------------------------------------------------------
// 这一行就是 Boost 的 hash_combine，也是 C++ 社区事实上的标准写法：
//   seed ^= h + 0x9e3779b97f4a7c15 + (seed << 6) + (seed >> 2);
// 其中 0x9e3779b97f4a7c15 是 2^64 / phi（黄金比例），它的二进制表示「没有规律」，
// 用来打散；先左移 6 位再右移 2 位是让 seed 的高位和低位互相参与运算。
// 为什么不能直接相加？因为 (a,b) 和 (b,a) 会得到同样的结果，而且 (1,2) 与 (2,1)
// 这类「分量交换」的碰撞在真实数据里非常常见。
std::size_t CombineHash(std::size_t seed, std::size_t value) {
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
}

// ---------------------------------------------------------------------------
// 质数工具（开放寻址的表长要用质数）
// ---------------------------------------------------------------------------
bool IsPrime(std::size_t n) {
    if (n < 2) {
        return false;
    }
    if (n % 2 == 0) {
        return n == 2;
    }
    // 这里用 d * d <= n 判断上界；n 很小时不会溢出（本文件里 n 最大也就几十万）。
    for (std::size_t d = 3; d * d <= n; d += 2) {
        if (n % d == 0) {
            return false;
        }
    }
    return true;
}

std::size_t NextPrime(std::size_t n) {
    std::size_t candidate = n < 2 ? 2 : n;
    while (!IsPrime(candidate)) {
        ++candidate;
    }
    return candidate;
}

// ---------------------------------------------------------------------------
// 造数据
// ---------------------------------------------------------------------------
// 用「奇数乘法器」生成 key：i * 2654435761 在 32 位下是一个双射（因为乘数是奇数），
// 所以 keys 一定两两不同（不会因为重复 key 污染负载因子统计），
// 同时相邻 i 生成的 key 相差极大，看起来像随机数——很适合当散列表的输入。
std::vector<int> MakeDistinctKeys(std::size_t count, std::uint32_t salt) {
    std::vector<int> keys(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t mixed = static_cast<std::uint32_t>(i + 1u) * (2654435761u + salt);
        keys[i] = static_cast<int>(mixed);
    }
    return keys;
}

// 一份真实的英文词表（高频词 + 若干共享前缀的词），用来实测字符串散列的分布。
const std::vector<std::string>& RealWordSet() {
    static const std::vector<std::string> words = {
        "the", "of", "and", "to", "in", "a", "is", "that", "it", "for",
        "was", "as", "with", "be", "by", "on", "not", "he", "this", "are",
        "or", "his", "from", "at", "which", "but", "have", "an", "had", "they",
        "you", "were", "their", "one", "all", "we", "can", "her", "has", "there",
        "been", "if", "more", "when", "will", "would", "who", "so", "no", "she",
        "other", "its", "may", "what", "time", "up", "out", "them", "then", "these",
        "some", "two", "into", "only", "very", "after", "first", "any", "new", "now",
        "such", "like", "our", "over", "man", "even", "most", "made", "also", "did",
        "many", "before", "must", "through", "back", "years", "where", "much", "your", "way",
        "well", "down", "should", "because", "each", "just", "those", "people", "how", "too",
        "little", "state", "good", "make", "world", "still", "own", "see", "men", "work",
        "long", "get", "here", "between", "both", "life", "being", "under", "never", "day",
        "same", "another", "know", "while", "last", "might", "us", "great", "old", "year",
        "understand", "understanding", "understood", "underground", "underlying", "underscore",
        "connect", "connected", "connection", "connector", "disconnect", "reconnect",
        "hash", "hashed", "hashing", "hashtable", "hashmap", "hashset",
        "table", "tables", "tablet", "tabular", "tabulate", "tabulation",
    };
    return words;
}

// 合成一批「工程里真的会见到」的字符串键：前缀 + 数字。
std::vector<std::string> MakeIdentifierSet(std::size_t count, unsigned seed) {
    static const char* const kPrefixes[] = {"user",  "order", "item",   "session", "cache",
                                            "request", "handler", "buffer", "config", "payload"};
    constexpr std::size_t kPrefixCount = sizeof(kPrefixes) / sizeof(kPrefixes[0]);

    std::mt19937 rng(seed);
    std::vector<std::string> keys;
    keys.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t which = static_cast<std::size_t>(rng()) % kPrefixCount;
        keys.push_back(std::string(kPrefixes[which]) + "_" +
                       std::to_string(static_cast<unsigned>(rng() % 1000000u)));
    }
    return keys;
}

// ---------------------------------------------------------------------------
// 桶分布统计
// ---------------------------------------------------------------------------
struct BucketStats {
    std::size_t bucket_count = 0;
    std::size_t keys = 0;
    std::size_t occupied = 0;   // 至少装了 1 个元素的桶数
    std::size_t max_chain = 0;  // 最长链（也就是最坏情况的查找长度）
    // histogram[i] = 装了恰好 i 个元素的桶数；最后一格 kHistogramMax 表示「>= 这么多」。
    std::vector<std::size_t> histogram;
};

constexpr std::size_t kHistogramMax = 5;

template <typename Key, typename HashFn>
BucketStats MeasureDistribution(const std::vector<Key>& keys, std::size_t bucket_count, HashFn hash_fn) {
    BucketStats stats;
    stats.bucket_count = bucket_count;
    stats.keys = keys.size();

    std::vector<std::size_t> counts(bucket_count, 0);
    for (const Key& key : keys) {
        ++counts[hash_fn(key) % bucket_count];
    }

    stats.histogram.assign(kHistogramMax + 1, 0);
    for (const std::size_t count : counts) {
        if (count > 0) {
            ++stats.occupied;
        }
        if (count > stats.max_chain) {
            stats.max_chain = count;
        }
        ++stats.histogram[count > kHistogramMax ? kHistogramMax : count];
    }
    return stats;
}

void PrintBucketStats(const std::string& name, const BucketStats& stats) {
    Label("  " + name, 24);
    std::cout << " 占用桶 " << stats.occupied << "/" << stats.bucket_count
              << "  冲突元素 " << (stats.keys - stats.occupied)
              << "  最长链 " << stats.max_chain << "\n";

    std::cout << "    桶长直方图(0.." << kHistogramMax << "+): ";
    for (std::size_t i = 0; i < stats.histogram.size(); ++i) {
        std::cout << i << (i + 1 == stats.histogram.size() ? "+:" : ":") << stats.histogram[i] << " ";
    }
    std::cout << "\n";
}

// 均匀散列下「冲突元素占比」的理论值：1 - (1 - e^-alpha) / alpha，alpha = n / m。
// 用来判断实测到的冲突率是「哈希不行」还是「生日悖论本来就这样」。
double TheoreticalCollisionRate(double load_factor) {
    if (load_factor <= 0.0) {
        return 0.0;
    }
    return 1.0 - (1.0 - std::exp(-load_factor)) / load_factor;
}

// ===========================================================================
// 1. 链地址法 + 一个能用的 MyHashMap<K,V>
// ===========================================================================
// 结构：一个「桶头指针数组」，每个桶挂一条单链表，冲突的元素就挂在同一条链上。
//
// 时间复杂度（平均 / 最坏）：
//   insert / find / erase ：O(1 + alpha) / O(n)，alpha = n / m 是负载因子。
//   平均情况成立的前提是「散列均匀」，最坏情况就是所有 key 撞进同一个桶（见第 8 节实测）。
// 空间复杂度：O(n + m)，每个元素额外背一个 next 指针（8 字节）。
//
// rehash：负载因子超过 max_load_factor 时把桶数组放大一倍并重新挂链。
//   - 注意链地址法的 rehash 只需要改指针，节点本身不搬家，
//     所以「rehash 之前拿到的元素引用」在 rehash 之后依然有效（std::unordered_map 也保证这点）；
//   - 均摊 O(1)：总搬移次数约为 n * (1 + 1/2 + 1/4 + ...) = 2n，摊到 n 次插入上是常数。
//
// 教学用途，生产请用 std::unordered_map。
template <typename K, typename V, typename Hash = std::hash<K>>
class MyHashMap {
private:
    struct Node {
        std::pair<K, V> entry;
        Node* next;
        Node(const K& key, const V& value, Node* next_node) : entry(key, value), next(next_node) {}
    };

public:
    // 只读前向迭代器：支持范围 for 和 std::vector 的范围构造，但不允许改 key。
    // 这里两个 begin() 都返回它，所以 for (auto& kv : map) 这种「想改值」的写法不支持——
    // 教学代码里明确一点，比偷偷给出一个会破坏哈希不变量的可变引用要安全。
    class ConstIterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::pair<K, V>;
        using difference_type = std::ptrdiff_t;
        using pointer = const value_type*;
        using reference = const value_type&;

        ConstIterator() = default;
        ConstIterator(const MyHashMap* owner, std::size_t bucket, Node* node)
            : owner_(owner), bucket_(bucket), node_(node) {}

        reference operator*() const { return node_->entry; }
        pointer operator->() const { return &node_->entry; }

        ConstIterator& operator++() {
            node_ = node_->next;
            if (node_ == nullptr) {
                AdvanceBucket();
            }
            return *this;
        }

        ConstIterator operator++(int) {
            ConstIterator copy = *this;
            ++(*this);
            return copy;
        }

        bool operator==(const ConstIterator& other) const { return node_ == other.node_; }
        bool operator!=(const ConstIterator& other) const { return node_ != other.node_; }

    private:
        // 当前链走完了，去后面找一个非空桶。
        void AdvanceBucket() {
            ++bucket_;
            const std::size_t count = owner_->bucket_count();
            while (bucket_ < count && owner_->buckets_[bucket_] == nullptr) {
                ++bucket_;
            }
            node_ = bucket_ < count ? owner_->buckets_[bucket_] : nullptr;
        }

        const MyHashMap* owner_ = nullptr;
        std::size_t bucket_ = 0;
        Node* node_ = nullptr;
    };

    MyHashMap() : MyHashMap(kDefaultBucketCount, kDefaultMaxLoadFactor) {}

    explicit MyHashMap(std::size_t bucket_count, float max_load_factor = kDefaultMaxLoadFactor)
        : buckets_(bucket_count < 1 ? 1 : bucket_count, nullptr), max_load_(max_load_factor) {}

    ~MyHashMap() { ClearNodes(); }

    MyHashMap(const MyHashMap& other)
        : buckets_(other.buckets_.size(), nullptr), max_load_(other.max_load_), hasher_(other.hasher_) {
        try {
            CloneFrom(other);
        } catch (...) {
            ClearNodes();  // 拷贝到一半抛异常：把自己已经挂上的节点全部释放
            throw;
        }
    }

    MyHashMap& operator=(const MyHashMap& other) {
        if (this != &other) {
            MyHashMap tmp(other);  // 先完整拷贝成功，再交换（强异常安全）
            Swap(tmp);
        }
        return *this;
    }

    MyHashMap(MyHashMap&& other) noexcept
        : buckets_(std::move(other.buckets_)),
          size_(other.size_),
          collisions_(other.collisions_),
          max_load_(other.max_load_),
          hasher_(std::move(other.hasher_)) {
        other.buckets_.clear();
        other.size_ = 0;
        other.collisions_ = 0;
    }

    MyHashMap& operator=(MyHashMap&& other) noexcept {
        if (this != &other) {
            ClearNodes();
            buckets_ = std::move(other.buckets_);
            size_ = other.size_;
            collisions_ = other.collisions_;
            max_load_ = other.max_load_;
            hasher_ = std::move(other.hasher_);
            other.buckets_.clear();
            other.size_ = 0;
            other.collisions_ = 0;
        }
        return *this;
    }

    // 有则更新、无则插入；返回 true 表示这次是新插入的。
    bool insert(const K& key, const V& value) {
        const std::size_t index = BucketIndex(key);
        for (Node* node = buckets_[index]; node != nullptr; node = node->next) {
            if (node->entry.first == key) {
                node->entry.second = value;  // 键已存在：只更新值，个数不变
                return false;
            }
        }
        if (buckets_[index] != nullptr) {
            ++collisions_;  // 这次插入发生了冲突（新元素要挂到已有链上）
        }
        // 头插法：O(1)。新节点插在链首，顺便让「最近插入的」访问最快。
        buckets_[index] = new Node(key, value, buckets_[index]);
        ++size_;
        if (load_factor() > max_load_) {
            Rehash(buckets_.size() * 2);
        }
        return true;
    }

    // 键不存在就插入一个「值初始化」的 V（和 std::unordered_map 的 operator[] 语义一致）。
    V& operator[](const K& key) {
        const std::size_t index = BucketIndex(key);
        for (Node* node = buckets_[index]; node != nullptr; node = node->next) {
            if (node->entry.first == key) {
                return node->entry.second;
            }
        }
        if (buckets_[index] != nullptr) {
            ++collisions_;
        }
        Node* fresh = new Node(key, V{}, buckets_[index]);
        buckets_[index] = fresh;
        ++size_;
        if (load_factor() > max_load_) {
            // rehash 只重新挂链、不动节点，所以下面返回的引用依然有效。
            Rehash(buckets_.size() * 2);
        }
        return fresh->entry.second;
    }

    V& at(const K& key) {
        V* found = FindValue(key);
        if (found == nullptr) {
            throw std::out_of_range("MyHashMap::at：键不存在");
        }
        return *found;
    }

    const V& at(const K& key) const {
        const V* found = FindValue(key);
        if (found == nullptr) {
            throw std::out_of_range("MyHashMap::at：键不存在");
        }
        return *found;
    }

    ConstIterator find(const K& key) const {
        const std::size_t index = BucketIndex(key);
        for (Node* node = buckets_[index]; node != nullptr; node = node->next) {
            ++probe_count_;  // 统计用：走过了多少个节点
            if (node->entry.first == key) {
                return ConstIterator(this, index, node);
            }
        }
        return end();
    }

    bool contains(const K& key) const { return FindValue(key) != nullptr; }

    bool erase(const K& key) {
        const std::size_t index = BucketIndex(key);
        Node* node = buckets_[index];
        Node* previous = nullptr;
        while (node != nullptr) {
            if (node->entry.first == key) {
                if (previous == nullptr) {
                    buckets_[index] = node->next;
                } else {
                    previous->next = node->next;
                }
                delete node;  // 链地址法的删除就是这么简单：改指针 + 释放节点
                --size_;
                return true;
            }
            previous = node;
            node = node->next;
        }
        return false;
    }

    void clear() noexcept { ClearNodes(); }

    // 预留至少能装 count 个元素而不触发 rehash 的桶数。
    void reserve(std::size_t count) {
        const double needed = static_cast<double>(count) / static_cast<double>(max_load_) + 1.0;
        Rehash(static_cast<std::size_t>(needed));
    }

    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }
    float load_factor() const noexcept {
        return static_cast<float>(static_cast<double>(size_) / static_cast<double>(buckets_.size()));
    }
    float max_load_factor() const noexcept { return max_load_; }
    void max_load_factor(float value) noexcept { max_load_ = value; }

    std::size_t bucket_count() const noexcept { return buckets_.size(); }

    std::size_t bucket_size(std::size_t index) const {
        std::size_t count = 0;
        for (Node* node = buckets_[index]; node != nullptr; node = node->next) {
            ++count;
        }
        return count;
    }

    std::size_t max_chain_length() const {
        std::size_t longest = 0;
        for (std::size_t i = 0; i < buckets_.size(); ++i) {
            const std::size_t len = bucket_size(i);
            if (len > longest) {
                longest = len;
            }
        }
        return longest;
    }

    // 插入时「桶非空」的次数：直接反映了哈希质量与负载因子。
    // 注意 Rehash 会把它清零重新统计。
    std::size_t collision_count() const noexcept { return collisions_; }
    std::size_t rehash_count() const noexcept { return rehash_count_; }
    std::uint64_t rehash_moves() const noexcept { return rehash_moves_; }

    std::uint64_t probe_count() const noexcept { return probe_count_; }
    void reset_probe_count() const noexcept { probe_count_ = 0; }

    ConstIterator begin() const {
        for (std::size_t i = 0; i < buckets_.size(); ++i) {
            if (buckets_[i] != nullptr) {
                return ConstIterator(this, i, buckets_[i]);
            }
        }
        return end();
    }

    ConstIterator end() const { return ConstIterator(this, buckets_.size(), nullptr); }

private:
    static constexpr std::size_t kDefaultBucketCount = 16;
    static constexpr float kDefaultMaxLoadFactor = 0.75f;

    std::size_t BucketIndex(const K& key) const { return hasher_(key) % buckets_.size(); }

    Node* FindNode(const K& key) const {
        const std::size_t index = BucketIndex(key);
        for (Node* node = buckets_[index]; node != nullptr; node = node->next) {
            ++probe_count_;
            if (node->entry.first == key) {
                return node;
            }
        }
        return nullptr;
    }

    V* FindValue(const K& key) {
        Node* node = FindNode(key);
        return node == nullptr ? nullptr : &node->entry.second;
    }

    const V* FindValue(const K& key) const {
        const Node* node = FindNode(key);
        return node == nullptr ? nullptr : &node->entry.second;
    }

    void ClearNodes() noexcept {
        for (Node*& head : buckets_) {
            while (head != nullptr) {
                Node* victim = head;
                head = head->next;
                delete victim;
            }
        }
        size_ = 0;
        collisions_ = 0;
    }

    void Swap(MyHashMap& other) noexcept {
        std::swap(buckets_, other.buckets_);
        std::swap(size_, other.size_);
        std::swap(collisions_, other.collisions_);
        std::swap(max_load_, other.max_load_);
        std::swap(hasher_, other.hasher_);
    }

    void CloneFrom(const MyHashMap& other) {
        for (std::size_t i = 0; i < other.buckets_.size(); ++i) {
            Node* tail = nullptr;
            for (Node* node = other.buckets_[i]; node != nullptr; node = node->next) {
                // 保持原有的桶归属和链内顺序，这样 bucket_size / max_chain 也完全一致。
                Node* fresh = new Node(node->entry.first, node->entry.second, nullptr);
                if (tail == nullptr) {
                    buckets_[i] = fresh;
                } else {
                    tail->next = fresh;
                }
                tail = fresh;
            }
        }
        size_ = other.size_;
        collisions_ = other.collisions_;
    }

    // 只重新挂链，不重新分配节点——这是链地址法 rehash 的核心优势。
    void Rehash(std::size_t new_bucket_count) {
        if (new_bucket_count < 1) {
            new_bucket_count = 1;
        }
        std::vector<Node*> fresh(new_bucket_count, nullptr);
        std::uint64_t moved = 0;
        for (Node* head : buckets_) {
            Node* node = head;
            while (node != nullptr) {
                Node* next = node->next;
                const std::size_t index = hasher_(node->entry.first) % new_bucket_count;
                node->next = fresh[index];
                fresh[index] = node;
                ++moved;
                node = next;
            }
        }
        buckets_.swap(fresh);
        ++rehash_count_;
        rehash_moves_ = rehash_moves_ + moved;  // 实测「均摊 O(1)」用的计数器
        collisions_ = 0;                        // 布局变了，冲突计数重新开始统计
    }

    std::vector<Node*> buckets_;
    std::size_t size_ = 0;
    std::size_t collisions_ = 0;
    float max_load_ = kDefaultMaxLoadFactor;
    Hash hasher_{};

    // 下面几个只是统计量，不属于「值语义」的一部分，所以不进拷贝 / 移动的初始化列表，
    // 每次构造都是从 0 开始。
    mutable std::uint64_t probe_count_ = 0;
    std::size_t rehash_count_ = 0;
    std::uint64_t rehash_moves_ = 0;
};

// 撞库演示用：所有 key 都返回同一个散列值，模拟「哈希函数被攻击者打穿」的场景。
template <typename K>
struct ConstantHasher {
    std::size_t operator()(const K&) const noexcept { return 0; }
};

// ===========================================================================
// 2. 开放寻址：线性探测 / 二次探测（含墓碑）
// ===========================================================================
// 结构：一个「槽位数组」，所有元素都直接存在数组里，不用链表。
//   - 缓存友好：探测就是在数组里顺序/跳跃扫描，没有指针追逐；
//   - 省内存：不需要 next 指针，也不需要每元素一次堆分配；
//   - 代价：删除变复杂（必须写墓碑），并且负载因子不能高（一般压在 0.5 左右）。
//
// 三种槽位状态，缺一不可：
//   Empty     ：从建表以来这里从没放过元素 => 探测到这里可以断定「key 不存在」；
//   Occupied  ：放着真正的元素；
//   Tombstone ：放过元素、后来被删了 => 探测必须越过它继续找，但新的插入可以复用它。
//
// 线性探测（linear probing）：probe(i) = (h + i) % m
//   会产生一次聚集（primary clustering）：一旦出现一段连续被占用的区域，
//   所有散列到这段区域里的 key 都会排队继续往后找，越排越长，
//   于是平均探测长度按 (1 + 1/(1-alpha)^2) / 2 爆炸式增长。
//
// 二次探测（quadratic probing）：probe(i) = (h + i^2) % m
//   不同 key 即使初始桶相同，后续探测的步长也按 1, 3, 5, 7... 拉开，
//   所以不会形成长条形聚集，缓解了一次聚集。
//   但它仍有二次聚集（secondary clustering）：初始桶相同的 key 探测序列完全一样。
//   另外，探测序列不再是「所有槽位」，能不能覆盖到空槽取决于 m 和探测函数：
//   表长 m 取质数时，i = 0..(m-1)/2 的 i^2 mod m 互不相同，
//   只要负载因子压在一半以下，就一定能在前半段找到空槽，从而保证插入成功。
//   （如果表长是 2 的幂，用 i^2 只能覆盖约一半槽位，必须换成三角数 i(i+1)/2。）
//
// 时间复杂度：平均 O(1 / (1 - alpha))，最坏 O(n)；空间 O(m)。
// 教学用途，生产请用 std::unordered_map（它是链地址法系）。
enum class ProbeMode { Linear, Quadratic };

template <typename K, typename V, typename Hash = std::hash<K>, ProbeMode kMode = ProbeMode::Linear>
class OpenAddressTable {
private:
    enum class SlotState : unsigned char { Empty, Occupied, Tombstone };

    struct Slot {
        K key{};
        V value{};
        SlotState state = SlotState::Empty;
    };

    static constexpr std::size_t kNoSlot = static_cast<std::size_t>(-1);

public:
    explicit OpenAddressTable(std::size_t requested_slots, double max_load = 0.5)
        : slots_(NextPrime(requested_slots < 4 ? 4 : requested_slots)), max_load_(max_load) {}

    // 成员是 std::vector<Slot> 和几个标量，值语义天然正确，
    // 所以 Rule of Five 显式写成 default 就是完整且正确的版本。
    OpenAddressTable(const OpenAddressTable&) = default;
    OpenAddressTable& operator=(const OpenAddressTable&) = default;
    OpenAddressTable(OpenAddressTable&&) noexcept = default;
    OpenAddressTable& operator=(OpenAddressTable&&) noexcept = default;
    ~OpenAddressTable() = default;

    bool insert(const K& key, const V& value) { return InsertImpl(key, value, true); }

    bool contains(const K& key) const { return FindSlot(key) != kNoSlot; }

    V* find(const K& key) {
        const std::size_t index = FindSlot(key);
        return index == kNoSlot ? nullptr : &slots_[index].value;
    }

    const V* find(const K& key) const {
        const std::size_t index = FindSlot(key);
        return index == kNoSlot ? nullptr : &slots_[index].value;
    }

    // 删除是开放寻址最危险的操作：只能标记墓碑，绝不能置成 Empty。
    bool erase(const K& key) {
        const std::size_t index = FindSlot(key);
        if (index == kNoSlot) {
            return false;
        }
        slots_[index].state = SlotState::Tombstone;
        --size_;
        ++tombstones_;
        MaybeRehash();
        return true;
    }

    // 原地重散列：把墓碑全部清掉，槽位数不变。墓碑太多时应该调它。
    void cleanup_tombstones() { Rehash(slots_.size()); }

    void rehash(std::size_t requested_slots) { Rehash(requested_slots); }

    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }
    std::size_t slot_count() const noexcept { return slots_.size(); }
    std::size_t tombstone_count() const noexcept { return tombstones_; }

    double load_factor() const noexcept {
        return static_cast<double>(size_) / static_cast<double>(slots_.size());
    }

    // 平均探测长度（每个 find 平均访问了多少个槽位）：开放寻址最该盯住的指标。
    double mean_probe_length() const noexcept {
        return probe_lookups_ == 0
                   ? 0.0
                   : static_cast<double>(probe_count_) / static_cast<double>(probe_lookups_);
    }

    std::uint64_t probe_count() const noexcept { return probe_count_; }
    std::uint64_t probe_lookups() const noexcept { return probe_lookups_; }
    void reset_probe_stats() const noexcept {
        probe_count_ = 0;
        probe_lookups_ = 0;
    }

private:
    std::size_t Home(const K& key) const { return hasher_(key) % slots_.size(); }

    std::size_t ProbeIndex(std::size_t home, std::size_t step) const {
        if constexpr (kMode == ProbeMode::Linear) {
            return (home + step) % slots_.size();
        } else {
            return (home + step * step) % slots_.size();
        }
    }

    bool InsertImpl(const K& key, const V& value, bool allow_rehash) {
        std::size_t first_tombstone = kNoSlot;
        const std::size_t home = Home(key);

        for (std::size_t step = 0; step < slots_.size(); ++step) {
            const std::size_t index = ProbeIndex(home, step);
            Slot& slot = slots_[index];

            if (slot.state == SlotState::Empty) {
                // 探测链到此为止：key 一定不在表里。优先复用之前遇到的第一个墓碑，
                // 这样既不会让探测链变长，又顺便消耗掉一个墓碑。
                const std::size_t target = first_tombstone == kNoSlot ? index : first_tombstone;
                const bool reusing_tombstone = slots_[target].state == SlotState::Tombstone;
                slots_[target].key = key;
                slots_[target].value = value;
                slots_[target].state = SlotState::Occupied;
                if (reusing_tombstone) {
                    --tombstones_;
                }
                ++size_;
                if (allow_rehash) {
                    MaybeRehash();
                }
                return true;
            }
            if (slot.state == SlotState::Tombstone) {
                if (first_tombstone == kNoSlot) {
                    first_tombstone = index;
                }
                continue;  // 墓碑不能停：它后面可能还有同族的元素
            }
            if (slot.key == key) {
                slot.value = value;  // 键已存在
                return false;
            }
        }
        // 正常情况下走不到这里：负载因子被压在阈值以下，探测序列一定能碰到空槽。
        throw std::overflow_error("OpenAddressTable::insert：探测序列走完仍未找到空槽");
    }

    // 返回槽位下标，找不到返回 kNoSlot。命中墓碑必须继续找，命中 Empty 才能断定不存在。
    std::size_t FindSlot(const K& key) const {
        ++probe_lookups_;  // 每次「查找」计一次，循环里只累计「访问了多少个槽位」
        const std::size_t home = Home(key);
        for (std::size_t step = 0; step < slots_.size(); ++step) {
            const std::size_t index = ProbeIndex(home, step);
            ++probe_count_;
            const Slot& slot = slots_[index];
            if (slot.state == SlotState::Empty) {
                return kNoSlot;
            }
            if (slot.state == SlotState::Occupied && slot.key == key) {
                return index;
            }
        }
        return kNoSlot;
    }

    void MaybeRehash() {
        const double load_with_tombstones =
            static_cast<double>(size_ + tombstones_) / static_cast<double>(slots_.size());
        if (load_with_tombstones <= max_load_) {
            return;
        }
        if (tombstones_ > 0 && tombstones_ >= size_) {
            Rehash(slots_.size());  // 墓碑比有效元素还多：原地重散列，纯清理
        } else {
            Rehash(NextPrime(slots_.size() * 2));
        }
    }

    // 开放寻址的 rehash 会真的搬家（元素在数组里重新落位），
    // 所以 rehash 之前拿到的引用 / 指针全部失效——这一点和链地址法正好相反。
    void Rehash(std::size_t requested_slots) {
        const std::size_t target = NextPrime(requested_slots < 4 ? 4 : requested_slots);

        std::vector<Slot> old_slots;
        old_slots.swap(slots_);
        slots_.resize(target);  // slots_ 交换后是空的，resize 会把新槽位都初始化成 Empty
        size_ = 0;
        tombstones_ = 0;
        for (const Slot& slot : old_slots) {
            if (slot.state == SlotState::Occupied) {
                InsertImpl(slot.key, slot.value, false);
            }
        }
        ++rehash_count_;
    }

    std::vector<Slot> slots_;
    std::size_t size_ = 0;
    std::size_t tombstones_ = 0;
    double max_load_ = 0.5;
    Hash hasher_{};

    // 统计量（用 mutable 是为了让 const 的 find 也能计数），不参与值语义。
    mutable std::uint64_t probe_count_ = 0;
    mutable std::uint64_t probe_lookups_ = 0;
    std::size_t rehash_count_ = 0;

public:
    std::size_t rehash_count() const noexcept { return rehash_count_; }
};

// 两个常用的类型别名，省得每次写一长串模板参数。
using LinearIntTable = OpenAddressTable<int, int, std::hash<int>, ProbeMode::Linear>;
using QuadraticIntTable = OpenAddressTable<int, int, std::hash<int>, ProbeMode::Quadratic>;

// ---------------------------------------------------------------------------
// 「删除时直接置空」到底会坏成什么样：一个小规模可复现的演示
// ---------------------------------------------------------------------------
// 表长 8，散列函数简化成 key % 8。key = 3、11、19 的初始桶都是 3：
//   slots[3] = 3, slots[4] = 11, slots[5] = 19
// 现在删掉 3。如果直接把 slots[3] 置成「空」，那么查 11 走到 slots[3] 就停下了，
// 于是报告「11 不存在」——数据还在数组里，却永远找不到了。
// 换成墓碑（标记为「已删除」）后，查找会越过墓碑继续走到 slots[4]，正常命中。
bool DemonstrateBrokenProbeChain() {
    constexpr std::size_t kSlots = 8;
    constexpr int kEmpty = -1;
    constexpr int kTombstone = -2;

    std::vector<int> slots(kSlots, kEmpty);
    auto place = [&](int key) {
        std::size_t index = static_cast<std::size_t>(key % static_cast<int>(kSlots));
        while (slots[index] != kEmpty) {
            index = (index + 1) % kSlots;
        }
        slots[index] = key;
    };
    place(3);
    place(11);
    place(19);
    assert(slots[3] == 3 && slots[4] == 11 && slots[5] == 19);

    auto search = [&](int key) {
        std::size_t index = static_cast<std::size_t>(key % static_cast<int>(kSlots));
        while (slots[index] != kEmpty) {
            if (slots[index] == key) {
                return true;
            }
            index = (index + 1) % kSlots;
        }
        return false;
    };
    assert(search(11));  // 删除之前是找得到的

    // 错误做法：直接置空。
    slots[3] = kEmpty;
    const bool found_after_wrong_erase = search(11);

    // 正确做法：写墓碑。
    slots[3] = kTombstone;
    const bool found_after_tombstone = search(11);

    assert(!found_after_wrong_erase);   // 探测链被截断 -> 11 永远找不到
    assert(found_after_tombstone);      // 墓碑 -> 越过它继续找，命中
    return !found_after_wrong_erase && found_after_tombstone;
}

// ===========================================================================
// 3. 自定义类型的哈希
// ===========================================================================
// 容器要求两件事：能找到「相等」的元素，能算出「散列值」。
//   - 相等：C++20 之后一个 `bool operator==(const T&) const = default;` 就够了；
//   - 散列：要么给容器传一个哈希仿函数，要么特化 std::hash<T>。
//
// 特化 std::hash 必须写在 namespace std 里（那是主模板所在的命名空间），
// 而且必须在第一次「实例化用到它」之前出现。它属于「标准库允许你做的事」之一，
// 除此之外往 std 里塞任何东西都是未定义行为。
struct Student {
    std::string name;
    int id = 0;
    bool operator==(const Student&) const = default;  // C++20：自动生成逐成员比较
};

struct Point {
    int x = 0;
    int y = 0;
    bool operator==(const Point&) const = default;
};

// 另一种写法：不碰 std，直接给容器一个哈希仿函数。
struct PointHash {
    std::size_t operator()(const Point& p) const noexcept {
        std::size_t seed = 0;
        seed = CombineHash(seed, std::hash<int>{}(p.x));
        seed = CombineHash(seed, std::hash<int>{}(p.y));
        return seed;
    }
};

}  // namespace

// 特化 std::hash<Student>：标准写法就长这样，必须放在 namespace std 里。
// 两个字段用 CombineHash 组合——直接把 name 的哈希和 id 相加是新手最常见的错误，
// 那样 (name="a", id=1) 与 (name="b", id=... ) 之类的组合会成批撞车。
namespace std {
template <>
struct hash<Student> {
    std::size_t operator()(const Student& s) const noexcept {
        std::size_t seed = 0;
        seed = CombineHash(seed, std::hash<std::string>{}(s.name));
        seed = CombineHash(seed, std::hash<int>{}(s.id));
        return seed;
    }
};
}  // namespace std

namespace {

// ===========================================================================
// 4. 测试与测量
// ===========================================================================

// --- 第 1 节：整数散列，「m 取 2 的幂」到底会坏成什么样 ---------------------
void ReportIntegerHashing() {
    constexpr std::size_t kKeyCount = 1000;
    constexpr std::uint64_t kStride = 1024;  // key 全是 1024 的倍数，低位全是 0

    std::vector<std::uint64_t> keys;
    keys.reserve(kKeyCount);
    for (std::size_t i = 0; i < kKeyCount; ++i) {
        keys.push_back(kStride * (i + 1));
    }

    auto count_occupied = [&](std::size_t bucket_count, bool use_multiply) {
        std::vector<std::size_t> counts(bucket_count, 0);
        for (const std::uint64_t key : keys) {
            const std::size_t index = use_multiply ? HashMultiplyKnuth(key, bucket_count)
                                                   : HashDivideModulo(key, bucket_count);
            ++counts[index];
        }
        std::size_t occupied = 0;
        std::size_t longest = 0;
        for (const std::size_t count : counts) {
            if (count > 0) {
                ++occupied;
            }
            if (count > longest) {
                longest = count;
            }
        }
        return std::pair<std::size_t, std::size_t>(occupied, longest);
    };

    Label("  1000 个 key（全都是 1024 的倍数）散进不同表长", 44);
    std::cout << "\n";
    Label("  方案", 34);
    Label("表长 m", 10);
    Label("占用桶", 10);
    Label("最长链", 10);
    std::cout << "\n";
    PrintSeparator(64);

    const std::pair<std::size_t, std::size_t> power_of_two = count_occupied(1024, false);
    const std::pair<std::size_t, std::size_t> prime_mod = count_occupied(1021, false);
    const std::pair<std::size_t, std::size_t> prime_bigger = count_occupied(2053, false);
    const std::pair<std::size_t, std::size_t> knuth = count_occupied(1024, true);

    Label("  key % m，m = 1024（2 的幂）", 34);
    Label("1024", 10);
    Label(std::to_string(power_of_two.first), 10);
    std::cout << power_of_two.second << "\n";

    Label("  key % m，m = 1021（质数）", 34);
    Label("1021", 10);
    Label(std::to_string(prime_mod.first), 10);
    std::cout << prime_mod.second << "\n";

    Label("  key % m，m = 2053（质数）", 34);
    Label("2053", 10);
    Label(std::to_string(prime_bigger.first), 10);
    std::cout << prime_bigger.second << "\n";

    Label("  Knuth 乘法散列，m = 1024", 34);
    Label("1024", 10);
    Label(std::to_string(knuth.first), 10);
    std::cout << knuth.second << "\n";

    Note("");
    Note("读表：质数表长把 1000 个 key 铺满了 1000 个桶（最长链 1）；");
    Note("而 m = 1024 = 步长时，1000 个 key 全部挤进同一个桶，最长链 1000。");
    Note("这不是巧合，是 gcd(步长, m) 决定的：");
    Note("  key = 1024 * i，m = 1024 => gcd = 1024 => 只能用到 1024 / 1024 = 1 个桶；");
    Note("  而 m = 1021 是质数，与 1024 互质 => 1021 个桶全都用得上。");
    Note("乘法散列取的是乘积高位，低位规律被进位搅乱，所以它不挑 m（但多了乘法开销）。");
}

// --- 第 2 节：字符串散列对比 -------------------------------------------------
void ReportStringHashing() {
    const std::vector<std::string>& words = RealWordSet();
    constexpr std::size_t kWordBuckets = 128;

    Label("真实词表：", 24);
    std::cout << words.size() << " 个英文单词，表长 " << kWordBuckets << "\n\n";

    PrintBucketStats("FNV-1a", MeasureDistribution(words, kWordBuckets, HashFnv1a));
    PrintBucketStats("djb2", MeasureDistribution(words, kWordBuckets, HashDjb2));
    PrintBucketStats("只把字节相加（坏）", MeasureDistribution(words, kWordBuckets, HashWeakByteSum));

    Note("");
    Note("坏哈希的直方图一眼就能看出问题：占用桶明显偏少、最长链明显偏长，");
    Note("而且所有字母异位词（例如 \"state\" 与 \"taste\"）必然撞在同一个桶里——");
    Note("因为它完全丢掉了字符的顺序信息。");

    // 大批工程风格的字符串键，看冲突率是否接近「均匀散列」的理论值。
    const std::vector<std::string> identifiers = MakeIdentifierSet(20000, 20240605u);
    constexpr std::size_t kIdentifierBuckets = 4096;

    std::cout << "\n";
    Label("工程风格字符串键：", 24);
    std::cout << identifiers.size() << " 个，表长 " << kIdentifierBuckets << "\n\n";

    const BucketStats fnv_stats = MeasureDistribution(identifiers, kIdentifierBuckets, HashFnv1a);
    const BucketStats djb2_stats = MeasureDistribution(identifiers, kIdentifierBuckets, HashDjb2);
    PrintBucketStats("FNV-1a", fnv_stats);
    PrintBucketStats("djb2", djb2_stats);

    const double load = static_cast<double>(identifiers.size()) / static_cast<double>(kIdentifierBuckets);
    Label("  理论冲突率（均匀散列）", 24);
    std::cout << " " << (TheoreticalCollisionRate(load) * 100.0) << " %\n";
    Label("  FNV-1a 实测冲突率", 24);
    std::cout << " "
              << (100.0 * static_cast<double>(identifiers.size() - fnv_stats.occupied) /
                  static_cast<double>(identifiers.size()))
              << " %\n";
    Label("  djb2 实测冲突率", 24);
    std::cout << " "
              << (100.0 * static_cast<double>(identifiers.size() - djb2_stats.occupied) /
                  static_cast<double>(identifiers.size()))
              << " %\n";

    Note("");
    Note("两者都贴着理论值，说明在这个数据上它们和「理想均匀散列」几乎没差别——");
    Note("哈希质量的好坏，只有在这种「冲突率对得上理论」的检验里才能被量化。");

    // 吞吐量：每个字符串散列一次要多少纳秒。
    constexpr int kInnerRepeat = 200;
    const double t_fnv = BenchMedianUs(
        [&] {
            return Repeat(
                [&] {
                    std::uint64_t acc = 0;
                    for (const std::string& word : words) {
                        acc += HashFnv1a(word);
                    }
                    return acc;
                },
                kInnerRepeat);
        },
        5);
    const double t_djb2 = BenchMedianUs(
        [&] {
            return Repeat(
                [&] {
                    std::uint64_t acc = 0;
                    for (const std::string& word : words) {
                        acc += HashDjb2(word);
                    }
                    return acc;
                },
                kInnerRepeat);
        },
        5);

    const double hash_count = static_cast<double>(words.size() * kInnerRepeat);
    std::cout << "\n";
    Label("  散列 " + std::to_string(static_cast<long long>(hash_count)) + " 个字符串：FNV-1a", 46);
    std::cout << " " << t_fnv << " us（" << (t_fnv * 1000.0 / hash_count) << " ns/个）\n";
    Label("  散列 " + std::to_string(static_cast<long long>(hash_count)) + " 个字符串：djb2", 46);
    std::cout << " " << t_djb2 << " us（" << (t_djb2 * 1000.0 / hash_count) << " ns/个）\n";
    Note("");
    Note("两者吞吐在同一量级（每字符一两个纳秒），工程上随便选一个都行；");
    Note("真要追求分布质量，现代选择是 xxHash / wyhash / CityHash 这类带 128 位乘法混合的算法。");
}

// --- 第 3 节：MyHashMap 的接口、Rule of Five、迭代器 ------------------------
void ReportMyHashMapBasics() {
    MyHashMap<std::string, int> ages;
    assert(ages.empty() && ages.size() == 0);

    ages.insert("alice", 30);
    ages.insert("bob", 25);
    const bool inserted_new = ages.insert("carol", 41);
    const bool inserted_again = ages.insert("carol", 42);  // 同键再插：只更新
    assert(inserted_new);
    assert(!inserted_again);
    assert(ages.size() == 3);
    assert(ages.at("carol") == 42);

    ages["dave"] = 19;                       // operator[] 插入
    assert(ages["dave"] == 19);
    assert(ages["eve"] == 0);                // operator[] 读不存在的键会「顺手插入默认值」
    assert(ages.size() == 5);
    assert(ages.contains("eve"));
    assert(!ages.contains("frank"));

    bool threw = false;
    try {
        g_sink = g_sink + static_cast<std::uint64_t>(ages.at("frank"));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    assert(threw);  // at 与 operator[] 的区别：at 找不到就抛异常

    assert(ages.erase("eve"));
    assert(!ages.erase("eve"));
    assert(ages.size() == 4);

    // 迭代器：用范围 for 遍历，顺序不重要，内容必须齐全。
    std::vector<std::pair<std::string, int>> collected(ages.begin(), ages.end());
    std::sort(collected.begin(), collected.end());
    std::vector<std::pair<std::string, int>> expected = {
        {"alice", 30}, {"bob", 25}, {"carol", 42}, {"dave", 19}};
    std::sort(expected.begin(), expected.end());
    assert(collected == expected);

    // Rule of Five：拷贝必须是深拷贝。
    MyHashMap<std::string, int> copied(ages);
    copied.insert("grace", 33);
    assert(copied.size() == 5);
    assert(ages.size() == 4);
    assert(!ages.contains("grace"));

    // 拷贝赋值 + 自赋值。
    MyHashMap<std::string, int> assigned;
    assigned.insert("tmp", 1);
    assigned = ages;
    assert(assigned.size() == 4 && !assigned.contains("tmp"));
    MyHashMap<std::string, int>& alias = assigned;
    assigned = alias;
    assert(assigned.size() == 4);

    // 移动：源对象被掏空，且析构时不会 double free。
    MyHashMap<std::string, int> moved(std::move(copied));
    assert(moved.size() == 5);
    assert(copied.empty());
    MyHashMap<std::string, int> move_assigned;
    move_assigned = std::move(moved);
    assert(move_assigned.size() == 5);
    assert(moved.empty());

    std::cout << "  接口验证：insert / operator[] / at / find / erase / contains / size / empty，断言通过\n";
    std::cout << "  Rule of Five：深拷贝、自赋值、移动置空、移动后析构无 double free，断言通过\n";
    std::cout << "  迭代器：范围 for 与 vector 范围构造收集到 " << collected.size()
              << " 个键值对，与期望完全一致\n";

    // rehash 后引用依然有效（链地址法的节点不搬家）。
    MyHashMap<int, int> stable;
    const int* reference = nullptr;
    for (int i = 0; i < 100; ++i) {
        stable.insert(i, i * 3);
        if (i == 0) {
            reference = &stable.at(0);
        }
    }
    assert(stable.bucket_count() > 16);  // 已经扩容过
    assert(reference != nullptr && *reference == 0);
    std::cout << "  rehash 稳定性：扩容到 " << stable.bucket_count()
              << " 个桶之后，早先取到的元素引用仍然有效，断言通过\n";
}

// --- 第 4 节：开放寻址的正确性 ----------------------------------------------
void ReportOpenAddressing() {
    // 先把「删除直接置空」的后果演示一遍。
    assert(DemonstrateBrokenProbeChain());
    std::cout << "  删除演示：直接置空会让同族元素永远找不到；改墓碑后恢复正常，断言通过\n";

    // 线性探测与二次探测，同一串操作，与 std::unordered_map 对拍。
    LinearIntTable linear(1021, 0.5);
    QuadraticIntTable quadratic(1021, 0.5);
    std::unordered_map<int, int> reference;

    std::mt19937 rng(99u);
    for (int step = 0; step < 3000; ++step) {
        const int key = static_cast<int>(rng() % 200u);
        const int value = static_cast<int>(rng() % 10000u);
        const unsigned op = rng() % 3u;
        if (op == 0) {
            const bool a = linear.insert(key, value);
            const bool b = quadratic.insert(key, value);
            const bool existed = reference.count(key) != 0;
            reference[key] = value;
            assert(a == !existed && b == !existed);
        } else if (op == 1) {
            const bool a = linear.erase(key);
            const bool b = quadratic.erase(key);
            const bool c = reference.erase(key) != 0;
            assert(a == c && b == c);
        } else {
            assert(linear.contains(key) == (reference.count(key) != 0));
            assert(quadratic.contains(key) == (reference.count(key) != 0));
        }
        assert(linear.size() == reference.size());
        assert(quadratic.size() == reference.size());
    }
    for (int key = 0; key < 200; ++key) {
        const bool in_ref = reference.count(key) != 0;
        assert(linear.contains(key) == in_ref);
        assert(quadratic.contains(key) == in_ref);
        if (in_ref) {
            assert(*linear.find(key) == reference.at(key));
            assert(*quadratic.find(key) == reference.at(key));
        } else {
            assert(linear.find(key) == nullptr);
            assert(quadratic.find(key) == nullptr);
        }
    }

    std::cout << "  对拍 3000 步随机 insert / erase / find：线性探测与二次探测都与 unordered_map 一致\n";
    std::cout << "  最终 size = " << reference.size() << "，线性表墓碑数 = " << linear.tombstone_count()
              << "，二次表墓碑数 = " << quadratic.tombstone_count() << "\n";

    // 删除后再插入同一个键，墓碑必须被复用（否则表会被墓碑慢慢撑满）。
    std::vector<int> erased_keys;
    for (int key = 0; key < 200 && erased_keys.size() < 60; ++key) {
        if (linear.erase(key)) {
            erased_keys.push_back(key);
        }
    }
    const std::size_t tombstones_peak = linear.tombstone_count();
    assert(tombstones_peak >= erased_keys.size());  // 每删成功一个就多一个墓碑

    // 把刚才删掉的键原样插回去：插入会优先落在探测链上遇到的第一个墓碑上，
    // 所以每个键都会消耗掉一个墓碑，墓碑数应当显著回落。
    for (const int key : erased_keys) {
        linear.insert(key, key);
    }
    assert(linear.tombstone_count() < tombstones_peak);
    std::cout << "  墓碑复用：删掉 " << erased_keys.size() << " 个键后墓碑升到 "
              << tombstones_peak << " 个，把它们插回去后降到 " << linear.tombstone_count()
              << " 个（插入优先复用墓碑，表不会被墓碑撑满）\n";
}

// --- 第 5 节：自定义类型哈希 ------------------------------------------------
void ReportCustomTypeHashing() {
    const Student a{"alice", 1};
    const Student b{"alice", 2};
    const Student c{"bob", 1};

    const std::size_t hash_a = std::hash<Student>{}(a);
    const std::size_t hash_b = std::hash<Student>{}(b);
    const std::size_t hash_c = std::hash<Student>{}(c);
    assert(hash_a != hash_b);  // 同名字不同学号必须散开
    assert(hash_a != hash_c);

    MyHashMap<Student, std::string> roster;
    roster.insert(a, "一班");
    roster.insert(b, "二班");
    roster.insert(c, "三班");
    assert(roster.size() == 3);
    assert(roster.at(Student{"alice", 2}) == "二班");
    assert(roster.contains(Student{"bob", 1}));
    assert(!roster.contains(Student{"bob", 2}));

    std::unordered_map<Student, std::string> std_roster;
    for (const auto& kv : roster) {
        std_roster.insert(kv);
    }
    assert(std_roster.size() == roster.size());

    std::cout << "  std::hash<Student> 特化生效：name=alice id=1 -> " << hash_a << "\n";
    std::cout << "                                 name=alice id=2 -> " << hash_b
              << "（不同学号散列值不同，说明两个字段都参与了组合）\n";
    std::cout << "  MyHashMap<Student,string> 用标准哈希特化工作正常，size = " << roster.size()
              << "，断言通过\n";

    // 自定义哈希仿函数（不动 std）。
    MyHashMap<Point, int, PointHash> grid;
    constexpr int kSide = 30;
    for (int x = 0; x < kSide; ++x) {
        for (int y = 0; y < kSide; ++y) {
            grid.insert(Point{x, y}, x * kSide + y);
        }
    }
    assert(grid.size() == static_cast<std::size_t>(kSide * kSide));
    for (int x = 0; x < kSide; ++x) {
        for (int y = 0; y < kSide; ++y) {
            assert(grid.at(Point{x, y}) == x * kSide + y);
        }
    }
    std::cout << "  PointHash 仿函数：" << grid.size() << " 个坐标全部命中，桶数 "
              << grid.bucket_count() << "，最长链 " << grid.max_chain_length() << "\n";

    // 只拿一个字段做哈希会怎样：直接对比「组合哈希」与「只用 name」。
    const std::size_t weak_a = std::hash<std::string>{}("alice");
    assert(weak_a == std::hash<std::string>{}("alice"));
    Note("");
    Note("如果 Student 的哈希只写 std::hash<std::string>{}(name)，那么 (alice,1) 和 (alice,2)");
    Note("会得到完全相同的散列值——它们仍然是不相等的元素，但会被塞进同一条链。");
    Note("字段越多、前缀越相似，这种「只用一个字段」的写法退化得越厉害。");
}

// --- 第 6 节：与 std::unordered_map 逐步对拍 --------------------------------
void CrossCheckWithStdUnorderedMap() {
    MyHashMap<int, int> mine;
    std::unordered_map<int, int> reference;

    std::mt19937 rng(20240605u);
    std::size_t rehash_growth_steps = 0;
    std::size_t previous_buckets = mine.bucket_count();

    constexpr int kSteps = 400;
    for (int step = 0; step < kSteps; ++step) {
        const int key = static_cast<int>(rng() % 200u);
        const int value = static_cast<int>(rng() % 1000u);
        const unsigned op = rng() % 3u;

        if (op == 0) {
            const bool inserted = mine.insert(key, value);
            const bool existed = reference.count(key) != 0;
            reference[key] = value;  // unordered_map 用 [] 实现同样的「有则更新」
            assert(inserted == !existed);
        } else if (op == 1) {
            const bool erased_mine = mine.erase(key);
            const bool erased_ref = reference.erase(key) != 0;
            assert(erased_mine == erased_ref);
        } else {
            const bool in_mine = mine.contains(key);
            assert(in_mine == (reference.count(key) != 0));
            if (in_mine) {
                assert(mine.at(key) == reference.at(key));
            }
        }

        if (mine.bucket_count() != previous_buckets) {
            previous_buckets = mine.bucket_count();
            ++rehash_growth_steps;
        }

        // 每一步都比一遍全部内容：把两边都摊平、排序，再逐项比较。
        assert(mine.size() == reference.size());
        std::vector<std::pair<int, int>> left(mine.begin(), mine.end());
        std::vector<std::pair<int, int>> right(reference.begin(), reference.end());
        assert(left.size() == right.size());
        std::sort(left.begin(), left.end());
        std::sort(right.begin(), right.end());
        assert(left == right);
    }

    std::cout << "  逐步对拍 " << kSteps << " 步（每步都比完整内容）：";
    std::cout << "最终 size = " << mine.size() << "，两层实现完全一致，断言通过\n";
    std::cout << "  期间桶数从 16 增长到 " << mine.bucket_count() << "，共发生 " << rehash_growth_steps
              << " 次扩容，负载因子 " << mine.load_factor() << "\n";
}

// 统计一个 MyHashMap 里有多少个非空桶（表格里的「占用桶」一列）。
template <typename K, typename V, typename Hash>
std::size_t CountOccupiedBuckets(const MyHashMap<K, V, Hash>& table) {
    std::size_t occupied = 0;
    for (std::size_t i = 0; i < table.bucket_count(); ++i) {
        if (table.bucket_size(i) > 0) {
            ++occupied;
        }
    }
    return occupied;
}

// --- 第 7 节：负载因子与 rehash ---------------------------------------------
void ReportLoadFactorAndRehash() {
    constexpr std::size_t kKeys = 50000;
    const std::vector<int> keys = MakeDistinctKeys(kKeys, 0u);

    // (a) 链地址法：把表长固定住，扫一遍不同的负载因子。
    Note("链地址法：固定表长，只改负载因子 alpha = n / m。");
    Note("「冲突率」= 插入时桶非空的元素占比；理论值是 1 - (1 - e^-alpha) / alpha。");
    std::cout << "\n";
    Label("  负载因子 alpha", 20);
    Label("桶数 m", 10);
    Label("占用桶", 10);
    Label("最长链", 10);
    Label("实测冲突率", 14);
    Label("理论冲突率", 14);
    Label("平均查找 ns", 14);
    std::cout << "\n";
    PrintSeparator(100);

    const double loads[] = {0.25, 0.5, 0.75, 0.9, 1.5};
    for (const double target_load : loads) {
        const std::size_t buckets =
            static_cast<std::size_t>(static_cast<double>(kKeys) / target_load) + 1;
        // max_load_factor 设成 100 是为了「锁住」表长：否则一超过 0.75 就会扩容，
        // 我们就没法在指定负载因子上做对比了。
        MyHashMap<int, int> table(buckets, 100.0f);
        for (const int key : keys) {
            table.insert(key, key);
        }

        table.reset_probe_count();
        const double lookup_us = BenchMedianUs(
            [&] {
                std::uint64_t acc = 0;
                for (const int key : keys) {
                    const auto it = table.find(key);
                    if (it != table.end()) {
                        acc += static_cast<std::uint64_t>(it->second);
                    }
                }
                return acc;
            },
            3);
        const double ns_per_lookup = lookup_us * 1000.0 / static_cast<double>(kKeys);

        const double measured_rate = static_cast<double>(kKeys - CountOccupiedBuckets(table)) /
                                     static_cast<double>(kKeys);

        Label("  " + FormatFixed(target_load, 2), 20);
        Label(std::to_string(table.bucket_count()), 10);
        Label(std::to_string(CountOccupiedBuckets(table)), 10);
        Label(std::to_string(table.max_chain_length()), 10);
        Label(FormatFixed(measured_rate * 100.0, 3) + " %", 14);
        Label(FormatFixed(TheoreticalCollisionRate(target_load) * 100.0, 3) + " %", 14);
        std::cout << ns_per_lookup << "\n";
        assert(std::fabs(table.load_factor() - static_cast<float>(target_load)) < 0.01f);
    }

    Note("");
    Note("两个结论：");
    Note("  1) 实测冲突率和理论值几乎重合，说明 std::hash<int> + 除留余数在这个数据上");
    Note("     等价于理想均匀散列；如果哪天实测远高于理论，那就是哈希函数的问题；");
    Note("  2) 负载因子越高，链越长、平均查找越慢，但桶数越少、越省内存——");
    Note("     0.75 这个默认值就是「空间」和「时间」的折中，它来自 1/(1-alpha) 这个均值。");

    // (b) rehash 的均摊 O(1)：数一数一共搬了多少个节点。
    MyHashMap<int, int> growing;  // 默认 16 个桶、0.75 阈值，会一路扩容
    for (const int key : keys) {
        growing.insert(key, key);
    }
    std::cout << "\n";
    Label("  从 16 个桶开始插入 " + std::to_string(kKeys) + " 个元素", 44);
    std::cout << "\n";
    Label("  最终桶数", 44);
    std::cout << " " << growing.bucket_count() << "\n";
    Label("  rehash 次数", 44);
    std::cout << " " << growing.rehash_count() << "\n";
    Label("  rehash 累计搬移节点数", 44);
    std::cout << " " << growing.rehash_moves() << "（= 元素数的 "
              << (static_cast<double>(growing.rehash_moves()) / static_cast<double>(kKeys))
              << " 倍）\n";
    Note("");
    Note("每次扩容桶数翻倍，所以第 k 次扩容要搬的节点数大约是 n / 2^k；");
    Note("把它们加起来：n + n/2 + n/4 + ... < 2n，是一个与 n 同阶的量，");
    Note("摊到 n 次插入上就是常数——这就是「rehash 是均摊 O(1)」的实证。");
    Note("如果每次只把桶数加 1（而不是翻倍），总搬移就会变成 O(n^2)，这是经典陷阱。");

    // (c) 开放寻址：负载因子对探测长度的影响（含理论值）。
    Note("");
    Note("开放寻址：负载因子对「未命中查找」的平均探测长度影响极大。");
    Note("  线性探测理论值 = (1 + 1/(1-alpha)^2) / 2；均匀探测理论值 = 1/(1-alpha)。");
    Note("二次探测实测值通常介于两者之间，明显优于线性探测。");
    std::cout << "\n";
    Label("  alpha", 10);
    Label("槽位数 m", 12);
    Label("线性探测实测", 16);
    Label("二次探测实测", 16);
    Label("线性理论", 12);
    Label("均匀理论", 12);
    std::cout << "\n";
    PrintSeparator(78);

    constexpr std::size_t kProbeKeys = 20000;
    constexpr std::size_t kMissLookups = 2000;
    const std::vector<int> probe_keys = MakeDistinctKeys(kProbeKeys, 7u);
    const std::vector<int> missing = MakeDistinctKeys(kMissLookups, 12345u);  // 保证不在表里

    const double probe_loads[] = {0.25, 0.5, 0.75, 0.9};
    for (const double alpha : probe_loads) {
        const std::size_t slots =
            static_cast<std::size_t>(static_cast<double>(kProbeKeys) / alpha) + 1;

        LinearIntTable linear(slots, 100.0);        // max_load 设大，锁住槽位数
        QuadraticIntTable quadratic(slots, 100.0);
        for (const int key : probe_keys) {
            linear.insert(key, key);
            quadratic.insert(key, key);
        }

        linear.reset_probe_stats();
        quadratic.reset_probe_stats();
        for (const int key : missing) {
            assert(!linear.contains(key));
            assert(!quadratic.contains(key));
        }

        const double linear_measured = linear.mean_probe_length();
        const double quadratic_measured = quadratic.mean_probe_length();
        const double linear_theory = (1.0 + 1.0 / ((1.0 - alpha) * (1.0 - alpha))) / 2.0;
        const double uniform_theory = 1.0 / (1.0 - alpha);

        Label("  " + FormatFixed(alpha, 2), 10);
        Label(std::to_string(linear.slot_count()), 12);
        Label(FormatFixed(linear_measured, 3), 16);
        Label(FormatFixed(quadratic_measured, 3), 16);
        Label(FormatFixed(linear_theory, 3), 12);
        std::cout << FormatFixed(uniform_theory, 3) << "\n";
    }
    Note("");
    Note("读表：alpha 从 0.25 涨到 0.9，线性探测的未命中探测长度从 1 点几涨到几十；");
    Note("这就是「开放寻址必须把负载因子压在 0.5 左右」的量化理由——");
    Note("过了 0.7 之后，每次没命中的查找都要摸十几个槽位，缓存优势会被彻底吃掉。");
}

// --- 第 8 节：性能实测 ------------------------------------------------------
void ReportPerformance() {
    constexpr std::size_t kKeys = 100000;
    const std::vector<int> keys = MakeDistinctKeys(kKeys, 3u);

    WarmUp(
        [&] {
            MyHashMap<int, int> table;
            for (int i = 0; i < 1000; ++i) {
                table.insert(i, i);
            }
            return static_cast<std::uint64_t>(table.size());
        },
        2);

    // (a) MyHashMap vs std::unordered_map：同样 10 万次插入 + 10 万次查找。
    const double t_mine = BenchMedianUs(
        [&] {
            MyHashMap<int, int> table;
            std::uint64_t acc = 0;
            for (const int key : keys) {
                table.insert(key, key);
            }
            for (const int key : keys) {
                const auto it = table.find(key);
                if (it != table.end()) {
                    acc += static_cast<std::uint64_t>(it->second);
                }
            }
            return acc;
        },
        5);

    const double t_std = BenchMedianUs(
        [&] {
            std::unordered_map<int, int> table;
            std::uint64_t acc = 0;
            for (const int key : keys) {
                table.insert({key, key});
            }
            for (const int key : keys) {
                const auto it = table.find(key);
                if (it != table.end()) {
                    acc += static_cast<std::uint64_t>(it->second);
                }
            }
            return acc;
        },
        5);

    std::cout << "  " << kKeys << " 次插入 + " << kKeys << " 次查找：\n";
    Label("    手写 MyHashMap（链地址法）", 34);
    std::cout << " " << t_mine << " us\n";
    Label("    std::unordered_map", 34);
    std::cout << " " << t_std << " us\n";
    if (t_std > 0.0) {
        Label("    MyHashMap / unordered_map", 34);
        std::cout << " " << (t_mine / t_std) << " x\n";
    }
    Note("");
    Note("同一量级就说明实现是合理的。差距主要来自：unordered_map 的迭代器/局部迭代器");
    Note("封装更重、每个节点多存一个哈希缓存和一个桶前驱指针，而我们的节点只有 next。");

    // (b) 撞库：所有 key 散列值相同，链地址法立刻退化成 O(n)。
    constexpr std::size_t kBadKeys = 3000;
    const std::vector<int> bad_keys = MakeDistinctKeys(kBadKeys, 11u);

    const double t_good = BenchMedianUs(
        [&] {
            MyHashMap<int, int> table(1024, 100.0f);
            std::uint64_t acc = 0;
            for (const int key : bad_keys) {
                table.insert(key, key);
            }
            table.reset_probe_count();
            for (const int key : bad_keys) {
                acc += static_cast<std::uint64_t>(table.find(key) != table.end());
            }
            return acc;
        },
        3);

    const double t_bad = BenchMedianUs(
        [&] {
            // ConstantHasher：所有 key 都散列到第 0 号桶，直接撞成一条 5000 长的链。
            MyHashMap<int, int, ConstantHasher<int>> table(1024, 100.0f);
            std::uint64_t acc = 0;
            for (const int key : bad_keys) {
                table.insert(key, key);
            }
            table.reset_probe_count();
            for (const int key : bad_keys) {
                acc += static_cast<std::uint64_t>(table.find(key) != table.end());
            }
            return acc;
        },
        3);

    // 单独测一次「未命中查找」走了多少个节点——这个数字最能说明问题。
    MyHashMap<int, int> good_table(1024, 100.0f);
    MyHashMap<int, int, ConstantHasher<int>> bad_table(1024, 100.0f);
    for (const int key : bad_keys) {
        good_table.insert(key, key);
        bad_table.insert(key, key);
    }
    const std::vector<int> absent = MakeDistinctKeys(200, 999u);
    good_table.reset_probe_count();
    bad_table.reset_probe_count();
    for (const int key : absent) {
        assert(!good_table.contains(key));
        assert(!bad_table.contains(key));
    }
    const std::uint64_t good_probes = good_table.probe_count();
    const std::uint64_t bad_probes = bad_table.probe_count();

    std::cout << "\n";
    Label("  正常哈希：插入 + 查找 " + std::to_string(kBadKeys) + " 个元素", 46);
    std::cout << " " << t_good << " us\n";
    Label("  撞库哈希：同样 " + std::to_string(kBadKeys) + " 个元素", 46);
    std::cout << " " << t_bad << " us";
    if (t_good > 0.0) {
        std::cout << "（慢 " << (t_bad / t_good) << " 倍）";
    }
    std::cout << "\n";
    Label("  最长链：正常哈希", 46);
    std::cout << " " << good_table.max_chain_length() << "，撞库哈希 "
              << bad_table.max_chain_length() << "\n";
    Label("  200 次未命中查找访问的节点数：正常哈希", 46);
    std::cout << " " << good_probes << "，撞库哈希 " << bad_probes << "\n";

    Note("");
    Note("撞库（hash flooding）就是让所有 key 落进同一个桶：单次操作从 O(1) 变成 O(n)，");
    Note("在 Web 服务里这意味着「攻击者用几十万个精心构造的键就能把 CPU 打满」，");
    Note("这是真实发生过的 CVE（多个语言的哈希表实现都中过招）。");
    Note("常见的两道防线：");
    Note("  1) 给哈希函数加一个「每次进程启动都不同的随机种子」（SipHash、wyhash 的 seed）；");
    Note("  2) 链太长时把桶从链表升级成红黑树（Java 8 的 HashMap 就是这么做的）；");
    Note("     代价是节点更大、常数更高，所以只有超过阈值（Java 用 8）才升级。");
    Note("std::unordered_map 没有做这些，所以在「键可以被外部控制」的接口上要自己评估风险。");
}

}  // namespace

int main() {
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "本文件在 Debug 配置下运行，绝对耗时比 Release 慢，请只看趋势。\n";

    // =======================================================================
    Section("1. 整数散列：除留余数法 vs Knuth 乘法散列（m 取质数 vs 2 的幂）");
    // =======================================================================
    ReportIntegerHashing();

    // =======================================================================
    Section("2. 字符串散列：FNV-1a vs djb2 vs 坏哈希（桶长直方图）");
    // =======================================================================
    ReportStringHashing();

    // =======================================================================
    Section("3. 链地址法 MyHashMap：接口、Rule of Five、迭代器");
    // =======================================================================
    ReportMyHashMapBasics();

    // =======================================================================
    Section("4. 开放寻址：线性探测 / 二次探测 / 墓碑");
    // =======================================================================
    ReportOpenAddressing();

    // =======================================================================
    Section("5. std::hash 与自定义类型的哈希");
    // =======================================================================
    ReportCustomTypeHashing();

    // =======================================================================
    Section("6. 正确性对拍：MyHashMap vs std::unordered_map");
    // =======================================================================
    CrossCheckWithStdUnorderedMap();

    // =======================================================================
    Section("7. 负载因子、rehash 与探测长度");
    // =======================================================================
    ReportLoadFactorAndRehash();

    // =======================================================================
    Section("8. 性能实测：MyHashMap vs std::unordered_map，以及撞库退化");
    // =======================================================================
    ReportPerformance();

    // 让 g_sink 真正被「读过」一次，确保它不会被整个优化掉。
    std::cout << "\n[校验] 累加器非零，说明所有测量结果都真实参与了运算: "
              << (g_sink != 0 ? "是" : "否") << "\n";

    return 0;
}
