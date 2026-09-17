// ============================================================================
//  05_debugging_techniques.cpp
// ----------------------------------------------------------------------------
//  演示主题：
//    1. 怎么读崩溃：访问违例 / 栈溢出 / 堆破坏各自的现象与异常码
//    2. 五类经典内存错误的「故意错误版」与「安全版」对照：
//         数组越界、空指针解引用、迭代器失效、悬垂指针、栈溢出递归
//       —— 错误版全部放在 `#ifdef DEMO_ENABLE_BUGS` 里，**默认不参与编译**，
//          所以 build.ps1 -Chapter 07-engineering 一定不会因为故意写的 bug 失败。
//          想亲眼看崩溃：加 /DDEMO_ENABLE_BUGS 编译，再在调试器里 F5。
//    3. __debugbreak() / DebugBreak() / 条件断点 / 数据断点 的用法
//    4. std::vector::at() 而不是 []：把「静默越界」变成「立刻抛异常」
//    5. AddressSanitizer（/fsanitize=address）与 CRT 堆调试
//       （_CrtSetDbgFlag / _CrtCheckMemory / _CrtDumpMemoryLeaks）的实际用法
//    6. std::stacktrace（C++23）在本机的可用性
//    7. 「二分注释法」：用注释掉一半代码的方式快速定位问题
//
//  关键结论：
//    - 崩溃不可怕，「静默的错误结果」才可怕。让错误尽早、尽响亮地暴露，
//      是调试能力的一部分：at() 而不是 []、断言、ASan，都是这个思路。
//    - 每个内存错误都有「安全版」：越界 -> at()；空指针 -> 引用或 optional；
//      迭代器失效 -> 用下标或先 reserve；悬垂指针 -> unique_ptr / 按值；
//      递归过深 -> 显式栈或迭代。
//    - 调试的一半工作量是「缩小范围」：二分注释法、最小可复现、二分查找提交。
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
//  CRT 调试堆：要用它报出「泄漏发生在哪一行」，必须按下面这个顺序写。
//
//    1) _CRTDBG_MAP_ALLOC 必须在 include <crtdbg.h> **之前** 定义；
//    2) <crtdbg.h> 应该在**所有标准库头文件之后** include ——
//       因为它会 #define new 成 new(_NORMAL_BLOCK, __FILE__, __LINE__)，
//       如果在标准库头文件之前生效，就会把标准库里的 new 声明一起改写掉。
//    3) 只在 Windows/MSVC 下有效；其它平台用 ASan/valgrind 替代。
//
//  这个顺序是本文件刻意安排的：前面那批 include 就是「先包含标准库」，
//  下面这一小段才是「再包含 crtdbg.h」。
//  注意 C5105 只是「宏展开产生了定义行为」的提醒，官方模板就是这样写的，
//  所以这里精确压制它并写清楚理由。
// ---------------------------------------------------------------------------
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 5105)  // 演示用：_CRTDBG_MAP_ALLOC 的官方用法会触发 C5105
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>   // _CrtSetDbgFlag / _CrtCheckMemory / _CrtDumpMemoryLeaks（含 #define new）
#include <intrin.h>   // __debugbreak
#include <windows.h>  // DebugBreak（注意：windows.h 会引入大量宏，工程里尽量只放到 .cpp 里）
#pragma warning(pop)
#endif

// ---------------------------------------------------------------------------
//  第 6 节要用：std::stacktrace 是 C++23 特性。
//  MSVC 目前只在 /std:c++latest（或 c++23preview）下提供，且需要链接
//  dbghelp.lib。这里用特性测试宏判断，不支持就打印说明，不硬编。
// ---------------------------------------------------------------------------
#if defined(__cpp_lib_stacktrace)
#include <stacktrace>
#define HAS_STACKTRACE 1
#else
#define HAS_STACKTRACE 0
#endif

// ---------------------------------------------------------------------------
//  MSVC 的 CRT 调试堆：要拿到「泄漏的分配发生在哪一行」，必须先定义
//  _CRTDBG_MAP_ALLOC 再 include <crtdbg.h>（顺序不能反，这是官方文档要求）。
//  我们在文件顶部已经 include 过 crtdbg.h 了，所以这里只做说明，
//  实际的宏定义演示放在下面的独立函数里（见第 6 节注释）。
// ---------------------------------------------------------------------------

