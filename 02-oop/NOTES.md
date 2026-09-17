# 第 02 章：面向对象（C++）

> 面向读者：已经会写 C 风格的 C++（函数、指针、`struct`、`std::vector`），但**从未写过面向对象**的实用派工程师。
> 本章目标：不是背概念，而是建立一套**能在真实项目里做决策**的模型 —— 什么时候该用继承、对象什么时候被拷贝、资源什么时候被释放、多态到底花了多少钱。

---

## 0. 怎么用这一章

1. 先读本文的「三条主线」（第 2 节），建立心智模型；
2. 再按顺序编译运行 `01` 到 `09` 的示例，**先看输出、再回头看代码**（每个示例的输出本身就是讲义）；
3. 卡住的地方查第 3 节「重点难点详解」，那里按「错误直觉 → 正确模型 → 代码证据 → 工程建议」四段写；
4. 最后做第 6 节的自测题，只做错题对应的章节复习。

编译方式（不要手敲 `cl.exe`，用仓库统一的脚本）：

```powershell
cd C:\Users\Waj07\Desktop\CPPstudy\cpp-notes
.\build.ps1 -Chapter 02-oop -WX          # 编译，警告当错误
.\build.ps1 -Chapter 02-oop -WX -Run     # 编译并运行
```

每个 `.cpp` 都自带 `int main()`，一个文件 = 一个独立可执行文件，产物在 `build\02-oop\`。

---

## 1. 本章地图

| 文件 | 主题 | 一句话结论 |
| --- | --- | --- |
| `01_class_basics.cpp` | `class`/`struct`、成员函数、`this`、访问控制、构造/析构、初始化列表、`explicit`、`mutable`、`= default` / `= delete` | 构造函数体内的 `=` 是**赋值**不是初始化；成员永远按**声明顺序**初始化 |
| `02_copy_move_semantics.cpp` | Rule of Three / Five / Zero、深浅拷贝、`std::move`、RVO/NRVO、copy-and-swap | 类**直接持有**裸资源时才需要 Rule of Five；用 `vector`/`string`/`unique_ptr` 当成员就什么都别写 |
| `03_raii_and_resource.cpp` | RAII：文件/锁/内存守卫、栈展开、析构不抛异常、两阶段构造 | 把资源生命周期绑到对象生命周期，就再也不需要手写配对的释放与 `finally` |
| `04_inheritance_basics.cpp` | 三种继承方式、权限表、构造析构顺序、名字隐藏、多重/菱形/虚继承 | `public` 继承表达 is-a；只为复用代码应改用组合 |
| `05_virtual_polymorphism.cpp` | 虚函数、`override`/`final`、虚析构、抽象类、`dynamic_cast`、`typeid`、vptr/vtable、`sizeof` 实测 | 多态的代价是一个隐藏指针 + 一次间接调用；打算多态使用就必须有虚析构 |
| `06_slicing_and_pitfalls.cpp` | 对象切片、`vector<Base>` 陷阱、构造/析构中调用虚函数、隐藏 vs 覆盖 | 多态只在**指针和引用**上生效；构造/析构期间没有多态 |
| `07_special_members_operator_overload.cpp` | 成员 vs 非成员、`operator<<`、`operator[]` 双版本、`operator<=>`、前后缀自增、仿函数、`explicit operator bool` | 运算符重载的判据是「语义对使用者是否显然」，不是「能不能写」 |
| `08_design_patterns_practical.cpp` | 简单/抽象工厂、Meyers 单例、策略（虚接口 vs 模板）、观察者（虚接口 vs `std::function`）、Pimpl、组合优于继承 | 先找变化点，再决定用继承、组合还是模板 |
| `09_mini_project_shapes.cpp` | 图形库：抽象基类 + 三个派生类 + `vector<unique_ptr<Shape>>` + 排序 + RAII 报表 | 多态容器的标准写法：抽象接口 + `unique_ptr` 存储 + `const&` 传参 + lambda 定制算法 |

---

## 2. 三条主线（读这一章之前必须先建立的心智模型）

### 主线一：对象生命周期 —— 「谁在什么时候创建、拷贝、移动、销毁它」

C++ 与 Java / C# / Python 最大的区别是：**对象的存在方式本身就是程序语义的一部分**。四件事必须同时想清楚：

| 阶段 | 触发时机 | 一句话要点 |
| --- | --- | --- |
| 构造 | 变量定义、`new`、按值传参、按值返回、容器扩容 | 按「基类 → 成员（按声明顺序）→ 自己的 body」执行 |
| 拷贝 | 按值传参、按值返回（未优化时）、`v = w`、容器插入左值 | 默认是**逐成员拷贝**，对裸指针来说是浅拷贝 |
| 移动 | `std::move`、返回局部变量、容器扩容（成员移动是 `noexcept` 时） | 只「偷」资源，源对象进入合法但空的状态 |
| 析构 | 离开作用域、`delete`、异常栈展开、容器清空 | 按构造的**逆序**执行；默认 `noexcept` |

三条最容易吃亏的推论：

1. **「拷贝」不是免费的、也不是无害的。** 一个按值接收 `std::string` 的函数，每次调用都在堆上分配一次；一个按值接收多态基类的函数，会直接把派生部分切掉（见第 3.4 节）。
2. **析构函数是你唯一的、可靠的清理点。** 只要有资源（内存、文件、锁、socket、事务），就应该有一个对象的析构函数负责释放它。这就是第 03 章 RAII 的全部内容。
3. **对象的生命周期与指针无关。** `Base* p = new Derived();` 里存在两个概念：指针 `p`（一个变量，作用域结束时消失）和它指向的对象（只有在 `delete p` 时才消失）。这两件事经常被初学者混成一件。

### 主线二：封装与不变量 —— 「类的职责是维护不变量，不是装数据」

**不变量（invariant）** 是「这个类型的对象在任何时刻都必须成立的条件」。例如：

- 银行账户：`balance_ >= 0`；
- 三角形：任意两边之和大于第三边；
- `std::vector`：`size() <= capacity()`，且 `data_[0..size())` 都是已构造对象。

面向对象设计的核心动作就是：**把所有可能破坏不变量的路径，从接口层面堵死。**

```cpp
// 01_class_basics.cpp / 09_mini_project_shapes.cpp 里的做法
class Triangle final : public Shape {
public:
    Triangle(double a, double b, double c) : a_(a), b_(b), c_(c) {
        if (a + b <= c || a + c <= b || b + c <= a) {
            throw std::invalid_argument("三条边不满足三角不等式，无法构成三角形");
        }
    }
    // ...
private:
    double a_, b_, c_;      // 私有：外部无法绕过构造函数的校验
};
```

三条实践规则：

1. **数据成员一律 `private`**（`protected` 只在「派生类确实需要」时才用，而且一个类里的 `protected` 数据成员往往是设计缺陷）；
2. **校验写在构造函数里**，让「非法的对象」根本造不出来，而不是让每个使用者都去检查；
3. **成员函数分两类**：查询（`const`，承诺不改对象）与命令（非 `const`，负责保持不变量）。

判断一个类设计得好不好，最简单的问法就是：**这个类的不变量是什么？有多少条路径能破坏它？**

### 主线三：运行时多态 vs 编译期多态

「同一个操作，对不同类型有不同的行为」有两种实现路线，本章讲的是第一种，第二种在 `04-templates` 章：

| 维度 | 运行时多态（虚函数，本章） | 编译期多态（模板 / 概念，`04-templates`） |
| --- | --- | --- |
| 决定时机 | 运行期，看对象的**动态类型** | 编译期，看**实例化时的类型实参** |
| 语法 | `virtual` + `override` + 基类指针/引用 | `template <typename T>`、`concept` |
| 调用开销 | 一次 vptr 查表 + 间接跳转，**阻止内联** | 可完全内联，理论零开销 |
| 对象大小 | 每个对象多一个 `vptr`（`05` 章实测：16 字节 vs 8 字节） | 不变 |
| 代码体积 | 一份实现 | 每种类型实参各生成一份（代码膨胀） |
| 能否放进同一个容器 | **能**（`vector<unique_ptr<Base>>`） | 不能（类型不同） |
| 能否运行期切换 | **能** | 不能 |
| 编译依赖 | 低（只要接口头文件） | 高（要看到实现） |

**决策口诀**：

- 需要「运行期才知道具体类型」、「放进同一个容器统一处理」、「二进制接口不暴露实现」→ **虚函数**；
- 「类型在编译期就固定」且「在热路径上」→ **模板**；
- 贪心一点：外层用虚接口做粗粒度选择，内层热路径用模板优化（`08` 章的策略模式演示了两种写法并存）。

---

## 3. 重点难点详解

### 3.1 初始化列表 vs 构造函数体内赋值 —— 以及成员初始化顺序

**错误直觉**
「初始化列表只是一种写法，和函数体里赋值等价，甚至更啰嗦。」

**正确模型**
构造函数体里的 `=` 是**赋值运算符**调用；初始化列表是**直接构造**。

- `name_ = n;`（在函数体里）等价于：先 `name_` 默认构造 → 再调用 `operator=`。对 `std::string` 就是一次空串构造 + 一次可能的堆分配。
- `name_(n)`（在初始化列表里）是：直接用 `n` 拷贝构造 `name_`。一次构造，没有多余步骤。

更关键的是**有些成员根本无法赋值**：

| 成员类型 | 能否在函数体里赋值 | 原因 |
| --- | --- | --- |
| `const int` | 不能 | 必须在创建时确定，之后不可修改（`error C2789`） |
| `int&` | 不能 | 引用必须初始化，不能重新绑定（`error C2530`） |
| 没有默认构造函数的类类型 | 不能 | 压根无法「先默认构造」 |
| 基类（子对象） | 不能 | 基类构造只能通过初始化列表指定 |

**代码证据**（`01_class_basics.cpp`）

```cpp
class ConstAndRefMember {
public:
    // 只能用初始化列表
    ConstAndRefMember(int v, int& ext) : const_value_(v), ext_(ext) {}

