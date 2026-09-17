# 第 1 章：C++ 基础与「反直觉」清单（MSVC / C++20 实战版）

这一章不是「C++ 入门教程」，而是一份**面向软件开发工程实践的纠错清单 + 心智模型说明**。

它的起点是仓库根目录那份原始笔记 `main.cpp`——里面有不少**事实性错误**，
以及大量「结论对但机理说不清」的表述。这一章做了两件事：

1. 把每一条错误**改成能编译、能运行的代码**，并用 `static_assert` / 实测输出把正确结论钉死。
2. 把「为什么错」讲清楚，因为**只有理解了机理，才能在下一个陌生场景里自己推导出正确答案**。

- 环境：Visual Studio / MSVC 19.x，x64（LLP64 数据模型）
- 编译参数：`/nologo /std:c++20 /EHsc /W4 /WX /utf-8 /permissive- /Zc:__cplusplus /diagnostics:caret`
- 构建：在 `cpp-notes` 目录下执行 `.\build.ps1 -Chapter 01-basics -Run`
- 本章 9 个 `.cpp` 全部在 `/W4 /WX` 下**零警告**通过，9 个可执行文件退出码均为 0

> **关于 `#pragma warning(push)/disable/pop`**
> 本章有若干处刻意保留了「错误写法」作为反面教材（`C4018`、`C4554`、`C4172`、`C4456`、`C4324`）。
> 为了让 `/WX` 下仍能编译，这些位置用 `push/pop` **局部**关掉了对应的警告，并在注释里写明理由。
> **工程原则：永远不要用全局 `/wd` 关警告**——那会把真正的问题一起藏起来。

---

## 1. 本章地图

| 文件 | 主题 | 一句话结论 |
| --- | --- | --- |
| `01_hello_and_types.cpp` | 程序结构、`NOMINMAX` 顺序陷阱、基本类型与 `sizeof`、整型提升与常用算术转换、字面量后缀、`const` / `constexpr` / `auto`、保留标识符 | `char` 变量在 C 和 C++ 里都恒为 1 字节；错的是「C 里字符常量 `'a'` 是 `int`」这件事被原笔记安到了 `char` 身上 |
| `02_integer_and_float.cpp` | 无符号回绕、有符号溢出 UB、无符号循环反模式、补码的模 `2^n` 模型、`0.1 + 0.2`、浮点比较、`numeric_limits` | 无符号溢出是**有定义的模运算**，有符号溢出是 **UB**；浮点 `==` 几乎必然失败，必须用带容差的比较 |
| `03_operators_and_control.cpp` | 运算符优先级陷阱、短路求值、`++i` / `i++`、`switch` 与 `[[fallthrough]]`、`goto` 跳出多层循环、范围 `for` 迭代器失效、`=` 与 `==` | 记不住优先级就加括号（零成本）；范围 `for` 是「值拷贝」语义的语法糖，改容器结构会让迭代器失效 |
| `04_arrays_pointers_refs.cpp` | 数组退化、`a` / `*a` / `&a[0]` / `&a`、二维数组元素类型、指针算术、引用 vs 指针、指针与 `const` 四种组合、`nullptr`、`std::array` / `std::span` / `string_view` | `a` 与 `&a` 的**值**相同但**类型**不同，所以 `a+1` 与 `&a+1` 的步长差很多；用 `nullptr`，别用 `NULL` 或 `0` |
| `05_string_and_io.cpp` | C 风格字符串风险、`printf` 转换规范完整拆解、格式串漏洞、`%n`、`snprintf`、`std::format`、`scanf` 三问题、`fgets`、`%lf` 差异、`cin >>` 与 `getline` 混用 | `printf` 的第一个参数是**格式模板**而不是数据；用户数据只能进参数位置。日常 C++ 用 `std::string` + `getline` + `std::format` |
| `06_functions_and_scope.cpp` | 声明与定义、默认参数静态绑定、传参决策表、返回局部变量的引用（UB）、重载决议、`inline` 真实含义、`static` 三种身份、作用域与生命周期 | 返回局部变量的引用/指针是 **UB**；`inline` 的语义是「允许多处定义」而不是「一定展开」 |
| `07_memory_model.cpp` | 四种存储区、`memcpy` 参数语义、`memcpy` / `std::copy` / `copy_backward`、`new` 与 `delete` 配对、泄漏与悬垂、容器与智能指针、`offsetof` 实测 padding | `memcpy(dest, src, count)`：**第一个是目的地，第二个是来源，第三个是字节数**；按大小降序排成员能减少 padding |
| `08_build_and_debug.cpp` | 四阶段与各阶段的典型错误、预定义宏、`static_assert`、`assert` 与 `NDEBUG`、`#pragma once`、日志宏、`#error` 与条件编译、调试手段速查 | 语法/类型错误是 `Cxxxx`（编译期），符号错误是 `LNKxxxx`（链接期）；能在编译期证明的就别拖到运行期 |
| `09_struct_layout.cpp` | padding 两条规则、三种手段实测偏移、成员顺序与大小、`alignas` / `alignof`、`#pragma pack`、序列化正解、位域 | `struct` 大小不等于成员大小之和；跨机器传输数据必须**逐字段显式编解码**，不能 `sizeof(struct) + write` |

---

## 2. 原笔记纠错清单

下表是本章的**核心资产**。左列是 `main.cpp` 里的原写法，中列是问题所在，右列是本章代码实际采用的正确写法。

### 2.1 预处理与构建

| 原写法 | 问题 | 正确写法 | 为什么 |
| --- | --- | --- | --- |
| `#include <Windows.h>` 之后才 `#define NOMINMAX` | `windows.h` 里的 `#ifndef NOMINMAX` 判断**发生在这一行之前**，`min` / `max` 函数式宏已经注入 | 放在 `#include <windows.h>` **之前**，或项目级 `/DNOMINMAX`（推荐） | 预处理是**从上到下的一次性文本替换**；`windows.h` 有 include guard，不会再看第二遍。宏一旦定义，`std::min` / `std::max` 会被整体替换掉而编译失败 |
| `#include <memory.h>` 取 `memcpy` | `<memory.h>` 是非标准的历史头 | `#include <cstring>`（`memcpy` / `memmove` / `strlen`），或 `#include <memory>`（C++ 智能指针，完全不同的东西） | C++ 标准把 `memcpy` 放在 `<cstring>`；`<memory.h>` 是 C 时代的实现细节，跨编译器不保证存在 |
| 依赖 MSVC 默认的 `__cplusplus` | MSVC 不写 `/Zc:__cplusplus` 时 `__cplusplus` **恒为 `199711L`** | 用 `_MSVC_LANG`，或编译时加 `/Zc:__cplusplus` | `_MSVC_LANG` 是 MSVC 自报的真实语言标准（C++20 = `202002L`）；本章 `08` 篇用 `static_assert(__cplusplus >= 202002L` 或 `_MSVC_LANG >= 202002L)` 把这条钉死 |
| 在头文件/多个 `.cpp` 里写函数定义 | 多个翻译单元各有一份同名定义 | 加 `inline`，或把定义移到某个 `.cpp` | ODR（单一定义规则）：函数可多次声明、只能定义一次。违反是链接期 `LNK2005` |
| 用 `#pragma once` 当「防重复链接」手段 | `#pragma once` 只防**同一翻译单元内**的重复包含 | 防重复链接要靠 `inline` / 移入 `.cpp` | 两件不同的事：一个是编译期文本问题，一个是链接期符号问题 |

### 2.2 类型与数值