namespace {

// ============================================================================
//  第 1 节：怎么读崩溃 —— 三类常见崩溃的现象对照
// ----------------------------------------------------------------------------
//  在 Visual Studio 里崩溃会先弹一个对话框，然后中断在出错处。
//  「异常码」是定位问题的第一把钥匙（调试器里看「异常设置」窗口，或看输出）：
//
//   0xC0000005  ACCESS_VIOLATION     访问违例：读写了一块不属于你的地址
//              常见原因：空指针解引用、数组越界、悬垂指针/use-after-free、
//                        野指针、写入只读内存（字符串字面量）
//              现象：崩在一条看起来毫无问题的语句上；调用栈里往往不是「真正出错的地方」
//
//   0xC00000FD  STACK_OVERFLOW       栈溢出：把线程栈（默认 1 MB）用完了
//              常见原因：无终止条件的递归、递归深度随输入增长、
//                        在栈上开了巨大的局部数组（int buf[10'000'000]）
//              现象：崩在函数序言（prologue）附近，调用栈里有成百上千层同一个函数；
//                    或者直接「Stack overflow」提示，看不到有用的栈
//
//   堆破坏（heap corruption）        没有统一的异常码，现象千奇百怪
//              常见原因：越界写、double free、free 了非堆指针、
//                        跨模块 free（Debug/Release 运行时库混用）
//              现象：崩在 malloc/free 内部（_heap_alloc、RtlValidateHeap），
//                    或者「在完全无关的地方」崩 —— 因为破坏发生在很久以前；
//                    也可能表现为「程序跑完莫名其妙报堆损坏」
//
//  读法总结：
//    1) 先看异常码，判断大类；
//    2) 再看调用栈的**最底层那几帧**（你的代码），而不是最顶层（库函数）；
//    3) 访问违例看「地址是多少」：接近 0 -> 空指针；很大的值 -> 野指针；
//    4) 堆破坏必须开调试堆（第 6 节）或 ASan，否则只能靠二分注释法；
//    5) Release 下崩溃点常常和 Debug 不同（优化/内联/栈布局变了）——
//       不是「Release 才有 bug」，而是 Debug 下被掩盖了（未初始化内存恰好是 0）。
// ============================================================================

void print_crash_codes() {
    std::cout << "  异常码        名称              典型原因\n";
    std::cout << "  ------------  ----------------  ------------------------------------------\n";
    std::cout << "  0xC0000005    ACCESS_VIOLATION  空指针、越界、悬垂指针、use-after-free\n";
    std::cout << "  0xC00000FD    STACK_OVERFLOW    无终止递归、栈上开巨大数组\n";
    std::cout << "  （无固定码）  堆破坏            越界写、double free、Debug/Release 混用\n";
    std::cout << "  0xC0000094    INTEGER_DIVIDE_BY_ZERO  除零\n";
    std::cout << "  0xE06D7363    C++ 异常（未捕获）  throw 没有被 catch\n";
}

// ============================================================================
//  第 2 节：五类经典内存错误 —— 故意错误版 + 安全版
// ----------------------------------------------------------------------------
//  所有「错误版」都在 #ifdef DEMO_ENABLE_BUGS 里。
//  默认不定义这个宏，所以：
//    - 不影响 build.ps1 的编译验证（不会崩、不会有警告）
//    - 学习者看代码就能知道错误长什么样
//  想亲手看崩溃，用命令行编译时加 /DDEMO_ENABLE_BUGS，然后在 VS 里 F5。
// ============================================================================

// ---------------------------------------------------------------- 2.1 数组越界
std::int64_t read_out_of_range_bad(const std::vector<int>& v, std::size_t index) {
#ifdef DEMO_ENABLE_BUGS
    // 错误版：operator[] 不做边界检查。越界读在 Debug 下大概率能「读到垃圾」而不崩，
    // 越界**写**则可能破坏堆头，引发几万行之后莫名其妙的崩溃。
    return v[index];  // index >= size() 时：未定义行为
#else
    (void)v;
    (void)index;
    return -1;  // 故意错误版未编译进来
#endif
}

std::int64_t read_out_of_range_safe(const std::vector<int>& v, std::size_t index) {
    // 安全版：at() 会做边界检查，越界抛 std::out_of_range，调用栈直接指到这一行。
    // 代价：一次比较 + 可能的分支（热点循环里可以用 []，但要有断言兜底）。
    try {
        return v.at(index);
    } catch (const std::out_of_range& e) {
        std::cout << "    安全版捕获到越界：" << e.what() << "\n";
        return -1;
    }
}

// ------------------------------------------------------------ 2.2 空指针解引用
struct Node {
    int value = 0;
    Node* next = nullptr;
};

[[nodiscard]] Node* find_node_bad(Node* head, int target) {
    for (Node* p = head; p != nullptr; p = p->next) {
        if (p->value == target) {
            return p;
        }
    }
    return nullptr;  // 找不到就返回空指针 —— 调用者必须处理
}

std::int64_t deref_maybe_null_bad(Node* p) {
#ifdef DEMO_ENABLE_BUGS
    // 错误版：不检查返回值就解引用。
    // 现象：0xC0000005 访问违例，出错地址接近 0x00000000（空指针 + 成员偏移）。
    // 调试器会停在下面这一行 —— 这是少数「出错点 = 真正原因」的情况。
    return p->value;
#else
    (void)p;
    return -1;
#endif
}

// 安全版 A：引用表达「一定不为空」的前置条件，并在入口断言。
// 安全版 B：optional 表达「可能没有」。
[[nodiscard]] std::optional<int> deref_maybe_null_safe(const Node* p) {
    if (p == nullptr) {
        return std::nullopt;  // 明确表达「没有值」，调用者必须处理
    }
    return p->value;
}

// 更工程化的做法：让「空」根本不可能出现 —— 用 unique_ptr + 引用传递。
int deref_never_null(const Node& node) { return node.value; }

// ------------------------------------------------------------ 2.3 迭代器失效
std::vector<int> iterator_invalidation_bad() {
    std::vector<int> v{1, 2, 3, 4, 5};
#ifdef DEMO_ENABLE_BUGS
    // 错误版：push_back 可能触发扩容，扩容会重新分配内存，
    // 于是原来拿到的迭代器/指针/引用**全部失效**（指向已释放的旧内存）。
    // 现象：可能是访问违例，也可能「碰巧还是对的」—— 这正是它最危险的地方。
    const std::vector<int>::iterator it = v.begin();
    v.push_back(6);      // 这里 5 个元素 -> 6 个元素，必然扩容
    (void)*it;           // it 已经悬垂：use-after-free
#endif
    return v;
}

std::vector<int> iterator_invalidation_safe() {
    std::vector<int> v{1, 2, 3, 4, 5};
    // 安全版有三条路：
    //   (a) 先 reserve，保证不会扩容（最推荐，也最快）；
    //   (b) 用下标而不是迭代器（下标在扩容后依然有效）；
    //   (c) 需要时重新获取迭代器（v.begin() / std::next(...)）。
    v.reserve(6);  // (a) 一次分配到位，之后的 push_back 不会让任何迭代器失效
    const std::vector<int>::iterator it = v.begin();
    v.push_back(6);
    // (c) 注意：即使 reserve 了，插入/删除**中间**元素仍然会让之后的迭代器失效
    //     （元素要搬移）。所以真正安全的做法是「用下标」或「用完再取」。
    const int first = *it;  // 这里安全：reserve 之后再 push_back 不搬移已有元素
    std::cout << "    安全版：首元素 = " << first << "，容量 = " << v.capacity() << "\n";
    return v;
}

// ------------------------------------------------------------ 2.4 悬垂指针
int* dangling_pointer_bad() {
#ifdef DEMO_ENABLE_BUGS
    int local = 42;
    return &local;  // 错误版：返回局部变量的地址，函数一返回这块栈就失效了
#else
    return nullptr;
#endif
}

// 安全版：返回所有权明确的对象。
//   (a) 需要堆对象 -> std::unique_ptr（所有权清晰，不会泄漏）
//   (b) 只需要值 -> 直接按值返回（最简单，也最快）
[[nodiscard]] std::unique_ptr<int> make_owned_int(int value) { return std::make_unique<int>(value); }

[[nodiscard]] int make_int_by_value(int value) { return value; }

// ------------------------------------------------------------ 2.5 栈溢出
// 危险版：没有终止条件的递归。每层调用消耗一个栈帧，
// 默认线程栈 1 MB，几千层就到顶。
int infinite_recursion_bad(int depth) {
#ifdef DEMO_ENABLE_BUGS
    volatile int padding[64] = {};  // 让每层栈帧大一点，更快溢出（也避免被优化成尾调用）
    (void)padding[0];
    return infinite_recursion_bad(depth + 1) + 1;  // 永不返回
#else
    return depth;  // 故意错误版未编译进来
#endif
}

// 安全版：给递归加深度上限（把「崩溃」变成「可处理的结果」）。
// 递归深度上限要根据「每层栈帧大小 x 上限 < 线程栈大小」来估。
constexpr int kMaxRecursionDepth = 1000;

[[nodiscard]] std::optional<int> bounded_recursion(int depth) {
    if (depth > kMaxRecursionDepth) {
        return std::nullopt;  // 到达上限：明确失败，而不是把栈用爆
    }
    if (depth == kMaxRecursionDepth) {
        return depth;
    }
    const std::optional<int> inner = bounded_recursion(depth + 1);
    if (!inner) {
        return std::nullopt;
    }
    return *inner;
}

// 更彻底的安全版：把递归改成迭代（显式栈），深度只受堆内存限制。
[[nodiscard]] std::int64_t iterative_sum_to(int n) {
    std::int64_t sum = 0;
    for (int i = 1; i <= n; ++i) {
        sum += i;  // 循环不会消耗栈帧
    }
    return sum;
}

// ============================================================================
//  第 3 节：主动断点 —— 让程序停在你想看的地方
// ----------------------------------------------------------------------------
//  调试器里「加断点」人人都会，但真正省时间的是这几种：
//
//   a) 条件断点（Conditional Breakpoint）
//      右键断点 -> 条件，写 `i == 5000 && data[i] < 0`。
//      适合「循环第 5000 次才出错」这种问题 —— 否则你要按 5000 次 F5。
//      也支持「命中次数」（Hit Count）：只在第 n 次命中时中断。
//
//   b) 数据断点（Data Breakpoint，VS 里叫「数据断点」/「内存断点」）
//      调试 -> 新建断点 -> 数据断点，填 `&my_object.member` 或某个地址，
//      当这块内存被写入时中断。**这是找「谁改坏了我的变量」的唯一有效手段** ——
//      堆破坏、不变量被破坏、成员被意外修改，都靠它。
//      注意：硬件数据断点数量有限（x86/x64 通常是 4 个），且需要 Debug 配置。
//
//   c) 函数断点：在「断点」窗口里直接输入函数名（如 `MyClass::update`），
//      适合「不知道在哪调用」的场景；配合调用栈一眼就能看出是谁调的。
//
//   d) __debugbreak() / DebugBreak()：在代码里主动断下来。
//      用途：断言失败处、不该走到的分支、临时排查。
//      __debugbreak() 是编译器内建（生成 int 3 指令），任何配置下都能断；
//      DebugBreak() 是 Win32 API（同样生成断点异常），需要 windows.h。
//      重要区别：**不带调试器运行时，__debugbreak() 会直接让程序崩溃**，
//      所以它只应该出现在「Debug 专用」的代码里（我们的 CHECK 宏就是这么做的）。
// ============================================================================

void demo_breakpoints() {
    // 主动断点演示：默认不真的断下来（否则没法自动验证），只是打印说明。
    // 想试：把下面的条件改成 true，在调试器里 F5，会断在这一行。
    constexpr bool kBreakHere = false;  // 改成 true 就会断
    if (kBreakHere) {
#if defined(_MSC_VER)
        __debugbreak();  // 生成 int 3：调试器会在这里停下，调用栈完整
#endif
    }

    int watched = 0;
    watched += 1;  // 数据断点候选：对 &watched 下数据断点，这行会中断
    std::cout << "    主动断点：__debugbreak() / DebugBreak() 会在调试器里停下；\n";
    std::cout << "    不带调试器运行时 __debugbreak() 会变成崩溃（int 3 无人接管）。\n";
    std::cout << "    watched = " << watched << "（对 &watched 下数据断点即可捕获写入）\n";
#if defined(_MSC_VER)
    std::cout << "    本机可用：DebugBreak() 声明于 windows.h，__debugbreak() 是编译器内建。\n";
#endif
}

// ============================================================================
//  第 4 节：让错误尽早暴露 —— at() / 断言 / 不变量
// ----------------------------------------------------------------------------
//  「不崩溃」不等于「正确」：越界读常常读到合法但错误的内存，程序继续跑，
//  最后在别的地方给出错误结果。这类 bug 比崩溃难查一百倍。
//  对策是把「静默」变成「响亮」：
//    - 容器访问用 at()（抛异常）或 [] + 断言；
//    - 关键计算结果用断言检查范围；
//    - 用 ASan / 调试堆在运行期抓越界与泄漏。
// ============================================================================

void demo_at_vs_subscript() {
    const std::vector<int> data{10, 20, 30};
    std::cout << "  容器大小 = " << data.size() << "，故意访问下标 5：\n";
    const std::int64_t r = read_out_of_range_safe(data, 5);
    std::cout << "    at(5) 的结果 = " << r << "（越界已转成异常，程序继续跑）\n";
    std::cout << "    operator[](5) 的结果 = " << read_out_of_range_bad(data, 5)
              << "（错误版未启用；启用后是未定义行为）\n";
}

// ============================================================================
//  第 5 节：AddressSanitizer（MSVC 的 /fsanitize=address）
// ----------------------------------------------------------------------------
//  ASan 在**编译期插桩**，在**运行期**检查每次内存访问：
//    - 堆/栈/全局变量的越界读写
//    - use-after-free / use-after-return / double-free
//    - 内存泄漏（/fsanitize=address 默认带 LeakSanitizer）
//  代价：运行时大约慢 2 倍、内存占用约 2~3 倍，所以只在测试/CI 里开。
//
//  用法（MSVC）：
//      cl /std:c++20 /fsanitize=address /Zi /MDd  05_debugging_techniques.cpp
//      cl /std:c++20 /fsanitize=address /Zi /MD   05_debugging_techniques.cpp
//  出错时会打印带完整调用栈的报告，例如：
//      ERROR: AddressSanitizer: heap-buffer-overflow on address 0x...
//      #0 ... in main ...05_debugging_techniques.cpp:123
//
//  注意：ASan 与 /RTC、增量链接、编辑继续(EnC) 不兼容；开了 ASan 就别开它们。
// ============================================================================

void demo_asan_detection() {
#if defined(__SANITIZE_ADDRESS__)
    std::cout << "  AddressSanitizer：**已启用**（编译时带了 /fsanitize=address）\n";
    std::cout << "    本进程的任何越界/use-after-free 都会被检测并打印调用栈。\n";
#else
    std::cout << "  AddressSanitizer：未启用\n";
    std::cout << "    想启用：在命令行加 /fsanitize=address，或 CMake 里加 -DENGINEERING_ENABLE_ASAN=ON\n";
    std::cout << "    ASan 只在测试/CI 里开；它会拖慢约 2 倍、多占 2~3 倍内存。\n";
#endif
}

// ============================================================================
//  第 6 节：MSVC 的 CRT 堆调试 —— 检测内存泄漏与堆破坏
// ----------------------------------------------------------------------------
//  三步走（缺一不可）：
//    1) 在**所有 include 之前**定义 _CRTDBG_MAP_ALLOC，
//       然后再 include <crtdbg.h>。这样 new/malloc 会被替换成带文件行号的版本，
//       泄漏报告里能直接看到「哪一行分配的内存没释放」。
//    2) 在 main() 的最开始调用 _CrtSetDbgFlag(...) 打开泄漏检测。
//    3) 在 main() 返回前（所有对象析构之后）调用 _CrtDumpMemoryLeaks()，
//       或者在「程序退出时」自动 dump（_CRTDBG_LEAK_CHECK_DF）。
//
//  想让泄漏检测真的报出「泄漏」并看到效果，最稳的办法是用一个独立的小程序。
//  本文件的做法是：把泄漏检测的调用放在一个显式的函数里，
//  并在 NOTES.md 里给出完整可复制的模板。
//
//  _CrtCheckMemory()：随时校验堆的完整性（返回 FALSE 说明堆已经被写坏）。
//    这是找「堆破坏」的利器：在可疑操作前后各调一次，就能把范围缩小到两行之间。
// ============================================================================

// 故意泄漏 128 字节（**不是 UB**，只是不释放；用于演示泄漏检测）。
// 定义了 _CRTDBG_MAP_ALLOC 时，官方文档的说法是 malloc 会被映射成
//   _malloc_dbg(size, _NORMAL_BLOCK, __FILE__, __LINE__)
// 于是泄漏报告里会带文件名和行号。
//
// 本机实测（MSVC 14.51 + 新版 UCRT 的 <crtdbg.h>）：报告能准确报出
// 「检测到泄漏、块大小、块的地址、块内容」，但**没有**打印文件名和行号。
// 原因是新版 crtdbg.h 里那套 `#define malloc(...)` 的映射在 C++ 下不会
// 覆盖到我们这里（换行号的机制变了）。
// 工程结论：
//   - 想定位「哪一行泄漏」，最可靠的办法是直接用 _malloc_dbg 显式带位置：
//         _malloc_dbg(128, _NORMAL_BLOCK, __FILE__, __LINE__);
//     或者用 AddressSanitizer（/fsanitize=address，默认带 LeakSanitizer），
//     它会打印完整的调用栈 —— 这也是本文件推荐 ASan 的原因之一。
//   - 看看自己项目用到的 CRT 版本，再决定依赖 _CRTDBG_MAP_ALLOC 的行号功能。
void leak_some_memory() {
    void* leaked = std::malloc(128);  // _CRTDBG_MAP_ALLOC 下走调试堆（会记录分配块）
    if (leaked != nullptr) {
        std::memset(leaked, 0xAB, 128);  // 真的写一下，防止被优化掉
    }
    // 故意不 free：让泄漏检测有东西可报
}

void demo_crt_debug_heap() {
#if defined(_MSC_VER)
    // 打开调试堆的全部检查（注意：只在 Debug 配置下有实际意义）
    const int flags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
    const int new_flags = flags | _CRTDBG_ALLOC_MEM_DF       // 打开调试分配（记录每次分配）
                                  | _CRTDBG_CHECK_ALWAYS_DF  // 每次分配/释放都校验堆（慢，仅排查时开）
                                  | _CRTDBG_LEAK_CHECK_DF;   // 程序退出时自动 dump 泄漏
    _CrtSetDbgFlag(new_flags);

    // 默认的泄漏报告走 _CRT_WARN 通道，只在 VS 的「输出」窗口里出现。
    // 命令行下想看到，就把 _CRT_WARN 改到 stderr。
    const int saved_mode = _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);