    // 【错误写法】下面的写法无法编译：
    //   ConstAndRefMember(int v, int& ext) { const_value_ = v; ext_ = ext; }
    //   error C2789: 必须是初始化一个 const 限定的对象
    //   error C2530: 引用必须初始化
private:
    const int const_value_;
    int& ext_;
};
```

**第二个坑：初始化顺序由声明顺序决定**

```cpp
class OrderTrap {
public:
    // 初始化列表故意写成 second_ 在前
    OrderTrap() : second_(first_ + 1), first_(100) {}
    // 实际结果：first_ = 100，second_ = 101
    // 因为 first_ 在类里先声明，所以先初始化
private:
    int first_;      // 先声明 => 先初始化
    int second_;     // 后声明 => 后初始化
};
```

运行 `01_class_basics.cpp` 可以看到实际输出是 `first_ = 100, second_ = 101`，**而不是**按初始化列表书写顺序算出来的结果。

如果两个成员真的有依赖关系（`second_` 依赖 `first_`），而声明顺序又反了，那么 `second_` 读到的就是**未初始化的值**（未定义行为）。这种 bug 在 Debug / Release 下表现可能不同。

**工程建议**

1. 初始化列表顺序**永远**与声明顺序一致；MSVC 在 `/W4` 下会报 `C5038` 提示顺序不一致，本项目把警告当错误（`/WX`），所以写错根本编译不过。
2. 需要「先算一个值、再用它初始化成员」时，用**委托构造函数**或**静态工厂函数**，不要在初始化列表里互相引用。
3. `explicit` 不是可选项：所有单参数构造函数（以及转换运算符）默认加 `explicit`，只在确实需要隐式转换时去掉。

### 3.2 Rule of Three / Five / Zero

**错误直觉**
「编译器会自动生成拷贝构造、拷贝赋值、析构，所以我什么都不用写。」——只在**成员自己管好资源**时才成立。

**正确模型**

| 规则 | 内容 | 何时必须 |
| --- | --- | --- |
| Rule of Three | 需要自定义**析构 / 拷贝构造 / 拷贝赋值**中的任意一个时，通常三个都要写 | 类直接持有需要手工释放的资源（裸 `new` / 文件句柄 / 锁） |
| Rule of Five | 再加上**移动构造 / 移动赋值** | 同上，且希望容器扩容、返回值传递走移动 |
| Rule of Zero | 一个都不写，让编译器生成 | 成员是 `vector` / `string` / `unique_ptr` / `shared_ptr` 这类自己会管资源的类型 |

**编译器自动生成的规则（C++20）** —— 这一条必须记住：

| 你声明了什么 | 拷贝构造 | 拷贝赋值 | 移动构造 | 移动赋值 |
| --- | --- | --- | --- | --- |
| 什么都没声明 | 生成 | 生成 | 生成 | 生成 |
| 析构函数 / 拷贝构造 / 拷贝赋值（任一） | 生成（弃用） | 生成（弃用） | **不生成** | **不生成** |
| 移动构造 / 移动赋值（任一） | **删除** | **删除** | 生成 | 生成 |
| 显式 `= delete` | 删除 | 删除 | 删除 | 删除 |

「声明了析构函数 → 不再生成移动操作」是最阴险的一格：`std::move(x)` 仍然能编译，只是**静默退化成拷贝**，编译器不给任何警告。`02_copy_move_semantics.cpp` 里的 `HasDestructorOnly` 演示了这一点，并用一个 concept 断言把它固定成编译期证据：

```cpp
template <typename T>
concept HasTrueMoveCtor = requires(T&& r) {
    { T(static_cast<T&&>(r)) } noexcept;   // 只有真正的 noexcept 移动构造才满足
};
static_assert(!HasTrueMoveCtor<HasDestructorOnly>, "std::move 会静默退化成拷贝");
static_assert(HasTrueMoveCtor<String>, "String 自己写了 noexcept 移动构造");
```

> 顺带一个「陷阱的陷阱」：`std::is_move_constructible_v<T>` **不能**用来判断有没有移动构造。它问的是「能不能用右值构造」，而拷贝构造的参数是 `const T&`，能绑定右值，所以「只能拷贝」的类型在这里也返回 `true`。

**浅拷贝 → double free 的完整机制**（`02_copy_move_semantics.cpp`）

```cpp
// 【错误实现】
String(const String& o) : data_(o.data_), size_(o.size_) {}   // 只复制指针！
```

```
a ---+
     +--> [同一块堆缓冲区]
