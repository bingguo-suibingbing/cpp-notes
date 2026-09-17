// =============================================================================
//  m2_format.cpp —— Boost 的"劣势区"：格式化与字符串<->数字转换
//
//  结论速览：
//    * Boost.Format 在 C++11 年代是"唯一像样"的类型安全格式化方案，
//      但 C++20 的 std::format 后来居上：编译期检查、语法更简洁、速度更快。
//    * 这就是 Boost 最现实的"缺点"——它的大量组件被标准库吸收后，
//      留在 Boost 里的那个版本反而成了历史包袱。
//    * 教训：新项目优先用标准库；Boost 只在标准库"确实没有"时上。
// =============================================================================
#include <chrono>
#include <cstdio>
#include <format>
#include <iostream>
#include <string>

#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

#include "demo_api.h"
#include "demo_common.h"

using namespace demo;

void demo_m2_format() {
    title("M2. 格式化 / 转换：std::format 已经反超 boost::format");

    char rel[192];   // 用于打印性能倍数，整个函数内复用

    // -------------------------------------------------------------------
    item("1) 同一个输出，三种写法对比");
    {
        const std::string user = "Waj";
        const int id = 42;
        const double score = 95.5;

        // --- Boost.Format：位置参数 %1% %2%
        std::string b = str(boost::format("  [Boost ] user=%-6s id=%04d score=%.2f") % user % id % score);

        // --- std::format：C++20
        std::string s = std::format("  [STL   ] user={:<6} id={:04d} score={:.2f}", user, id, score);

        // --- printf
        char buf[128];
        std::snprintf(buf, sizeof buf, "  [printf] user=%-6s id=%04d score=%.2f", user.c_str(), id, score);

        line(s);
        line(b);
        line(buf);
    }

    // -------------------------------------------------------------------
    item("2) Boost.Format 少打一会儿字的场景（参数复用时优势）");
    {
        // Boost.Format 对象可以"攒"参数多次使用，位置参数可以重复引用
        boost::format f("%1% 打 %1% 的广告，成绩 %2%");
        f % "王五" % 88;
        side("Boost", str(f));
        side("Boost", "位置参数 %1% 可以重复引用，不用把同一个变量传两遍");

        // std::format 也能做到：用 {0} 索引
        side("STL  ", std::format("{0} 打 {0} 的广告，成绩 {1}", "王五", 88));
        side("STL  ", "std::format 用 {0} 索引也能重复引用，其实打平了");
    }

    // -------------------------------------------------------------------
    item("3) 编译期检查 —— std::format 的决定性优势");
    line("  [Boost] boost::format(\"%1% %2%\") % 1;      <-- 参数个数不对");
    line("          编译能过，运行到 str() 才抛 boost::io::too_few_args");
    line("  [STL  ] std::format(\"{} {}\", 1);          <-- 同样参数不够");
    line("          MSVC 直接编译报错: 值不足以填充格式字符串");
    line("          写错格式符 std::format(\"{:d}\", \"abc\") 也是编译期报错，");
    line("          而 boost::format / printf 都是运行期崩溃或乱码。");
    line("");
    line("  >>> 这是实测结论：把错误从运行期提前到编译期，是 std::format 最大价值。");
    {
        // 演示"参数不够"到底是运行期才发现的：捕获它
        try {
            boost::format f("%1% %2%");
            f % 1;              // 只给 1 个参数，编译完全通过
            std::string out = str(f);   // <-- 到这里才发现
            side("Boost", "意外没抛异常，输出: " + out);
        } catch (const std::exception& e) {
            side("Boost", std::string("运行期才抛异常: ") + e.what());
            side("STL  ", "同样的错误 std::format 在编译期就被 MSVC 拦住，程序根本发不出去");
        }
    }

    // -------------------------------------------------------------------
    item("4) 性能对比：20 万次格式化");
    {
        constexpr int N = 200000;
        volatile size_t sink = 0;

        double t_std = time_ms([&] {
            for (int i = 0; i < N; ++i) {
                std::string s = std::format("id={:04d} name={:<8} score={:.2f}", i, "Waj", 95.5);
                sink += s.size();
            }
        });

        double t_printf = time_ms([&] {
            char buf[128];
            for (int i = 0; i < N; ++i) {
                std::snprintf(buf, sizeof buf, "id=%04d name=%-8s score=%.2f", i, "Waj", 95.5);
                sink += buf[0];
            }
        });

        double t_boost = time_ms([&] {
            for (int i = 0; i < N; ++i) {
                std::string s = str(boost::format("id=%04d name=%-8s score=%.2f") % i % "Waj" % 95.5);
                sink += s.size();
            }
        });

        side("STL  ", "std::format   : " + ms(t_std));
        side("STL  ", "std::snprintf : " + ms(t_printf));
        side("Boost", "boost::format : " + ms(t_boost));
        std::snprintf(rel, sizeof rel, "  ==> boost::format 比 std::format 慢 %.1f 倍", t_boost / t_std);
        line(rel);
        line("  (请用 Release x64 跑；Debug 下两者都被拖慢，但倍数关系一致)");
        line("  原因: boost::format 每个 %N% 都要构造/销毁一个小对象并压栈，");
        line("        std::format 是编译期把格式串拆成指令序列，直接写进结果缓冲。");
        (void)sink;
    }

    // -------------------------------------------------------------------
    item("5) 字符串 <-> 数字：lexical_cast vs std::from_chars / to_chars");
    {
        side("Boost", "boost::lexical_cast<double>(\"3.14\")      -> " +
                          std::to_string(boost::lexical_cast<double>("3.14")));
        double d = 0;
        std::from_chars("3.14", "3.14" + 4, d);
        side("STL  ", "std::from_chars(\"3.14\", ...)            -> " + std::to_string(d));

        // 错误处理：一个抛异常，一个返回 error_code
        try {
            boost::lexical_cast<int>("12abc");
        } catch (const boost::bad_lexical_cast& e) {
            side("Boost", std::string("lexical_cast<int>(\"12abc\") 抛异常: ") + e.what());
        }
        int v = 0;
        auto r = std::from_chars("12abc", "12abc" + 5, v);
        side("STL  ", std::string("std::from_chars 不抛异常，返回 error_code=") +
                          (r.ec == std::errc() ? "ok" : "invalid_argument") +
                          "，v=" + std::to_string(v) + "，停在第 " +
                          std::to_string(r.ptr - "12abc") + " 个字符");
        side("STL  ", "  --> 无异常、不分配内存，禁用异常/高并发的项目里更合适");
        side("Boost", "  --> lexical_cast 一行搞定且泛型更强（任何可流式输入的类型都能转），");
        side("Boost", "      还能直接 lexical_cast<std::string>(3.14) 转回来，from_chars 只做数值");

        // 性能
        constexpr int M = 200000;
        volatile long long acc = 0;
        double tb = time_ms([&] {
            for (int i = 0; i < M; ++i) acc += boost::lexical_cast<int>(std::to_string(i % 10000));
        });
        double ts = time_ms([&] {
            char buf[16];
            for (int i = 0; i < M; ++i) {
                int x = i % 10000;
                auto rr = std::to_chars(buf, buf + sizeof buf, x);
                int y = 0;
                std::from_chars(buf, rr.ptr, y);
                acc += y;
            }
        });
        side("Boost", "lexical_cast 往返 : " + ms(tb));
        side("STL  ", "to_chars/from_chars: " + ms(ts));
        std::snprintf(rel, sizeof rel, "  ==> 数值转换 from_chars/to_chars 快 %.1f 倍", tb / ts);
        line(rel);
        (void)acc;
    }

    // -------------------------------------------------------------------
    line();
    line("小结(Boost 输在): 这一块是 STL 反超的典型 —— C++17/20 之后，");
    line("  格式化(std::format)、数值转换(from_chars/to_chars)、文件系统(std::filesystem)、");
    line("  智能指针、optional/variant/any/string_view 全部进了标准库，");
    line("  对应的 Boost 组件从必需品变成了兼容旧代码的历史包袱。");
}
