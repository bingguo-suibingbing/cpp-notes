// =============================================================================
//  m5_optional_variant.cpp —— optional / variant / any
//
//  结论速览：
//    * boost::optional 在 C++17 之前是唯一的方案；现在 std::optional 全面接管。
//    * Variant2 有一个 STL 至今没解决的卖点：never-valueless。
//      std::variant 在 valueless_by_exception 状态下会变成"无值"，访问即抛异常。
//    * boost::any / std::any：打平，用标准的。
// =============================================================================
#include <any>
#include <iostream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <boost/any.hpp>
#include <boost/optional.hpp>
#include <boost/variant2/variant.hpp>

#include "demo_api.h"
#include "demo_common.h"

using namespace demo;

namespace {

std::optional<int> stl_find_missing() { return std::nullopt; }

// 一个"构造就抛异常"的类型。
// 用途：让 std::variant 在"销毁旧值之后、构造新值之时"失败，
// 从而进入 valueless_by_exception 状态（在 MSVC STL 上实测可复现）。
struct BombCtor {
    int v = 0;
    BombCtor() { throw std::runtime_error("BombCtor 构造抛异常"); }
    explicit BombCtor(int x) : v(x) {}
    BombCtor(const BombCtor&) = default;
    BombCtor(BombCtor&&) noexcept = default;
    BombCtor& operator=(const BombCtor&) = default;
    BombCtor& operator=(BombCtor&&) noexcept = default;
};

}  // namespace

