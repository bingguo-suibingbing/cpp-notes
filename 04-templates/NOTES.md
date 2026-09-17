# 第 04 章 · 模板（Templates）

> 面向「实用派」：每一条结论都尽量给出**可编译的代码证据**和**本机实测数据**。
> 所有实测数据来自本项目环境，标注为「实测」的都可以用本目录下的文件复现。

**本机实测环境**

| 项 | 值 |
|---|---|
| Visual Studio | `C:\Program Files\Microsoft Visual Studio\18\Community`（VS 18 Community） |
| MSVC 工具集 | `14.51.36231` |
| 编译命令 | `.\build.ps1 -Chapter 04-templates -WX` |
| 实际编译选项 | `/std:c++20 /EHsc /W4 /WX /utf-8 /permissive- /Zc:__cplusplus /diagnostics:caret /bigobj /MDd /Zi` |
| 结果 | **8/8 编译通过，零警告** |

---

## 1. 本章地图

| 文件 | 主题 | 一句话结论 |
|---|---|---|
| `01_function_template.cpp` | 函数模板、推导、返回类型、NTTP、重载优先 | 推导**不做隐式转换**；显式模板实参**可以做**隐式转换；模板是纯编译期机制 |
| `02_class_template.cpp` | 类模板、默认实参、成员模板、CTAD、别名/变量模板、模板模板参数 | 自定义推导指引能把 `Box{"hi"}` 从 `Box<const char*>` 修正成 `Box<std::string>` |
| `03_specialization.cpp` | 全特化、偏特化、`void_t` 检测、手写 `enable_if`/`remove_const` | 偏特化是「模式匹配」；`void_t` 把「表达式是否合法」变成 `true_type`/`false_type` |
| `04_variadic_forwarding.cpp` | 变参、折叠表达式、完美转发、引用折叠、值类别计数 | `forward(lvalue)` → 1 拷贝 0 移动；`forward(rvalue)` → 0 拷贝 1 移动（实测） |
| `05_concepts.cpp` | C++20 concepts / requires / 缩写模板 | 约束参与重载决议的「更强优先」比较；不满足时编译器直接点名 **concept** 名字 |
| `06_crtp_policy.cpp` | CRTP 静态多态、基于策略的设计、CRTP 计数器、聚合初始化真相 | `sizeof(Point)==8` 且 `!is_polymorphic` → CRTP 零虚函数开销；**空基类也算聚合元素** |
| `07_template_metaprogramming.cpp` | `integer_sequence`/`index_sequence`、type traits、SFINAE vs concepts、`if constexpr` | `index_sequence` 是「用它驱动运行期展开」的标准手法；编译期分支已进化到第三代 |
| `08_practical_templates.cpp` | 强类型 ID、编译期字符串/哈希、ScopeGuard、策略模板 vs 虚函数实测、变参工厂、**不该用模板的判据** | 策略模板比虚函数快一倍以上（实测）；模板的价值是把约定从运行期搬到编译期 |

---

## 2. 模板的两条主线

学模板最容易迷路，因为「模板」其实是两个几乎独立的话题共用了一套语法。

### 主线 A：泛型（generic）—— 一份代码适配多种类型

目标：**消除重复**。`std::vector<T>`、`std::sort`、`std::make_unique` 都是这条线的产物。

- 关键机制：**模板参数推导**、**特化**、**重载决议**、**SFINAE / concepts**。
- 成本：代码膨胀（每个实例化一份）、编译时间、报错可读性。
- 收获：零抽象开销（能在编译期绑定的都能内联）。

### 主线 B：编译期计算（metaprogramming）—— 把工作从运行期搬到编译期

目标：**消除运行期开销**。`Factorial<5>::value`、`std::ratio`、`std::index_sequence` 属于这条线。

- 关键机制：**模板递归**、**偏特化做条件分支**、**`constexpr` / `consteval`**、**折叠表达式**。
- 成本：代码难读、编译慢、报错晦涩。
- 收获：运行期**一条指令都不多**。

两条线的交汇点是 **type traits**：它用主线 B 的手段（模板递归 + 偏特化）实现，服务于主线 A 的需求（「这个类型能不能 `+=`？」）。

> **实用派记住这一句就够了**：
> 模板真正不可替代的价值不是「少写代码」，而是**把错误从运行期提前到编译期**，
> 同时**不产生运行期开销**。凡是达不到这两点之一的场景，模板就不是最优解
> （见第 6 节决策清单）。

---

## 3. 重点难点详解

每一节按「**错误直觉 → 正确模型 → 代码证据 → 工程建议**」四段展开。

### 3.1 推导不做隐式转换，显式模板实参可以做

**错误直觉**

「`maxValue("a","b")` 应该等价于 `maxValue<std::string>("a","b")`，反正 `const char*` 能转成 `std::string`。」

**正确模型**

这两件事发生在**完全不同的阶段**：

| 写法 | 模板参数怎么定 | 形参类型 | 是否允许隐式转换 |
|---|---|---|---|
| `maxValue(3, 5.0)` | **从实参推导** | —— | 推导阶段**不做任何转换**，`T` 同时被推成 `int` 和 `double` → 编译错误 |
| `maxValue<double>(3, 5)` | **显式指定** | `double` | 转换发生在「调用」阶段，走普通函数的隐式转换规则 → 合法 |
| `maxValue("a","b")` | 从实参推导 | `const char*` | `T = const char*`，**能编译**，但比的是两个指针地址 |
| `maxValue<std::string>("a","b")` | 显式指定 | `std::string` | `"a"`（`const char[2]`）通过 `std::string` 的转换构造函数隐式转成临时对象 → 合法 |

一句话：**模板参数一旦确定，函数就退化成普通函数，普通函数怎么转换它就怎么转换；
推导阶段则只认「精确类型」。**

**代码证据**

`01_function_template.cpp` 里同时写出了两种调用，并用 `static_assert` 把返回类型钉死：

```cpp
std::cout << maxValue<std::string>("a", "b") << '\n';   // 输出 b
std::cout << maxValue("a", "b") << '\n';                // 输出 a（比的是指针地址）
static_assert(std::is_same_v<decltype(maxValue<std::string>("a", "b")), std::string>);
static_assert(std::is_same_v<decltype(maxValue<double>(3, 5)), double>);
static_assert(std::is_same_v<decltype(maxValue("a", "b")), const char*>);
```

**本机实测输出**（`build\04-templates\01_function_template.exe`）：

```
maxValue<double>(3, 5)  = 5
maxValue<std::string>("a","b") = b
maxValue("a","b")（推导）  = a   <- 返回 const char*，比的是指针
```

**顺手一提**：`maxValue("a","b")` 里两个字面量的地址大小是**未指定的**，
`a`/`b` 谁大谁小取决于编译器把字面量放在哪，**不要依赖这个结果**。
这条也是「为什么泛型代码里几乎不该用 `const char*` 做业务类型」的注脚。

**工程建议**

- 需要「接受可转换的参数」时，显式写出模板实参，或者在函数内部统一做一次转换。
- 更安全的做法：**改约束**而不是靠隐式转换。C++20 下写成
  `template <std::convertible_to<std::string> S> ...` 或干脆收 `std::string_view`。
- 泛型函数里对 `const char*` / 数组 / 函数类型的推导都容易出意外，
  觉得不对劲就 `static_assert(std::is_same_v<decltype(x), ...>)` 把类型钉出来。

---

### 3.2 函数模板不能偏特化，只能重载

