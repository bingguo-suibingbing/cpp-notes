// ============================================================================
// 07_special_members_operator_overload.cpp
// 演示主题：
//   1. 成员 vs 非成员：哪些运算符【必须】是成员，哪些【应该】是非成员
//   2. operator<< 输出运算符（必须是非成员，否则写成 a << std::cout 才行）
//   3. operator[] 的 const 与非 const 两个版本
//   4. operator== 与 C++20 三路比较 operator<=>（写一个自动得到 < > <= >=）
//   5. 自增自减的前缀与后缀（后缀那个 int 参数只是占位标记）
//   6. operator() 仿函数，以及与 lambda 的关系
//   7. operator bool 与 explicit：避免意外转换
//   8. 不允许重载的运算符清单
//
// 关键结论：
//   运算符重载的目标是「让自定义类型用起来像内建类型」，不是炫技；
//   能用普通函数名表达清楚的时候，就不要用运算符。
// ============================================================================

#include <algorithm>
#include <compare>
#include <cstddef>
#include <iostream>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

void PrintLine() { std::cout << "--------------------------------------------------\n"; }

// ===========================================================================
// 2/4. 二维向量：对称的二元运算符写成非成员，operator+= 写成成员
// ===========================================================================
class Vec2 {
public:
    Vec2() = default;
    constexpr Vec2(double x, double y) : x_(x), y_(y) {}

    // 复合赋值必须是成员：它修改左侧对象，语义上「属于」这个类型
    Vec2& operator+=(const Vec2& rhs) {
        x_ += rhs.x_;
        y_ += rhs.y_;
        return *this;
    }

    Vec2& operator-=(const Vec2& rhs) {
        x_ -= rhs.x_;
        y_ -= rhs.y_;
        return *this;
    }

    // 一元负号：成员或非成员都可以；作为成员时没有参数
    Vec2 operator-() const { return Vec2(-x_, -y_); }

    double x() const { return x_; }
    double y() const { return y_; }

    // 非成员（友元）函数：左右操作数地位对等，还能访问私有成员
    friend Vec2 operator+(Vec2 lhs, const Vec2& rhs) {
        lhs += rhs;                    // 复用 operator+=，只实现一次逻辑
        return lhs;
    }

    friend Vec2 operator-(Vec2 lhs, const Vec2& rhs) {
        lhs -= rhs;
        return lhs;
    }

    friend bool operator==(const Vec2& a, const Vec2& b) {
        return a.x_ == b.x_ && a.y_ == b.y_;
    }

    // C++20：只写 != 也能用，但显式删除 / 默认都可以；这里演示默认合成
    friend bool operator!=(const Vec2& a, const Vec2& b) = default;

    // 输出运算符：第一个参数必须是 std::ostream&，所以只能是非成员
    friend std::ostream& operator<<(std::ostream& os, const Vec2& v) {
        return os << "(" << v.x_ << ", " << v.y_ << ")";
    }

private:
    double x_ = 0.0;
    double y_ = 0.0;
};

// ===========================================================================
// 3. operator[] 的 const 与非 const 双版本
// ===========================================================================
class IntArray {
public:
    explicit IntArray(std::size_t n) : data_(new int[n]()), size_(n) {}

    // 非 const 版本：返回引用，允许写
    int& operator[](std::size_t i) { return data_[i]; }

    // const 版本：返回 const 引用，只允许读
    // 注意：两个版本只有 const 限定不同，这是标准允许且推荐的成对写法。
    const int& operator[](std::size_t i) const { return data_[i]; }

    std::size_t size() const { return size_; }

private:
    std::unique_ptr<int[]> data_;      // Rule of Zero：用 unique_ptr 管资源，自己不写析构
    std::size_t size_;
};

// ===========================================================================
// 4. C++20 三路比较 operator<=>
// ===========================================================================
class Version {
public:
    Version(int major, int minor, int patch) : major_(major), minor_(minor), patch_(patch) {}