b ---+
```

析构顺序是 `b` 先 `delete[]`，`a` 再 `delete[]` **同一个地址** ⇒ double free。
现象：Debug 下 MSVC 报 heap corruption / CRT 断言；Release 下随机崩溃或静默数据损坏。

正确做法是**深拷贝**：

```cpp
String(const String& o) : data_(nullptr), size_(o.size_) {
    if (size_ > 0) {
        data_ = new char[size_ + 1];
        std::memcpy(data_, o.data_, size_ + 1);
    }
}
```

**copy-and-swap 惯用法**（异常安全 + 自赋值安全）

```cpp
String& operator=(const String& o) {
    String tmp(o);      // ① 先做副本：这里可能抛异常，此时 *this 完全没被碰过
    Swap(tmp);          // ② 只做 noexcept 的指针交换
    return *this;       // ③ tmp 析构，释放旧资源
}

String& operator=(String&& o) noexcept {
    if (this != &o) {   // 移动赋值不经过 copy-and-swap，必须自己检查自赋值
        Release();
        data_ = o.data_; size_ = o.size_;
        o.data_ = nullptr; o.size_ = 0;
    }
    return *this;
}
```

- 异常安全：要么完全成功，要么完全没变（强异常保证）；
- 自赋值安全：`d = d;` 天然正确，不需要额外判断；
- 代价：多一次拷贝，热路径上可以改写成「先分配新资源、成功后再释放旧的」。

**`std::move` 只是类型转换**

`std::move(x)` 唯一做的事是把表达式变成右值（静态类型 `T&&`），它**不移动任何东西**。真正搬资源的是被选中的移动构造 / 移动赋值。两个常见误解：

- `std::move` 对 `const` 对象无效：`const T&&` 无法匹配 `T&&`，只能匹配 `const T&`，于是走拷贝（示例里 `cs` 被 `std::move` 后内容仍在）；
- `return std::move(local);` 是**负优化**：它把返回值变成右值引用，**阻止 NRVO**，反而多一次移动。直接 `return local;` 就好。

**RVO / NRVO**（`02_copy_move_semantics.cpp` 实测输出）

| 写法 | 实测拷贝/移动次数 | 说明 |
| --- | --- | --- |
| `return Tracker(7);` | 0 拷贝 0 移动 | C++17 起是**语言保证**的拷贝消除（guaranteed copy elision） |
| `Tracker local(9); return local;` | 0 拷贝 0 移动 | NRVO，只是**允许的优化**，不是保证 |
| `Tracker PassThrough(Tracker t) { return t; }` | 1 拷贝 1 移动 | 实参→形参那次拷贝无法消除；返回参数时走 `noexcept` 移动 |

**工程建议**

1. 先问「我这个类直接持有裸资源吗？」——不是就走 **Rule of Zero**，一个特殊成员函数都别写；
2. 是的话写 **Rule of Five**，赋值运算符用 copy-and-swap；
3. 移动构造 / 移动赋值一律标 `noexcept`（否则 `std::vector` 扩容时会退回复制）；
4. 写完之后用 `static_assert` 把接口约定固定下来：

```cpp
static_assert(!std::is_copy_constructible_v<NonCopyable>, "NonCopyable 必须禁止拷贝");
static_assert(std::is_nothrow_move_constructible_v<ZeroRuleWidget>, "移动必须 noexcept");
```

### 3.3 虚析构函数

**错误直觉**
「基类的析构函数和别的成员函数一样，不写 `virtual` 也没关系。」

**正确模型**
`delete base_ptr` 时，如果析构函数**不是**虚函数，编译器做的是**静态绑定**：只调用 `Base::~Base()`，`Derived::~Derived()` 根本不会执行 ⇒ 派生类持有的资源全部泄漏，而且是**未定义行为**。

**代码证据**（`05_virtual_polymorphism.cpp` 实测输出）

```
--- 场景 A：基类析构【不是】virtual ---
  分配次数 = 1, 释放次数 = 0  => 派生类析构【没有】被调用，内存泄漏 1 次
--- 场景 B：基类析构是 virtual ---
  分配次数 = 1, 释放次数 = 1  => 配对成功，无泄漏
```

编译期也能把它变成硬约束：

```cpp
static_assert(!std::has_virtual_destructor_v<BaseNoVirtualDtor>, "这个基类不该被多态删除");
static_assert(std::has_virtual_destructor_v<BaseWithVirtualDtor>, "多态基类必须有虚析构");
```

**判定标准（工程版）**

| 情况 | 是否加 `virtual` 析构 |
| --- | --- |
| 类里有任何虚函数（哪怕只有一个） | **必须加** |
| 类没有任何虚函数，也不打算被多态使用 | **不加**（加了会平白多一个 `vptr`） |
| 拿不准 | **加**（代价是一个指针，漏掉的代价是泄漏 + UB） |
| 用 `unique_ptr` / `shared_ptr` 且从不写 `delete base_ptr` | 严格说可以不加，但只要有一处可能写出 `delete base_ptr` 就必须加 |

**补充**：`std::shared_ptr<Base> p = std::make_shared<Derived>();` 在构造时「记住」了真实类型，删除时不需要虚析构；但**裸指针 `delete` 需要**。不要依赖这个差别，统一加 `virtual` 更省心。

### 3.4 对象切片（object slicing）

**错误直觉**
「多态是类型系统的事，我把 `Derived` 传给要 `Base` 的函数，它自然还是 `Derived`。」

**正确模型**
按值传递 / 按值存储基类时，发生的是**基类的拷贝构造**，只复制基类子对象，派生类新增的成员**整个被丢掉**。这个新对象的动态类型就是 `Base`，虚函数分发也随之退化为基类版本。

**代码证据**（`06_slicing_and_pitfalls.cpp` 实测输出）

```
premium.Type() = 尊享账户, CashbackRate() = 0.05
sizeof(Account) = 56, sizeof(PremiumAccount) = 64

--- 错误写法：按值传参 ---
  FeeByValue(premium) 里 Type() 会变成 "基础账户"
  Account sliced = premium;
    sliced.Type()       = 基础账户   <- 变成基类
    premium.Type()      = 尊享账户   <- 原对象不受影响

--- 正确写法：const 引用 / 指针 ---
  FeeByReference(premium) = 0，Type() = 尊享账户
```

容器版本更危险，因为它是**静默**的：

```cpp
std::vector<Account> v;               // 【错误】存进去就切片，没有任何警告
v.push_back(premium);                 // 编译通过，运行也不报错，业务逻辑悄悄错了

std::vector<Account*> v2;             // 【错误】多态对了，但谁 delete？异常就泄漏
v2.push_back(new PremiumAccount(...));

