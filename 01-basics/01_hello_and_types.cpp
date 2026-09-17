// ============================================================================
//  01_hello_and_types.cpp  —— 01-basics 第 1 篇
// ----------------------------------------------------------------------------
//  演示主题：
//    1. 一个 C++ 程序的最小结构：预处理 -> 编译 -> 汇编 -> 链接
//    2. 头文件该放在哪里；#define NOMINMAX 与 #include <windows.h> 的顺序陷阱
//    3. 基本类型与 sizeof（Windows LLP64 下 long 是 4 字节，这个反直觉）
//    4. 整型提升与常用算术转换（unsigned int + long 到底得到什么类型）
//    5. 字面量与后缀（3.14 是 double，3.14f 才是 float）
//    6. const / constexpr / auto 的取舍
//    7. 标识符命名规则，以及「下划线开头」为什么不能随便用
//
//  关键结论（先记住这几条，细节看下面的代码和输出）：
//    * NOMINMAX 必须在 #include <windows.h> 之前；否则 windows.h 会定义
//      min/max 宏，把 std::min/std::max 顶掉。
//    * sizeof 返回 std::size_t，打印用 %zu；不要用 %d。
//    * 字符常量：C 里 'a' 的类型是 int（4 字节），C++ 里是 char（1 字节）。
//      「char 变量恒为 1 字节」在两个语言里都成立。
//    * 大括号初始化禁止窄化转换，是编译期的第一道防线。
//    * 用户代码不要以下划线开头命名；_Xxx 和 __xxx 是保留给实现的。
//
//  编译（仓库根目录 cpp-notes 下执行）：
//    .\build.ps1 -Chapter 01-basics -WX
// ============================================================================

// ---- NOMINMAX 必须出现在 windows.h 之前（本文件用后置 #define 演示反例）----
#include <iostream>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <climits>
#include <string>
#include <type_traits>
#include <algorithm>
#include <windows.h>

// 反例演示：正确写法是把这行放到 #include <windows.h> 的上面。
// windows.h 有 include guard，第二次包含不会再展开，所以这里定义已经晚了，
// min/max 宏（如果存在）已经注入。下面用 (std::min) 加括号的方式说明影响。
#define NOMINMAX

namespace {

// 统一把控制台输出代码页切成 UTF-8，否则源代码里的中文在 GBK 控制台会变乱码。
void use_utf8_console() {
    if (SetConsoleOutputCP(CP_UTF8) == 0) {
        std::fprintf(stderr, "[warn] 切换到 UTF-8 输出代码页失败，中文可能显示为乱码\n");
    }
}

void title(const char* text) {
    std::cout << "\n==== " << text << " ====\n";
}

// ---- 用编译期断言把「类型推导」这类结论钉死，比运行期打印更可靠 ----------------
static_assert(sizeof(char) == 1, "char 恒为 1 字节（C++ 标准规定 sizeof(char) == 1）");
static_assert(sizeof(long) == 4, "Windows LLP64：long 是 4 字节，和 int 一样宽");
static_assert(sizeof(long long) == 8, "long long 恒为 8 字节");
static_assert(sizeof(void*) == 8, "本仓库按 x64 编译，指针 8 字节");

}  // namespace

