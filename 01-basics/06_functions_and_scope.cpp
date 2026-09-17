// ============================================================================
//  06_functions_and_scope.cpp  —— 01-basics 第 6 篇
// ----------------------------------------------------------------------------
//  演示主题：
//    1. 声明与定义分离：为什么需要前置声明、什么时候必须放在头文件里
//    2. 默认参数：必须从右往左连续给；以及「默认参数是静态绑定」的证据
//    3. 传参方式的选择：值 / 引用 / const 引用 / 指针，各自代价
//    4. 返回值的坑：返回局部变量的引用（悬垂引用）与返回局部指针
//    5. 重载决议：精确匹配 > 提升 > 转换；以及 const/引用如何影响重载
//    6. inline 的真实含义（不是「内联展开」，是「允许多处定义」）
//    7. static 的三种身份：文件内链接、函数内静态存储、类的静态成员
//    8. 匿名命名空间：现代 C++ 里替代 static 文件内链接的做法
//    9. 作用域与生命周期：块作用域、初始化顺序、static 局部变量的线程安全初始化
//
//  关键结论：
//    * 默认参数在【调用点】填进去，是静态绑定；虚函数用默认参数会「静态类型决定默认值」。
//    * 传大对象用 const&；要改调用方用 &；小标量按值传最省事。
//    * 千万不要返回局部变量的引用或指针——这是 C++ 最常见的 UB 之一。
//    * 头文件里不要写 using namespace；全局作用域也不要写（教学代码用 std:: 限定）。
// ============================================================================

#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// 1. 声明与定义：下面两行是【声明】（prototype），函数体在 main 之后。
//    编译器按「从上到下、一次一遍」的方式解析，所以在使用点之前必须见过声明。
// ---------------------------------------------------------------------------
int declared_later(int value);

// 带默认参数的声明：默认值只能出现在【声明或定义中的一处】，不能两处都写。
int with_defaults(int a, int b = 10, int c = 20);

// 重载家族（同名不同参数）
std::string describe_value(int value);
std::string describe_value(double value);
std::string describe_value(std::string_view value);

namespace {

void use_utf8_console() {
    static_cast<void>(SetConsoleOutputCP(CP_UTF8));
}

void print_title(const char* text) {
    std::cout << "\n==== " << text << " ====\n";
}

// ---------------------------------------------------------------------------
// static 的第一种身份：文件内链接（internal linkage）。
// 这个函数只有本翻译单元可见，链接器不会拿它去满足别的 .obj 的符号请求。
// 好处：不会和其它文件里的同名函数冲突（比如另一个文件也叫 helper）。
// ---------------------------------------------------------------------------
static int helper_with_internal_linkage() {
    return 1;
}

// ---------------------------------------------------------------------------
// 匿名命名空间：现代 C++ 推荐用它替代 static 函数/变量。
// 效果一样（内部链接），但连类型定义也能放进去，而且对模板/类更友好。
// 【注意】不要重复写 `namespace {` 来「补充」内容——那会开出嵌套的匿名命名空间，
//   和外面的匿名命名空间是两个不同的命名空间，会出现「同名函数两个候选」的二义性错误。
//   正确做法是：一个匿名命名空间，把本文件所有私有实现都放进去。
// ---------------------------------------------------------------------------
int helper_inside_anonymous_namespace() {
    return 2;
}

}  // namespace（上面的内容是本文件私有的、也是 main 用到的工具）

// ---------------------------------------------------------------------------
// 【重要教训】下面这些函数在文件开头已经有了【全局作用域】的声明。
// 如果把它们塞进上面的匿名命名空间，就会变成
//   ::describe_value(...)                  <- 声明
//   `<匿名命名空间>::describe_value(...)`  <- 定义
// 两个不同命名空间里的不同函数，调用点自然就「二义性」了（C2668）。
// 规则：声明和定义必须在【同一个命名空间】里。
// 所以这里把它们留在全局作用域，和开头的声明保持一致。
// ---------------------------------------------------------------------------

// 默认参数在「声明」里给了，定义里就不要再写一遍默认值。
int with_defaults(int a, int b, int c) {
    return a + b + c;
}

int declared_later(int value) {
    return value * 2;
}

// ---------------------------------------------------------------------------
// 重载决议演示：三个同名函数
// ---------------------------------------------------------------------------
std::string describe_value(int value) {
    return "describe(int) -> " + std::to_string(value);
}