    std::cout << "  CRT 调试堆：已配置（当前编译带 _CRTDBG_MAP_ALLOC）\n";
    std::cout << "    _CRTDBG_ALLOC_MEM_DF      记录每次分配（否则下面的检查都没数据）\n";
    std::cout << "    _CRTDBG_CHECK_ALWAYS_DF   每次分配/释放都校验堆（很慢，只在排查时开）\n";
    std::cout << "    _CRTDBG_LEAK_CHECK_DF     退出时自动打印泄漏报告\n";
    std::cout << "    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE) + 指向 stderr\n";
    std::cout << "        -> 让报告出现在命令行窗口，而不是只进 VS 的「输出」窗口\n";

    // 堆完整性检查：把可疑操作夹在两次检查之间，就能定位「谁写坏了堆」。
    // 这是排查「堆破坏」最有效的一招（比读代码快得多）。
    const int ok_before = _CrtCheckMemory();
    leak_some_memory();  // 只泄漏，不破坏
    const int ok_after = _CrtCheckMemory();
    std::cout << "    _CrtCheckMemory() 调用前 = " << (ok_before ? "堆完好" : "堆已损坏") << "\n";
    std::cout << "    _CrtCheckMemory() 调用后 = " << (ok_after ? "堆完好" : "堆已损坏") << "\n";
    std::cout << "    本文件泄漏了 128 字节，程序退出时会打印泄漏报告（见本程序最后几行输出）。\n";
    (void)saved_mode;  // 真实项目里通常不恢复（要一直保持检测）
#else
    std::cout << "  CRT 调试堆：非 MSVC 编译器不可用。\n";
    std::cout << "    等价方案：GCC/Clang 用 -fsanitize=address,leak；或用 valgrind。\n";
#endif
}

