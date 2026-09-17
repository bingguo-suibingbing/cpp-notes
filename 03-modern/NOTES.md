# 第 3 章：现代 C++ 实战（C++11 / 14 / 17 / 20）

本章只讲**工程项目里天天会用到**的特性。所有示例都能编译、都能跑，并且尽量给出实测数字。

- 环境：Visual Studio 18 Community / MSVC 14.51，`/std:c++20`
- 编译参数：`/W4 /WX /permissive- /utf-8 /Zc:__cplusplus`
- 构建：在 `cpp-notes` 目录下执行 `.\build.ps1 -Chapter 03-modern -Run`

> **关于性能数字的免责声明**
> 本仓库默认以 Debug 配置编译（`/MDd`，无优化）。Debug 下每一次容器访问都带迭代器调试检查、
> 每一次堆分配都走调试堆，所以**绝对数字没有参考意义，只有趋势有意义**。
> 文中给出的数字来自本机的 Debug 实测，用来演示「方向」；Release（`/O2`）下趋势不变，
> 但差距通常缩小。任何结论都应该在你自己的目标平台 + Release 配置下复测。

---

## 1. 本章地图

| 文件 | 主题 | 一句话结论 |
| --- | --- | --- |
| `01_value_categories_move.cpp` | 值类别、`std::move`、转发引用、引用折叠、`std::forward` | `std::move` 不移动任何东西，它只是把表达式标记成「可以被移动」 |
| `02_smart_pointers.cpp` | `unique_ptr` / `shared_ptr` / `weak_ptr` / 自定义删除器 | 智能指针解决的是**所有权归属**问题，不是「指针换个写法」 |
| `03_lambda_and_functional.cpp` | lambda 捕获、泛型 lambda、`std::function` 开销、scope guard | 捕获列表就是闭包的成员列表，它直接决定了生命期语义 |
| `04_exceptions_and_errors.cpp` | 异常、`noexcept`、异常安全等级、`optional` / `error_code` / `expected` | 异常是给「异常情况」用的通道，不是控制流；正常失败用返回值表达 |
| `05_constexpr_and_compiletime.cpp` | `const` / `constexpr` / `consteval` / `constinit`、编译期算表、`if constexpr` | 能算在编译期的就算在编译期；能钉死的约定用 `static_assert` 钉死 |
| `06_structured_bindings_and_auto.cpp` | 结构化绑定、`auto` / `decltype`、CTAD、`string_view`、`optional` / `variant` / `any` | `auto` 会丢掉引用和顶层 `const`，所以遍历大对象必须写 `const auto&` |
| `07_algorithms_and_ranges.cpp` | `<algorithm>` 高频算法、`<ranges>`、`std::span` | 算法负责「怎么遍历」，lambda 负责「做什么」；`remove` / `unique` 必须配 `erase` |
| `08_concurrency_basics.cpp` | `thread` / `jthread`、互斥量、`atomic`、条件变量、`future` | 数据竞争是 UB，不是「偶尔算错」；先保证正确，再谈性能 |
| `09_cpp20_features_tour.cpp` | concepts / `<=>` / `format` / ranges / `jthread` / `bit_cast` / `source_location` | 每个 C++20 特性都对应一个真实的工程痛点，按收益排序采用 |

---

## 2. 现代 C++ 的核心心智模型：所有权（ownership）

现代 C++ 的一切都可以挂在**一个问题**下面：

> **这块资源（内存、文件、锁、连接、线程）由谁拥有？它什么时候被释放？**

只要这个问题有明确答案，代码就不会泄漏、不会 double free、不会悬垂。现代 C++ 的三个「新东西」
其实是同一件事的三个面：

| 工具 | 它回答的问题 | 关键机制 |
| --- | --- | --- |
| RAII | 「资源释放挂在哪里？」 | 析构函数在作用域退出时一定执行（包括异常路径） |
| 移动语义 | 「所有权怎么转交？」 | 右值引用 + 移动构造，把「拷贝一份」变成「接管一份」 |
| 智能指针 | 「所有权怎么表达？」，独占还是共享？ | `unique_ptr` = 独占，`shared_ptr` = 共享，`weak_ptr` = 只观察 |

把它们串成一句话：

> **RAII 是机制，移动语义是转交手段，智能指针是把所有权写进类型里。**

由此推出三条不需要记的「规则」，因为它们是推导结果而不是教条：

1. **不用裸 `new` / `delete`**：因为裸指针不带所有权信息，读代码的人无法判断该不该释放。
2. **默认 `unique_ptr`**：默认应该是「只有一个所有者」，需要共享才升级成 `shared_ptr`。
   倒过来（先 `shared_ptr` 再想办法）几乎必然产生循环引用和模糊的生命期。
