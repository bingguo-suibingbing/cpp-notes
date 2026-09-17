// ============================================================================
//  02_class_template.cpp
// ----------------------------------------------------------------------------
//  演示主题：
//    1. 类模板 + 默认模板实参 + 数据成员（StaticBuffer<T, N>）
//    2. 成员模板：元素类型不同、长度相同的两个 buffer 可以互拷
//    3. 类外定义成员/自由函数时必须重复模板参数列表
//    4. CTAD（类模板实参推导，C++17）与自定义推导指引
//    5. 别名模板（alias template）与变量模板（variable template，C++14）
//    6. 模板模板参数（template template parameter）
//
//  关键结论：
//    - 类模板永远要写全 <T, N>，除了 CTAD 能省掉；别名模板可以给常用组合起短名字。
//    - 自定义推导指引（Box(const char*) -> Box<std::string>）能改掉编译器默认
//      「退化到 const char*」的行为，这是最实用的 CTAD 技巧。
//    - 模板模板参数的形参列表必须和实参「至少一样宽松」：
//      std::vector 的真实签名是 template<class T, class Alloc = allocator<T>>，
//      所以形参要写成 template <typename...> class Container 才接得住任何容器。
// ============================================================================

#include <cstddef>
#include <iostream>
#include <list>
#include <string>
#include <type_traits>
#include <vector>

// ---------------------------------------- 1. 类模板 + 默认模板实参 + 成员模板
// 默认模板实参只能写在类模板上（C++11 起函数模板也支持，但类模板用得最多）。
template <typename T, std::size_t N = 4>
class StaticBuffer {
public:
    std::size_t size() const noexcept { return N; }

    T&       operator[](std::size_t i)       noexcept { return data_[i]; }
    const T& operator[](std::size_t i) const noexcept { return data_[i]; }

    void fill(const T& v) { for (auto& x : data_) x = v; }

    void print(const char* label) const {
        std::cout << label << ": ";
        for (std::size_t i = 0; i < N; ++i)
            std::cout << data_[i] << (i + 1 == N ? '\n' : ' ');
    }

    // 成员模板：让「元素类型不同、长度相同」的两个 buffer 互拷。
    // 注意 U 和 T 是独立的模板参数，但长度 N 被外层模板固定，所以只能同长互拷。
    template <typename U>
    void fillFrom(const StaticBuffer<U, N>& other) {
        for (std::size_t i = 0; i < N; ++i) data_[i] = static_cast<T>(other[i]);
    }

private:
    // 有了这个默认成员初始化器，C++20 起 StaticBuffer<int> b; 才算聚合初始化之外的
    // 正常默认构造；这里也顺便保证 data_ 被零初始化，避免读出垃圾值。
    T data_[N]{};
};

// 补充：C++20 允许对聚合做带括号的初始化，但 StaticBuffer 有默认成员初始化器
// 且是 class（不是 struct）-- 是否聚合取决于是不是有 private 成员。这里给了
// public/private 混合访问，所以它不是聚合，必须走构造函数。

// --------------------- 2. 类外定义成员/自由函数：必须重复模板参数列表
// 错误直觉：写成 void clear(StaticBuffer& b) 就能匹配所有实例。
// 正确模型：类外定义必须把 <T, N> 重新写一遍，让编译器知道怎么推导。
template <typename T, std::size_t N>
void clear(StaticBuffer<T, N>& b) {
    for (std::size_t i = 0; i < N; ++i) b[i] = T{};
}

// ---------------------------------------- 3. CTAD（C++17）+ 自定义推导指引
template <typename T>
struct Box {
    T v;
    Box(T x) : v(x) {}          // 构造函数隐式生成推导指引 Box(T) -> Box<T>
};
// 自定义推导指引：把「字符串字面量」直接映射到 std::string，
// 否则 Box{"hello"} 会推导成 Box<const char*>，得到一个存悬垂指针的箱子。
Box(const char*) -> Box<std::string>;

// ---------------------------------------- 4. 别名模板 / 变量模板
// 别名模板不是新类型，只是给 StaticBuffer<T, 4> 起了个短名字，写日志/报错时更短。
template <typename T> using Buffer4 = StaticBuffer<T, 4>;
// 变量模板：每个 T 一份独立常量，比宏安全（有类型、能被 ADL 找到）。
template <typename T> constexpr T pi = T(3.14159265358979323846);

// ---------------------------------------- 5. 模板模板参数
// 形参用 template <typename...> class 才能同时接住 vector（2 个模板参数，第 2 个有默认值）
// 和 list；若写成 template <typename> class 则只能接住只有一个参数的容器。
template <template <typename...> class Container, typename T>
std::size_t totalSize(const Container<T>& c) { return c.size(); }

int main() {
    std::cout << "==== 1. 类模板与默认实参 ====\n";
    StaticBuffer<int> b;                 // 用默认长度 N = 4
    b.fill(7);
    b.print("StaticBuffer<int>");
    std::cout << "sizeof(StaticBuffer<int>)  = " << sizeof(StaticBuffer<int>) << '\n';
    std::cout << "sizeof(StaticBuffer<int,64>) = " << sizeof(StaticBuffer<int, 64>) << '\n';

    StaticBuffer<double, 6> d;           // 显式指定长度
    d.fill(1.5);
    d.print("StaticBuffer<double,6>");

    std::cout << "\n==== 2. 成员模板：跨元素类型互拷 ====\n";
    StaticBuffer<int, 4> src;
    src.fill(3);
    StaticBuffer<double, 4> dst;
    dst.fillFrom(src);                   // 成员模板：int -> double
    dst.print("fillFrom(int->double)");

    clear(src);
    src.print("after clear()");

    // 编译期证据：默认实参确实生效，类型就是 StaticBuffer<int, 4>
    static_assert(std::is_same_v<decltype(b), StaticBuffer<int, 4>>);
    static_assert(std::is_same_v<decltype(d), StaticBuffer<double, 6>>);

    std::cout << "\n==== 3. CTAD 与自定义推导指引 ====\n";
    Box b1{42};                          // -> Box<int>
    Box b2{"hello"};                     // 推导指引 -> Box<std::string>（而不是 const char*）
    static_assert(std::is_same_v<decltype(b1), Box<int>>);
    static_assert(std::is_same_v<decltype(b2), Box<std::string>>);
    std::cout << "Box b1.v = " << b1.v << ", b2.v = " << b2.v << '\n';
    // 对照：如果不写推导指引，b2 会是 Box<const char*>，这里用显式写法证明区别
    static_assert(!std::is_same_v<decltype(b2), Box<const char*>>);

    std::cout << "\n==== 4. 别名模板 / 变量模板 ====\n";
    Buffer4<char> bc;
    bc.fill('x');
    bc.print("Buffer4<char> (alias)");
    static_assert(std::is_same_v<Buffer4<char>, StaticBuffer<char, 4>>);
    std::cout << "pi<double> = " << pi<double> << '\n';
    std::cout << "pi<float>  = " << pi<float> << '\n';
    static_assert(pi<int> == 3);              // 变量模板也能当编译期常量用
    static_assert(pi<double> > 3.14);

    std::cout << "\n==== 5. 模板模板参数 ====\n";
    std::vector<int>    v{1, 2, 3};
    std::list<double>   l{1.0, 2.0};
    std::cout << "totalSize(vector<int>)  = " << totalSize(v) << '\n';
    std::cout << "totalSize(list<double>) = " << totalSize(l) << '\n';
    return 0;
}
