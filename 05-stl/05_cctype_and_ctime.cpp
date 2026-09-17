// ============================================================================
// 05_cctype_and_ctime.cpp  ——  <cctype> 字符分类 与 <ctime>/<chrono> 时间日期
//
// 演示主题：
//   第一部分 <cctype>：字符分类与转换
//     1. isalpha / isdigit / isalnum / isspace / isupper / islower / ispunct
//     2. toupper / tolower 与「必须转 unsigned char」这个 UB 坑
//     3. 用 ctype 做输入校验（判断纯数字、统计字符类别）
//     4. 现代 C++ 替代：<charconv> 判数字、std::ranges::all_of + lambda
//   第二部分 <ctime> / <chrono>：时间与日期
//     5. time_t / tm / localtime_s / strftime / mktime 的完整闭环
//     6. tm 的两个反直觉字段：tm_mon 从 0 开始、tm_year 要减 1900
//     7. mktime 的自动进位能力（日期加减的实用技巧）
//     8. clock() 测的是 CPU 时间，steady_clock 才是测墙钟时间
//     9. C++20 <chrono> 的日历类型：year_month_day / sys_days / days
//    10. std::format 直接格式化时间点（C++20）
//
// 关键结论：
//   - ctype 系列函数的参数必须是「unsigned char 能表示的值」或者 EOF。
//     对 char 为负的字节（中文 UTF-8 字节、Latin-1 扩展字符）直接传进去是 UB。
//     正确写法：std::isalpha(static_cast<unsigned char>(c))。
//   - clock() 返回的是「进程消耗的 CPU 时间」，不是墙上时钟。
//     测「这段代码多快」用 std::chrono::steady_clock；测「程序占了多少 CPU」才用 clock()。
//   - 时间戳运算必须用 64 位：ts += 100 * 24 * 3600 在 32 位 int 上会溢出。
//   - C++20 起有了 <chrono> 日历类型，不再需要 tm 的 +1900 / +1 手工修正。
//
// 说明：本文件定义了 _CRT_SECURE_NO_WARNINGS 以便演示 C 风格的 localtime；
//       正式写法应该用 localtime_s / std::chrono（两者本文件都会演示）。
// ============================================================================
#define _CRT_SECURE_NO_WARNINGS  // 为了能把 C 风格 localtime 和 C++ 写法对照展示

#include <algorithm>   // std::all_of
#include <cctype>      // 字符分类与转换
#include <charconv>    // std::from_chars：判断「能不能解析成数字」
#include <chrono>      // 现代计时与日历
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>       // time / tm / localtime / strftime / mktime / clock
#include <format>      // C++20：格式化时间点
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void EnableUtf8Console() {
#ifdef _WIN32
    std::system("chcp 65001 > nul");
#else
    (void)0;
#endif
}

void Section(const char* title) {
    std::printf("\n================ %s ================\n", title);
}

void SubSection(const char* title) {
    std::printf("\n---- %s ----\n", title);
}

// ===========================================================================
// ==============  第一部分：<cctype>  =======================================
// ===========================================================================

// ---------------------------------------------------------------------------
// 用 ctype 统计一串文本里各类字符的数量。
// ★ 每个字符都要先转成 unsigned char 再传给 isXxx —— 原因见下面 DemoCtype 的讲解。
// ---------------------------------------------------------------------------
struct CharStats {
    int alpha = 0;
    int digit = 0;
    int space = 0;
    int punct = 0;
    int upper = 0;
    int lower = 0;
    int other = 0;  // 注意：UTF-8 的每个中文字节都会落到这里
};

CharStats Analyze(std::string_view text) {
    CharStats st;
    for (const char raw : text) {
        const unsigned char c = static_cast<unsigned char>(raw);  // ★ 关键的一步
        if (std::isalpha(c)) {
            ++st.alpha;
            if (std::isupper(c)) ++st.upper;
            if (std::islower(c)) ++st.lower;
        } else if (std::isdigit(c)) {
            ++st.digit;
        } else if (std::isspace(c)) {
            ++st.space;
        } else if (std::ispunct(c)) {
            ++st.punct;
        } else {
            ++st.other;
        }
    }
    return st;
}

