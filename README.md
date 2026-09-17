# cpp-notes —— C++ 学习笔记与可运行示例集

一份**面向软件开发方向、以工程实用为主**的 C++ 学习仓库。

特点：

- **能编译、能运行**。仓库里每个 `.cpp` 都自带 `main()`，配一条命令即可全部验证通过（MSVC `/W4 /WX` 零警告）。
- **讲清「为什么」**。每个坑都先演示错误写法会怎样，再给正确写法，能用 `static_assert` / `sizeof` / 实测耗时证明的绝不停留在口头。
- **面向工程**。不只讲语言特性，还讲编译选项、调试手法、性能测量、项目结构、代码评审清单这些真正决定工程质量的东西。
- **每章一份 `NOTES.md`**。代码是「证据」，文档是「结论与经验」。

---

## 目录

| 章节 | 主题 | 你会学到 |
|---|---|---|
| [01-basics](01-basics/) | 语言基础（重写 + 纠错） | 类型与转换、整数/浮点陷阱、运算符与流程控制、数组与指针与引用的关系、`printf` 家族、函数与作用域、内存模型、从源码到 exe 的完整链路 |
| [02-oop](02-oop/) | 面向对象 | 类的设计（不变量）、构造/拷贝/移动/析构、Rule of Three/Five/Zero、RAII、继承、虚函数与多态、对象切片、运算符重载、实战设计模式 |
| [03-modern](03-modern/) | 现代 C++ | 值类别与移动语义、智能指针、lambda 与 `std::function`、异常与错误处理、`constexpr`、结构化绑定与 `auto` 陷阱、`string_view`/`optional`/`variant`、ranges、并发基础、C++20 特性速览 |
| [04-templates](04-templates/) | 模板 | 函数/类模板、推导规则、特化与萃取、可变参数与完美转发、Concepts、CRTP 与策略设计、模板元编程、什么时候**不该**用模板 |
| [05-stl](05-stl/) | 标准库 | 容器选型决策表、`vector` 专项（扩容/迭代器失效/`reserve`）、关联容器与哈希、容器适配器、高频算法、`iostream` 与文件流；并附 C 标准库头文件用法与 C++ 替代品对照 |
| [06-algorithms](06-algorithms/) | 数据结构与算法 | 复杂度与测量、手写 `vector`/链表/栈队列/哈希表/BST/堆，与 STL 实测对拍；排序全家桶、图算法、动态规划；「看到什么题想到什么结构」的解题框架 |
| [07-engineering](07-engineering/) | 工程实践 | `/W4 /WX` 警告策略、断言与契约、日志与错误处理、可靠性能测量、调试手册、编译/链接错误速查、CMake、单元测试、项目结构与评审清单 |
| [08-boost](08-boost/) | Boost vs STL | 用同一份代码并排对比 11 个主题，实测性能数据，给出「实际项目该选哪个」的结论表（**独立工程，需外部 Boost**） |

---

## 快速开始

### 环境要求

| 项 | 本机实测值 |
|---|---|
| 操作系统 | Windows |
| 编译器 | Visual Studio 18 Community，MSVC 14.51.36231 |
| 语言标准 | C++20（`/std:c++20`） |
| 构建脚本 | Windows PowerShell 5.1 |
| CMake | 4.4.2（可选，用于真实项目式构建） |

### 方式一：一条命令验证全部示例（推荐）

不需要配任何环境变量，脚本会用 `vswhere.exe` 自己找到 Visual Studio：

```powershell
cd cpp-notes

.\build.ps1                 # 编译全部章节
.\build.ps1 -WX             # 编译全部章节，且警告视为错误（提交前跑这个）
.\build.ps1 -Run            # 编译并逐个运行
.\build.ps1 -Chapter 02-oop # 只编译某一章
.\build.ps1 -Clean          # 先清空 build 目录
```

编译产物统一放在 `build\<章节>\` 下，源码目录保持干净。

### 方式二：CMake（更接近真实项目）

```powershell
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --config Release
ctest --test-dir build-cmake --output-on-failure
```

### 方式三：在 Visual Studio 里打开

直接「打开文件夹」整个 `cpp-notes` 目录，或者用 CMake 生成 VS 工程。注意：**每个 `.cpp` 都自带 `main()`**，所以不要把它们加进同一个 VS 项目的「源文件」里，否则会报多重定义 `main`。

> ⚠️ 新建 VS 项目时第一件事是改语言标准：**项目属性 → C/C++ → 语言 → C++ 语言标准 → ISO C++20**。VS 的默认值往往是 C++14，不改的话 `concepts`、`std::format`、`ranges` 全都用不了。

---

## 环境特有的注意事项（实测踩过的坑）

**1. `.ps1` 文件必须存成 UTF-8 with BOM。**
Windows PowerShell 5.1 对没有 BOM 的 `.ps1` 会按系统 ANSI（中文系统是 GBK）解码，脚本里的中文注释会变成乱码字节，其中某些字节会被当成反引号 `` ` ``，直接把脚本语法搞崩。改完脚本记得别把 BOM 弄丢。

