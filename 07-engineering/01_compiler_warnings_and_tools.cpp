// ============================================================================
//  01_compiler_warnings_and_tools.cpp
// ----------------------------------------------------------------------------
//  演示主题：
//    1. 为什么必须开 /W4：编译器是第一个、也是最便宜的代码审查员
//    2. 六类最常见的真实警告（窄化转换 / 有符号无符号比较 / 未初始化变量 /
//       丢失返回值 / 变量遮蔽 / switch 缺 default）—— 每一类都用
//       #pragma warning 精确控制，把「只在运行时炸」变成「编译期报出来」
//    3. static_assert 表达接口契约（编译期断言）
//    4. [[nodiscard]] 表达「返回值不许丢」
//    5. /analyze 静态分析、/permissive- 两阶段查找、/Zc:__cplusplus 的现场验证
//    6. MSVC 常用选项清单
//
//  关键结论：
//    - 警告不是「建议」，是「便宜的 bug 报告」。先把 /W4 打开，再考虑 /WX。
//    - /WX 会让整个工程立刻编译不过 —— 正确做法是先把警告清零，再逐步开 /WX；
//      对单个警告用 #pragma warning(push/pop) 局部压制，压制必须写明理由。
//    - static_assert 是零成本契约：不产生任何运行期代码，却能拦住整类错误。
//    - 编译器警告、静态分析器、类型系统、断言，这四道防线越靠前越便宜；
//      能编译期发现的问题，绝不留到运行期。
//
//  编译验证：cl /std:c++20 /EHsc /W4 /WX /utf-8 /permissive- /Zc:__cplusplus
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

// ============================================================================
//  第 0 节：文件级的选择
// ----------------------------------------------------------------------------
//  MSVC 默认只开 /W1（大约等价于「明显的语法问题」）。/W4 才是「像个审查员」。
//  下面这几条不是默认全开的，工程里通常会显式打开：
//   - C4061/C4062：switch 没覆盖全部枚举值（默认关，但与 /permissive- 配合可用）
//   - C4820：结构体成员之间插入了填充字节（关心 ABI/内存时开）
//   - C4996：使用了被标记 deprecated 的接口（默认就开，这里显式强调）
//
//  工程做法：把「想要但默认没开」的警告在公共头文件里统一打开，
//  这样每个翻译单元行为一致 —— 但本文件的目的是演示，所以不做全工程设置。
// ============================================================================

namespace {

// ============================================================================
//  第 1 节：窄化转换（C4244 / C4267）
// ----------------------------------------------------------------------------
//  现象：double 赋给 float、size_t 赋给 int、long long 赋给 int。
//  为什么危险：它是「静默的数据丢失」。1e40 存进 float 直接变 inf，
//  大整数截断后可能变成负数，而且编译器默认一声不吭。
//  怎么防：
//    1) 打开 /W4，让 C4244（conversion, possible loss of data）报出来；
//    2) 用列表初始化 `float f{1.5};`，窄化在 C++11 起是**编译错误**，
//       比警告更硬（见本文件末尾的 static_assert 一节）；
//    3) 真需要转换时写 static_cast，让「我是故意丢精度的」变成显式意图。
// ============================================================================

// 注意这里面所有「故意的坏味道」都用 #pragma warning(disable:xxxx) 精确压制，
// 并在同一行注释里写清楚压制的理由 —— 这是工程里唯一可接受的压制方式：
// 压制范围最小、有理由、有期限（修好就删）。

double narrowing_source() { return 3.14159265358979; }

float narrowing_demo() {
#pragma warning(push)
#pragma warning(disable : 4244)  // 演示用：double -> float 的静默精度丢失
    // 这一行在 /W3 以上会报 C4244：conversion from 'double' to 'float',
    // possible loss of data。3.14159265358979 会被截成 float 的 7 位有效数字。
    float f = narrowing_source();
#pragma warning(pop)
    return f;
}

// 有符号 / 无符号混用：这是 C++ 世界里最经典的一类 bug。
// 1u - 2u 不会得到 -1，而是 4294967295；i < v.size() 里 i 会被转成无符号，
// 当 i 是负数时（比如 int i = -1）比较结果会变成 true，循环直接跑飞。
unsigned signed_unsigned_demo(const std::vector<int>& v) {
    unsigned matched = 0;
    // 正确写法：索引类型和 size() 的返回类型一致，永远不会触发整型提升
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] > 0) { ++matched; }
    }