void DemoCharClassify() {
    Section("1. isalpha / isdigit / isspace / ispunct 分类统计");

    const std::string_view text = "Hello, World 2026! C/C++ Rocks.";
    const CharStats st = Analyze(text);

    std::printf("  被统计的文本：\"%.*s\"\n", static_cast<int>(text.size()), text.data());
    std::printf("    字母 %d（其中大写 %d，小写 %d）\n", st.alpha, st.upper, st.lower);
    std::printf("    数字 %d\n", st.digit);
    std::printf("    空白 %d\n", st.space);
    std::printf("    标点 %d\n", st.punct);
    std::printf("    其它 %d\n", st.other);
    std::printf("  合计 %d == 字符串长度 %zu ? %s\n",
                st.alpha + st.digit + st.space + st.punct + st.other, text.size(),
                (static_cast<std::size_t>(st.alpha + st.digit + st.space + st.punct + st.other) ==
                 text.size())
                    ? "是"
                    : "否");

    SubSection("逐字符分类表");
    std::printf("  %-6s %-10s %-6s %-6s %-6s %-6s\n", "字符", "isalnum", "isspace", "ispunct",
                "isupper", "islower");
    const char samples[] = {'A', 'z', '7', ' ', ',', '\t', '_', '-'};
    for (const char ch : samples) {
        const unsigned char c = static_cast<unsigned char>(ch);
        // 注意每个参数都是 int（非 0 表示真），直接当 bool 用也行
        std::printf("  %-6s %-10s %-6s %-6s %-6s %-6s\n",
                    ch == '\t' ? "\\t" : std::string(1, ch).c_str(), std::isalnum(c) ? "是" : "否",
                    std::isspace(c) ? "是" : "否", std::ispunct(c) ? "是" : "否",
                    std::isupper(c) ? "是" : "否", std::islower(c) ? "是" : "否");
    }
    std::printf("  其它成员：iscntrl（控制字符）、isgraph（可打印且非空格）、isprint、isxdigit。\n");
}

void DemoToupperTolower() {
    Section("2. toupper / tolower 与那个必须记住的 UB 坑");

    const char* kText = "Hello, World 2026! C/C++ Rocks.";

    std::string lower;
    std::string upper;
    for (const char* p = kText; *p != '\0'; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        lower.push_back(static_cast<char>(std::tolower(c)));
        upper.push_back(static_cast<char>(std::toupper(c)));
    }
    std::printf("  原串   ：%s\n", kText);
    std::printf("  小写化 ：%s\n", lower.c_str());
    std::printf("  大写化 ：%s\n", upper.c_str());
    std::printf("  非字母字符原样返回：toupper('7') = '%c'，toupper(' ') = '%c'\n",
                static_cast<char>(std::toupper(static_cast<unsigned char>('7'))),
                static_cast<char>(std::toupper(static_cast<unsigned char>(' '))));

    SubSection("★ 为什么必须先转 unsigned char（这是真实的 UB）");
    std::printf("  ctype 函数的形参是 int，但标准要求实参必须是：\n");
    std::printf("    (a) unsigned char 能表示的值（0..255），或者\n");
    std::printf("    (b) EOF（-1）。\n");
    std::printf("  MSVC 的 char 是【有符号】的，所以字节 0x80..0xFF 会变成负数 -128..-1，\n");
    std::printf("  直接传进去就是「非 (a) 非 (b)」-> 未定义行为。\n");
    std::printf("  这在实际项目里非常容易触发：UTF-8 的中文、Latin-1 的重音字母都在这个范围。\n");

    // 演示有符号 char 的实际取值
    const std::string_view chinese = "中文";
    std::printf("\n  实测：UTF-8 字符串 \"%.*s\" 的每个字节，作为 char 时的值：\n",
                static_cast<int>(chinese.size()), chinese.data());
    std::printf("   ");
    for (const char raw : chinese) {
        std::printf(" %4d", static_cast<int>(raw));  // 有符号，所以是负数
    }
    std::printf("\n  作为 unsigned char 时：");
    for (const char raw : chinese) {
        std::printf(" %4d", static_cast<int>(static_cast<unsigned char>(raw)));
    }
    std::printf("\n  -> 负值进了 isalpha 就是 UB。正确写法永远是这个：\n");
    std::printf("     std::isalpha(static_cast<unsigned char>(c))\n");
    std::printf("     顺便：上面统计里 \"中文\" 的两个字会被算进 other（UTF-8 每个汉字 3 字节）。\n");
    std::printf("     要正确处理多字节字符必须用专门的 Unicode 库（ICU）或 C++23 的 <text>。\n");
    std::printf("     附带实测：对负 y 值调用 isalpha 在 MSVC 上通常返回 false（没崩），\n");
    std::printf("     但结果不可移植也不可依赖 —— UB 的表现形式不保证。\n");
}