| 原写法 | 问题 | 正确写法 | 为什么 |
| --- | --- | --- | --- |
| 「char 在 c 编译器中是 4 字节，char 在 cpp 编译器中是 1 字节」 | 把**字符常量**和**字符变量**混为一谈，结论只对了一半 | `char` **变量**在 C 和 C++ 里**都恒为 1 字节**；C 里**字符常量** `'a'` 的类型是 `int`（`sizeof('a') == 4`），C++ 里是 `char`（`sizeof('a') == 1`） | 标准规定 `sizeof(char) == 1`，两个语言都一样。原笔记想说的是「C 里字符常量占 4 字节」，却写成了「char 本身」 |
| `int _a[10];` 未初始化数组 | `_a` 是**保留标识符**：全局作用域下下划线开头的名字保留给实现，用户代码使用是 UB | `int a_uninitialized[10];`；需要全 0 就写 `int a[10]{};` 或 `int a[10] = {};` | 保留规则：任何位置含双下划线的名字、任何位置下划线 + 大写字母开头的名字、**全局命名空间**下划线开头的名字，都归实现所有。编译器不会报错，但这是 UB |
| 「对于负数 用模减去这数的整数，把结果直接用二进制表示，结果就为负数补码」 | 「减去这数的整数」**指代不清**，读者会以为是「减去整数部分」 | 把 n 位二进制看作**模 2^n 的整数集合**：最高位为 1 时值 = 二进制 − 2^n，范围 `[-2^(n-1), -1]`；求 `-x` 的补码 = `2^n - x` = 「各位取反再加 1」 | 两种说法等价：`~x + 1 == (2^n - 1 - x) + 1 == 2^n - x`。标准的关键词是「模 `2^n`」，说清楚模数就不歧义了。C++20 起标准明确规定有符号整数必须是补码 |
| 只列举了每种类型「能表示什么范围」，没提溢出 | 漏掉了最关键的**行为差异** | 无符号溢出 = **有定义的模 `2^N` 回绕**；有符号溢出 = **UB**（编译器有权假设它不发生） | 无符号回绕可以进 `static_assert`（`static_cast<std::uint8_t>(0U - 1U) == 255`）；有符号溢出没有任何保证，换编译器、换优化级别结果就变 |
| 「比 int 低的或等于的都会变成 int 作为结果，比 int 高的保留运算中最大的类型」 | 对**常用算术转换**的描述不准确：`int` 与 `unsigned int` 相遇时**不保留 `int`**；`long` 与 `unsigned int` 相遇时结果也不是「最大的那个」（两者同宽） | 分两步：**第 1 步整型提升**（比 `int` 低的提升为 `int` 或 `unsigned int`）；**第 2 步常用算术转换**（按 rank 比较，无符号 rank 不低于对方时，有符号方转无符号方） | 见 `01` 篇第 4 节的 `static_assert`：`decltype(u + l)` 是 `unsigned long`，而 `decltype(u + ll)` 是 **`long long`**——同样一个 `unsigned`，结果类型完全相反 |
| 「float 中间参与计算时是 double 计算结果」 | **错误**：`float + int` 的结果是 `float`，不是 `double` | 整型操作数转成**浮点操作数**的类型，所以 `float + int -> float`。真正提升为 `double` 只有两种情况：(1) 与 `double` / `long double` 混合；(2) **可变参数函数的默认实参提升**（`printf` 的 `%f` 能收 `float` 就是这个原因） | `static_assert(std::is_same_v<decltype(f + 1), float>)` 与 `static_assert(std::is_same_v<decltype(f + 1.0), double>)` 同时成立，直接证明「看的是另一个操作数」 |
| 「unsigned 不可用于 float 和 double」 | 表述错误且误导 | `unsigned int` 与 `float` 混合运算是合法的，结果类型是 `float`（整型被转换成 `float`） | 不存在「unsigned float」这种类型；`unsigned` 只能修饰整型类型。原笔记想说的应该是这件事，但写成了「不可用于」 |
| `auto x = m + d;` 注释只覆盖了单个情形 | 注释里「long 比 unsigned int 小」的说法在 Windows LLP64 下**不准确**：`long` 与 `unsigned int` **同宽**（都是 4 字节），正确的理由是「`long` 装不下 `unsigned int` 的负半区对应的全部无符号值」 | 结论仍是 `unsigned long`，但理由要写成「`long` 无法表示 `unsigned int` 的全部值（`unsigned int` 的最大值超出 `long` 的正半区），所以两侧都转成 `unsigned long`」 | `01` 篇里同时给出对照组：`unsigned int + long long` 得到 **`long long`**（因为 8 字节能装下 4 字节无符号的全部值）。同一段规则，结果完全不同 |
| `%f` 相关注释含糊 | 原笔记写「double 要用 `%lf` 输出有提升不用考虑，但是输入必须考虑！！！」——意思对但太含糊 | `printf`：`%f` 与 `%lf` **完全等价**（C99 起标准明确写了 `l` 对 `f` 无影响）；`scanf`：**必须区分**，读 `double` 用 `%lf`、读 `float` 用 `%f` | `printf` 里 `float` 已经被**默认实参提升**成 `double` 了，所以根本不存在「float 版本的 `%f`」，也解释了为什么没有 `%hf` 这种东西。而 `scanf` 收的是**指针**，它按你给的指示符决定写几个字节——写错就是 UB |
| 转义字符注释「`\b` 退格---删除上一个字符」 | `\b` 只是「把光标左移一格」，**不删除**已输出的字符 | `\b` 是退格（光标左移）；要真正擦除得输出 `\b \b`（回退、覆盖空格、再回退） | 输出流里没有「删除」这个操作，只有「写字符」和「移动光标」 |

### 2.3 输入输出与安全

| 原写法 | 问题 | 正确写法 | 为什么 |
| --- | --- | --- | --- |
| `void chapter(const char a[]) { printf(a); }` | **格式串漏洞**（format string vulnerability）：用户数据变成了格式模板 | `printf("%s", a);`，或直接用 `std::cout` / `std::format` | `printf` 的第一个参数是**格式模板**，不是「要打印的内容」。把用户输入当模板，等于让攻击者决定程序去栈上取几个参数、按什么类型解释：`%x` 泄露栈内容、`%s` 解引用任意地址、`%n` 往任意地址写值 |
| `printf(a)` 里 `a` 是 `char[20]`，注释说「发生隐性转换 从 `char*` 到 `const char*`」 | 转换描述不准确，而且这里真正的问题不是类型转换 | 数组实参传到 `const char*` 形参时发生的是**数组到指针的退化（decay）**，得到 `char*` 再隐式加 `const`；但**即使类型完全正确，`printf(用户数据)` 依然是漏洞** | 干扰项：把安全意识转移到「类型对不对」上，会掩盖真正的问题 |
| 「scanf 函数也会自动删除换行 空格 制表符等并结束当前输入」 | 措辞不准：`%s` 是**跳过前导空白**，然后在下一个空白**前**停下，并且**不消费**那个分隔符 | 分隔符（通常是换行符）会**留在输入流里**，必须显式清掉（`ignore` 或 `std::ws`） | 这正是后面 `cin >>` 与 `getline` 混用时 `getline` 读到空串的根因。把这个机理说清楚，就不会再被这个坑咬第二次 |
| 只提到 `scanf` 要限制宽度 | 没有点明**返回值**与「读不到空字符串」这两个问题 | 三件事一起管：检查返回值（成功赋值的变量个数）、`%s` 必须写 `%19s` 给 20 字节缓冲区留 `'\0'`、`%s` 无法读空字符串也无法读带空格的行 | 输入失败时变量还是旧值/未初始化值；缓冲区溢出是安全事故；要读整行只能用 `fgets` 或 `std::getline` |
| `scanf("%19s", a)` 之后紧跟 `getline` | 没意识到分隔符残留 | 三种修法：`cin.ignore(numeric_limits<streamsize>::max(), '\n')`、`getline(cin >> std::ws, line)`、或者**全程统一用 `getline`** 再自己解析（交互式程序最稳） | `operator>>` 遇到分隔符就停但**不消费**它；`getline` 一看第一个字符就是 `'\n'`，立刻返回空串 |
| `scanf("%s", str)`（`str` 是 `char[20]`，无宽度限制） | 缓冲区溢出 | `scanf("%19s", str)`；更好的做法是用 `std::string` + `std::getline` | 无宽度限制的 `%s` 会一直写下去，这是经典的栈溢出漏洞 |

### 2.4 数组、指针与内存

| 原写法 | 问题 | 正确写法 | 为什么 |
| --- | --- | --- | --- |
| `printf("a=%p\n", a)`，`a` 是 `char[2][3]` | `%p` 要求实参是 `void*`（或兼容指针），传 `char(*)[3]` 在 x64 上**经常碰巧能工作**，但严格来说是 UB | `printf("a=%p\n", static_cast<const void*>(a));` | 可移植性：不同 ABI 下指针表示可能不同。本章 `04` 篇所有地址打印都显式转换 |
| 「`int[5] a[10] 经过调整得到 int a[10][5]`」 | 方向对，但「调整」是什么没说，读者无法自己推导 | **声明模仿使用（declaration mimics use）**：先写「怎么用」的表达式 `a[i][j]`（类型 `int`），推断出 `a[i]` 的类型必须是 `int[5]`，于是 `a` 的类型是 `int[5][10]`（即 `int a[10][5]`） | 有了这条原则，`int c[10][6][5]` 的元素类型（`int[6][5]`）和元素个数（10）就是推出来的，不用死记 |
| 打印了 `a` / `*a` / `*a+1` / `&a[0]` / `&a[0]+1` / `a[0]` / `a[1]`，但没有解释步长差异 | 只看到「有些地址只差 1、有些差一行」，没有提炼出模型 | 关键区分**地址值**（全都是同一处）与**类型**（决定 `+1` 走多远）。`a` 和 `&a[0]` 完全等价，类型 `char(*)[3]`，步长 3；`*a` 和 `a[0]` 是 `char*`，步长 1；`&a` 类型 `char(*)[2][3]`，步长 6 | 指针算术：`p + n` 的地址 = `p` 的地址 + `n * sizeof(*p)`。`sizeof(*p)` 由 `p` 的**类型**决定，与地址值无关 |
| 「指针增量的算法是 `n*sizeof(*p) -> p+n`」 | 说法正确但没成体系 | 补全两条工程推论：(1) 数组名在**大多数**表达式里退化成首元素指针，但 `sizeof` / `&` / `decltype` / 绑定到数组引用 / 模板推导**不会**退化；(2) 传参退化后长度信息丢失，必须额外传长度，这正是 C 风格 API 的根源问题 | `04` 篇用 `const int (&arr)[N]` 模板保住长度，用 `std::span` 把「指针 + 长度」打包成一个参数，是这个问题的现代解法 |
| 没有涉及「引用返回」的风险 | 这是 C++ 最高频 UB 之一 | 绝不返回局部变量的引用/指针；需要新对象就按值返回（依赖 RVO / 移动语义），需要借用就让调用方保证对象活得够久 | 函数返回时局部变量（自动存储期）被销毁，返回的引用指向已失效内存。**危险之处在于它经常看起来是对的**，因为那块栈内存暂时还没被覆盖。MSVC 给 `C4172` |
| 没有区分存储区 | 「内存」是一个模糊的词，无法回答「这块内存什么时候失效」 | 分清四区：**栈**（局部变量、参数、返回地址，进/出作用域分配释放，快但有上限）、**堆**（`new` / `malloc`，手动或智能指针，灵活但慢且要管理）、**静态区**（全局与 `static` 变量，程序启动/结束时分配释放，生命周期 = 进程）、**常量区**（字符串字面量与 `const` 全局，**只读**，写入会崩） | 「谁拥有、什么时候释放」这个问题的答案，本质上就是「它在哪个区」。`07` 篇把四种变量的地址直接打印出来对照 |

### 2.5 函数、作用域与内存操作