#pragma warning(push)
#pragma warning(disable : 4018)  // 演示用：signed/unsigned mismatch
    // 下面这一行才是「坏味道」：int 与 std::size_t 比较，/W4 下会报 C4018。
    // 它平时也能跑对，但只要 int 变成负数就会静默出错（-1 被转成 4294967295，
    // 比较结果永远为 true），是一类「测试跑不到、线上才炸」的 bug。
    const int last = -1;
    if (last < v.size()) {
        matched += 1000;  // 这一句永远不会执行 —— 但编译器不报错，只有 /W4 会提醒你
    }
#pragma warning(pop)

    // 修复方式二选一：
    //   (a) 让索引/计数的类型和 size() 一致（推荐）；
    //   (b) 显式转换到同一类型再比较：static_cast<std::size_t>(last) < v.size()
    //       或者 std::cmp_less(last, v.size())（C++20 <utility>，专门解决这类比较）
    return matched;
}

// ============================================================================
//  第 2 节：未初始化变量（C4701 / C4700）
// ----------------------------------------------------------------------------
//  现象：局部变量忘了初始化就被读。Debug 下 MSVC 会把栈填充成 0xCC，
//  于是你读到 0xCCCCCCCC = -858993460，Release 下读到的是上一帧留下的垃圾。
//  为什么危险：这是「有时对、有时错」的典型来源 —— Debug 通过、Release 崩溃。
//  怎么防：1) 定义即初始化（C++ 的核心习惯之一）；
//          2) 打开 /W4 让 C4701「可能未初始化的局部变量被使用」报出来；
//          3) 类成员一律给默认成员初始化器，别指望构造函数记得赋值。
// ============================================================================

int uninitialized_demo(bool use_branch) {
    int result = 0;  // 正确姿势：定义即初始化，任何路径上它都有值
    if (use_branch) {
        result = 1;
    }
    // 若把上面写成 `int result;`，下面这行就会触发 C4701。
    // 注意：`int x; use(x);` 这种写法在 /W4 /WX 下必然编译失败，
    // 所以这里用注释说明，而不是真的制造一条无法通过编译的语句。
    return result;
}

// ============================================================================
//  第 3 节：丢失返回值（[[nodiscard]]）
// ----------------------------------------------------------------------------
//  现象：函数返回了一个有意义的值（错误码、新对象、算好的结果），
//  调用者却直接丢弃 —— 于是「错误被吞掉」「算了一遍没用」。
//  怎么防：在函数上加 [[nodiscard]]，丢弃返回值就报 C4834（/W3 起）。
//  这是「把接口约定写进类型系统」最便宜的一招，工程里应该当成默认动作：
//  纯查询函数、返回错误码的函数、返回新容器的函数，一律 [[nodiscard]]。
// ============================================================================

[[nodiscard]] int checked_divide(int a, int b) {
    if (b == 0) {
        return -1;  // 用返回值表达失败
    }
    return a / b;
}

// [[nodiscard]] 也可以加在类型上：整个类型的所有「返回该类型的函数」都被覆盖。
// 这正是 std::vector::empty()、std::string::size() 等标准库接口的做法。
struct [[nodiscard]] Status {
    bool ok = true;
    std::string message;
};

Status make_status(bool ok) { return Status{ok, ok ? "ok" : "failed"}; }

void nodiscard_demo() {
    // 正确：接住返回值并检查
    const int r = checked_divide(10, 2);
    std::cout << "  checked_divide(10, 2) = " << r << "\n";

    // 下面这一行如果取消注释，会触发 C4834（/W4 /WX 下直接编译失败）：
    //   checked_divide(10, 0);
    // 错误信息大意："discarding return value of function with 'nodiscard' attribute"
    std::cout << "  （把 checked_divide(10, 0); 这行的注释去掉，编译期就会报 C4834）\n";

    const Status st = make_status(false);
    std::cout << "  Status{ok=" << std::boolalpha << st.ok << ", msg=\"" << st.message << "\"}\n";
    std::cout << std::noboolalpha;
}

