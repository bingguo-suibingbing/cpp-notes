// ============================================================================
//  07_template_metaprogramming.cpp
// ----------------------------------------------------------------------------
//  演示主题：
//    1. std::integer_sequence / std::index_sequence 与编译期展开
//    2. 用 index_sequence 实现 tuple 遍历（std::apply 的原理）
//    3. type traits 家族的实际用途：is_same / conditional_t / enable_if_t /
//       void_t / decay_t / common_type_t
//    4. SFINAE（enable_if / void_t）与 C++20 concepts 的对比
//    5. if constexpr 做编译期分支，对比 tag dispatch 与 SFINAE 两代写法
//    6. 编译期与运行期的一致性证明（同一算法两种执行时机）
//
//  关键结论：
//    - 模板元编程的价值不是「炫技」，而是把「运行期的分支/循环/查表」搬到编译期：
//      编译期算完的东西在运行期是常数，运行期的类型分支在运行期不存在。
//    - index_sequence 是「用编译期下标集合驱动运行期展开」的标准手法：
//      一个 for 循环配 std::get<I>，展开成 N 条互不相同的语句。
//    - tag dispatch（靠重载决议选重载）-> SFINAE（靠替换失败剔除候选）
//      -> if constexpr（靠编译器丢弃分支）-> concepts（靠约束参与有序比较），
//      是同一个需求的四代写法，新代码首选 concepts + if constexpr。
//    - 编译期能做的前提：constexpr 上下文里不能有 reinterpret_cast、
//      dynamic_cast、未定义行为、以及 C++20 之前不能有 new/delete（C++20 起可以）。
// ============================================================================

#include <array>
#include <concepts>
#include <cstddef>
#include <iostream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// ======================================================== 1. integer_sequence
// 手写一个「编译期下标包生成器」，理解标准库的 integer_sequence 怎么来的。
template <typename T, T... Ints>
struct IntSeq {
    static constexpr std::size_t size = sizeof...(Ints);
};
// 用递归把 0..N-1 展开成参数包。
// 踩坑记录（本项目 MSVC 14.51 实测）：终止条件**不能**写成偏特化
//     template <typename T, T... Ints> struct MakeIntSeq<T, 0, Ints...>;
// MSVC 报 error C2754: a partial specialization cannot have a dependent
// non-type template parameter —— 拿模板参数 T 去给非类型参数 0 写偏特化属于
// 「依赖的非类型参数」，不允许。
// 绕法：把「数字」包成类型（IntConst<N>），偏特化只匹配类型形状。
// 这是模板元编程里最常用的手法之一（std::integral_constant 就是干这个的）。
template <typename T, T N> struct IntConst { static constexpr T value = N; };

template <typename T, typename N, T... Ints>
struct MakeIntSeqImpl : MakeIntSeqImpl<T, IntConst<T, N::value - 1>, N::value - 1, Ints...> {};
template <typename T, T... Ints>
struct MakeIntSeqImpl<T, IntConst<T, 0>, Ints...> {
    using type = IntSeq<T, Ints...>;
};
template <typename T, T N>
using MakeIntSeq = typename MakeIntSeqImpl<T, IntConst<T, N>>::type;

// 编译期规约：把数组所有元素相乘（C++17 折叠表达式 + constexpr，没有运行期循环）
constexpr int productOf(const int* p, std::size_t n) {
    int acc = 1;
    for (std::size_t i = 0; i < n; ++i) acc *= p[i];
    return acc;
}
template <std::size_t N>
constexpr int productOf(const std::array<int, N>& a) {
    int acc = 1;
    for (int v : a) acc *= v;
    return acc;
}

// ======================================================== 2. index_sequence 遍历 tuple
// 每个元组元素都要用「编译期常量下标」去取，所以必须靠下标包展开成 N 条语句。
template <typename Tuple, std::size_t... I>
void printTupleImpl(const Tuple& t, std::index_sequence<I...>) {
    std::cout << "  (";
    // 折叠表达式依次取第 I 个元素，I 是编译期常量，std::get<I> 才能成立
    ((std::cout << (I == 0 ? "" : ", ") << std::get<I>(t)), ...);
    std::cout << ")\n";
}
template <typename... Ts>
void printTuple(const std::tuple<Ts...>& t) {
    printTupleImpl(t, std::index_sequence_for<Ts...>{});
}