std::vector<std::unique_ptr<Account>> v3;                 // 【正确】
v3.push_back(std::make_unique<PremiumAccount>(...));      // 多态完整 + RAII 自动释放
```

**工程建议**

1. **看到形参是 `Base`（没有 `&` 或 `*`）就警惕**，这几乎总是 bug；
2. 传参统一用 `const Base&`；需要表达「可能没有」时用 `Base*`（并约定为空表示无）或 `std::optional<std::reference_wrapper<Base>>`；
3. 容器统一用 `std::vector<std::unique_ptr<Base>>`；需要共享所有权才用 `shared_ptr`；
4. 把基类的拷贝构造**显式删除**可以从编译器层面堵住切片（`09_mini_project_shapes.cpp` 里的 `Shape` 就是这么做的）：

```cpp
Shape(const Shape&) = delete;
Shape& operator=(const Shape&) = delete;
```

### 3.5 构造函数 / 析构函数中调用虚函数

**错误直觉**
「构造函数里调用虚函数，会调用到派生类的实现，正好用来做初始化。」

**正确模型**
**不会。** 在构造函数和析构函数执行期间，虚函数调用退化为**静态绑定**，调用的是「当前正在构造/析构的那一层」的版本。

原因（实现机制）：构造过程自外向内依次执行「基类 → 成员 → 自己的 body」，每进入一层，编译器就把 `vptr` 设置为**当前这一层**的虚表；析构时反过来，`vptr` 逐层退回基类。所以「当前层」永远看不到更派生的实现。

标准为什么这么规定：如果基类构造期间能调到派生类实现，那个实现很可能访问**尚未初始化**的派生类成员 ⇒ 未定义行为。语言选择了「安全但不直观」。

**代码证据**（`06_slicing_and_pitfalls.cpp` 实测顺序）

```
BaseWithVirtualCall 构造函数里调用 Describe()
  [BaseWithVirtualCall::Describe] 基类版本
  typeid(*this).name() = class `anonymous namespace'::BaseWithVirtualCall  <- 不是派生类
DerivedWithVirtualCall 构造函数开始（基类已构造完，派生成员刚初始化）
  [DerivedWithVirtualCall::Describe] 派生类版本      <- 在派生类 body 里才正常
对象构造完成后，正常调用：
  [DerivedWithVirtualCall::Describe] 派生类版本
离开作用域，开始析构：
DerivedWithVirtualCall 析构函数（派生部分还完整）
  [DerivedWithVirtualCall::Describe] 派生类版本
BaseWithVirtualCall 析构函数里调用 Describe()
  [BaseWithVirtualCall::Describe] 基类版本           <- 又退回基类
```

四条结论：

1. 基类构造期间 → **基类**版本；
2. 基类构造完、派生成员初始化完之后（派生类构造函数体内）→ **派生**版本；
3. 派生类析构函数体内 → **派生**版本（派生部分还完整）；
4. 进入基类析构阶段后 → 又退回**基类**版本。

「读到还没初始化的派生成员」的具体后果，示例里的 `FragileBase` 打印出基类版本返回的 `-1`：

```cpp
class FragileBase {
public:
    FragileBase() {
        // 期望拿到派生类的值，实际拿到基类版本 => -1
        std::cout << DerivedValue() << "\n";
    }
    virtual int DerivedValue() const { return -1; }
};
class FragileDerived : public FragileBase {
public:
    FragileDerived() : value_(99) {}
    int DerivedValue() const override { return value_; }   // 只有在构造完成后才可用
private:
    int value_;
};
```

**更危险的版本**：在构造 / 析构中调用**纯虚函数**，会调用到未定义的纯虚实现，运行时报
`pure virtual function call`（MSVC 错误号 R6025）并**立即终止进程**。

**工程建议**

1. 构造函数 / 析构函数里**不要**调用虚函数（除非它完全不依赖派生状态）；
2. 需要「对象构造完成后再初始化」时，提供显式的 `Init()` / `Start()` / `Open()`，
   或者用工厂函数在对象完全构造之后再调用它；
3. 基类构造期间要报告信息时，用**非虚**函数 + 参数，不要用虚函数。

### 3.6 隐藏（hiding）vs 覆盖（overriding）

**错误直觉**
「派生类写一个同名函数就是覆盖了基类的虚函数。」

**正确模型**
覆盖（override）需要**同时**满足：

1. 基类函数是 `virtual`；
2. 派生类函数的名字、参数列表、`const` 限定、引用限定符**全部一致**；
3. 返回类型兼容（协变返回类型允许派生类返回更派生的指针/引用）。

只要有一条不满足，就是**名字隐藏（name hiding）**：派生类作用域里所有同名函数都会把基类的同名重载**全部隐藏**（与参数无关），基类版本从此在派生类上不可见。

**三种最隐蔽的写法错误**（`05` / `06` 章示例）

| 错误 | 例子 | 后果 |
| --- | --- | --- |
| 参数类型不同 | `Base::F(int)` vs `Derived::F(double)` | 通过 `Base&` 调用走基类版本；通过 `Derived` 对象调用走派生版本 —— 同一行代码行为不同 |
| 忘了 `const` | `Base::F() const` vs `Derived::F()` | `Derived::F()` 不覆盖，`const Derived&` 上调用走基类版本 |
| 基类漏写 `virtual` | `void Base::F()` | 连覆盖的前提都不存在，永远静态绑定 |

**为什么必须写 `override`**：上面三种错误，编译器**都不会报错**（除第三种会不同表现），只会在运行时表现「多态静默失效」。加上 `override` 后全部变成编译错误：

```
error C3668: "Derived::F": 带有重写说明符 "override" 的方法没有重写任何基类方法
```

**代码证据**（`04` 章的名字隐藏）

```cpp
class Shape {
public:
    void Describe() const;                          // 无参
    void Describe(const std::string& tag) const;    // 带参重载
};

class Square : public Shape {
public:
    void Describe() const;              // 只定义无参版本 => 基类的带参版本被隐藏
    using Shape::Describe;              // 用 using 把基类的重载「拉」进派生类作用域
};
```

删掉 `using Shape::Describe;` 之后，`sq.Describe("x")` 报的是
`error C2664`（参数不匹配），而不是「找不到函数」—— 这个报错信息很容易让人看懵。

**工程建议**

1. **虚函数一律写 `override`**（这条几乎没有例外）；
2. 想让某层之后不许再覆写，写 `final`；整个类不想被继承，在类名后写 `final`；
3. 派生类需要保留基类的同名重载时，写 `using Base::func;`；
4. 最省事的方案：**给函数取不同的名字**，从根本上避免隐藏；
5. 基类的虚函数如果是「必须被覆写」，写成纯虚函数（`= 0`），让编译器替你强制。

---

## 4. 面向对象设计经验（工程视角）

### 4.1 什么时候用继承

只有在**同时**满足下面两条时才用 `public` 继承：

1. **语义上是 is-a**：`Derived` 确实「是一种」`Base`，把 `Derived` 用在任何需要 `Base` 的地方都成立且不破坏 `Base` 的契约；
2. **需要被统一处理（多态）**：你会写出 `Base&` / `Base*` / `vector<unique_ptr<Base>>` 这类代码。

只满足第 1 条、但不需要多态时，继承也是可用的（比如「标签分派」），但收益很小。
只满足第 2 条、语义不是 is-a 时，应该用**组合 + 接口**。

**反面教材（`04_inheritance_basics.cpp` 里详细讨论）**：

```cpp
class Stack : public std::vector<int> { ... };   // 错在哪？
```

- `vector` 的 `insert` / `erase` / `operator[]` 全部对外可见，使用者可以绕过栈的规则直接改中间元素 ⇒ 不变量失效；
- `vector` 没有虚析构函数，多态删除是未定义行为；
- 栈「不是一种」`vector`，is-a 不成立。

正确写法：

```cpp
class Stack {
public:
    void push(int v) { data_.push_back(v); }
    int pop() { int v = data_.back(); data_.pop_back(); return v; }
private:
    std::vector<int> data_;              // 组合：只暴露我愿意承诺的接口
};
```

