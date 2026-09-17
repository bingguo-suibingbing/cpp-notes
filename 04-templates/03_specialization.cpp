// ============================================================================
//  03_specialization.cpp
// ----------------------------------------------------------------------------
//  演示主题：
//    1. 主模板 + 全特化（template <> struct TypeName<int>）
//    2. 偏特化：固定「形状」（T*、const T&、std::vector<T>）
//    3. 用特化做编译期条件分支：手写 enable_if / EnableIf_t
//    4. 按 bool 选类型（Storage<true> / Storage<false>）
//    5. 手写 remove_const，理解标准库萃取怎么实现
//    6. void_t 检测惯用法（C++17）：SFINAE 版「这个类型有没有 size()」
//    7. 偏特化做「像不像指针」的判定
//
//  关键结论：
//    - 类模板可以偏特化，函数模板不行（函数模板只能重载，见 01）。
//    - 偏特化是「模式匹配」：编译器在所有可行的偏特化里挑最特化的那个。
//      所以偏特化的声明顺序不重要，但写全 0 个模板参数的 <> 就是全特化。
//    - SFINAE 的本质：替换失败不是错误（Substitution Failure Is Not An Error），
//      不满足条件的候选被静默地从重载集里移走，而不是让编译失败。
//    - void_t<Ts...> 恒等于 void；把「表达式是否合法」塞进模板参数，
//      表达式非法就触发替换失败，于是命中 false_type 那个偏特化。
// ============================================================================

#include <cstddef>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// ------------------------------------------------------------ 1. 主模板 + 全特化
template <typename T>
struct TypeName { static constexpr const char* value = "unknown"; };

// 全特化：模板参数列表写成空的 <>，必须出现在主模板之后。
template <> struct TypeName<int>         { static constexpr const char* value = "int"; };
template <> struct TypeName<double>      { static constexpr const char* value = "double"; };
template <> struct TypeName<std::string> { static constexpr const char* value = "std::string"; };

// ------------------------------------------------------------ 2. 偏特化：固定「形状」
// 偏特化仍然带模板参数，只是把主模板的 <T> 换成一个「模式」。
template <typename T> struct TypeName<T*>              { static constexpr const char* value = "T*"; };
template <typename T> struct TypeName<const T&>        { static constexpr const char* value = "const T&"; };
template <typename T> struct TypeName<std::vector<T>>  { static constexpr const char* value = "std::vector<T>"; };
// 再补一个：右值引用形状。注意它和 const T& 不冲突（T&& 与 const T& 没有包含关系）。
template <typename T> struct TypeName<T&&>             { static constexpr const char* value = "T&&"; };

// ------------------------------------------ 3. 用特化做编译期条件分支（手写 enable_if）
// 主模板没有 type 成员；只有 B == true 的偏特化才定义 type。
template <bool B, typename T = void>
struct EnableIf {};
template <typename T>
struct EnableIf<true, T> { using type = T; };

// 别名模板：把 typename ...::type 藏起来（std::enable_if_t 就是这么实现的）
template <bool B, typename T = void>
using EnableIf_t = typename EnableIf<B, T>::type;

// 经典 SFINAE 用法：只有整型才能匹配这个重载。
// 当 T 不是整型时，EnableIf_t<..., int> 替换失败，该候选被静默移出重载集，
// 于是编译器去找别的重载；若一个都没有，才报「未找到匹配的重载函数」。
template <typename T, EnableIf_t<std::is_integral_v<T>, int> = 0>
T halfIntegral(T x) { return x / 2; }

// ------------------------------------------ 4. 依 bool 选择存储类型 → 偏特化挑分支
template <bool IsSmall> struct Storage;
template <> struct Storage<true>  { using type = char; };
template <> struct Storage<false> { using type = long long; };

// ------------------------------------------ 5. 手写 remove_const（理解标准库怎么实现的）
template <typename T> struct RemoveConst          { using type = T; };
template <typename T> struct RemoveConst<const T> { using type = T; };

// ------------------------------------------ 6. void_t 检测惯用法（C++17）
// 主模板假定「没有 size()」；只要能写出 declval<T>().size()，偏特化就更匹配。
// std::declval<T>() 只用于不求值语境，不需要 T 可默认构造。
template <typename T, typename = void>
struct HasSize : std::false_type {};

