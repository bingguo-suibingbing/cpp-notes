// ============================================================================
//  05_concepts.cpp
// ----------------------------------------------------------------------------
//  演示主题：
//    1. 定义 concept：简单要求、复合要求、类型要求、嵌套要求
//    2. concept 的三种使用位置：<Concept T> / requires 子句 / 缩写模板
//    3. 用约束做重载分派（比 SFINAE 更清晰，且参与「更特化」比较）
//    4. requires 表达式当编译期布尔用（可直接 static_assert）
//    5. 用 concept 写泛型算法
//    6. 同一个约束的两种写法：SFINAE + enable_if vs concept，并排对比
//
//  关键结论：
//    - concept 不是新语法糖那么简单：它参与重载决议的「部分有序」比较，
//      约束更强的候选优先，不再需要 enable_if 那种「塞一个默认模板参数」的 hack。
//    - concept 在**约束满足检查**阶段就被短路求值，报错信息只包含
//      「哪个 concept 的哪个要求不满足」，而不是一整面模板实例化墙。
//    - requires 表达式本身是 bool 常量表达式，可以直接喂给 static_assert
//      或 if constexpr，这是 enable_if 很难做到的。
//    - 注意：concept 只约束「语法上是否合法」，不保证语义正确；
//      而且 concept 检查发生在模板实参替换阶段，仍然可能实例化很深。
// ============================================================================

#include <concepts>
#include <cstddef>
#include <iostream>
#include <iterator>
#include <string>
#include <type_traits>
#include <vector>

// ------------------------------------------------------------ 1. 定义 concept
// 复合要求（compound requirement）：{ 表达式 } -> 返回类型约束。
// 除了要求表达式合法，还要求它 convertible_to<T>。
template <typename T>
concept Addable = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
};

// 要求：能用 std::ostream 打印（std::ostream& 是左值引用，必须精确写在箭头右边）
template <typename T>
concept Printable = requires(std::ostream& os, const T& v) {
    { os << v } -> std::same_as<std::ostream&>;
};

// 要求：看起来像容器。typename T::value_type 是「类型要求」。
// std::input_iterator 用在箭头右边时，会检查 begin() 的返回类型满足该 concept。
template <typename T>
concept Container = requires(T c) {
    typename T::value_type;                                    // 嵌套类型要求
    { c.size() }  -> std::convertible_to<std::size_t>;
    { c.begin() } -> std::input_iterator;
};

// concept 可以互相组合（&& / || / !），组合后的约束仍然能被编译器化简比较。
template <typename T>
concept PrintableContainer = Container<T> && Printable<typename T::value_type>;

// ------------------------------------------------------------ 2. 三种使用方式
template <Addable T>                                   // (a) 直接约束模板参数
T addConstrained(T a, T b) { return a + b; }

template <typename T> requires std::integral<T>         // (b) requires 子句
T half(T x) { return x / 2; }

void showIntegral(std::integral auto x) {               // (c) 缩写函数模板（C++20）
    std::cout << "  showIntegral: " << x << '\n';
}

// ------------------------------------------------------------ 3. 用约束做重载分派
// 三个重载的约束强度依次递减，编译器自动选约束最强且满足的那一个。
template <typename T> requires std::integral<T>
std::string describe(T) { return "integral"; }

template <typename T> requires std::floating_point<T>
std::string describe(T) { return "floating point"; }

template <typename T>
std::string describe(T) { return "something else"; }    // 无约束：永远排最后

// ------------------------------------------------------------ 4. requires 表达式当编译期布尔用
template <typename T>
constexpr bool hasPlus = requires(T a, T b) { a + b; };

// ------------------------------------------------------------ 5. 用 concept 写泛型算法
template <PrintableContainer C>
void dump(const C& c) {
    std::cout << "  [";
    bool first = true;
    for (const auto& v : c) {
        if (!first) std::cout << ", ";
        std::cout << v;
        first = false;
    }
    std::cout << "] size=" << c.size() << '\n';
}

// ------------------------------------------------------------ 6. 同一约束的两种写法
// 目标：只接受「可打印」的类型，输出它的值。

// (a) C++17 风格：SFINAE + enable_if 塞一个默认模板参数。
//     模板签名里混进一个和语义无关的 int = 0，读起来费劲；
//     约束不满足时报的是「未找到匹配的重载函数」，要找半天才知道是约束问题。
template <typename T, std::enable_if_t<Printable<T>, int> = 0>
void printValueSfinae(const T& v) {
    std::cout << "  [SFINAE] " << v << '\n';
}

// (b) C++20 风格：concept 直接写在模板参数上。
//     签名自解释；约束不满足时 MSVC 报 C7602「不满足关联的约束」并列出是哪一条。
template <Printable T>
void printValueConcept(const T& v) {
    std::cout << "  [concept] " << v << '\n';
}

int main() {
    std::cout << "==== 1. 编译期「是 / 否满足约束」====\n";
    static_assert(Addable<int>);
    static_assert(Addable<double>);
    static_assert(Addable<std::string>);
    static_assert(!Addable<std::vector<int>>);      // vector 没有 operator+
    static_assert(Container<std::vector<int>>);
    static_assert(Container<std::string>);
    static_assert(!Container<int>);
    static_assert(PrintableContainer<std::vector<int>>);
    static_assert(!PrintableContainer<std::vector<std::vector<int>>>);  // 内层不可打印
    static_assert(hasPlus<int>);
    static_assert(!hasPlus<std::vector<int>>);
    static_assert(Printable<int>);
    static_assert(!Printable<std::vector<int>>);
    std::cout << "以上 static_assert 全部在编译期通过\n";

    std::cout << "\n==== 2. 三种使用方式 ====\n";
    std::cout << "addConstrained(1, 2) = " << addConstrained(1, 2) << '\n';
    std::cout << "addConstrained(\"a\",\"b\") = "
              << addConstrained(std::string("a"), std::string("b")) << '\n';
    std::cout << "half(9) = " << half(9) << '\n';
    showIntegral(5);

    std::cout << "\n==== 3. 约束参与重载决议 ====\n";
    std::cout << "describe(1)    = " << describe(1) << '\n';
    std::cout << "describe(1.5)  = " << describe(1.5) << '\n';
    std::cout << "describe(\"x\") = " << describe("x") << '\n';   // const char* -> 兜底重载

    std::cout << "\n==== 4. 用 concept 写泛型算法 ====\n";
    dump(std::vector<int>{1, 2, 3});
    dump(std::string("abc"));

    std::cout << "\n==== 5. SFINAE 与 concept 并排对比 ====\n";
    printValueSfinae(42);
    printValueConcept(42);
    printValueConcept(std::string("hello"));
    // 两种写法对合法实参的行为完全一致；差别只在「约束不满足时」的报错可读性：
    //   printValueSfinae(std::vector<int>{});
    //     error C2672: 'printValueSfinae': 未找到匹配的重载函数
    //     error C2783: ... 未能为 '_Enabled' 推导模板参数   <- 线索藏在模板参数名里
    //   printValueConcept(std::vector<int>{});
    //     error C2672: 'printValueConcept': 未找到匹配的重载函数
    //     error C7602: 'printValueConcept': 不满足关联的约束    <- 直接点名约束
    // 取消注释自己对比一下，就知道为什么新代码应该优先用 concept。

    // 约束不满足时的另一类报错（模板参数 T 无法同时推导）：
    // addConstrained(1, 2.5);   // error: T 无法同时推导为 int 和 double
    return 0;
}
