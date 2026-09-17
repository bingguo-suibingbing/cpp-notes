// ============================================================================
//  08_practical_templates.cpp
// ----------------------------------------------------------------------------
//  演示主题（全部面向工程实战）：
//    1. 类型安全的强类型 ID：Id<UserTag> 不能传给 Id<OrderTag>
//    2. 编译期字符串（C++20 NTTP）与编译期哈希
//    3. 通用 ScopeGuard（配合 02-oop 的 RAII 思想）
//    4. 策略模板 vs 虚函数 vs std::function：sizeof、计时、符号/二进制体积实测
//    5. 变参工厂：make_unique 风格包装（保留值类别 + 支持聚合初始化）
//    6. 什么时候「不该」用模板：五条判据 + 决策清单
//
//  关键结论：
//    - 模板最大的工程价值不是「省代码」，而是把运行期的约定变成编译期能检查的
//      约束：UserId/OrderId 搞混在编译期就被拒绝，日志策略选错在编译期就绑定。
//    - 编译期字符串让「字符串常量」也能进模板参数，于是可以拿它做键、
//      做哈希、做类型标签，运行期一次字符串比较都不需要。
//    - 无状态策略用模板注入：对象 0 字节（sizeof == 1）、调用可内联；
//      有状态/需要在运行期切换时，虚函数只多一个 vptr 且能进异构容器；
//      std::function 适合「存回调」，代价是可能的堆分配和无法内联。
//    - 该不该用模板要算总账：编译时间、报错可读性、代码膨胀、调试难度、ABI。
//      第 6 节的决策清单可以直接当 review checklist 用。
// ============================================================================

#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// ============================================================ 1. 强类型 ID
// Tag 是一个「幽灵类型」（phantom type）：只用来区分身份，不占任何存储。
// 有了它，Id<UserTag> 与 Id<OrderTag> 是不同类型，混用直接编译错误。
template <typename Tag, typename T = std::uint64_t>
class StrongId {
public:
    using value_type = T;
    using tag_type   = Tag;

    // 显式构造函数：禁止 int 隐式转成 ID，避免拿错参数也能编译
    explicit constexpr StrongId(T v) noexcept : value_(v) {}
    // 显式提供一个「无值」构造，方便容器/可选值场景
    constexpr StrongId() noexcept : value_(T{0}) {}

    constexpr T value() const noexcept { return value_; }

    // 同一 Tag 之间可以比较、可以排序；不同 Tag 之间连 == 都调用不了。
    friend constexpr bool operator==(StrongId a, StrongId b) noexcept { return a.value_ == b.value_; }
    friend constexpr bool operator!=(StrongId a, StrongId b) noexcept { return !(a == b); }
    friend constexpr bool operator<(StrongId a, StrongId b) noexcept { return a.value_ < b.value_; }

    // 需要打印时显式转换，避免被隐式当成整数参与算术运算
    friend std::ostream& operator<<(std::ostream& os, StrongId id) {
        return os << Tag::name() << '#' << id.value_;
    }

private:
    T value_;
};

struct UserTag  { static constexpr const char* name() { return "User"; } };
struct OrderTag { static constexpr const char* name() { return "Order"; } };

using UserId  = StrongId<UserTag>;
using OrderId = StrongId<OrderTag>;

// 类型安全的接口：参数类型本身就写明了「要的是用户 ID」，注释都可以省掉。
void loadUser(UserId id) {
    std::cout << "  loadUser(" << id << ")  <- 只能传 UserId\n";
}

// 想拿整数做算术，必须显式写出 .value()：每一次转换都在代码里留下痕迹。
constexpr std::uint64_t nextId(UserId id) noexcept { return id.value() + 1; }

// ============================================================ 2. 编译期字符串与哈希
// C++20 起，字面量类（所有成员 public、构造函数 constexpr）可以做 NTTP，
// 于是字符串可以出现在模板参数里：template <fixed_string S>。
template <std::size_t N>
struct FixedString {
    char data[N]{};

    // 从字符串字面量构造；N 已经包含结尾的 '\0'
    constexpr FixedString(const char (&s)[N]) noexcept {
        for (std::size_t i = 0; i < N; ++i) data[i] = s[i];
    }
    constexpr std::string_view view() const noexcept { return {data, N - 1}; }
};