void DemoCtypeValidation() {
    Section("3. 用 ctype 做输入校验");

    const char* kIds[] = {"12345", "12a45", "", " 123", "0012"};

    SubSection("写法 A：ctype 逐字符判断（老派但通用）");
    for (const char* s : kIds) {
        bool allDigit = (*s != '\0');  // ★ 空串必须判成「不是纯数字」，否则循环一次都不进
        for (const char* p = s; *p != '\0'; ++p) {
            if (!std::isdigit(static_cast<unsigned char>(*p))) {
                allDigit = false;
                break;
            }
        }
        std::printf("    \"%s\" 是纯数字？%s\n", s, allDigit ? "是" : "否");
    }

    SubSection("写法 B：C++ 现代写法（std::ranges + from_chars）");
    for (const char* s : kIds) {
        const std::string_view sv(s);
        // all_of 表达「每个字符都是数字」更直观；空串默认返回 true，要单独处理
        const bool allDigit = !sv.empty() && std::all_of(sv.begin(), sv.end(), [](char raw) {
                                  return std::isdigit(static_cast<unsigned char>(raw)) != 0;
                              });

        // from_chars：直接尝试解析成 int，并检查「是否把整个串都吃掉了」
        int value = 0;
        const auto res = std::from_chars(sv.data(), sv.data() + sv.size(), value);
        const bool parseOk = (res.ec == std::errc{}) && (res.ptr == sv.data() + sv.size());
        std::printf("    \"%s\" all_of 判定=%s | from_chars 完全解析=%s（值 %d）\n", s,
                    allDigit ? "是" : "否", parseOk ? "是" : "否", value);
    }
    std::printf("  -> 只是「判断是不是数字」用 all_of 最清楚；\n");
    std::printf("     需要「顺便拿到数值」就用 from_chars，一次遍历同时完成校验和转换。\n");
    std::printf("  -> from_chars 不会跳过前导空白，\" 123\" 会失败 —— 这常常正是我们想要的行为。\n");
    std::printf("     如果要容忍前后空白，先自己 trim 掉（见 06_cpp_string.cpp）。\n");

    SubSection("写法 C：用 ctype 做简单的规范化");
    std::string raw = "  Hello   World  ";
    // 把连续空白压成一个空格，并去掉首尾空白
    std::string normalized;
    bool pendingSpace = false;
    bool started = false;
    for (const char ch : raw) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isspace(c)) {
            if (started) pendingSpace = true;
            continue;
        }
        if (pendingSpace) {
            normalized.push_back(' ');
            pendingSpace = false;
        }
        normalized.push_back(ch);
        started = true;
    }
    std::printf("  原串   ：\"%s\"（长度 %zu）\n", raw.c_str(), raw.size());
    std::printf("  规范化 ：\"%s\"（长度 %zu）\n", normalized.c_str(), normalized.size());
    std::printf("  这是配置文件解析、日志清洗里最常用的一个小工具函数。\n");
}

// ===========================================================================
// ==============  第二部分：<ctime> / <chrono>  =============================
// ===========================================================================

