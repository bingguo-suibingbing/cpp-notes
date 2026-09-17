# 第 07 章：工程实践（警告 / 断言 / 日志 / 性能 / 调试 / 结构与风格）

> 本章的教学主线只有一句话：
> **前面六章解决的是「怎么写对」，本章解决的是「怎么在真实项目里长期写对、并且出问题能查出来」。**
>
> 本章六个 `.cpp` 不是新语法，而是六种「工程质量」：
> 编译器警告、断言与契约、日志与错误处理、性能测量、调试技巧、项目结构与风格。
> 它们共同的前提是：**人是会犯错的，所以要把检查交给机器（编译器、类型系统、静态分析、运行期检查、测试、CI）。**

本章面向「实用派、软件开发方向、要在实际工程项目里用得上」的读者，
所以每一节都尽量做到「有表格可查、有命令可复制、有结论可以直接拿去用」。

阅读建议：

- 只想快速上手：看第 1 节（章节地图）→ 第 2 节（十条习惯）→ 第 3 节（编译选项表）。
- 正在被一个具体错误卡住：直接跳到第 5 节（调试手册）。
- 正在建新项目：看第 4 节（构建系统）→ 第 7 节（版本控制）→ 第 8 节（评审清单）。

---

## 0. 本章交付物与验证结果

### 0.1 文件清单

| 路径 | 规模 | 说明 |
| --- | --- | --- |
| `01_compiler_warnings_and_tools.cpp` | 21.1 KB | 警告即 bug 报告、六类真实警告、`static_assert`、`[[nodiscard]]`、工具链开关 |
| `02_assert_and_contracts.cpp` | 24.1 KB | `assert` 语义、断言与异常的界线、类不变量、`CHECK` 宏、`source_location` |
| `03_logging_and_error_handling.cpp` | 31.9 KB | 可直接使用的日志设施、`ScopeGuard`、四种错误处理策略对比与选型 |
| `04_performance_measurement.cpp` | 40.4 KB | 可靠测量工具、分支预测、缓存局部性、`reserve` / 传参、优化性价比排序 |
| `05_debugging_techniques.cpp` | 36.1 KB | 崩溃异常码、五类内存错误、断点技巧、ASan、CRT 调试堆、二分注释法 |
| `06_project_structure_and_style.cpp` | 29 KB | 接口实现分离、强类型、`const` 正确性、头文件原则、命名与目录布局 |
| `CMakeLists.txt` | 296 行 | 本章的独立 CMake 工程，同时是一份「最小可用模板 + 逐行讲解」 |
| `CMakePresets.json` | 91 行 | `debug` / `release` / `asan` 三个预设（生成器写的是 Ninja，见 0.3 的说明） |
| `tests/checked_int.h`、`tests/checked_int.cpp` | 42 + 68 行 | 被测实现（SUT）：严格整数解析，返回错误码 + `[[nodiscard]]` |
| `tests/ring_buffer.h` | 101 行 | 定长环形缓冲区（纯头文件模板库，容量固定、不分配内存） |
| `tests/mini_test.h` | 114 行 | 30 行级别的手写断言框架（`CHECK_TRUE` / `CHECK_FALSE` / `CHECK_EQ`） |
| `tests/test_checked_int.cpp` | 119 行 | 26 项以上检查：正常 / 边界 / 非法输入 / 失败不污染输出 |
| `tests/test_ring_buffer.cpp` | 143 行 | 空 / 半满 / 覆盖最旧 / 绕多圈 / 容量 1 / 容量 0 抛异常 / `std::string` 移动 |
| `tests/CMakeLists.txt` | 109 行 | `eng07_sut` 静态库 + 用 CTest 注册两个测试 |

### 0.2 本机实测结果（都已经跑过）

| 验证项 | 命令 | 结果 |
| --- | --- | --- |
| 单文件编译（零警告） | `build.ps1 -Chapter 07-engineering -WX` | 成功 6 / 共 6，零警告 |
| 独立 CMake 工程生成 + 构建 | `cmake -S . -B build-cmake -G "Visual Studio 18 2026" -A x64` 然后 `cmake --build build-cmake --config Debug` | 成功（Debug x64） |
| 单元测试 | `ctest --test-dir build-cmake -C Debug` | 100% tests passed，2 / 2（`checked_int`、`RingBuffer`） |
| 示例程序运行 | 6 个示例 exe 逐个运行 | 全部退出码 0 |
| 运行耗时 | 同上 | `03_logging_and_error_handling` 约 28.4 秒（最长），`04_performance_measurement` 约 20.6 秒 |

关于耗时的说明：`03` 慢是因为它现场跑「异常 vs 返回码」的对比（失败路径每次要微秒级）；
`04` 慢是因为它在 Debug（`/Od`）下跑基准测试。这两个数字本身不说明代码有问题，
但它们说明一件工程事实：**基准测试和现场测量不要放进「每次构建都必须跑」的冒烟测试里**，
应该单独成一个 target 或一个 CI 阶段（见第 9 节的 CI/CD）。

### 0.3 环境事实（写给下一个接手的人）

| 项 | 状态 |
| --- | --- |
| CMake / CTest | 4.4.2，已安装并实测通过 |
| 生成器 | `Visual Studio 18 2026`（可用，本章实测用的就是它） |
| VS / MSVC | Visual Studio 18 Community，MSVC 工具集 14.51（依据：构建目录下的 `vc145.pdb` 与本仓库其它 README 记录） |
| Ninja | **未安装**。所以 `CMakePresets.json` 里的 `cmake --preset debug` 路径在本机**没有验证过**，它需要你**自行安装 Ninja**；本机验证走的是手写 `-G "Visual Studio 18 2026"` 命令 |
| clang-format | **未安装**。第 4.2 与第 8 节提到它时要记成「需要自行安装」，本机没有实测过它 |
| clang-tidy | **未安装**，同上 |
| AddressSanitizer | MSVC 自带（`/fsanitize=address`），本章的 CMake 与 `build.ps1` 都能开；但 `asan` 预设依赖 Ninja，本机未跑过 |

---

## 1. 章节地图

### 1.1 六个示例文件

| 文件 | 主题 | 一句话结论 |
| --- | --- | --- |
| `01_compiler_warnings_and_tools.cpp` | 警告级别、六类真实警告、`static_assert`、`[[nodiscard]]`、MSVC 选项 | **警告不是建议，是免费的 bug 报告**：第一步永远是把 `/W4` 打开，清零后再逐步上 `/WX` |
| `02_assert_and_contracts.cpp` | `assert` 语义、断言 vs 异常、前置 / 后置条件、类不变量、`CHECK` 宏、`std::source_location` | 断言是**可执行的文档**；判据只有一句：条件不成立时是「我们写错了」（断言）还是「世界变了」（错误处理） |
| `03_logging_and_error_handling.cpp` | 日志设施、`ScopeGuard`、返回码 / `optional` / `expected` / 异常四种策略 | 日志管可观测性、RAII 管资源、**错误策略必须全项目统一**；异常在成功路径免费、失败路径昂贵 |
| `04_performance_measurement.cpp` | `steady_clock`、预热 + 多轮 + 最小值、防优化、分支预测、缓存局部性、`reserve` / `emplace_back`、传参 | **先测量再优化**；Debug 下的性能数字没有参考价值；换算法是数量级收益，抠代码是个位数百分比 |
| `05_debugging_techniques.cpp` | 崩溃异常码、五类内存错误、断点类型、`at()` vs `[]`、ASan、CRT 调试堆、二分注释法 | **崩溃不可怕，静默的错误结果才可怕**；调试的一半工作量是「用二分法缩小范围」 |
| `06_project_structure_and_style.cpp` | 接口实现分离、强类型、`const` 正确性、`explicit` / `override` / `final` / `[[nodiscard]]`、头文件原则、命名、目录布局 | 风格靠工具固化，接口靠类型表达，**不变量靠封装保证** |

六个文件的次序不是随意的，它是一条「从便宜到昂贵」的防线：

```text
编译器警告 / static_assert      ← 最便宜，写代码时就报
        ↓
类型系统（强类型 / explicit / [[nodiscard]] / enum class）
        ↓
断言与不变量（Debug 下立刻挂）
        ↓
错误处理与日志（运行期可观测、可恢复）
        ↓
单元测试 / ASan / 静态分析      ← 提交前
        ↓
性能测量与剖析                  ← 上线前
        ↓
线上崩溃转储与事后追溯          ← 最昂贵
```

**能往前挪一层，就不要留在后一层。**

### 1.2 测试目录

| 文件 | 角色 | 一句话结论 |
| --- | --- | --- |
| `tests/checked_int.h` / `.cpp` | SUT：严格整数解析 | 「严格、明确、可预期」的解析器比自己想象的短；`[[nodiscard]]` 把「必须检查返回值」交给编译器 |
| `tests/ring_buffer.h` | SUT：定长环形缓冲 | 固定容量、不分配、覆盖最旧 —— 滑动窗口 / 收包缓冲 / 最近 N 条日志的标准解 |
| `tests/mini_test.h` | 极简断言框架 | 任何测试框架的本质只有三件事：计数器、断言宏、用退出码表达结果 |
| `tests/test_checked_int.cpp` | 测试 | 测试的价值在于「失败时不污染输出」这类**契约**，而不只是「正常路径能跑」 |
| `tests/test_ring_buffer.cpp` | 测试 | 环形缓冲最容易错的是「写满后覆盖最旧」和「容量为 1」，这两个必须有用例 |
| `tests/CMakeLists.txt` | 构建 | 一个测试一个可执行文件 + CTest 注册，退出码直接给 CI 用；测试也继承 `/W4 /WX` |

**为什么本章坚持不用 GoogleTest / Catch2**：为了「clone 下来就能跑、零依赖」。
真实项目当然应该上 GoogleTest（见第 9 节），但这 30 行让你看清测试框架的本质 ——
理解之后，换任何框架都只是换 API。

---

## 2. 十条开发习惯

每条都给出「为什么」和「怎么做」。这十条是从本章六个文件里提炼出来的，
它们都是**低成本的、可以今天就改的**，而不是「等有空再重构」的大工程。

### 习惯 1：把编译警告当成 bug 报告

- **为什么**：编译器是第一个、也是最便宜的代码审查员。它一次能看完整份代码，
  永不疲倦，而且它报出来的每一类警告（窄化转换、有符号无符号比较、未初始化变量、
  变量遮蔽、丢失返回值）在真实项目里都对应过线上事故。
- **怎么做**：工程里统一开 `/W4`；历史代码先用 `#pragma warning(push/pop)` 局部压制
  （压制必须写明理由和期限），等警告清零后再开 `/WX` 让它永久保持。
  想按警告号精准升级，用 `#pragma warning(error : 4996)`。

### 习惯 2：定义即初始化，能 `const` 就 `const`

- **为什么**：未初始化变量是「Debug 能过、Release 崩」的头号来源（Debug 下栈被填成
  `0xCC`，Release 下是上一帧的垃圾）。`const` 则是「我不会改它」的机器可验证声明，
  同时给编译器更多优化空间。