3. **`std::move` 之后不要再使用源对象**：因为它表达的是「我把所有权交出去了」，
   之后的源对象处于「有效但未指定」状态。

把一个函数签名当成一份合同来读：

```cpp
void f(const std::string& s);          // 我只看，不持有，不延长你的生命期
void g(std::string s);                 // 我要一份副本（或吃掉你的右值）
void h(std::string& s);                // 我会改你
void k(std::unique_ptr<T> p);          // 我接管所有权
void m(const std::unique_ptr<T>& p);   // 我借用，不接管
std::unique_ptr<T> make();             // 我交出新对象的所有权
```

---

## 3. 重点难点详解

每个难点按 **错误直觉 → 正确模型 → 代码证据 → 工程建议** 展开。

### 3.1 `std::move` 不是移动

- **错误直觉**：`std::move(x)` 会把 `x` 的内容搬走。
- **正确模型**：`std::move` 是一次无条件的类型转换，等价于 `static_cast<T&&>(x)`。
  它**运行期什么都不做**，只是把表达式变成「将亡值（xvalue）」，让编译器在重载解析时
  优先选择移动构造 / 移动赋值。真正「搬数据」的是移动构造函数。
- **代码证据**（`01_value_categories_move.cpp` 第 1、2 节）：

  ```text
  左值 a 的地址      = 000000xxxx
  右值引用 r 的地址  = 000000xxxx        <- 地址完全相同
  (a) 绑定引用：Tracker&& r = std::move(t);   <- 没有任何 [move] 输出
  (b) 真正构造新对象：Tracker u(std::move(t)); <- 这时才有 [move]
  ```

  同一个文件里还有 `const Tracker c; Tracker m(std::move(c));` 的实验，输出是 `[copy]`
  而不是 `[move]`：因为 `const T&&` 匹配不上 `T&&` 移动构造，只能退化成拷贝构造。
- **工程建议**：
  - 不要对 `const` 对象 `std::move`（它会被静默拷贝，还会误导读者）。
  - 不要 `return std::move(局部变量)`：这会阻止 NRVO，白白多一次移动。
    示例里的日志是最直接的证据——正常返回没有任何 `[copy]` / `[move]`，
    而 `return std::move(...)` 会多出一行 `[move] 移动构造`；
    Debug 计时因调试堆抖动较大（实测 706 ms vs 777 ms），看日志比看数字更可靠。
  - 返回**函数参数**时 `std::move` 是合理的（参数不参与 NRVO）。

### 3.2 万能引用与引用折叠

- **错误直觉**：「`T&&` 就是右值引用」。
- **正确模型**：只有在**模板参数推导**语境下、形如 `T&&` 且 `T` 确实被推导时，
  `T&&` 才是转发引用（万能引用）。它同时能绑定左值和右值，靠的是**引用折叠**：

  ```text
  T&  &   -> T&        T&  &&  -> T&
  T&& &   -> T&        T&& &&  -> T&&
  记忆：只要有一个 &，结果就是 &；两个都是 && 才是 &&
  ```

  典型反例：`void f(std::vector<int>&& v)` **不是**万能引用，因为 `std::vector<int>`
  不是被推导出来的模板参数，它只能绑定右值。

- **代码证据**：示例里用 `if constexpr (std::is_lvalue_reference_v<T>)` 把 `T` 的推导结果
  直接打出来——传左值时 `T` 是引用类型，传右值时 `T` 是非引用类型。
- **工程建议**：
  - 转发引用只和 `std::forward<T>` 配对使用；写成 `std::move` 会把左值也搬走（严重 bug）。
  - 只有需要「原样转发值类别」时才写模板 `T&&`；普通函数参数优先用 `const T&` 或值传递。
  - 转发引用会吞掉一切（包括 `const T&` 的重载），所以别让一个类同时有 `T&&` 模板构造和拷贝构造，
    否则拷贝会用模板把非 `const` 左值抢走。

### 3.3 `auto` 会丢引用和顶层 `const`

- **错误直觉**：`auto x = container[i];` 只是「省得写类型」。
- **正确模型**：`auto` 的类型推导规则与模板参数推导一致，会**剥掉引用、剥掉顶层 `const`**。
  想保留就得显式写 `auto&` / `const auto&` / `auto&&`。这一条在遍历容器时是**性能问题**，
  不只是风格问题。