int main() {
    use_utf8_console();

    // ======================================================================
    title("1. 程序结构与编译流程");
    // ======================================================================
    // 四个阶段（build.ps1 里 cl.exe 一次调用会全部帮你跑完）：
    //   预处理 .cpp -> .i   展开 #include / #define / #if，删除注释
    //   编译   .i   -> .asm 语法分析、类型检查、优化（static_assert 发生在这里）
    //   汇编   .asm -> .obj 生成机器码 + 符号表
    //   链接   .obj -> .exe 拼上 CRT 与其它目标文件，解析外部符号（LNK2019 出在这里）
    // 这些阶段用预定义宏可以直接观察到（详见 08_build_and_debug.cpp）。
    std::cout << "  本文件路径 __FILE__      : " << __FILE__ << '\n';
    std::cout << "  当前行号 __LINE__        : " << __LINE__ << '\n';
    std::cout << "  编译日期 __DATE__        : " << __DATE__ << '\n';
    std::cout << "  __cplusplus              : " << __cplusplus
              << "  （注意：MSVC 默认恒为 199711L，build.ps1 用 /Zc:__cplusplus 修正）\n";
    std::cout << "  _MSVC_LANG               : " << _MSVC_LANG
              << "  （MSVC 自己报的真实语言标准，202002 即 C++20）\n";
    std::cout << "  _MSC_VER                 : " << _MSC_VER << '\n';

    // ======================================================================
    title("2. 头文件顺序陷阱：NOMINMAX 与 windows.h");
    // ======================================================================
    // 【错误直觉】「#define 是给编译器看的，放在文件哪一行都行」。
    // 【正确模型】预处理是从上到下的一次性文本替换。windows.h 里是这样的逻辑：
    //               #ifndef NOMINMAX
    //               #define min(a,b) ...
    //               #define max(a,b) ...
    //               #endif
    //            所以只要 #define NOMINMAX 出现在 #include <windows.h> 之后，
    //            宏就已经被定义了（include guard 保证 windows.h 不会再看第二遍）。
    // 【影响】min/max 变成宏后，任何叫 min/max 的东西都会被替换掉，
    //        <algorithm> 里的 std::min / std::max 直接编译不过。
    // 【工程建议】三条任选其一，推荐第 1 条：
    //        1) 项目级定义：编译时加 /DNOMINMAX
    //        2) 把 windows.h 包进一个自己的头，在它上面写 #define NOMINMAX
    //        3) 加括号 (std::min)(a, b)，绕过函数式宏——能用但很难看
    int lhs = 3;
    int rhs = 5;
    std::cout << "  (std::min)(3,5) = " << (std::min)(lhs, rhs)
              << "   —— 加括号是为了让函数式宏 min(a,b) 无法匹配\n";

    // ======================================================================
    title("3. 基本类型与 sizeof");
    // ======================================================================
    // 【重点】sizeof 的结果是 std::size_t（无符号 64 位），printf 必须配 %zu。
    //        写成 %d 会触发 C4477（/W4）甚至运行期读到垃圾值。
    std::cout << "  sizeof(bool)        = " << sizeof(bool) << '\n';
    std::cout << "  sizeof(char)        = " << sizeof(char) << "   <- 恒为 1，标准规定\n";
    std::cout << "  sizeof(short)       = " << sizeof(short) << '\n';
    std::cout << "  sizeof(int)         = " << sizeof(int) << '\n';
    std::cout << "  sizeof(long)        = " << sizeof(long)
              << "   <- Windows 上等于 4，Linux LP64 上是 8，跨平台代码别依赖它\n";
    std::cout << "  sizeof(long long)   = " << sizeof(long long) << '\n';
    std::cout << "  sizeof(float)       = " << sizeof(float) << '\n';
    std::cout << "  sizeof(double)      = " << sizeof(double) << '\n';
    std::cout << "  sizeof(long double) = " << sizeof(long double) << "   <- MSVC 上和 double 同宽\n";
    std::cout << "  sizeof(void*)       = " << sizeof(void*) << '\n';
    std::cout << "  sizeof(std::size_t) = " << sizeof(std::size_t) << '\n';

    // 用 <cstdint> 里的定宽类型写「必须 4 字节」的代码，而不是靠 int 的巧合。
    static_assert(sizeof(std::int32_t) == 4 && sizeof(std::uint64_t) == 8);
    std::cout << "  std::int8_t 范围: [" << static_cast<int>(INT8_MIN) << ", "
              << static_cast<int>(INT8_MAX) << "]\n";
    std::cout << "  std::int32_t 范围: [" << INT32_MIN << ", " << INT32_MAX
              << "]\n";  // INT32_MIN/MAX 已经是 int，无需强转

    // ---- 字符常量 vs 字符变量：原笔记最大的类型误解就出在这里 ----------------
    // 【原笔记】「char 在 c 编译器中是 4 字节，char 在 cpp 编译器中是 1 字节」。
    // 【问题】把两件不同的事混成了一件，而且结论只对了一半：
    //   * char 变量：C 和 C++ 里都恒为 1 字节，标准规定 sizeof(char) == 1。
    //   * 字符常量 'a'：C 语言里它的类型是 int（所以 sizeof('a') == 4）；
    //                    C++ 语言里它的类型是 char（所以 sizeof('a') == 1）。
    //   原笔记想说的大概是「C 里字符常量占 4 字节」，但把它说成了 char 本身，
    //   于是得出了「char 在 C 里是 4 字节」这个错误结论。
    // 【实测证据】在 C++ 编译器下：
    static_assert(sizeof(char) == 1, "char 变量恒为 1 字节");
    static_assert(sizeof('a') == 1, "C++ 中字符常量的类型是 char，占 1 字节");
    static_assert(std::is_same_v<decltype('a'), char>, "C++ 里 'a' 就是 char");
    // 如果这段代码放到 .c 文件里用 C 编译器编译，sizeof('a') 会变成 4、
    // 类型会变成 int——注意本仓库 build.ps1 是 C++ 构建，不会有这个问题。
    std::cout << "  sizeof('a')  = " << sizeof('a') << "  (C++ 里 'a' 是 char)\n";
    std::cout << "  sizeof(char) = " << sizeof(char) << "  (char 变量)\n";
    std::cout << "  注意：若把这段代码放进 .c 文件用 C 编译器编译，sizeof('a') 会是 4\n";
    char narrow = 'a';
    std::cout << "  char 变量 narrow 的字节数 = " << sizeof(narrow) << '\n';

    // ---- sizeof 是编译期算的，不求值 ----------------------------------------
    int side_effect = 0;
    std::size_t n = sizeof(++side_effect);  // ++side_effect 根本不会执行
    std::cout << "  sizeof(++x) 之后 x = " << side_effect
              << "（sizeof 的操作数不求值）, n = " << n << '\n';

    // ======================================================================
    title("4. 整型提升与常用算术转换");
    // ======================================================================
    // 规则分两步走，顺序不能颠倒：
    //   第 1 步【整型提升】：bool/char/signed char/unsigned char/short 等
    //           rank 低于 int 的类型，若 int 能装下它的全部值就提升为 int，
    //           否则提升为 unsigned int。这一步只看「比 int 低」的类型。
    //   第 2 步【常用算术转换】：
    //           a) 任一操作数为 long double / double / float -> 另一个也转过去
    //           b) 否则做整型提升，然后按 rank 比较：
    //              - 无符号类型 rank >= 有符号类型 rank -> 有符号的转成无符号的
    //              - 否则若「有符号类型能表示无符号类型的全部值」-> 无符号的转成有符号的
    //              - 否则 -> 两者都转成「有符号类型对应的无符号类型」
    char c1 = 'A';
    short s1 = 1;
    auto promoted = static_cast<int>(c1) + static_cast<int>(s1);
    static_assert(std::is_same_v<decltype(c1 + s1), int>,
                  "char + short 的结果是 int：整型提升的典型效果");
    std::cout << "  decltype(char + short) 是 int，值 = " << promoted << '\n';

    unsigned int u1 = 110;
    long l1 = 0;
    static_assert(std::is_same_v<decltype(u1 + l1), unsigned long>,
                  "unsigned int + long：Windows 下 long 装不下 unsigned int 的全部值，"
                  "所以两者都转 unsigned long");
    auto mixed = u1 + l1;
    std::cout << "  decltype(unsigned int + long) = unsigned long，值 = " << mixed
              << "（Windows LLP64 上 long 与 unsigned int 同宽，装不下对方的负半区）\n";

    // 反例：如果混合的是 long long（8 字节，能装下 unsigned int 的全部值），
    // 结果就变成有符号的 long long——同一个 unsigned，结果类型却完全相反。
    long long ll1 = 0;
    static_assert(std::is_same_v<decltype(u1 + ll1), long long>,
                  "unsigned int + long long：long long 装得下 unsigned int，故转 long long");
    std::cout << "  decltype(unsigned int + long long) = long long，值 = " << (u1 + ll1) << '\n';

    // 最经典的同 rank 陷阱：int 与 unsigned int 相遇 -> 有符号的也变无符号。
    int negative = -1;
    unsigned int one = 1;
    static_assert(std::is_same_v<decltype(negative + one), unsigned int>);
    std::cout << "  (-1) + 1u == 0u ? " << std::boolalpha << (negative + one == 0u)
              << "   因为 -1 被解释为 4294967295u，再加 1 就回绕成 0\n";
    // 有符号/无符号比较是 C++ 里最著名的坑之一。MSVC 在 /W4 下会报 C4018
    // （signed/unsigned mismatch）。这里先把警告关掉，让你看见「不做任何处理时
    // 表达式本身算出来是什么」；生产代码应该改写成显式强转，见下面的 case C。
#pragma warning(push)
#pragma warning(disable : 4018)  // 仅为了演示，正常代码不要关这个警告
    std::cout << "  -1 < 1u 的原始结果是 " << (negative < one)
              << "   <- 有符号被转成无符号后 -1 变成了 4294967295，所以结果为假\n";
#pragma warning(pop)
    // 正确写法：把比较双方统一到同一符号性上，让意图写在代码里。
    // 这里我们故意「按无符号解释 -1」，得到和上面完全一致的结果。
    std::cout << "  static_cast<unsigned>(-1) < 1u 的结果是 "
              << (static_cast<unsigned int>(negative) < one)
              << "   这就是上面那一行的真实语义\n";
    std::cout << "  如果本意是「按数学值比较」，则要写成 "
              << (negative < static_cast<int>(one))
              << "（把无符号一方转成有符号）\n";
    std::cout << std::noboolalpha;

    // ---- 浮点方向的提升：float 与整数运算，结果是 float --------------------
    // 【原笔记】「float 中间参与计算时是 double计算结果」。这个说法是错的：
    // 常用算术转换的规则是「整型操作数转成浮点操作数的类型」，
    // 所以 float + int 得到的是 float，不是 double。
    // 真正提升为 double 的场景有两个：
    //   (1) float 与 double / long double 混合运算，按「更宽的那个」走；
    //   (2) C 语言遗留的「默认实参提升」：可变参数函数的 float 实参会被提升成
    //       double，这就是 printf 的 %f 能收 float 的原因（也解释了为什么
    //       printf 里没有 %hf 这种东西）。
    float f1 = 0.5f;
    static_assert(std::is_same_v<decltype(f1 + 1), float>,
                  "float + int 的结果是 float：整型被转成 float，而不是 float 被提升成 double");
    static_assert(std::is_same_v<decltype(f1 + 1.0), double>,
                  "float + double 才是 double：看的是另一个操作数");
    std::cout << "  decltype(float + int)    = float，值 = " << (f1 + 1) << '\n';
    std::cout << "  decltype(float + double) = double，值 = " << (f1 + 1.0) << '\n';
    std::cout << "  printf 的 %%f 能收 float，是因为可变参数有默认实参提升：float -> double\n";

    // ======================================================================
    title("5. 字面量与后缀");
    // ======================================================================
    // 【重点】3.14 是 double，不是 float。给 float 变量直接赋 3.14 会做一次
    //        隐式窄化，MSVC /W4 会报 C4244/C4305。写 3.14f。
    double d_lit = 3.14;      // 无后缀浮点字面量 = double
    float f_lit = 3.14f;      // f/F 后缀 = float
    long double ld_lit = 3.14L;  // L 后缀 = long double
    std::cout << std::boolalpha;
    std::cout << "  std::is_same_v<decltype(3.14), double>  = "
              << std::is_same_v<decltype(3.14), double> << '\n';
    std::cout << "  std::is_same_v<decltype(3.14f), float>  = "
              << std::is_same_v<decltype(3.14f), float> << '\n';
    std::cout << "  std::is_same_v<decltype(42), int>       = "
              << std::is_same_v<decltype(42), int> << '\n';
    std::cout << "  std::is_same_v<decltype(42u), unsigned int> = "
              << std::is_same_v<decltype(42u), unsigned int> << '\n';
    std::cout << "  std::is_same_v<decltype(42L), long>     = "
              << std::is_same_v<decltype(42L), long> << '\n';
    std::cout << "  std::is_same_v<decltype(42LL), long long> = "
              << std::is_same_v<decltype(42LL), long long> << '\n';
    std::cout << std::noboolalpha;
    std::cout << "  d_lit=" << d_lit << " f_lit=" << f_lit << " ld_lit=" << ld_lit << '\n';

    // 进制前缀与数字分隔符
    int hex_lit = 0xFF;        // 0x / 0X 十六进制
    int oct_lit = 0777;        // 前导 0 是八进制！010 等于 8，不是 10
    int bin_lit = 0b1010'1010;  // C++14 起支持 0b 二进制与 ' 数字分隔符
    std::cout << "  0xFF=" << hex_lit << "  0777=" << oct_lit
              << "  0b1010'1010=" << bin_lit << "  注意前导 0 是八进制，不是十进制\n";

    // ======================================================================
    title("6. const / constexpr / auto 的取舍");
    // ======================================================================
    // const      : 运行期常量，值只在运行时确定；不能用于数组长度、模板实参。
    // constexpr  : 编译期常量（若可能），可用于数组长度、模板实参、static_assert。
    // auto       : 类型推导，省字但会把 const/引用丢掉的场景要当心（见下）。
    // 工程建议：能用 constexpr 就别用 const；接口不变式优先 constexpr。
    const int runtime_const = static_cast<int>(std::string("abc").size());  // 运行期才知道
    constexpr int compile_const = 4;
    int buffer[compile_const];          // 只有编译期常量能当数组长度
    static_assert(compile_const * 2 == 8, "constexpr 可以进 static_assert");
    buffer[0] = 1;
    std::cout << "  const runtime_const 只能运行时用；constexpr compile_const 进了数组长度和 static_assert\n";
    std::cout << "  buffer[0]=" << buffer[0] << " runtime_const=" << runtime_const << '\n';

    // auto 的推导规则：按值推导会丢掉顶层 const 和引用。
    const int ci = 7;
    const int& cri = ci;
    auto a1 = ci;    // int      —— 顶层 const 被丢掉
    auto a2 = cri;   // int      —— 引用和 const 都被丢掉（发生一次拷贝）
    auto& a3 = ci;   // const int& —— 显式写 & 才保留绑定，且 const 自动带上
    static_assert(std::is_same_v<decltype(a1), int>);
    static_assert(std::is_same_v<decltype(a2), int>);
    static_assert(std::is_same_v<decltype(a3), const int&>);
    std::cout << "  auto 按值推导丢 const 和引用：a1/a2 是 int，a3 加了 & 才是 const int&\n";

    // auto 与初始化列表：C++17 起 auto x{1} 是 int；C++20 起可用 auto 做函数参数。
    auto deduced_from_brace = 42;   // int
    static_assert(std::is_same_v<decltype(deduced_from_brace), int>);
    std::cout << "  auto x = 42 推导为 int\n";

    // ======================================================================
    title("7. 标识符命名规则与「保留标识符」");
    // ======================================================================
    // 硬性规则：只能由字母、数字、下划线组成；不能以数字开头；区分大小写；
    //          不能是关键字；不能是「保留给实现的标识符」。
    // 保留标识符（用户代码碰了就是未定义行为，别指望编译器报错）：
    //   * 任何位置：含双下划线的名字 __x
    //   * 任何位置：下划线 + 大写字母开头的名字 _Xxx
    //   * 全局命名空间：下划线开头的名字 _xxx
    // 所以原笔记里的 int _a[10]; 属于「全局下划线开头」这一类，在函数内虽然
    // 合法，但习惯上应该改名为 a_uninitialized 之类，避免踩到实现保留区。
    int a_uninitialized[10];                 // 原笔记是 int _a[10];
    int a_zero_filled[10]{};                 // 空大括号 = 值初始化，全 0
    int a_also_zero[10] = {};                // 等号 + 空大括号，效果相同
    std::cout << "  int a[10];   第一个元素 = " << a_uninitialized[0]
              << "（未初始化，值不确定，读它是 UB，这里只是演示）\n";
    std::cout << "  int a[10]{}; 第一个元素 = " << a_zero_filled[0]
              << "（值初始化，全部为 0）\n";
    std::cout << "  int a[10] = {}; 同样全 0，第一个元素 = " << a_also_zero[0] << '\n';

    // ======================================================================
    title("8. 预告：本文件的 printf 只用字面量做格式串");
    // ======================================================================
    // 下面这句是安全的：格式串是字面量，编译器能静态检查参数类型。
    std::printf("  printf(\"%%zu\\n\", sizeof(int)) -> %zu\n", sizeof(int));
    // 反例（格式串漏洞）：
    //     const char* user = "%s%s%s%s";
    //     printf(user);          // 用户数据变成了格式串，可以读栈、写内存
    // 正确写法永远是 printf("%s", user);
    // 详见 05_string_and_io.cpp 的完整讲解。

    std::cout << "\n[01] 结束。下一步：02_integer_and_float.cpp\n";
    return 0;
}