- **怎么做**：声明变量时立刻给值；类成员一律写默认成员初始化器；
  成员函数只要能 `const` 就 `const`；参数默认 `const&`（小 POD 除外，见第 6 节）。
  加不上 `const` 的地方（缓存、锁、计数器）才用 `mutable`，并在注释里写明理由。

### 习惯 3：能编译期查的，绝不留给运行期

- **为什么**：编译期发现的错误成本大约等于「改一行」；运行期发现的错误成本等于
  「复现 + 定位 + 回归测试 + 可能的线上损失」。两者差几个数量级。
- **怎么做**：类型假设、平台假设、模板契约一律写 `static_assert`；
  单位混用一律用强类型（`Meters` / `Seconds` 而不是两个 `double`）；
  构造函数默认加 `explicit`；虚函数重写必须写 `override` ；
  有返回值的纯查询函数一律 `[[nodiscard]]`。

### 习惯 4：用类型表达契约，而不是用注释和口头约定

- **为什么**：注释不会阻止任何人写错代码；类型会。`std::optional` 比
  「返回 -1 表示失败」更难用错；`enum class` 比 `#define OK 0` 更难用错。
- **怎么做**：可能没有值 → `std::optional`；可能失败且要带原因 → `std::expected`（C++23）；
  状态用 `enum class`；单位用新类型；「不可能为空」用引用而不是指针；
  把「不变量」写成 `check_invariant()` 并在构造完成与每次修改后调用。

### 习惯 5：断言对外，错误处理对内，边界必须校验

- **为什么**：断言在 Release 下会被整个编译掉（`NDEBUG`）。
  对外部数据断言 = 把「用户敲错一个字符」变成「程序崩溃」；
  而忘了在边界校验 = 把「世界变了」当成「不可能发生」，最后在深层代码里变成越界访问。
- **怎么做**：在系统边界（用户输入、文件、网络、第三方返回值、`errno`）做**真正的校验**
  并返回错误；校验通过之后的内部代码只依赖断言。
  判据一句话：**「这个条件不成立，是我们写错了，还是世界变了？」**

### 习惯 6：一切资源都用 RAII 管理

- **为什么**：手写清理代码一定会漏 —— 尤其是后来有人加了一个 `return` 或者加了 `throw` 之后。
  析构函数是 C++ 唯一「无论怎么退出都会执行」的机制。
- **怎么做**：锁用 `std::lock_guard` / `std::unique_lock`；内存用 `unique_ptr` / 容器；
  文件用 `std::ofstream`；「成对的开始 / 结束」用 `ScopeGuard`；
  多返回值路径的清理动作一律绑到作用域上。

### 习惯 7：日志要能分级、能关闭、带上下文、能落文件

- **为什么**：生产环境的日志是唯一的「事后证据」。没有级别的日志无法过滤，
  没有时间戳和文件行号的日志无法定位，没有锁的日志在多线程下是一堆交错碎片，
  而热路径里无条件拼字符串的日志本身就是性能 bug。
- **怎么做**：用 `enum class` 定义级别；全局对象用函数内静态变量实现（线程安全初始化）；
  输出加 `std::mutex` 保证整行原子；时间戳用 `localtime_s` / `localtime_r`（不是 `localtime`）；
  支持切换输出目标；`LOG_DEBUG` 用 `if constexpr` 包住，让它在 Release 下编译期消失。

### 习惯 8：先测量，再优化；只认 Release 的数字

- **为什么**：人的直觉在性能上极不准。测出来是 0.0000 ms 的基准测试通常不是「太快」，
  而是「被优化器删掉了」；而优化过的代码如果没有基线数字，你既无法证明它变快了，
  也无法证明它没把别处改坏。
- **怎么做**：用 `steady_clock`；预热 3 轮丢掉、正式测 15 轮；看最小值和中位数；
  用 `do_not_optimize` 防止结果被优化掉；每轮至少几十毫秒再除以调用次数；
  改一处测一次；把「机器、配置、编译器版本」记在结论旁边。

### 习惯 9：每次提交都能编译、能过测试

- **为什么**：这是 `git bisect` 和「随时可以发版」的前提。
  一个连编译都不过的提交会让二分法直接失效，也会让同事在你身上浪费半天。
- **怎么做**：提交前跑 `build.ps1 -Chapter <本章> -WX`（或 CMake 构建 + `ctest`）；
  测试用**退出码**表达结果，让 CI 和脚本都能一条命令判断；
  测试里不要依赖「运行时间」（把长基准测试排除在冒烟测试之外）。

### 习惯 10：风格交给工具，评审只看逻辑

- **为什么**：人争论缩进是在浪费生命；而「格式不一致」会让 diff 里塞满无关改动，
  把真正的逻辑改动淹没掉。
- **怎么做**：`.clang-format` 与 `.editorconfig` 进版本库；
  `CMakePresets.json` 固化「Debug / Release / ASan」这些组合；
  编辑器和 CI 都跑同一套工具；评审清单（第 8 节）只留「工具查不出来」的事。

---

## 3. 编译选项完全指南

### 3.1 MSVC 与 GCC / Clang 对照表

「不开会怎样」列里写的是**真实会看到的后果**，不是理论风险。

| 选项（MSVC） | GCC / Clang 对应 | 作用 | 不开会怎样 | 什么时候必须开 |
| --- | --- | --- | --- | --- |
| `/std:c++20` | `-std=c++20` | 指定语言标准 | VS 默认是 C++14；concepts、ranges、`<source_location>`、`std::span` 全部不可用，且报错方式五花八门 | 永远。并且要配 `CXX_STANDARD_REQUIRED ON`，否则编译器不支持时会**静默降级** |
| `/EHsc` | `-fexceptions`（GCC/Clang 默认开） | 标准 C++ 异常模型 | 报 C4530；STL 抛异常时行为不标准，`catch` 可能接不到 | 只要用了 STL 或异常（几乎总是） |
| `/W4` | `-Wall -Wextra` | 高警告级别 | 窄化转换、有符号无符号比较、未初始化变量、变量遮蔽全部看不见 | 永远。新项目第一天就开 |
| `/WX` | `-Werror` | 警告当错误 | 警告会慢慢涨回来，最后没人看 | 警告已清零之后。历史代码可以按文件逐步开 |
| `/permissive-` | `-pedantic-errors`（近似） | 关闭宽松模式，标准两阶段查找 | 依赖基类成员不写 `this->` 也能编过，换编译器直接炸 | 需要可移植性时；MSVC 上新项目建议一直开 |
| `/utf-8` | `-finput-charset=UTF-8 -fexec-charset=UTF-8` | 源码与执行字符集都按 UTF-8 | 中文注释触发 C4819，字符串字面量乱码 | 源码里有非 ASCII 字符时（本仓库就是） |
| `/Zc:__cplusplus` | 不需要（GCC/Clang 本来就对） | 让 `__cplusplus` 报真实值 | MSVC 永远报 `199711L`，`#if __cplusplus >= 202002L` 静默失效，代码走错分支 | 用 `__cplusplus` 做条件编译时。或者改用 `_MSVC_LANG` |
| `/diagnostics:caret` | `-fcaret-diagnostics`（Clang） | 错误定位到列 | 只定位到行；模板报错时几乎无法定位 | 模板用得多的项目 |
| `/analyze` | `clang --analyze` / `clang-tidy` | 静态分析：空指针、越界、资源泄漏 | 这三类问题只能靠人眼和运行期崩溃发现 | CI 上定期跑一遍（慢，不建议每次编译都开） |
| `/fsanitize=address` | `-fsanitize=address` | AddressSanitizer：越界、use-after-free、泄漏 | 内存错误只能靠「碰巧崩」发现 | 测试与 CI。运行慢约 2 倍、内存多 2 到 3 倍 |
| `/MDd`（Debug） | 无直接对应（等价物是 `_GLIBCXX_DEBUG`） | 动态链接**调试版**运行时库 | 见 4.3 节：混用 `/MDd` 与 `/MD` 是经典灾难 | Debug 配置。且**全工程必须一致** |
| `/MD`（Release） | 无直接对应 | 动态链接发布版运行时库 | 同上 | Release 配置 |
| `/Zi` + `/DEBUG` | `-g` | 生成调试信息 | 崩溃时看不到调用栈和变量，只能猜 | Debug 永远开；Release 也建议开（结果是 pdb 单独存档） |
| `/Od` / `/O2` | `-O0` / `-O2`（GCC 常用 `-O3`） | 关优化 / 开优化 | 不开 `/O2` 就没有性能；开着 `/Od` 做性能测试则数据无意义 | 按配置来（见 4.3） |
| `/RTC1` | `-fsanitize=undefined`（近似） | 运行期检查（栈帧、未初始化变量） | 栈被破坏、变量没初始化这两类问题在 Debug 下查不出来 | 仅 Debug。与 ASan 冲突，开 ASan 时必须去掉 |
| `/bigobj` | 不需要 | 允许更多节 | 模板实例化极多时报 `fatal error C1128` | 报 C1128 时。本仓库直接全开，没有代价 |
| `/showIncludes` | `-H` | 打印 include 树 | 排查「为什么改了这个头要重编这么多」时没有数据 | 排查编译依赖时临时开 |
| `/d1reportAllClassLayout` | `-fdump-lang-class`（GCC） | 打印类内存布局 | 无法确认空基类优化 / vptr / 填充 | 排查 ABI、内存占用、对齐问题时 |
| `/Fa<file>` | `-S` | 输出汇编 | 无法验证「有没有内联、有没有多余拷贝」 | 最后手段：确认优化是否真的发生 |

### 3.2 本仓库实际用的选项

`build.ps1` 里的固定选项（每章都一样）：

```text
/nologo /std:c++20 /EHsc /W4 /utf-8 /permissive- /Zc:__cplusplus
/diagnostics:caret /bigobj /MDd /Zi
```

再按参数追加：`-WX` 加 `/WX`；`-Config Release` 加 `/O2 /DNDEBUG`
（注意：`build.ps1` 的 `/MDd` 是写死的，做 Release 性能对比时要以 CMake 那边为准 ——
这也是第 4.3 节要强调「配置要成套」的原因）。

本章 `CMakeLists.txt` 里的选项（打包在一个 `INTERFACE` 目标 `project_warnings` 里）：

```cmake
target_compile_options(project_warnings INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:MSVC>:/permissive->
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/EHsc>
    $<$<CXX_COMPILER_ID:MSVC>:/diagnostics:caret>
    ...)
```

好处是「改一处，所有目标生效」：本章 6 个示例 + 2 个测试 + 1 个库全都一行
`target_link_libraries(... PRIVATE project_warnings)` 就继承同一套选项。

### 3.3 三个常见的选项误用