- **代码证据**（`06_structured_bindings_and_auto.cpp`）：

  ```cpp
  static_assert(std::is_same_v<decltype(a), int>);         // const int ci; auto a = ci;
  static_assert(std::is_same_v<decltype(c), int>);         // int& rx; auto c = rx;
  static_assert(std::is_same_v<decltype(f), const int&>);  // const auto& f = crx;
  ```

  实测（400 条含 512 个 `double` 的记录，400 轮）：

  ```text
  for (auto record : records)        : 109.98 ms
  for (const auto& record : records) :   0.22 ms     <- 差约 500 倍
  ```

  Debug 下这个倍数被调试堆放大；Release 下会缩小到十倍量级，但**「拷贝 vs 引用」的方向不变**。
- **工程建议**：
  - 遍历一律 `const auto&`；要改就 `auto&`；只有确实需要独立副本时才用 `auto`。
  - 结构化绑定同理：`for (auto [k, v] : map)` 会拷贝每个 `value`，
    应该写 `for (const auto& [k, v] : map)`。
  - `auto` 的另一个好处是「类型改名时不漏改」，但**不要**用 `auto` 隐藏真正重要的类型
    （比如把 `std::int64_t` 写成 `auto` 会丢掉「必须 64 位」这个信息）。

### 3.4 `shared_ptr` 循环引用 = 内存泄漏

- **错误直觉**：都用 `shared_ptr` 最安全，谁需要就多来一份。
- **正确模型**：`shared_ptr` 的强引用计数归零才释放对象。只要两个对象互相持有 `shared_ptr`，
  计数永远至少是 1，对象永远不释放——**这是确定性的内存泄漏，不是「可能泄漏」**。
- **代码证据**（`02_smart_pointers.cpp` 第 4 节）：

  ```text
  (a) a->next = b; b->next = a;   a.use_count = 2, b.use_count = 2
      作用域已退出，但没有 [dtor] 输出 -> 两个对象泄漏了
  (b) a->next = b; b->prev = a;   a.use_count = 1, b.use_count = 2
      [dtor] NodeFixed A 被销毁
      [dtor] NodeFixed B 被销毁
  ```

- **工程建议**：
  - 任何「双向 / 回指」关系，**必须有一条边是 `weak_ptr`**。
  - 观察者模式、缓存、父子关系中的「子 → 父」边，全部用 `weak_ptr`。
  - 需要跨生命周期访问时用 `weak_ptr::lock()` 提升成 `shared_ptr` 再操作，
    不要先 `expired()` 判断再解引用（中间那一瞬间对方可能刚好死掉，是典型的 TOCTOU 竞争）。
  - `use_count()` 只用于日志 / 断言 / 调试，业务逻辑依赖它就会在优化和并发下出错。

### 3.5 lambda 捕获悬垂

- **错误直觉**：`[&]` 最省事，反正就是「引用外面的变量」。
- **正确模型**：闭包对象会**活得比当前作用域久**（被返回、被存进容器、被投递到异步任务 / 线程）时，
  按引用捕获就变成悬垂引用。这类 bug 往往在测试环境不复现，在生产环境偶发崩溃。
- **代码证据**（`03_lambda_and_functional.cpp` 第 2 节）：

  ```cpp
  std::function<int()> make_dangling_lambda() {
      int local = 42;
      return [&local]() { return local; };   // 悬垂
  }
  ```

  示例里调用它「读到了 42」，这正是最危险的地方：**它可能看起来是对的**。
- **工程建议**：
  - 闭包要离开当前作用域 → **只按值捕获**，或 C++14 初始化捕获 `[data = std::move(buffer)]`。
  - 需要按引用捕获但闭包会活很久 → 生命期问题不应该用 lambda 解决，应该用 `shared_ptr` 明确共享所有权。
  - 成员函数里 lambda 要用到成员时：闭包可能比对象活得久就用 `[*this]`（C++17）或拷贝所需成员，
    否则才用 `[this]`。
  - 不要把 `[=]` 当默认写法：在成员函数里它还会隐式捕获 `this`（C++20 已弃用这种隐式捕获），
    显式列出捕获项是自文档化。

### 3.6 异常 vs 错误码

- **错误直觉**：「异常会拖慢正常路径」，所以全部用返回码。
- **正确模型**：现代实现采用「零开销异常」——只要不抛，正常路径几乎不付出代价（异常表是静态数据）。
  真正贵的是 `throw` 本身：构造异常对象、查表、栈展开。
- **代码证据**（`04_exceptions_and_errors.cpp` 第 8 节，Debug 实测 2 万次解析）：

  ```text
  A. 永不失败（错误码）: 0.46 ms
  A. 永不失败（异常）  : 0.39 ms     <- 正常路径没有变慢
  B. 一半失败（错误码）: 5.70 ms
  B. 一半失败（异常）  : 34.38 ms    <- 抛异常本身很贵
  ```