void DemoTimeBasics() {
    Section("5. time / localtime_s / strftime：C 风格时间处理");

    // time(nullptr)：返回自 1970-01-01 00:00:00 UTC 起的秒数（日历时间 / Unix 时间戳）
    const std::time_t now = std::time(nullptr);
    std::printf("  std::time(nullptr) = %lld（Unix 秒；time_t 在 64 位平台上是 64 位）\n",
                static_cast<long long>(now));
    std::printf("  注意：time_t 在 32 位平台上可能是 32 位，2038 年会溢出（著名的 2038 问题）。\n");

    // localtime 返回指向【函数内部静态缓冲区】的指针：非线程安全，下次调用就被覆盖。
    // MSVC 提供 localtime_s（参数顺序和 POSIX 的 localtime_r 不同，注意别搞混）。
    std::tm local{};
    if (localtime_s(&local, &now) != 0) {
        std::printf("  localtime_s 失败！\n");
        return;
    }
    std::printf("\n  localtime_s 得到的 tm 各字段（注意前两个反直觉的地方）：\n");
    std::printf("    tm_year = %d  -> 真实年份 = tm_year + 1900 = %d\n", local.tm_year,
                local.tm_year + 1900);
    std::printf("    tm_mon  = %d  -> 真实月份 = tm_mon + 1 = %d（0 表示一月！）\n", local.tm_mon,
                local.tm_mon + 1);
    std::printf("    tm_mday = %d（这个是从 1 开始的，不反直觉）\n", local.tm_mday);
    std::printf("    tm_hour = %d, tm_min = %d, tm_sec = %d\n", local.tm_hour, local.tm_min,
                local.tm_sec);
    std::printf("    tm_wday = %d（0=周日），tm_yday = %d（0=1 月 1 日）\n", local.tm_wday,
                local.tm_yday);
    std::printf("    tm_isdst = %d（>0 夏令时，0 非夏令时，<0 未知）\n", local.tm_isdst);

    // strftime：把 tm 按自定义格式写成字符串。做时间格式化优先用它。
    char buf[128] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %A %Z", &local);
    std::printf("\n  strftime(\"%%Y-%%m-%%d %%H:%%M:%%S %%A %%Z\") = %s\n", buf);
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &local);
    std::printf("  ISO 8601 风格（机器可读、可排序、适合写日志）          = %s\n", buf);
    std::strftime(buf, sizeof(buf), "%F %T", &local);
    std::printf("  等价的简写 %%F %%T（%%F = %%Y-%%m-%%d，%%T = %%H:%%M:%%S）        = %s\n",
                buf);
    std::printf("  常用格式符：%%Y 四位年 %%m 月 %%d 日 %%H 时 %%M 分 %%S 秒 %%A 星期全名 %%j 年内第几天\n");
    std::printf("  注意：strftime 返回「写入的字符数」，返回 0 表示放不下（不是成功）。\n");

    // UTC 对照
    std::tm utc{};
    if (gmtime_s(&utc, &now) == 0) {
        char ubuf[64] = {};
        std::strftime(ubuf, sizeof(ubuf), "%Y-%m-%d %H:%M:%S", &utc);
        std::printf("\n  gmtime_s（UTC）       = %s\n", ubuf);

        // 时区偏移的标准算法：把「本地时间字段」当成 UTC 再转回 time_t，
        // 得到的值与真实 UTC 时间戳之差就是偏移量（秒）。
        std::tm localCopy = local;
        localCopy.tm_isdst = -1;  // 让 mktime 自己判断夏令时
        const std::time_t localAsIfUtc = std::mktime(&localCopy);
        const long long offsetSeconds =
            static_cast<long long>(now) - static_cast<long long>(localAsIfUtc);
        std::printf("  本地时区相对 UTC 的偏移 = %+lld 秒（%+.2f 小时，%s）\n", offsetSeconds,
                    static_cast<double>(offsetSeconds) / 3600.0,
                    offsetSeconds == 0 ? "即 UTC" : (offsetSeconds > 0 ? "东时区" : "西时区"));
        std::printf("  说明：时间戳本身没有时区，时区只在「转成人类可读形式」时才出现。\n");
        std::printf("        跨时区系统务必统一存 UTC，显示时才转本地时间。\n");
        std::printf("        如果是 UTC+8 的系统，上面的偏移就是 +28800 秒；\n");
        std::printf("        本机若显示 +0，说明系统时区本身设成了 UTC（Windows 上很常见）。\n");
        std::printf("  ★ 顺带一个坑：strftime 的 %%Z / %%A 等「本地化」字段是按【活动代码页】\n");
        std::printf("    编码的，UTF-8 控制台下可能显示成乱码（本机实测 %%Z 就是乱码）。\n");
        std::printf("    要跨平台稳定输出时区名，请自己维护字符串映射表，别依赖 %%Z。\n");
    }
}