| 误用 | 后果 | 正确做法 |
| --- | --- | --- |
| 用 `/Wall` 代替 `/W4` | `/Wall` 会把 STL 头文件里的警告也报出来（成千上万条），于是整个团队学会「无视警告」 | 用 `/W4`，再按需补 `C4061` / `C4062` / `C4820` 等 |
| 用 `/wd` 或 `-w` 全局关警告 | 等于放弃了最便宜的检查 | 用 `#pragma warning(push/pop)` 做**最小范围**压制，并写明理由 |
| 只开 `/W4` 不开 `/WX`，或者反过来只开 `/WX` 不开 `/W4` | 前者警告涨回来；后者把项目卡死在没法编译 | 先 `/W4` 清零，再开 `/WX`；历史代码按文件逐步升级 |

---

## 4. 构建系统

### 4.1 CMake 最小可用模板（逐行讲解）

下面是一份**够用的最小模板**，它把本章 `CMakeLists.txt` 的第 1 到第 10 节压缩成了
二十来行。先能背下这个骨架，再看真实项目就好懂了。

```cmake
# 1. 声明最低 CMake 版本（同时决定 policy 默认行为，不只是打个招呼）
cmake_minimum_required(VERSION 3.20)

# 2. 项目名 / 版本 / 只启用 C++（有 .c 文件时必须写成 C CXX）
project(myapp VERSION 1.0.0 LANGUAGES CXX)

# 3. 语言标准：三个变量一起写，缺一个都会出怪问题
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)   # 不支持就报错，而不是静默降级
set(CMAKE_CXX_EXTENSIONS OFF)         # 不用编译器扩展，换编译器少踩坑

# 4. 把「编译选项」打包成一个不产出二进制的 INTERFACE 库
add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:MSVC>:/permissive->
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/EHsc>
    $<$<CXX_COMPILER_ID:MSVC>:/WX>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror>)

# 5. 可执行目标 + 继承选项
add_executable(myapp src/main.cpp)
target_link_libraries(myapp PRIVATE project_warnings)

# 6. 测试：include(CTest) 会定义 BUILD_TESTING（默认 ON）并调用 enable_testing()
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
```

逐行讲解：

| 行 | 为什么这么写 | 不这么写会怎样 |
| --- | --- | --- |
| `cmake_minimum_required(VERSION 3.20)` | 让旧版 CMake 立刻报「版本太老」；同时启用对应版本的 policy | 旧版会在一堆奇怪语法错误里挣扎；policy 默认值不对会改变构建行为 |
| `project(... LANGUAGES CXX)` | 只启用 C++，配置更快，也不会意外去找 C 编译器 | 有 `.c` 文件而没写 `C` 时，那些文件会被**静默忽略** |
| `CXX_STANDARD` / `CXX_STANDARD_REQUIRED` / `CXX_EXTENSIONS` | 三者一起才完整：要求标准、不许降级、不许用扩展 | 只写第一个：编译器不支持时静默用旧标准编译，你在运行期才发现概念不能用 |
| `add_library(... INTERFACE)` | 把「一组编译选项」变成一个可以 `target_link_libraries` 的东西；改一处全体生效 | 选项散落在每个 `add_executable` 里，新增目标必然漏配 |
| `$<CXX_COMPILER_ID:MSVC>` | 生成器表达式在**生成阶段**展开，同一份脚本同时支持 MSVC 与 GCC / Clang | 用 `if(MSVC)` 也能用，但一旦要按配置 / 按目标区分就会写得很乱 |
| `add_executable` + `target_link_libraries(... PRIVATE ...)` | `PRIVATE` 表示选项只用于编译自己，不传播给依赖者 | 用全局 `add_compile_options` 会污染所有目标，包括第三方库 |
| `include(CTest)` + `add_subdirectory(tests)` | CTest 让「跑测试」是一条命令、一个退出码 | 手工跑每个测试 exe，CI 里一定会漏 |
| `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)`（本章第 4 节） | 生成 `compile_commands.json`，给 clangd / clang-tidy 用 | 编辑器智能提示退化成猜；只对 Ninja / Makefile 生成器有效 |
| `set(CMAKE_RUNTIME_OUTPUT_DIRECTORY .../bin)`（本章第 4 节） | 可执行文件集中，源码目录保持干净 | exe 散落在源码目录里，和 `.cpp` 混在一起 |
| `message(STATUS ...)` 打印摘要（本章第 11 节） | 排查「我明明改了选项」时省半天时间 | 只能靠 `CMakeCache.txt` 反推当前配置 |

### 4.2 三种构建方式怎么选

本章同时提供了两套构建方式，它们**互不依赖**（`build.ps1` 不调用 CMake，CMakeLists 也不调用 `build.ps1`）。

| 维度 | `build.ps1` | 本章独立 CMake 工程 | Visual Studio 手建工程 |
| --- | --- | --- | --- |
| 依赖 | 只要 VS + PowerShell（零第三方依赖） | 需要 CMake（本机 4.4.2 已装） | 只要 VS |
| 粒度 | 一个 `.cpp` = 一个 exe | 一个 `.cpp` = 一个 target，另有库与测试 | 一个项目一个 exe，要手工建 N 个 |
| 调试体验 | 只能命令行跑，或手工挂调试器 | VS 生成器下可以 F5、可以设断点 | 最好（原生） |
| 多配置 | `-Config Debug / Release` 参数 | Debug / Release / RelWithDebInfo / MinSizeRel | 解决方案配置管理器 |
| 测试 | 不支持 | 支持（CTest，2 / 2 通过） | 要手工配 |
| 第三方库 | 不支持 | 支持 `find_package` / `add_subdirectory` / vcpkg | 手工配路径 |
| CI 友好度 | 中（要保证有 VS） | 高（一条命令 + 一个退出码） | 低（`.slnx` / `.vcxproj` 不好维护） |
| 适合 | 快速验证「语法 / 警告」 | **真实项目的起点** | 学习、临时试验 |
| 不该用它做 | 依赖管理、单元测试、发布打包 | （没有明显短板） | 团队协作、代码评审（工程文件是二进制式 diff 灾难） |

**结论**：

- 学习、快速验证语法与警告 → `build.ps1 -Chapter 07-engineering -WX`。
- 要写「像一个真项目」的代码 → CMake。本章的 `CMakeLists.txt` 就是可直接抄的模板。
- **不要**手工维护一堆 `.vcxproj`：
  工程文件一旦进版本库，每次加文件都会有巨大的、无法评审的 diff。

本章 `CMakePresets.json` 提供了 `debug` / `release` / `asan` 三个预设，
用法是 `cmake --preset debug`、`cmake --build --preset debug`、`ctest --preset debug`。
**但要注意**：预设里 `base` 用的生成器是 `Ninja`，而本机**没有装 Ninja**，
所以这条路径在本机**未验证**；要用它就**需要自行安装 Ninja**。
本机实测走的是手写命令：

```powershell
cd cpp-notes\07-engineering
cmake -S . -B build-cmake -G "Visual Studio 18 2026" -A x64
cmake --build build-cmake --config Debug
ctest --test-dir build-cmake -C Debug --output-on-failure
```

多配置生成器（Visual Studio）的好处：一个构建目录同时容纳 Debug 和 Release，
用 `--config` 切换；坏处：`CMAKE_BUILD_TYPE` 是空的，配置阶段的
`if(CMAKE_BUILD_TYPE STREQUAL "Debug")` **永远不成立** —— 必须用生成器表达式
`$<CONFIG:Debug>`（见 4.3）。

### 4.3 Debug 与 Release 的差异

这不是「快和慢」的差别，而是**两套语义下的两个程序**。

| 维度 | Debug | Release |
| --- | --- | --- |
| 优化 | `/Od`（GCC `-O0`） | `/O2`（GCC 常用 `-O3`） |
| 调试信息 | `/Zi` + `/DEBUG` | 建议也加 `/Zi`，pdb 单独存档 |
| `NDEBUG` | 未定义 | **已定义** |
| `_DEBUG` | 已定义 | 未定义 |
| `assert` | 生效（失败打印 + `abort()`） | **被预处理阶段整体删除**（连表达式都不参与编译） |
| `[[nodiscard]]` | 生效 | **依然生效**（这是它比 `assert` 可靠的原因） |
| `LOG_DEBUG`（本章 03） | 完整输出 | 编译期消失，零成本 |
| 运行时库 | `/MDd` | `/MD` |
| 运行期检查 | `/RTC1`（栈帧、未初始化变量） | 无 |
| 迭代器调试 | `_ITERATOR_DEBUG_LEVEL=2` | `_ITERATOR_DEBUG_LEVEL=0` |
| 典型速度差 | 慢 | 快。纯循环 2 到 5 倍；大量小函数调用 10 到 50 倍；模板 / STL 密集更大 |

**为什么 Debug / Release 运行时库混用是经典灾难**：

1. **两套堆**。`/MDd` 与 `/MD` 各自链接不同的 CRT DLL，各自有自己的堆。
   模块 A（`/MDd`）里 `new` 出来的指针，传给模块 B（`/MD`）去 `delete`，
   就是把 A 的堆块交给 B 的堆去释放 —— 直接堆破坏 / 访问违例。
   症状：崩在 `free` 或 `_heap_alloc` 内部，地址看起来完全正常。
2. **对象布局不一致**。`_ITERATOR_DEBUG_LEVEL` 不同，`std::vector` / `std::string`
   的大小和内部结构就不同；`/MDd` 下的 `std::string` 传给 `/MD` 的代码，
   读到的成员偏移是错的 —— 数据静默损坏，比崩溃更难查。
3. **链接器会当场报错（如果它发现得了）**：
   `LNK2038: mismatch detected for '_ITERATOR_DEBUG_LEVEL': value '2' doesn't match value '0'`，
   或者 `RuntimeLibrary` mismatch。看到这个错误，第一反应就是「检查所有依赖是用哪个配置编的」。
4. **最危险的是它有时不报错**：如果第三方库只暴露 C 风格接口、只传 POD，链接器看不出来，
   于是问题留到线上。

**纪律**：

- 整个工程（主程序 + 所有第三方库 + 所有 DLL）必须使用**同一个配置**。
- 第三方库只提供一种配置时，主程序就用同一种，不要「混着凑」。
- 本章 `CMakeLists.txt` 用一行显式固定了这件事，让规则可见：

```cmake
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
```

**另一个必须记住的坑**：多配置生成器在**配置阶段不知道当前配置**。
所以下面这种写法是错的（VS 生成器下永远不生效）：

```cmake
if(CMAKE_BUILD_TYPE STREQUAL "Debug")      # 错：VS 下 CMAKE_BUILD_TYPE 是空的
    target_compile_options(t PRIVATE /Od)
endif()
```

正确写法是用生成器表达式，它在生成 / 构建阶段才展开：

```cmake
target_compile_options(project_warnings INTERFACE
    $<$<CONFIG:Debug>:$<$<CXX_COMPILER_ID:MSVC>:/Od>>
    $<$<CONFIG:Debug>:$<$<CXX_COMPILER_ID:MSVC>:/RTC1>>
    $<$<CONFIG:Release>:$<$<CXX_COMPILER_ID:MSVC>:/O2>>)
```

### 4.4 顶层 CMake 与本章独立 CMake 的关系