| 原写法 | 问题 | 正确写法 | 为什么 |
| --- | --- | --- | --- |
| 注释写 `memory(目标地址, 数据来源地址, 赋值大小)`，示例写 `memcpy(a,b,sizeof(a))` | 三处问题：函数名写成 `memory`（实际是 `memcpy`）；注释与示例的名字容易读反；`memcpy(a,b,...)` 的语义全靠读者猜 | `void* memcpy(void* dest, const void* src, std::size_t count);`——**参数 1 是目的地，参数 2 是来源，参数 3 是字节数**，返回 `dest` | 本章 `07` 篇用 `int dest[3]` / `const int src[3]` 这样不会混淆的命名，并打印结果证明方向。写反了就是「拿目标去覆盖来源」，属于写错方向的经典事故 |
| `memcpy(a, b, sizeof(a))` 用于 `int[2]` | `sizeof` 的用法在这里碰巧正确，但没说明为什么危险 | 关键是**第三个参数永远是字节数**，不是元素个数；而且 `sizeof(指针)` 是 8 而不是数组长度 | `07` 篇并列打印 `sizeof(dest_array)`（数组，12 字节）与 `sizeof(src_ptr)`（指针，8 字节）：写成 `sizeof(指针)` 就只拷 8 字节，是极其常见的 bug |
| 把 `memcpy` 当作通用的「拷贝」手段 | `memcpy` 只做**字节搬运**，不调用构造/析构 | 对含 `std::string`、`std::vector`、`shared_ptr` 等成员的**非平凡可拷贝**类型，用 `std::copy` / 赋值 / 拷贝构造；`memcpy` 只留给内存缓冲区与 C 接口 | 对含 `std::string` 成员的对象做 `memcpy` 会得到两个对象共享同一块字符缓冲区，析构时**双重释放**。`08` 篇用 `std::is_trivially_copyable_v<T>` 做编译期判定 |
| 没有涉及 `new` / `delete` 的配对 | 这是最常见的资源管理错误 | `new T` 配 `delete p`；`new T[n]` 配 `delete[] p`。更好的做法是用 `std::vector` / `std::string` / `std::make_unique` 让标准库接管 | 数组形式的 `new` 会额外记录元素个数（以便逐个析构并回收正确大小），非数组形式的 `delete` 不会去读这个记录，于是行为未定义。Double free 在 Debug 堆上会直接报 heap corruption 并 abort |
| 「`break` 对 `switch` 不影响外边；`continue` 会影响外边循环」 | 结论基本对，但没说清「为什么」以及 `switch` 内部循环里的 `continue` 行为 | `break` 只跳出最近的一层 `switch` 或循环；`continue` 跳过当前迭代的剩余部分，对 `for` 会**执行更新表达式** | 记法：`break` 结束「这个结构」，`continue` 结束「这一轮」。在 `switch` 内部的 `for` 里写 `continue`，跳过的是那个 `for` 的当前迭代，不是 `switch` |
| 「短路求值」只是被当作优化技巧记录 | 短路求值是**语言保证**，不是可选的优化 | `&&` 左边为假则右边**不执行**；逻辑或左边为真则右边**不执行**。可以放心把「前提条件」放左边、「可能崩溃的操作」放右边 | `03` 篇用带副作用的函数实测：`false && f()` 的副作用计数为 0；`if (p != nullptr && *p > 0)` 因为短路而永远安全 |
| 缺「同一表达式内多次修改同一变量」的说明 | `i = i++;` 这类写法在 C/C++ 里是 **UB**，不是「结果不确定但能用」 | 拆成两条语句，顺序就明确了 | 标准没有规定求值顺序，编译器可以任意安排。不同编译器 / 不同优化级别下结果不同，这就是 UB 的定义 |

### 2.6 其它

| 原写法 | 问题 | 正确写法 | 为什么 |
| --- | --- | --- | --- |
| 关键字列表混排了 C 与 C++ 的关键字 | 列表里有 C 独有的 `_Bool` / `_Complex` / `_Imaginary` / `restrict`，C++ 独有的 `class` / `namespace` / `template` 等反而没列，还漏了保留标识符整类 | 不要背关键字表：真正的硬规则是「不能是关键字 + **不能是保留给实现的标识符**」 | 保留标识符才是真正会咬人的那类：编译器**不会报错**，但你的代码已经处于 UB 状态 |
| `printf` 的 `%p` / `%zu` 用法只有零散注释 | 没有成体系，容易写错 | `sizeof` 的结果是 `std::size_t`，**必须配 `%zu`**；写 `%d` 在 `/W4` 下会触发 `C4477`，甚至运行期读到垃圾；指针配 `%p` 且实参转成 `const void*` | 可变参数没有类型信息，格式串与实际参数类型不匹配时 `printf` 会按你**声称**的类型去读参数，读几个字节、怎么解释全由指示符决定 |
| `SetConsoleOutputCP(CP_UTF8)` 的调用与效果 | 原笔记调了两次（`SetConsoleCP` + `SetConsoleOutputCP`），但没说前提 | 源码必须保存为 **UTF-8**（最好带 BOM 或用 `/utf-8` 编译选项告编译器），否则源码本身在别的编码下就已经是乱码了 | 程序里的中文字符串来自源文件字节；如果源文件是 GBK 而编译器按 UTF-8 读，输出代码页设对了也没用 |
| 文件末尾没有 `return 0;` | C++ 里 `main` 省略 `return` 会隐式返回 0，合法但不够明确 | 显式写 `return 0;` | 本章 9 个文件都显式 `return 0;`，退出码是脚本化验证的直接依据 |
| 局部的辅助函数名叫 `chapter` / `title` | 起名过于通用，容易与标准库或其它文件的符号冲突 | 加不常见的前缀（本章用 `print_title` / `show_` / `demo_`），并优先放进**匿名命名空间** | `06` 篇记录了真实踩坑：名为 `title` 的辅助函数与 `<string>` 中的本地化函数冲突，名字查找 + ADL 会让调用变成二义性（`C2668`） |
| 未初始化数组直接读 | `int a[10];` 元素值**不确定**，读它是 UB | 需要全 0 就写 `int a[10]{};`（值初始化） | 空大括号 = 值初始化 = 全 0；`int a[10];` 只是「分配了存储」。本章 `01` 篇把两种写法并列打印 |

---

## 3. 重点难点详解

每个难点按 **错误直觉 → 正确模型 → 代码证据 → 工程建议** 四段展开。

### 3.1 整型提升与常用算术转换

- **错误直觉**：「比 `int` 低的或等于 `int` 的都会变成 `int`，比 `int` 高的保留运算中最大的类型。」
  这句话在 `unsigned int` 与 `int`、`long` 与 `unsigned int` 相遇时就错了。
- **正确模型**：规则分**两步**，顺序不能颠倒。
  1. **整型提升（integral promotion）**：`bool` / `char` / `signed char` / `unsigned char` / `short` 等
     rank 低于 `int` 的类型，若 `int` 能装下它的全部值就提升为 `int`，否则提升为 `unsigned int`。
     这一步**只看比 `int` 低的类型**。
  2. **常用算术转换（usual arithmetic conversions）**：
     - 任一操作数是 `long double` / `double` / `float`，另一个就转过去；
     - 否则先做整型提升，再按 rank 比较：无符号方 rank **不低于**有符号方，则**有符号方转成无符号方**；
     - 否则若「有符号类型能表示无符号类型的全部值」，则无符号方转有符号方；
     - 否则两者都转成「有符号类型对应的无符号类型」。
- **代码证据**（`01_hello_and_types.cpp` 第 4 节，全部是编译期断言）：

  ```cpp
  static_assert(std::is_same_v<decltype(c1 + s1), int>,  "char + short -> int");
  static_assert(std::is_same_v<decltype(u1 + l1), unsigned long>,
                "unsigned int + long -> unsigned long");
  static_assert(std::is_same_v<decltype(u1 + ll1), long long>,
                "unsigned int + long long -> long long");
  static_assert(std::is_same_v<decltype(negative + one), unsigned int>);
  static_assert(std::is_same_v<decltype(f1 + 1), float>,  "float + int -> float");
  static_assert(std::is_same_v<decltype(f1 + 1.0), double>);
  ```

  同一个 `unsigned int`，加 `long` 得到 **`unsigned long`**，加 `long long` 得到 **`long long`**——
  结果类型的符号性完全相反。这就是「不许背结论、必须记规则」的最好证明。
- **工程建议**：
  - 混合整数类型运算前，**先显式 `static_cast` 到同一个类型**，让意图写在代码里。
  - 需要「必须 4 字节」的语义时用 `<cstdint>` 的定宽类型（`std::int32_t` / `std::uint64_t`），
    不要依赖 `int` / `long` 的巧合宽度。
  - 遇到 `C4018`（signed/unsigned mismatch）时，**不要关警告**，要改代码。

### 3.2 无符号回绕陷阱

- **错误直觉**：「无符号数减到 0 以下会出错/会变成负数」或者「回绕和溢出一样危险」。
- **正确模型**：无符号整数的运算结果**按模 `2^N` 回绕**（`N` 是该类型的位宽），
  这是标准**精确定义**的算术，不是错误、更不是 UB。
  真正危险的是**有符号与无符号混用**：有符号方被隐式转成无符号方之后，负值变成极大的正值，
  于是比较、循环、边界检查全部失效。
- **代码证据**（`02_integer_and_float.cpp` 第 1、2、3 节）：

  ```cpp
  constexpr std::uint8_t wrap_evidence = static_cast<std::uint8_t>(0U - 1U);
  static_assert(wrap_evidence == 255, "0 - 1 == 2^8 - 1，编译期即可证明");
  ```

  ```text
  (uint8_t)250 + 10 = 4        <- 260 按模 256 回绕
  (uint8_t)0 - 1    = 255      <- 不是 -1，也不是「错误」
  UINT_MAX + 1u     = 0        <- 完全合法
  ```

  混用陷阱的实测（`02` 篇第 3 节，`#pragma warning(push)/disable : 4018/pop` 内）：

  ```text
  negative_index = -1，直接比较 negative_index < text.size() 得到 false
  <- 因为 -1 被转成 SIZE_MAX，所以「-1 < 5」居然是假
  ```

- **工程建议**：
  - **「能用无符号就用无符号」是错的**。只有做位运算 / 表示位模式时才用无符号；
    表示数量、下标、金额这类「有数学含义」的值时优先用有符号类型。
  - 倒序循环不要写 `for (unsigned i = n - 1; i >= 0; --i)`（`i >= 0` 恒真，死循环且越界）。
    三种正确写法：用有符号下标；用 `rbegin()` / `rend()`；真要用无符号就写 `for (i = n; i != 0; --i)`。
  - 与 `size()` 比较的下标，要么声明成 `std::size_t`，要么写成 `i >= 0 && static_cast<std::size_t>(i) < v.size()`。
  - 需要检测有符号溢出时用编译器内建（GCC/Clang 的 `__builtin_add_overflow`）或先做范围检查，
    **不要依赖 UB 的「实际表现」**。

