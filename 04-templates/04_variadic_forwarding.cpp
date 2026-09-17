// ============================================================================
//  04_variadic_forwarding.cpp
// ----------------------------------------------------------------------------
//  演示主题：
//    1. 可变参数模板：C++11 递归展开 vs C++17 折叠表达式
//    2. 折叠表达式的四种形态（一元左右 / 二元左右）与空包陷阱
//    3. 完美转发工厂（make_unique 的原理）
//    4. 万能引用（forwarding reference）与引用折叠规则
//    5. const T&& / vector<T>&& 不是万能引用
//    6. 用计数器实测「forward 是否保住了值类别」
//
//  关键结论：
//    - 引用折叠只有 4 种组合：& + & -> &，& + && -> &，&& + & -> &，&& + && -> &&。
//      一句话记忆：「只要有一个 &，结果就是 &」。
//    - 形如 T&& 且 T 真的由实参推导时才是万能引用；一旦被 const 修饰、或不是
//      模板参数本身（如 std::vector<T>&&）就退化成普通右值引用。
//    - std::forward<T>(x) 是「有条件地 move」：T 是左值引用类型就不动，
//      T 是非引用类型才转成右值。它本身在运行期不产生任何代码。
//    - 折叠表达式对空参数包的行为要小心：(... && xs) 空包是 true，
//      (... || xs) 空包是 false，(xs + ...) 空包直接编译错误（需要初值版）。
// ============================================================================

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// ------------------------------------------------ 1. C++11 风格：递归展开参数包
// 空包终止重载必须写在最前面（调用点要能看到它）。
void printAll() { std::cout << '\n'; }

template <typename T, typename... Rest>
void printAll(const T& first, const Rest&... rest) {
    std::cout << first << ' ';
    printAll(rest...);                     // 参数包展开：每递归一层消掉一个参数
}

// ------------------------------------------------ 2. C++17 折叠表达式（推荐）
// 生成代码更短、不用写终止重载，编译期展开成一条链式表达式。
template <typename... Ts>
void printFold(const Ts&... xs) {
    ((std::cout << xs << ' '), ...);       // 一元右折叠（逗号运算符）
    std::cout << '\n';
}

// constexpr：让折叠表达式的结果也能出现在 static_assert 等常量表达式里
template <typename... Ts> constexpr auto sumAll(const Ts&... xs) { return (xs + ... + 0); }
template <typename... Ts> constexpr bool allTrue(const Ts&... xs) { return (... && xs); }
// sizeof...(Ts) 是编译期常量，所以这个函数可以直接用在 static_assert 里。
template <typename... Ts> constexpr std::size_t howMany(const Ts&...) { return sizeof...(Ts); }

// ------------------------------------------------ 3. 完美转发工厂（make_unique 原理）
// 两个 &&：Args&& 是万能引用；std::forward<Args> 把值类别原样传给 T 的构造函数。
// 关键点：T 的类型单独写在尖括号里（第一个模板参数），不被推导，
// 这样既能显式指定要造什么，又不影响 Args 的推导。
template <typename T, typename... Args>
std::unique_ptr<T> makeUnique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

// ------------------------------------------------ 4. 万能引用 / 引用折叠演示
// 这里 T 由实参推导，所以 T&& 是万能引用。
template <typename T>
void deduce(T&&) {
    if constexpr (std::is_lvalue_reference_v<T>)
        std::cout << "  T = U&  ->  T&& 折叠为 U&   (传入左值)\n";
    else
        std::cout << "  T = U   ->  T&& 就是 U&&     (传入右值)\n";
}

// const T&& 不是万能引用：const 挡住了推导，它只能绑定右值。
template <typename T> void onlyRvalue(const T&&) { std::cout << "  onlyRvalue: 只接受右值\n"; }
// std::vector<T>&& 也不是万能引用：T&& 不是「裸的模板参数 T」。
template <typename T> void onlyVectorRvalue(std::vector<T>&&) {
    std::cout << "  onlyVectorRvalue: 只接受 vector 右值\n";
}

// 编译期探针：把「传进来的是不是左值」变成 is_lvalue_reference 的布尔结果。
// 写成普通函数模板（泛型 lambda 不能用在 decltype 这种不求值语境里）。
template <typename T>
constexpr bool deducedAsLvalueRef(T&&) { return std::is_lvalue_reference_v<T>; }