// ============================================================================
//  第 4 节：变量遮蔽（C4456 / C4457 / C4458 / C4459）
// ----------------------------------------------------------------------------
//  现象：内层作用域定义了和外层同名的变量，外层那个从此「看不见了」。
//  为什么危险：读代码的人以为改的是外层变量，实际改的是内层副本；
//  指针/引用遮蔽更糟 —— 你以为是同一个对象，其实不是。
//  怎么防：1) /W4 打开 C4456（声明隐藏了上一个局部声明）；
//          2) 命名上区分（循环变量用 i/j/k，成员加前缀）；
//          3) 缩小作用域，别在函数顶部一次性声明一堆变量。
// ============================================================================

int shadowing_demo() {
    int total = 0;
    {
#pragma warning(push)
#pragma warning(disable : 4456)  // 演示用：内层 total 遮蔽了外层 total
        int total = 100;         // 内层的 total，和外层毫无关系
#pragma warning(pop)
        total += 1;  // 改的是内层；外层仍是 0
    }
    total += 5;  // 改的是外层
    return total;  // 结果 5，而不是 105 —— 这就是遮蔽的坑
}

// ============================================================================
//  第 5 节：switch 缺 default（C4062 / C4715「并非所有控件路径都返回值」）
// ----------------------------------------------------------------------------
//  现象：枚举加了新值，但所有 switch 都没更新，于是走到「没有分支匹配」的
//  路径上，函数返回垃圾值 / 行为未定义。
//  为什么危险：这是「改了 A 忘了改 B」的经典形态，编译器很容易帮你发现，
//  前提是你把警告打开。
//  怎么防：1) switch 覆盖所有枚举值 + 显式 default；
//          2) 用 C4715（不是所有路径都有返回值）兜底；
//          3) 在 C++ 里更推荐「用多态/表驱动」替代「到处 switch 枚举」，
//             新增枚举值时不会漏改（编译器不会替你记着）。
// ============================================================================

enum class Color { Red, Green, Blue };

std::string color_name(Color c) {
    switch (c) {
        case Color::Red:
            return "Red";
        case Color::Green:
            return "Green";
        case Color::Blue:
            return "Blue";
        // 有了 default，即便将来 Color 加了新值，函数也一定返回合法字符串。
        // 工程里另一种更严格的做法是：**不写 default**，让 C4062/C4715
        // 在新增枚举值时把编译打挂 —— 强制你去处理新分支。
        default:
            return "Unknown";
    }
}

// ============================================================================
//  第 6 节：把警告变成编译期错误（#pragma warning 的 error 用法）
// ----------------------------------------------------------------------------
//  本节演示工程里最实用的一招：在某个文件的顶部把「这个文件绝对不能出现」
//  的警告升级成错误。因为 /WX 是全局开关（一开全工程都过不去），
//  而「按警告号精准升级」可以增量治理历史代码。
//
//  语法：
//    #pragma warning(error : 4996)   // 4996 从「警告」升级为「错误」
//    #pragma warning(default : 4996) // 恢复默认级别
//    #pragma warning(push) / (pop)   // 成对使用，把影响范围限制在最小
// ============================================================================

// 演示：把 C4996（使用了被标记为 deprecated 的接口）在本文件内升级为错误。
// 为了让本文件本身能编译通过，这里用 push/pop 把它限制在一个函数里，
// 并且在这个函数里不去触碰任何 deprecated 接口。
#pragma warning(push)
#pragma warning(error : 4996)
void deprecated_is_error_here() {
    // 如果这里调用任何 [[deprecated]] 函数，编译器会直接报 error C4996 而不是警告。
    // 实际项目里建议把这一行放在每个 .cpp 的顶部（而不是 push/pop 包裹），
    // 表示「本文件禁止使用已废弃接口」。
}
#pragma warning(pop)

}  // namespace