这是本章构建系统里**最容易踩、也最值得学**的一处。本章是**独立工程**，
但仓库顶层 `cpp-notes/CMakeLists.txt` 也是一个完整工程，两者都会遍历章节目录。

**问题**：如果两边都编 `07-engineering` 的 6 个 `.cpp`，会出现

1. **同一份源码在同一个解决方案里被注册两次**（顶层按 `07_engineering_01_...` 命名，
   本章按 `01_...` 命名），构建产物重复、VS 里出现两套目标；
2. 两个工程各自定义自己的「警告目标」，选项容易不一致（本书第 3.2 节的两套选项就是实例）；
3. 本章的 `tests/` 是**带自己的 `CMakeLists.txt` 的子目录**，
   顶层用 `file(GLOB)` 只取第一层 `.cpp`，所以不会重复编测试 —— 但测试也只会在
   本章自己的工程里被注册一次。

**顶层的解决办法**（已实现）：默认跳过自带独立 CMake 工程的章节，并提供一个开关：

```cmake
# cpp-notes/CMakeLists.txt（节选）
option(CPPNOTES_BUILD_07_HERE "在顶层工程里也编译 07-engineering（会与它自己的 CMake 工程重复）" OFF)

if(EXISTS "${_dir}/CMakeLists.txt" AND NOT CPPNOTES_BUILD_07_HERE)
    message(STATUS "跳过 ${_chapter}: 它自带独立 CMake 工程，请单独构建")
    continue()
endif()
```

于是：

| 场景 | 命令 | 结果 |
| --- | --- | --- |
| 只想构建 01 到 06 章（默认） | `cmake -S . -B build-cmake` | 07-engineering 被跳过，并打印一行 `STATUS` 说明原因 |
| 想在顶层一起编 07 | `cmake -S . -B build-cmake -DCPPNOTES_BUILD_07_HERE=ON` | 07 的 6 个 `.cpp` 被纳入顶层；同时顶层的 `enable_testing()` + `add_subdirectory("07-engineering/tests")` 会接入它的测试 |
| 只构建本章（推荐） | `cmake -S 07-engineering -B 07-engineering/build-cmake` | 完整独立工程：6 个示例 + `eng07_sut` 库 + 2 个 CTest 测试，且带自己的 `CMakePresets.json` |

**这就是工程上的通用原则**：**同一个源文件，只应该由一个「拥有它的构建系统」负责注册。**
让两个构建系统同时拥有同一批源码，几乎必然产生重复目标、不一致的编译选项、
以及「为什么我这里编过了你那里编不过」的争论。

**关于 `build.ps1` 与 CMakeLists 的关系**：两者**互不依赖**，可以同时存在。
`build.ps1` 的 `/MDd` 是写死的，所以它适合「验证语法与警告」，
不适合做 Release 性能对比；做性能对比用 CMake 的 `--config Release`。
本章 `CMakeLists.txt` 顶部那条注释说的就是这个意思，这条关系现在依然成立。

### 4.5 CMake 目标作用域（一个真实踩过的坑）

**本章的 `tests/CMakeLists.txt` 曾经写过这一行（错的）**：

```cmake
target_link_libraries(test_checked_int PRIVATE cppnotes_warnings)
```

`cppnotes_warnings` 是**顶层** `cpp-notes/CMakeLists.txt` 里定义的目标名。
本章 `07-engineering/CMakeLists.txt` 第 141 行定义的目标叫 `project_warnings`。
于是：

- **独立构建本章时**（`cmake -S 07-engineering -B ...`），顶层工程根本没参与，
  那个目标**不存在**。
- 因为 `cppnotes_warnings` 里没有 `::`，CMake 不会在配置阶段报「目标找不到」，
  而是把它当成**要链接的库名**处理 —— 于是失败推迟到链接期，
  在 MSVC 上表现为 `LNK1104: 无法打开文件 cppnotes_warnings.lib`，
  在 GCC 上表现为 `cannot find -lcppnotes_warnings`。
- 修正后写的是 `project_warnings`，独立构建与顶层构建都能工作。

**要记住的规则**：

> CMake 的 target 名只在**同一次配置过程可见的作用域**里有意义。
> 跨工程（跨独立 `cmake -S`）复用目标名是**不可能**的 ——
> 目标必须由同一个配置过程里的某份 `CMakeLists.txt` 定义出来。

四种正确的复用方式：

```cmake
# 方式 1（最推荐）：本章作为独立工程，自己定义自己用，不假设外部存在。
add_library(project_warnings INTERFACE)
target_link_libraries(test_checked_int PRIVATE project_warnings)

# 方式 2：给下游一个带命名空间的稳定名字（ALIAS），避免和别人的目标撞名。
add_library(myproject::warnings ALIAS project_warnings)
target_link_libraries(myapp PRIVATE myproject::warnings)

# 方式 3：只有当目标真的存在时才复用，否则退回自己定义（兼容两种构建入口）。
if(TARGET cppnotes_warnings)
    set(_warnings_target cppnotes_warnings)
else()
    set(_warnings_target project_warnings)
endif()
target_link_libraries(test_checked_int PRIVATE ${_warnings_target})

# 方式 4：跨工程复用，必须由「同一个配置过程」引入该工程才有目标。
add_subdirectory(../other_lib other_lib_build)   # 引入后 other_lib::warnings 才存在
# 或者走真正的包机制：
find_package(otherlib REQUIRED)                  # 由 otherlibConfig.cmake 提供导入目标
```

另一个相关细节：**带 `::` 的名字 CMake 会当作目标引用并要求它必须存在**，
所以 `target_link_libraries(t PRIVATE Something::Missing)` 会在**配置阶段**直接报
「target not found」—— 这反而更好（错误暴露得更早）。
这也是为什么「给库起带命名空间的名字」是好习惯：错误从链接期提前到配置期。

**顺带一条教学价值**：这个坑本身就是本章 05 与 06 讲的思路的实例 ——
错误的写法在「不需要跨工程」的场景下完全正常，只在独立构建时才暴露。
**同一份代码在不同构建入口下行为不同，必须有「用每一种入口都构建一遍」的习惯**，
这正是第 9 节 CI/CD 要解决的问题。

---

## 5. 调试手册

### 5.1 常见崩溃类型：现象与定位方法

先记住「异常码」是定位的第一把钥匙（VS：调试 → 窗口 → 异常设置，或者看输出窗口）。

| 异常码 / 类型 | 名称 | 典型原因 | 现象 | 定位方法 |
| --- | --- | --- | --- | --- |
| `0xC0000005` | `ACCESS_VIOLATION`（访问违例） | 空指针解引用、数组越界、悬垂指针 / use-after-free、野指针、写只读内存 | 崩在一条看起来毫无问题的语句上；调用栈里往往不是真正出错的地方 | 看「地址是多少」：接近 0 → 空指针；是一个大数 → 野指针；看调用栈**最底层属于你自己的帧**；开 ASan 一次报出精确位置 |
| `0xC00000FD` | `STACK_OVERFLOW`（栈溢出） | 没有终止条件的递归、递归深度随输入增长、在栈上开了巨大局部数组 | 崩在函数序言附近；调用栈里有成百上千层同一个函数；或者直接提示 Stack overflow | 看调用栈是不是同一个函数重复；改成迭代（显式栈）、加深度上限、把大数组放到堆上 |
| 无固定码 | 堆破坏（heap corruption） | 越界写、double free、`free` 了非堆指针、跨模块 `free`（Debug / Release 混用） | 崩在 `malloc` / `free` / `_heap_alloc` / `RtlValidateHeap` 内部；或者在**完全无关的地方**崩（破坏发生在很久以前）；也可能程序结束时报堆损坏 | `_CrtCheckMemory()` 在可疑操作前后各调一次夹逼；或 ASan；或二分注释法；同时检查所有模块的运行时库是否一致 |
| `0xC0000094` | `INTEGER_DIVIDE_BY_ZERO` | 除零、对 0 取模 | 崩在除法指令上 | 除法前断言分母非零；`INT_MIN / -1` 也是未定义行为 |
| `0xE06D7363` | 未捕获的 C++ 异常 | `throw` 没有被 `catch` | 提示 `unhandled exception`，栈上能看到 `RaiseException` / `CxxThrowException` | 在 `main` 外层包一层 `try / catch`；打开异常设置里的「C++ 异常」断点 |
| 无异常码 | 死循环 / 卡死 | 无锁竞争、条件变量丢唤醒、迭代器失效后的比较、忘 `join` | 界面无响应、CPU 100% 或 0% | VS 里「调试 → 全部中断」看每个线程的调用栈；性能探查器看热点 |
| 无异常码 | **结果错误但不崩** | 越界读到了合法内存、整型溢出回绕、有符号 / 无符号比较、未初始化内存 | 程序跑完，结果不对 | **最危险的一类**。靠断言、`at()`、ASan、`/W4`、以及「先写测试」 |

**Debug 与 Release 的崩溃差异，怎么读**：

| 现象 | 最常见的真实原因 | 排查顺序 |
| --- | --- | --- |
| Debug 通过，Release 崩 | 未初始化内存（Debug 下栈被填成 `0xCC` 或恰好为 0，Release 下是垃圾）；`assert` 里的副作用在 Release 下消失了；`assert` 检查的东西在 Release 下没检查（空指针、越界）；`LOG_DEBUG` 消失后暴露的逻辑错误 | 1) 开 `/W4` 看 C4700 / C4701；2) 开 ASan；3) 检查所有 `assert` 里的表达式有没有副作用；4) 检查依赖 UB 的代码（有符号溢出、越界、悬垂） |
| Release 通过，Debug 崩 | 调试堆 / `/RTC1` / `_ITERATOR_DEBUG_LEVEL=2` 检查出了**本来就存在的**越界或迭代器失效；Debug 下的断言在替你报错 | 这不是「Debug 有问题」，而是「Debug 帮你发现了 bug」，按断言 / 异常信息去修 |
| 只在你机器上崩 | 硬件差异（对齐、指令集）、环境差异（路径、区域设置、编码）、并发时序 | 记录完整环境；比对本机与同事机器的依赖版本；先把时序问题排除（加日志看顺序） |

### 5.2 常见编译 / 链接错误速查表

按「错误信息 → 原因 → 解决」组织。链接错误（`LNK`）优先看，因为它们最难猜。