    // 一个 default 的 <=> 就自动得到 < <= > >= 四个比较运算符，
    // 而且比较是按成员【声明顺序】做的（major -> minor -> patch），正是版本号想要的效果。
    auto operator<=>(const Version&) const = default;

    // 写 default 的 == 会得到 == 和 !=；其实 default 的 <=> 已经隐含生成了 == 的候选，
    // 这里显式写出来只是为了演示「可以同时 default 两个」。
    bool operator==(const Version&) const = default;

    int major() const { return major_; }
    int minor() const { return minor_; }
    int patch() const { return patch_; }

private:
    int major_;
    int minor_;
    int patch_;
};

// 编译期证据：一个 default 的 <=> 确实换来了全套比较运算符
static_assert(std::three_way_comparable<Version>, "default 的 <=> 让类型满足三路可比较");
static_assert(!std::three_way_comparable<Vec2>, "只写了 ==/!= 的 Vec2 不能做大小比较");

// ===========================================================================
// 5. 自增自减：前缀与后缀
// ===========================================================================
class Counter {
public:
    explicit Counter(int v) : value_(v) {}

    // 前缀 ++：先加再返回自身引用（返回引用可以链式 ++（++c））
    Counter& operator++() {
        ++value_;
        return *this;
    }

    // 后缀 ++：参数 int 只是「标记这是后缀版本」的占位符，调用时不用传。
    // 返回旧值的副本 => 必须返回值，不能返回引用（否则返回的是悬空引用）。
    Counter operator++(int) {          // NOLINT：这里的 int 是语言规定，不是笔误
        Counter old(*this);
        ++value_;
        return old;
    }

    Counter& operator--() {
        --value_;
        return *this;
    }

    Counter operator--(int) {
        Counter old(*this);
        --value_;
        return old;
    }

    int value() const { return value_; }

private:
    int value_;
};

// ===========================================================================
// 6. operator() 仿函数，与 lambda 的关系
// ===========================================================================
class MultiplyBy {
public:
    explicit MultiplyBy(int factor) : factor_(factor) {}

    // 带状态的「函数」：这就是仿函数（function object）
    int operator()(int x) const { return x * factor_; }

private:
    int factor_;
};

// 仿函数能当模板参数（编译期确定，可内联）；lambda 本质就是编译器生成的仿函数
template <typename F>
std::vector<int> Transform(const std::vector<int>& in, F f) {
    std::vector<int> out;
    out.reserve(in.size());
    for (int v : in) {
        out.push_back(f(v));
    }
    return out;
}

// ===========================================================================
// 7. operator bool 与 explicit
// ===========================================================================
class FileChecker {
public:
    explicit FileChecker(bool ok) : ok_(ok) {}

    // explicit operator bool：只在条件判断语境里转换（if / while / && / || / !），
    // 不会隐式转成 int => 避免 `int n = checker;` 或 `checker + 1` 这种荒唐代码。
    explicit operator bool() const { return ok_; }

    // 【错误写法演示】不加 explicit 时：
    //   int n = checker;         // 能编译！n = 0 或 1
    //   checker + 1;             // 能编译！
    //   std::cout << checker;    // 能编译，输出 0/1 —— 本意往往是想输出文字描述
    // 加上 explicit 后这三行全部编译错误（error C2440 之类）。

private:
    bool ok_;
};