### 4.2 组合优先的理由

| 维度 | 继承 | 组合 |
| --- | --- | --- |
| 耦合强度 | 最强（派生类依赖基类实现细节） | 弱（只依赖被组合类型公开的接口） |
| 接口暴露 | `public` 继承会暴露基类**全部**接口 | 只暴露你愿意转发的方法 |
| 替换时机 | 编译期固定 | 可在运行期替换（传入不同实现） |
| 层次深度 | 一层层叠加，构造/析构/分派/菱形问题全部放大 | 「一层套一层」，关系始终局部可预测 |
| 测试 | 难以替换基类行为（除非用虚接口） | 直接注入假对象（`08` 章的 `MemoryLogger`） |

**一句话判据：为了「复用代码」而继承基本都是错的；为了「被统一处理（多态）」而继承才是对的。**

### 4.3 抽象基类（接口）的设计原则

1. **只放「所有实现都能承诺」的操作**，宁少勿多。接口一旦发布就很难改，加一个纯虚函数会让所有实现方编译失败；
2. **析构函数必须是 `virtual`**（或者 `protected` 且非虚，明确禁止通过基类指针删除）；
3. **不要有数据成员**，也不要有需要 `protected` 的状态 —— 接口应当是「纯契约」；
4. **可以有非虚的公共实现**（如 `05` 章的 `IShape::PrintInfo()`），它调用纯虚函数实现「模板方法」效果，避免派生类重复写样板代码；
5. **不要暴露实现细节**：参数和返回值尽量用接口类型或值类型，不要把 `Impl` 暴露出去；
6. **考虑用 `NVI`（非虚接口）惯用法**：public 非虚函数做校验/日志，再调用 private 虚函数做实际工作 —— 这样可以在不改变派生类的前提下加统一逻辑。

### 4.4 LSP（里氏替换）的实际含义

LSP 的工程化表述：**任何使用 `Base&` 的代码，换成任何 `Derived` 对象都必须继续正确工作。** 具体拆成四条可检查的规则：

1. **前置条件不能加强**：基类允许传 `-1`，派生类不能要求「必须为正」；
2. **后置条件不能减弱**：基类承诺「返回非空列表」，派生类不能返回空；
3. **不变量必须保持**：基类保证 `size() <= capacity()`，派生类不能破坏它；
4. **不能抛出基类没声明的异常**（除非异常规格允许）。

**经典的 LSP 反例：`Square` 继承 `Rectangle`**

```cpp
void Resize(Rectangle& r) {
    r.SetWidth(5);
    r.SetHeight(4);
    assert(r.Area() == 20);      // 对 Rectangle 成立
}
```

如果 `Square::SetWidth` 同时改了高度（为了保持正方形不变量），`Resize(square)` 就会失败 —— `Square` **不是**可以替换 `Rectangle` 的。这说明：「数学上正方形是矩形」并不等于「代码里 `Square` 该继承 `Rectangle`」。可选方案：

- 让 `Square` 不继承 `Rectangle`，而是各继承一个 `IShape` 接口（`09` 章的扩展练习就是这个思路）；
- 或者不提供 `SetWidth` / `SetHeight`，改成不可变对象 + `WithWidth()` 返回新对象。

**注意**：LSP 说的是**行为**契约，不是语法。编译器无法检查 LSP，只能靠设计评审和测试。

### 4.5 避免继承层次过深

经验值：**继承链超过 3 层就要重新审视**。原因：

1. 构造/析构顺序、虚函数分派、名字隐藏的排查成本随层数指数上升；
2. 中间层很容易变成「什么都有、什么都不明确」的上帝类，改动会波及所有下层；
3. 菱形继承（`04` 章）会出现数据冗余和二义性，虚继承虽然能解决但引入额外开销与「谁初始化虚基类」的复杂度；
4. 深层次往往暗示「这是在用继承表达组合关系」。

**替代方案**：

- 用**组合 + 接口**（实现类持有一个或多个策略对象）；
- 用**标签联合**（`std::variant` + `std::visit`）处理「封闭集合」的多态，避免虚函数开销；
- 用**模板**做编译期策略组合。

---

## 5. 实用开发习惯

### 5.1 `override` 必写

```cpp
// 正确
std::string Sound() const override;

// 错误：删掉 override 后，上面的签名一旦写错就变成「隐藏」，编译器不报错
std::string Sound() const;
```

一个团队约定：**只要意图是覆盖，就写 `override`；写不出来就说明签名不对。** 这条约定几乎零成本，却能挡住第 3.6 节里的全部三种隐蔽错误。

### 5.2 `explicit` 单参构造必写

```cpp
explicit Meters(double v);              // 不加的话 PrintMeters(3.5) 会隐式转换
explicit operator bool() const;         // 不加的话 int n = checker; 能编译
explicit Circle(double radius);
```

例外只有三种：确实要做隐式转换（如 `std::string` 从 `const char*`）、智能指针之间的转换、`std::initializer_list` 构造。**例外要写注释说明理由。**

### 5.3 `virtual` 析构的判定标准

见第 3.3 节的表格。压缩成一句话：

> **类里有虚函数 → 析构必须 `virtual`；没有虚函数也不打算多态使用 → 别加。**

可选加固：在多态基类后面加一行编译期约束，把这个约定变成构建失败而不是运行期泄漏：

```cpp
static_assert(std::has_virtual_destructor_v<IShape>, "多态基类必须有虚析构");
```

### 5.4 用 `final` 表达设计意图

```cpp
class Puppy final : public Dog { ... };     // 这个类不打算被继承
virtual int Legs() const final;             // 这个虚函数不允许再被覆写
```

三重收益：

1. 把设计意图写进代码（评审者一眼看到「到此为止」）；
2. 编译器可以**去虚化**，把虚调用优化成直接调用甚至内联；
3. 阻止有人无意中继承 —— 继承一个非为继承设计的类是常见的 bug 来源。

### 5.5 用 `static_assert` 做接口约束

把「口头约定」变成「编译期证据」，本章用到的清单：

```cpp
static_assert(std::is_abstract_v<Shape>, "Shape 必须是抽象类");
static_assert(std::has_virtual_destructor_v<Shape>, "多态基类必须有虚析构");
static_assert(!std::is_copy_constructible_v<Shape>, "Shape 禁止拷贝（防切片）");
static_assert(!std::is_copy_constructible_v<NonCopyable>, "NonCopyable 必须禁止拷贝");
static_assert(std::is_move_constructible_v<NonCopyable>, "NonCopyable 必须允许移动");
static_assert(std::is_nothrow_move_constructible_v<ZeroRuleWidget>, "移动必须 noexcept");
static_assert(sizeof(VirtualClass) == 2 * sizeof(int) + sizeof(void*), "虚函数给对象加了一个 vptr");
static_assert(std::three_way_comparable<Version>, "default 的 <=> 应带来全套比较运算符");
static_assert(std::is_nothrow_destructible_v<Derived>, "析构函数默认 noexcept");
```

好用的 trait 速查：`is_abstract_v`、`has_virtual_destructor_v`、`is_polymorphic_v`、`is_copy_constructible_v`、`is_move_constructible_v`、`is_nothrow_move_constructible_v`、`is_trivially_copyable_v`、`is_standard_layout_v`、`is_empty_v`。