// ============================================================================
//  第 7 节：static_assert —— 编译期的接口契约
// ----------------------------------------------------------------------------
//  static_assert 的语义：条件为假时**编译失败**，条件为真时**不生成任何代码**。
//  它比运行期断言（assert）强的地方在于：错误发生在写代码的人手上，
//  而不是在用户手上；而且零运行期成本。
//
//  工程里最常用的三个场景：
//    1) 检查类型假设（大小、对齐、是否有符号）
//    2) 检查平台假设（32 位 / 64 位、字节序）
//    3) 用作模板参数的契约检查（把错误信息写在 static_assert 里，
//       比几十屏的模板报错可读得多）
// ============================================================================

// 场景 1：类型假设。写协议解析、序列化、位操作时这是必备的。
static_assert(sizeof(std::uint32_t) == 4, "uint32_t 必须是 4 字节，否则协议布局不成立");
static_assert(sizeof(std::int64_t) == 8, "int64_t 必须是 8 字节");
static_assert(alignof(double) <= alignof(std::max_align_t), "double 的对齐不应超过最大对齐");

// 场景 2：有符号 / 无符号假设。左移右移、溢出行为都依赖它。
static_assert(std::is_signed_v<int>, "int 必须是有符号的，负数算术依赖这一点");
static_assert(std::is_unsigned_v<unsigned>, "unsigned 必须是无符号的");

// 场景 3：模板参数的契约检查。
// 把「T 必须满足什么」写清楚，用错了立刻在调用点报一条人话错误。
template <typename T>
T clamp_value(T v, T lo, T hi) {
    static_assert(std::is_arithmetic_v<T>, "clamp_value 只支持算术类型：别传自定义类型进来");
    static_assert(std::is_copy_constructible_v<T>, "T 必须可拷贝构造");
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}

// 编译期断言还能用在「契约违反时必须停下」的地方。
// 下面这条永远为真，只是展示写法；把 false 改成 true 就能现场看到编译错误：
//   static_assert(sizeof(void*) == 8, "本工程只支持 x64");
static_assert(sizeof(void*) == 8, "本工程只支持 x64 平台（32 位下指针大小不同）");