void DemoMktime() {
    Section("6. mktime：tm -> time_t，以及「日期加减」的正确姿势");

    // 构造 2026-03-01
    std::tm day{};
    day.tm_year = 2026 - 1900;  // ★ 必须减 1900
    day.tm_mon = 3 - 1;         // ★ 必须减 1
    day.tm_mday = 1;
    day.tm_hour = 0;
    day.tm_min = 0;
    day.tm_sec = 0;
    day.tm_isdst = -1;  // ★ 让 mktime 自己判断夏令时，写 0 可能差一小时

    const std::time_t base = std::mktime(&day);
    std::printf("  mktime(2026-03-01) = %lld\n", static_cast<long long>(base));
    std::printf("  ★ mktime 还会顺手把 day 里不合法的字段规范化（比如 tm_mday = 32 会变成下个月 1 号）。\n");

    // ★★ 正确的日期加减：用 64 位秒数，不要用 int
    constexpr long long kSecondsPerDay = 24LL * 3600LL;
    const long long shifted = static_cast<long long>(base) + 100LL * kSecondsPerDay;
    std::printf("  ★ 100 天后的时间戳 = %lld（用了 long long，避免 32 位溢出）\n", shifted);

    SubSection("反例：时间戳算术用 int 有多危险");
    // 用 volatile 挡住编译期常量折叠，好让我们在运行期观察溢出行为
    volatile int secondsPerDay32 = 24 * 3600;   // 86400，int 装得下
    volatile int days32 = 36524;                // 约 100 年
    const int product = secondsPerDay32 * days32;  // ★ int 溢出（UB / 实现定义）
    std::printf("    36524 天 * 86400 秒，用 int 相乘 = %d  <- 已经溢出成负数或垃圾值\n", product);
    std::printf("    正确的 64 位结果                        = %lld\n",
                static_cast<long long>(36524LL * 86400LL));
    std::printf("    正确写法：static_cast<long long>(base) + 100LL * 24 * 3600\n");
    std::printf("    记住：凡是和时间戳做算术，一律显式用 64 位（long long / int64_t）。\n");
    std::printf("    （2026 年当前时间戳约 1.77e9，还装得下 int；但加上 100 年就会溢出。）\n");

    SubSection("把时间戳转回日期");
    const std::time_t later = static_cast<std::time_t>(shifted);
    std::tm laterTm{};
    if (localtime_s(&laterTm, &later) == 0) {
        char b[64] = {};
        std::strftime(b, sizeof(b), "%Y-%m-%d %A", &laterTm);
        std::printf("    2026-03-01 之后 100 天 = %s\n", b);
    }

    SubSection("工程套路：日期加减用「规范化」技巧，比手算闰年可靠");
    // 想算「某个月的最后一天」：把下个月 1 号减 1 天
    std::tm firstOfNext{};
    firstOfNext.tm_year = 2026 - 1900;
    firstOfNext.tm_mon = 3;  // 4 月（0 起始）
    firstOfNext.tm_mday = 1;
    firstOfNext.tm_isdst = -1;
    const std::time_t nextMonth = std::mktime(&firstOfNext);
    std::tm lastDay{};
    if (localtime_s(&lastDay, &nextMonth) == 0) {
        lastDay.tm_mday -= 1;                            // 减一天
        std::mktime(&lastDay);                           // ★ 再规范化一次
        char b[64] = {};
        std::strftime(b, sizeof(b), "%Y-%m-%d", &lastDay);
        std::printf("    2026 年 3 月的最后一天 = %s（闰年也能自动算对）\n", b);
    }
    std::printf("    mktime 会自动处理「3 月 0 日」「2 月 30 日」这类越界字段，\n");
    std::printf("    所以日期加减可以「先粗暴地加减字段，再 mktime 规范化」。\n");
}