### 5.6 面向对象相关代码评审检查清单

**类的基本形态**

- [ ] 所有数据成员都是 `private`？`protected` 数据成员有充分理由？
- [ ] 单参数构造函数都写了 `explicit`？转换运算符都写了 `explicit`？
- [ ] 构造函数是否建立了全部不变量？非法状态能否被构造出来？
- [ ] 查询函数都标了 `const`？
- [ ] 成员初始化列表的顺序与声明顺序一致？

**资源与生命周期（对照 `02` / `03` 章）**

- [ ] 类直接持有裸资源吗？持有 → 有 Rule of Five（含 `noexcept` 移动）吗？
- [ ] 不持有裸资源 → 是否误写了特殊成员函数（应该走 Rule of Zero）？
- [ ] 拷贝是深拷贝吗？有没有可能出现 double free / 悬空指针？
- [ ] 赋值运算符是否自赋值安全？是否提供了强异常保证（copy-and-swap）？
- [ ] 析构函数是否可能抛异常？（应当 `noexcept`，可能失败的收尾工作应放进显式 `Close()`）

**继承与多态（对照 `04` / `05` / `06` 章）**

- [ ] 每个 `public` 继承都满足 is-a 吗？是否只是「复用代码」（应改组合）？
- [ ] 多态基类有 `virtual` 析构吗？有对应的 `static_assert` 吗？
- [ ] 每个覆盖都写了 `override` 吗？该封口的写了 `final` 吗？
- [ ] 有没有在构造函数 / 析构函数里调用虚函数？
- [ ] 有没有「按值传基类」或 `vector<Base>` 的地方（切片风险）？
- [ ] 派生类里的同名函数是覆盖还是隐藏？需要 `using Base::func;` 吗？
- [ ] 依赖 `dynamic_cast` 的地方多吗？多的话是否该在接口上加虚函数？
- [ ] 继承层次是否超过 3 层？是否出现了菱形？虚继承是否真的是唯一选择？

**接口设计（对照 `08` / `09` 章）**

- [ ] 抽象基类有数据成员或 `protected` 状态吗？（应当没有）
- [ ] 接口里是否有「某些实现无法有意义地提供」的方法？（拆分接口或改用组合）
- [ ] 工厂返回的是 `unique_ptr<接口>` 吗？所有权是否清晰？
- [ ] 多态对象是否用 `unique_ptr` / `shared_ptr` 管理，而不是裸指针 + 手工 `delete`？
- [ ] 传入依赖时是否用「构造函数注入」而不是类内部 `new` / 单例？

---

## 6. 自测题（8 道，含答案与原理）

### 第 1 题

下面这个类，`second_` 的值是多少？

```cpp
class A {
public:
    A() : second_(first_ * 2), first_(21) {}
    int first() const { return first_; }
    int second() const { return second_; }
private:
    int first_;
    int second_;
};
```

<details>
<summary>答案与原理</summary>

**答案**：`second_` 是**未定义行为**产生的值，通常是 0，但绝不能依赖它；`first_` 是 21。

**原理**：成员初始化顺序**只由声明顺序决定**，与初始化列表的书写顺序无关。本例 `first_` 先声明，所以先初始化，`second_` 在初始化时 `first_` 还**没有**被赋值。`02` 章的 `OrderTrap` 用「先声明 `first_`、初始化列表里写 `second_(first_ + 1)`」实测得到 `first_ = 100, second_ = 101`，证明了这一点。

**为什么危险**：这类 bug 在 Debug/Release 下可能表现不同，读未初始化的 `int` 是 UB，编译器可以做任何事。MSVC 在 `/W4` 下会报 `C5038`（成员初始化顺序与声明顺序不一致），本项目用 `/WX` 把它变成编译错误，从而彻底避免。

</details>

### 第 2 题

`Copyable` 只声明了析构函数，没有声明拷贝/移动操作。下面这行会调用哪个函数？性能上有什么影响？

```cpp
Copyable a;
Copyable b(std::move(a));
```

<details>
<summary>答案与原理</summary>

**答案**：调用**拷贝构造**（`Copyable(const Copyable&)`），不是移动构造。

**原理**：用户声明了析构函数之后，编译器**不再隐式生成移动构造 / 移动赋值**（拷贝操作仍然生成）。`std::move(a)` 只是把 `a` 变成右值，此时重载决议找不到 `Copyable(Copyable&&)`，就退而选择 `Copyable(const Copyable&)` —— 因为 `const T&` 能绑定右值。

**性能影响**：如果成员里有 `std::vector` / `std::string`，这就是一次**深拷贝**（可能触发堆分配）而不是指针窃取。更糟的是：**编译器不会给任何警告**，`std::move` 看起来完全正常。

**工程做法**：

1. 用 concept 把它变成编译期证据（`02` 章的做法）：

```cpp
template <typename T>
concept HasTrueMoveCtor = requires(T&& r) { { T(static_cast<T&&>(r)) } noexcept; };
static_assert(!HasTrueMoveCtor<Copyable>, "std::move 会静默退化成拷贝");
```

2. 注意 `std::is_move_constructible_v<Copyable>` 在这里返回 `true`（它只问「能不能用右值构造」，拷贝构造也算），**不能**用它来判断「有没有移动构造」。

**结论**：类里只要声明了析构函数，就要顺手问一句「移动操作还在吗？」，需要就显式 `= default` 或自己实现。

</details>

### 第 3 题

下面两段代码，哪一段有内存泄漏？为什么？

```cpp
// 版本 A
struct Base  { ~Base() = default; };
struct Child : Base { std::vector<int>* data; Child() : data(new std::vector<int>) {} ~Child() { delete data; } };

// 版本 B
struct Base2 { virtual ~Base2() = default; };
struct Child2 : Base2 { std::vector<int>* data; Child2() : data(new std::vector<int>) {} ~Child2() override { delete data; } };
```

使用方式：`Base* p = new Child(); delete p;`（版本 A）/ `Base2* p = new Child2(); delete p;`（版本 B）。

<details>
<summary>答案与原理</summary>

**答案**：**版本 A 泄漏**。`delete p` 时静态类型是 `Base*`，而 `Base::~Base()` 不是虚函数 ⇒ 静态绑定 ⇒ 只调用基类析构，`Child::~Child()` **根本没有执行**，`data` 指向的 `vector` 永远不会被释放。而且这是**未定义行为**，实践中还可能更糟（比如基类析构释放了派生类仍在使用的资源）。

**版本 B 正确**：`Base2` 有虚析构，`delete p` 通过 vtable 找到 `Child2::~Child2()`，然后自动链式调用 `Base2::~Base2()`。

**`05_virtual_polymorphism.cpp` 的实测输出**：

```
--- 场景 A：基类析构【不是】virtual ---
  分配次数 = 1, 释放次数 = 0  => 派生类析构【没有】被调用，内存泄漏 1 次
--- 场景 B：基类析构是 virtual ---
  分配次数 = 1, 释放次数 = 1  => 配对成功，无泄漏
```

**加固手段**：

```cpp
static_assert(std::has_virtual_destructor_v<Base2>, "多态基类必须有虚析构");
```

**额外说明**：如果用 `std::unique_ptr<Base2>`，它会在删除时通过基类指针调用虚析构，同样是安全的；但如果基类**没有**虚析构且用 `unique_ptr<Base>` 持有 `Derived`，仍然是 UB。所以判定标准还是「有没有虚析构」这一条。

