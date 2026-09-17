# Boost vs STL 优缺点对照演示（Visual Studio）

一个可以直接在 VS 里 `Ctrl+F5` 跑起来的菜单式演示工程，用**同一份代码、同一个编译器**，
把 Boost 和 STL 在 11 个主题上并排对比，逐条给出优点、缺点和"实际项目该选哪个"的结论。

- 环境：Visual Studio 18 (MSVC v145)，C++20，Boost 1.92（header-only 部分）
- 建议配置：**Release | x64**（性能小节在 Debug 下没有参考价值）
- 运行方式：`Ctrl+F5`（控制台窗口；不要用 F5 的调试宿主，ANSI 颜色和中文输出会有问题）

---

## 一、怎么跑

1. 用 VS 打开 `Project6.slnx`（或 `Project6.vcxproj`）。
2. 配置选 **Release** + **x64**。
3. `Ctrl+F5` 运行。
4. 菜单输入 `1`~`11` 单看一个主题，输入 `0` 或直接回车跑全部，`q` 退出。

Boost 路径配置在 `boost.props` 里：

```xml
<BoostRoot Condition="'$(BoostRoot)'==''">D:\boost\boost-1.92.0-b2-nodocs\boost-1.92.0</BoostRoot>
```

换机器只要改这一行（或在 VS 里改用户宏 `BoostRoot`）。本演示只用到 header-only 组件，
所以**不需要**事先 `b2` 编译 Boost 的 lib，也不需要配置库目录。

---

## 二、文件结构

| 文件 | 对应主题 | 结论倾向 |
|---|---|---|
| `src/m1_string.cpp` | 字符串算法 | **Boost 明显更强** |
| `src/m2_format.cpp` | 格式化 / 字符串↔数字 | **STL 反超** |
| `src/m3_smartptr.cpp` | 智能指针 | 通用打平，Boost 有独门 |
| `src/m4_container.cpp` | 容器 | **Boost 不可替代** |
| `src/m5_optional_variant.cpp` | optional / variant / any | 各有千秋 |
| `src/m6_json.cpp` | JSON / 配置解析 | **Boost 补空白** |
| `src/m7_regex_tokenizer.cpp` | 正则 / 分词 | **Boost 更快更全** |
| `src/m8_heap_flatmap.cpp` | 堆 / 容器补充 | **Boost 补空白** |
| `src/m9_error_scope.cpp` | 错误处理 / 作用域守卫 | 各有千秋 |
| `src/m10_datetime.cpp` | 日期时间 | 各有千秋 |
| `src/m11_summary.cpp` | 总结 + 决策指南 | — |
| `demo_common.h` / `demo_api.h` | 输出排版 / 计时工具 / 模块声明 | — |

另外还有：

| 路径 | 说明 |
|---|---|
| `boost.props` | Boost 包含目录的属性表（换机器只改这里的 `BoostRoot`） |
| `bench/compile_bench_std.cpp` | 编译耗时基准 A：只用 STL 实现 11 个功能 |
| `bench/compile_bench_boost.cpp` | 编译耗时基准 B：同一批功能改用 Boost（逐行对应） |
| `demo_output.txt` | 一次完整运行的输出记录（UTF-8，可直接查看结果） |

---

## 二点五、实测数据（本机 Release x64 现场跑出来的）

| 对比项 | 结果 | 结论 |
|---|---|---|
| 同一批功能的编译耗时（各 3 次取平均） | STL 2641 ms vs **Boost 4768 ms** | Boost 慢 **1.81 倍** |
| `std::format` vs `boost::format`（20 万次） | — | Boost 慢 **4.2 倍** |
| `from_chars` vs `lexical_cast`（20 万次往返） | — | STL 快 **3.6 倍** |
| `std::shared_ptr` vs `local_shared_ptr`（200 万次拷贝） | — | Boost 快 **16 倍**（单线程） |
| `boost::regex` vs `std::regex`（4 万行匹配） | — | Boost 快 **2.1 倍** |
| `flat_map` vs `std::map`（小表 2000 项） | — | flat_map 快 **1.0~4 倍** |
| 抛异常 vs 返回 `error_code`（20 万次失败） | — | 异常慢约 **430 倍**（每次仅 1~2 μs） |
| 本工程 12 个 cpp 完整重编译 | 约 38 秒（`/m:1`） | 主要时间花在 Boost 头文件 |

> 这些数字都是演示运行时**现场测出来的**，会随机器和负载波动；
> 趋势稳定，具体倍数不必当真。

---

## 三、结论速查表