// ============================================================================
//  第 7 节：std::stacktrace（C++23）
// ----------------------------------------------------------------------------
//  作用：在**运行期**拿到调用栈，用来写「崩溃时自动记录调用栈」的日志。
//  典型用法：在 catch 块或信号处理里打印 stacktrace，让线上崩溃可追溯。
//
//  本机可用性（MSVC 14.51 + /std:c++20）：
//    只在 /std:c++latest（或 /std:c++23preview）下提供 <stacktrace>，
//    并且需要链接 dbghelp.lib。本项目统一用 /std:c++20，
//    所以这里用 __cpp_lib_stacktrace 判断，不支持就打印说明。
// ============================================================================

void demo_stacktrace() {
#if HAS_STACKTRACE
    std::cout << "  std::stacktrace：可用（本机库支持）\n";
    const std::stacktrace st = std::stacktrace::current();
    // 只打印前几帧，避免刷屏
    const std::size_t limit = st.size() < 8 ? st.size() : 8;
    for (std::size_t i = 0; i < limit; ++i) {
        std::cout << "    #" << i << " " << st[i].description() << " @ " << st[i].source_file() << ":"
                  << st[i].source_line() << "\n";
    }
#else
    std::cout << "  std::stacktrace：本编译器/标准下不可用\n";
    std::cout << "    说明：std::stacktrace 是 C++23 特性，MSVC 需要 /std:c++latest 并链接 dbghelp.lib。\n";
    std::cout << "    本项目统一 /std:c++20，因此示例里没有直接调用它。\n";
    std::cout << "    替代方案：\n";
    std::cout << "      - Windows：CaptureStackBackTrace + SymFromAddr（dbghelp）\n";
    std::cout << "      - Linux：backtrace() / libbacktrace / backward-cpp\n";
    std::cout << "      - 生产环境：直接上传 minidump（MiniDumpWriteDump），事后用 VS 打开\n";
#endif
}