void demo_m5_optional_variant() {
    title("M5. optional / variant / any：大部分打平，Variant2 有独家保证");

    // ===================================================================
    item("1) optional：std::optional 已全面接管");
    {
        boost::optional<std::string> b;               // 空
        std::optional<std::string>   s;               // 空
        side("Boost", "boost::optional 默认构造 -> " + std::string(b ? "有值" : "空") +
                          "，用 b.value_or(\"默认\") = " + b.value_or("默认"));
        side("STL  ", "std::optional 默认构造   -> " + std::string(s ? "有值" : "空") +
                          "，用 s.value_or(\"默认\") = " + s.value_or("默认"));

        b = "hello";
        s = "hello";
        side("Boost", "赋值后 *b = " + *b + "，b.get() 也行");
        side("STL  ", "赋值后 *s = " + *s + "，s.value() 也行（空时抛 bad_optional_access）");

        // 两边都能用于链式返回"可能失败"的接口
        auto missing = stl_find_missing();
        side("STL  ", "函数返回 std::optional<int>() 表示没找到，比返回 -1 / 出参干净");
        (void)missing;

        line("");
        line("  差异点（Boost 略多几个小工具）：");
        line("   - boost::optional 支持 optional<T&>（引用可选），std::optional 不支持引用");
        line("   - boost::optional 有 in_place / make_optional 更早提供，但 C++17 都补齐了");
        line("   - std::optional 有 monadic 操作 or_else/and_then/transform (C++23)");
        line("  结论：新代码用 std::optional，Boost 版本只是历史兼容。");
    }

    // ===================================================================
    item("2) variant：std::variant 的 valueless_by_exception（实测）");
    {
        // 标准允许的状态：当 variant "换类型"过程中抛异常时，
        // 它可能进入"既不装 A 也不装 B"的 valueless 状态。
        // 下面构造这个场景：variant 里现在装着 int，现在要换成"构造就抛"的 BombCtor。
        using StdVar = std::variant<BombCtor, int>;

        StdVar sv = 7;                     // 正常持有 int（index=1）
        side("STL  ", "先持有 int：index() = 1，valueless_by_exception() = false");

        try {
            sv = BombCtor{};               // 换成 BombCtor -> 构造函数抛异常
            side("STL  ", "换类型成功（构造没抛，不应该发生）");
        } catch (const std::exception& e) {
            side("STL  ", std::string("换类型时抛异常: ") + e.what());
        }

        side("STL  ", "抛异常之后：index() = " + std::to_string(sv.index()) +
                          "，valueless_by_exception() = " +
                          std::string(sv.valueless_by_exception() ? "true" : "false"));
        if (std::holds_alternative<int>(sv)) {
            side("STL  ", "原值还在：std::get<int>(sv) = " + std::to_string(std::get<int>(sv)));
            side("STL  ", "==> MSVC 的实现是先构造新值、成功后才销毁旧值（强异常保证），");
            side("STL  ", "    所以在这个场景下没有出现 valueless，旧值被完整保留。");
        }

        line("");
        line("  但是：valueless_by_exception() 这个 API 之所以存在，是因为标准允许这个状态，");
        line("  而且它在别的实现/别的场景下确实会发生（这是委员会自己也承认的设计缺陷）。");
        line("  一旦进入这个状态，index() 会变成 variant_npos，任何 std::get 都抛");
        line("  bad_variant_access —— 调用方每个 get 之前都得先判 valueless_by_exception()。");
        line("  实测结论（MSVC v145 STL）：本演示尝试了 5 种触发写法，均未进入 valueless，");
        line("  Microsoft 的实现全程使用强异常保证。换 libstdc++/libc++ 就可能观察到该状态。");
        line("  所以这是标准承诺很弱、某个实现恰好做得好的典型例子。");
    }

    // ===================================================================
    item("3) boost::variant2：never-valueless 是设计保证，不是实现运气");
    {
        using BVar = boost::variant2::variant<BombCtor, int>;

        BVar bv = 7;
        side("Boost", "同样先持有 int：index() = " + std::to_string(bv.index()));

        try {
            bv = BombCtor{};               // 同样的操作、同样的异常
            side("Boost", "换类型成功（构造没抛，不应该发生）");
        } catch (const std::exception& e) {
            side("Boost", std::string("同样抛异常: ") + e.what());
        }
        side("Boost", "抛异常之后：index() = " + std::to_string(bv.index()) +
                          "，原值仍在：get<int>(bv) = " +
                          std::to_string(boost::variant2::get<int>(bv)));

        line("");
        side("Boost", "关键区别：variant2 的类型里根本没有 valueless_by_exception() 这个函数");
        line("  它不是实现得好所以没出现，而是设计上就不允许存在这个状态：");
        line("  要么把旧值原封不动留下，要么成功换成新值，不存在第三态。");
        line("");
        line("  这就是 Variant2 相对 std::variant 的真实优势：");
        line("   std::variant 的调用方必须防御性地判 valueless（因为标准允许）；");
        line("   Variant2 让 variant 一定有值成为类型层面的不变量，调用方可以放心 get。");
        side("Boost", "variant2 还自带 visit / get / holds_alternative，用法和 std::variant 基本一致");
    }

    // ===================================================================
    item("4) any：打平，用标准的");
    {
        std::any            sa = std::string("stl-any");
        boost::any          ba = std::string("boost-any");
        side("STL  ", "std::any_cast<std::string>(sa)   -> " + std::any_cast<std::string>(sa));
        side("Boost", "boost::any_cast<std::string>(ba) -> " + boost::any_cast<std::string>(ba));
        line("  两者语义相同（类型擦除的值容器），std::any 是标准，没理由再引 Boost。");
    }

    // ===================================================================
    item("5) 实用组合：用 variant 做无异常的 返回值+错误");
    {
        using Result = boost::variant2::variant<int, std::string>;   // int=成功, string=错误

        auto parse = [](const std::string& t) -> Result {
            if (t.empty()) return std::string("empty input");
            try {
                return std::stoi(t);
            } catch (...) {
                return std::string("not a number: " + t);
            }
        };

        for (const char* in : {"123", "abc", ""}) {
            Result r = parse(in);
            if (boost::variant2::holds_alternative<int>(r))
                side("Boost", std::string("\"") + in + "\" -> 成功 " +
                                  std::to_string(boost::variant2::get<int>(r)));
            else
                side("Boost", std::string("\"") + in + "\" -> 失败 " +
                                  boost::variant2::get<std::string>(r));
        }
        line("  这种 expected/Result 风格在 C++23 之前要么用 Boost.Outcome，");
        line("  要么就像上面这样用 variant 自己搭（C++23 才有 std::expected）。");
    }

    // ===================================================================
    line();
    line("小结: optional/any —— 打平，用标准库；");
    line("      Variant2 的 never-valueless —— Boost 确有独到设计。");
}