| 错误码 | 典型信息 | 原因 | 解决 |
| --- | --- | --- | --- |
| `LNK2019` | `unresolved external symbol` | 只声明没定义；`.cpp` 没加进工程；模板的定义放进了 `.cpp`（只有声明可见）；C 函数忘了 `extern "C"`；需要链接的库（如 `dbghelp.lib`）没加 | 补定义 / 把 `.cpp` 加进构建（CMake 里加进 `add_executable` 的源列表）；模板定义放头文件或显式实例化 + `extern template`；C 接口加 `extern "C"`；`target_link_libraries` 补库 |
| `LNK2005` | `already defined in xxx.obj` | 头文件里定义了非 `inline` 的全局函数 / 全局变量；同一个 `.cpp` 被两个 target 或两个工程重复注册；两套 CRT 混入 | 头文件里的函数加 `inline`，变量加 `inline`（C++17）或放 `.cpp`；把「只在本文件用」的放进匿名 `namespace`；**同一个源文件只让一个构建系统注册**（见 4.4） |
| `LNK1169` | `one or more multiply defined symbols found` | 上面 `LNK2005` 的总结性错误，通常紧随其后 | 同 `LNK2005` |
| `LNK2038` | `mismatch detected for '_ITERATOR_DEBUG_LEVEL'` 或 `'RuntimeLibrary'` | **Debug / Release 混用**：一部分模块用 `/MDd`，另一部分用 `/MD` | 让整个工程与所有第三方库使用同一个配置；重编依赖库；检查 `CMAKE_MSVC_RUNTIME_LIBRARY` |
| `LNK1104` | `cannot open file 'xxx.lib'` | 库路径不对 / 库名拼错 / 目标名写错（4.5 那个坑就是这种） | 检查 `target_link_libraries` 里的名字是否真的是一个存在的 target 或库；检查 `CMAKE_LIBRARY_PATH` |
| `LNK1120` | `N unresolved externals` | 和 `LNK2019` 配套出现，不是独立问题 | 解决每一条 `LNK2019` |
| `C4996` | `'strcpy': This function or variable may be unsafe` / `deprecated` | 用了被标记废弃的 CRT 函数（`strcpy`、`sprintf`、`localtime`、`scanf`）或 `[[deprecated]]` 接口 | 换成 `_s` 版本或标准库替代（`std::string`、`std::format`、`localtime_s`）；**最后一招**才是 `_CRT_SECURE_NO_WARNINGS`，且要写清理由 |
| `C4244` | `conversion from 'double' to 'float', possible loss of data` | 窄化转换（静默丢精度） | 用列表初始化 `float f{value};` 让它变成编译错误，或显式 `static_cast` 表明「我知道在丢精度」 |
| `C4267` | `conversion from 'size_t' to 'int', possible loss of data` | `size_t` 赋给 / 传给 `int` | 用 `std::size_t` / `std::ptrdiff_t`；确实要截断就 `static_cast` 并断言范围 |
| `C4018` | `'<': signed/unsigned mismatch` | `int` 与 `size()` 比较 | 索引 / 计数类型与 `size()` 一致；或 `std::cmp_less(a, b)`（C++20） |
| `C4101` | `unreferenced local variable` | 定义了没用 | 删掉；或 `[[maybe_unused]]`；或 `(void)x;` 并说明为什么保留 |
| `C4189` | `local variable is initialized but not referenced` | 初始化了但没读 | 同 `C4101`。常见于「忘了用返回值」 |
| `C4700` / `C4701` | `uninitialized local variable used` / `potentially uninitialized` | 变量未初始化就被读 | 定义即初始化；检查所有分支是否都赋值。这是 Debug / Release 行为差异的头号来源 |
| `C2065` | `'xxx': undeclared identifier` | 拼写错误、大小写错误、缺 `#include`、漏命名空间限定、宏没定义 | 检查拼写与命名空间；补 `#include`；用 `std::` 限定；头文件自包含性测试 |
| `C2084` / `C2086` | `function already has a body` / `redefinition` | 同一个函数被定义两次（头文件里定义且被多个 `.cpp` 包含是最常见的） | 加 `inline`，或把定义移到 `.cpp`，或加 include guard（后者只防重复包含，不防跨 TU 重复定义） |
| `C4819` | `The file contains a character that cannot be represented in the current code page` | 源码是 UTF-8 但编译器按 GBK 解析（源码里有中文注释） | 加 `/utf-8`，并确认文件真的存成 UTF-8（VS：文件 → 另存为 → 带编码保存） |
| `C4324` | `structure was padded due to alignment specifier` | `alignas` 造成的填充，结构体变大 | 这是提醒不是错误。确认填充是否符合预期；用 `/d1reportAllClassLayout` 看真实布局 |
| `C4820` | `'N' bytes padding added after data member` | 成员之间被插入填充字节 | 调整成员顺序（大对齐在前）；关心 ABI 时必须确认 |
| `C4061` / `C4062` | `enumerator ... is not explicitly handled by a case label` / `... not handled in switch` | `switch` 漏了枚举值 | 补全分支；或反过来**故意不写 default**，让新增枚举值时编译失败 |
| `C4715` | `not all control paths return a value` | 有分支没返回值（常在 `switch` 遗漏时出现） | 补全所有路径；末尾加不可达的 `throw` 或 `return` 让编译器满意 |
| `C4456` / `C4457` / `C4458` / `C4459` | `declaration of 'x' hides previous local` / `hides function parameter` / `hides class member` / `hides global` | 变量遮蔽 | 改名（成员加 `_` 后缀）；缩小作用域；开 `/W4` 就能自动发现 |
| `C4834` | `discarding return value of function with 'nodiscard' attribute` | 丢弃了 `[[nodiscard]]` 的返回值 | 接住并检查；确实要忽略就 `(void)func();` 并写理由。**这条警告非常值钱**，它挡住的是「错误被吞掉」 |
| `C4530` | `C++ exception handler used, but unwind semantics are not enabled` | 用了异常但没开 `/EHsc` | 加 `/EHsc` |
| `C1128` | `number of sections exceeded object file format limit` | 模板实例化过多，节数超限 | 加 `/bigobj`；或减少模板爆炸（`/Ob1`、`extern template`） |
| `C5105` | `macro expansion producing 'defined' has undefined behavior` | 形如 `#define _CRTDBG_MAP_ALLOC` 的官方用法会触发 | 精确 `#pragma warning(disable : 5105)` 并写明理由（本章 05 就是这么做的） |
| `C5038` | `data member 'a' will be initialized after data member 'b'` | 初始化列表顺序与成员声明顺序不一致 | 按声明顺序写初始化列表；这**不是风格问题**，顺序真的会影响结果 |
| `C4505` | `unreferenced local function has been removed` | 定义了局部函数但从未引用 | 删掉，或加 `[[maybe_unused]]`，或确实要用它（本章 04 用「自检调用」的方式保留 `measure_per_call`） |
| `MSB8020` / `MSB8036` | `The build tools for vXXX cannot be found` / `Windows SDK version was not found` | 项目要求的工具集 / SDK 本机没装 | VS Installer 装对应组件；或改项目的工具集 / SDK 版本；`CMakePresets.json` 里也指定 `toolset` |

### 5.3 调试动作清单（按成本从低到高）

1. **读异常码**，先判断是哪一大类（访问违例 / 栈溢出 / 堆破坏 / 未捕获异常）。
2. **看调用栈最底层属于你自己的帧**，而不是最顶层的库函数。
3. **写一个能稳定复现的最小例子**（这一步往往就解决了一半问题）。
4. **条件断点**：`i == 5000 && data[i] < 0`，避免手动 F5 五千次。
5. **数据断点**（VS：新建断点 → 数据断点，填 `&obj.member`）：
   这是找「谁改坏了我的变量」的唯一有效手段，用于堆破坏、不变量被破坏。
6. **`__debugbreak()`**：在断言失败处、不该走到的分支里主动断下。
   注意：不带调试器运行时它会直接崩（`int 3` 无人接管），所以只放在 Debug 专用代码里。
7. **二分注释法**：注释掉一半代码，看问题是否还在；每轮砍一半，上万行只要十几轮。
   如果是「改了某个提交之后才开始错」，用 `git bisect` 自动做这件事（前提是每次提交都能编译）。
8. **`_CrtCheckMemory()` 前后夹逼**：把可疑操作夹在两次检查之间，定位「谁写坏了堆」。
9. **AddressSanitizer**（`/fsanitize=address`）：编译期插桩、运行期检查越界、use-after-free、
   use-after-return、double-free、泄漏，出错时打印带完整调用栈的报告。
   与 `/RTC1`、增量链接、编辑继续（EnC）冲突，开了 ASan 就别开它们。
10. **崩溃转储**：生产环境用 `MiniDumpWriteDump` 存 dump（或用 WER 自动收集），
    事后用 VS 打开看调用栈。这是「线上崩溃可追溯」的标准方案。

**一个必须知道的现实**：本章 `05_debugging_techniques.cpp` 里实测发现，
本机这份新版 UCRT 的 `_CrtDumpMemoryLeaks()` 报告**能**报出
「检测到泄漏、块大小、块地址、块内容」，但**没有**打印文件名和行号 ——
也就是 `_CRTDBG_MAP_ALLOC` 的「定位到行」在这种 CRT 版本上不生效。
工程结论：想精确定位分配点，直接用
`_malloc_dbg(128, _NORMAL_BLOCK, __FILE__, __LINE__)`，
或者干脆用 ASan（默认带 LeakSanitizer 并打印调用栈）。

---

## 6. 性能优化手册

### 6.1 测量方法：让数字可信的八个要求

| 要求 | 为什么 | 怎么做 |
| --- | --- | --- |
| 用 `steady_clock` | `system_clock` 是墙上时钟，会被 NTP 校时、手动改时间、夏令时影响，可能**倒退**（测出负数） | 测耗时只用 `std::chrono::steady_clock`；`system_clock` 只用来回答「现在几点」 |
| 预热若干轮并丢弃 | 第一次执行总是偏慢：代码 / 数据没进缓存、分支预测器没学到模式、CPU 还在低频状态 | 预热 3 轮，结果全部丢掉 |
| 多轮迭代 | 单次测量会被调度、中断、其他进程打断，结果**只能偏大不会偏小** | 正式测 15 轮，收集样本 |
| 看最小值与中位数，别只看平均 | 最小值 = 「这段代码最快能多快」；中位数 = 「典型表现」；平均值容易被离群值拉高 | 三个都打印；最小值和中位数差距大说明环境不稳定，要重测 |
| 防止结果被优化掉 | 被测代码没有可观测副作用时，优化器会直接删掉它 —— 你测的是「什么都不做」的时间，这是最常见的假数据 | MSVC：`volatile` 中转 + `_ReadWriteBarrier()`；GCC / Clang：`asm volatile("" : : "r,m"(v) : "memory")` |
| 每轮至少几十毫秒 | 单次只有几十纳秒时，计时器分辨率本身就成了误差来源 | 先测「调用 N 次的总耗时」，再除以 N 得到人均耗时；测总耗时再除，不要反过来 |
| 在 Release 下得出结论 | `/Od` 会把小函数内联、常量折叠、向量化全部关掉，收益被严重低估甚至反向 | 任何性能结论都必须在 `/O2` 下得到；Debug 只用来调试 |
| 记录测量环境 | 没有环境的数字无法复现，也无法和别人比较 | 记下配置、编译器版本、机器、是否有其他任务在跑 |

**控制变量**：数据规模、随机种子、循环次数都要固定（本章用 `std::mt19937(12345u)`，
保证每次运行数据完全一样，结果可复现）。改一个变量测一次。

**分工**：

- 微基准（micro-benchmark）：验证「这两行代码哪个快」→ 本章的 `measure()`。
- 采样 / 插桩剖析（profiling）：找真实程序的热点 → Visual Studio 性能探查器（Alt+F2）、
  `perf`（Linux）、VTune。**真实项目里先做剖析，再做微基准** ——
  否则你优化的是一个根本不热的函数。