**错误直觉**

「类模板能偏特化，函数模板应该也能写 `template <typename T> void f<T*>(T*)`。」

**正确模型**

标准规定函数模板**只允许全特化，不允许偏特化**。想要「针对某一类形状给不同实现」，
统一用**重载**表达。重载决议的优先级是：

```
非模板函数（完全匹配）  >  更特化的模板  >  更通用的模板
```

**代码证据**

`01_function_template.cpp` 用同一组调用证明三条路各自命中：

```cpp
template <typename T> const char* kind(T)  { return "value (generic template)"; }
template <typename T> const char* kind(T*) { return "pointer (more specialized template)"; }
const char*           kind(int)            { return "int (non-template wins)"; }
```

实测输出：

```
kind(42)   -> int (non-template wins)
kind(3.14) -> value (generic template)
kind(p)    -> pointer (more specialized template)
```

**注意一个反直觉点**：`kind(42)` 命中的是**非模板**版本，因为非模板函数的
`int` 是精确匹配，而模板还需要推导。**只要非模板版本不差于模板版本，它永远赢。**

**工程建议**

- 「特化效果」优先用**重载 + concepts 约束**表达，语义最清楚、报错最好读。
- 函数模板的全特化（`template <> void f<int>(int)`）能写但坑多：
  它不参与重载决议的「更特化」比较，换编译器/换调用点的行为可能变。**能不用就不用。**

---

### 3.3 依赖名与 `typename`

**错误直觉**

「`T::value_type` 就是 `T` 里的一个类型，直接写就行。」

**正确模型**

编译器在解析模板定义时，**还不知道 `T` 是什么**（`T` 是「依赖」的）。于是：

- 依赖名默认为**值**（变量/静态成员），除非你用 `typename` 声明它是**类型**；
- 依赖模板的成员写成模板时，要用 `template` 关键字消歧（`x.template f<int>()`）；
- **依赖基类**里的名字不会被查找（两阶段查找的第一阶段不查依赖基类），
  必须用 `this->`、`Base<T>::` 或 `using` 引入。

**代码证据（本机实测）**

下面两段代码是本项目用来做「一次性实测」的探针（**故意写错**，放在
`cpp-notes\scratch\` 下，不进正式构建）：

```cpp
// scratch\probe5.cpp —— 依赖基类的名字
template <typename T> struct Base {
    void greet() const {}
    int value = 7;
    using Alias = T;
};
template <typename T> struct Derived : Base<T> {
    void bad()  const { greet(); }              // 直接调用
    void bad2() const { Alias x{}; }            // 直接用依赖基类的嵌套类型
};

// scratch\probe7.cpp —— 依赖模板参数的名字
struct Named { int value = 1; };
template <typename T> void show() { std::cout << T::value << '\n'; }
```

MSVC 14.51 实测报错（`/permissive-`，原文片段）：

```
# probe5.cpp：定义处就报，因为 greet() 的查找在第一阶段完成
.\probe5.cpp(14): error C3861: 'greet': identifier not found
.\probe5.cpp(14): note: 'greet': function declaration must be available as none of the
                   arguments depend on a template parameter
.\probe5.cpp(17): error C2065: 'Alias': undeclared identifier

# probe7.cpp：定义处不报，实例化时才报（这才是典型的「阶段 2」错误）
.\probe7.cpp(8): error C2597: illegal reference to non-static member 'Named::value'
.\probe7.cpp(8): note: the template instantiation context (the oldest one first) is
.\probe7.cpp(12): note: see reference to function template instantiation
                  'void show<Named>(void)' being compiled
```

两处的差别值得玩味：`greet()` 的实参**不依赖模板参数**，所以第一阶段就要查，
查不到直接报；`T::value` 是真依赖名，第一阶段放着不管，到实例化才知道 `Named::value`
是个非静态成员、不能这样用。

正确写法（`06_crtp_policy.cpp` 的同款结构）：

```cpp
void ok() const {
    this->greet();                 // this-> 让表达式变成依赖表达式，推迟到实例化期
    std::cout << this->value;
    typename Base<T>::Alias x{};   // typename 声明「这是类型」
    (void)x;
}
```

**重要更正**：很多人以为「不开 `/permissive-` 就不会报这个错」。
本机实测 **`/permissive-` 和默认宽松模式给出的报错完全一样**（两份输出的
`error C3861` / `error C2065` 逐字相同）。真正决定行为的是**调用是不是依赖的**：

- 调用 `greet()` 的函数**本身是模板成员**时，第一阶段的非依赖查找就找不到 `greet`
  → 无论哪种模式都报 `C3861`；
- 而一旦写了 `this->`，表达式变成依赖的，查找推迟到第二阶段，就能找到基类成员。

所以 `/permissive-` 的作用不是「制造」这个错误，而是**保证编译器严格按标准的
两阶段查找来判定**，从而让你在 MSVC 上就能发现 GCC/Clang 上一定会报的问题。

**工程建议**

- 模板里访问基类成员，**一律写 `this->`**，不要靠 `using Base<T>::member;` 的隐式兜底。
- 见到 `C3861`/`C2065` 指向模板成员内部，第一反应就是「漏了 `this->` 或 `typename`」。
- `typename` 的位置是 `typename T::type`，不是 `T::typename type`。

---

### 3.4 两阶段查找（two-phase lookup）

**错误直觉**

「模板函数体在实例化的时候一次性编译，跟普通函数一样。」

**正确模型**

标准要求编译器分两步：

| 阶段 | 时机 | 查什么 |
|---|---|---|
| 阶段 1（定义点） | 看到模板定义时 | **非依赖名**：普通名字、`typename` 明确声明的、以及 ADL 之外的普通查找 |
| 阶段 2（实例化点） | 每个实例化时 | **依赖名**：与模板参数相关的查找（含 ADL） |

`01_function_template.cpp` 的 `printArray` 依赖 `std::cout`（非依赖，阶段 1 就绑定），
而 `arr[i]` 依赖 `T`（阶段 2 才检查）。

**代码证据**

`scratch\probe7.cpp`（依赖模板参数在实例化时才检查）实测：

```cpp
struct Named { int value = 1; };
template <typename T>
void show() { std::cout << T::value << '\n'; }   // 依赖名，第一阶段不管
// 报错发生在实例化时，note 里能看到完整的实例化链：
//   error C2597: illegal reference to non-static member 'Named::value'
//   note: the template instantiation context (the oldest one first) is
//   note: see reference to function template instantiation 'void show<Named>(void)' being compiled
```

这就是**读模板报错的核心技巧**：先找第一条 `error`，再找
`note: the template instantiation context (the oldest one first) is`
——它给出的就是「谁把谁实例化成什么样」的链条。

**工程建议**

- 把模板**定义**放在使用它的地方之前，能减少「阶段 1 找不到非依赖名」的困惑。
- 依赖名写全 `this->` / `typename`，让第 3 节的坑不再出现。
- 想验证自己理解了：故意写一个错的依赖名，观察报错出现在**定义处**还是**实例化处**。

---

### 3.5 分离编译为什么不能用（含 `extern template` 方案）

**错误直觉**

「模板声明放头文件，实现放 `.cpp`，和普通类一样。」

**正确模型**

模板不是代码，是**代码生成说明书**。编译器必须**看到定义**才能为某个 `T` 生成机器码。
你把定义藏进另一个 `.cpp`，调用点就只看到声明，于是：

- 编译能过（语法没问题）；
- **链接失败**：`error LNK2019: 无法解析的外部符号`。

**代码证据（本机实测）**

只放声明不放定义（`scratch\probe9_fail.cpp`）：

```
probe9_fail.obj : error LNK2019: unresolved external symbol
  "public: void __cdecl Cat<int>::speak(void)const " (?speak@?$Cat@H@@QEBAXXZ)
  referenced in function main