// 同一个手法实现「把 tuple 当参数包调用函数」—— 这就是 std::apply 的核心。
// 三个函数都写 constexpr，于是可以在 static_assert 里验证行为与标准库一致。
template <typename F, typename Tuple, std::size_t... I>
constexpr auto applyImpl(F&& f, Tuple&& t, std::index_sequence<I...>)
    -> decltype(std::forward<F>(f)(std::get<I>(std::forward<Tuple>(t))...)) {
    return std::forward<F>(f)(std::get<I>(std::forward<Tuple>(t))...);
}
template <typename F, typename Tuple>
constexpr auto myApply(F&& f, Tuple&& t)
    -> decltype(applyImpl(std::forward<F>(f), std::forward<Tuple>(t),
                          std::make_index_sequence<std::tuple_size_v<std::remove_reference_t<Tuple>>>{})) {
    return applyImpl(std::forward<F>(f), std::forward<Tuple>(t),
                     std::make_index_sequence<std::tuple_size_v<std::remove_reference_t<Tuple>>>{});
}

// ======================================================== 3. type traits 的实际用途
// (a) void_t + 偏特化：检测「能不能 +=」
template <typename T, typename = void>
struct HasPlusAssign : std::false_type {};
template <typename T>
struct HasPlusAssign<T, std::void_t<decltype(std::declval<T&>() += std::declval<const T&>())>>
    : std::true_type {};

// (b) conditional_t：编译期三元表达式，常用来选容器/选存储类型
template <typename T>
using SmallContainer = std::conditional_t<(sizeof(T) <= 8), std::array<T, 4>, std::vector<T>>;

// (c) decay_t：把「数组/函数/引用/cv」全部退化掉，得到按值形参的真实类型
//     这正是 auto / 模板按值传参时的规则，写容器或仿函数存储时经常要用。
template <typename T>
using Stored = std::decay_t<T>;

// (d) enable_if_t 的老写法（对比用），见下面第 4 节
template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
constexpr T twiceSfinae(T x) { return x + x; }

// ======================================================== 4. SFINAE vs concepts
// 需求：写一个 require_addable 函数，只接受「能和自己相加」的类型。
// (a) C++17 SFINAE 版：约束信息藏在模板参数列表里
template <typename T, typename = std::void_t<decltype(std::declval<T>() + std::declval<T>())>>
auto requireAddableSfinae(T a, T b) { return a + b; }

// (b) C++20 concepts 版：约束写在名字旁边，一眼看懂
template <typename T>
concept SelfAddable = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;      // 结果还必须能转回 T
};

template <SelfAddable T>
T requireAddableConcept(T a, T b) { return a + b; }

// 两个 requires 表达式的组合：既要求能加，又要求能打印
template <typename T>
concept SelfAddableAndPrintable = SelfAddable<T> && requires(std::ostream& os, const T& v) {
    { os << v } -> std::same_as<std::ostream&>;
};

template <typename T> requires SelfAddableAndPrintable<T>
void reportSum(T a, T b) {
    std::cout << "  sum = " << (a + b) << '\n';
}

// ======================================================== 5. 编译期分支的三代写法
// 需求：整型参数用整数算法，浮点参数用浮点算法，其它类型给一句提示。

// 自己做整数转字符串，全程只依赖 std::string 的 constexpr 接口。
// 为什么不直接用 std::to_string？—— 本项目实测 MSVC 14.51 的 std::to_string
// 不是 constexpr（会报 C2131: expression did not evaluate to a constant），
// 所以想让三代写法都能进 static_assert，就得自己写一个 constexpr 版本。
// 这类「std::xxx 在 MSVC 上还不够 constexpr」的坑，在编译期计算代码里很常见。
template <typename T>
constexpr std::string intToString(T v) {
    if (v == 0) return "0";
    const bool negative = v < 0;
    unsigned long long u = negative ? 0ull - static_cast<unsigned long long>(v)
                                    : static_cast<unsigned long long>(v);
    std::string digits;
    while (u > 0) {
        digits.push_back(static_cast<char>('0' + u % 10));   // 低位先进，最后要反过来
        u /= 10;
    }
    if (negative) digits.push_back('-');
    std::string out;
    for (std::size_t i = digits.size(); i > 0; --i) out.push_back(digits[i - 1]);
    return out;
}