template <typename T>
struct HasSize<T, std::void_t<decltype(std::declval<T>().size())>> : std::true_type {};

// ------------------------------------------ 7. 偏特化做「迭代器类别分派」
// 注意 T* 与 T* const 两个偏特化互不包含，必须各写一条。
template <typename T> struct IsPointerLike           : std::false_type {};
template <typename T> struct IsPointerLike<T*>       : std::true_type  {};
template <typename T> struct IsPointerLike<T* const> : std::true_type  {};

int main() {
    std::cout << "==== 1. 全特化 vs 偏特化 vs 主模板 ====\n";
    std::cout << "TypeName<int>            = " << TypeName<int>::value << '\n';
    std::cout << "TypeName<double>         = " << TypeName<double>::value << '\n';
    std::cout << "TypeName<float>          = " << TypeName<float>::value << "\n";
    std::cout << "TypeName<int*>           = " << TypeName<int*>::value << '\n';
    std::cout << "TypeName<const int&>     = " << TypeName<const int&>::value << '\n';
    std::cout << "TypeName<int&&>          = " << TypeName<int&&>::value << '\n';
    std::cout << "TypeName<vector<int>>    = " << TypeName<std::vector<int>>::value << '\n';
    std::cout << "TypeName<vector<int>*>   = " << TypeName<std::vector<int>*>::value << '\n';

    std::cout << "\n==== 2. 编译期选择（特化的实际用途）====\n";
    static_assert(std::is_same_v<Storage<true>::type, char>);
    static_assert(std::is_same_v<Storage<false>::type, long long>);
    static_assert(std::is_same_v<RemoveConst<const int>::type, int>);
    static_assert(std::is_same_v<RemoveConst<int>::type, int>);
    static_assert(std::is_same_v<EnableIf<true, int>::type, int>);
    static_assert(std::is_same_v<EnableIf_t<true, int>, int>);
    std::cout << "sizeof(Storage<true>::type)  = " << sizeof(Storage<true>::type) << '\n';
    std::cout << "sizeof(Storage<false>::type) = " << sizeof(Storage<false>::type) << '\n';
    std::cout << "halfIntegral(9)              = " << halfIntegral(9) << '\n';
    // halfIntegral(3.5) 会被 SFINAE 移出重载集 -> error C2783/C2672（未找到匹配的重载）

    std::cout << "\n==== 3. 表达式检测（void_t）====\n";
    static_assert(HasSize<std::vector<int>>::value);
    static_assert(HasSize<std::string>::value);
    static_assert(!HasSize<int>::value);
    static_assert(!HasSize<double>::value);
    std::cout << std::boolalpha
              << "HasSize<vector<int>> = " << HasSize<std::vector<int>>::value
              << ", HasSize<int> = " << HasSize<int>::value << '\n';

    std::cout << "\n==== 4. 形状匹配 ====\n";
    static_assert(IsPointerLike<int*>::value);
    static_assert(IsPointerLike<int* const>::value);
    static_assert(!IsPointerLike<int>::value);
    // 注意「指向 const 的指针」与「const 指针」是两回事：
    //   int const*  = 指向 const int 的指针 -> 仍然是 T* 形状（T = const int）-> true
    //   int* const  = const 指针本身        -> 命中 T* const 那条     -> true
    // 两条偏特化形状不同，编译器按最匹配的一条选，不会歧义。
    static_assert(IsPointerLike<int const*>::value);

    // 标准库里的同类萃取（都是靠特化 + SFINAE 实现的）
    static_assert(std::is_pointer_v<int*>);
    static_assert(std::is_same_v<std::remove_reference_t<int&>, int>);
    static_assert(std::is_same_v<std::remove_cv_t<const volatile int>, int>);
    static_assert(std::is_same_v<std::conditional_t<true, int, double>, int>);
    std::cout << "以上 static_assert 全部在编译期通过\n";

    // 提醒：std::hash 这类库组件只允许全特化（偏特化是未定义行为/UBSan 会报），
    // 给自定义类型开哈希槽的正确写法是 template <> struct std::hash<MyType> { ... };
    return 0;
}