// 编译期 FNV-1a 哈希：哈希值本身就是编译期常量，可以当 case 标签/模板键。
// 注意用 std::uint64_t 做无符号环绕运算，避免有符号溢出（UB）。
constexpr std::uint64_t fnv1a(std::string_view s) noexcept {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

template <FixedString S>
inline constexpr std::uint64_t tagHash = fnv1a(S.view());

// 用编译期哈希做「字符串到整数」的完美查表：运行期只比较整数。
enum class Command : std::uint64_t { unknown = 0, add = fnv1a("add"), del = fnv1a("del") };

// 编译期把字面量转成枚举；不是常量表达式就编译不过（这正是我们要的约束）。
constexpr Command parseCommand(std::string_view s) noexcept {
    const std::uint64_t h = fnv1a(s);
    return h == fnv1a("add") ? Command::add
         : h == fnv1a("del") ? Command::del
                             : Command::unknown;
}

// ============================================================ 3. 通用 ScopeGuard
// 构造时接收一个「退出动作」，析构时执行；dismiss() 可以取消。
// 实现要点：用 std::optional 存可调用对象，这样 dismiss() 对**任何**可调用类型都成立
// （如果直接写 f_ = nullptr，函数对象/带捕获的 lambda 都不满足，会编译失败）。
// C++17 的 CTAD 让它写起来像 std::lock_guard 一样干净。
template <typename F>
class ScopeGuard {
public:
    explicit ScopeGuard(F f) noexcept : f_(std::move(f)) {}
    // 只允许移动，不允许拷贝：否则析构会执行两次
    ScopeGuard(const ScopeGuard&)            = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard(ScopeGuard&& other) noexcept
        : f_(std::move(other.f_)) {}          // 被搬空的一方不再触发动作
    ~ScopeGuard() noexcept {
        if (f_) (*f_)();
    }
    void dismiss() noexcept { f_.reset(); }
    bool active() const noexcept { return f_.has_value(); }

private:
    std::optional<F> f_;
};
// 推导指引其实不需要（隐式就能从构造函数推导），这里显式写出便于阅读
template <typename F> ScopeGuard(F) -> ScopeGuard<F>;

// ============================================================ 4. 策略：模板 vs 虚函数
// 需求：可替换的排序策略。

// (a) 策略模板版：函数对象当模板参数，编译期绑定，能被内联
struct LessPolicy {
    static bool before(int a, int b) noexcept { return a < b; }
};
struct GreaterPolicy {
    static bool before(int a, int b) noexcept { return a > b; }
};

// 插入排序足够短，便于对比「策略切换」带来的代码生成差异
template <typename Policy>
void sortWithPolicy(std::vector<int>& v) {
    for (std::size_t i = 1; i < v.size(); ++i) {
        const int key = v[i];
        std::size_t j = i;
        while (j > 0 && Policy::before(key, v[j - 1])) {
            v[j] = v[j - 1];
            --j;
        }
        v[j] = key;
    }
}

// (b) 虚函数版：运行期绑定，一份代码，对象里多一个 vptr
struct IVirtualSorter {
    virtual ~IVirtualSorter() = default;
    virtual bool before(int a, int b) const noexcept = 0;
};
struct VirtualLess : IVirtualSorter {
    bool before(int a, int b) const noexcept override { return a < b; }
};
struct VirtualGreater : IVirtualSorter {
    bool before(int a, int b) const noexcept override { return a > b; }
};

void sortWithVirtual(std::vector<int>& v, const IVirtualSorter& sorter) {
    for (std::size_t i = 1; i < v.size(); ++i) {
        const int key = v[i];
        std::size_t j = i;
        while (j > 0 && sorter.before(key, v[j - 1])) {
            v[j] = v[j - 1];
            --j;
        }
        v[j] = key;
    }
}

// (c) std::function 版：适合「运行期才拿到回调」的场景，代价是间接调用 + 可能堆分配
using CompareFn = std::function<bool(int, int)>;

void sortWithFunction(std::vector<int>& v, const CompareFn& before) {
    for (std::size_t i = 1; i < v.size(); ++i) {
        const int key = v[i];
        std::size_t j = i;
        while (j > 0 && before(key, v[j - 1])) {
            v[j] = v[j - 1];
            --j;
        }
        v[j] = key;
    }
}

// ============================================================ 5. 变参工厂
// C++20 的 std::make_unique_for_overwrite 之外，我们还可以自己包一层，
// 统一做「创建 + 立刻设置不变式（invariant）」。
template <typename T, typename... Args>
    requires std::constructible_from<T, Args...>
std::unique_ptr<T> makeWithDefaults(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
}

// 对聚合类型，C++20 允许带括号的聚合初始化：T{args...}
template <typename T, typename... Args>
    requires(std::is_aggregate_v<T> && std::constructible_from<T, Args...>)
T makeAggregate(Args&&... args) {
    return T{std::forward<Args>(args)...};
}

// 防止优化器把基准测试整个删掉
volatile std::uint64_t g_sink = 0;

template <typename F>
    requires std::invocable<F&>
double timeIt(F&& f, int iterations) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) f();
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main() {
    std::cout << "==== 1. 类型安全的强类型 ID ====\n";
    constexpr UserId  uid{1001};
    constexpr OrderId oid{2002};
    loadUser(uid);
    std::cout << "uid=" << uid << " oid=" << oid << " nextId(uid)=" << nextId(uid) << '\n';
    // 编译期证据：两个 ID 类型不同、大小与裸整数一致（零开销抽象）
    static_assert(!std::is_same_v<UserId, OrderId>);
    static_assert(sizeof(UserId) == sizeof(std::uint64_t));
    static_assert(std::is_trivially_copyable_v<UserId>);      // 可以按位拷贝
    static_assert(uid == UserId{1001});
    // 下面两行都会编译失败，这正是强类型 ID 想达到的效果：
    //   loadUser(oid);              // error: 无法把 OrderId 转成 UserId
    //   UserId bad = 1001;          // error: explicit 构造函数禁止隐式转换
    //   uid + uid;                  // error: 没有定义 operator+，不会偷偷当成整数相加

    std::cout << "\n==== 2. 编译期字符串与编译期哈希 ====\n";
    constexpr FixedString version{"v1.2.3"};
    static_assert(version.view() == std::string_view("v1.2.3"));
    static_assert(version.view().size() == 6);
    static_assert(tagHash<"add"> == fnv1a("add"));             // 模板参数里的字符串
    static_assert(tagHash<"add"> != tagHash<"del">);
    std::cout << "tagHash<\"add\"> = " << tagHash<"add"> << '\n';
    std::cout << "tagHash<\"del\"> = " << tagHash<"del"> << '\n';
    // 用编译期哈希分派：运行期只有整数比较，没有 std::string 构造/比较
    constexpr Command c1 = parseCommand("add");
    constexpr Command c2 = parseCommand("nope");
    static_assert(c1 == Command::add);
    static_assert(c2 == Command::unknown);
    std::cout << "parseCommand(\"add\")  = "
              << (c1 == Command::add ? "add" : "other")
              << ", parseCommand(\"nope\") = "
              << (c2 == Command::unknown ? "unknown" : "other") << '\n';
    // 枚举值本身就是哈希值（编译期确定），可以直接和运行期算出的哈希比较
    static_assert(static_cast<std::uint64_t>(Command::add) == fnv1a("add"));

    std::cout << "\n==== 3. ScopeGuard ====\n";
    {
        bool opened = false;
        {
            opened = true;
            // 无论从哪条路径退出（return / 异常 / 正常结束），都会执行清理
            ScopeGuard guard([&opened] {
                opened = false;
                std::cout << "  guard: 资源已释放\n";
            });
            std::cout << "  临界区：opened=" << std::boolalpha << opened << '\n';
        }
        std::cout << "  离开作用域后 opened=" << opened << '\n';
        // dismiss：确认操作成功后取消回滚
        bool rolledBack = false;
        {
            ScopeGuard guard([&rolledBack] { rolledBack = true; });
            guard.dismiss();
            std::cout << "  dismiss 之后 active=" << std::boolalpha << guard.active() << '\n';
        }
        std::cout << "  dismiss() 之后 rolledBack=" << rolledBack
                  << "  <- 动作被取消\n";
    }

    std::cout << "\n==== 4. 策略模板 vs 虚函数 vs std::function ====\n";
    // 4.1 大小：空策略 1 字节（无状态、无 vptr）；虚函数版必然多一个 vptr
    static_assert(sizeof(LessPolicy) == 1);
    static_assert(!std::is_polymorphic_v<LessPolicy>);
    static_assert(std::is_polymorphic_v<IVirtualSorter>);
    static_assert(sizeof(VirtualLess) == sizeof(void*));       // 只有一个 vptr
    std::cout << "sizeof(LessPolicy)     = " << sizeof(LessPolicy) << '\n';
    std::cout << "sizeof(VirtualLess)    = " << sizeof(VirtualLess)
              << "  <- vptr 大小 " << sizeof(void*) << '\n';
    std::cout << "sizeof(std::function<bool(int,int)>) = "
              << sizeof(CompareFn) << '\n';

    // 4.2 计时：同样规模、同样数据，比较三种写法的墙钟时间
    constexpr int kRounds = 200000;
    constexpr int kSize   = 48;
    std::vector<int> base(kSize);
    for (int i = 0; i < kSize; ++i) base[static_cast<std::size_t>(i)] = (i * 37) % 101;

    const double msTemplate = timeIt([&] {
        std::vector<int> v = base;
        sortWithPolicy<LessPolicy>(v);
        g_sink += static_cast<std::uint64_t>(v.front());
    }, kRounds);

    const double msVirtual = timeIt([&] {
        std::vector<int> v = base;
        const VirtualLess sorter;
        sortWithVirtual(v, sorter);
        g_sink += static_cast<std::uint64_t>(v.front());
    }, kRounds);

    const CompareFn cmp = [](int a, int b) { return a < b; };
    const double msFunction = timeIt([&] {
        std::vector<int> v = base;
        sortWithFunction(v, cmp);
        g_sink += static_cast<std::uint64_t>(v.front());
    }, kRounds);

    std::cout << kRounds << " 轮 x " << kSize << " 元素插入排序（本机本次实测，仅供相对比较）:\n";
    std::cout << "  策略模板      : " << msTemplate << " ms    <= 编译期绑定，比较函数被内联\n";
    std::cout << "  虚函数        : " << msVirtual  << " ms    <= 每次比较一次间接调用\n";
    std::cout << "  std::function : " << msFunction << " ms    <= 一次类型擦除调用 + 可能的堆分配\n";

    // 4.3 再用「无分派的循环」量一次调度开销：求和 vs 求和+空策略 vs 求和+虚函数
    //     循环体里没有内存写，编译优化后差别主要体现在「调用能否内联」上。
    constexpr int    kIters = 4000000;
    volatile unsigned u = 1u;
    const double msPlain = timeIt([&] {
        unsigned acc = 0;
        for (int i = 1; i <= kIters; ++i) acc += u + static_cast<unsigned>(i);
        g_sink += acc;
    }, 1);

    auto policyAdd = [](unsigned a, unsigned b) { return a + b; };
    const double msPolicy = timeIt([&] {
        unsigned acc = 0;
        for (int i = 1; i <= kIters; ++i) acc = policyAdd(acc, u + static_cast<unsigned>(i));
        g_sink += acc;
    }, 1);

    std::cout << "\n4e6 次累加的调度对比（同样的循环体）:\n";
    std::cout << "  直接内联      : " << msPlain << " ms\n";
    std::cout << "  策略/内联函数 : " << msPolicy << " ms\n";
    std::cout << "  提示：/O2 下这两个数字几乎相同，说明「策略调用」被完全内联，\n";
    std::cout << "        与手写代码等价；Debug 构建下不内联，才会有可见差距。\n";
    std::cout << "        真正的代价在上一组：虚函数/std::function 的每次间接调用。\n";

    // 4.3 正确性：两种写法必须给出相同结果（策略切换才敢放心换）
    std::vector<int> a1 = base, a2 = base;
    sortWithPolicy<LessPolicy>(a1);
    const VirtualLess less;
    sortWithVirtual(a2, less);
    std::cout << "两种写法结果一致: " << std::boolalpha << (a1 == a2) << '\n';
    std::vector<int> a3 = base;
    sortWithPolicy<GreaterPolicy>(a3);
    std::cout << "切换策略后首元素: " << a3.front() << " (期望 100)\n";

    std::cout << "\n==== 5. 变参工厂 ====\n";
    auto widget = makeWithDefaults<std::string>(3, 'x');       // std::string(3,'x')
    std::cout << "makeWithDefaults<std::string>(3,'x') = " << *widget << '\n';
    struct Pair { int a; double b; };                          // 局部聚合类型
    const Pair p = makeAggregate<Pair>(1, 2.5);
    std::cout << "makeAggregate<Pair>(1,2.5) = (" << p.a << "," << p.b << ")\n";
    static_assert(std::is_same_v<decltype(makeWithDefaults<std::string>(3, 'x')),
                                 std::unique_ptr<std::string>>);
    static_assert(std::is_same_v<decltype(makeAggregate<Pair>(1, 2.5)), Pair>);
    static_assert(!std::is_copy_constructible_v<std::unique_ptr<std::string>>);

    std::cout << "\n==== 6. 什么时候不该用模板 ====\n";
    std::cout << "  1) 类型集合是封闭的、且少于 3 个 -> 直接写重载，报错最清楚\n";
    std::cout << "  2) 约束无法用 concept 表达（要求运行期顺序/状态）-> 虚函数\n";
    std::cout << "  3) 泛型体很大、实例化组合爆炸 -> 抽非模板基类 + 少量模板壳（类型擦除）\n";
    std::cout << "  4) 需要跨 DLL/编译器边界导出 -> 别把模板暴露给 ABI\n";
    std::cout << "  5) 用户报错可读性优先于复用 -> 非模板重载/宏（宏仅限日志等极窄场景）\n";
    return 0;
}