### 3.3 浮点精度与比较

- **错误直觉**：「计算机算 `0.1 + 0.2` 结果就是 `0.3`，出问题说明程序有 bug」，
  或者反过来的「浮点就是不准，量化误差没法避免，所以不用管」。
- **正确模型**：`0.1` 这样的十进制小数**无法写成有限位二进制分数**（就像 `1/3` 在十进制里写不完），
  存进去时就已经带了一个极小的舍入误差。IEEE-754 双精度是「符号 + 11 位指数 + 52 位尾数」，
  两个带误差的数相加，误差按 ulp 累积。所以 `0.1 + 0.2` 得到 `0.30000000000000004`，
  而 `0.3` 本身又是**另一个**近似值。这是**表示误差**，不是**计算错误**——
  同样的表达式在任何合规编译器上结果都一样。
- **代码证据**（`02_integer_and_float.cpp` 第 5 节）：

  ```text
  0.1 + 0.2 = 0.30000000000000004
  0.3       = 0.3
  两者是否 == : false
  累加 0.1f 十万次（float ）: 误差约 -6.5e-3
  累加 0.1  十万次（double）: 误差小好几个数量级
  std::nextafter(1.0, 2.0) - 1.0 = 2.22e-16   <- 1.0 处的 1 ulp
  1.0 + 1e-17 == 1.0 ? true                   <- 小于半个 ulp 的增量被吸收
  0.0 / 0.0 = nan   isnan = true              <- IEEE-754 定义，不是 UB
  1.0 / 0.0 = inf   isinf = true              <- 整数除零才是 UB
  ```

  比较函数同时使用绝对误差与相对误差（只用绝对误差在大数值时失效，只用相对误差在接近 0 时失效）：

  ```cpp
  bool nearly_equal(double a, double b, double abs_eps = 1e-12, double rel_eps = 1e-9) {
      const double diff = std::fabs(a - b);
      if (diff <= abs_eps) { return true; }
      return diff <= rel_eps * std::fmax(std::fabs(a), std::fabs(b));
  }
  ```

- **工程建议**：
  - **绝不用 `==` 比较浮点数**。要么用带容差的比较，要么改用整数（金额用「分」为单位的整数）。
  - 金融、科学计算一律用 `double`，不要用 `float`（`float` 只有约 7 位有效十进制数字）。
  - 判断 NaN 用 `std::isnan`，不要用 `x == x` 之外的技巧；记住 `NaN != NaN` 恒为真。
  - 大量累加时考虑 Kahan 求和或按量级排序后再加，别指望误差自己消失。
  - 用 `std::numeric_limits<T>::epsilon()` 表达「机器精度」，用 `digits10` / `max_digits10`
    表达「能安全打印多少位」，别硬编码魔法数字。

### 3.4 数组退化为指针（`a` / `*a` / `&a[0]` / `&a` 的区别）

- **错误直觉**：「数组就是指针」「`a` 和 `&a` 是一回事」。
  原笔记打印了这组地址，但没解释为什么有的只差 1、有的差一整行。
- **正确模型**：必须把两个概念**分开**。
  - **地址值**：`a`、`*a`、`&a[0]`、`a[0]`、`&a` **全都是同一个地址**（数组首字节的地址）。
  - **类型**：决定 `+1` 走多远，也就是指针算术的步长。规则是 `p + n` 的地址 = `p` 的地址 + `n * sizeof(*p)`。

  以 `char a[2][3]` 为例：

  | 表达式 | 类型 | `sizeof(*p)` | `+1` 的步长 | 含义 |
  | --- | --- | --- | --- | --- |
  | `a` | `char(*)[3]` | 3 | 3 字节 | 退化成「首元素（一个 `char[3]`）的指针」 |
  | `*a` | `char*` | 1 | 1 字节 | 解引用一次，指向第一个字符 |
  | `a[0]` | `char*` | 1 | 1 字节 | 就是 `*a` 的另一种写法 |
  | `&a[0]` | `char(*)[3]` | 3 | 3 字节 | 与 `a` **完全等价**（同值同类型） |
  | `&a` | `char(*)[2][3]` | 6 | 6 字节 | 「整个数组的指针」，不是一个指针的指针 |

  另外，**退化不是无条件发生的**。会退化：赋值给指针、传给函数参数、参与算术。
  **不会退化**：`sizeof`、`&`（取地址）、`decltype`、绑定到数组引用、模板推导。
- **代码证据**（`04_arrays_pointers_refs.cpp` 第 1、2 节）：

  ```cpp
  static_assert(sizeof(matrix[0]) == 5 * sizeof(int), "matrix[0] 是一个 int[5]");
  static_assert(std::is_same_v<decltype(matrix[0]), int(&)[5]>);
  static_assert(sizeof(grid) == 6);    // &  不退化：整个数组 6 字节
  static_assert(sizeof(*grid) == 3);   // 一行 3 字节
  static_assert(sizeof(**grid) == 1);  // 一个字符
  ```

  步长是用指针差**实测**出来的（避免编译器把地址运算折叠掉）：

  ```text
  a + 1      前进 3 字节（= sizeof(*a) = sizeof(char[3])）
  *a + 1     前进 1 字节（= sizeof(**a) = sizeof(char)）
  &a[0] + 1  前进 3 字节（与 a + 1 相同）
  &a + 1     前进 6 字节（= sizeof(a) 整个数组）
  ```

  `sizeof(numbers) = 16` 而 `sizeof(decayed) = 8`，是「退化会丢长度、`sizeof` 不退化」的直接对照。
- **工程建议**：
  - 二维数组传参时**不要以为元素类型是指针**：`int m[10][5]` 的元素类型是 `int[5]`。
  - 需要保留长度就用 `template <std::size_t N> void f(const int (&arr)[N])`，
    或者直接用 `std::array<T, N>`（长度是类型的一部分）。
  - 传「指针 + 长度」的场景用 `std::span<const T>`（C++20），一次传完，还不会丢长度。
  - 只读字符串参数优先用 `std::string_view`（借用语义、零拷贝），但要注意被借的数据必须活得更久。

### 3.5 指针与 `const` 的四种组合

- **错误直觉**：「`const int* p` 和 `int* const p` 差不多，`const` 就是「不能改」」。
- **正确模型**：从右往左读，**`const` 修饰它左边最近的那个类型**（`const` 写在类型名前或后等价）。

  | 写法 | 读法 | 能否改指向 | 能否改值 |
  | --- | --- | --- | --- |
  | `const int* p` | 「`p` 是**指向 `const int`** 的指针」 | 能（`p = &y` 合法） | 不能（`*p = 1` 非法） |
  | `int const* p` | 同上（与上一行完全等价） | 能 | 不能 |
  | `int* const p` | 「`p` 是 **const 指针**，指向 `int`」 | 不能 | 能 |
  | `const int* const p` | 两者都是 const | 不能 | 不能 |

  转换方向也遵循同一模型：**非 const 指针赋给 const 指针是允许的**（增加限制），
  反过来（`const int*` 到 `int*`）必须 `const_cast`。
- **代码证据**（`04_arrays_pointers_refs.cpp` 第 6 节）：

  ```cpp
  const int* ptr_to_const = &x;
  ptr_to_const = &y;        // 合法：可以改指向
  // *ptr_to_const = 5;     // 非法：不能改值
  int* const const_ptr = &x;
  *const_ptr = 111;         // 合法：可以改值（x 变成 111）
  // const_ptr = &y;        // 非法：不能改指向
  const int* const both = &y;   // 两者都不可改
  ```
- **工程建议**：
  - **能加 `const` 就加**。`const` 是最便宜的自文档化工具，而且它让编译器帮你查错。
  - 函数签名里用 `const T&` 表达「我只看、不改、不延长你的生命期」——这是 C++ 最常用的性能习惯
    （`04` 篇实测：300 字符的 `std::string` 按值传要多一次深拷贝，按 `const&` 是零拷贝）。
  - 唯一「只读参数」的例外是**小标量**（`int` / `double` / 指针）：按值传比解引用还便宜，不必加 `const&`。
  - 不要写依赖 `sizeof(T&)` 的代码：语言层面引用「不是对象」，标准没有规定它的大小
    （MSVC 给 `sizeof(int&) == sizeof(int)`，GCC/Clang 常给指针大小，两套行为都合规）。

### 3.6 `printf` 格式串安全

- **错误直觉**：「`printf` 的第一个参数是**要打印的内容**」，所以 `printf(user_text)` 天经地义。
- **正确模型**：`printf` 的第一个参数是**格式模板**——它不是数据，它是**代码级别的说明**。
  把用户输入当模板，等于让攻击者决定程序：
  - 去栈上取几个参数、按什么类型解释（`%x` **泄露**栈内容）；
  - 把栈上的整数当**地址**去解引用（`%s` **读**任意内存，通常直接崩溃）；
  - 把「已输出字符数」写回你给的指针（`%n` **写**任意内存，直接改写返回地址或函数指针）。
- **代码证据**（`05_string_and_io.cpp` 第 4、5 节）：

  ```cpp
  void chapter_unsafe(const char* text) {
      std::printf(text/*, 没有参数可传*/);   // 【漏洞】text 被当成格式串
  }
  void chapter_safe(const char* text) {
      std::printf("%s", text);               // 【正确】内容进参数位置
  }

  const char* user_input = "这是一段用户数据 %d";
  chapter_unsafe(user_input);   // 内容里的 %d 被当成格式规范，printf 去栈上取了一个整数打印
  chapter_safe(user_input);     // 原样输出
  ```

  关于 `%n`：MSVC 的 CRT **默认禁用** `%n`，不显式打开就使用会直接 abort（退出码 3）。
  示例用 `_set_printf_count_output(1)` 打开以便观察，同时打印出「默认关闭」这件事本身就是微软对漏洞的回应。