// ============================================================================
//  第 8 节：二分注释法 —— 用「排除法」定位问题
// ----------------------------------------------------------------------------
//  症状：程序在某个不确定的时刻出错，或者结果莫名其妙。
//  思路（和 git bisect 是同一个思想）：
//
//    1) 先做一个**能稳定复现**的最小例子（这一步就解决了一半问题）；
//    2) 注释掉一半代码，看问题是否还在；
//        还在  -> 问题在剩下的一半里
//        消失  -> 问题在被注释掉的一半里
//    3) 重复，每轮把范围缩小一半；10 轮就能从上万行缩到一行；
//    4) 如果是「改了某个提交之后才开始出错」，用 `git bisect` 自动做这件事；
//    5) 如果问题只在 Release 出现：先怀疑未初始化内存、越界、数据竞争、UB，
//       开 /W4、ASan、/analyze，往往比读代码快得多。
//
//  下面用一个「被注释掉一半的代码」演示这个流程（结果已经算好，只打印过程）。
// ============================================================================

int suspicious_pipeline(int input) {
    int value = input;  // 输入 15，目标结果 26
    value += 7;         // 步骤 1：15 + 7 = 22
    value *= 3;         // 步骤 2：22 * 3 = 66（这里有 bug：不该乘）
    value -= 11;        // 步骤 3：66 - 11 = 55
    value /= 1;         // 步骤 4：55 / 1 = 55
    return value;
}