</details>

### 第 4 题

```cpp
class Base {
public:
    Base() { Print(); }
    virtual ~Base() = default;
    virtual void Print() const { std::cout << "Base\n"; }
};
class Derived : public Base {
public:
    Derived() { Print(); }
    void Print() const override { std::cout << "Derived\n"; }
};
int main() { Derived d; }
```

输出是什么？如果在 `Base::~Base()` 里也调用 `Print()`，析构时会输出什么？

<details>
<summary>答案与原理</summary>

**答案**：构造过程输出

```
Base
Derived
```

析构时如果 `~Base()` 调用 `Print()`，输出 `Base`（不是 `Derived`）。

**原理**：构造/析构期间虚函数**退化为静态绑定**，调用的是「当前正在构造/析构的那一层」的版本。

实现机制：构造顺序是「基类 → 成员 → 派生类 body」，每进入一层，编译器就把对象的 `vptr` 设置为**当前这一层**的虚表。所以在 `Base::Base()` 里 `vptr` 还指向 `Base` 的虚表，`Print()` 只能调到 `Base::Print`；等到 `Derived` 的成员初始化完、进入 `Derived::Derived()` 的函数体时，`vptr` 已经改成 `Derived` 的虚表，这时才输出 `Derived`。析构完全反过来：`vptr` 逐层退回基类，进入 `~Base()` 时又变回 `Base`。

**标准为什么这么规定**：如果基类构造期间能调到 `Derived::Print()`，而 `Derived::Print()` 访问了某个尚未初始化的派生类成员，那就是 UB。语言选择「安全但不直观」。

**`06_slicing_and_pitfalls.cpp` 的实测顺序**：基类构造中 → 基类版本；派生类构造体内 → 派生版本；派生类析构体内 → 派生版本；基类析构中 → 基类版本。

**延伸**：如果这里调用的是**纯虚函数**，运行时会报 `pure virtual function call`（MSVC R6025）并立即终止进程。

</details>

### 第 5 题

```cpp
class Base {
public:
    virtual void F(int x) const { std::cout << "Base::F(int)\n"; }
};
class Derived : public Base {
public:
    void F(double x) const { std::cout << "Derived::F(double)\n"; }
};
int main() {
    Derived d;
    Base& r = d;
    r.F(1);      // 输出？
    d.F(1);      // 输出？
}
```

两次调用分别输出什么？怎么改才能让两处行为一致？

<details>
<summary>答案与原理</summary>

**答案**：

- `r.F(1)` 输出 `Base::F(int)` —— 通过基类引用，静态绑定到 `Base::F(int)`，因为 `Derived::F(double)` **不是覆盖**；
- `d.F(1)` 输出 `Derived::F(double)` —— 在 `Derived` 的作用域里，名字 `F` 被 `Derived::F` 隐藏了基类的所有 `F`，重载决议只在 `Derived::F(double)` 中做，`1` 隐式转成 `double`。

**原理**：参数类型不一致（`int` vs `double`）⇒ 不构成覆盖，只构成**名字隐藏**。派生类作用域里出现同名函数，会把基类的**所有**同名重载全部隐藏（与参数列表无关）。这就是「多态静默失效」：同一行 `F(1)` 在两处行为完全不同。

**怎么改**：

1. **首选**：把签名改成一致，并加 `override`：

```cpp
void F(int x) const override { std::cout << "Derived::F(int)\n"; }
```

只要写了 `override`，保留 `double` 参数就会得到编译错误 `error C3668: 带有重写说明符 "override" 的方法没有重写任何基类方法` —— 编译器替你抓错。

2. 如果确实想同时提供两个重载，用 `using` 把基类版本拉进来：

```cpp
using Base::F;
void F(double x) const;
```

3. 或者干脆**取不同的名字**（最省事，也最不容易被后人改坏）。

**工程习惯**：虚函数一律写 `override`。这条约定几乎零成本，却能挡住「参数类型不同 / 忘了 `const` / 基类漏写 `virtual`」这三类隐蔽错误。

</details>

### 第 6 题

为什么默认应该优先选择「组合」而不是「继承」？请给出至少三条具体理由，并说明什么情况下仍然应该使用继承。

<details>
<summary>答案与原理</summary>

**组合优先的理由**：

1. **耦合更弱**：继承让派生类依赖基类的**实现细节**（基类改一个 `protected` 成员、加一个重载、调整初始化顺序，都可能悄悄影响派生类，见「名字隐藏」）；组合只依赖被组合类型**公开的接口**。
2. **接口不泄漏**：`public` 继承会把基类的**全部**接口暴露给使用者。经典反面教材是 `class Stack : public std::vector<int>`：使用者可以直接调用 `insert` / `erase` / `operator[]` 绕过栈的规则，不变量失效。组合只暴露你愿意转发的那几个方法。
3. **可以运行期替换**：组合的对象可以在运行期换成另一个实现（`08` 章把 `MemoryLogger` 注入 `OrderServiceWithDi`，测试里直接断言日志内容）；继承关系在编译期就固定了。
4. **可控的复杂度**：继承层次一深，构造/析构顺序、虚函数分派、名字隐藏、菱形继承问题全部放大；组合是「一层套一层」，关系始终局部、可预测。
5. **更好测试**：依赖注入让替换假实现变得容易，不需要为了测试去构造复杂的继承体系。

**仍然应该用继承的情况**：

1. **需要运行期多态**：要通过 `Base&` / `Base*` / `vector<unique_ptr<Base>>` 统一处理一族对象（如 `09` 章的 `Shape`）；
2. **语义上确实是 is-a**，并且基类是**为继承而设计**的：有虚析构、有明确的覆写点、文档写清了继承约定；
3. **需要覆写虚函数来定制行为**（模板方法模式），此时用 `public` 或 `private` 继承都合理；
4. **需要复用接口的默认实现**，且 is-a 成立（例如 `RectImpl : IShape` 复用了 `IShape::PrintInfo()`）。

**一句话判据**：**为了「复用代码」而继承基本都是错的；为了「被统一处理（多态）」而继承才是对的。**

如果对「B 是一种 A」这句话有丝毫犹豫，就用组合。

</details>

### 第 7 题

```cpp
class Resource {
public:
    Resource() { std::cout << "acquire\n"; }
    ~Resource() { std::cout << "release\n"; throw std::runtime_error("boom"); }
};
void f() {
    Resource r;
    throw std::logic_error("something failed");
}
int main() { try { f(); } catch (const std::exception& e) { std::cout << e.what() << "\n"; } }
```

程序会输出什么？会正常打印 `something failed` 吗？

<details>
<summary>答案与原理</summary>

**答案**：会输出

```
acquire
```

然后**进程直接终止**（调用 `std::terminate`），**不会**打印 `something failed`，`catch` 块也不会执行。

**原理**：C++11 起析构函数**默认带 `noexcept`**（隐式 `noexcept(true)`）。在栈展开过程中（此时已经有 `std::logic_error` 处于活跃状态），`~Resource()` 里又抛出异常 ⇒ 两个异常同时活跃 ⇒ 运行时调用 `std::terminate`，进程立即结束。

**`03_raii_and_resource.cpp` 的实测证据**：