.\pr9_fail.exe : fatal error LNK1120: 1 unresolved externals
```

**正确做法 A：定义放头文件**（99% 的场景就这么办）

```cpp
// myvec.hpp
template <typename T> struct Vec {
    void push(T v) { data_.push_back(v); }      // 定义也在头文件里
    std::size_t size() const { return data_.size(); }
private:
    std::vector<T> data_;
};
```

**正确做法 B：显式实例化 + `extern template`**（只在编译时间/二进制体积成为问题时用）

```cpp
// myvec.hpp —— 声明 + 抑制本 TU 的隐式实例化
template <typename T> struct Vec {
    void push(T v);
    std::size_t size() const;
};
extern template struct Vec<int>;      // 「Vec<int> 别在我这儿生成，别处有」

// myvec.cpp —— 唯一定义点，也是唯一生成代码的地方
#include "myvec.hpp"
template <typename T> void Vec<T>::push(T v) { data_.push_back(v); }
template <typename T> std::size_t Vec<T>::size() const { return data_.size(); }

template struct Vec<int>;                       // 显式实例化定义
template void Vec<int>::push(int);              // 保险起见把成员也点名
template std::size_t Vec<int>::size() const;
```

**这个坑的关键细节（本机实测，很容易被忽略）**

`template struct Vec<int>;` 只会实例化**该 TU 内可见的成员函数定义**。
两种情况的行为完全不同：

| 情况 | `template struct Vec<int>;` 的效果 | 本机实测证据 |
|---|---|---|
| 成员定义在**同一个 TU**（内联在类里，或定义在下面） | 成员符号**正常生成** | `dumpbin /symbols` 里看到 `?push@?$Vec@H@@QEAAXH@Z` 是 `External`（已定义） |
| 成员定义在**另一个 TU**（声明在头、定义在 `.cpp`） | 成员符号是 `UNDEF`，并报 **C4661** | `warning C4661: 'void Vec<int>::push(T)': no suitable definition provided for explicit template instantiation request` |

第二行是本项目里**必须知道**的一条：`build.ps1` 用了 `/W4 /WX`，于是这个警告
会直接变成错误：

```
error C2220: the following warning is treated as an error
warning C4661: 'void Vec<int>::push(T)': no suitable definition provided
               for explicit template instantiation request
```

也就是说，如果你在头文件里写了 `template struct Vec<int>;` 却没让编译器看到成员定义，
**编译就会挂**，而不是天真地以为「链接期再说」。正确做法是二选一：

- 让成员定义对该 TU 可见（把定义移到这个 `.cpp` 里）；或者
- 逐个写成员函数的显式实例化（下面做法 B 里的那两行）。

`scratch\probe9_main.cpp` + `scratch\probe9_impl.cpp` 实测：
加上 `extern template` 后链接通过、运行输出 `Dog speak: 7`。

**工程建议**

| 场景 | 做法 |
|---|---|
| 普通业务代码 | 定义放头文件，不要折腾 `extern template` |
| 同一套实例在几百个 TU 里出现、编译/链接变慢 | 头文件里声明 + `extern template`，一个 `.cpp` 里显式实例化 |
| 内部实现细节不想暴露 | 抽非模板基类（类型擦除），只把薄薄的模板壳放头文件 |
| 库要跨 DLL 边界 | 不要把模板导出去；导出具体实例（`__declspec(dllexport)` 一个非模板包装函数） |

---

### 3.6 引用折叠与万能引用

**错误直觉**

「`T&&` 就是右值引用，只能接右值。」

**正确模型**

**引用折叠**只有 4 条规则：

| 组合 | 结果 |
|---|---|
| `& + &` | `&` |
| `& + &&` | `&` |
| `&& + &` | `&` |
| `&& + &&` | `&&` |

一句话记忆：**只要有一个 `&`，结果就是 `&`**。

**万能引用**（forwarding reference）需要**同时满足**两个条件：

1. 形如 `T&&`，`T` 是**这个函数自己的模板参数**；
2. `T` **真的由实参推导**得到。

破坏条件的两种常见写法：

```cpp
template <typename T> void f(const T&&);        // 有 const -> 只是右值引用
template <typename T> void g(std::vector<T>&&); // T&& 不是「裸的模板参数 T」-> 右值引用
```

**代码证据**

`04_variadic_forwarding.cpp` 实测输出：

```
deduce(i):      T = U&  ->  T&& 折叠为 U&   (传入左值)
deduce(0):      T = U   ->  T&& 就是 U&&     (传入右值)
deduce(move):   T = U   ->  T&& 就是 U&&     (传入右值)
deduce(const):   T = U&  ->  T&& 折叠为 U&   (传入左值)
```

并且用 `static_assert` 把推导结果钉死（不依赖运行期打印）：

```cpp
static_assert(deducedAsLvalueRef(i));              // 左值  -> T = int&
static_assert(!deducedAsLvalueRef(0));             // 右值  -> T = int
static_assert(!deducedAsLvalueRef(std::move(i)));  // 将亡值 -> T = int
```

**`std::forward` 到底做了什么？**

它是「**有条件地 move**」，等价于：

```cpp
template <typename T>
constexpr T&& forward(std::remove_reference_t<T>& x) noexcept {
    return static_cast<T&&>(x);       // T 是左值引用 -> 返回左值；T 是非引用 -> 返回右值
}
```

运行期**零代码**。这就是为什么「漏掉 `std::forward`」的代价不是慢，而是**错**：

```
forward(lvalue)  copies=1 moves=0
forward(rvalue)  copies=0 moves=1
no forward       copies=1 moves=0   <- 丢掉了值类别，传右值也走拷贝
```

**工程建议**

- 只有**真的要转发**（把参数原样交给下一层）时才写 `std::forward`；
  函数体内把 `T&&` 当普通引用用就行（有名形参一律是左值）。
- 不要给万能引用加 `const`，也不要在里面省略 `std::forward`——这两件事都会静默降级成拷贝。
- `std::move` 用在「确定要放弃这个对象」的地方；`std::forward` 用在模板转发处。

---

### 3.7 SFINAE vs concepts

**错误直觉**

「concept 就是写起来短一点的 `enable_if`，报错也更短。」

**正确模型**

concept 与 SFINAE 有**三个本质区别**：

1. **表达能力**：concept 有「复合要求」（`{ e } -> Concept`）、类型要求、嵌套要求、
   逻辑组合（`&&`/`||`/`!`）、以及「约束可以互相引用」。`enable_if` 只能塞一个 `bool`。
2. **重载决议**：concept 之间可以比较「**谁更强**」（部分有序），
   于是可以写「`integral` 版」「`floating_point` 版」「兜底版」三个重载，
   编译器自动挑最合适的。`enable_if` 做不到这点，只能靠标签分派或手写互斥条件。
3. **可诊断性**：concept 的检查结果**可以被编译器直接报出来**（「哪个 concept
   的哪条要求不满足」），而 `enable_if` 失败只会变成「模板参数推导失败」。

**代码证据（本机实测，同一需求的两种写法）**

`scratch\probe_sfinae_err.cpp`（C++17 风格）与 `scratch\probe_concept_err.cpp`
（C++20 concept），都对 `std::vector<int>` 调用一个「要求支持 `+`」的函数：

```
# SFINAE 版
error C2672: 'bySfinae': no matching overloaded function found
note: could be 'void bySfinae(const T &,const T &)'
note: 'void bySfinae(const T &,const T &)': could not deduce template argument
      for '<unnamed-symbol>'                 <-- 关键线索是「匿名符号」，看不出是什么约束