**2. MSVC 把 `__cplusplus` 恒定为 `199711L`，除非加 `/Zc:__cplusplus`。**
不加这个开关，任何 `#if __cplusplus >= 202002L` 之类的条件编译都会失效。本仓库的构建脚本和 CMake 都已加上。

**3. 中文源文件要加 `/utf-8`。**
否则会报 C4819 警告，甚至把 UTF-8 当 GBK 解析导致字符串字面量乱码。

**4. `#define NOMINMAX` 必须在 `#include <windows.h>` 之前。**
`windows.h` 定义的 `min`/`max` 宏会把 `std::max(a, b)` 展开成 `((a) > (b) ? (a) : (b))`，`std::min<T>`/`std::max<T>` 直接语法错误。顺序写反了等于没定义。

**5. 本机 C++23 库特性支持情况（实测，MSVC 14.51 / `/std:c++20`）**

| 特性 | 可用 | 备注 |
|---|---|---|
| `std::format` | ✅ | `__cpp_lib_format = 202304` |
| `std::ranges` | ✅ | 202110 |
| `std::span` / `std::jthread` / `<=>` / `std::bit_cast` / `std::source_location` | ✅ | |
| `std::expected` | ❌ | C++23 |
| `std::print` | ❌ | C++23 |
| `std::stacktrace` | ❌ | C++23 |
| `std::flat_map` | ❌ | C++23（`<flat_map>` 头文件存在但内容要求 C++23） |

所以本仓库的示例一律以 **C++20 为下限**，需要 C++23 的地方会明确标注并给出替代方案。

---

## 目录结构

```
cpp-notes/
├── build.ps1                 # 统一构建脚本（vswhere 找 VS + cl.exe 编译 + 可选运行）
├── CMakeLists.txt            # 等价的 CMake 构建（跨平台 / 生成 VS 工程）
├── .gitignore                # 构建产物、.vs/、VS 输出目录等都不进版本库
├── README.md                 # 本文件
├── 01-basics/                # 每章：若干自带 main() 的 .cpp + NOTES.md
│   ├── 01_xxx.cpp
│   └── NOTES.md
├── 02-oop/
├── 03-modern/
├── 04-templates/
├── 05-stl/
├── 06-algorithms/
├── 07-engineering/
├── 08-boost/                 # 独立 VS 工程，依赖外部 Boost，手动构建
└── build/                    # 编译产物（已 gitignore）
```

**约定**：一个 `.cpp` = 一个可以独立编译运行的小程序，文件名前缀是章内序号。这样任何时候都可以只挑一个文件单独编译，改坏了也只影响自己。

---

## 建议的学习路线

因人而异，但如果你是**软件开发方向、目标是写出能维护的代码**，建议这个顺序：

1. **01-basics** 快速过一遍，重点是「整数/浮点陷阱」「数组指针引用」「内存模型」——这些是后面所有 bug 的源头。
2. **02-oop** 认真学。工程代码的主体是类：类的职责是维护不变量，RAII 是 C++ 管理资源的唯一正确姿势。**Rule of Three/Five/Zero 和虚析构必须吃透。**
3. **03-modern** 认真学。所有权（ownership）是现代 C++ 的主线：智能指针 + 移动语义 + RAII 其实是同一件事的三个面。
4. **05-stl** 当工具书用。容器选型决策表和迭代器失效规则要背下来，其他用到再查。
5. **04-templates** 分两次学：第一次只学「会用」（函数模板、类模板、Concepts 约束），第二次再学元编程和 CRTP。
6. **06-algorithms** 和刷题并行。手写一遍是为了理解，之后一律用 STL；DP 和图的框架要练熟。
7. **07-engineering** 边做项目边看。这一章的价值在「遇到问题时能查到解法」。

---

## 说明

本仓库由原有的零散学习文件整合而成，整合过程中：

- 修正了原笔记中的事实性错误（每章 `NOTES.md` 里有「原笔记纠错清单」记录了改了什么、为什么）。
- 统一了构建方式、代码风格和中文注释规范。
- 补齐了原来缺失的面向对象、现代 C++、数据结构与算法、工程实践等内容。

原始素材（`模块/`、`模板/`、`boost/`、`math.h/`）保留在工作区里未删除，作为对照。
