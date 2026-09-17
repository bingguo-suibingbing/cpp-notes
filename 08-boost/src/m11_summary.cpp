// =============================================================================
//  m11_summary.cpp —— 总结：Boost 相对 STL 的优缺点全景
// =============================================================================
#include <iostream>
#include <string>

#include "demo_api.h"
#include "demo_common.h"

using namespace demo;

namespace {

void row(const std::string& topic, const std::string& boost, const std::string& stl,
         const std::string& verdict) {
    say("  " + topic);
    say(std::string(28 > topic.size() + 4 ? 28 - topic.size() - 4 : 1, ' '));
    say("Boost: " + boost);
    say(std::string(30 > boost.size() + 7 ? 30 - boost.size() - 7 : 1, ' '));
    say("STL: " + stl);
    say(std::string(24 > stl.size() + 5 ? 24 - stl.size() - 5 : 1, ' '));
    say("-> " + verdict + "\n");
}

}  // namespace

void demo_m11_summary() {
    title("M11. 总结：Boost 相对 STL 的优缺点全景");

    // ===================================================================
    item("A. 逐主题结论表");
    line("  主题                        Boost 方案                 STL 方案               结论");
    line("  " + std::string(120, '-'));
    row("字符串算法",   "algorithm::string",  "无(要手写)",       "Boost 明显更强");
    row("字符串格式化", "boost::format",      "std::format(C++20)","STL 更好(编译期检查)");
    row("字符串<->数字", "lexical_cast",      "from/to_chars(C++17)","STL 更快, Boost 更泛型");
    row("智能指针(通用)", "shared/unique_ptr", "std:: 同名版本",   "打平, 用标准的");
    row("intrusive_ptr", "有",               "无",               "Boost 独有");
    row("local_shared_ptr", "有(非原子计数)", "无",               "Boost 独有");
    row("multi_index",   "multi_index_container", "无",          "Boost 独有(无替代)");
    row("环形缓冲",      "circular_buffer",   "无",               "Boost 独有");
    row("双向映射",      "bimap",             "无",               "Boost 独有");
    row("flat_map",      "container::flat_map","无(C++23 flat_map)","Boost 先行");
    row("small_vector",  "container::small_vector","无",          "Boost 独有");
    row("可合并堆",      "heap::*_heap",      "只有 priority_queue","Boost 独有");
    row("optional",      "boost::optional",   "std::optional(C++17)","打平, 用标准的");
    row("variant",       "variant2(never-valueless)","std::variant", "各有千秋");
    row("any",           "boost::any",        "std::any(C++17)",  "打平, 用标准的");
    row("JSON/XML/INI",  "property_tree / json","无",             "Boost 独有");
    row("正则",          "boost::regex",      "std::regex(C++11)", "Boost 更快更全");
    row("分词",          "tokenizer",         "无",               "Boost 独有");
    row("文件系统",      "filesystem",        "std::filesystem",  "打平, 用标准的");
    row("日期时间",      "date_time",         "chrono(C++20)",    "各有千秋");
    row("作用域守卫",    "BOOST_SCOPE_EXIT",  "无",               "Boost 独有");
    row("错误码",        "system::error_code","std::error_code",  "打平, 用标准的");
    row("result<T>",     "system::result",    "std::expected(C++23)","Boost 先行");

    // ===================================================================
    item("B. Boost 的优点（实测/客观）");
    line("  1. 补标准库的空白：multi_index / circular_buffer / bimap / 可合并堆 /");
    line("     JSON 解析 / scope_exit —— 这些 STL 到今天都没有，自己写基本都会踩坑。");
    line("  2. 一个依赖换一大堆能力：引一次 Boost，就有 160+ 个库可用，");
    line("     不用为每个需求单独挑一个第三方库（也就少了一堆许可证和版本冲突）。");
    line("  3. 标准的试验田：shared_ptr / regex / filesystem / optional / variant / any /");
    line("     thread / chrono 全是先在 Boost 里跑通、再进标准的 —— 用 Boost 常常等于");
    line("     \"提前用上两三年后的标准库\"。");
    line("  4. 跨平台一致性：同一份代码在 MSVC/GCC/Clang 上行为一致，");
    line("     不像各标准库实现的边角行为有差异（std::regex 尤其明显）。");
    line("  5. 成熟度：很多组件在金融/游戏/通信里跑了 20 年，边界条件被踩遍了。");

    // ===================================================================
    item("C. Boost 的缺点（也是实测/客观）");
    line("  1. 编译时间爆炸：纯头文件的模板展开极重。");
    line("     实测（本机 MSVC v145 / /O2 / Release x64，各编译 3 次取平均）：");
    line("       bench\\compile_bench_std.cpp   (只用 STL 实现 11 个功能) : 2641 ms");
    line("       bench\\compile_bench_boost.cpp (同一批功能改用 Boost)   : 4768 ms");
    line("       ==> Boost 版编译耗时是 STL 版的 1.81 倍");
    line("     两个文件逐行对应（见 bench\\ 目录），可以自己复现：");
    line("       cl /c /O2 /std:c++20 /utf-8 /I bench /I <BoostRoot> bench\\compile_bench_boost.cpp");
    line("     本演示工程 12 个 cpp 完整重编译一次约 38 秒（/m:1 单进程）。");
    line("  2. 依赖与体积：完整 Boost 源码几百 MB；虽然大多 header-only，");
    line("     但 regex / filesystem / thread / iostreams 等仍是编译库，要构建+链接。");
    line("  3. 学习曲线与文档：模板报错动辄几十屏（类型名全部展开），");
    line("     新手看到 boost::multi_index_container<...> 的模板参数就劝退。");
    line("  4. 历史包袱 / 已被标准库取代的组件：format、lexical_cast、shared_ptr、");
    line("     optional、variant(非2)、any、chrono、filesystem …");
    line("     继续用它们只会让团队同时维护两套风格。");
    line("  5. 版本升级风险：Boost 不保证跨版本 API 兼容，");
    line("     升级一次可能大面积改代码（Boost 1.92 已移除不少废弃接口）。");
    line("  6. ABI 面：混用 Debug/Release 或不同编译器版本的 Boost 库会直接崩，");
    line("     二进制分发场景比标准库麻烦。");
    line("  7. 没有\"标准\"约束：API 由社区决定，可能出现设计不一致；");
    line("     标准库至少有个委员会 + 正式规范。");

    // ===================================================================
    item("D. 决策指南（实际项目怎么做）");
    line("  默认策略：先看 C++ 标准库有没有 —— 有就用标准的。");
    line("");
    line("  只在下列情况引入 Boost：");
    line("   [1] 标准库确实没有：容器多样性(multi_index/flat_map)、");
    line("       JSON/INI/XML、正则性能、Asio 网络、可合并堆、Graph 算法。");
    line("   [2] 需要跨平台且不想引多个第三方库；");
    line("   [3] 项目已经在用 Boost，为了风格统一继续用同一套（比如 Asio）。");
    line("");
    line("  明确不要用 Boost 的地方：");
    line("   [x] shared_ptr / unique_ptr / optional / variant / any / thread / chrono /");
    line("       filesystem / regex(若性能不敏感) —— 标准库版本已经够好且无依赖；");
    line("   [x] 只需要一两个小工具（比如只想要 trim）就引整个 Boost 不划算，");
    line("       考虑自己写十几行或用更小的库。");
    line("");
    line("  VS 工程实践建议：");
    line("   - 用 vcpkg / NuGet 管 Boost，别手工复制头文件到工程里；");
    line("   - 把 Boost 包含目录放进独立属性表（本工程的 boost.props），");
    line("     这样\"是否使用 Boost\"是一个开关，而不是散落在各处的配置；");
    line("   - 预先编译头文件(PCH) + /MP 多核编译，能显著缓解编译慢的问题；");
    line("   - 只链接用到的编译库（BOOST_ALL_NO_LIB 关掉自动链接，手动指定，");
    line("     依赖更清晰）；");
    line("   - 团队里写进编码规范：\"标准库优先，Boost 需在评审时说明理由\"。");

    // ===================================================================
    item("E. 一句话总结");
    line("   Boost 是\"标准库的扩展包 + 试验田\"，不是\"标准库的替代品\"。");
    line("   它的优点在于覆盖了 STL 没覆盖的空白，缺点在于它覆盖的那些");
    line("   后来大多进了标准库，导致它背上越来越多的历史包袱。");
    line("   正确的用法是：把 Boost 当成标准库的补充，按需取用其中 STL 没有的部分。");
}