# concept 版
error C2672: 'byConcept': no matching overloaded function found
note: could be 'void byConcept(const T &,const T &)'
note: the associated constraints are not satisfied          <-- 直接说「约束不满足」
note: the concept 'Addable<std::vector<int,...>>' evaluated to false   <-- 点名 concept
note: binary '+': 'std::vector<...>' does not define this operator ...  <-- 点名缺口
```

**重要更正（诚实结论）**：**报错的「行数」几乎一样**（实测 SFINAE 版 23 行，
concept 版 20 行），后面那一长串 `could be 'std::operator +(reverse_iterator...)'`
是**两种写法都有的**（因为编译器都会去枚举所有 `operator+` 候选）。
concept 的优势不在「更短」，而在于：

- 报错里出现**你自己起的 concept 名字**（`Addable<...> evaluated to false`），
  而不是 `<unnamed-symbol>`；
- 报错里出现 `the associated constraints are not satisfied` 这个**明确的原因**；
- 多人协作时，读者从报错就能知道**设计意图**，而不是去人肉解析模板参数列表。

一句话：concept 把「约束」从**实现细节**提升成了**接口的一部分**。

**代码证据（约束参与重载决议）**

`05_concepts.cpp` 的三个 `describe` 重载实测：

```
describe(1)    = integral
describe(1.5)  = floating point
describe("x") = something else
```

约束更强且满足的候选优先，不需要任何 `enable_if`。

**工程建议**

- **新代码一律用 concepts**。C++17 及以前的代码在维护时再逐步迁移。
- 用 concept 表达**语义契约**，不要只表达「语法合法」：
  `concept Serializable` 比 `concept HasToJson` 有价值得多。
- 给 concept 起**业务名字**（`Addable` / `Printable` / `Container`），
  这直接决定了报错的可读性。
- `requires` 表达式本身是 `bool` 常量表达式，可以直接
  `static_assert(SelfAddable<int>)`，也可以 `if constexpr (requires {...})`
  做局部探测——这是 `enable_if` 给不了的。

---

## 4. VS / MSVC 实战踩坑清单

原素材 `模板\template_demo\README.md` 的 8 条踩坑清单质量很高。这里**逐条核对**，
错的改正、含糊的写细，并补了本机新发现的 4 条。

### 4.1 逐条核对

| # | 原说法 | 核对结论 |
|---|---|---|
| 1 | `windows.h` 的 `min`/`max` 宏会把模板搞崩，要 `#define NOMINMAX` | **准确**。宏是文本替换，`std::max(a,b)` 会被展开成 `((a)>(b)?(a):(b))`，`max<T>` 直接语法错误。同类污染宏还有 `ERROR`、`GetObject`、`CreateFile`、`small`、`near`。 |
| 2 | VS 默认语言标准是 C++14 | **准确**。新建空项目默认不是 C++20，concepts/折叠表达式/CTAD 全都会报错，第一件事就是改 `/std:c++20`。 |
| 3 | `__cplusplus` 永远是 `199711L`，要加 `/Zc:__cplusplus` | **准确，本机实测**：不加该选项时 `__cplusplus = 199711`，加了之后 `202002`；而 `_MSVC_LANG` **两种情况下都是 202002**。所以判断标准版本优先用 `_MSVC_LANG`。 |
| 4 | 模板实现不能只放 `.cpp`，要么放头文件，要么显式实例化 + `extern template` | **方向正确，细节不够**。见 3.5：`template struct Vec<int>;` 只实例化**本 TU 内可见的成员定义**。定义不可见时 MSVC 报 **C4661**（在本项目的 `/W4 /WX` 下直接变成 error C2220），并且成员符号是 `UNDEF`。修法：让定义可见，或逐个补 `template void Vec<int>::push(int);`。另外**「类的显式实例化会连带实例化成员」这个常见说法容易误导人**——实测同 TU 内可见时确实如此，跨 TU 时则不成立。 |
| 5 | 中文源码报 C4819，要加 `/utf-8` | **准确**。另外 VS 里要用「另存为 → 带编码保存 → UTF-8」。本项目统一 `/utf-8`，中文注释与输出都正常。 |
| 6 | 模板报错刷屏几万行，要从第一条 `error` 看起，再找 `note: 请参阅对 ... 实例化` | **准确且重要**。本机 MSVC 14.51 的英文原句是 `note: the template instantiation context (the oldest one first) is`，下面会列出完整实例化链。配合 `/diagnostics:caret` 能定位到列。 |
| 7 | 实例化太多导致 `C1128`，加 `/bigobj` | **准确**。`C1128: number of sections exceeded object file format limit` 的默认上限是 **65279 个节**；`/bigobj` 把上限提高到约 40 亿，节地址从 32 位扩到 64 位。模板实例化爆炸、大量 inline/匿名命名空间都会顶上去。 |
| 8 | **CRTP 会破坏聚合初始化**，因为 C++17 起基类也算聚合的一个元素，`Point{1,2}` 会报 `C2078` | **结论对、解释错、修法错**。见 4.2 的完整实测。 |

### 4.2 重点更正：CRTP、空基类与聚合初始化

**原素材的说法**：

> 「C++17 起基类也算聚合的一个元素，所以 `Point{1,2}` 会报 `error C2078: too many
> initializers`（本目录 06 里就是这么改的：加个构造函数）。」

**问题在哪**

1. **只讲了「基类算元素」，没讲「空基类也算」**。这是最容易踩的一点：
   `Comparable<Point>` 是**空类**，很多人以为「空的东西不用给初始化器」——错。
   实测 `sizeof(Comparable<Point>) == 1`，但它作为聚合元素照样占一个位置。
2. **暗示「只能加构造函数」**。其实还有更轻的写法：显式给空基类一个 `{}` 初始化器，
   也就是 `Point{{}, 1, 2}`——**它是能编译的**，原素材把它评成「很难看」而没提「可用」。
3. **错误号说对了，但错误原因说反了**。`C2078: too many initializers` 的字面意思是
   「初始化器太多」，而真实情况是「**元素比初始化器多**」——`Point{1,2}` 只有 2 个
   初始化器，却要初始化 3 个元素（空基类 + `x` + `y`）。MSVC 的措辞有误导性。

**本机实测（MSVC 14.51 / `/std:c++20 /permissive-`）**

```cpp
struct EmptyBase {};
struct WithEmptyBase : EmptyBase { int x, y; };

WithEmptyBase ok{{}, 1, 2};     // 通过
// WithEmptyBase bad{1, 2};     // error C2078: too many initializers
```

实测输出（`06_crtp_policy.cpp`）：

```
sizeof(EmptyBase)      = 1
sizeof(WithEmptyBase)  = 8   <- 空基类被优化掉，等于 2 个 int
WithEmptyBase{{},1,2}  = (1,2)
is_aggregate<WithEmptyBase> = true, is_aggregate<Point> = false
```

`/d1reportAllClassLayout` 的布局 dump 也印证了「基类占 0 字节、但占一个元素位置」：