// ===========================================================================
// 演示入口
// ===========================================================================
void DemoMemberVsNonMember() {
    std::cout << "==== 1. 运算符重载：成员 vs 非成员 ====\n";
    Vec2 a(1.0, 2.0);
    Vec2 b(3.0, 4.0);
    std::cout << "    a = " << a << ", b = " << b << "\n";
    std::cout << "    a + b   = " << (a + b) << "   （非成员，左右对称）\n";
    std::cout << "    a - b   = " << (a - b) << "\n";
    std::cout << "    -a      = " << (-a) << "   （一元运算符）\n";
    std::cout << "    a == b  = " << std::boolalpha << (a == b) << "\n";
    std::cout << "    a != b  = " << (a != b) << "   （default 合成）\n";

    Vec2 c;
    c += a;                                   // 复合赋值是成员
    c += b;
    std::cout << "    c = 默认构造后 c += a; c += b; -> " << c << "\n";

    std::cout << "    设计规则：\n";
    std::cout << "      1. 【必须】是成员的运算符：\n";
    std::cout << "         =  []  ()  ->  （以及类型转换运算符）\n";
    std::cout << "         理由：语言规定，它们的第一操作数就是 *this，写成非成员无法表达。\n";
    std::cout << "      2. 【应该】是非成员的运算符：\n";
    std::cout << "         ==  !=  <  <=  >  >=  +  -  *  /  <<  >>\n";
    std::cout << "         理由：让左右操作数地位对等，尤其支持「字面量在左」的写法，\n";
    std::cout << "               例如 2 * v、std::cout << v；\n";
    std::cout << "               如果 2 * v 要调用 v.operator*(2)，就必须依赖隐式转换，容易失效。\n";
    std::cout << "      3. 【必须】是成员的复合赋值：+= -= *= /= %= &= |= ^= <<= >>=\n";
    std::cout << "         理由：语义上它修改左侧对象，是成员的自然选择（也便于链式调用）。\n";
    std::cout << "    实现技巧：非成员的 operator+ 直接复用成员的 operator+=，逻辑只写一遍。\n";
    PrintLine();
}

void DemoSubscriptAndOutput() {
    std::cout << "==== 2/3. operator[] 的 const 与非 const ====\n";
    IntArray arr(4);
    for (std::size_t i = 0; i < arr.size(); ++i) {
        arr[i] = static_cast<int>(i * i);      // 非 const 版本，可写
    }
    std::cout << "    写入后: ";
    for (std::size_t i = 0; i < arr.size(); ++i) {
        std::cout << arr[i] << (i + 1 == arr.size() ? "" : ", ");
    }
    std::cout << "\n";

    const IntArray& cref = arr;                // const 引用只能看到 const 版本
    std::cout << "    const 引用读取 cref[2] = " << cref[2] << "\n";
    // cref[2] = 99;                           // 【错误写法】error C3892: 不能给常量赋值
    std::cout << "    cref[2] = 99; 会报 error C3892: 不能给常量赋值 —— 这正是我们要的编译期保护。\n";
    std::cout << "    为什么不只写一个 const 版本？因为那样的话连非 const 对象也没法写入。\n";
    std::cout << "    operator<< 为什么必须是非成员？\n";
    std::cout << "      写成成员就变成 a << std::cout，与惯用法 std::cout << a 相反。\n";
    std::cout << "      返回 std::ostream& 是为了支持链式：std::cout << a << b << \"\\n\";\n";
    std::cout << "      参数用 const& 是为了不拷贝；友元声明是为了能读私有成员（也可以用公有 getter）。\n";
    PrintLine();
}