void DemoClocks() {
    Section("7. clock() 与 steady_clock：测的东西根本不一样");

    SubSection("clock() = 进程消耗的 CPU 时间");
    const std::clock_t c0 = std::clock();
    volatile long long sum = 0;  // volatile 防止循环被优化掉
    for (int i = 0; i < 20000000; ++i) {
        sum += i;
    }
    const std::clock_t c1 = std::clock();
    const double cpuSeconds =
        static_cast<double>(c1 - c0) / static_cast<double>(CLOCKS_PER_SEC);
    std::printf("  2000 万次加法，clock() 测得 CPU 时间 = %.3f 秒（CLOCKS_PER_SEC=%d）\n",
                cpuSeconds, static_cast<int>(CLOCKS_PER_SEC));
    std::printf("  sum = %lld（防止优化）\n", sum);

    SubSection("steady_clock = 单调递增的墙钟时间");
    const auto t0 = std::chrono::steady_clock::now();
    volatile long long sum2 = 0;
    for (int i = 0; i < 20000000; ++i) {
        sum2 += i;
    }
    const auto t1 = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::printf("  同样的循环，steady_clock 测得墙钟时间 = %lld ms（%.3f 秒）\n",
                static_cast<long long>(ms), static_cast<double>(ms) / 1000.0);

    std::printf("\n  ★ 三种时钟的分工（工程上必须分清）：\n");
    std::printf("    std::chrono::steady_clock  ：单调递增，不受系统时间调整影响 -> 测耗时用这个\n");
    std::printf("    std::chrono::system_clock  ：能映射到日历时间，可能被 NTP 往回拨 -> 取当前时间用\n");
    std::printf("    std::chrono::high_resolution_clock ：通常是上面两者的别名，不要作假设\n");
    std::printf("    std::clock()               ：CPU 时间。多线程程序里它可能比墙钟大很多，\n");
    std::printf("                                 睡眠/等待 IO 的时间也不算进去。\n");
    std::printf("  -> 结论：「这段代码多快」永远用 steady_clock，不要用 clock()。\n");

    SubSection("不同精度单位的取法");
    const auto tv0 = std::chrono::steady_clock::now();
    volatile int acc = 0;
    for (int i = 0; i < 100000; ++i) acc += i;
    const auto tv1 = std::chrono::steady_clock::now();
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(tv1 - tv0).count();
    const auto fsec = std::chrono::duration<double>(tv1 - tv0).count();
    std::printf("    微秒：%lld us；秒（浮点）：%.9f s；acc=%d\n", static_cast<long long>(us), fsec,
                acc);
    std::printf("    提示：Debug 下小片段计时容易被首次调用、内存分配等噪声淹没，\n");
    std::printf("          要么循环放大次数，要么改用 Release + 多次取中位数。\n");
}