### 6.2 优化性价比排序

工程经验值（配合实测使用），**从上到下**做：

| 优先级 | 手段 | 典型收益 | 风险 / 代价 |
| --- | --- | --- | --- |
| 1 | 换算法、换数据结构（`O(n²)` → `O(n log n)`） | 10 到 1000 倍，n 越大差距越大 | 需要想清楚，但最值 |
| 2 | 减少内存分配（`reserve`、对象池、复用缓冲、环形缓冲） | 2 到 10 倍 | 内存占用上升 |
| 3 | 减少拷贝与临时对象（`const&`、`std::move`、`emplace_back`） | 1.5 到 10 倍 | 可读性略降 |
| 4 | 改善缓存局部性（连续容器、调整遍历顺序、扁平数组） | 2 到 25 倍 | 数据结构要改 |
| 5 | 编译期计算（`constexpr`、模板、查表） | 1.2 到 5 倍 | 编译时间变长 |
| 6 | 分支与内联（`likely` / `unlikely`、`noexcept`、`inline`） | 1.05 到 2 倍 | 可移植性下降 |
| 7 | 手写 SIMD / 汇编微调 | 1.1 到 4 倍 | 极难维护，最后考虑 |

**读法**：前四项通常带来数量级收益，后两项只有个位数百分比。
本章 `04` 里现场实测也印证了这一点：抠 `reserve` / `emplace_back` 只能拿到
1.2 到 2 倍（源文件里记录的观察值），而把 `O(n²)` 换成 `O(n log n)` 是数量级。
**所以永远先问「有没有更好的算法 / 数据结构」，再问「这行代码怎么写更快」。**

### 6.3 常见性能陷阱

| 陷阱 | 为什么慢 | 对策 |
| --- | --- | --- |
| 在 Debug 下下性能结论 | `/Od` 关掉内联与向量化 | 用 Release；Debug 只用来调试 |
| 循环里拼日志字符串 | 字符串拼接会分配内存；即使日志级别没开也在拼 | `if (logger.enabled(...))` 或 `if constexpr` 包住（本章 03 的 `LOG_DEBUG`） |
| `push_back` 不 `reserve`，元素还很贵 | 反复扩容 + 每次扩容都要搬移全部元素 | 先 `reserve`；元素是 `std::string` / 大结构体时收益最明显；元素是指针时几乎无差别 |
| 先构造临时对象再 `push_back` | 构造一次 + 移动一次 | 用 `emplace_back` 直接传构造参数 |
| `std::vector<std::vector<T>>` 存二维数据 | 每行一次堆分配，行间不连续，还多一层指针跳转 | 用扁平一维数组 + 手动算下标（本章 `FlatMatrix`）；二维数据优先「一维 + stride」 |
| 按值传大对象 | 每次调用一次拷贝（本章实测 `BigPod` 1 KB 是数量级差距） | 大对象用 `const&`；小 POD（不超过两个指针大小）按值反而可能更快 |
| 在循环里 `operator+` 拼字符串 | 每个 `+` 都可能产生临时对象和一次分配 | `reserve` + `+=` / `append` |
| 用异常表达「常见的失败」 | 失败路径要栈展开 + 构造异常对象，比返回错误码贵两个数量级 | 解析 / 校验 / 协议这类高失败率路径用返回码或 `std::expected` |
| 分支不可预测 | 每次预测失败要清空流水线，约 15 到 20 个周期 | 让数据有序（排序后处理在统计上常常更快）；或用无分支写法；**但要实测** —— 编译器可能已经帮你做了变换 |
| 单次测量 / 不预热 | 冷启动明显偏慢（缓存空、未升频） | 预热 + 多轮 + 取最小值 |
| 被测代码被优化掉 | 优化器发现结果没人用，直接删掉 | `do_not_optimize` / `volatile` 中转 |
| 无条件 `flush` 日志 | 每次写盘都是系统调用 | 只在 WARN 以上 flush（本章 03 的做法）；或批量落盘 |
| 锁粒度过大 / 锁竞争 | 多线程互相等待，CPU 空转 | 缩小临界区；无锁数据结构（谨慎）；`local_shared_ptr` 这类非原子计数方案 |

### 6.4 实测数据汇总

**注意三件事**：绝对值随机器、负载、编译器版本变化；趋势稳定；下面每行都标了来源。

**A. 本章已验证的构建与测试结果**（见 0.2 节）

| 项目 | 结果 |
| --- | --- |
| `build.ps1 -Chapter 07-engineering -WX` | 6 / 6 成功，零警告 |
| 独立 CMake 工程（Debug x64，VS 18 2026 生成器） | 生成 + 构建成功 |
| `ctest --test-dir build-cmake -C Debug` | 100% passed，2 / 2 |
| 6 个示例 exe | 退出码全 0 |

**B. 本章 `04_performance_measurement.cpp` 内的对照项**（倍数由程序现场打印，
下表是「测了什么」的清单，具体数字以你本机运行为准）

| 对比项 | 结论方向 |
| --- | --- |
| 冷启动单次测量 vs 预热 3 轮后测 15 轮 | 冷启动明显偏慢；平均值 > 中位数 > 最小值是常态 |
| 随机数组 + `if` vs 有序数组 + `if` | 数据内容相同、指令条数相同，仅排列顺序不同就能差好几倍；现代 CPU 一次预测失败约 15 到 20 周期 |
| 随机数组 + 无分支写法 vs 随机数组 + `if` | Debug 下无分支写法甚至更慢；Release 下才明显胜出 —— **实测，别凭直觉** |
| 列优先 vs 行优先遍历 2048 x 2048 矩阵 | 指令数与算法复杂度相同，差距完全来自内存访问模式（64 字节缓存行 = 16 个 `int`，列优先只用 4 字节） |
| `push_back` 不 `reserve` vs 先 `reserve` | 收益来自消除扩容与元素搬移；元素越贵收益越大 |
| `push_back` vs `emplace_back`（都 `reserve`） | 省掉「构造临时对象 + 移动」；构造代价低时编译器可能已经优化掉 |
| `SmallPod`(8B) 按值 vs `const&` | 小 POD 按值通常不差，甚至更快（少一次解引用） |
| `BigPod`(1KB) 按值 vs `const&` | 按值等于每次调用拷 1 KB，数量级差距 |
| 多次 `operator+` 拼字符串 vs `reserve` + `+=` | 字符串是「看起来很便宜的昂贵操作」 |
| `O(n²)` 暴力枚举 vs 排序 + 二分 `O(n log n)`（n = 4000） | 数量级收益，n 越大差距越大 |
| Debug vs Release（400 万元素条件求和） | 源文件里记录的观察值：Debug 约 10.5 到 14.1 ms，Release 约 6.4 到 7.9 ms（约 1.6 到 2 倍）。源文件同时注明：纯循环 2 到 5 倍，大量小函数调用 10 到 50 倍，模板 / STL 密集更大 |

**C. 来自 `08-boost/README_Boost_vs_STL.md` 的实测数据**（本机 Release x64 现场跑出来的；
来源：`C:\Users\Waj07\Desktop\CPPstudy\cpp-notes\08-boost\README_Boost_vs_STL.md` 第「二点五」节）

| 对比项 | 结果 | 结论 |
| --- | --- | --- |
| 同一批功能的编译耗时（各 3 次取平均） | STL 2641 ms vs Boost 4768 ms | Boost 慢 **1.81 倍**（纯头文件模板展开的重） |
| `std::format` vs `boost::format`（20 万次） | — | Boost 慢 **4.2 倍** |
| `from_chars` vs `lexical_cast`（20 万次往返） | — | STL 快 **3.6 倍** |
| `std::shared_ptr` vs `local_shared_ptr`（200 万次拷贝） | — | Boost 快 **16 倍**（单线程；原子计数的代价） |
| `boost::regex` vs `std::regex`（4 万行匹配） | — | Boost 快 **2.1 倍** |
| `flat_map` vs `std::map`（小表 2000 项） | — | `flat_map` 快 **1.0 到 4 倍**（连续内存 + 二分） |
| **抛异常 vs 返回 `error_code`（20 万次失败）** | — | **异常慢约 430 倍**（每次异常仅 1 到 2 微秒） |
| 该工程 12 个 `.cpp` 完整重编译 | 约 38 秒（`/m:1`） | 主要时间花在 Boost 头文件上 |

这组数据给本章 `03_logging_and_error_handling.cpp` 的「成功路径免费、失败路径昂贵」
提供了外部佐证：**异常的开销在栈展开，量级是微秒，不是纳秒**。
所以判据不是「异常好不好」，而是「失败有多频繁」：

| 失败频率 | 推荐策略 |
| --- | --- |
| 极其频繁（解析 / 校验 / 协议） | 返回码 / `std::expected` |
| 「没有值」是正常语义（查找） | `std::optional` |
| 失败要带原因且要层层上报 | `std::expected` |
| 错误要跨多层展开栈、逐层 `if` 会把代码写烂 | 异常 |
| 构造函数失败（没有返回值的替代方案） | 异常 |
| C 接口 / 嵌入式 / 禁异常 / 硬实时 | 返回码 |

**D. 来自 `模板/template_demo/README.md` 的编译选项速查**
（来源：`C:\Users\Waj07\Desktop\CPPstudy\模板\template_demo\README.md` 第二节；
那是一个模板专题工程，这里吸收的是它的「选项 → 不给会怎样」对照，已经并入第 3.1 节的表格）

它额外提醒的两条值得单独记：

- `/std:c++20`：**VS 默认是 C++14**，新建项目不改的话 concepts / 折叠表达式 / CTAD 全部报错。
- `/bigobj`：模板实例化极多时报 `fatal error C1128`，加它就行，没有代价。

---

## 7. 版本控制与协作习惯

### 7.1 `.gitignore` 该写什么

本仓库的 `cpp-notes/.gitignore` 已经是一份很好的范例，结构是「按来源分组」：

| 分组 | 典型条目 | 为什么要 |
| --- | --- | --- |
| 构建产物 | `build/`、`out/`、`bin/`、`*.exe`、`*.obj`、`*.lib`、`*.pdb`、`*.ilk` | 它们都能从源码重新生成；进库只会造成冲突和仓库膨胀 |
| Visual Studio | `.vs/`、`*.vcxproj.user`、`*.suo`、`*.user`、`x64/`、`Debug/`、`Release/`、`*.tlog`、`*.recipe` | 用户级设置与中间产物；`*.tlog` 里全是本机路径 |
| CMake | `CMakeCache.txt`、`CMakeFiles/`、`cmake_install.cmake`、`CTestTestfile.cmake`、`Testing/`、`compile_commands.json`、`CMakeUserPresets.json`、`build-*/` | 构建目录里的内容全部是本机绝对路径，换台机器就没用；`build-*/` 正好覆盖本章的 `build-cmake/` |
| 编辑器 / 系统 | `.vscode/`、`.idea/`、`*.swp`、`Thumbs.db`、`Desktop.ini` | 个人偏好，不该强加给所有人 |
| 临时实验 | `scratch/`、`tmp/`、`*.tmp`、`a.out` | 草稿代码不进库 |