std::string describe_value(double value) {
    return "describe(double) -> " + std::to_string(value);
}

std::string describe_value(std::string_view value) {
    return "describe(string_view) -> " + std::string(value);
}

// ---------------------------------------------------------------------------
// 默认参数「静态绑定」的证据：默认参数不是运行期从对象里取的，
// 而是调用点根据【静态类型】在编译期填进去的。
// 这里定义一个基类指针指向派生类对象，通过基类调用虚函数。
// ---------------------------------------------------------------------------
struct Shape {
    virtual ~Shape() = default;
    virtual std::string name(int tag = 0) const {
        return "Shape::name(tag=" + std::to_string(tag) + ")";
    }
};

struct Circle : Shape {
    std::string name(int tag = 99) const override {
        // 注意：派生类这里写的 99 永远不会通过 Shape* 调用生效
        return "Circle::name(tag=" + std::to_string(tag) + ")";
    }
};

// ---------------------------------------------------------------------------
// 悬垂引用演示：返回局部变量的引用（这是 UB，绝不要这么写）
//
// 【编译器会救你】MSVC 在这里给 C4172「returning address of local variable or
//   temporary」。为了在 -WX（警告即错误）下仍能编译并保留这段反面教材，
//   这里就地关掉 C4172，并用注释明确标注这是错的。
//   真实项目里看到 C4172 一定要改代码，而不是关警告。
//   正确改法见下面三个函数：按值返回 / 返回生命周期由调用方保证的引用 / 依赖 RVO。
// ---------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4172)  // 只为保留反面教材
int& return_reference_to_local() {
    int local = 42;
    return local;  // 【错误】local 在函数返回时销毁
}

int* return_pointer_to_local() {
    int local = 7;
    return &local;  // 【错误】同上
}
#pragma warning(pop)

// 正确做法 1：按值返回（小对象、可拷贝的类型）
int return_by_value() {
    int local = 42;
    return local;  // 返回的是拷贝，没问题
}

// 正确做法 2：返回引用——前提是对象活得比函数调用久
// 这里参数是引用，被引用对象由调用方负责活着，所以返回引用是安全的。
const std::string& longer_of(const std::string& a, const std::string& b) {
    return a.size() >= b.size() ? a : b;
}

// 正确做法 3：需要新对象又不想拷贝 -> 返回值 + 依赖 RVO / 移动语义
std::vector<int> make_range(int count) {
    std::vector<int> result;
    result.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        result.push_back(i);
    }
    return result;  // 不会真的拷贝，NRVO/移动语义接管
}

// ---------------------------------------------------------------------------
// 类的静态成员演示
//   声明：static int total;  必须在类外再「定义」一次（分配存储）。
//   C++17 起可以写 inline static int total = 0; 就不用类外定义了。
//   static constexpr 成员（如 kMax）在 C++17 起隐式 inline，无需类外定义。
// ---------------------------------------------------------------------------
struct Counter {
    static int total;                 // 类内声明
    static constexpr int kMax = 100;  // C++17 起隐式 inline
    static int bump() {
        return ++total;
    }
};

int Counter::total = 0;  // 类外定义（只能出现一次，所以放在 .cpp 里）

// ---------------------------------------------------------------------------
// static 的第二种身份：函数内静态局部变量
//   特点：只初始化一次、生命周期到程序结束、C++11 起初始化是线程安全的。
// ---------------------------------------------------------------------------
int next_sequence_number() {
    static int counter = 0;  // 只在这里执行一次初始化
    ++counter;
    return counter;
}

// 对比：普通局部变量每次调用都会重新初始化
int next_local_number() {
    int counter = 0;
    ++counter;
    return counter;
}