void DemoModernChrono() {
    Section("8. C++20 <chrono> 日历类型：告别 +1900 / +1");

    using namespace std::chrono;

    // system_clock::now() 是 time_point；floor<seconds> 抹掉小数秒
    const auto now = floor<seconds>(system_clock::now());
    const auto today = floor<days>(now);  // 拿到「今天 00:00」这个时间点

    // sys_days 是「以天为单位的时间点」，可以直接构造 year_month_day
    const year_month_day ymd{today};
    std::printf("  今天的日期：%d-%02u-%02u（星期 %u）\n", static_cast<int>(ymd.year()),
                static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()),
                static_cast<unsigned>(weekday{today}.c_encoding()));
    std::printf("  ★ 对比 C 风格：不需要 tm_year + 1900，也不需要 tm_mon + 1，\n");
    std::printf("     类型系统直接告诉你「这是年、那是月」，写错编译不过。\n");

    // 日期字面量：2026y / March / 1d，可读性远超 mktime 那一堆赋值
    const year_month_day march1{2026y, March, 1d};
    std::printf("  日期字面量 year_month_day{2026y, March, 1d} = %d-%02u-%02u\n",
                static_cast<int>(march1.year()), static_cast<unsigned>(march1.month()),
                static_cast<unsigned>(march1.day()));

    // ★ 日期加减：sys_days + days{n}，纯整数运算，没有溢出和夏令时陷阱
    const sys_days base = sys_days{march1};
    const sys_days plus100 = base + days{100};
    const year_month_day result{plus100};
    std::printf("  2026-03-01 之后 100 天 = %d-%02u-%02u（%s）\n",
                static_cast<int>(result.year()), static_cast<unsigned>(result.month()),
                static_cast<unsigned>(result.day()), weekday{plus100}.ok() ? "计算成功" : "?");

    // 闰年判断：year 类型自带 is_leap
    std::printf("\n  闰年判断：year{2024}.is_leap() = %s，year{2026}.is_leap() = %s\n",
                year{2024}.is_leap() ? "true" : "false", year{2026}.is_leap() ? "true" : "false");
    std::printf("  某月有多少天：\n");
    for (const month m : {January, February, March, April}) {
        const year_month_day_last ymdl{year{2024}, month_day_last{m}};
        std::printf("    2024 年 %u 月有 %u 天\n", static_cast<unsigned>(m),
                    static_cast<unsigned>(ymdl.day()));
    }

    // 日期差：两个 sys_days 相减直接得到 days
    const sys_days newYear{2027y / January / 1d};
    const auto gap = newYear - today;
    std::printf("\n  距离 2027-01-01 还有 %lld 天\n", static_cast<long long>(gap.count()));

    // 时间点做运算
    const auto in90min = now + minutes{90};
    std::printf("  90 分钟后的时间戳（秒）= %lld\n",
                static_cast<long long>(in90min.time_since_epoch().count()));

    SubSection("std::format 直接格式化时间点（C++20）");
    std::printf("  std::format(\"{:%%Y-%%m-%%d %%H:%%M:%%S}\", now) = %s\n",
                std::format("{:%Y-%m-%d %H:%M:%S}", now).c_str());
    std::printf("  std::format 里 %%F / %%T 同样可用：%s\n", std::format("{:%F %T}", now).c_str());
    std::printf("  日期部分：%s\n", std::format("{:%Y-%m-%d %a}", today).c_str());
    std::printf("  -> 比 strftime 好在：类型安全、自动扩容、不会因为缓冲区不够而静默失败。\n");

    SubSection("三种「现在几点了」写法对照");
    // C 风格
    const std::time_t tt = system_clock::to_time_t(now);
    std::tm tmBuf{};
    localtime_s(&tmBuf, &tt);
    char cbuf[64] = {};
    std::strftime(cbuf, sizeof(cbuf), "%Y-%m-%d %H:%M:%S", &tmBuf);
    std::printf("    C 风格 strftime + tm ：%s\n", cbuf);

    // C++ 流 + put_time
    std::ostringstream os;
    os << std::put_time(&tmBuf, "%Y-%m-%d %H:%M:%S");
    std::printf("    C++ 流 + put_time   ：%s\n", os.str().c_str());

    // C++20 format
    std::printf("    C++20 std::format   ：%s\n", std::format("{:%Y-%m-%d %H:%M:%S}", now).c_str());
    std::printf("  -> 新代码选第三种：最短、最安全、最快。\n");
}