void DemoSpaceship() {
    std::cout << "==== 4. C++20 三路比较 operator<=> ====\n";
    const Version v1(1, 2, 3);
    const Version v2(1, 10, 0);
    const Version v3(1, 2, 3);

    std::cout << "    v1 = 1.2.3, v2 = 1.10.0, v3 = 1.2.3\n";
    std::cout << "    v1 <  v2 : " << (v1 < v2) << "   （1.2.3 < 1.10.0，按声明顺序逐成员比较）\n";
    std::cout << "    v1 >  v2 : " << (v1 > v2) << "\n";
    std::cout << "    v1 <= v3 : " << (v1 <= v3) << "\n";
    std::cout << "    v1 >= v3 : " << (v1 >= v3) << "\n";
    std::cout << "    v1 == v3 : " << (v1 == v3) << "    （default 的 == 逐成员比较）\n";
    std::cout << "    v1 != v2 : " << (v1 != v2) << "\n";

    // 三路比较的返回值本身也有意义：它是 std::strong_ordering
    const std::strong_ordering ord = (v1 <=> v2);
    std::cout << "    (v1 <=> v2) 的结果: "
              << (ord < 0 ? "小于" : (ord == 0 ? "相等" : "大于"))
              << "（类型是 std::strong_ordering，可继续参与比较）\n";

    // 排序：一个 <=> 就够 std::sort 用
    std::vector<Version> versions{{2, 0, 0}, {1, 5, 0}, {1, 2, 9}, {1, 2, 3}};
    std::sort(versions.begin(), versions.end());
    std::cout << "    std::sort 之后: ";
    for (const Version& v : versions) {
        std::cout << v.major() << "." << v.minor() << "." << v.patch() << "  ";
    }
    std::cout << "\n";
    std::cout << "    三路比较的三种返回类型（理解它们的区别很实用）：\n";
    std::cout << "      std::strong_ordering  : 相等就是可互换（整数、版本号、字符串）=> 默认选择\n";
    std::cout << "      std::weak_ordering    : 相等但不可互换（例如「大小写不敏感的名字」）\n";
    std::cout << "      std::partial_ordering : 存在不可比较的值（浮点数的 NaN）\n";
    std::cout << "      auto operator<=>(const T&) const = default; 会按成员自动推导出最合适的那个。\n";
    std::cout << "    static_assert(std::three_way_comparable<Version>) 已在编译期验证。\n";
    PrintLine();
}

void DemoIncrementDecrement() {
    std::cout << "==== 5. 前置与后置自增自减 ====\n";
    Counter c(5);
    std::cout << "    初始            c.value() = " << c.value() << "\n";
    std::cout << "    ++c 的返回值     = " << (++c).value() << "，之后 c.value() = " << c.value() << "\n";
    const int after_post = (c++).value();
    std::cout << "    c++ 的返回值     = " << after_post << "（旧值），之后 c.value() = " << c.value() << "\n";
    std::cout << "    --c 的返回值     = " << (--c).value() << "\n";
    const int after_post_dec = (c--).value();
    std::cout << "    c-- 的返回值     = " << after_post_dec << "（旧值），之后 c.value() = " << c.value()
              << "\n";
    std::cout << "    规则：\n";
    std::cout << "      1. 后缀版本的 int 参数是语言规定的占位符，调用时不用写；\n";
    std::cout << "      2. 前缀返回引用（可以 ++(++c)），后缀返回值（必须保存旧值）；\n";
    std::cout << "      3. 性能上优先用前缀 ++：后缀要多构造并返回一个临时对象。\n";
    PrintLine();
}

void DemoFunctorsAndLambda() {
    std::cout << "==== 6. operator() 仿函数与 lambda ====\n";
    const std::vector<int> input{1, 2, 3, 4};
    const MultiplyBy times3(3);
    const std::vector<int> out1 = Transform(input, times3);        // 传仿函数
    const std::vector<int> out2 = Transform(input, [](int x) { return x + 100; });  // 传 lambda

    std::cout << "    MultiplyBy(3) : ";
    for (int v : out1) {
        std::cout << v << " ";
    }
    std::cout << "\n";
    std::cout << "    lambda x+100  : ";
    for (int v : out2) {
        std::cout << v << " ";
    }
    std::cout << "\n";
    std::cout << "    lambda 的本质：编译器为每个 lambda 生成一个匿名类，\n";
    std::cout << "      捕获列表就是它的数据成员，函数体就是 operator()。\n";
    std::cout << "      所以 lambda 就是「语法糖版的仿函数」。\n";
    std::cout << "    与 std::function 的取舍：\n";
    std::cout << "      - 模板参数 / auto 参数：编译期确定类型 => 可内联，零开销（首选）；\n";
    std::cout << "      - std::function：类型擦除，运行期一次间接调用，有构造与拷贝开销，\n";
    std::cout << "        但能存进容器、能作为普通成员变量 => 需要「运行时替换行为」时用它。\n";
    std::cout << "      经验：能用模板参数就别用 std::function；\n";
    std::cout << "            需要长期存储或跨 ABI 传递回调时才用 std::function。\n";
    PrintLine();
}