- **工程建议**：
  - 可预期的业务失败（解析、查找、校验、IO 返回「没找到」）→ `optional` / `expected` / `error_code`。
  - 无法用返回值表达的失败（构造函数、运算符重载、`begin()` 内部）→ 异常。
  - 跨模块 / 跨 ABI / 跨语言（C 接口、DLL 边界、插件系统）→ 不要抛异常，用 `error_code` / `expected`。
  - 实时性要求高的路径（音频回调、飞控、内核、游戏主循环）→ 关异常或用 `noexcept` 路径。
  - 永远 `catch (const std::exception&)`，不要按值（对象切片会让派生类信息彻底消失，
    示例第 2 节用 `Base` / `Derived` 直接演示了这一点）。

### 3.7 `string_view` 的生命周期

- **错误直觉**：`string_view` 是「更好的 `const std::string&`」，可以随便存、随便返回。
- **正确模型**：`string_view` 是**非拥有视图**（指针 + 长度），它不延长任何数据的生命期，
  也**不保证以 `\0` 结尾**。它的正确用法边界很清晰：作为**函数参数**，
  且被引用数据在调用期间一定活着。
- **代码证据**（`06_structured_bindings_and_auto.cpp` 第 6 节）：

  ```cpp
  std::string_view bad_make_view() {
      std::string local = "临时字符串内容";
      return std::string_view(local);   // 返回后 local 已销毁
  }
  ```

- **工程建议**：
  - 参数的默认选择：`std::string_view`（只读、可接字面量、零拷贝）——但**不要存成成员**。
  - 需要长期持有 → 存 `std::string`（明确拥有）。
  - 传给 C API 前必须转成 `std::string`（因为不保证 `\0` 结尾）。
  - 返回 `string_view` 只允许「返回的是参数里的视图」或「返回静态存储期的字符串」。
  - 同类陷阱：`std::span`、`std::ranges` 视图、`std::filesystem::path::c_str()`。

### 3.8 数据竞争 = 未定义行为（不是「偶尔算错」）

- **错误直觉**：`++counter` 在多线程下「最多就是少加几次」，能接受。
- **错误直觉之二**：加个 `volatile` 就能解决多线程同步。
- **正确模型**：C++ 标准规定数据竞争是 **UB**。编译器可以假设不存在数据竞争，于是它可以
  把变量长期放在寄存器、把循环合并、重排指令顺序。结果是「大多数时候结果偏小，偶尔程序行为完全异常」。
  `volatile` **不提供任何原子性或内存序保证**，它不是同步原语。
- **代码证据**（`08_concurrency_basics.cpp` 第 4 / 7 节，4 线程 × 10 万次自增）：

  ```text
  无保护 int   : 1.81 ms（结果 229724 / 期望 400000，错误 -> 数据竞争）
  atomic       : 22.05 ms（结果 400000，正确）
  mutex 保护   : 48.91 ms（结果 400000，正确但最慢）
  ```

  无保护版本「最快」，但那是因为它做了错事；**快的错误结果没有意义**。
- **工程建议**：
  - 单个变量用 `std::atomic`；多个变量需要保持一致（不变量）用互斥量。
  - 内存序先用默认 `seq_cst`，确认瓶颈后再降级成 `acquire` / `release`；计数场景可以 `relaxed`。
  - 检测手段：`-fsanitize=thread`（clang / gcc）、MSVC `/analyze`、代码评审 + 明确的所有权约定。
    压力测试只能提高发现概率，**不能证明没有竞争**。
  - 并发三条建议：**能不用共享状态就不用**；必须共享就**优先消息传递（队列）、其次锁**；
    **锁的粒度要小**（临界区里不做 IO、不 sleep、不调用未知回调）。

---

## 4. 实用开发习惯与技巧

按「日常写代码时真正会用到」排序。