```cpp
static_assert(std::is_nothrow_destructible_v<Derived>, "析构函数默认 noexcept");
```

**为什么语言这么规定**：如果析构函数可以随意抛异常，栈展开过程就会变得无法可靠完成 —— 到底该继续展开（那么新异常怎么办），还是停下（那么已展开部分的清理做不做）？标准选择了「直接终止」这条最保守的路线。

**工程做法**：

1. **析构函数里只做不会失败的释放动作**（`delete`、`close`、`unlock` 都是不会失败的）；
2. 必须报告错误的收尾工作（提交事务、刷新缓冲、关闭连接）**放到显式的 `Close()` / `Commit()` 里**，让调用方在对象还活着的时候处理失败；
3. 如果某个清理动作「理论上可能失败」，在析构里也要 `try { ... } catch (...) { /* 记日志，不传播 */ }` 兜住；
4. 需要「析构时必须报告失败」的场景，通常说明设计有问题：应该在对象存活期间显式调用一个可能失败的方法。

**相关的另一半**：抛异常时析构函数**会**被执行（栈展开），这正是 RAII 能提供异常安全保证的原因：

```cpp
try {
    ScopeGuard g("rollback-transaction", cleanup);
    throw std::runtime_error("模拟业务异常");
} catch (const std::exception& e) { ... }
// 输出：清理动作执行次数 = 1
```

</details>

### 第 8 题

下面是两次「求和」的实现。它们在性能和适用场景上有什么区别？在团队代码里你更推荐哪一个，为什么？

```cpp
// 写法甲
class IShape { public: virtual double Area() const = 0; virtual ~IShape() = default; };
double Sum(const std::vector<std::unique_ptr<IShape>>& shapes) {
    double t = 0;
    for (const auto& s : shapes) t += s->Area();
    return t;
}

// 写法乙
template <typename Shape>
double SumT(const std::vector<Shape>& shapes) {
    double t = 0;
    for (const auto& s : shapes) t += s.Area();
    return t;
}
```

<details>
<summary>答案与原理</summary>

**答案要点**

| 维度 | 写法甲（虚接口，运行期多态） | 写法乙（模板，编译期多态） |
| --- | --- | --- |
| 调用开销 | 每次 `Area()` 是一次间接跳转，**阻止内联**，也可能阻碍向量化 | 静态绑定，可完全内联；对具体类型甚至可以把 `Area()` 算成常量 |
| 对象大小 | 每个图形多一个 `vptr`（`05` 章实测：`sizeof` 从 8 涨到 16） | 无额外开销 |
| 代码体积 | 一份 `Sum` | 每种 `Shape` 实例化一份（类型多时膨胀） |
| 类型要求 | 必须继承 `IShape` | 只要「有 `Area()`」即可（隐式接口 / duck typing） |
| 能否混装不同类型 | **能**（`vector<unique_ptr<IShape>>` 可同时放圆和矩形） | 不能（`vector<Shape>` 类型必须统一） |
| 运行期替换 | **能** | 不能 |
| 编译依赖 | 低（只需接口头文件，实现可放动态库） | 高（模板要看到实现） |
| 编译错误可读性 | 好（错误指向具体类型不符） | 差（模板报错往往很长） |

**推荐哪一个**：**看场景，不看性能数字。**

- 「运行时才知道有哪些类型」（插件、跨模块、配置驱动、`vector` 里混装不同图形）→ **写法甲**。这是 `09` 章 `vector<unique_ptr<Shape>>` 的场景。
- 「类型在编译期就固定，且在**热路径**上」（数值计算内层循环、图像处理逐像素）→ **写法乙**。
- **实际项目里绝大多数情况下选甲**，理由是：
  1. `Shape::Area()` 这种调用的开销（一次间接跳转）相对于「算面积」本身可以忽略；
  2. 虚接口的编译期解耦收益（改实现不用重编译调用方）往往比那点运行期开销更值钱；
  3. 甲能放进同一个容器、能在运行期替换，扩展性更好；
  4. 乙一旦类型数量爆炸会显著拖慢编译、增加二进制体积。

**一个重要提醒**：不要用「我在 Debug 里测了一下虚函数慢」来做决定。微基准极易被编译器优化、inline 决策、CPU 分支预测和缓存效应误导。正确做法是：先用清晰的设计把功能做对，**用 profiler 找到真实热点**，再针对性优化（`05` 章示例里刻意没有打印耗时，就是为了说明这一点）。

**还有一个折中点**：如果类型集合是**封闭**的（就那几个），可以用 `std::variant<Circle, Rectangle, Triangle>` + `std::visit` —— 既没有虚函数开销，又能放进同一个容器，代价是类型集合固定、编译期可变性差。`05` 章末尾提到了这个方案。

</details>

---

## 7. 一页速查

```
对象生命周期
  构造：基类 -> 成员(按声明顺序) -> 自己的 body
  析构：完全反过来；默认 noexcept
  按值传递/返回、容器插入左值 => 会拷贝；std::move / 返回局部变量 => 可移动

三条规则
  Rule of Three : 析构 / 拷贝构造 / 拷贝赋值 要写就一起写
  Rule of Five  : 再加移动构造 / 移动赋值（都要 noexcept）
  Rule of Zero  : 成员自己管资源（vector/string/unique_ptr）=> 一个都别写

编译器生成的边界
  声明了 析构/拷贝构造/拷贝赋值 => 不再生成移动操作（std::move 静默退化为拷贝）
  声明了 移动操作             => 拷贝操作被删除（编译期报错，安全）

安全默认值（照抄即可）
  class Foo final : public IBar {           // 不打算被继承就 final
  public:
      explicit Foo(int x);                  // 单参构造必写 explicit
      ~Foo() override = default;            // 多态基类的析构必须 virtual
      void Do() const override;             // 覆盖必写 override
      Foo(const Foo&) = delete;             // 需要独占就不给拷贝
      Foo(Foo&&) noexcept = default;        // 移动一律 noexcept
  private:
      std::unique_ptr<Impl> impl_;          // 数据私有 + Rule of Zero
  };
  static_assert(std::is_abstract_v<IBar>, "接口必须是抽象类");
  static_assert(std::has_virtual_destructor_v<IBar>, "多态基类必须有虚析构");

多态四条禁令
  1. 不要按值传基类（切片）—— 用 const Base& / Base*
  2. 不要用 vector<Base>（切片）—— 用 vector<unique_ptr<Base>>
  3. 不要在构造/析构函数里调用虚函数（静态绑定）
  4. 虚函数不要漏写 override（漏了就是隐藏，编译器不报错）

设计判据
  只是为了复用代码 => 用组合
  需要被统一处理（多态） => 才用继承
  运行期替换 => 虚接口；编译期固定且热路径 => 模板
```

---

## 8. 延伸阅读（本仓库内）

- `01-basics` —— 指针、引用、函数重载、`const`，本章的前置知识；
- `03-modern` —— 智能指针、`auto`、范围 `for`、移动语义的更多细节；
- `04-templates` —— 编译期多态、策略模板、`concept`，与本章第 2 节主线三对照阅读；
- `05-stl` —— 容器与算法：本章 `09` 示例里的 `std::sort` + 比较器在那里有完整讲解；
- `07-engineering` —— 头文件组织、Pimpl 与 ABI、代码评审规范。