- **工程建议**：
  - **格式串必须是字面量**；用户数据只能出现在**参数**位置。
  - 现代替代：`std::format`（C++20）的格式串必须是**编译期常量**，写错在编译期就失败；
    类型和参数个数都在编译期检查，从根本上消灭这类漏洞。
  - 安全敏感项目里禁用 `%n`；真需要计数就自己累加。
  - 打开编译器的格式串检查（MSVC 默认对非常量格式串给警告；GCC/Clang 用 `-Wformat-security`）。
  - 用 `snprintf` 而不是 `sprintf`：它的返回值是「假如缓冲区足够大本应写入的字符数」，
    `返回值 >= 容量` 就说明发生了截断。**注意变量名不要取 `small` / `near` / `far` / `min` / `max`**
    ——`windows.h` 里它们是宏。

### 3.7 `cin >>` 与 `getline` 混用

- **错误直觉**：「`cin >>` 读完数字，`getline` 自然读下一行」。
- **正确模型**：`operator>>` 遇到分隔符（空白）就停，但**不消费**那个分隔符——
  换行符还躺在流里。接着 `getline` 一看第一个字符就是 `'\n'`，
  立刻认为「这一行是空的」并返回。看上去像是「`getline` 被跳过了」，
  实际上是它**读到了一个空行**。
- **代码证据**（`05_string_and_io.cpp` 第 9、10 节）：

  ```cpp
  std::istringstream fake_input("42\nhello world\n");
  int number = 0;
  fake_input >> number;          // number = 42
  std::string rest;
  std::getline(fake_input, rest);  // rest 是空串（长度 0），因为吃到了残留的 '\n'
  ```

  同一个文件里 `scanf` 与 `fgets` 的混用给出了**另一种同源现象**：
  `scanf("%19s", buf)` 读走单词后把行尾换行留在流里，紧接着的 `fgets` 读到的是一行「空内容」。
  两种情形机理完全一样。
- **工程建议**：
  - 修法一：`std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');`
  - 修法二：`std::getline(std::cin >> std::ws, line);`（`std::ws` 吃掉前导空白，含换行）
  - 修法三（**最稳，推荐给交互式程序**）：**全程统一用 `getline`**，数字用 `std::from_chars` / `std::stoi` 解析。
  - 还要检查流状态：`cin >> x` 失败后流进入失败状态，**后续所有输入都会失效**，
    必须 `cin.clear()` 并清掉残留输入（`05` 篇用 `istringstream` 喂 `"abc"` 演示了 `fail()` 为 `true`）。
  - `fgets` 的 `size` 永远写 `sizeof buf`，不要手写数字；Windows 文本换行是 CRLF，记得同时去掉 `'\n'` 和 `'\r'`。

### 3.8 返回局部变量引用（悬垂引用）

- **错误直觉**：「引用比指针安全，返回引用不会有问题」。
- **正确模型**：函数返回时，**自动存储期**的局部变量被销毁。返回它的引用/指针，
  得到的是一个指向已失效内存的句柄，后续使用是 **UB**。
  危险之处在于它**经常看起来是对的**——那块栈内存暂时还没被其它调用覆盖。
- **代码证据**（`06_functions_and_scope.cpp` 第 4 节）：

  ```cpp
  #pragma warning(push)
  #pragma warning(disable : 4172)  // 只为保留反面教材；真实项目看到 C4172 一定要改代码
  int& return_reference_to_local() {
      int local = 42;
      return local;   // 【错误】local 在函数返回时销毁
  }
  int* return_pointer_to_local() {
      int local = 7;
      return &local;  // 【错误】同上
  }
  #pragma warning(pop)
  ```

  三种正确做法：

  ```cpp
  int return_by_value();                                   // A 按值返回（小对象/可拷贝类型）
  const std::string& longer_of(const std::string& a, const std::string& b);  // B 借用，调用方保证生命期
  std::vector<int> make_range(int count);                  // C 返回新对象，依赖 RVO / 移动语义
  ```

  还有一层易混点：对返回引用的函数，`auto x = f()` 会**拷贝**，`auto& x = f()` 才是引用。
  两者都能通过编译，但语义和开销完全不同。
- **工程建议**：
  - 看到 `C4172`（returning address of local variable or temporary）**必须改代码**，而不是关警告。
  - 决策顺序：能按值返回就按值返回（有 RVO / 移动语义兜底，通常零拷贝）；
    要借用就返回引用/指针，但必须能证明对象活得比调用方久；否则返回智能指针。
  - 临时对象绑定到 `const` 引用会**延长寿命**（`for (int v : make_vector())` 是安全的），
    但寿命延长**只对「直接绑定」生效**：对「从函数返回的引用」、对「聚合初始化里的引用成员」都不生效。
  - 同理，`auto` 按值推导会**丢掉引用和顶层 `const`**（`01` 篇有 `static_assert` 证明），
    遍历大对象必须写 `const auto&`。

### 3.9 `static` 的三种含义

- **错误直觉**：「`static` 就是静态的，到处意思差不多」。
- **正确模型**：同一个关键字在三个位置有**三种完全不同的语义**。

  | 位置 | 含义 | 关键词 | 现代替代 |
  | --- | --- | --- | --- |
  | 命名空间作用域的函数/变量 | **内部链接**（只有本 `.cpp` 可见） | 不导出符号，不会与其它文件的同名符号冲突 | **匿名命名空间**（推荐，还能放类型定义，对模板/类更友好） |
  | 函数内的局部变量 | **静态存储期**，只初始化一次，生命周期到程序结束 | 初始化在 C++11 起**线程安全**（magic static） | 需要「只初始化一次」又不想引入全局对象时很合适 |
  | 类的静态成员 | **属于类而不属于对象** | 类内声明 + 类外定义（`Counter::total = 0;`） | C++17 起写 `inline static int total = 0;` 就不用类外定义；`static constexpr` 成员隐式 `inline` |

- **代码证据**（`06_functions_and_scope.cpp` 第 7 节）：

  ```cpp
  static int helper_with_internal_linkage() { return 1; }        // (1) 内部链接
  int helper_inside_anonymous_namespace() { return 2; }          // (1) 匿名命名空间（推荐）

  int next_sequence_number() { static int counter = 0; ++counter; return counter; }  // (2)
  int next_local_number()     { int counter = 0;        ++counter; return counter; }  // 对照

  struct Counter {
      static int total;                 // (3) 类内声明
      static constexpr int kMax = 100;  // C++17 起隐式 inline
      static int bump() { return ++total; }
  };
  int Counter::total = 0;               // (3) 类外定义（只能出现一次，放 .cpp）
  ```

  输出对照：`static` 局部变量连续调用三次得到 `1 2 3`；
  普通局部变量连续调用三次得到 `1 1 1`。
- **工程建议**：
  - 新代码用**匿名命名空间**替代 `static` 自由函数/变量。
    **注意不要重复写 `namespace {`** 来「补充」内容——那会开出**嵌套**的匿名命名空间，
    和外面的匿名命名空间是两个不同的命名空间，会造成「同名函数两个候选」的 `C2668`。
  - **声明和定义必须在同一个命名空间里**。`06` 篇记录了这个真实踩坑：
    函数在文件开头有全局作用域的声明，若把定义塞进匿名命名空间，
    就变成两个不同函数，调用点直接二义性。
  - `static` 局部变量的**首次初始化**是线程安全的，但**初始化之后的读写仍需你自己加锁**。
  - 跨翻译单元的全局对象**初始化顺序不确定**，不要写依赖「另一个 `.cpp` 里的全局对象已经构造好」的代码。

### 3.10 栈 / 堆 / 静态区 / 常量区

- **错误直觉**：「内存就是内存，`new` 出来的和局部变量差不多」。
- **正确模型**：四种存储区各有明确的分配时机、生命周期与访问特征。
  回答「谁拥有它、什么时候释放」本质上就是回答「它在哪个区」。

  | 区域 | 存放什么 | 何时分配/释放 | 特点 |
  | --- | --- | --- | --- |
  | 栈 | 局部变量、函数参数、返回地址 | 进入/离开作用域 | 快、有大小上限 |
  | 堆 | `new` / `malloc` 出来的对象 | 手动或智能指针 | 灵活、慢、要管理 |
  | 静态区 | 全局变量、`static` 变量 | 程序启动/结束时 | 生命周期 = 进程；未初始化的在 `.bss` 自动清零 |
  | 常量区 | 字符串字面量、`const` 全局 | 程序启动/结束时 | 只读，写入会崩 |

- **代码证据**（`07_memory_model.cpp` 第 1 节）：四类变量的地址被直接打印出来对照；
  同函数内 `&stack_a - &stack_b`（x64 上通常为负）说明**栈向低地址增长**；
  `global_uninitialized` 的值是 0，证明 `.bss` 段自动清零。
- **工程建议**：
  - 悬垂指针的三种典型来源，本质上就是「对象已经离开它所在的区」：
    (1) `delete` 之后继续用（堆，use-after-free）；
    (2) 指向局部变量（栈，见 3.8）；
    (3) 容器扩容后旧的迭代器/指针（堆上的缓冲区被换掉了，见 3.11）。
  - 防御习惯：`delete` 之后把指针置空；**更好的做法是根本不用裸指针拥有对象**。
  - 所有权选择顺序：**栈对象 > `unique_ptr` > `shared_ptr` > 裸 `new`**。
    绝大多数代码用不到裸 `new`；`shared_ptr` 有原子引用计数开销，慎用；
    循环引用时其中一边用 `weak_ptr` 观察。
  - 排查泄漏：Visual Studio 诊断工具的内存快照对比、CRT 调试堆 `_CrtDumpMemoryLeaks()`、
    AddressSanitizer（`/fsanitize=address`）。

### 3.11 对齐与 padding

- **错误直觉**：「`struct` 的大小等于成员大小之和」「成员顺序无关紧要」。
- **正确模型**：只有**两条**规则，理解之后所有「看不见的字节」都能自己推出来。
  - **规则 A（成员对齐）**：每个成员的**起始偏移**必须是该成员**对齐值**的整数倍。
    对齐值通常等于 `sizeof`（`char` = 1、`int` = 4、`double` = 8），也可以用 `alignas` 改。
  - **规则 B（整体补齐）**：结构体的**总大小**必须是「最大成员对齐值」的整数倍。
    这样 `struct S arr[10];` 里每个元素都满足规则 A。

  这两条规则还解释了一个现象：**「按大小降序排成员」并不能保证省空间**，
  因为尾部补齐可能把好处吃掉。
