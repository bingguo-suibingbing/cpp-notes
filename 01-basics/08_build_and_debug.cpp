// ============================================================================
//  08_build_and_debug.cpp  —— 01-basics 第 8 篇
// ----------------------------------------------------------------------------
//  演示主题：
//    1. 一个 .cpp 到 .exe 的四个阶段，以及哪些工作发生在哪个阶段
//    2. 用预定义宏「看见」预处理与编译阶段（__FILE__ / __LINE__ / __DATE__ …）
//    3. static_assert：编译期断言（本题库大量使用）
//    4. assert 与 NDEBUG：运行期断言，Debug 开、Release 关
//    5. #pragma once 与 include guard：头文件防重复包含
//    6. 手写日志宏：__VA_ARGS__、do{}while(0)、Release 下零开销
//    7. #error 与条件编译：让不支持的平台/配置直接编译失败
//
//  关键结论：
//    * 语法错误发生在「编译」阶段，找不到符号发生在「链接」阶段（LNK2019）。
//    * 能在编译期证明的事情就用 static_assert，别拖到运行期。
//    * assert 是开发期工具，不是错误处理手段；发布版本里它会被编译掉。
//    * 宏要用 do { } while (0) 包起来，否则在 if/else 里会断裂。
// ============================================================================

#define NOMINMAX
#include <windows.h>

#include <array>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

void use_utf8_console() {
    static_cast<void>(SetConsoleOutputCP(CP_UTF8));
}

void print_title(const char* text) {
    std::cout << "\n==== " << text << " ====\n";
}

// ---------------------------------------------------------------------------
// 编译期断言：不满足就【编译失败】，一行运行期代码都不会生成。
// static_assert 从 C++17 起可以只写一个参数（消息可省略）。
// ---------------------------------------------------------------------------
static_assert(sizeof(int) == 4, "本仓库假定 int 是 4 字节");
static_assert(CHAR_BIT == 8, "本仓库假定 1 字节 = 8 位");
static_assert(__cplusplus >= 202002L || _MSVC_LANG >= 202002L,
              "本题库要求 C++20；MSVC 下请加 /Zc:__cplusplus，否则 __cplusplus 恒为 199711L");

// 用 constexpr 函数做编译期计算，再用 static_assert 锁住结果
constexpr int factorial(int n) {
    return n <= 1 ? 1 : n * factorial(n - 1);
}
static_assert(factorial(5) == 120, "阶乘在编译期算好了");

// 模板参数约束也能用 static_assert 表达（C++20 更推荐 concepts，见 04 章）
template <typename T>
constexpr bool is_small_pod() {
    static_assert(std::is_trivially_copyable_v<T>, "只接受可平凡拷贝的类型");
    return sizeof(T) <= 8 && std::is_trivially_copyable_v<T>;
}
static_assert(is_small_pod<int>());
static_assert(is_small_pod<double>());

// ---------------------------------------------------------------------------
// 手写日志宏
//   * do { ... } while (0) 是为了让它像一条语句：if (x) LOG("a"); else LOG("b");
//     如果宏展开成多语句块，else 就会挂到错误的分支上（dangling else）。
//   * ##__VA_ARGS__ 是 GCC/MSVC 的扩展，允许「没有可变参数时去掉前面的逗号」。
//     标准做法是 __VA_OPT__（C++20）。
//   * 用 (void)sizeof(...) 之类的技巧可以在 Release 下让参数「被使用但不求值」，
//     从而不产生「未使用变量」警告——不过 __VA_ARGS__ 本身在 MSVC 下已经处理得不错。
// ---------------------------------------------------------------------------
#ifdef NDEBUG
#define LOG_INFO(...) ((void)0)  // Release：直接展开成空语句，零运行时开销
#else
#define LOG_INFO(...)                                    \
    do {                                                 \
        std::printf("[LOG] %s:%d ", __FILE__, __LINE__); \
        std::printf(__VA_ARGS__);                        \
        std::printf("\n");                               \
    } while (0)
#endif

// 带可变参数的调试打印（用 __VA_OPT__ 处理「无附加参数」的情况，C++20）
#define DEBUG_VALUE(expr) \
    std::cout << "    " #expr " = " << (expr) << "   [" << __FILE__ << ":" << __LINE__ << "]\n"