void demo_bisect_commenting() {
    std::cout << "  目标结果应为 26，实际得到 " << suspicious_pipeline(15)
              << "（输入 15：+7=22，*3=66，-11=55，/1=55）\n";
    std::cout << "  二分注释法流程（每一步只注释掉一半）：\n";
    std::cout << "    第 1 轮：注释掉步骤 3、4 -> 结果 66，和预期 26 不符 -> 问题在步骤 1、2 里\n";
    std::cout << "    第 2 轮：只保留步骤 1     -> 结果 22（正确）      -> 问题锁定在步骤 2\n";
    std::cout << "    => 4 行代码用 2 轮定位；上万行代码也只要十几轮（每轮砍一半）。\n";
    std::cout << "  工程实践：\n";
    std::cout << "    - 先写出「能稳定复现的最小例子」再动手，比盲目读代码快得多；\n";
    std::cout << "    - 用 git bisect 自动定位「哪个提交引入的」（需要每次提交都能编译）；\n";
    std::cout << "    - 只在 Release 崩：优先怀疑未初始化内存、越界、UB、数据竞争；\n";
    std::cout << "    - 只在一台机器崩：优先怀疑硬件差异（对齐、指令集）、环境差异、并发时序。\n";
}

// ============================================================================
//  第 9 节：故意错误版的「启用入口」
// ----------------------------------------------------------------------------
//  只有在 /DDEMO_ENABLE_BUGS 时才真的执行那些错误代码。
//  这样默认编译/运行完全安全，而学习者加上宏就能亲眼看到崩溃。
// ============================================================================

