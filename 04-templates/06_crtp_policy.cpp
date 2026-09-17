// ============================================================================
//  06_crtp_policy.cpp
// ----------------------------------------------------------------------------
//  演示主题：
//    1. CRTP（奇异递归模板模式）静态多态：Comparable<Point>
//    2. 「CRTP 与聚合初始化」的真实规则（本项目实测修正的一条常见错误说法）
//    3. 基于策略的设计（Policy-Based Design）：Hasher<FastHash>
//    4. CRTP 做「每个派生类独立的实例计数器」
//    5. 用 static_assert / sizeof 证明编译期多态零虚函数开销
//
//  关键结论：
//    - CRTP 把「派生类类型」作为模板参数传给基类，调用在编译期就绑定，
//      可以内联，对象里没有 vptr。sizeof(Point) == 2*sizeof(int) 就是证据。
//    - 「CRTP 会破坏聚合初始化」这个说法**不准确**。准确的说法是：
//      C++17 起基类也算聚合的一个元素（空基类也算），聚合初始化的初始化器
//      个数必须 <= 元素个数，而 Point 有 3 个元素（1 个空基类 + 2 个 int），
//      写 Point{1,2} 只有 2 个初始化器但想初始化第 2、3 个元素 ->
//      error C2078: too many initializers。
//      正确写法是显式给出基类的初始化器：Point{{}, 1, 2}，它**能编译**。
//      本文件实测：MSVC 14.51 / C++20 下 Point{{},1,2} 通过，
//      Point{1,2} 报 C2078。反过来，如果给 Point 写一个用户构造函数，
//      Point 就不再是聚合（is_aggregate 为 false），Point{1,2} 走构造函数。
//    - 代价：CRTP 每种派生类一份基类代码（可能膨胀），且不同类型无法放进
//      同一个容器；需要运行期多态时还是得用虚函数。
// ============================================================================

#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>

// ============================================================ 1. CRTP 静态多态
// 派生类把自己作为模板参数传进来，基类在编译期就「知道」派生类的真实类型。
// 这里用友元函数（hidden friend）提供全套比较运算符，只需派生类实现 == 和 <。
template <typename Derived>
struct Comparable {
    friend bool operator!=(const Derived& a, const Derived& b) { return !(a == b); }
    friend bool operator> (const Derived& a, const Derived& b) { return b < a; }
    friend bool operator<=(const Derived& a, const Derived& b) { return !(b < a); }
    friend bool operator>=(const Derived& a, const Derived& b) { return !(a < b); }
};

// 先看「空基类 + 聚合」的独立小例子，把规则讲清楚：
// EmptyBase 是空类（sizeof == 1，但作为基类时可以被优化掉），
// 它仍然是 WithEmptyBase 的一个「元素」，必须给初始化器。
struct EmptyBase {};
struct WithEmptyBase : EmptyBase { int x, y; };

struct Point : Comparable<Point> {
    int x{}, y{};

    // 注意：C++17 起聚合初始化会把基类当作第一个「元素」，而且**空基类也算**。
    // 所以继承 CRTP 基类后写 Point{1,2} 会报 error C2078: too many initializers
    // （2 个初始化器，却要初始化 3 个元素：空基类 + x + y）。
    // 两种修法，本项目都验证过：
    //   1) 不写构造函数，显式给出基类初始化器：Point{{}, 1, 2}；
    //   2) 写一个用户构造函数，此时 Point 不再是聚合，Point{1,2} 走构造函数。
    // 这里选第 2 种，因为后续还要给 Point 加成员函数，本来也要构造函数。
    Point() = default;
    Point(int x_, int y_) : x(x_), y(y_) {}

    friend bool operator==(const Point& a, const Point& b) { return a.x == b.x && a.y == b.y; }
    friend bool operator< (const Point& a, const Point& b) {
        return a.x != b.x ? a.x < b.x : a.y < b.y;
    }
    friend std::ostream& operator<<(std::ostream& os, const Point& p) {
        return os << '(' << p.x << ',' << p.y << ')';
    }
};

// ======================================================== 2. 基于策略的设计
struct FastHash {
    static std::size_t hash(std::string_view s) {
        std::size_t h = 1469598103934665603ull;          // FNV-1a 偏移基准
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
        return h;
    }
    static const char* name() { return "FastHash"; }
};

struct LengthHash {
    static std::size_t hash(std::string_view s) { return s.size(); }
    static const char* name() { return "LengthHash"; }
};

// 策略通过模板参数注入，编译期绑定，调用可内联，对象本身是空类（sizeof == 1）。
// 对比虚函数版：需要 vptr（64 位下 8 字节）、运行期间接调用、无法内联到调用点。
template <typename HashPolicy>
class Hasher {
public:
    std::size_t operator()(std::string_view s) const { return HashPolicy::hash(s); }
    const char* policy() const { return HashPolicy::name(); }
};

// 如果策略是无状态的、且需要在运行期选，也可以把策略对象当成员（EBO 后仍为 1 字节）。
// 这里只保留编译期注入这一种，避免引入 EBO 的额外话题。