void DemoTimeUtilities() {
    Section("9. 几个工程里天天用的时间小工具");

    SubSection("把时间戳格式化成日志前缀（实测）");
    const auto LogStamp = [](std::chrono::system_clock::time_point tp) {
        using namespace std::chrono;
        return std::format("{:%Y-%m-%d %H:%M:%S}", floor<seconds>(tp));
    };
    std::printf("    %s  [INFO] 服务启动\n", LogStamp(std::chrono::system_clock::now()).c_str());
    std::printf("    %s  [WARN] 磁盘使用率 81%%\n", LogStamp(std::chrono::system_clock::now()).c_str());
    std::printf("    （带毫秒的写法：std::format(\"{:%%F %%T}.{:03d}\", 秒级时间点, 毫秒数)）\n");
    std::printf("    （上面这行里的 {:03d} 是 std::format 的占位符，%% 才是 printf 的）\n");

    SubSection("带毫秒的时间戳");
    const auto tp = std::chrono::system_clock::now();
    const auto msPart =
        std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;
    std::printf("    %s.%03lld\n", std::format("{:%F %T}", std::chrono::floor<std::chrono::seconds>(tp)).c_str(),
                static_cast<long long>(msPart.count()));

    SubSection("测量一段作用域的耗时（RAII 计时器）");
    struct ScopedTimer {
        const char* name;
        std::chrono::steady_clock::time_point start;
        explicit ScopedTimer(const char* n) : name(n), start(std::chrono::steady_clock::now()) {}
        ~ScopedTimer() {
            const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - start)
                                .count();
            std::printf("    [计时] %s 用了 %lld us\n", name, static_cast<long long>(us));
        }
    };
    {
        ScopedTimer timer("构造并排序 100 万个随机数");
        std::vector<int> v(1000000);
        unsigned seed = 12345;
        for (int& x : v) {
            seed = seed * 1103515245u + 12345u;  // 简单 LCG，仅为演示，不用于生产
            x = static_cast<int>(seed >> 16);
        }
        std::sort(v.begin(), v.end());
        std::printf("    排序完成，首元素 = %d，末元素 = %d\n", v.front(), v.back());
    }
    std::printf("    -> 这种「析构时自动打印耗时」的 RAII 计时器，是性能剖析里最省事的工具。\n");

    SubSection("休眠：std::this_thread::sleep_for");
    std::printf("    代码：std::this_thread::sleep_for(std::chrono::milliseconds(50));\n");
    std::printf("    注意：sleep 期间不消耗 CPU，但 clock() 也不会计入这段时间；\n");
    std::printf("          测「端到端延迟」必须用 steady_clock。\n");
}

}  // namespace

int main() {
    EnableUtf8Console();

    std::printf("==========================================================\n");
    std::printf(" 05_cctype_and_ctime.cpp —— <cctype> 与时间日期\n");
    std::printf("==========================================================\n");
    std::printf("\n########## 第一部分：<cctype> 字符分类 ##########\n");

    DemoCharClassify();
    DemoToupperTolower();
    DemoCtypeValidation();

    std::printf("\n########## 第二部分：<ctime> / <chrono> 时间日期 ##########\n");

    DemoTimeBasics();
    DemoMktime();
    DemoClocks();
    DemoModernChrono();
    DemoTimeUtilities();

    std::printf("\n================ 小结 ================\n");
    std::printf("1. ctype 函数的参数必须先转 unsigned char，否则 MSVC 上有符号负值进函数是 UB。\n");
    std::printf("2. 判「纯数字」用 all_of，判「能解析成数字」用 from_chars，别用 atoi 猜。\n");
    std::printf("3. tm_year 要 +1900，tm_mon 要 +1；C++20 的 year_month_day 不再需要这些偏移。\n");
    std::printf("4. time_t 算术一律用 64 位；localtime 非线程安全，用 localtime_s。\n");
    std::printf("5. 测代码耗时用 steady_clock；clock() 测的是 CPU 时间，语义不同。\n");
    std::printf("6. 格式化时间首选 std::format（格式串里写 冒号+百分号 F 空格 百分号 T），其次 strftime。\n");
    return 0;
}