```
class Point	size(8):
	+---
 0	| +--- (base class Comparable<struct Point>)
	| +---
 0	| x
 4	| y
	+---
```

**把规则写准确**

> C++17 起，「基类」也是聚合的一个元素，**空基类同样算**（因为它属于
> aggregate 的元素定义：每个非静态数据成员、每个基类）。
> 所以「有 1 个空基类 + 2 个数据成员的派生类」，聚合初始化需要 **3 个初始化器**。
> 少的写 `{{}, a, b}`；多的写 `{Base{...}, a, b, 多}` 才报 `C2078`（MSVC 措辞反直觉）。
>
> 反过来，**只要写了用户提供的构造函数，类型就不是聚合了**，
> `is_aggregate_v<Point> == false`，`Point{1,2}` 走构造函数，一切正常。

**三种写法的取舍**

| 写法 | 是否聚合 | 适用场景 |
|---|---|---|
| `Point{{}, 1, 2}` | 是 | 想保留聚合能力（结构化绑定、`std::array` 风格的批量初始化） |
| 写用户构造函数 | 否 | 有不变式要维护、要重载、要 `explicit` |
| 不继承 CRTP 基类，改成自由函数/`operator<=>` 默认实现 | 是 | C++20 起最省事：`auto operator<=>(const Point&) const = default;` |

> **C++20 补充**：如果只是想要全套比较运算符，**根本不需要 CRTP**。
> 写一个 `auto operator<=>(const Point&) const = default;` 就够了，
> 编译器生成 `== != < <= > >=`，而且 `Point` 仍然是聚合。

### 4.3 本机新增的 4 条坑

| # | 坑 | 现象与本机实测 |
|---|---|---|
| 9 | **偏特化里不能出现「依赖的非类型模板参数」** | `template <typename T, T N, T... I> struct S` 配 `struct S<T, 0, I...>` 报 `error C2754: a partial specialization cannot have a dependent non-type template parameter`。绕法：把数字**包成类型**（`IntConst<N>`），偏特化只匹配类型形状。见 `07_template_metaprogramming.cpp` 第 1 节。 |
| 10 | **MSVC 的 `std::to_string` 不是 `constexpr`** | 在 `constexpr` 函数里调用它，`static_assert` 会报 `error C2131: expression did not evaluate to a constant`，note 说 `failure was caused by call of undefined function or one not declared 'constexpr'`。要在编译期转字符串得自己写（`07` 里的 `intToString`）。 |
| 11 | **模板函数忘了 `constexpr` 就不能进 `static_assert`** | `template <typename... Ts> auto sumAll(const Ts&... xs)` 没有 `constexpr`，`static_assert(sumAll(1,2,3,4)==10)` 直接 `C2131`。折叠表达式本身没问题，**是函数缺 `constexpr`**。 |
| 12 | **`decltype` 里不能用泛型 lambda / 未实例化的模板调用** | 想要「证明这次调用推导成左值引用」，得写成普通函数模板（`constexpr bool deducedAsLvalueRef(T&&)`），泛型 lambda 不能出现在 `decltype` 这种不求值语境里。 |

---

## 5. 编译期诊断技巧

### 5.1 怎么读模板报错

**三步法**

1. **只看第一条 `error`。** 后面 90% 是它的连锁反应。
2. **找实例化链。** MSVC 的固定句式：

   ```
   note: the template instantiation context (the oldest one first) is
   note: see reference to function template instantiation 'void show<Named>(void)' being compiled
   ```

   从「最老的」往下读，就是「谁把谁实例化成了什么」。真正的错误点通常在链条最内侧。

3. **定位到列。** 加 `/diagnostics:caret`（本项目 build.ps1 已默认开启），
   编译器会用 `^` 指出具体是哪一个 token 出问题：

   ```
   .\probe1.cpp(44,14): error C2078: too many initializers
       PointA a{1, 2};
                ^
   ```

**本机实测的报错体量**（`/std:c++20 /permissive- /c`，统计 `error`/`note` 行数）：

| 错误场景 | error | note | 说明 |
|---|---|---|---|
| SFINAE 约束不满足 | 1 | 17 | 关键是 `could not deduce template argument for '<unnamed-symbol>'` |
| concept 约束不满足 | 1 | 18 | 关键是 `the associated constraints are not satisfied` + concept 名字 |
| CRTP 空基类聚合写错 | 1 | 0 | `C2078`，只有 2 行，很好读 |
| 推导不做转换（`maxValue(3, 5.0)`） | 1 | 5 | `C2782` 模板参数不明确 + `C2784` 无法推导 |
| 依赖基类名漏写 `this->`（probe5） | 2 | 2 | `C3861` + `C2065`，**在定义处**报 |

结论：**报错长不长主要取决于「有没有一长串候选重载」（比如 `operator+`）**，
而不是 SFINAE 还是 concept。别指望 concept 让报错变短，它的价值是**让报错说人话**。

### 5.2 用 `/Fa` 看汇编，验证「模板真的被内联了」

**关键：不要搜符号，要搜 `call`。**

模板实例化出来的函数体**一定会生成**（COMDAT 形式，链接器再决定丢不丢），
所以 `grep maxValue` 永远能搜到，会误判。真正要看的是**调用点有没有 `call`**。

```powershell
# 1) 生成汇编
cl /nologo /std:c++20 /EHsc /utf-8 /permissive- /O2 /Faout\01.asm `
   /Foout\ /Feout\01.exe cpp-notes\04-templates\01_function_template.cpp

# 2) 统计（期望 0 条）
Select-String -Path out\01.asm -Pattern 'call.*maxValue'    # 0
Select-String -Path out\01.asm -Pattern 'call.*Factorial'   # 0
```

**本机实测（MSVC 14.51 / `/O2`）**

```
总行数=24081
call.*maxValue = 0
call.*Factorial = 0
立即数 mov edx, 120 出现次数 = 1
```

汇编里的直接证据：

- 符号**存在**（COMDAT，等链接器丢）：`PUBLIC ??$maxValue@H@@YAHHH@Z ; maxValue<int>`
- `main` 里 `maxValue(3,5)` 被折成一条 `mov edx, 5`，**没有任何 `call` 指向它**；
- `Factorial<5>::value` 被折成 `mov edx, 120`（0x78），连模板函数都没生成，
  因为它是 `static constexpr` 成员，纯编译期常量；
- `maxValue<std::string>` 生成出来的函数体里能看到它调用了 `std::basic_string` 的
  比较和拷贝——说明**「零开销」的前提是「类型本身没有运行期成本」**，
  模板只是保证「不额外加成本」。

### 5.3 用 `/d1reportAllClassLayout` 看对象布局

```powershell
cl /nologo /std:c++20 /EHsc /utf-8 /permissive- /d1reportAllClassLayout /c `
   06_crtp_policy.cpp > out\layout.txt
Select-String -Path out\layout.txt -Pattern 'class Point' -Context 0,8
```

**本机实测输出**

```
class Point	size(8):
	+---
 0	| +--- (base class Comparable<struct Point>)
	| +---
 0	| x
 4	| y
	+---
```

读法：`size(8)` 正好是 2 个 `int`；CRTP 基类 `Comparable<Point>` 占据**0 字节**
（空基类优化），**整块布局里没有 `vfptr`**。这就是「编译期多态零虚函数开销」的直接证据。

对照：如果换成带虚函数的运行时基类，这里会出现 `vfptr`，`size` 会变成 16（8 字节 vptr + 8 字节数据）。
本项目 `08_practical_templates.cpp` 用 `sizeof` 把这个差异写成了 `static_assert`：

