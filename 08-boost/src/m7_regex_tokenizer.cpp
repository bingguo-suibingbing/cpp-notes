// =============================================================================
//  m7_regex_tokenizer.cpp —— 正则与分词
//
//  结论速览：
//    * std::regex 在 C++11 补上了正则，但性能和功能都被 boost::regex 压一头；
//      尤其 std::regex 的实现（libstdc++/MSVC）在大规模匹配时慢得有名。
//    * 语法上两者都支持 Perl 风格，"能写什么"基本一致；
//      但 Boost.Regex 额外支持 lookbehind、\K、命名捕获等更靠近日用正则引擎的特性。
//    * boost::tokenizer：惰性分词迭代器，支持"带引号的 CSV"这种真实场景。
// =============================================================================
#include <chrono>
#include <cstdio>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <boost/regex.hpp>
#include <boost/tokenizer.hpp>

#include "demo_api.h"
#include "demo_common.h"

using namespace demo;

void demo_m7_regex_tokenizer() {
    title("M7. 正则 / 分词：boost::regex 比 std::regex 更快也更全");

    // -------------------------------------------------------------------
    item("1) 同样的捕获，两边写法几乎一样");
    {
        const std::string log = "2026-03-14 09:12:33 [ERROR] user=alice code=5003";

        boost::regex bre(R"(^(\d{4})-(\d{2})-(\d{2})\s+(\d{2}):(\d{2}):(\d{2})\s+\[(\w+)\]\s+user=(\w+)\s+code=(\d+))");
        boost::smatch bm;
        if (boost::regex_match(log, bm, bre)) {
            side("Boost", "regex_match 捕获组: 日期=" + bm[1].str() + "-" + bm[2].str() + "-" + bm[3].str() +
                              " 级别=" + bm[7].str() + " 用户=" + bm[8].str() + " 码=" + bm[9].str());
        }

        std::regex sre(R"(^(\d{4})-(\d{2})-(\d{2})\s+(\d{2}):(\d{2}):(\d{2})\s+\[(\w+)\]\s+user=(\w+)\s+code=(\d+))");
        std::smatch sm;
        if (std::regex_match(log, sm, sre)) {
            side("STL  ", "regex_match 捕获组: 日期=" + sm[1].str() + "-" + sm[2].str() + "-" + sm[3].str() +
                              " 级别=" + sm[7].str() + " 用户=" + sm[8].str() + " 码=" + sm[9].str());
        }
        line("  这一层两者打平（C++11 的 std::regex 就是从 Boost.Regex 来的）。");
    }

    // -------------------------------------------------------------------
    item("2) 性能：这才是差距所在");
    {
        // 注意：这里必须逐行匹配。
        // 如果用 regex_search 扫整段文本，模式里的 ^ 只会匹配文本开头，
        // 结果只能命中 1 次、耗时接近 0，测不出真实性能。
        std::vector<std::string> lines;
        lines.reserve(40000);
        for (int i = 0; i < 20000; ++i) {
            lines.push_back("GET /api/v1/items?id=" + std::to_string(i) + " from=10.0.0.1");
            lines.push_back("POST /api/v1/items body=x from=10.0.0.2");
        }
        const char* pattern = R"(^(GET|POST) (/api/v1/\w+)\?id=(\d+))";

        int hits_std = 0;
        double t_std = time_ms([&] {
            std::regex re(pattern);          // 每次都构造（真实代码里也常这么写）
            std::smatch m;
            for (const auto& l : lines)
                if (std::regex_search(l, m, re)) ++hits_std;
        });

        int hits_boost = 0;
        double t_boost = time_ms([&] {
            boost::regex re(pattern);
            boost::smatch m;
            for (const auto& l : lines)
                if (boost::regex_search(l, m, re)) ++hits_boost;
        });

        side("STL  ", "std::regex   匹配 " + std::to_string(lines.size()) + " 行: " + ms(t_std) +
                          "  命中 " + std::to_string(hits_std));
        side("Boost", "boost::regex 匹配 " + std::to_string(lines.size()) + " 行: " + ms(t_boost) +
                          "  命中 " + std::to_string(hits_boost));
        char buf[128];
        std::snprintf(buf, sizeof buf, "  ==> boost::regex 快 %.1f 倍", t_std / t_boost);
        line(buf);
        line("  (Release 下跑；std::regex 慢主要来自 MSVC/libstdc++ 的回溯式实现)");
        line("  提示: 真实项目里固定模式应该把 regex 对象构造一次、复用，而不是每次重建；");
        line("        即使那样优化，boost::regex 在多数模式上仍然更快。");
    }

    // -------------------------------------------------------------------
    item("3) boost::regex 能写、std::regex 写不了的语法");
    {
        const std::string s = "price: 1980 CNY, discount: 200 CNY";

        // lookbehind：std::regex 默认 ECMAScript 语法不支持 (?<=...)
        boost::regex lb(R"((?<=price: )\d+)");
        boost::smatch m;
        if (boost::regex_search(s, m, lb))
            side("Boost", "(?<=price: ) 后行断言 -> " + m.str() + "   <-- std::regex 会抛 regex_error");

        // \K：丢弃之前匹配的内容（PCRE 特性）
        boost::regex k(R"(\d+\s+\K\w+)");
        if (boost::regex_search(s, m, k))
            side("Boost", R"(\d+\s+\K\w+ -> )" + m.str());

        // 命名捕获（Boost 用 (?<name>...)）
        boost::regex named(R"(price: (?<amount>\d+) (?<cur>\w+))");
        boost::smatch nm;
        if (boost::regex_search(s, nm, named))
            side("Boost", "命名捕获 amount=" + nm["amount"].str() + " cur=" + nm["cur"].str());

        std::smatch sm2;
        try {
            std::regex slb(R"((?<=price: )\d+)");
            (void)std::regex_search(s, sm2, slb);
            side("STL  ", "居然支持后行断言？");
        } catch (const std::regex_error& e) {
            side("STL  ", std::string("std::regex 直接抛 regex_error: ") + e.what());
        }
    }

    // -------------------------------------------------------------------
    item("4) 替换：两边都有，Boost 多了 format 语法");
    {
        std::string s = "alice=100; bob=200; carol=300";
        side("Boost", "boost::regex_replace(s, re, \"$1\") 支持 $1 / $& / (?1) 等");
        boost::regex re(R"((\w+)=(\d+))");
        std::string r1 = boost::regex_replace(s, re, "$1:$2");
        side("Boost", "结果: " + r1);
        std::string r2 = boost::regex_replace(s, re, "[\"$2|$1\"]", boost::regex_constants::format_all);
        side("Boost", "format_all 结果: " + r2);

        std::regex sre(R"((\w+)=(\d+))");
        std::string r3 = std::regex_replace(s, sre, "$1:$2");
        side("STL  ", "std::regex_replace 结果: " + r3 + "   (打平)");
    }

    // -------------------------------------------------------------------
    item("5) boost::tokenizer：惰性分词，还懂引号");
    {
        // 普通分词
        std::string csv = "apple,banana,,cherry";
        boost::tokenizer<boost::char_separator<char>> tok(csv, boost::char_separator<char>(","));
        std::ostringstream os;
        int n = 0;
        for (const auto& t : tok) { os << "[" << t << "] "; ++n; }
        side("Boost", "按逗号分词 (" + std::to_string(n) + " 个): " + os.str());
        side("Boost", "注意：空字段保留为空串；而 boost::algorithm::split 会丢掉尾部的空字段");
        side("Boost", "差异点：tokenizer 不需要先构造 vector<string>，是惰性迭代 + 零拷贝 string 视图");

        // 带引号的 CSV —— 真实场景
        std::string quoted = "name,\"Li, Wei\",\"Beijing, CN\",42";
        boost::tokenizer<boost::escaped_list_separator<char>> qtok(quoted);
        os.str("");
        for (const auto& t : qtok) os << "[" << t << "] ";
        side("Boost", "escaped_list_separator 解析带引号的 CSV: " + os.str());
        line("");
        line("  >>> 这个例子最能说明 Boost 的价值：");
        line("      用 STL 手写带引号的 CSV 解析要处理状态机（引号内逗号不算分隔符、转义引号 …），");
        line("      而 Boost 给你 escaped_list_separator，一行搞定。");
    }

    // -------------------------------------------------------------------
    line();
    line("小结(Boost 赢在): 正则性能 + PCRE 级特性、以及 tokenizer 的惰性分词；");
    line("  缺点: boost::regex 是编译库（要链接 boost_regex），而 std::regex 开箱即用无依赖。");
}