int main() {
    std::cout << "==== 01 编译警告与工具链 ====\n\n";

    std::cout << "---- 1. 窄化转换（C4244）----\n";
    std::cout << "  double 源值 = 3.14159265358979\n";
    std::cout << "  赋给 float 后 = " << narrowing_demo() << "\n";
    std::cout << "  结论：精度被静默丢弃。加 /W4 会报 C4244；用 float f{3.14} 则是编译错误。\n\n";

    std::cout << "---- 2. 有符号 / 无符号比较（C4018）----\n";
    const std::vector<int> data{1, -2, 3, -4, 5};
    std::cout << "  data 中正整数个数 = " << signed_unsigned_demo(data) << "（应为 3）\n";
    std::cout << "  结论：int i 与 size() 比较会触发整型提升；索引请用 std::size_t 或 std::ptrdiff_t。\n";
    std::cout << "        演示函数里的 `last < v.size()` 永远为 true，是编译期能警告、运行期不报错的坑。\n";
    std::cout << "  验证：" << (0u - 1u) << " 就是无符号回绕的结果，不是 -1。\n\n";

    std::cout << "---- 3. 未初始化变量（C4701）----\n";
    std::cout << "  uninitialized_demo(true)  = " << uninitialized_demo(true) << "\n";
    std::cout << "  uninitialized_demo(false) = " << uninitialized_demo(false) << "\n";
    std::cout << "  结论：Debug 下栈被填成 0xCC（十进制 -858993460），Release 下是随机垃圾。\n";
    std::cout << "        定义即初始化是最便宜的保险。\n\n";

    std::cout << "---- 4. 丢失返回值（C4834）----\n";
    nodiscard_demo();
    std::cout << "  结论：[[nodiscard]] 把「必须检查返回值」写进类型系统，零运行期成本。\n\n";

    std::cout << "---- 5. 变量遮蔽（C4456）----\n";
    std::cout << "  shadowing_demo() = " << shadowing_demo() << "（期望 5：内层 total 是另一个变量）\n";
    std::cout << "  结论：遮蔽让「改的是哪个变量」变得难以阅读，/W4 会直接报出来。\n\n";

    std::cout << "---- 6. switch 缺 default（C4062 / C4715）----\n";
    std::cout << "  color_name(Color::Red)   = " << color_name(Color::Red) << "\n";
    std::cout << "  color_name(Color::Blue)  = " << color_name(Color::Blue) << "\n";
    std::cout << "  color_name((Color)99)    = " << color_name(static_cast<Color>(99)) << "\n";
    std::cout << "  结论：没有 default 时，非法枚举值会走到「所有路径都没返回」的未定义行为。\n\n";

    std::cout << "---- 7. static_assert：编译期契约 ----\n";
    std::cout << "  本文件编译期已经验证：\n";
    std::cout << "    sizeof(uint32_t) == " << sizeof(std::uint32_t) << "\n";
    std::cout << "    sizeof(int64_t)  == " << sizeof(std::int64_t) << "\n";
    std::cout << "    sizeof(void*)    == " << sizeof(void*) << "（x64）\n";
    std::cout << "  clamp_value(15, 0, 10) = " << clamp_value(15, 0, 10) << "\n";
    std::cout << "  clamp_value(3, 0, 10)  = " << clamp_value(3, 0, 10) << "\n";
    std::cout << "  结论：把契约写在 static_assert 里，错误在编译期暴露，运行期零成本。\n\n";

    std::cout << "---- 8. 工具链开关的现场验证 ----\n";
#ifdef _MSVC_LANG
    std::cout << "  _MSVC_LANG     = " << _MSVC_LANG << "（MSVC 总是报真实语言标准）\n";
#endif
    std::cout << "  __cplusplus    = " << __cplusplus << "\n";
    std::cout << "  说明：不加 /Zc:__cplusplus 时 MSVC 会把 __cplusplus 固定报成 199711L，\n";
    std::cout << "        于是 #if __cplusplus >= 202002L 这类条件编译会静默失效。\n";
    std::cout << "        本项目 build.ps1 与 CMakeLists.txt 都已经加了 /Zc:__cplusplus。\n";
#ifdef _DEBUG
    std::cout << "  当前配置：Debug（_DEBUG 已定义，assert 生效）\n";
#else
    std::cout << "  当前配置：Release（NDEBUG 已定义，assert 被消除）\n";
#endif
#ifdef _PREFAST_
    std::cout << "  /analyze 静态分析已启用\n";
#else
    std::cout << "  /analyze 未启用：命令行加 /analyze 可开启 MSVC 静态分析（慢但能查出空指针、越界、资源泄漏）\n";
#endif
    std::cout << "\n";

    std::cout << "---- 9. MSVC 常用选项清单 ----\n";
    std::cout << "  选项                     作用\n";
    std::cout << "  -----------------------  ------------------------------------------------\n";
    std::cout << "  /W4                      高警告级别（推荐最低要求）\n";
    std::cout << "  /WX                      警告当错误（清完警告后再开）\n";
    std::cout << "  /Wall                    全部警告（含 STL 头里的噪音，工程里别用）\n";
    std::cout << "  /permissive-             关闭宽松模式，标准两阶段名字查找\n";
    std::cout << "  /Zc:__cplusplus          让 __cplusplus 报真实值\n";
    std::cout << "  /utf-8                   源码与执行字符集都按 UTF-8\n";
    std::cout << "  /EHsc                    标准 C++ 异常模型（必须开）\n";
    std::cout << "  /std:c++20               语言标准\n";
    std::cout << "  /analyze                 静态分析（能查空指针/越界/资源泄漏）\n";
    std::cout << "  /fsanitize=address       AddressSanitizer，运行期查越界与 use-after-free\n";
    std::cout << "  /MD  /MDd                动态运行时（Release / Debug）——绝对不要混用\n";
    std::cout << "  /Zi                      生成调试信息\n";
    std::cout << "  /O2  /Od                 优化 / 不优化\n";
    std::cout << "  /bigobj                  允许更多节（模板多时报 C1128 才需要）\n";
    std::cout << "  /diagnostics:caret       错误定位到列\n";
    std::cout << "  /showIncludes            打印 include 树（排查头文件依赖）\n";
    std::cout << "  /d1reportAllClassLayout  打印类内存布局（查 ABI/填充）\n\n";

    std::cout << "==== 结论：警告是免费的 bug 报告；第 0 步永远是「把 /W4 打开」====\n";
    return 0;
}