// 把表达式变成字符串再打印：注意展开出来的必须是合法表达式
#define SHOW_EXPR(expr) std::cout << "  " #expr "  ->  " << (expr) << '\n'

}  // namespace

int main() {
    use_utf8_console();

    // ======================================================================
    print_title("1. 四个阶段，以及每阶段能发现什么问题");
    // ======================================================================
    // | 阶段   | 输入 -> 输出      | 做什么                             | 典型错误        |
    // | 预处理 | .cpp -> .i        | 展开 #include/#define/#if，删注释  | 宏/头文件缺失   |
    // | 编译   | .i -> .asm/.obj   | 语法语义分析、类型检查、优化       | C2xxx 语法/类型 |
    // | 汇编   | .asm -> .obj      | 生成机器码与符号表                 | 极少见          |
    // | 链接   | .obj + 库 -> .exe | 解析外部符号、合并段               | LNK2019/LNK2005 |
    // 三种「找不到名字」的错误分别属于不同阶段，搞清楚这一点能省很多时间：
    //   * 忘记 #include  -> 编译期 C2065「未声明的标识符」
    //   * 忘记定义函数    -> 链接期 LNK2019「无法解析的外部符号」
    //   * 头文件里定义了函数被多个 .cpp 包含 -> 链接期 LNK2005「符号已定义」
    std::cout << "  编译期错误 = 语法/类型问题（C 开头）；链接期错误 = 符号问题（LNK 开头）\n";
    std::cout << "  本题库的构建命令（build.ps1）等价于一行 cl.exe，四个阶段一次跑完：\n";
    std::cout << "    cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /permissive- /Zc:__cplusplus ...\n";

    // ======================================================================
    print_title("2. 预定义宏：把「编译时刻」的信息带进程序");
    // ======================================================================
    std::cout << "  __FILE__       = " << __FILE__ << '\n';
    std::cout << "  __LINE__       = " << __LINE__ << "（就是这一行的行号）\n";
    std::cout << "  __DATE__       = " << __DATE__ << '\n';
    std::cout << "  __TIME__       = " << __TIME__ << '\n';
    std::cout << "  __func__       = " << __func__ << "（C++11 起，函数内的局部字符数组）\n";
    std::cout << "  __cplusplus    = " << __cplusplus << '\n';
    std::cout << "  _MSVC_LANG     = " << _MSVC_LANG
              << "   <- MSVC 真实的语言标准（199711 是它的历史包袱）\n";
    std::cout << "  _MSC_VER       = " << _MSC_VER << '\n';
#if defined(_DEBUG)
    std::cout << "  _DEBUG 已定义：当前是 Debug 构建（/MDd，assert 生效）\n";
#else
    std::cout << "  _DEBUG 未定义：当前是 Release 构建\n";
#endif
#ifdef NDEBUG
    std::cout << "  NDEBUG 已定义：assert 被编译掉（Release 的标准做法）\n";
#else
    std::cout << "  NDEBUG 未定义：assert 生效（Debug 构建的默认行为）\n";
#endif
    std::cout << "  工程技巧：把 __DATE__/__TIME__ 打进启动日志，"
                 "排查「用户装的到底是哪个版本」非常有用\n";
    std::cout << "  注意：__DATE__/__TIME__ 会让构建结果不可复现，"
                 "对可重现构建（reproducible build）有要求的项目要关掉\n";

    // ======================================================================
    print_title("3. static_assert：把能编译期证明的结论钉死");
    // ======================================================================
    constexpr int compiled_result = factorial(5);
    std::cout << "  factorial(5) = " << compiled_result
              << "（编译期就算完了，运行期没有任何循环）\n";
    std::cout << "  static_assert 的好处：错误在编译期暴露，且不产生任何运行期代码\n";
    std::cout << "  实战用法：锁住结构体大小、锁住枚举值、锁住平台假设\n";
    // 编译期检查「这个类型是否可平凡拷贝」，从而决定能否 memcpy
    struct Pod { int a; int b; };
    struct NotPod { std::string s; };
    std::cout << "  is_trivially_copyable_v<Pod>    = " << std::boolalpha
              << std::is_trivially_copyable_v<Pod> << "（可以 memcpy）\n";
    std::cout << "  is_trivially_copyable_v<NotPod> = "
              << std::is_trivially_copyable_v<NotPod> << "（不许 memcpy）\n";
    std::cout << std::noboolalpha;
    static_assert(std::is_trivially_copyable_v<Pod>);
    static_assert(!std::is_trivially_copyable_v<NotPod>);

    // ======================================================================
    print_title("4. assert 与 NDEBUG");
    // ======================================================================
    // assert(条件)：条件为假时打印「表达式、文件、行号」并调用 abort()。
    // 用途：检查【程序内部的逻辑不变式】（本该永远为真的东西），
    //      例如「这个指针不可能为空」「这个索引一定在范围内」。
    // 不适合：检查用户输入、网络数据、文件内容——因为发布版里 assert 会被删掉，
    //        那些检查会凭空消失。外部数据要用「返回错误码 / 抛异常 / 日志」。
    auto checked_divide = [](int numerator, int denominator) {
        // 这是【内部不变式】：调用方保证了分母非零
        assert(denominator != 0 && "check_divide 的调用方必须保证分母非零");
        return numerator / denominator;
    };
    std::cout << "  checked_divide(10, 2) = " << checked_divide(10, 2) << '\n';
    std::cout << "  assert 的消息用 && \"...\" 拼：assert(x && \"说明\")，"
                 "这样消息会出现在失败输出里\n";
    std::cout << "  发布版（/DNDEBUG）里 assert 完全消失，"
                 "所以绝不能用它做「有副作用的检查」，也不要写 assert(f())\n";
    // 演示「不能把有副作用的表达式放进 assert」
    int side_effect_counter = 0;
#ifndef NDEBUG
    // Debug 下会执行，Release 下不会 —— 这正是不能这么写的原因
    assert(++side_effect_counter == 1);
#endif
    std::cout << "  Debug 下 assert(++counter) 会真的自增，Release 下不会，"
                 "所以外层用 #ifndef NDEBUG 包住（本行 counter = "
              << side_effect_counter << "）\n";

    // ======================================================================
    print_title("5. #pragma once 与 include guard");
    // ======================================================================
    // 头文件被重复包含会导致「重复定义」错误。两种防护方式：
    //   (a) include guard：
    //           #ifndef MY_HEADER_H
    //           #define MY_HEADER_H
    //           ... 内容 ...
    //           #endif
    //       优点：完全标准，任何编译器都支持；宏名要全项目唯一（用路径做前缀）。
    //   (b) #pragma once：写在文件第一行。
    //       优点：短、不会撞名字、大项目里编译略快；
    //       缺点：非标准（但 GCC/Clang/MSVC 全都支持）。
    // 工程建议：新项目直接用 #pragma once；需要极强可移植性的库可以两者都写。
    std::cout << "  本题库是「一个 .cpp 一个可执行文件」，不写头文件，"
                 "所以不会遇到重复包含问题\n";
    std::cout << "  但读到别人的头文件时，要认得出 #ifndef / #pragma once 这两种写法\n";
    // 顺带提醒：不要用 #pragma once 当「防止多次链接」的手段，那是 inline 的职责
    std::cout << "  #pragma once 只防同一翻译单元内的重复包含；"
                 "函数重复定义要靠 inline 或移到 .cpp\n";

    // ======================================================================
    print_title("6. 手写日志宏");
    // ======================================================================
    LOG_INFO("日志宏演示：值 = %d, 字符串 = %s", 42, "hello");
    LOG_INFO("第二个参数可以为空之外的任何内容：%s", "done");
    // Debug 构建下上面会打印；Release 构建下这两行会变成空语句
    std::cout << "  LOG_INFO 在 Debug 下打印到 stdout（带文件名和行号），"
                 "在 Release（NDEBUG）下完全消失\n";
    DEBUG_VALUE(sizeof(int));
    DEBUG_VALUE(3 + 4 * 5);
    SHOW_EXPR(sizeof(double));
    SHOW_EXPR(__cplusplus);
    std::cout << "  DEBUG_VALUE / SHOW_EXPR 用 # 运算符把表达式原样变成字符串，"
                 "调试时非常省事\n";
    // 宏用 do{}while(0) 包起来的必要性演示
    const bool condition = true;
    if (condition) {
        LOG_INFO("do{...}while(0) 保证宏在 if/else 里表现像一条语句");
    } else {
        LOG_INFO("这条不会执行");
    }
    std::cout << "  如果把宏写成 { ... }（不带 do-while），上面这个 if/else 编译就不过\n";

    // ======================================================================
    print_title("7. #error 与条件编译");
    // ======================================================================
    // #error 让编译在预处理阶段就停下，用于「明确拒绝不支持的配置」。
    // 比起运行时抛异常，它在构建阶段就把问题暴露出来，代价最低。
// 下面这段是真实项目里的常见写法：不满足要求就直接编译失败。
#if defined(_MSC_VER) && _MSC_VER < 1929
#error "本题库需要 MSVC 19.29 (Visual Studio 2019 16.10) 或更新的版本"
#endif
    // #if 0 里的 #error 不会生效，可以用来演示「被注释掉的断言」
#if 0
#error "这段代码在 #if 0 里，永远不会被编译到，所以这个 #error 也不会触发"
#endif
    std::cout << "  #error 让编译在【预处理阶段】就停下，是「明确拒绝坏配置」的标准手段\n";
    std::cout << "  #if 0 / #endif 是标准的「注释掉一大段代码」手法（比 /* */ 安全，"
                 "不会因为内部有注释而提前结束）\n";
    // 条件编译的常见用法
#if defined(_WIN32)
    std::cout << "  当前平台：Windows（_WIN32 已定义）\n";
#elif defined(__linux__)
    std::cout << "  当前平台：Linux\n";
#else
    std::cout << "  当前平台：其它\n";
#endif
#if __has_include(<format>)
    std::cout << "  __has_include(<format>) 为真 -> 可以用 std::format\n";
#endif
    std::cout << "  __has_include 是 C++17 起的能力检测手法，比只判断编译器版本可靠\n";
    // C++ 特性测试宏（feature-test macro）是更精细的检测方式
#if defined(__cpp_lib_format)
    std::cout << "  __cpp_lib_format = " << __cpp_lib_format
              << "（标准库特性测试宏，MSVC/GCC/Clang 都提供）\n";
#else
    std::cout << "  未检测到 __cpp_lib_format\n";
#endif
    std::cout << "  特性检测优先级：__has_include / __cpp_xxx > 判断编译器版本 > 判断平台宏\n";

    // ======================================================================
    print_title("8. 常用调试手段速查");
    // ======================================================================
    // 1) 断点 + 监视窗口：最直接，看变量、调用栈（Call Stack）、内存窗口。
    // 2) 条件断点：循环第 10000 次才崩时，把断点设成「条件：i == 10000」。
    // 3) 数据断点（Data Breakpoint）：某个地址被写入时断下，专治「谁改了我的变量」。
    // 4) 输出窗口看「模块 / 异常 / 线程」；崩溃时用「异常设置」勾上 Win32 异常。
    // 5) 诊断工具（Diagnostic Tools）：内存快照对比找泄漏。
    // 6) 编译器内建：/RTC1 检查未初始化变量与栈损坏（Debug 默认开），
    //    /fsanitize=address 检测越界与 use-after-free（MSVC 支持）。
    // 7) printf / 日志宏：最简单也最可靠，分布式和异步场景常常只能靠日志。
    std::cout << "  最有效的三招：条件断点、数据断点、内存快照对比\n";
    std::cout << "  编译期可用的额外武器：/RTC1（Debug 默认）、/fsanitize=address、"
                 "/analyze（静态分析）\n";
    std::cout << "  本题库 build.ps1 已启用 /W4 /WX /permissive- /diagnostics:caret，"
                 "把警告当错误并给出插入符定位\n";

    std::cout << "\n[08] 结束。本章 9 个示例全部完成，继续读 NOTES.md。\n";
    return 0;
}