// ======================================================== 3. CRTP + 计数派生类数量
// 每个 Derived 一份独立的 InstanceCounter<Derived>，所以 alive 计数器互不干扰。
template <typename Derived>
struct InstanceCounter {
    static inline int alive = 0;                          // C++17 inline 静态变量
    InstanceCounter()  { ++alive; }
    // 析构函数写成虚函数是笔误高发区：CRTP 基类绝不能有虚函数，否则就失去零开销意义。
    ~InstanceCounter() { --alive; }
    InstanceCounter(const InstanceCounter&) { ++alive; }
};

struct Widget : InstanceCounter<Widget> { int id = 0; };
struct Gadget : InstanceCounter<Gadget> { int id = 0; };

int main() {
    std::cout << "==== 1. CRTP：一行 operator== 换来全套比较运算符 ====\n";
    Point p{1, 2}, q{1, 2}, r{3, 0};
    std::cout << std::boolalpha
              << "p=" << p << " q=" << q << " r=" << r << '\n'
              << "p == q : " << (p == q) << '\n'
              << "p != q : " << (p != q) << '\n'
              << "p <  r : " << (p <  r) << '\n'
              << "p >  r : " << (p >  r) << '\n'
              << "p <= q : " << (p <= q) << '\n'
              << "p >= q : " << (p >= q) << '\n';

    // 关键：整个过程没有虚函数、没有运行期查表
    static_assert(!std::is_polymorphic_v<Point>, "CRTP 不引入虚函数");
    static_assert(sizeof(Point) == 2 * sizeof(int), "无 vptr，零开销");
    static_assert(sizeof(Comparable<Point>) == 1, "CRTP 基类是空类");
    static_assert(std::is_empty_v<Comparable<Point>>);

    std::cout << "\n==== 2. 空基类是聚合的一个元素（实测规则）====\n";
    std::cout << "sizeof(EmptyBase)      = " << sizeof(EmptyBase) << '\n';
    std::cout << "sizeof(WithEmptyBase)  = " << sizeof(WithEmptyBase)
              << "   <- 空基类被优化掉，等于 2 个 int\n";
    static_assert(std::is_aggregate_v<WithEmptyBase>, "有空基类仍然是聚合");
    static_assert(sizeof(WithEmptyBase) == 2 * sizeof(int));
    // 显式给空基类一个初始化器 {} —— 这是 C++17 以后唯一正确的聚合写法
    WithEmptyBase web{{}, 1, 2};
    std::cout << "WithEmptyBase{{},1,2}  = (" << web.x << "," << web.y << ")\n";
    // 反例（取消注释即可看到）：WithEmptyBase bad{1, 2};
    //   error C2078: too many initializers
    // 有用户构造函数的 Point 不再是聚合，所以 Point{1,2} 反而没问题：
    static_assert(!std::is_aggregate_v<Point>, "写了构造函数就不再是聚合");
    std::cout << "is_aggregate<WithEmptyBase> = " << std::is_aggregate_v<WithEmptyBase>
              << ", is_aggregate<Point> = " << std::is_aggregate_v<Point> << '\n';

    std::cout << "\n==== 3. 策略模式：同一份 Hasher 代码，两套行为 ====\n";
    Hasher<FastHash>   h1;
    Hasher<LengthHash> h2;
    std::cout << "h1(\"hello\") policy=" << h1.policy() << " value=" << h1("hello") << '\n';
    std::cout << "h2(\"hello\") policy=" << h2.policy() << " value=" << h2("hello") << '\n';

    // 策略在编译期绑定，可以内联；对比虚函数版：运行期才决定
    static_assert(sizeof(Hasher<FastHash>) == 1, "空类，无数据成员");
    static_assert(!std::is_polymorphic_v<Hasher<FastHash>>);
    // 两个策略各自的 Hasher 是不同类型 —— 这是「不能放同一个容器」的根源
    static_assert(!std::is_same_v<Hasher<FastHash>, Hasher<LengthHash>>);

    std::cout << "\n==== 4. CRTP 计数器：每个派生类有独立的静态计数器 ====\n";
    {
        Widget w1, w2;
        Gadget g1;
        std::cout << "Widget alive = " << Widget::alive
                  << ", Gadget alive = " << Gadget::alive << '\n';
        (void)w1; (void)w2; (void)g1;
    }
    std::cout << "after scope: Widget alive = " << Widget::alive
              << ", Gadget alive = " << Gadget::alive << '\n';

    std::cout << "\n==== 5. 编译期多态 vs 运行期多态 ====\n";
    std::cout << "CRTP:    编译期绑定, 可内联, 每类型一份代码(可能膨胀), 不能放异构容器\n";
    std::cout << "virtual: 运行期绑定, 有 vptr/vtable 开销, 一份代码, 可放异构容器\n";
    std::cout << "sizeof(Point)=" << sizeof(Point)
              << " sizeof(Hasher<FastHash>)=" << sizeof(Hasher<FastHash>) << '\n';
    return 0;
}