void run_planted_bugs() {
#ifdef DEMO_ENABLE_BUGS
    std::cout << "  *** DEMO_ENABLE_BUGS 已定义：下面会真的崩溃，请用调试器 F5 运行 ***\n";

    // 1) 数组越界：at() 抛异常，operator[] 越界（未定义行为）
    const std::vector<int> v{1, 2, 3};
    std::cout << "  [1] 先做一次越界写（破坏堆头）...\n";
    std::vector<int> writable{1, 2, 3};
    writable[5] = 42;  // 越界写：Debug 下可能触发断言/堆检查，Release 下静默破坏

    std::cout << "  [2] 空指针解引用...\n";
    Node* head = nullptr;                       // 空链表
    Node* found = find_node_bad(head, 42);      // 返回 nullptr
    std::cout << "      解引用结果 = " << deref_maybe_null_bad(found) << "\n";  // 崩在这里

    std::cout << "  [3] 迭代器失效...\n";
    (void)iterator_invalidation_bad();

    std::cout << "  [4] 悬垂指针...\n";
    int* dangling = dangling_pointer_bad();
    std::cout << "      *dangling = " << *dangling << "\n";  // use-after-return

    std::cout << "  [5] 无限递归（栈溢出，0xC00000FD）...\n";
    std::cout << "      " << infinite_recursion_bad(0) << "\n";

    (void)v;
#else
    std::cout << "  故意错误版：**未启用**（当前编译没有定义 DEMO_ENABLE_BUGS）\n";
    std::cout << "  想亲眼看到崩溃，用命令行加宏编译，然后在 Visual Studio 里 F5：\n";
    std::cout << "      cl /nologo /std:c++20 /EHsc /W4 /utf-8 /permissive- /Zi /DDEMO_ENABLE_BUGS \\\n";
    std::cout << "         05_debugging_techniques.cpp\n";
    std::cout << "  依次观察：越界写 -> 空指针（0xC0000005，地址接近 0）\n";
    std::cout << "            -> 迭代器失效（时对时错）-> 悬垂指针（use-after-return）\n";
    std::cout << "            -> 无限递归（0xC00000FD，调用栈上千层）。\n";
    std::cout << "  注意顺序：前一个错误可能破坏内存，让后面几个「看起来像别的问题」。\n";
#endif
}

}  // namespace