```
sizeof(LessPolicy)     = 1        <- 空策略，无状态无 vptr
sizeof(VirtualLess)    = 8        <- 一个 vptr（64 位）
sizeof(std::function<bool(int,int)>) = 64
```

### 5.4 什么时候需要 `/bigobj`

`C1128: number of sections exceeded object file format limit` 的**默认节数上限是 65279**。
以下情况容易顶到：

- 单个 `.cpp` 里实例化了成千上万个模板（典型：把重模板的头文件塞进了巨大的 TU）；
- 大量 `inline` 函数、匿名命名空间、静态初始化；
- `/Zi`（调试信息）会额外增加节数。

对策（按推荐顺序）：

1. **加 `/bigobj`**——把上限提高到 40 亿，本项目 build.ps1 已默认开启。代价：`.obj` 略大，
   **链接器不支持旧格式时可能不兼容老工具**，但对现代 MSVC 无痛。
2. **减少实例化**：`extern template`、类型擦除、把模板实现拆到更小的 TU。
3. **不要**用 `/Ob0` 或降 `/Zi` 当常规手段——那是在牺牲可调试性换编译通过。

---

## 6. 工程决策清单

### 6.1 五种「多态/复用」手段对比

| 手段 | 绑定时机 | 运行期开销 | 二进制体积 | 能否内联 | 异构容器 | 报错可读性 | 适用场景 |
|---|---|---|---|---|---|---|---|
| **函数重载** | 编译期 | 0 | 极小 | 是 | 不适用 | 最好 | 类型集合封闭且少（<3~5 个） |
| **模板 / 策略模板** | 编译期 | 0 | 每个实例一份，可能膨胀 | 是 | 不能（不同类型） | 差（concept 可改善） | 热点路径、泛型算法、无状态策略 |
| **CRTP** | 编译期 | 0 | 每个派生类一份基类代码 | 是 | 不能 | 差 | 要「给一组类型注入相同接口」时 |
| **虚函数** | 运行期 | 一次间接调用 + vptr | 一份代码 | 否 | **能** | 好 | 运行期需要切换、要进同一容器 |
| **`std::function`** | 运行期 | 间接调用 + **可能堆分配** | 一份代码 | 否 | 能 | 好 | 存回调、`std::function` 是数据成员时 |
| **宏** | 预处理 | 0 | 取决于展开 | 是（就是文本） | 不适用 | 最差（报错指向展开处） | 日志/断言等极窄场景，**不用于泛型** |

### 6.2 本机实测的性能与体积差异

环境：MSVC 14.51，`08_practical_templates.cpp`，48 元素插入排序 × 200000 轮。

| 实现 | Debug 实测 | Release（`/O2 /DNDEBUG`）实测 | 结论 |
|---|---|---|---|
| 策略模板 | 845 ms | **52.3 ms** | 比较函数被内联，最快 |
| 虚函数 | 970 ms | 109.5 ms | 每次比较一次间接调用，约 **2.1×** |
| `std::function` | 1981 ms | 140.7 ms | 类型擦除 + 间接调用，约 **2.7×** |

`sizeof` 实测：`LessPolicy` 1 字节、`VirtualLess` 8 字节（一个 vptr）、
`std::function<bool(int,int)>` 64 字节。

> **注意**：`build.ps1 -Config Release` 会加 `/O2 /DNDEBUG`，但 CRT 仍是 `/MDd`
> （脚本里写死了 `/MDd`），所以 Release 数据只适合做**相对**比较，不要当生产性能基准。

### 6.3 「什么时候不该用模板」——review checklist

按顺序自问，**任何一条命中就停下来**：

1. **类型集合是不是封闭且很少？**
   只有 `int` / `double` / `std::string` 三种？→ 写三个重载。
   报错信息直接指向调用点，新人也能改。
2. **约束能不能用 concept 表达？**
   不能（比如「必须先 `open` 再 `read`」「依赖运行期状态机」）→ 用虚函数/接口类，
   模板解决不了语义约束。
3. **需要运行期切换实现吗？**
   配置项决定用哪种日志后端、插件在运行期加载 → 虚函数或 `std::function`。
   策略模板做不到「运行期换」。
4. **泛型体大不大？实例化组合多不多？**
   `template <typename A, typename B, typename C> class Pipeline` 被实例化几十种组合、
   每个实例几千行 → 抽非模板基类 + 薄模板壳（类型擦除），把 90% 代码变成一份。
5. **要不要跨 DLL / 编译器 / 语言边界导出？**
   模板没有稳定 ABI。导出**具体实例的包装函数**，或者只导出非模板接口。
6. **编译时间还够用吗？**
   模板会让编译时间非线性增长。项目里跑一次
   `cl /Bt+ /nologo ...` 看每个阶段耗时，或者用 `/d1reportTime` 找最贵的头文件。
7. **新人能不能在 1 分钟内读懂报错并改对？**
   不能 → 加 `static_assert` 提供友好提示（见 7.4），或者退回重载。

**反过来，「该用模板」的典型信号**：

- 逻辑与类型无关，且要对**用户自定义类型**也生效（`std::sort`、`std::hash`）；
- 需要**零开销**的证据能在汇编里找到（热点循环、嵌入式、游戏引擎）；
- 需要**编译期**就拒绝错误组合（强类型 ID、单位量纲、策略选择）；
- 需要**编译期计算**（`std::array` 大小、`std::ratio`、状态机的转移表）。

### 6.4 一个具体的选择流程

```
要复用一段逻辑？
├─ 只对 1~2 个具体类型 -> 写重载（或一个函数 + 重载）
├─ 对「所有支持 X 的类型」-> template + concept
│    ├─ 需要运行期切换？ -> 虚函数 或 std::function
│    ├─ 性能/体积敏感？ -> 策略模板 / CRTP
│    └─ 实例化组合爆炸？ -> 非模板核心 + 薄模板壳（类型擦除）
└─ 对「类型本身」做文章（推导、萃取、约束）-> 模板 + type traits
```

---

## 7. 模板代码规范建议

### 7.1 约束：concepts 优先

```cpp
// 好：约束写在名字旁边，报错点名 concept
template <Printable T>
void log(const T& v);

// 可接受（C++17 兼容）：至少把约束抽成有名字的别名模板
template <typename T, std::enable_if_t<is_printable_v<T>, int> = 0>
void log(const T& v);

// 差：约束条件散落在函数体里，靠运行期 assert 才报错
template <typename T>
void log(const T& v);   // 里面用 std::cout << v
```

- 约束条件**用 concept 命名**，不要写裸 `requires(...)` 大表达式。
- concept 名用**形容词语义**（`Addable`、`Serializable`、`NothrowMovable`），
  不要用 `HasFooBar` 这种「检测项名」——接口意图比实现细节重要。
- 组合约束时用 `&&`，并给组合结果起名字：

  ```cpp
  template <typename T>
  concept Storable = Serializable<T> && EqualityComparable<T> && Destructible<T>;
  ```

### 7.2 命名

| 对象 | 约定 | 例 |
|---|---|---|
| 类型模板参数 | 大写单词，语义化 | `T`（通用）、`Allocator`、`HashPolicy`、`Tag` |
| 非类型模板参数 | 短名或语义名 | `N`、`M`、`Bits` |
| concept | 形容词/能力名 | `Addable`、`PrintableContainer` |
| 策略类 | `XxxPolicy` | `FastHashPolicy`、`LessPolicy` |
| trait | `is_xxx_v` / `xxx_t` 对齐标准库 | `is_printable_v<T>`、`printable_t<T>` |
| 变量模板 | 小写下划线，和常量一样 | `pi<T>`、`tagHash<S>` |

