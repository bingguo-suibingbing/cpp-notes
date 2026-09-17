// ============================================================================
//  01_function_template.cpp
// ----------------------------------------------------------------------------
//  演示主题：
//    1. 函数模板的基本形式与模板参数推导（template <typename T>）
//    2. 「推导不做隐式转换，显式实参可以做隐式转换」-- 本章最重要的一个区分
//    3. 返回类型的三种写法：auto 推导 / 尾置 decltype / common_type_t
//    4. 非类型模板参数（NTTP）：数组维度、编译期斐波那契、C++17 的 auto 参数
//    5. 函数模板没有偏特化，想要「特化效果」只能重载（重载决议优先级）
//    6. 用 static_assert 把「编译期事实」变成可见证据
//
//  关键结论：
//    - 模板是编译期机制：实例化出来的函数体可以完全是编译期常量折叠的结果，
//      运行期一条指令都不多（见 NOTES.md 第 5 节的 /Fa 汇编验证）。
//    - 推导（deduction）只看实参的「类型」，不做任何隐式转换；
//      显式指定实参（maxValue<std::string>）时模板参数已定，形参退化成普通形参，
//      于是正常的隐式转换规则重新生效 -- 这就是 maxValue<std::string>("a","b")
//      能编译、而 maxValue("a","b") 推导出的却是 const char* 的原因。
//    - 函数模板不能偏特化，只能靠重载；非模板函数在不差于模板时永远优先。
// ============================================================================

#include <cstddef>
#include <iostream>
#include <string>
#include <type_traits>

// ---------------------------------------------------------------- 1. 基本形式
// typename T 与 class T 在模板参数列表里完全等价；团队里统一用 typename 更表意。
template <typename T>
T maxValue(T a, T b) { return a > b ? a : b; }

// ------------------------------------------------------- 2. 返回类型的三种写法
// C++14：让编译器从 return 语句推导返回类型。缺点：读函数签名看不出返回什么。
template <typename A, typename B>
auto add(A a, B b) { return a + b; }

// C++11 尾置返回类型：显式写出 decltype，参与重载决议时不用实例化函数体。
template <typename A, typename B>
auto addTrailing(A a, B b) -> decltype(a + b) { return a + b; }

// 用公共类型显式收口：addCommon('a', 1) 的返回类型是 int 而不是 char。
template <typename A, typename B>
std::common_type_t<A, B> addCommon(A a, B b) { return a + b; }

// ------------------------------------------------------ 3. 非类型模板参数(NTTP)
// 形参写成「数组的引用」时，维度 N 会被推导出来，于是数组长度成了编译期常量。
// 注意：如果写成 const T* 或 const T[]，N 就丢了（会退化成指针）。
template <typename T, std::size_t N>
constexpr std::size_t arraySize(const T (&)[N]) noexcept { return N; }

// 经典的编译期递归：Factorial<5>::value 是编译期常量，运行期不可能有乘法指令。
template <int N>
struct Factorial {
    static constexpr int value = N * Factorial<N - 1>::value;
};
template <>                                  // 全特化充当递归终止条件
struct Factorial<0> {
    static constexpr int value = 1;
};

// C++17 起非类型模板参数可以用 auto（类型也一起推导），写法更短。
template <auto N>
struct ConstValue { static constexpr auto value = N; };

// ------------------------------------------- 4. 引用形参可以保留数组的维度信息
template <typename T, std::size_t N>
void printArray(const T (&arr)[N]) {
    for (std::size_t i = 0; i < N; ++i)
        std::cout << arr[i] << (i + 1 == N ? '\n' : ' ');
}

// ------------------- 5. 函数模板不支持偏特化，想要「特化效果」就用重载
// 重载决议的优先级：完全匹配的非模板函数 > 更特化的模板 > 通用模板。
template <typename T> const char* kind(T)   { return "value (generic template)"; }
template <typename T> const char* kind(T*)  { return "pointer (more specialized template)"; }
const char*           kind(int)             { return "int (non-template wins)"; }