| 习惯 | 为什么 | 反例 |
| --- | --- | --- |
| 默认 `unique_ptr`，需要共享才 `shared_ptr` | 独占语义能自动排除循环引用和模糊生命期 | 一上手就 `shared_ptr`，最后靠 `weak_ptr` 打补丁 |
| 遍历用 `const auto&` | `auto` 会静默拷贝，大对象代价极高 | `for (auto row : rows)` 每行拷一份 |
| 结构化绑定也要带引用 | `auto [k, v]` 同样拷贝 `value` | `for (auto [k, v] : map)` |
| 接口只读字符串用 `std::string_view` | 零拷贝、可接字面量、调用方不必构造 `std::string` | `const std::string&` 参数让字面量产生临时对象 |
| 接口连续区间用 `std::span` | 一个参数表达「指针 + 长度」，`vector` / 数组 / 子区间都能传 | `void f(T* p, size_t n)` |
| 想清楚「可能没有」时用 `std::optional` | 消灭 `-1` / `""` / `nullptr` 这类魔法值 | 返回 `-1` 表示失败，调用方忘了检查 |
| 失败有原因时用 `expected` / `error_code` | `optional` 丢掉了失败原因 | 只返回 `bool`，调用方无从诊断 |
| 能 `constexpr` 就 `constexpr` | 编译期算完，运行期零成本，还能被 `static_assert` 验证 | 运行期初始化一个大查找表 |
| 热路径先 `reserve` | 减少扩容与元素搬迁次数 | 循环 `push_back` 十万次不 `reserve` |
| 构造容器元素用 `emplace_back` / `emplace` | 少一次临时对象（但**不等于**一定更快，见下） | 明明只有移动构造却写 `push_back(T(...))` |
| 不返回 `std::move(局部变量)` | 阻止 NRVO，多一次移动 | 以为这样「更快」 |
| 自定义类型的移动操作标 `noexcept` | 否则 `vector` 扩容会退化成拷贝 | `MyType(MyType&&)` 不带 `noexcept` |
| 析构函数不抛异常 | 栈展开中再抛会直接 `terminate` | 析构里做可能失败的 IO 并抛异常 |
| `[[nodiscard]]` 标「不看就有坑」的返回值 | 让编译器帮你抓漏检错误码 | `lock()` / 工厂函数返回值被静默丢弃 |
| 能用 `variant` 表达的用 `variant`，不用 `any` | 编译期类型检查 + `visit` 穷尽性 + 无堆分配 | 用 `any` 到处 `any_cast`，错误推迟到运行期 |

**参数传递方式的选择**（实测数据见 `06_structured_bindings_and_auto.cpp` 第 6b 节，
1 万个字符的字符串 × 1 万次调用，被调函数只取 `size()`；数字为多次运行中的一次）：

```text
void f(std::string s)        :  8.6 ~ 11.5 ms   <- 每次深拷贝一份字符数据
void f(const std::string& s) :  2.7 ~  2.8 ms   <- 只传引用（这一档被 to_string 的堆分配拉高）
void f(std::string_view s)   :  0.05 ~ 0.06 ms  <- 指针 + 长度
传值 / 传 const 引用         :  约 3 ~ 4 倍
```

注意这个倍数的含义：被调函数越轻，传参成本占比越高，差距越明显；
如果函数体要扫描全部字符（O(n) 工作），差距会缩到 1.1 倍左右。所以正确表述不是
「传值一定慢」，而是「**只读参数不该产生拷贝**」——需要保存副本时，按值 + `std::move`
反而是最优解。

**`emplace_back` 的真相**：`emplace_back` 是把参数直接转发给元素的构造函数，
省掉「先构造临时对象再移动」这一步。但如果类型有 `noexcept` 移动构造，
`push_back(T(...))` 的额外成本只是一次移动，通常可以忽略；而 `emplace_back`
在遇到 `explicit` 构造函数或 `initializer_list` 重载时反而会带来意外（比如
`v.emplace_back({1, 2})` 编译失败）。**默认用 `push_back`，需要就地构造（如
`emplace_back(key, value)` 构造 `pair`）时用 `emplace_back`。**

**禁止的写法**（示例里全部实测或演示过）：

- `return std::move(local);` → 阻止 NRVO，多一次移动（`01` 第 7.3 节日志为证）
- `auto x = big_container[i];` → 静默拷贝（`06` 第 10 节实测约 500 倍）
- 只读参数写成 `void f(std::string s)` → 每次调用深拷贝（`06` 第 6b 节实测约 3 ~ 4 倍）
- `for (auto [k, v] : map)` → 每个 `value` 都拷贝（`06` 第 2 节用拷贝计数证明）
- `[&]` 捕获后返回 / 存起来 → 悬垂（`03` 第 2 节）
- `std::shared_ptr<T>(new T)` 当习惯用 → 两次分配（`02` 第 8 节实测约 1.5 倍）
- 对同一个裸指针构造两个 `shared_ptr` → double free（`02` 第 7 节）
- `catch (Base b)` → 对象切片（`04` 第 2 节）
- `container.erase(std::remove_if(...))` 写成只 `remove_if` → 元素没真正删除（`07` 第 4 节）
- `detach()` 之后 `main` 直接退出 → 结果不确定 / 崩溃（`08` 第 1 节）
- `i++` 保护多线程共享计数 → 数据竞争 UB（`08` 第 4 / 7 节实测结果错误）
- 用异常表达「可预期失败」→ 抛异常本身很贵（`04` 第 8 节实测约 6 倍）