**本章特有的三条**（建议补进 `.gitignore` 或至少心里有数）：

```text
# 本章示例运行时会往「当前工作目录」写文件，所以要忽略：
07-engineering_log_demo.txt
07-engineering_raii_demo.txt
# 本章独立 CMake 工程的构建目录（已被 build-*/ 覆盖，这里显式再写一遍更保险）
07-engineering/build-cmake/
```

前两条是实际会发生的：本章 `03_logging_and_error_handling.cpp` 会写
`07-engineering_log_demo.txt` 和 `07-engineering_raii_demo.txt`。
如果你用 `build.ps1` 从 `cpp-notes/` 目录运行，它们会落在 `cpp-notes/` 根目录；
如果从 `07-engineering/` 运行，就落在章节目录里。
**写文件到「当前工作目录」是真实项目里的常见 bug 来源**，
更好的做法是写到明确指定的输出目录（测试那边就用
`WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"` 做了这件事，见 `tests/CMakeLists.txt`）。

**不要进 `.gitignore` 的东西**：`.clang-format`、`.editorconfig`、`CMakePresets.json`、
`CMakeLists.txt`、`README.md`、测试数据、小的二进制资源。
它们都是「团队共享的约定」，必须进库。

### 7.2 git 工作流

**基本纪律**：

1. **一个提交做一件事**。修 bug 和改格式不要混在一个提交里 —— 否则 review 时
   真正的逻辑改动会被淹没在格式 diff 里。
2. **提交前必须能编译、能过测试**。这是 `git bisect` 有效的前提
   （本章 05 讲的二分法，在 git 上就是 `git bisect`）。
3. **提交信息写「为什么」，而不是「改了什么」**。`git diff` 已经说明了改了什么。
4. **分支上的改动要 rebase 到最新主线**再合并，避免无意义的 merge 提交。
5. **大文件、构建产物、密钥永不进库**。密钥一旦进库，就算删掉也在历史里。

### 7.3 Conventional Commits（提交信息规范）

格式：

```text
<type>(<scope>): <subject>

<body：为什么这么改、有什么取舍>

<footer：BREAKING CHANGE / 关联 issue>
```

| type | 含义 | 例子 |
| --- | --- | --- |
| `feat` | 新功能 | `feat(07-engineering): 增加独立 CMake 工程与 CTest 测试` |
| `fix` | 修 bug | `fix(07-engineering): tests 链接的警告目标名改为 project_warnings` |
| `docs` | 只改文档 | `docs(07-engineering): 补写 NOTES.md 的调试与性能手册` |
| `refactor` | 重构（行为不变） | `refactor(tests): 把断言宏统一成 do-while-false 形式` |
| `perf` | 性能优化 | `perf(04): 用扁平数组替代嵌套 vector 以改善缓存局部性` |
| `test` | 只改测试 | `test(07-engineering): 补环形缓冲容量为 1 的用例` |
| `build` | 构建系统 / 依赖 | `build(07-engineering): 默认在顶层 CMake 中跳过本章` |
| `ci` | CI 配置 | `ci: 增加 Debug / Release 两个构建任务` |
| `style` | 纯格式 | `style: 应用 .clang-format` |
| `chore` | 杂项 | `chore: 更新 .gitignore` |

好处：能自动生成 changelog；能按 type 过滤（只看 `fix` 就知道这个版本修了什么）；
**`fix` 与 `feat` 的界限逼你想清楚这次改动到底改了什么**。

### 7.4 分支策略简介

| 策略 | 怎么做 | 适合 |
| --- | --- | --- |
| 主干开发（trunk-based） | 所有人往 `main` 提交，功能用「特性开关」控制；分支活不过一两天 | 持续交付、部署频繁、测试自动化程度高的团队 |
| GitHub Flow | `main` 永远可发布；每个改动开一个短分支 + Pull Request，评审后合并 | **大多数团队和开源项目的默认选择**，推荐从它开始 |
| Git Flow | `main` / `develop` / `release/*` / `hotfix/*` / `feature/*` | 有明确的版本发布周期、需要同时维护多个已发布版本的产品 |
| 单一长期分支 + 标签 | 只维护 `main`，发版打 tag | 个人项目、教学仓库（本仓库就属于这一类） |

**两条通用规则**（比选哪种策略更重要）：

1. **`main` 永远处于可发布 / 可编译状态**。
2. **分支要短命**。活过一周的分支，合并冲突的成本会指数上升。

**与本章其它内容的关系**：
`git bisect` 是第 5 节「二分注释法」的自动化版本，靠的是「每次提交都能编译」；
代码评审（第 8 节）是「人工的最后一道防线」，只应该用来检查工具查不出来的东西。

---

## 8. 代码评审检查清单

直接拿去用。分六组，共 22 条。**能靠工具查的，就不要靠人眼**（把 `/W4 /WX`、
clang-format、静态分析先跑一遍，再用这份清单看逻辑）。

### 资源与所有权

1. 有没有裸 `new` / `delete`、裸 `malloc` / `free`？（应该用 `unique_ptr` / 容器 / `std::string`）
2. 每个资源的释放路径是否唯一且由析构负责（RAII）？有没有提前 `return`、`break`、`continue` 会漏掉的清理？
3. 有多个 `return` 点或可能抛异常的函数，清理动作有没有绑在作用域上（`ScopeGuard` / `lock_guard` / `ofstream`）？
4. 谁拥有这块内存、谁只借用？所有权是否在类型上表达清楚（`unique_ptr` vs 引用 vs `shared_ptr`）？

### 接口与类型

5. 每个单参数构造函数都加 `explicit` 了吗？（除了确实要参与隐式转换的，如 `string_view` 从 `const char*`）
6. 虚函数重写都写了 `override` 吗？不再被继承 / 重写的类型和函数不写 `final` 是有意的吗？
7. 有返回值的纯查询函数、返回错误码的函数、返回新对象的函数，都加 `[[nodiscard]]` 了吗？
8. 能 `const` 的成员函数和参数都 `const` 了吗？加不上的地方（缓存、锁、计数器）写清理由了吗？
9. 参数类型选对了吗？小 POD 按值、大对象 `const&`、需要副本时按值 + `std::move`。
10. 容易写反的参数（宽 / 高、米 / 秒、度 / 弧度）有没有用强类型区分？
11. 基类的析构函数是 `virtual` 吗？（否则通过基类指针 `delete` 派生类对象是未定义行为）
12. 是否用类型而不是注释表达了契约（`optional` / `expected` / `enum class` / 强类型）？

### 错误处理

13. 系统边界（用户输入、文件、网络、第三方返回值、`errno`）的返回值**每次**都检查了吗？
14. 有没有把「可预期的失败」写成断言（`assert` 对外部输入），或者把「不可能发生」写成静默的 `if` 忽略？
15. `assert` 的表达式里有副作用吗？（`assert(++i < n)` 在 Release 下 `i` 不会增长）
16. 整个模块的错误处理策略统一吗？（不能一半抛异常、一半返回错误码）
17. 有没有「因为懒得想」而跳过错误检查的路径？有没有吞掉错误（空 `catch`、忽略返回值）？
18. 日志是否带级别、并且热路径里的调试日志能在 Release 下编译期消失？有没有在循环里无条件拼日志字符串？

### 并发与内存安全

19. 共享数据是否都有明确的同步手段？临界区是否最小？有没有「先判断再操作」的竞态（`check-then-act`）？
20. 有没有可能失效的迭代器 / 指针 / 引用（容器扩容、`erase`、返回局部变量地址、`string_view` 指向临时 `string`）？有没有悬垂的 `string_view` / `span`？

### 构建、风格与测试

21. 新增文件有没有加进构建脚本（CMake 的源列表 / 新 target）？头文件是否自包含（单独编译能过）？头文件里有没有 `using namespace`？
22. 这次改动有对应的测试吗？测试只依赖退出码吗？边界情况（空输入、容量为 0、容量为 1、越界一位、失败时不污染输出）覆盖了吗？

**评审时的两条心态建议**：

- 评论要针对代码，不针对人；提出问题时附上「为什么」和一个替代方案。
- 如果一个错误可以被工具永久挡住（加一条警告、加一个测试、加一个 `static_assert`），
  **就在这次评审里把它变成工具规则**，而不是期待下次记得。

---

## 9. 学习路线建议

每条都给出「解决什么问题」，以及「学到什么程度就够用」。
推荐顺序就是从 1 到 6（前两个立刻能做，后两个等有团队 / 项目再说）。

| 顺序 | 主题 | 解决什么问题 | 学到什么程度 |
| --- | --- | --- | --- |
| 1 | GoogleTest / Catch2 | 「测试写得慢、断言不够用」：真实的测试框架提供 `TEST` 自动注册、`EXPECT_THROW`、参数化测试、`gtest_discover_tests` 与 CTest / CI 的一键集成 | 会写 `TEST` / `TEST_F`、会断言异常与浮点近似、会把测试注册进 CTest。**先掌握 GoogleTest**（面试与工业界最常见），Catch2 作为「单头文件更轻」的备选 |
| 2 | clang-tidy | 「评审意见重复出现」：把「该用 `nullptr`」「`std::move` 用错」「没有 `override`」「循环变量可以 `const`」这类机械问题交给工具，评审只讨论逻辑 | 会写 `.clang-tidy`、会用 `--fix` 批量修复、会把 `compile_commands.json` 喂给它。**注意：本机未安装，需要自行安装** |
| 3 | clang-format + `.editorconfig` | 「格式争论」与「diff 噪音」：让机器决定缩进、换行、include 排序 | 会写最小 `.clang-format`（`BasedOnStyle` / `IndentWidth` / `ColumnLimit` / `SortIncludes` / `PointerAlignment`）、会配置保存时自动格式化。**注意：本机未安装 clang-format，需要自行安装** |
| 4 | 性能剖析（profiling） | 「不知道该优化哪里」：采样剖析告诉你真实的热点，而不是你「觉得」的热点 | 会用 VS 性能探查器（Alt+F2）、会读火焰图 / 调用树、能区分「CPU 热点」与「等待（IO / 锁）」。做到「只优化总时间占比超过 5% 的函数」就够 |
| 5 | CI/CD（GitHub Actions / GitLab CI） | 「只有我机器上能编」与「改动没人验证」：每次 push 自动编译 + 跑测试，把第 4.5 节那类「换个构建入口就崩」的问题在合并前抓住 | 会写一个「矩阵构建」的 workflow：Debug 与 Release、MSVC 与 GCC/Clang 交叉组合；会跑 `ctest` 并用退出码判定；会把 ASan 单独放一个 job |
| 6 | vcpkg / Conan | 「第三方库怎么装、怎么保证一致」：依赖的获取、版本锁定、可复现构建 | 会用 vcpkg manifest（`vcpkg.json`）+ `find_package`，或 Conan 的 profiles；理解「为什么不能靠「我机器上装好了」来交付」 |