int main() {
    use_utf8_console();

    // ======================================================================
    print_title("1. 声明与定义");
    // ======================================================================
    // 声明：告诉编译器「有这么个东西，签名是这样」——不产生代码。
    // 定义：给出函数体 / 分配存储——产生代码或数据。
    // 一个函数可以声明多次，但只能定义一次（ODR，One Definition Rule）。
    // 链接错误 LNK2019「无法解析的外部符号」就是「声明了但没定义」。
    // 另外这里必须给函数调用加括号：<< 的优先级高于 <，但低于函数调用；
    // 真正的原因是 cout << f(x) 里 f(x) 已经是完整的表达式，加括号只是为了对齐书写习惯。
    // 反过来，如果你写 cout << a < b 就会解析成 (cout << a) < b，直接编译报错。
    std::cout << "  declared_later(21) = " << (declared_later(21))
              << "   （定义写在 main 之后，靠前面的声明通过编译）\n";
    // 如果把函数定义放进头文件并被多个 .cpp 包含，就会违反 ODR ->
    // LNK2005「符号已经定义」。解决办法：加 inline，或把定义移到 .cpp。
    std::cout << "  函数可以多次声明、只能一次定义；头文件里放定义必须加 inline\n";

    // ======================================================================
    print_title("2. 默认参数：规则与静态绑定");
    // ======================================================================
    // 规则 1：默认参数必须从右往左连续。
    //         int f(int a, int b = 1, int c = 2);  合法
    //         int f(int a = 1, int b, int c = 2);  非法：b 没有默认值却在 a 右边
    // 规则 2：默认值只能写在一处（通常是声明）。
    // 规则 3：默认参数是在【调用点】填入的，属于静态绑定——
    //         它不看对象的动态类型，只看你调用的那个静态类型。
    std::cout << "  with_defaults(1)       = " << with_defaults(1) << "   （b=10, c=20）\n";
    std::cout << "  with_defaults(1, 2)    = " << with_defaults(1, 2) << "   （c=20）\n";
    std::cout << "  with_defaults(1, 2, 3) = " << with_defaults(1, 2, 3) << '\n';

    // 用函数指针也能看到「默认参数是调用点的语法」：
    // int (*fp)(int, int, int) = &with_defaults;   // 函数指针类型里没有默认值
    // 通过 fp 调用必须给全三个参数——这证明了默认参数不是函数签名的一部分。
    std::cout << "  默认参数不属于函数类型：函数指针 int(*)(int,int,int) 调用时必须给全参数\n";

    // 虚函数 + 默认参数 = 经典陷阱
    Circle circle;
    const Shape& shape_ref = circle;   // 静态类型 Shape&，动态类型 Circle
    std::cout << "  通过 Circle 对象直接调： " << circle.name() << '\n';
    std::cout << "  通过 Shape& 引用调用   ： " << shape_ref.name()
              << "   <- 虚函数是动态绑定（进了 Circle::name），\n";
    std::cout << "                                           但默认参数 tag=0 来自 Shape 的声明（静态绑定）\n";
    std::cout << "  结论：绝不依赖虚函数的默认参数；也不要给虚函数写默认参数\n";

    // ======================================================================
    print_title("3. 传参方式的选择");
    // ======================================================================
    // 决策表：
    //   | 参数类型                         | 推荐写法        | 理由                     |
    //   | 小标量（int/double/指针）        | T               | 拷贝比解引用还便宜        |
    //   | 只读的大对象（string/vector/类） | const T&        | 零拷贝，且防止被改        |
    //   | 需要修改调用方的对象             | T&              | 表达「这是输出参数」      |
    //   | 可能没有值                       | const T* / T*   | 可以用 nullptr 表达缺失   |
    //   | 需要保留一份数据                 | T（按值然后 std::move） | 让调用方选择拷贝还是移动 |
    const std::string long_text(300, 'x');
    auto takes_by_value = [](std::string s) { return s.size(); };
    auto takes_by_const_ref = [](const std::string& s) { return s.size(); };
    std::cout << "  按值传 std::string（300 字符）：发生一次深拷贝，"
              << "takes_by_value -> " << takes_by_value(long_text) << '\n';
    std::cout << "  按 const& 传：零拷贝，takes_by_const_ref -> "
              << takes_by_const_ref(long_text) << '\n';
    // 修改调用方：必须用引用或指针。C++ 没有「出参」语法，靠 & 表达。
    auto bump = [](int& value) { value += 1; };
    int counter_value = 10;
    bump(counter_value);
    std::cout << "  int& 出参：bump(counter_value) 后 counter_value = " << counter_value << '\n';
    // 「按值 + move」的用法：需要在函数内持有一份数据时，让调用方决定拷贝成本
    auto store = [](std::string data) { return data.size(); };
    std::cout << "  按值 + std::move：store(std::move(s)) 时零拷贝，"
              << "store(s) 时一次拷贝，成本由调用方决定 -> " << store(long_text) << '\n';

    // ======================================================================
    print_title("4. 返回值的坑：绝不能返回局部变量的引用/指针");
    // ======================================================================
    // 【为什么会错】函数返回时局部变量（自动存储期）被销毁，
    //   返回的引用/指针指向一块已经失效的内存，后续使用是 UB。
    //   危险之处在于它「经常看起来是对的」——因为那块栈内存暂时还没被覆盖。
    // 【编译器会警告】MSVC 给 C4172「returning address of local variable」。
    //   本文件为了让构建保持零警告，把这两个错误函数写出来但不调用它们。
    std::cout << "  return_reference_to_local / return_pointer_to_local 是反面教材，"
                 "本程序不调用它们\n";
    std::cout << "  MSVC 会对它们报 C4172（returning address of local variable or temporary）\n";
    std::cout << "  正确做法 A（按值返回）：return_by_value() = " << return_by_value() << '\n';
    const std::string first = "short";
    const std::string second = "a longer one";
    const std::string& longest = longer_of(first, second);
    std::cout << "  正确做法 B（返回引用，但对象由调用方保证活着）：longer_of -> \""
              << longest << "\"\n";
    std::cout << "  正确做法 C（返回值 + RVO/移动）：make_range(3).size() = "
              << make_range(3).size() << "，不会产生额外拷贝\n";
    // 由返回引用引申出的另一个坑：把返回的引用绑到 auto（不是 auto&）
    auto copied_result = longer_of(first, second);   // 这一步是一次拷贝，安全但多花钱
    const auto& still_reference = longer_of(first, second);  // 仍然是引用，没有拷贝
    std::cout << "  auto x = f() 会拷贝，auto& x = f() 才是引用；"
                 "对返回引用的函数两者都能编译，但语义不同\n";
    std::cout << "  （copied_result.size()=" << copied_result.size()
              << " still_reference.size()=" << still_reference.size() << "）\n";

    // ======================================================================
    print_title("5. 重载决议：精确匹配优先");
    // ======================================================================
    // 顺序大致是：
    //   1) 精确匹配（含数组到指针、函数到指针、限定转换）
    //   2) 提升（char/short -> int、float -> double …）
    //   3) 标准转换（int <-> double、派生类 -> 基类指针 …）
    //   4) 用户定义转换（构造函数/转换运算符）
    // 如果有多个候选「一样好」，就是二义性错误（C2668 ambiguous call）。
    std::cout << "  " << describe_value(42) << '\n';              // int 精确匹配
    std::cout << "  " << describe_value(3.14) << '\n';            // double 精确匹配
    std::cout << "  " << describe_value("literal") << '\n';       // 字符串字面量 -> string_view
    std::cout << "  " << describe_value('A') << '\n';             // char 会提升成 int
    std::cout << "  " << describe_value(1.0f) << '\n';            // float 会提升成 double
    std::cout << "  注意 describe_value('A') 选的是 int 版本：char -> int 属于「提升」，"
                 "优先级高于任何标准转换\n";
    // 传 nullptr 会选中指针重载；传 NULL/0 可能选中 int 重载（见 04 篇）
    std::cout << "  重载里最容易踩的坑：字面量 0 / NULL 会被当成 int 而不是指针，用 nullptr\n";

    // ======================================================================
    print_title("6. inline 的真实含义");
    // ======================================================================
    // 【错误直觉】「inline 就是让编译器把函数体展开，加快速度」。
    // 【正确模型】inline 的关键语义是【允许多个翻译单元里出现同名定义】，
    //   链接器会把它们合并成一个。它是为了解决「头文件里写函数定义」的 ODR 问题。
    //   「展开成机器码」只是编译器的一个优化选项，现代编译器对没写 inline 的
    //   短函数也会自动内联，而且还可以用 __forceinline / [[gnu::always_inline]] 强制。
    // 结论：不要为了性能写 inline；为了「头文件里能放定义」才写 inline。
    // 类内部直接实现的成员函数隐式就是 inline。
    auto small_sum = [](int x, int y) { return x + y; };  // lambda 的 operator() 也是隐式 inline
    std::cout << "  inline 的语义是「允许多处定义」，不是「一定展开」：small_sum(1,2) = "
              << small_sum(1, 2) << '\n';
    std::cout << "  类内定义的成员函数隐式 inline；头文件里的自由函数必须显式写 inline\n";

    // ======================================================================
    print_title("7. static 的三种身份");
    // ======================================================================
    // 【同一个关键字，三种完全不同的含义】这是 C++ 里最容易混淆的关键字之一。
    //   (1) 命名空间作用域的 static 函数/变量 -> 内部链接（仅本 .cpp 可见）
    //   (2) 函数内的 static 局部变量          -> 静态存储期，只初始化一次
    //   (3) 类的 static 成员                  -> 属于类而不属于对象
    std::cout << "  (1) static 自由函数：helper_with_internal_linkage() = "
              << helper_with_internal_linkage()
              << "，匿名命名空间里的同名功能 = " << helper_inside_anonymous_namespace() << '\n';
    std::cout << "      C++ 推荐用匿名命名空间替代 static 自由函数（名字更清晰、也能放类型定义）\n";
    std::cout << "  (2) static 局部变量：连续调用三次 -> " << next_sequence_number() << ' '
              << next_sequence_number() << ' ' << next_sequence_number() << '\n';
    std::cout << "      普通局部变量对比：     连续调用三次 -> " << next_local_number() << ' '
              << next_local_number() << ' ' << next_local_number() << '\n';
    // 类的 static 成员用一个结构体演示（Counter 定义在本文件顶部）
    std::cout << "  (3) 类静态成员：Counter::bump() 三次 -> " << Counter::bump() << ' '
              << Counter::bump() << ' ' << Counter::bump()
              << "，Counter::total = " << Counter::total
              << "，kMax = " << Counter::kMax << '\n';
    // static 局部变量的初始化在 C++11 起是线程安全的（magic static）
    std::cout << "      static 局部变量的首次初始化由编译器插入同步代码，多线程下是安全的\n";
    std::cout << "      但初始化之后的读写仍需你自己加锁\n";

    // ======================================================================
    print_title("8. 作用域与生命周期");
    // ======================================================================
    // 作用域（scope）：名字在哪里可见 —— 编译期概念（块、函数、类、命名空间）。
    // 生命周期（lifetime）：对象什么时候存在 —— 运行期概念。
    // 两者不是一回事：static 局部变量作用域是块，生命周期是整个程序。
    {
        const int block_scope = 1;
        std::cout << "  块作用域：block_scope 出了这对大括号就不可见（值 " << block_scope << "）\n";
    }
    for (int i = 0; i < 1; ++i) {
        std::cout << "  for 的循环变量 i 只在循环体内可见（值 " << i << "）\n";
    }
    // 名字查找顺序：先内层，再外层；用 :: 可以显式指定全局作用域。
    // 【真实踩坑记录】给文件内的辅助函数起名 title 会与本文件里用到的
    //   <string> 中的 std::title（C++ 标准库确实有这个面向本地化的函数）发生
    //   名字冲突：using 之外的名字查找 + ADL 会让调用变成二义性。
    //   教训：自己的辅助函数要用不常见的前缀（本仓库用 print_ / show_ / demo_），
    //         并且不要与标准库的常见名字（title/data/size/left/right）重名。
    const int shadowed = 100;
    {
#pragma warning(push)
#pragma warning(disable : 4456)  // 这里就是故意演示遮蔽，所以关掉这条警告
        const int shadowed = 200;  // 内层遮蔽外层（shadowing）
        std::cout << "  变量遮蔽：内层 shadowed = " << shadowed
                  << "，外层仍是 100（这里能看到内层的 200）\n";
#pragma warning(pop)
    }
    std::cout << "  出了块之后 shadowed = " << shadowed << "（外层那个）\n";
    std::cout << "  建议：不要用遮蔽，编译器 /W4 会对部分情况给 C4456/C4457/C4458\n";

    // 临时对象的生命周期：绑定到 const 引用 / 右值引用时会被延长
    const std::string& temp_extended = std::string("temporary");  // 生命周期延长到引用作用域结束
    std::cout << "  const& 绑定临时对象会延长其寿命：temp_extended = " << temp_extended << '\n';
    // 但用它初始化「有名字的引用成员」不延长，这是类设计里的经典坑（见 02 章会细讲）
    std::cout << "  注意：临时对象寿命延长只对「直接绑定」生效，"
                 "对从函数返回的引用、对聚合初始化里的引用成员都不生效\n";

    // 初始化顺序：同一作用域内按声明顺序初始化；不同翻译单元之间的
    // 全局对象初始化顺序不确定（静态初始化顺序问题）。
    std::cout << "  全局对象在不同 .cpp 之间的初始化顺序是未指定的，"
                 "跨文件依赖全局对象构造是危险的\n";

    std::cout << "\n[06] 结束。下一步：07_memory_model.cpp\n";
    return 0;
}