- **代码证据**（`07_memory_model.cpp` 第 7 节、`09_struct_layout.cpp`）：

  ```cpp
  struct LayoutWasteful { char flag; double value; int count; };  // 1 + 7pad + 8 + 4 -> 24
  struct LayoutCompact  { double value; int count; char flag; };  // 8 + 4 + 1 + 3tail -> 16
  static_assert(offsetof(LayoutCompact, value) == 0);
  static_assert(offsetof(LayoutCompact, count) == 8);
  static_assert(offsetof(LayoutCompact, flag)  == 12);
  static_assert(sizeof(LayoutCompact) == 16, "尾部补齐到 8 的倍数");
  ```

  反直觉的对照组（`09` 篇第 1 节）：只有两个成员时，两种顺序**一样大**——
  `struct {char, double}` 是 16，`struct {double, char}` 也是 16，尾部补齐把差异抹平了。

  ```text
  PoorlyPacked {char,double,char,int,char,double} sizeof = 40
  WellPacked   {double,double,int,char,char,char} sizeof = 24   <- 省 16 字节（40%）
  ```

  三种实测手段（`09` 篇第 2 节）：
  1. `offsetof`（编译期常量，最推荐，可进 `static_assert`）；
  2. 成员地址相减（运行期实测，用来验证 `offsetof` 的结论）；
  3. 把结构体当 `unsigned char[]` 打印十六进制（调试内存布局最直观）——
     给 `char c = 0x11; int i = 0x22334455;` 之后字节序列是 `11 00 00 00 55 44 33 22`：
     第一个 `11` 是 `c`，接着 3 个 `00` 就是 padding，然后是 `i` 的小端序字节。
- **工程建议**：
  - 手工重排成员只在「结构体数量巨大」或「结构体非常热（缓存敏感）」时才值得做；
    一旦这么做，**必须写注释说明顺序是有意的**，否则后人重排会踩回来。
  - 二进制序列化协议（网络包、文件格式）**绝不能依赖 padding**：padding 的内容**不确定**
    （可能泄露内存里的旧数据），字节序也不一致，升级协议时字段一动全废。
    正确做法是**逐字段显式编解码**（`09` 篇给了 `write_u32_le` / `read_u32_le` 的小端实现），
    或者用 `#pragma pack` + 显式字节序处理 + 专门的序列化方案（protobuf / flatbuffers）。
  - `#pragma pack` 的代价要记住：非对齐访问在 x86/x64 上只是变慢，**部分 ARM 上会直接异常**；
    结构体的 `alignof` 变成 1，当数组元素会失去对齐保证。
  - `alignas` 的典型用途是避免多线程**伪共享（false sharing）**：把频繁被不同线程写入的计数器
    各自放到独立的缓存行上（`alignas(64)`，x64 常见缓存行 64 字节）。
    代价是每个计数器多占 56 字节——**只在 profiler 指出伪共享时才做**。
  - `alignas` 造成的填充会让 MSVC 报 `C4324`（structure was padded due to alignment specifier）。
    **这不是错误**，是编译器在确认「你确实造成了填充」。用 `push/pop` 局部关掉并写清理由。
  - 位域（bit-field）只适合硬件寄存器映射或「一个字节拆成几个标志」的场景：
    位序是**实现定义**的（不能用于跨平台协议）、**不能取地址**（`&flags.readable` 非法）。
    可移植的替代方案是用一个 `std::uint32_t` + 掩码/移位，或 `std::bitset`。

### 3.12 迭代器失效与范围 `for`

- **错误直觉**：「范围 `for` 是编译器魔法，边遍历边删应该也行」。
- **正确模型**：范围 `for` 展开后等价于用 `begin()` / `end()` 迭代器遍历：

  ```cpp
  auto __begin = v.begin(); auto __end = v.end();
  for (; __begin != __end; ++__begin) { auto& x = *__begin; /* 循环体 */ }
  ```

  一旦 `push_back` 触发扩容，所有迭代器（**包括被缓存的 `__end`**）全部失效，
  继续用它们就是 UB——可能崩溃，也可能「看起来正常」。
- **代码证据**（`03_operators_and_control.cpp` 第 6 节）：三种安全写法并列演示。
  1. 先遍历、把要加的元素收集到另一个容器，遍历结束后统一插入；
  2. 确实要边遍历边删时，用迭代器并接收 `erase` 的返回值（`it = data.erase(it);`）；
  3. C++20 的 `std::erase_if(data, pred)` 一行搞定（推荐）。
  另外：**只「修改元素的值」是安全的**，只有改「容器大小/结构」才失效——
  但前提是遍历时写 `int& v` 而不是 `int v`。
- **工程建议**：
  - 遍历时不要改容器结构。要改就先收集（两阶段），或用 `erase_if` / `remove_if` + `erase`。
  - 遍历大对象用 `const auto&`（`auto` 会拷贝，且丢引用）。
  - 记住「失效」是**运行期**才可能显形的，Debug 下 STL 的迭代器调试会帮你抓住，Release 下不会。

---

## 4. 实用开发习惯与技巧

### 4.1 编译选项

本章使用的构建参数（`build.ps1` 里统一设置）：

| 选项 | 作用 | 为什么工程里必须有 |
| --- | --- | --- |
| `/std:c++20` | 语言标准 | 决定哪些特性可用（`std::format` / `span` / `ranges` 都要 C++20） |
| `/W4` | 高警告级别 | `/W4` 能发现的很多问题（`C4018`、`C4244`、`C4101`）在低级别下完全不报 |
| `/WX` | 警告即错误 | **提交前必开**。警告是「将来会出 bug 的代码」，不是噪音 |
| `/utf-8` | 源码按 UTF-8 解析 | 含中文注释/字符串的源文件必备，否则编码问题会在别人机器上爆发 |
| `/permissive-` | 关闭宽松模式 | 强制标准两阶段名字查找，让代码在 GCC/Clang 下也能编过 |
| `/Zc:__cplusplus` | 让 `__cplusplus` 报真实值 | MSVC 默认恒为 `199711L`，会让大量特性检测代码失效 |
| `/EHsc` | 标准 C++ 异常模型 | 与标准一致，避免 `catch(...)` 语义差异 |
| `/diagnostics:caret` | 用插入符指出出错位置 | 大幅降低读报错的成本 |
| `/bigobj` | 允许更多节 | 大文件/模板重的文件必需 |
| `/MDd` + `/Zi` | 调试版 CRT + 调试信息 | Debug 下走调试堆，能抓到越界与重复释放 |
| `/FS` | 串行化 PDB 写入 | **并行编译必需**，不加会 `fatal error C1041` |
| `/DNDEBUG`（Release） | 关掉 `assert` | 发布版的 `assert` 必须消失（同时也要保证它没有副作用） |
| `/RTC1`（Debug 默认） | 检查未初始化变量与栈损坏 | Debug 下的第一道防线 |
| `/fsanitize=address` | ASan：越界、use-after-free | 定位内存问题的效率远高于靠猜 |
| `/analyze` | 静态分析 | CI 里跑，把问题挡在提交前 |

**一条实践原则**：不要用全局 `/wd4996` 之类的方式一次性关掉一整类警告。
本章连演示错误代码都用 `#pragma warning(push)/disable/pop` **局部**关闭，并在注释里写明理由。

### 4.2 调试手段

| 手段 | 适用场景 | 要点 |
| --- | --- | --- |
| 断点 + 监视窗口 | 通用 | 看变量、调用栈（Call Stack）、内存窗口 |
| **条件断点** | 循环第 10000 次才崩 | 把断点条件设成 `i == 10000`，不要手动按 9999 次继续 |
| **数据断点** | 「谁改了我的变量」 | 某个地址被写入时断下，专治幽灵修改 |
| 异常设置 | 崩溃在看不出原因的地方 | 勾上 Win32 异常，让调试器在**抛出点**而不是**崩溃点**停下 |
| 诊断工具 / 内存快照 | 内存泄漏 | 两次快照对比，看哪类对象只增不减 |
| `assert` | **内部不变式**（本该永远为真） | 不要用于用户输入/网络数据/文件内容——发布版里它会被删掉 |
| `static_assert` | 能编译期证明的一切 | 平台假设、结构体大小、类型是否可以 `memcpy` |
| 日志宏 | 分布式 / 异步 / 无法复现 | 带 `__FILE__` / `__LINE__`；把 `__DATE__` / `__TIME__` 打进启动日志，排查「用户装的是哪个版本」极其有用 |
| 编译器内建 | 内存问题 | `/RTC1`、`/fsanitize=address` |

日志宏的两个工程细节（`08` 篇）：

```cpp
#ifdef NDEBUG
#define LOG_INFO(...) ((void)0)          // Release：展开成空语句，零运行时开销
#else
#define LOG_INFO(...)                    \
    do {                                 \
        std::printf("[LOG] %s:%d ", __FILE__, __LINE__); \
        std::printf(__VA_ARGS__);        \
        std::printf("\n");               \
    } while (0)
#endif
```

- `do { ... } while (0)` 是为了让宏在 `if (x) LOG(a); else LOG(b);` 里表现得像**一条语句**；
  写成裸 `{ ... }` 时 `else` 会挂到错误的分支上（dangling else），编译直接不过。
- `__VA_OPT__`（C++20 标准做法）或 `##__VA_ARGS__`（GCC/MSVC 扩展）用来处理「没有可变参数时去掉前面的逗号」。

**能编译期证明的就别拖到运行期**（`08` 篇第 3 节）：

```cpp
static_assert(sizeof(int) == 4, "本仓库假定 int 是 4 字节");
static_assert(CHAR_BIT == 8, "本仓库假定 1 字节 = 8 位");
static_assert(factorial(5) == 120, "阶乘在编译期算好了");
static_assert(std::is_trivially_copyable_v<Pod>);      // 可以 memcpy
static_assert(!std::is_trivially_copyable_v<NotPod>);  // 含 std::string，不许 memcpy
```

**`assert` 的正确用法与禁忌**：