// 整型分支：用整数除法（截断），返回字符串方便统一比较
template <typename T>
constexpr std::string calcIntegral(T v) { return intToString(v / 2) + " (integral)"; }
// 浮点分支：用浮点除法（保留小数）
template <typename T>
constexpr std::string calcFloating(T v) { return intToString(static_cast<int>(v / 2.0)) + " (floating)"; }

// (a) 第一代：tag dispatch —— 靠重载决议，把一个标签类型当第一个参数
//     全部写成 constexpr，才能在 static_assert 里比较三代写法的行为一致性。
template <typename T>
constexpr std::string calcImpl(T v, std::true_type)  { return calcIntegral(v); }
template <typename T>
constexpr std::string calcImpl(T v, std::false_type) { return calcFloating(v); }
template <typename T>
constexpr std::string calcTag(T v) { return calcImpl(v, std::is_integral<T>{}); }

// (b) 第二代：SFINAE —— 两个互斥约束的重载，非法那个被移出重载集
template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
constexpr std::string calcSfinae(T v) { return calcIntegral(v); }
template <typename T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
constexpr std::string calcSfinae(T v) { return calcFloating(v); }

// (c) 第三代：if constexpr —— 一个函数体，编译器只保留命中的分支
//     未命中分支里的代码甚至不必对当前 T 合法（这是它最强的地方）。
template <typename T>
constexpr std::string calcConstexpr(T v) {
    if constexpr (std::is_integral_v<T>) {
        return calcIntegral(v);
    } else if constexpr (std::is_floating_point_v<T>) {
        return calcFloating(v);
    } else {
        // 这个分支对 int/double 而言根本不会被实例化，所以可以写 T 不支持的代码
        return std::string("unsupported type (sizeof=") + intToString(sizeof(T)) + ")";
    }
}