**跨编译器 / 跨平台可移植性注意**：

- MSVC 的 `__cplusplus` 默认恒为 `199711L`，本仓库的 `build.ps1` 已经加了 `/Zc:__cplusplus`，
  示例里的 `__cplusplus == 202002` 才是真实值。
- `__cpp_lib_*` 特性宏要用 `#ifdef` 检测，不要假设「C++20 就意味着所有库特性都有」。
  本环境（MSVC 14.51 + `/std:c++20`）实测：`std::format`、`ranges`、`jthread`、
  `bit_cast`、`source_location` 可用；**`std::expected`（C++23）不可用**，
  所以 `04_exceptions_and_errors.cpp` 用了自写的最小 `Expected` 做演示。
- `[[likely]]` / `[[unlikely]]`、`std::format` 的完整格式串支持在不同编译器上成熟度不同，
  发布前要在所有目标编译器上过一遍。

---

## 5. 现代特性采用建议

按「分层 + 优先级」给出。判断标准是：**它解决的痛点有多常见，以及它会不会让代码更难被同事维护。**

### C++11

| 特性 | 建议 | 理由 |
| --- | --- | --- |
| `auto` / `nullptr` / 范围 for | 必须掌握 | 日常基础，`nullptr` 消除 `NULL` 的重载歧义 |
| 移动语义 + `std::move` | 必须掌握 | 现代 C++ 的性能基石 |
| `unique_ptr` / `shared_ptr` | 必须掌握 | 所有权表达的唯一主流方式 |
| lambda | 必须掌握 | 算法、回调、RAII 封装的通用工具 |
| `constexpr` | 必须掌握 | 编译期计算的第一步 |
| `override` / `final` / `= default` / `= delete` | 必须掌握 | 显式表达意图，防止静默错误 |
| `noexcept` | 推荐 | 影响容器优化路径与异常安全推理 |
| `std::thread` / `mutex` / `atomic` | 推荐 | 并发基础，但要先想清楚共享状态 |
| `std::function` | 了解即可（谨慎用） | 有类型擦除与分配成本，热路径避免 |
| 变参模板 | 推荐 | 写通用库的必备，业务代码用得少 |

### C++14

| 特性 | 建议 | 理由 |
| --- | --- | --- |
| 泛型 lambda（`auto` 参数） | 推荐 | 少写很多模板胶水代码 |
| 初始化捕获 `[x = std::move(y)]` | 必须掌握 | 把 move-only 对象安全搬进闭包的唯一方式 |
| `constexpr` 函数放宽（循环 / 分支 / 局部变量） | 必须掌握 | 让编译期计算真正可用 |
| 返回类型推导（`auto` 返回） | 推荐 | 方便，但接口边界建议显式写清类型 |
| `std::make_unique` | 必须掌握 | 补齐了 C++11 的缺口 |

### C++17

| 特性 | 建议 | 理由 |
| --- | --- | --- |
| 结构化绑定 | 必须掌握 | 配合 `const auto&` 是遍历 `map` 的标准写法 |
| `if constexpr` | 必须掌握 | 写类型相关分支的核心工具 |
| `std::string_view` | 必须掌握 | 接口设计的标准选择（注意生命期） |
| `std::optional` / `std::variant` | 必须掌握 | 取代魔法值与 `union` + tag |
| `std::any` | 了解即可 | 类型信息丢失，能用 `variant` 就用 `variant` |
| `std::scoped_lock` | 必须掌握 | 多锁场景防死锁的最省心写法 |
| `std::filesystem` | 推荐 | 跨平台文件操作 |
| `[[nodiscard]]` / `[[maybe_unused]]` / `[[fallthrough]]` | 推荐 | 让编译器帮你抓错 |
| `[*this]` 捕获 | 推荐 | 解决「闭包比对象活得久」的悬垂问题 |
| 类模板实参推导（CTAD） | 推荐 | 少写模板参数，但别依赖它表达语义 |
| 内联变量 / `inline static` | 推荐 | 头文件里定义常量的正确方式 |

### C++20