```cpp
assert(denominator != 0 && "check_divide 的调用方必须保证分母非零");  // 内部不变式，OK
// assert(++counter == 1);   // 【禁止】发布版里这行会消失，副作用跟着消失
```

消息用 `&& "说明"` 拼：条件为真时整个表达式为真，条件为假时字符串字面量会出现在失败输出里（非空指针恒为真）。

### 4.3 常见错误信息速查表

先记住**阶段划分**，这一条能省掉最多时间：

| 症状 | 阶段 | 原因 |
| --- | --- | --- |
| 「未声明的标识符」`C2065` | 编译期 | 忘记 `#include`，或名字拼错 |
| 「无法解析的外部符号」`LNK2019` | 链接期 | 声明了但没定义、忘了把 `.obj` / 库加进链接 |
| 「符号已经定义」`LNK2005` | 链接期 | 头文件里写了函数定义被多个 `.cpp` 包含（缺 `inline`） |

| 错误信息 | 原因 | 解决 |
| --- | --- | --- |
| `C2065` 未声明的标识符 | 忘记 `#include`、拼写错误、名字不在这个命名空间 | 加对应头文件；检查作用域；别忘 `std::` 限定 |
| `C2143` 语法错误：缺少「;」 | 上一行漏了分号、类定义后漏分号、宏展开破坏了语法 | 看**上一行**；把宏展开看一遍 |
| `LNK2019` 无法解析的外部符号 | 声明了没定义：函数体没写、`.cpp` 没加入构建、库没加进链接 | 补定义 / 加源文件 / 加 `#pragma comment(lib, ...)` 或链接选项 |
| `LNK2005` 符号已经定义 | 头文件里放函数或全局变量定义，被多个翻译单元包含 | 加 `inline`（函数）或 `static` / 匿名命名空间（内部用）；变量用 `inline`（C++17）或移到 `.cpp` |
| `LNK1561` 必须定义入口点 | 该源文件没有 `main`（例如实现与测试分离的文件被当成独立程序编译） | 不要用「一个文件一个 exe」的方式编译库文件；用 CMake 组合 |
| `LNK1168` 无法打开文件进行写入 | 上一次的 `.exe` 还在运行，文件被占用 | 关掉那个进程（或调试器）再编 |
| `C4996` 函数或变量被标记为不安全 | `strcpy` / `sprintf` / `scanf` / `getenv` 等被 MSVC 标记为不安全的 CRT 函数 | **不要**一关了事：改用 `_s` 版本（仅 Windows）、`std::string` / `std::format` / `std::getline`。确有必要时用 `_CRT_SECURE_NO_WARNINGS` 并写明理由 |
| `C4244` 「初始化」从 `double` 转换到 `float` 可能丢失数据 | 隐式窄化：`float f = 3.14;` | 写 `3.14f`；或用 `static_cast<float>` 显式表达「我知道会丢精度」 |
| `C4305` 从 `double` 到 `float` 的截断 | 同上 | 同上 |
| `C4018` 有符号/无符号不匹配 | `int` 与 `size()`（`std::size_t`）比较 | **改代码**：统一类型，或先判 `>= 0` 再 `static_cast<std::size_t>`。不要关警告 |
| `C4101` 未引用的局部变量 | 声明了没用（常见于调试残留、或「本该用上」的返回值） | 删掉它；参数确实不用就注释掉参数名或 `static_cast<void>(x)` |
| `C4189` 局部变量已初始化但未引用 | 同上 | 同上；检查是不是漏了真正的逻辑 |
| `C4477` `printf` 的格式串与实参类型不匹配 | `printf("%d", sizeof(x))` | `sizeof` 用 `%zu`；指针用 `%p` 并转 `const void*` |
| `C4478` `scanf` 的格式串与实参不匹配 | 读 `double` 用了 `%f`（或反过来） | 读 `double` 用 `%lf`、读 `float` 用 `%f` |
| `C4706` 条件表达式中的赋值 | `while (ch = 'y')` 的 `=` / `==` 手滑 | 把常量写左边（`'y' == ch`），或真的要用赋值时多写一层括号 `while ((ch = next()) != 0)` |
| `C4554` 检查运算符优先级 | `a << b + c`、`flags & mask == mask` | 加括号表达真实意图。这条警告是编译器在帮你兜底，**别关它** |
| `C4172` 返回局部变量或临时变量的地址 | `return local;` / `return &local;` | 改成按值返回，或让对象由调用方保证生命期。**必须改代码** |
| `C4456` / `C4457` / `C4458` 声明隐藏了上一层/参数/类成员 | 变量遮蔽 | 改名，不要用遮蔽。这类警告能防住真实的 bug |
| `C4324` 结构体因对齐说明符而被填充 | `alignas` 让结构体变大 | **不是错误**，是编译器确认「你确实造成了填充」。演示/有意为之可 `push/pop` 局部关闭 |
| `C2668` 对重载函数的调用有歧义 | 多个候选「一样好」：`0` / `NULL` 传给重载、匿名命名空间里出现同名函数 | 用 `nullptr` 而不是 `0` / `NULL`；检查声明和定义是否在**同一个命名空间** |
| `C2124` 除数为零或求模为零 | 编译期常量折叠时遇到 `0.0 / 0.0` | 想看运行期 IEEE-754 行为时用变量/`volatile` 挡住常量折叠 |
| `C1041` 无法打开程序数据库 | 多个 `cl.exe` 同时写同一个 PDB | 加 `/FS`，并用 `/Fd` 把 PDB 放进构建目录 |

**关于 `C4996` 的一条重要经验**：它出现时，第一反应应该是「有没有更安全的替代品」，
而不是「怎么把它关掉」。本章 `02` 篇与 `05` 篇为了**演示**这些函数的行为才临时关闭它，
并且在每一处都写明了更好的替代方案。

---

## 5. 自测题（7 道，含答案与原理）

### 第 1 题

下面哪些断言成立？逐条说明理由。

```cpp
char c = 'A'; short s = 1; unsigned int u = 110; long l = 0; long long ll = 0;
float f = 0.5f;
```

- (A) `decltype(c + s)` 是 `int`
- (B) `decltype(u + l)` 是 `unsigned long`
- (C) `decltype(u + ll)` 是 `unsigned long long`
- (D) `decltype(f + 1)` 是 `double`
- (E) `decltype(f + 1.0)` 是 `double`

**答案**：(A) 成立，(B) 成立，(C) 不成立，(D) 不成立，(E) 成立。

**原理**：

- (A) 整型提升：`char` 和 `short` 的 rank 都低于 `int`，且 `int` 能表示它们的全部值，
  所以两者都提升为 `int`，结果 `int`。
- (B) 常用算术转换：`long` 与 `unsigned int` 在 LLP64（Windows）下**同宽**，
  `long` 装不下 `unsigned int` 的全部值（`UINT_MAX` 超出 `LONG_MAX`），
  按规则两者都转成 `unsigned long`。
- (C) 错。`long long` 是 8 字节，**能**表示 `unsigned int` 的全部值，
  所以走「有符号类型能表示无符号类型的全部值」这条分支：`unsigned int` 转成 `long long`，
  结果是 **`long long`**。同一个 `unsigned`，加 `long` 和加 `long long` 得到符号性完全相反的类型。
- (D) 错。整型操作数转成**浮点操作数**的类型：`1` 转成 `float`，结果是 `float`。
  这是原笔记「float 中间参与计算时是 double」说错的地方。
- (E) 对。`1.0` 是 `double` 字面量，`float` 提升为 `double`。

**工程含义**：混合整数类型前先显式 `static_cast` 到同一类型；需要精确宽度的语义就用 `<cstdint>`。

---

### 第 2 题

```cpp
int a[10] = {0};
int* p = a;
```

以下哪些表达式得到 `sizeof(a)` 的值（40）？哪些得到 `sizeof(p)` 的值（8）？
哪些**不退化**？

`sizeof(a)`、`sizeof(p)`、`sizeof(a + 0)`、`sizeof(&a)`、`sizeof(*&a)`、`sizeof(a[0])`

**答案**：

| 表达式 | 结果 | 说明 |
| --- | --- | --- |
| `sizeof(a)` | 40 | `sizeof` **不退化** |
| `sizeof(p)` | 8 | 指针本身的大小 |
| `sizeof(a + 0)` | 8 | 参与算术**会退化**成 `int*` |
| `sizeof(&a)` | 8 | 取地址**不退化**，但类型是 `int(*)[10]`（指针），所以是 8 |
| `sizeof(*&a)` | 40 | `&a` 是 `int(*)[10]`，解引用后是 `int[10]`，`sizeof` 不退化 |
| `sizeof(a[0])` | 4 | 一个 `int` |

**原理**：数组名在**大多数**表达式里退化成首元素指针，但 `sizeof`、`&`、`decltype`、
绑定到数组引用、模板推导**不会**。要特别注意 `&a` 的类型是「指向整个数组的指针」`int(*)[10]`，
不是「指针的指针」——所以 `&a + 1` 前进 40 字节，而 `a + 1` 前进 4 字节。
两者**地址值相同、类型不同**，这正是 `04` 篇要讲透的那组实验。

---

### 第 3 题

下面的循环为什么不终止？给出两种修改方案。

```cpp
unsigned int n = 5;
for (unsigned int i = n - 1; i >= 0; --i) {
    std::cout << i << ' ';
}
```

**答案**：`i` 是无符号类型，当 `i == 0` 时执行 `--i`，
结果按模 `2^32` 回绕成 `UINT_MAX`（4294967295），而 `i >= 0` 对无符号类型**恒为真**，
所以条件永远成立，循环永远不终止，并且会越界访问。

两种修改：

```cpp
// 方案一：用有符号下标（推荐，可读性最好）
for (int i = static_cast<int>(n) - 1; i >= 0; --i) { std::cout << i << ' '; }

// 方案二：真必须用无符号时，把判断放在自减之前
for (unsigned int i = n; i != 0; --i) { std::cout << (i - 1) << ' '; }
```

（第三种同样常见：容器场景直接用 `rbegin()` / `rend()`。）