**一句提醒**：这六项里，**第 1 和第 4 项的性价比最高**。
会写测试 + 会用剖析器，就能覆盖「改对了没有」和「快不快」这两个最实际的工程问题。

---

## 10. 自测题（6 道，含答案）

**题 1**：为什么 `assert` 里的表达式**绝对不能有副作用**？
如果写成 `assert(++index < n);`，在 Debug 和 Release 下分别会发生什么？

**答案**：因为 `assert` 在定义了 `NDEBUG` 时（Release）会被预处理阶段整体替换成
`((void)0)`，表达式**根本不参与编译**。
所以 Debug 下 `index` 会自增，Release 下 `index` 一次都不增长 ——
同一个程序在两种配置下行为不同，这是最难查的一类 bug。
同理，`assert(p != nullptr);` 在 Release 下不检查，之后解引用就崩；
`assert(do_something());` 在 Release 下那个函数根本不会被调用。
判据：**如果这段检查在 Release 里被删掉会导致行为错误，它就不是断言，是错误处理。**

---

**题 2**：一个模块用 `/MDd` 编译、另一个用 `/MD` 编译，链接过了但程序一跑就崩在 `free` 里。
请解释原因，并说出至少两种可能看到的错误信息。

**答案**：`/MDd` 与 `/MD` 链接的是**两套不同的 CRT**，各自有自己的堆。
模块 A 在 `/MDd` 的堆上分配，交给模块 B 用 `/MD` 的堆去释放 ——
把不属于这个堆的指针交给它释放，直接堆破坏 / 访问违例，
所以崩在 `free` / `_heap_alloc` 内部。
可能看到的错误信息：

- 链接期：`LNK2038: mismatch detected for '_ITERATOR_DEBUG_LEVEL': value '2' doesn't match value '0'`，
  或 `RuntimeLibrary` mismatch；也有 `LNK2005` / `LNK1169`；
- 运行期：`0xC0000005` 访问违例，崩在 `free` / `_heap_alloc` / `RtlValidateHeap` 里，
  或者「程序跑完报堆损坏」；
- 还有一种更糟的情况：**什么都不报，只是数据静默损坏**（`std::string` / `std::vector`
  因 `_ITERATOR_DEBUG_LEVEL` 不同而布局不一致）。
  对策：整个工程与所有第三方库使用同一个配置；CMake 里用
  `CMAKE_MSVC_RUNTIME_LIBRARY` 显式固定。

---

**题 3**：本章 `tests/CMakeLists.txt` 里原本写的是
`target_link_libraries(test_checked_int PRIVATE cppnotes_warnings)`。
为什么在「独立构建本章」时它会出问题？为什么 CMake 在配置阶段没有报错？
如果确实需要跨工程复用这个警告目标，有哪几种正确写法？

**答案**：`cppnotes_warnings` 是**顶层** `cpp-notes/CMakeLists.txt` 定义的目标，
本章独立工程的 `CMakeLists.txt` 定义的目标叫 `project_warnings`。
独立构建时顶层工程没有参与，那个目标根本不存在。
因为 `cppnotes_warnings` 里没有 `::`，CMake 不把它当作目标引用，
而是当成「要链接的库名」，于是把它原样传给链接器 ——
失败推迟到链接期，MSVC 上是 `LNK1104: 无法打开文件 cppnotes_warnings.lib`，
GCC 上是 `cannot find -lcppnotes_warnings`。
（如果名字写成 `Something::Missing` 这种带 `::` 的形式，
CMake 会在**配置阶段**直接报 target not found —— 错误暴露得更早，这是好事。）

正确写法：

1. 独立工程自己定义自己用（`add_library(project_warnings INTERFACE)`），不假设外部存在；
2. 用 `add_library(myproject::warnings ALIAS project_warnings)` 给下游一个带命名空间的稳定名字；
3. 用 `if(TARGET cppnotes_warnings)` 判断后再选择链接哪个目标，否则退回自己定义的；
4. 真要跨工程复用，必须由**同一个配置过程**引入该工程
   （`add_subdirectory` 或 `find_package` 拿到导入目标），否则目标不存在。

核心规则：**target 名只在同一次配置过程的作用域里有意义。**

---

**题 4**：为什么测耗时必须用 `std::chrono::steady_clock` 而不是 `system_clock`？
在「预热 + 多轮」得到的样本里，最小值、中位数、平均值分别说明什么？
为什么不能只测一次？

**答案**：`system_clock` 是墙上时钟，会被 NTP 校时、手动改时间、夏令时调整影响，
可能**倒退** —— 用它测间隔可能得到负数。
`steady_clock` 单调递增（Windows 上底层是 `QueryPerformanceCounter`），
专门用于测时间间隔；`system_clock` 只用来回答「现在几点」。

- **最小值**：这段代码在理想情况下的最快速度。因为调度、中断、其他进程
  只能让测量**变慢**，不会让它变快，所以最小值是最稳定的指标。
- **中位数**：典型表现。
- **平均值**：容易被离群值拉高；三者差距大说明测量环境不稳定，应该重测。

不能只测一次，因为：单次测量会被系统调度和中断干扰；第一次执行必然偏慢
（代码 / 数据没进缓存、分支预测器没学到模式、CPU 可能还在低频状态）。

---

**题 5**：抛出异常和返回 `error_code`，在**成功路径**和**失败路径**上分别是什么代价？
本章引用的实测数据说明了什么？给出一个「该用异常」和「该用返回码」的具体场景。

**答案**：

- **成功路径**：异常是**零成本**的 —— 没有抛异常时没有任何额外开销，
  和返回码同量级（本章 `03` 程序现场测量证实了这一点）。
- **失败路径**：异常昂贵 —— 要栈展开并构造异常对象，量级是**微秒**而不是纳秒。
  来自 `08-boost/README_Boost_vs_STL.md` 的现场实测：20 万次失败下，
  抛异常比返回 `error_code` **慢约 430 倍**（每次异常 1 到 2 微秒）。

所以判据不是「异常好不好」，而是「**失败有多频繁**」：

- 该用**异常**：错误要跨多层函数展开栈、逐层 `if` 检查会把代码写烂；
  或者构造函数失败（构造函数没有返回值的替代方案）。
- 该用**返回码 / `std::expected`**：失败极其频繁的正常业务路径
  （解析、校验、协议处理）；或者 C 接口 / 嵌入式 / 禁用异常 / 硬实时系统
  （异常延迟不可预测）。

补充一条同样重要的纪律：**不要在同一个模块里混着用**，
否则调用者永远不知道该 `try` 还是该查返回值，错误一定会被漏掉。

---

**题 6**：一个程序在 Debug 下一切正常，Release 下偶发崩溃或结果错误。
请按成本从低到高的顺序给出你的排查步骤。

**答案**：

1. **开 `/W4`（最好 `/WX`）看 C4700 / C4701**：未初始化变量是这类现象的头号原因
   （Debug 下栈恰好是 0 或 `0xCC`，Release 下是上一帧的垃圾）。
2. **检查所有 `assert`**：表达式里有没有副作用？Release 下这些检查消失了，
   会不会导致后面的代码对着未校验的数据继续跑？
3. **检查 `LOG_DEBUG` 之类的「Release 下会消失的代码」**：逻辑有没有藏在里面。
4. **开 AddressSanitizer（`/fsanitize=address`）跑一遍测试**：
   越界、use-after-free、use-after-return、double-free 会带调用栈报出来。
5. **检查未定义行为**：有符号溢出、除零、越界、悬垂指针 / 迭代器失效、
   返回局部变量地址、`string_view` 指向已销毁的 `string`。
   特别注意：优化器**有权假设 UB 不会发生**，所以 UB 在 Release 下的表现可以任意。
6. **检查运行时库与依赖是否成套**（`/MDd` vs `/MD`），以及所有第三方库的配置。
7. **用二分法缩小范围**：
   - 代码层面：二分注释法（每轮砍一半）；
   - 提交层面：`git bisect`（前提是每次提交都能编译）；
   - 只在某台机器崩：怀疑硬件差异（对齐、指令集）、环境差异、并发时序。
8. **上数据断点 / `_CrtCheckMemory()` / 崩溃转储**：当怀疑「谁改坏了我的内存」时。

**注意方向性**：不是「Release 才有 bug」，而是「Debug 下被掩盖了」。
所以修完之后一定要在**两种配置下都跑一遍测试**。

---

## 11. 本章的已知边界

诚实说明本章**没有**验证或没有覆盖的事，避免把未验证的东西当成已验证：

| 项 | 状态 |
| --- | --- |
| `cmake --preset debug` 这条路径 | **未在本机验证**：`CMakePresets.json` 的 `base` 预设用 Ninja 生成器，本机**未安装 Ninja，需要自行安装**。本机验证过的是手写 `-G "Visual Studio 18 2026"` 命令 |
| clang-format | **本机未安装，需要自行安装**。第 4.2 / 6 / 8 节提到的格式化配置属于「应该这样做」，没有在本机跑过 |
| clang-tidy | **本机未安装，需要自行安装** |
| ASan（`asan` 预设、`-DENGINEERING_ENABLE_ASAN=ON`） | 代码路径与 CMake 选项都已写好，但**没有在本机完整跑过一遍**（依赖 Ninja 预设或手工加选项） |
| GCC / Clang 编译 | 本章的跨编译器选项都用生成器表达式写好了，但**本机只有 MSVC**，GCC / Clang 路径未验证 |
| `std::expected`（C++23） | 本章统一用 `/std:c++20`，所以 `03` 里的 `expected` 分支由 `__cpp_lib_expected` 判断，本机走的是「不可用」分支 |
| `std::stacktrace`（C++23） | 同上：MSVC 需要 `/std:c++latest` 并链接 `dbghelp.lib`，本章用 `__cpp_lib_stacktrace` 判断，本机走的是「不可用」分支 |
| `_CRTDBG_MAP_ALLOC` 的「泄漏定位到行」 | 本机实测**不生效**：泄漏报告有块大小 / 地址 / 内容，但没有文件名和行号。替代方案见 5.3 与本章 `05` 的注释 |
| `04_performance_measurement.cpp` 的具体倍数 | 由程序在运行时现场打印，**本文件没有抄录具体数字**（会随机器变化）。只有源文件里明确记录的观察值（Debug 约 10.5 到 14.1 ms vs Release 约 6.4 到 7.9 ms）被引用 |

**下一步可以做的事**（如果要把本章再往前推一步）：

1. 安装 Ninja 与 clang-format，把 `cmake --preset` 与格式化流程真正跑通。
2. 写一个 GitHub Actions 工作流：Debug / Release 两个配置 + `ctest` + 一个 ASan job。
3. 把 `tests/mini_test.h` 换成 GoogleTest，体验「真实测试框架」与 CTest 的 `gtest_discover_tests` 集成。
4. 给本章的环形缓冲与严格整数解析加 `-fsanitize=address` 的 CI 任务，
   让「内存错误在提交前就被抓住」成为自动化事实。