| 特性 | 建议 | 理由 |
| --- | --- | --- |
| `concepts` | 必须掌握 | 把模板报错变成一句人话，收益最大 |
| `std::format` | 必须掌握 | 同时解决 `printf` 的类型不安全与 `iostream` 的啰嗦 |
| `<=>` 三路比较 | 推荐 | 比较运算符只维护一处 |
| `ranges` / `views` | 推荐（先小范围用） | 可读性提升明显；Debug 下开销大，性能敏感路径要实测 |
| `std::span` | 必须掌握 | 接口传连续区间的现代答案 |
| `std::jthread` + `stop_token` | 推荐 | 自动 `join`，取消协议类型安全 |
| `consteval` / `constinit` | 推荐 | 强制编译期求值 / 消除静态初始化顺序问题 |
| 指定初始化 | 推荐 | 配置结构体可读性大幅提升 |
| `using enum` | 推荐 | `switch` 里的样板代码减半 |
| `std::bit_cast` | 推荐 | 类型双关的安全写法，`constexpr` 友好 |
| `std::source_location` | 推荐 | 日志不用宏就能拿到调用点 |
| `[[likely]]` / `[[unlikely]]` | 了解即可（谨慎用） | 没有 profile 数据就是噪声，收益通常 < 1% |
| 协程 / 模块 / `std::expected` | 了解即可 | 编译器与生态成熟度还在演进，本环境 `expected` 不可用 |

---

## 6. 自测题（8 道，含答案）

### 题 1

下面的输出是什么？为什么？

```cpp
int a = 10;
int&& r = std::move(a);
std::cout << (&a == &r) << " " << (r = 20, a);
```

**答案**：输出 `1 20`。

`std::move(a)` 只是 `static_cast<int&&>(a)`，没有任何运行期动作；`r` 是绑定到 `a` 的引用，
所以 `&a == &r` 为真，改 `r` 就是改 `a`。**要点**：`std::move` 是编译期的类型转换，
它不会创建新对象，也不会搬数据；「移动」发生在有构造 / 赋值调用移动构造函数的时候。

### 题 2

```cpp
template <typename T> void f(T&& x);       // (1)
void g(std::vector<int>&& v);              // (2)
```

哪个是转发引用？为什么？

**答案**：只有 (1) 是转发引用。(2) 中的类型 `std::vector<int>` 是确定的、不需要推导，
所以 `std::vector<int>&&` 就是普通的右值引用，只能绑定右值。

(1) 中 `T` 由实参推导：传左值时 `T` 推导为 `U&`，`T&&` 经引用折叠变回 `U&`；
传右值时 `T` 推导为 `U`，`T&&` 就是右值引用。**要点**：模板 + `T&&` + `T` 确实被推导，
三者缺一不可；`const T&&`、`std::vector<T>&&`、`T&&`（`T` 已在别处确定）都不是转发引用。

### 题 3

```cpp
std::vector<std::string> names = {"a", "bb", "ccc"};
auto s = names[0];
auto& t = names[1];
const auto& u = names[2];
```

三个变量的类型分别是什么？哪个会拷贝？

**答案**：`s` 是 `std::string`（值，发生一次拷贝）；`t` 是 `std::string&`（引用，无拷贝）；
`u` 是 `const std::string&`（const 引用，无拷贝）。

`auto` 的推导会剥掉引用和顶层 `const`，所以 `auto s = names[0]` 得到的是一个**独立副本**。
**要点**：遍历或访问大对象时，`auto` 是性能陷阱；用 `const auto&` 才是默认正确写法。
（`decltype(names[0])` 则是 `std::string&`，因为 `decltype` 看的是表达式类型而不是推导规则。）

### 题 4

用 `shared_ptr` 实现父子双向关系，为什么程序退出时父子对象都没被析构？怎么改？

**答案**：父子各自持有对方的 `shared_ptr`，形成引用环。父子外部的 `shared_ptr` 销毁后，
两者的强引用计数都至少是 1（对方持有），永远不归零，`delete` 永不发生——这是确定性的内存泄漏。

改法：把**反向边**（子 → 父）改成 `std::weak_ptr`，需要访问时用 `lock()` 提升为 `shared_ptr`：

```cpp
struct Child {
    std::shared_ptr<Parent> owner;    // 或 weak_ptr
};
struct Parent {
    std::vector<std::shared_ptr<Child>> children;   // 强引用：父拥有子
};
```

**要点**：在设计阶段就确定「哪条边是拥有、哪条边是观察」；`weak_ptr` 只增加 weak count，
不影响 `use_count`，所以不会阻止对象释放。

### 题 5

为什么性能敏感的代码里不应该用 `std::function` 传 lambda？

**答案**：`std::function` 是**类型擦除**容器，它有两个成本：

1. 构造时要判断闭包大小是否超过内部小对象缓冲，超过就**堆分配**；
2. 调用时经过一次间接跳转（虚函数 / 函数指针表），**通常无法内联**。

