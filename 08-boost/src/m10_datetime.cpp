// =============================================================================
//  m10_datetime.cpp —— 日期时间：一个"各有所长"的对比
//
//  结论速览：
//    * C++20 的 <chrono> 已经能表达日历日期（year_month_day），
//      从 Boost.Date_Time 手里接过了"日期表示"这块地盘。
//    * 但 chrono 缺的是"业务化日期运算"和"现成的解析/格式化入口"：
//      Boost 的 end_of_month / next_weekday / 加月份自动裁剪到月末 /
//      from_simple_string / to_simple_string，chrono 里要么没有，要么要绕。
//    * 时间点、时长、单调时钟、时区：一律 std::chrono，Boost 已无优势。
// =============================================================================
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

#include <boost/date_time/gregorian/gregorian.hpp>

#include "demo_api.h"
#include "demo_common.h"

using namespace demo;
namespace bg = boost::gregorian;

namespace {

// std::chrono 的日期 -> 字符串：C++20 标准里没有直接入口，必须手写
std::string stl_to_string(std::chrono::year_month_day d) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u",
                  (int)d.year(), (unsigned)d.month(), (unsigned)d.day());
    return buf;
}

// 星期几的名字：chrono 的 weekday 不提供名字，要自己查表
const char* stl_weekday_name(std::chrono::weekday wd) {
    static const char* n[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    return n[wd.c_encoding()];
}

}  // namespace

void demo_m10_datetime() {
    title("M10. 日期时间：chrono 接手了日期表示，但没接手全部");

    // ===================================================================
    item("1) 同一个日期，两边怎么表示");
    {
        bg::date bd(2026, bg::Mar, 14);
        side("Boost", "bg::date(2026, Mar, 14)  -> " + bg::to_simple_string(bd) +
                          "   等价 ISO 写法: " + bg::to_iso_extended_string(bd));

        auto sd = std::chrono::year{2026} / std::chrono::March / 14;
        side("STL  ", "year{2026}/March/14      -> " + stl_to_string(sd) +
                          "   (chrono::year_month_day)");
        line("");
        line("  两边都能表达日历日期，但运算和格式化的便利性差很多 —— 往下看。");
    }

    // ===================================================================
    item("2) 日期运算：Boost 明显更好用");
    {
        bg::date d(2026, bg::Jan, 31);
        side("Boost", "起点: " + bg::to_simple_string(d));

        // 加一个月 —— 自动裁剪到 2 月末（1/31 + 1月 -> 2/28）
        side("Boost", "d + months(1)          = " + bg::to_simple_string(d + bg::months(1)) +
                          "   <-- 1/31 加 1 个月自动裁到 2 月末");
        side("Boost", "d + days(1)            = " + bg::to_simple_string(d + bg::days(1)));
        side("Boost", "d.end_of_month()       = " + bg::to_simple_string(d.end_of_month()));
        side("Boost", "day_of_week()=" + std::to_string(d.day_of_week()) +
                          "(0=周日)  day_of_year()=" + std::to_string(d.day_of_year()));

        // 下一个工作日（业务语义，几行就写出来）
        auto next_wd = d;
        do { next_wd += bg::days(1); } while (next_wd.day_of_week() == 0 || next_wd.day_of_week() == 6);
        side("Boost", "下一个工作日(跳过周末) = " + bg::to_simple_string(next_wd));

        side("Boost", "is_leap_year(2024) = " +
                          std::string(bg::gregorian_calendar::is_leap_year(2024) ? "true" : "false"));
        side("Boost", "两个日期相减 -> " +
                          std::to_string((bg::date(2026, bg::Dec, 31) - bg::date(2026, bg::Jan, 1)).days()) +
                          " 天");
        line("");

        // ---- 等价 std::chrono 写法
        auto s1 = std::chrono::year{2026} / std::chrono::January / 31;
        side("STL  ", "起点: " + stl_to_string(s1));

        // 天真写法：直接给 day{31} 会得到"非法日期"，必须自己判断
        std::chrono::year_month ym{s1.year(), s1.month() + std::chrono::months{1}};
        std::chrono::year_month_day naive{ym.year(), ym.month(), std::chrono::day{31}};
        side("STL  ", "year_month_day{2026年2月, day{31}} -> " +
                          (naive.ok() ? stl_to_string(naive) : std::string("ok()==false（2月31日非法）")));

        // 正确写法：绕 year_month_day_last 手动裁剪
        auto last = std::chrono::year_month_day_last{ym.year(), std::chrono::month_day_last{ym.month()}};
        unsigned last_day = (unsigned)last.day();
        std::chrono::year_month_day fixed{ym.year(), ym.month(),
                                          std::chrono::day{(std::min)(31u, last_day)}};
        side("STL  ", "手动裁剪后 -> " + stl_to_string(fixed) +
                          "   <-- 要 year_month_day_last 绕一圈，Boost 一行搞定");

        auto wd = std::chrono::weekday{fixed};
        side("STL  ", "weekday 名字要自己查表: " + std::string(stl_weekday_name(wd)) +
                          "   (Boost: d.day_of_week() 直接用)");
    }

    // ===================================================================
    item("3) 字符串 <-> 日期：解析与格式化");
    {
        bg::date bd = bg::from_simple_string("2026-03-14");
        side("Boost", "bg::from_simple_string(\"2026-03-14\") -> " + bg::to_simple_string(bd));
        side("Boost", "bg::to_iso_extended_string(bd)         -> " + bg::to_iso_extended_string(bd));
        side("Boost", "还有 from_undelimited_string / 以及 operator>> 流式解析");

        side("STL  ", "std::chrono 里没有从字符串解析日期的入口");
        side("STL  ", "  手写 sscanf(y,m,d) 会丢掉日历校验（2月30日也能解析出来）");
        side("STL  ", "  std::get_time 只能填 std::tm，没有日历语义");
        side("STL  ", "  C++20 有格式化路径（std::format + chrono），但解析支持较新且不完整");
        line("");
        line("  >>> 只要涉及\"把用户输入的日期读进来\"，Boost.Date_Time 目前仍然最省事。");
    }

    // ===================================================================
    item("4) 时间点 / 时长 / 计时：这里 STL 完胜");
    {
        using namespace std::chrono;
        auto t0 = steady_clock::now();

        // 忙等一小会儿，确保测出的不是 0
        volatile double x = 0;
        for (int i = 0; i < 300000; ++i) x += i * 0.5;

        auto t1 = steady_clock::now();
        auto us = duration_cast<microseconds>(t1 - t0).count();

        side("STL  ", "steady_clock 测出一段循环耗时: " + std::to_string(us) + " 微秒");
        side("STL  ", "duration<double, milli> / duration_cast 编译期检查单位，不会算错量级");
        side("STL  ", "C++20 还有 system_clock 与 tzdb 时区支持（zoned_time）");
        side("Boost", "Boost.Chrono 是 C++11 之前的标准库替代品 —— 现在没有任何理由再用它");
        line("  结论：计时/时长/时间点/时区 -> 一律 std::chrono。");
        (void)x;
    }

    // ===================================================================
    line();
    line("小结(各有所长):");
    line("  Boost.Date_Time 赢在业务化日期运算（加月裁剪、月末、工作日）和现成的解析/格式化；");
    line("  std::chrono      赢在时间点/时长/时区、类型安全、零依赖。");
    line("  推荐：日期业务逻辑多 -> 可用 Boost.Date_Time；纯计时/时长 -> 一律 std::chrono。");
}