**参数包一律用 `Ts...` / `Args...`**，不要用 `T...`；`std::forward` 里写 `Args` 而不是 `T`。

### 7.3 头文件组织

```cpp
// mylib/vec.hpp
#pragma once                    // 或 include guard，二选一，别混用
#include <cstddef>              // 头文件里只 include「自己用到的」
#include <vector>

namespace mylib {

// 1) 公开 concept / trait
template <typename T> concept Numeric = std::is_arithmetic_v<T>;

// 2) 主模板（+ 默认实参）
template <typename T, typename Alloc = std::allocator<T>>
class Vec { /* 定义直接放这里 */ };

// 3) 别名模板放最后，方便阅读
template <typename T> using SmallVec = Vec<T>;

}  // namespace mylib
```

**规则**

- 模板定义**必须**在头文件（或显式实例化，见 3.5）。
- 头文件里**不要** `using namespace`；函数里也尽量不写，全写 `std::`。
- 模板相关的头文件尽量**自洽**：把 `#include` 写全，别靠「调用方已经 include 了」。
- 想缩短编译时间：把不依赖模板参数的实现下沉到一个非模板的 `.cpp`，
  头文件里只留模板壳（经典的类型擦除手法）。

### 7.4 用 `static_assert` 提供友好报错

这是模板代码**最被低估**的工程技巧：把「晦涩的实例化失败」变成「一句话」。
本项目通篇在用，示例：

```cpp
// 类型不符合预期时，先给人一句话，而不是让人去解析 200 行实例化链
template <typename T>
void serialize(const T& v) {
    static_assert(!std::is_pointer_v<T>,
                  "serialize() 不支持裸指针：请传对象或智能指针，避免序列化地址");
    static_assert(std::is_trivially_copyable_v<T>,
                  "serialize() 要求 T 可平凡拷贝：含虚函数/自定义析构的类型请提供 to_bytes()");
    ...
}
```

配合 C++20 的 `consteval` 或 `if constexpr`，还能做到「只有在真正走那条分支时才报错」：

```cpp
template <typename T>
void handle(T v) {
    if constexpr (std::is_integral_v<T>) {
        // 整数分支
    } else {
        static_assert(sizeof(T) == 0, "handle() 目前只支持整型");
    }
}
```

### 7.5 控制实例化数量

| 手法 | 效果 | 代价 |
|---|---|---|
| `extern template struct X<int>;` + 一处显式实例化 | 同实例在多个 TU 只生成一份 | 需要手动维护实例清单 |
| 类型擦除（非模板基类 + 薄模板壳） | 实例化代码量骤降 | 一次间接调用（可能可接受） |
| 把 `T` 的差异提到**数据**而不是**类型** | 从根源减少实例化 | 设计上要想清楚 |
| 只在头文件里放声明，实现放 `.cpp`（针对具体实例） | `.obj` 更小、编译更快 | 不支持任意 `T`，需列出实例 |
| 编译期 `if constexpr` 合并分支 | 减少重复代码路径 | 逻辑可能变复杂 |

**量化手段**：用 `/d1reportTime`（MSVC 内部选项）或 `cl /Bt+` 找编译时间热点；
用 `dumpbin /symbols` 统计 COMDAT 符号数量，观察模板实例化的规模。

---

## 8. 自测题（8 道，含答案）

### Q1. `maxValue(3, 5.0)` 编译失败，`maxValue<double>(3, 5)` 却可以。为什么？

**答案**：模板参数推导**不做隐式转换**。`maxValue(3, 5.0)` 中 `T` 需要同时满足
`T = int`（从 3）和 `T = double`（从 5.0），推导冲突 → `C2782`。
而 `maxValue<double>(3, 5)` 已经显式给定了 `T = double`，形参类型随之确定为 `double`，
调用阶段走普通函数的隐式转换规则（`int → double`）→ 合法。

**原理**：模板实参的确定（推导或显式指定）与函数调用的实参转换是**两个阶段**。
显式指定把「确定模板参数」这一步短路了，于是只剩普通的调用阶段。

---

### Q2. `template <typename T> void f(T*)` 和 `template <typename T> void f(T)` 同时存在，`f(&x)` 选哪个？能不能写 `f<int*>` 的偏特化？

**答案**：选 `f(T*)`，因为它**更特化**（部分有序：`T*` 能匹配 `T` 能匹配的所有情况，
反之不成立）。函数模板**不能偏特化**，但可以像这样重载来达到同样效果。

`f<int*>` 的偏特化写法 `template <typename T> void f<int*>(int*)` 是**非法的**；
如果要为某个具体类型换实现，只能写**全特化** `template <> void f<int*>(int*)`
（不推荐，坑多）或者直接写非模板重载 `void f(int*)`。

---

### Q3. `sizeof(Point) == 8`、`std::is_polymorphic_v<Point> == false`，但 `Point` 继承了 `Comparable<Point>`。这是怎么做到的？代价是什么？

**答案**：CRTP 把派生类类型作为**模板参数**传给基类，所有调用在编译期就能解析，
不需要虚表。基类是**空类**（无数据成员、无虚函数），空基类优化让它占 0 字节，
所以 `sizeof(Point)` 就是两个 `int`。

代价：
1. **代码膨胀**——每个派生类都实例化一份 `Comparable<Derived>` 的代码；
2. **不能异构存放**——`Point` 与 `Other` 是不同类型，塞不进同一个 `vector`；
3. **编译期耦合**——基类定义必须在派生类可见处；`Derived` 不完整时不能访问其成员。

---

### Q4. `Point{{}, 1, 2}` 里的 `{}` 是干什么的？不写会怎样？

**答案**：`{}` 是给**空基类 `Comparable<Point>`** 的初始化器。C++17 起基类也算聚合的
一个元素，**空基类也算**，所以 `Point` 的聚合元素是「空基类 + `x` + `y`」共 3 个。
`Point{1,2}` 只有 2 个初始化器去初始化 3 个元素 → MSVC 报
`error C2078: too many initializers`（措辞反直觉，实际是「给的初始化器不够位置」）。

另外两条路：写用户构造函数（`Point` 就不再是聚合，`Point{1,2}` 走构造函数）；
或者干脆去掉 CRTP，用 C++20 的 `auto operator<=>(const Point&) const = default;`。

---

### Q5. 为什么 `std::forward<T>(x)` 不能省略？省略了会怎样？

**答案**：省略之后 `x` 在函数体里是一个**具名形参**，而具名形参永远是**左值**，
于是「传进来的右值」退化成左值，下一层只能走拷贝构造。

本机实测（`04_variadic_forwarding.cpp`）：

```
forward(lvalue)  copies=1 moves=0
forward(rvalue)  copies=0 moves=1
no forward       copies=1 moves=0   <- 传右值也变成拷贝
```

原理：`std::forward<T>` 只在 `T` 是非引用类型时把实参转成右值；
`T` 是左值引用时原样返回。它本身不产生任何运行期代码。

---

### Q6. 类模板声明放头文件、成员函数定义放 `.cpp`，为什么链接会失败？`template struct Vec<int>;` 能不能救？

**答案**：编译模板实例化时，编译器必须**看到成员函数的定义**才能生成代码。
调用点只看到声明 → 生成了对外部符号的引用 → 链接期 `LNK2019`。