实测（Debug，2000 次调用 × 3000 轮）：`std::function` 约 `41 ms`，函数指针约 `23 ms`，
模板 / `auto` 约 `23 ms`；闭包从捕获 1 个 `int` 变成捕获 8 个 `int` 时，构造 / 销毁成本
从 `39 ms` 涨到 `45 ms`（额外堆分配）。

正确做法：接口用模板参数或 `auto`（编译期单态化、可内联）；确实需要「运行期多态的可调用对象」
（存进容器、作为回调注册）时才用 `std::function`，并尽量让闭包保持小。
**要点**：`std::function` 不是「更通用的函数指针」，它是「带分配的类型擦除容器」。

### 题 6

```cpp
void process(std::string s);        // (A)
void process(const std::string& s); // (B)
```

调用 `process("hello")` 时哪个更好？如果函数内部要保存一份副本呢？

**答案**：只读且不保存副本时，(B) 更好——不会构造 `std::string` 临时对象（准确地说，
字面量在 (A) 会构造一个临时 `std::string`，有一次堆分配可能）。
如果函数**需要保存副本**（比如存成成员），那么按值 (A) + 内部 `std::move` 通常更好：

```cpp
class Sink {
    std::string data_;
public:
    explicit Sink(std::string data) : data_(std::move(data)) {}   // 传左值拷一次，传右值移一次
};
```

这样传左值 1 次拷贝、传右值 1 次移动，总次数最少；而 `const std::string&` 版本内部还要显式拷贝一次。
**要点**：参数传递方式应该由「函数是否需要一份自己的副本」决定，而不是「哪种看起来更快」。
另外注意：如果参数类型是 `std::string_view`，那么「保存副本」是**禁止**的（视图不拥有数据）。

### 题 7

```cpp
std::string_view get_name() {
    std::string name = "temporary";
    return name;
}
```

这段代码有什么问题？什么情况下返回 `string_view` 才是安全的？

**答案**：`name` 是局部对象，函数返回时被销毁，返回的 `string_view` 立刻悬垂；使用它就是 UB。
（很多实现上它「看起来能读到内容」，这正是它危险的原因。）

安全的返回 `string_view` 只有两种情况：

1. 返回的视图指向**参数**里的数据，且调用者保证该数据在视图使用期间存活，例如
   `std::string_view trim(std::string_view text);`；
2. 返回的视图指向**静态存储期**的字符串，例如字符串字面量或 `static const std::string`。

**要点**：`string_view` 是非拥有视图，「谁拥有数据、活多久」必须由调用约定写清楚；
需要返回「新字符串」时返回 `std::string`。

### 题 8

`noexcept` 只是文档性质的标注吗？它会影响性能吗？析构函数为什么不应该是 `noexcept(false)`？

**答案**：不只是文档。`noexcept` 是**类型系统的一部分**：

1. **影响容器的选择**：`std::vector` 扩容时需要把元素搬到新内存。只有当移动构造是 `noexcept`
   时它才敢用移动；否则为了满足强异常保证，它必须退化成拷贝。所以自定义类型不标 `noexcept`
   会**静默变慢**。（示例 `04` 里用 `MoveNoexcept` / `MoveThrowing` 与 `static_assert` 演示了这一点。）
2. **影响优化与代码生成**：调用方知道不会抛异常，就不需要生成展开清理代码，甚至可以省掉栈帧信息。

析构函数默认就是 `noexcept`，因为**栈展开过程中会调用析构函数**：如果在展开时析构又抛异常，
就会同时存在两个活跃异常，C++ 只能直接 `std::terminate`——连 `catch` 的机会都没有。
所以析构函数里要做可能失败的操作时，必须自己吞掉异常（记录日志）或提供显式的 `close()` / `shutdown()`
让调用者在正常路径上处理错误。

**要点**：`noexcept` 是承诺，承诺错了代价是进程终止；移动构造、移动赋值、`swap`、析构、
简单 getter 都应该是 `noexcept`。

---

## 7. 运行与验证

```powershell
# 在 cpp-notes 目录下
.\build.ps1 -Chapter 03-modern -WX        # 编译（警告即错误）
.\build.ps1 -Chapter 03-modern -Run       # 编译并逐个运行
.\build.ps1 -Chapter 03-modern -Config Release -Run   # Release 下重测性能数字
```

- 每个 `.cpp` 都是独立可执行文件（各自带 `int main()`），产物在 `build\03-modern\`。
- 性能实测的代码都在 `main()` 里，带 `volatile` 消费结果，防止被优化掉。
- 多线程示例全部 `join`（或使用 `jthread` 自动 `join`），可稳定重复运行。