// ============================================================================
//  CRT 泄漏检测的官方推荐写法（放在 main 之前）
// ----------------------------------------------------------------------------
//  #define _CRTDBG_MAP_ALLOC
//  #include <cstdlib>
//  #include <crtdbg.h>
//  这三行的**顺序**很重要：_CRTDBG_MAP_ALLOC 必须在 <crtdbg.h> 之前，
//  而 <crtdbg.h> 又必须在其它会用 new/malloc 的头文件之前。
//  本项目为了让一个文件同时演示多种主题，把顺序放宽了 —— 真实项目里请严格按顺序写。
// ============================================================================

int main() {
    std::cout << "==== 05 调试技巧 ====\n\n";

    std::cout << "---- 1. 怎么读崩溃：现象与异常码 ----\n";
    print_crash_codes();
    std::cout << "\n";

    std::cout << "---- 2. 五类经典内存错误（安全版可运行，错误版需 /DDEMO_ENABLE_BUGS）----\n";
    std::cout << "  2.1 数组越界\n";
    {
        const std::vector<int> data{10, 20, 30};
        std::cout << "    安全版 at(1) = " << read_out_of_range_safe(data, 1) << "\n";
    }
    std::cout << "  2.2 空指针解引用\n";
    {
        Node* head = nullptr;
        Node* found = find_node_bad(head, 42);
        const std::optional<int> value = deref_maybe_null_safe(found);
        std::cout << "    安全版：find 返回 "
                  << (value ? std::to_string(*value) : std::string("nullopt"))
                  << "，调用者必须显式处理\n";
        const Node concrete{99, nullptr};
        std::cout << "    更工程化的写法：传引用（deref_never_null = " << deref_never_null(concrete)
                  << "），让「空」在类型上不可能出现\n";
    }
    std::cout << "  2.3 迭代器失效\n";
    {
        (void)iterator_invalidation_safe();
    }
    std::cout << "  2.4 悬垂指针\n";
    {
        const std::unique_ptr<int> owned = make_owned_int(42);
        std::cout << "    安全版：unique_ptr 持有所有权，*owned = " << *owned << "\n";
        std::cout << "    安全版：按值返回更简单，make_int_by_value(42) = " << make_int_by_value(42)
                  << "\n";
    }
    std::cout << "  2.5 栈溢出递归\n";
    {
        const std::optional<int> r = bounded_recursion(0);
        std::cout << "    安全版：bounded_recursion(0) = "
                  << (r ? std::to_string(*r) : std::string("到达深度上限，返回 nullopt")) << "\n";
        std::cout << "    更彻底的安全版：iterative_sum_to(100000) = " << iterative_sum_to(100000)
                  << "（迭代不消耗栈帧）\n";
    }
    std::cout << "\n";

    std::cout << "---- 3. 主动断点：__debugbreak / DebugBreak / 条件断点 / 数据断点 ----\n";
    demo_breakpoints();
    std::cout << "\n";

    std::cout << "---- 4. 让错误尽早暴露：at() 而不是 [] ----\n";
    demo_at_vs_subscript();
    std::cout << "\n";

    std::cout << "---- 5. AddressSanitizer（/fsanitize=address）----\n";
    demo_asan_detection();
    std::cout << "\n";

    std::cout << "---- 6. CRT 堆调试：泄漏与堆破坏 ----\n";
    demo_crt_debug_heap();
    std::cout << "\n";

    std::cout << "---- 7. std::stacktrace（C++23）----\n";
    demo_stacktrace();
    std::cout << "\n";

    std::cout << "---- 8. 二分注释法 ----\n";
    demo_bisect_commenting();
    std::cout << "\n";

    std::cout << "---- 9. 故意错误版的启用入口 ----\n";
    run_planted_bugs();
    std::cout << "\n";

#if defined(_MSC_VER)
    std::cout << "---- 退出前的手动泄漏检查 ----\n";
    std::cout << "  下面调用 _CrtDumpMemoryLeaks()；报告写到 stderr，形如：\n";
    std::cout << "    Detected memory leaks!\n";
    std::cout << "    {128} normal block at 0x..., 128 bytes long.\n";
    std::cout << "  说明：本机这份 CRT 的泄漏报告不含文件名和行号；\n";
    std::cout << "        要精确定位分配点，用 _malloc_dbg(..., __FILE__, __LINE__) 或 ASan。\n";
    std::cout.flush();
    _CrtDumpMemoryLeaks();  // 会在 stderr 打印上面那几行（这一行也是排查泄漏的标准动作）
    std::cout << "  （如未看到报告：在 VS 里 F5 运行时请看「输出」窗口的 Debug 通道）\n\n";
#endif

    std::cout << "==== 结论：让错误尽早暴露（at/断言/ASan），用二分法缩小范围 ====\n";
    return 0;
}