`template struct Vec<int>;`（类的显式实例化定义）**只在成员函数的定义对该 TU 可见时**
才会实例化那些成员。如果成员定义在另外的 `.cpp` 里，这句**不够**，
必须补上每个成员的显式实例化：

```cpp
template void Vec<int>::push(int);
template std::size_t Vec<int>::size() const;
```

本机实测：在 `extern_template_impl.cpp` 里**只**写 `template struct Dog<int>;`
而不写成员函数的显式实例化，且成员定义放在类外时，调用点仍然缺符号；
把 `template void Dog<int>::speak() const;` 补上就链接通过。
**结论：类的显式实例化不等于「成员函数的显式实例化」。**

**推荐**：定义直接放头文件；只有编译时间成为瓶颈时才用
`extern template`（声明）+ 显式实例化（定义）。

---

### Q7. SFINAE 和 concept 在「报错友好」上到底差在哪？给一个具体差别。

**答案**：**不是行数差**（本机实测 SFINAE 23 行 / concept 20 行，几乎一样），
差在**能不能点名**：

```
# SFINAE
note: 'void bySfinae(const T &,const T &)': could not deduce template argument
      for '<unnamed-symbol>'                       <-- 匿名符号，读者不知道是什么

# concept
note: the associated constraints are not satisfied
note: the concept 'Addable<std::vector<int,...>>' evaluated to false   <-- 点名 concept
```

concept 让「约束」成为接口的一部分，报错里出现的是**你自己起的名字**，
维护者能从报错直接读到设计意图。此外 concept 还支持：
约束之间的**部分有序比较**（自动选更强约束的重载）、
`&&`/`||`/`!` 组合、以及作为 `bool` 常量直接喂给 `static_assert`/`if constexpr`。

---

### Q8. 团队里有人提议「把所有函数都改成模板，最大化复用」。给出至少 4 条反对理由，并说明什么情况下模板才是正确答案。

**答案**：反对理由（每条都对应真实成本）：

1. **编译时间**：模板实例化让编译时间非线性增长，大型项目里 CI 时间会明显变长。
2. **报错可读性**：约束不足的模板把「一个好错误」变成「一面实例化墙」，
   新人改不动的代码就是技术债。
3. **代码膨胀**：每个实例化一份机器码，嵌入式/移动端上会直接影响包体。
   实测中 `std::function` 对象 64 字节、虚函数对象 8 字节 vptr，
   模板版本虽然 1 字节但要为每个类型生成一份代码。
4. **调试困难**：模板实例化后的函数名经过名字修饰（如
   `??$maxValue@V?$basic_string@...`），调试器断点、调用栈、性能分析都更难读。
5. **ABI 影响**：模板没有稳定 ABI，跨 DLL/编译器/版本边界的模板接口极易碎。
6. **接口语义模糊**：模板把「必须满足什么」写在了实现里而不是签名里，
   使用者不知道能传什么（幸好 concept 能补救这一点）。

**模板真正的正确答案**：

- 逻辑**与类型无关**且要对用户自定义类型生效；
- 需要**编译期**拒绝非法组合（强类型 ID、单位量纲、策略选择）；
- 需要**零运行期开销**，且能在汇编里验证（热点循环、嵌入式）；
- 需要**编译期计算**（数组大小、哈希、状态转移表）。

否则：重载 > 虚函数 > `std::function` > 模板。

---

## 附录 A · 快速复现实验

```powershell
# 1) 编译 + 运行全部 8 个示例（零警告要求）
cd C:\Users\Waj07\Desktop\CPPstudy\cpp-notes
.\build.ps1 -Chapter 04-templates -WX
.\build.ps1 -Chapter 04-templates -Run

# 2) 验证「模板调用被内联」（期望两条都是 0）
cl /nologo /std:c++20 /EHsc /utf-8 /permissive- /O2 /Faout\01.asm `
   /Foout\ /Feout\01.exe 04-templates\01_function_template.cpp
Select-String -Path out\01.asm -Pattern 'call.*maxValue'
Select-String -Path out\01.asm -Pattern 'call.*Factorial'

# 3) 看对象布局（CRTP 无 vfptr、空基类 0 字节）
cl /nologo /std:c++20 /EHsc /utf-8 /permissive- /d1reportAllClassLayout /c `
   04-templates\06_crtp_policy.cpp > out\layout.txt
Select-String -Path out\layout.txt -Pattern 'class Point' -Context 0,8

# 4) 语言标准兼容性实测：只有 05/07/08 需要 C++20
.\build.ps1 -Chapter 04-templates -Std c++17 -WX   # 预期 05/07/08 失败
```

## 附录 B · 各文件的语言标准要求（本机实测）

| 文件 | C++17 | C++20 | 需要 C++20 的具体语法 |
|---|---|---|---|
| `01_function_template.cpp` | OK | OK | —— |
| `02_class_template.cpp` | OK | OK | ——（用到的 CTAD 是 C++17） |
| `03_specialization.cpp` | OK | OK | ——（`void_t` 是 C++17） |
| `04_variadic_forwarding.cpp` | OK | OK | ——（折叠表达式是 C++17） |
| `05_concepts.cpp` | **FAIL** | OK | `concept` / `requires` / `std::integral auto` |
| `06_crtp_policy.cpp` | OK | OK | ——（`inline static` 是 C++17） |
| `07_template_metaprogramming.cpp` | **FAIL** | OK | `concept`、`std::convertible_to`、缩写模板 |
| `08_practical_templates.cpp` | **FAIL** | OK | 类类型 NTTP（`FixedString` 做模板参数）、`std::constructible_from` |

`08` 在 C++17 下的第一条报错就是最好的说明：

```
error C7592: a non-type template-parameter of type 'FixedString'
              requires at least '/std:c++20'
error C2039: 'constructible_from': is not a member of 'std'
```

## 附录 C · 为什么证据探针不放在章节目录里

`build.ps1` 的取源文件逻辑是：

```powershell
$sources += Get-ChildItem -Path $dir -Recurse -Filter *.cpp -File |
            Where-Object { $_.FullName -notmatch '\\build\\' }
```

也就是**递归**扫描章节目录下的**所有** `.cpp`。本文里为了做实测而写的探针
（`probe5`/`probe7`/`probe9_fail`/`probe_sfinae_err`/`probe_concept_err`/
`probe1` 等）**全都是故意写错的代码**，一旦放进 `04-templates\` 子目录，
`.\build.ps1 -Chapter 04-templates -WX` 就会把它们一起编译，
必然产生 error，**破坏「全部 OK、零 warning」的验收标准**（本项目已经实际踩过一次）。

所以约定是：

| 文件类型 | 位置 | 是否进构建 |
|---|---|---|
| 教学示例（8 个，各自带 `main`，必须编译通过） | `04-templates\*.cpp` | 是 |
| 一次性实测探针（可能故意编译失败） | `cpp-notes\scratch\*.cpp` | 否（不在章节目录下） |
| 实测结论与报错原文 | `04-templates\NOTES.md` | 不参与编译 |

**如果以后确实需要把失败示例放进章节**，只有两个干净的做法：

1. 把文件名后缀改成构建脚本扫不到的形式（例如 `xxx.cpp.txt`），
   并在文档里说明「改名后即可编译」；
2. 或者给 `build.ps1` 加一个「排除目录」的约定——但这属于构建脚本的改动，
   需要单独评审，**不要为了放示例去动它**。