int main() {
    std::cout << "==== 1. integer_sequence / index_sequence ====\n";
    // 手写的序列生成器：0..4 一共 5 个编译期常量
    using Seq = MakeIntSeq<std::size_t, 5>;
    static_assert(Seq::size == 5);
    static_assert(std::is_same_v<Seq, IntSeq<std::size_t, 0, 1, 2, 3, 4>>);
    // 标准库版本：std::make_index_sequence<N> 就是它
    static_assert(std::is_same_v<std::make_index_sequence<5>,
                                 std::integer_sequence<std::size_t, 0, 1, 2, 3, 4>>);
    static_assert(std::index_sequence_for<int, double, char>::size() == 3);
    std::cout << "手写 MakeIntSeq<size_t,5> == IntSeq<0,1,2,3,4>：编译期已证明\n";

    // 编译期规约：结果直接当常量用
    constexpr std::array<int, 5> arr{1, 2, 3, 4, 5};
    static_assert(productOf(arr) == 120);
    static_assert(productOf(arr.data(), arr.size()) == 120);
    std::cout << "productOf({1,2,3,4,5}) = " << productOf(arr)
              << "   <- 编译期算好，运行期是常数\n";

    std::cout << "\n==== 2. index_sequence 遍历 tuple ====\n";
    std::tuple<int, std::string, double> t{7, "seven", 3.5};
    static_assert(std::tuple_size_v<decltype(t)> == 3);
    static_assert(std::is_same_v<std::tuple_element_t<1, decltype(t)>, std::string>);
    printTuple(t);

    // 用下标包把 tuple 展开成函数实参（std::apply 的核心手法）
    auto adder = [](int a, const std::string& b, double c) {
        return static_cast<double>(a) + static_cast<double>(b.size()) + c;
    };
    std::cout << "myApply(adder, t) = " << myApply(adder, t) << '\n';
    std::cout << "std::apply(adder, t) = " << std::apply(adder, t) << '\n';
    // 编译期证明：手写版本能把 tuple 拆成参数包（数字求和，结果可当常量）
    static_assert(myApply([](int a, int b) { return a + b; }, std::make_tuple(3, 4)) == 7);
    // 与标准库版本行为一致：t 不是常量，这里只能在运行期比较（下面这行输出 true）
    std::cout << "myApply 与 std::apply 结果一致: " << std::boolalpha
              << (myApply(adder, t) == std::apply(adder, t)) << '\n';

    std::cout << "\n==== 3. type traits 的实际用途 ====\n";
    static_assert(HasPlusAssign<std::string>::value);
    static_assert(HasPlusAssign<int>::value);                     // int 当然能 += int
    static_assert(!HasPlusAssign<std::array<int, 2>>::value);      // array 没有 +=
    static_assert(std::is_same_v<SmallContainer<char>, std::array<char, 4>>);
    static_assert(std::is_same_v<SmallContainer<double>, std::array<double, 4>>);
    // std::string 大于 8 字节（64 位 MSVC 下是 32 字节）-> 走 vector 分支
    static_assert(sizeof(std::string) > 8);
    static_assert(std::is_same_v<SmallContainer<std::string>, std::vector<std::string>>);
    // decay_t 把数组退化成指针、引用和 const 全部剥掉
    static_assert(std::is_same_v<Stored<const int&>, int>);
    static_assert(std::is_same_v<Stored<int[5]>, int*>);
    static_assert(std::is_same_v<Stored<int&>, int>);
    // common_type_t：算「两个类型混合运算后的类型」，写泛型数值代码必备
    static_assert(std::is_same_v<std::common_type_t<char, int>, int>);
    static_assert(std::is_same_v<std::common_type_t<int, double>, double>);
    static_assert(std::is_same_v<std::common_type_t<float, double>, double>);
    std::cout << "twiceSfinae(21) = " << twiceSfinae(21) << '\n';

    std::cout << "\n==== 4. SFINAE 与 concepts 并排 ====\n";
    std::cout << "requireAddableSfinae(1, 2)   = " << requireAddableSfinae(1, 2) << '\n';
    std::cout << "requireAddableConcept(1, 2)  = " << requireAddableConcept(1, 2) << '\n';
    std::cout << "requireAddableConcept(\"a\",\"b\") = "
              << requireAddableConcept(std::string("a"), std::string("b")) << '\n';
    reportSum(3, 4);
    static_assert(SelfAddable<int>);
    static_assert(SelfAddable<std::string>);
    static_assert(!SelfAddable<std::array<int, 2>>);      // array 没有 operator+
    static_assert(SelfAddableAndPrintable<int>);
    static_assert(!SelfAddableAndPrintable<std::array<int, 2>>);
    // 注意区别：SFINAE 版本无法直接问「这个类型满足约束吗」，
    // 只能靠 HasXxx 检测类间接判断；concept 本身就是一个可以被 static_assert 的谓词。

    std::cout << "\n==== 5. 编译期分支的三代写法 ====\n";
    std::cout << "tag dispatch : calcTag(9)=" << calcTag(9)
              << " calcTag(9.0)=" << calcTag(9.0) << '\n';
    std::cout << "SFINAE       : calcSfinae(9)=" << calcSfinae(9)
              << " calcSfinae(9.0)=" << calcSfinae(9.0) << '\n';
    std::cout << "if constexpr : calcConstexpr(9)=" << calcConstexpr(9)
              << " calcConstexpr(9.0)=" << calcConstexpr(9.0) << '\n';
    // if constexpr 的独门绝技：未命中分支对当前类型可以完全非法
    std::cout << "if constexpr : calcConstexpr(\"abc\")="
              << calcConstexpr(std::string("abc")) << "   <- 走了 else 分支\n";
    // 编译期证明三代写法对同一实参给出完全相同的行为。
    // 这里用 if constexpr 比较常量，避免依赖 std::string 在常量表达式里的比较。
    if constexpr (calcTag(9) == calcConstexpr(9)) {
        std::cout << "tag dispatch 与 if constexpr 结果一致：编译期已证明\n";
    } else {
        static_assert(calcTag(9) == calcConstexpr(9), "三代写法必须行为一致");
    }
    if constexpr (calcSfinae(9.0) == calcConstexpr(9.0)) {
        std::cout << "SFINAE 与 if constexpr 结果一致：编译期已证明\n";
    } else {
        static_assert(calcSfinae(9.0) == calcConstexpr(9.0), "三代写法必须行为一致");
    }
    static_assert(calcSfinae(9.0) == calcConstexpr(9.0));

    std::cout << "\n==== 6. 同一算法：编译期与运行期 ====\n";
    // 同一个 productOf，既能在编译期算（static_assert），也能在运行期算。
    // 编译期算的那个在汇编里就是一个立即数；运行期算的那个才真的走循环。
    constexpr int kSmall = productOf(std::array<int, 4>{2, 3, 4, 5});
    static_assert(kSmall == 120);
    std::array<int, 4> runtime{2, 3, 4, 5};
    std::cout << "编译期 kSmall = " << kSmall
              << ", 运行期 productOf = " << productOf(runtime) << '\n';
    return 0;
}