void DemoOperatorBool() {
    std::cout << "==== 7. operator bool 与 explicit ====\n";
    const FileChecker ok(true);
    const FileChecker bad(false);
    if (ok) {
        std::cout << "    好用：if (ok) 直接判断 -> true\n";
    }
    if (!bad) {
        std::cout << "    好用：if (!bad) 也支持 -> true\n";
    }
    std::cout << "    逻辑与/或也能参与：ok && !bad = " << (ok && !bad) << "\n";
    // const int n = ok;                       // 【错误写法】explicit 后 error C2440：无法转换
    std::cout << "    【错误写法演示】不加 explicit 时下面三行都能编译（这就是隐患）：\n";
    std::cout << "      int n = ok;              // 悄悄变成 1\n";
    std::cout << "      ok + 1;                  // 变成 2\n";
    std::cout << "      std::cout << ok;         // 输出 1，而你的本意大概是「可用」两个字\n";
    std::cout << "    工程习惯：所有「表示真假」的转换运算符都写 explicit operator bool。\n";
    std::cout << "    其他转换运算符同理：单参构造加 explicit，转换运算符也加 explicit，\n";
    std::cout << "    只在确实需要隐式转换时才去掉（例如智能指针之间的转换）。\n";
    PrintLine();
}

void DemoForbiddenOperators() {
    std::cout << "==== 8. 不允许重载的运算符 ====\n";
    std::cout << "    ::       作用域解析\n";
    std::cout << "    .        成员访问\n";
    std::cout << "    .*       成员指针访问\n";
    std::cout << "    ?:       三目条件\n";
    std::cout << "    sizeof   求大小\n";
    std::cout << "    typeid   类型信息\n";
    std::cout << "    alignof  对齐要求\n";
    std::cout << "    #  ##    预处理指令\n";
    std::cout << "    另外几条容易忽略的限制：\n";
    std::cout << "      - 不能发明新运算符，也不改变优先级与结合性（&& 仍是短路？不，重载后会失去短路！）\n";
    std::cout << "      - 重载 && || 与逗号 , 会破坏求值顺序与短路语义 => 几乎总是坏主意；\n";
    std::cout << "      - 重载 & 会破坏取地址语义，除非你确实在做智能指针；\n";
    std::cout << "      - operator= 只能作为成员，且不能重载为全局的 operator=(A, B)。\n";
    std::cout << "    判断标准（很重要）：\n";
    std::cout << "      只有当「这个运算符的语义对使用者来说是显然的」才重载它。\n";
    std::cout << "      Vec2 + Vec2 显然；Matrix << int 就纯属炫技了。\n";
    std::cout << "    本示例用到的头文件说明：<compare> 提供 std::strong_ordering 等三路比较类型，\n";
    std::cout << "      std::three_way_comparable 概念也在其中。\n";
    PrintLine();
}

// 单独验证 oper 的实用场景：用 ostringstream 生成文本
std::string Describe(const Vec2& v) {
    std::ostringstream os;
    os << "点 " << v << " 到原点距离约 "
       << (v.x() * v.x() + v.y() * v.y());
    return os.str();
}

}  // namespace

int main() {
    std::cout << "################ 07 特殊成员与运算符重载 ################\n\n";
    DemoMemberVsNonMember();
    DemoSubscriptAndOutput();
    DemoSpaceship();
    DemoIncrementDecrement();
    DemoFunctorsAndLambda();
    DemoOperatorBool();
    DemoForbiddenOperators();
    std::cout << "附加示例：operator<< 最大的价值是让自定义类型可以直接进日志与断言。\n";
    std::cout << "  " << Describe(Vec2(3.0, 4.0)) << "\n";
    std::cout << "  （用 std::ostringstream 拼接，说明 operator<< 与标准流完全兼容）\n";
    return 0;
}