int main() {
    std::cout << "==== 1. 基本调用与推导 ====\n";
    std::cout << "maxValue(3, 5)          = " << maxValue(3, 5) << '\n';
    std::cout << "maxValue(3.5, 2.5)      = " << maxValue(3.5, 2.5) << '\n';

    std::cout << "\n==== 2. 显式模板实参允许隐式转换 ====\n";
    // maxValue<double>(3, 5)：模板参数被显式钉成 double，形参类型随之确定为 double，
    // 于是实参 3 和 5 走的是普通函数调用的隐式转换（int -> double），可以编译。
    std::cout << "maxValue<double>(3, 5)  = " << maxValue<double>(3, 5) << '\n';

    // maxValue<std::string>("a","b")：T 被钉成 std::string，
    // 实参 "a" 的类型是 const char[2]，通过 std::string 的转换构造函数隐式转成
    // std::string 临时对象，再绑定到按值形参。全程合法，且不报任何警告。
    // 反面对照：maxValue("a","b") 不做转换，T 推导为 const char*，
    // 返回类型也就是 const char*（比较的是两个指针的字面量地址，语义完全不同）。
    std::cout << "maxValue<std::string>(\"a\",\"b\") = "
              << maxValue<std::string>("a", "b") << '\n';
    std::cout << "maxValue(\"a\",\"b\")（推导）  = "
              << maxValue("a", "b") << "   <- 返回 const char*，比的是指针\n";

    // 编译期证据：显式实参改变的是形参类型（能转换），推导决定的才是结果类型。
    static_assert(std::is_same_v<decltype(maxValue<std::string>("a", "b")), std::string>);
    static_assert(std::is_same_v<decltype(maxValue<double>(3, 5)), double>);
    static_assert(std::is_same_v<decltype(maxValue("a", "b")), const char*>);
    // 推导不看转换：maxValue(3, 5.0) 会让 T 同时被推成 int 和 double -> 编译错误。
    // 想亲手看报错？取消下面这行的注释：
    // maxValue(3, 5.0);
    //   error C2782: 'T maxValue(T,T)': 模板参数 'T' 不明确
    //   error C2784: ... 无法从 'double' 为 'T' 推导参数

    std::cout << "\n==== 3. 返回类型的三种写法 ====\n";
    std::cout << "add(1, 2.5)             = " << add(1, 2.5) << '\n';
    std::cout << "addCommon(1, 2.5)       = " << addCommon(1, 2.5) << '\n';
    std::cout << "addTrailing(2, 3)       = " << addTrailing(2, 3) << '\n';
    // add(1,2) 走 int+int -> int；add(1,2.5) 走 int+double -> double（自动提升）
    static_assert(std::is_same_v<decltype(add(1, 2)), int>);
    static_assert(std::is_same_v<decltype(add(1, 2.5)), double>);
    // 注意 common_type 的结果：char+int 的公共类型是 int，不是 char
    static_assert(std::is_same_v<decltype(addCommon('a', 1)), int>);
    static_assert(std::is_same_v<decltype(addCommon(1, 2.5)), double>);

    std::cout << "\n==== 4. 非类型模板参数 ====\n";
    int arr[7] = {1, 2, 3, 4, 5, 6, 7};
    static_assert(arraySize(arr) == 7);      // 维度是编译期常量，可当数组大小用
    std::cout << "arraySize(arr)          = " << arraySize(arr) << '\n';
    std::cout << "arr                     = ";
    printArray(arr);

    static_assert(Factorial<5>::value == 120);   // 编译期就算完了
    static_assert(ConstValue<42>::value == 42);
    // C++17 的 auto NTTP 还能放指针/枚举等，只要满足常量表达式要求
    static_assert(ConstValue<'x'>::value == 'x');
    std::cout << "Factorial<5>::value     = " << Factorial<5>::value << '\n';
    std::cout << "ConstValue<42>::value   = " << ConstValue<42>::value << '\n';

    std::cout << "\n==== 5. 重载优先于偏特化 ====\n";
    int  i = 1;
    int* p = &i;
    std::cout << "kind(42)   -> " << kind(42)   << '\n';   // 非模板精确匹配，最优
    std::cout << "kind(3.14) -> " << kind(3.14) << '\n';   // 只有通用模板能接
    std::cout << "kind(p)    -> " << kind(p)    << '\n';   // T* 版比重载通用版更特化
    return 0;
}