| 主题 | Boost 方案 | STL 方案 | 结论 |
|---|---|---|---|
| 字符串算法 | `algorithm::string` | 无，需手写 | Boost 明显更强 |
| 字符串格式化 | `boost::format` | `std::format` (C++20) | **STL 更好**（编译期检查） |
| 字符串↔数字 | `lexical_cast` | `from/to_chars` (C++17) | STL 更快，Boost 更泛型 |
| 智能指针（通用） | `shared_ptr` 等 | 同名 `std::` 版本 | 打平，用标准的 |
| `intrusive_ptr` | 有 | 无 | **Boost 独有** |
| `local_shared_ptr` | 有（非原子计数） | 无 | **Boost 独有** |
| 多索引容器 | `multi_index_container` | 无 | **Boost 独有，无替代** |
| 环形缓冲 | `circular_buffer` | 无 | **Boost 独有** |
| 双向映射 | `bimap` | 无 | **Boost 独有** |
| 连续内存 map | `container::flat_map` | 无（C++23 才有） | Boost 先行 |
| 小对象优化 | `container::small_vector` | 无 | **Boost 独有** |
| 可合并堆 | `heap::*_heap` | 只有 `priority_queue` | **Boost 独有** |
| `optional` | `boost::optional` | `std::optional` (C++17) | 打平，用标准的 |
| `variant` | `variant2`（never-valueless） | `std::variant` | 各有千秋 |
| `any` | `boost::any` | `std::any` (C++17) | 打平，用标准的 |
| JSON / XML / INI | `property_tree` / `json` | **无** | **Boost 独有** |
| 正则 | `boost::regex` | `std::regex` (C++11) | Boost 更快更全 |
| 分词 | `tokenizer` | 无 | **Boost 独有** |
| 文件系统 | `filesystem` | `std::filesystem` | 打平，用标准的 |
| 日期时间 | `date_time` | `chrono` (C++20) | 各有千秋 |
| 作用域守卫 | `BOOST_SCOPE_EXIT` | 无 | **Boost 独有** |
| 错误码 | `system::error_code` | `std::error_code` | 打平，用标准的 |
| `result<T>` | `system::result` | `std::expected` (C++23) | Boost 先行 |

---

## 四、Boost 的优点

1. **补标准库的空白**：`multi_index_container`、`circular_buffer`、`bimap`、可合并堆、
   JSON 解析、`scope_exit`、`tokenizer` —— 这些 STL 到今天都没有，自己写基本都会踩坑。
2. **一个依赖换一大堆能力**：引一次 Boost 就有 160+ 个库，不必为每个需求单独挑第三方。
3. **标准的试验田**：`shared_ptr` / `regex` / `filesystem` / `optional` / `variant` / `any` /
   `thread` / `chrono` 全是先在 Boost 里跑通再进标准的 —— 用 Boost 常常等于提前用上
   两三年后的标准库。
4. **跨平台行为一致**：同一份代码在 MSVC / GCC / Clang 上行为一致，
   不像各标准库实现的边角行为有差异（`std::regex` 尤其明显）。
5. **成熟度**：很多组件在金融 / 游戏 / 通信里跑了 20 年，边界条件被踩遍了。

## 五、Boost 的缺点

1. **编译时间爆炸**：纯头文件模板展开极重，`<boost/algorithm/string.hpp>` 一个头
   就比 `<string>` 重一个数量级（实测数据见演示的 M11 主题）。
2. **依赖与体积**：完整源码几百 MB；虽然大多 header-only，但 `regex` / `filesystem` /
   `thread` / `iostreams` 等仍是编译库，需要构建 + 链接。
3. **学习曲线与文档**：模板报错动辄几十屏，`multi_index_container<...>` 的模板参数
   就能劝退新手。
4. **历史包袱**：`format`、`lexical_cast`、`shared_ptr`、`optional`、`variant`(非 2)、
   `any`、`chrono`、`filesystem` 都已被标准库取代，继续用只会让团队维护两套风格。
5. **版本升级风险**：Boost 不保证跨版本 API 兼容，1.92 就移除了不少废弃接口。
6. **ABI 面**：混用 Debug / Release 或不同编译器版本的 Boost 库会直接崩，
   二进制分发比标准库麻烦。
7. **缺少"标准"约束**：API 由社区决定，可能出现设计不一致。

---

## 六、实际项目怎么选

**默认策略：先看 C++ 标准库有没有 —— 有就用标准的。**

只在下列情况引入 Boost：

1. 标准库确实没有：容器多样性（`multi_index` / `flat_map`）、JSON / INI / XML、
   正则性能、Asio 网络、可合并堆、Graph 算法。
2. 需要跨平台且不想引多个第三方库。
3. 项目已经在用 Boost，为了风格统一继续用同一套（比如 Asio）。

明确**不要**用 Boost 的地方：

- `shared_ptr` / `unique_ptr` / `optional` / `variant` / `any` / `thread` / `chrono` /
  `filesystem` / `regex`（性能不敏感时）—— 标准库版本已经够好且无依赖。
- 只需要一两个小工具（比如只想要 `trim`）就引整个 Boost 不划算。

**VS 工程实践建议**

- 用 vcpkg / NuGet 管 Boost，别手工复制头文件进工程。
- 把 Boost 包含目录放进独立属性表（本工程的 `boost.props`），
  让"是否使用 Boost"成为一个开关。
- 用预编译头（PCH）+ `/MP` 多核编译缓解编译慢的问题。
- 只链接用到的编译库，依赖更清晰。
- 写进编码规范：**标准库优先，用 Boost 需在评审时说明理由**。

---

## 七、一句话总结

Boost 是"标准库的扩展包 + 试验田"，不是"标准库的替代品"。
它的优点在于覆盖了 STL 没覆盖的空白（你可以在 M4 / M6 / M8 里直观看到）；
缺点在于它覆盖的那些后来大多进了标准库，导致它背上越来越多的历史包袱
（你可以在 M2 / M3 里直观看到）。

**正确用法：把 Boost 当成标准库的补充，按需取用其中 STL 没有的部分。**