**原理**：无符号溢出不是错误，是标准精确定义的**模 `2^N` 回绕**。
`i >= 0` 这种「以为自己在防负值」的写法，对有符号类型是必要的，对无符号类型是**恒真**的废条件。
把 `>= 0` 换成 `!= 0` 才表达出真实意图。

---

### 第 4 题

解释下面三行输出为什么是这样：

```cpp
const char* user = "用户数据 %d";
std::printf(user);            // 输出：用户数据 <某个整数>
std::printf("%s", user);      // 输出：用户数据 %d
std::printf("100%% 完成");     // 输出：100% 完成
```

**答案**：

- 第一行：`user` 出现在**格式串**的位置，所以 `printf` 把 `%d` 当成**格式规范**，
  按「读一个 `int`」去栈上取参数。这里没有传参数，于是它读到了栈上的垃圾值（**信息泄露**）。
  如果内容里是 `%s`，那个垃圾值会被当成**地址**去解引用，程序通常会直接崩溃；
  如果是 `%n`，还会往那个地址**写**值。这就是 format string vulnerability。
- 第二行：内容出现在**参数**位置，格式串是字面量 `"%s"`。`%s` 表示「把参数指向的字符串原样输出」，
  所以 `%d` 只是普通字符。
- 第三行：格式串里的 `%%` 是转义，表示输出一个字面量的百分号。

**正确写法**：格式串必须是字面量，用户数据永远只能出现在参数位置。
更现代的替代是 `std::format`——它的格式串必须是编译期常量，参数类型和个数都在**编译期**检查。
另外 MSVC 的 CRT **默认禁用** `%n`，不显式启用就使用会直接 abort（退出码 3）。

---

### 第 5 题

```cpp
int n;
std::cin >> n;
std::string line;
std::getline(std::cin, line);
std::cout << "line=[" << line << "] len=" << line.size() << '\n';
```

输入 `42` 然后回车，`line` 是什么？为什么？给出两种修法。

**答案**：`line` 是**空串**（`len=0`）。

**原理**：`operator>>` 读取整数时，读到 `'4'` `'2'` 之后遇到换行符**就停下**，
但**不消费**那个换行符——它仍然留在输入流里。
紧接着 `getline` 从流中读字符，第一个字符就是 `'\n'`，
它立刻认为「这一行到此结束」，于是返回一个空串。
看上去像是「`getline` 被跳过了」，实际上是它**读到了一个空行**。

两种修法：

```cpp
// 修法 A：用 std::ws 吃掉前导空白（含换行）
std::getline(std::cin >> std::ws, line);

// 修法 B：显式忽略掉这一行剩余的内容
std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
std::getline(std::cin, line);
```

（第三种也推荐，尤其对交互式程序：**全程统一用 `getline`**，数字用 `std::from_chars` / `std::stoi` 解析。）

另外要记住：`cin >> n` **失败**时（例如输入 `abc`），流进入失败状态，
**后续所有输入都会失效**，必须先 `cin.clear()` 再清掉残留输入。
`scanf` 与 `fgets` 混用会出现完全同源的现象。

---

### 第 6 题

```cpp
int& f() {
    int local = 42;
    return local;
}
int g() {
    int local = 42;
    return local;
}
```

`f()` 和 `g()` 分别有什么问题？为什么 `f()` 的错误「经常看起来是对的」？

**答案**：

- `g()` **没有问题**。它按值返回，返回的是 `local` 的一份**拷贝**，
  局部变量在函数返回时销毁不影响这个拷贝。编译器通常还会用 RVO / NRVO 把这次拷贝也省掉。
- `f()` 返回的是**局部变量的引用**。函数返回时 `local`（自动存储期）被销毁，
  返回的引用指向一块已经失效的内存。对这个引用做任何读写都是 **UB**。
  MSVC 会给 `C4172`（returning address of local variable or temporary）。

**为什么「经常看起来是对的」**：那块栈内存在函数返回后**并不会立刻被覆盖**，
后续如果没有新的函数调用占用同一片栈空间，读到的仍然是 `42`。
一旦中间插入任何别的调用（尤其是 Debug 构建、或加了日志之后），值就变成垃圾——
这就是典型的「加了一行 printf，bug 就变了」的现象。

**正确做法三种**：

1. 按值返回（小对象、可拷贝类型）；
2. 返回引用，但**对象由调用方保证活得比调用期更久**（例如参数传进来的引用）；
3. 需要新对象又不想拷贝就按值返回，依赖 RVO / 移动语义（不要写 `return std::move(local);`，那会阻止 NRVO）。

还要注意：对返回引用的函数，`auto x = f()` 会**拷贝**，`auto& x = f()` 才是引用。
另外，临时对象绑定到 `const` 引用会延长寿命，但这个延长**只对直接绑定生效**。

---

### 第 7 题

```cpp
struct S {
    char c;
    int i;
    double d;
};
```

`sizeof(S)` 是多少？画出各成员的偏移与 padding 位置。
如果把成员顺序改成 `{double, int, char}`，大小会变吗？为什么？
再问：`struct { char c; double d; }` 改成 `{ double d; char c; }` 会变小吗？

**答案**：

- `sizeof(S)` = **16**。偏移：`c` 在 0，`i` 在 4，`d` 在 8。
  布局：`c(1) + padding(3) + i(4) + d(8)`，总大小 16，恰好是最大对齐值 8 的倍数，不需要尾部补齐。
- 改成 `{double, int, char}` 后**还是 16**。
  布局：`d(0..7) + i(8..11) + c(12) + 尾部 padding(3)`。
  成员总量都是 1 + 4 + 8 = 13，两个顺序都补齐到 16，**这个例子里顺序不影响大小**。
- `struct {char, double}` 是 16（`c(1) + pad(7) + d(8)`）；
  `struct {double, char}` 也是 16（`d(8) + c(1) + 尾部 pad(7)`）。
  **不会变小**——尾部补齐把好处吃掉了。

**真正会省空间的例子**（`09` 篇实测）：

```text
PoorlyPacked {char,double,char,int,char,double} sizeof = 40
WellPacked   {double,double,int,char,char,char} sizeof = 24   <- 省 16 字节（40%）
```

**原理**：只有两条规则。
规则 A：每个成员的起始偏移必须是该成员对齐值的整数倍。
规则 B：结构体总大小必须是「最大成员对齐值」的整数倍。
所以「按大小降序排成员」**不是万能公式**——它只在成员较多、且小成员被大成员分隔开时才有效，
而且尾部补齐可能抹平收益。

**工程含义**：不要凭直觉重排成员，用 `offsetof` + `static_assert` 把结论钉死。
序列化协议里**绝不能依赖 padding**：padding 的内容不确定（可能泄露内存旧数据），
必须逐字段显式编解码，或者用 `#pragma pack` + 显式字节序处理。

---

## 6. 一页速查

| 主题 | 一句话 |
| --- | --- |
| `NOMINMAX` | 必须在 `#include <windows.h>` **之前**，或项目级 `/DNOMINMAX` |
| `char` 与 `'a'` | `char` 变量在 C/C++ 里都是 1 字节；C 里字符常量 `'a'` 是 `int`，C++ 里是 `char` |
| 保留标识符 | 别写 `__x` / `_Xxx` / 全局的 `_xxx` |
| 类型转换 | 先整型提升、再常用算术转换；无符号 rank 不低于对方时，有符号方转无符号方 |
| 无符号 | 溢出 = 模 `2^N` 回绕（有定义）；不要用无符号做倒序循环变量 |
| 有符号溢出 | **UB**，没有任何保证 |
| 补码 | n 位 = 模 `2^n`；`-x` 的补码 = `2^n - x` = `~x + 1` |
| 浮点比较 | 绝不用 `==`；用绝对容差 + 相对容差；金额用整数 |
| 浮点字面量 | `3.14` 是 `double`，`3.14f` 才是 `float` |
| 数组退化 | `sizeof` / `&` / `decltype` / 数组引用 / 模板推导**不退化** |
| `a` vs `&a` | 地址值相同、**类型不同**，所以步长不同 |
| `const` 指针 | 从右往左读，`const` 修饰它左边最近的那个类型 |
| `nullptr` | 永远用 `nullptr`，不用 `NULL` / `0` |
| `printf` | 格式串必须是**字面量**；用户数据只能进参数位置 |
| `%lf` | `printf` 里 `%f` 与 `%lf` 等价；`scanf` 里必须区分 |
| `%zu` | `sizeof` 的结果必须用 `%zu` |
| `>>` 与 `getline` | `>>` 不消费分隔符；用 `std::ws` 或 `ignore` 清掉 |
| 返回引用 | 绝不返回局部变量的引用/指针（`C4172`） |
| `static` | 三种身份：内部链接 / 静态存储期 / 类成员 |
| `inline` | 语义是「允许多处定义」，不是「一定展开」 |
| 存储区 | 栈 / 堆 / 静态区 / 常量区；「谁拥有」= 「它在哪个区」 |
| `memcpy` | `memcpy(dest, src, count)`；只对平凡可拷贝类型安全 |
| `new` / `delete` | `new[]` 必须配 `delete[]`；更好的是不用裸 `new` |
| padding | 成员对齐 + 整体补齐两条规则；协议里禁止依赖布局 |
| 对齐 | `alignof` 查、`alignas` 设；`alignas(64)` 防伪共享 |
| 编译期 | 能 `static_assert` 就别 `assert`；能在编译期算完就别留到运行期 |
| 错误阶段 | `Cxxxx` = 编译期（语法/类型），`LNKxxxx` = 链接期（符号） |
| 警告 | `/W4 /WX`，局部 `push/pop` 关闭并写明理由，**不要全局 `/wd`** |

---

## 7. 延伸阅读（本仓库内）

- `02-oop/NOTES.md`：类、RAII、Rule of Three/Five/Zero、虚函数相关的不变式
- `03-modern/NOTES.md`：值类别与移动语义、智能指针、lambda、`constexpr`、C++20 特性
- `07-engineering/NOTES.md`：多文件工程组织、CMake、测试
- 原始笔记：仓库根目录 `main.cpp`（本文件第 2 节的纠错对象）
- 本章源码：`01-basics\01_hello_and_types.cpp` 到 `09_struct_layout.cpp`