// ------------------------------------------------ 6. 用计数器验证「拷贝 vs 移动」
struct Tracked {
    static int copies;
    static int moves;
    Tracked() = default;
    Tracked(const Tracked&)            { ++copies; }
    Tracked(Tracked&&) noexcept        { ++moves;  }
    Tracked& operator=(const Tracked&) = default;
    Tracked& operator=(Tracked&&)      = default;
};
int Tracked::copies = 0;
int Tracked::moves  = 0;

struct Widget {
    std::string      name;
    std::vector<int> data;
    Widget(std::string n, std::vector<int> d) : name(std::move(n)), data(std::move(d)) {}
};

int main() {
    std::cout << "==== 1. 递归变参 vs 折叠表达式 ====\n";
    std::cout << "printAll:  "; printAll(1, "two", 3.5, 'c');
    std::cout << "printFold: "; printFold(1, "two", 3.5, 'c');

    std::cout << "\n==== 2. 折叠表达式 ====\n";
    std::cout << "sumAll(1,2,3,4)      = " << sumAll(1, 2, 3, 4) << '\n';
    std::cout << "sumAll(1,2.5)        = " << sumAll(1, 2.5) << '\n';
    std::cout << "allTrue(true,true)   = " << std::boolalpha << allTrue(true, true) << '\n';
    std::cout << "allTrue()  空包      = " << allTrue() << "   <- && 的空包恒为 true\n";
    std::cout << "howMany(1,2,3)       = " << howMany(1, 2, 3) << '\n';
    static_assert(howMany(1, 2, 3) == 3);
    static_assert(howMany() == 0);
    static_assert(sumAll(1, 2, 3, 4) == 10);   // 折叠表达式能在常量表达式里用
    // 空包提醒：sumAll() 是 ( ... + 0 )，有初值 0，合法；但 (xs + ...) 无初值版
    // 对空包是「无法推导」的编译错误，写通用求和函数时一定带上初值。

    std::cout << "\n==== 3. 完美转发工厂 ====\n";
    auto w = makeUnique<Widget>("hello", std::vector<int>{1, 2, 3});
    std::cout << "makeUnique<Widget>   = " << w->name << " size=" << w->data.size() << '\n';

    std::cout << "\n==== 4. 引用折叠 ====\n";
    int i = 0;
    std::cout << "deduce(i):    ";               deduce(i);
    std::cout << "deduce(0):    ";               deduce(0);
    std::cout << "deduce(move): ";               deduce(std::move(i));
    std::cout << "deduce(const): ";              const int ci = 5; deduce(ci);
    onlyRvalue(0);
    onlyVectorRvalue(std::vector<int>{1, 2});

    // 用 static_assert 把折叠规则钉死：
    //   传左值 -> T 推导为 int&   -> T&& 经引用折叠仍是 int&
    //   传右值 -> T 推导为 int    -> T&& 就是 int&&
    static_assert(deducedAsLvalueRef(i));            // 左值
    static_assert(!deducedAsLvalueRef(0));           // 右值
    static_assert(!deducedAsLvalueRef(std::move(i))); // 将亡值也是右值
    static_assert(std::is_lvalue_reference_v<decltype((i))>);   // 具名变量是左值
    static_assert(!std::is_lvalue_reference_v<decltype((std::move(i)))>);

    std::cout << "\n==== 5. forward 是否保住了值类别 ====\n";
    Tracked t;
    Tracked::copies = Tracked::moves = 0;
    auto a = makeUnique<Tracked>(t);                 // 传左值 -> 拷贝构造
    std::cout << "forward(lvalue)  copies=" << Tracked::copies
              << " moves=" << Tracked::moves << '\n';

    Tracked::copies = Tracked::moves = 0;
    auto b = makeUnique<Tracked>(std::move(t));      // 传右值 -> 移动构造
    std::cout << "forward(rvalue)  copies=" << Tracked::copies
              << " moves=" << Tracked::moves << '\n';

    // 对照组：如果不写 std::forward，参数在函数体里永远是左值（有名形参即左值），
    // 结果是「传右值也走拷贝」。下面这个 factory 故意漏掉 forward 做反例。
    auto noForward = [](const Tracked& x) { return std::unique_ptr<Tracked>(new Tracked(x)); };
    Tracked::copies = Tracked::moves = 0;
    auto c = noForward(std::move(t));
    std::cout << "no forward       copies=" << Tracked::copies
              << " moves=" << Tracked::moves << "   <- 丢掉了值类别\n";

    (void)a; (void)b; (void)c;
    return 0;
}
