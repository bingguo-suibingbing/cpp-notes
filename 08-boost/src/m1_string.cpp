// =============================================================================
//  m1_string.cpp  —— Boost 优势区：字符串算法
//
//  结论速览：
//    * boost::algorithm::string 提供了 40+ 个"一行搞定"的字符串算法，
//      STL（C++20）到现在都没有这些：trim / to_upper / starts_with(带谓词) …
//    * Boost 的算法是 range 版（接受迭代器区间），所以 char[]、std::string、
//      std::vector<char> 全都能用。
//    * 代价：编译期模板展开多，头文件很重（这是 Boost 最常被吐槽的缺点）。
// =============================================================================
#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>

#include "demo_api.h"
#include "demo_common.h"

using namespace demo;

//-----------------------------------------------------------------------------
// 对照组：用纯 STL 手写 STL 里没有的东西
//-----------------------------------------------------------------------------
namespace stl_only {

std::string trim_copy(const std::string& s) {
    auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };
    auto b = std::find_if_not(s.begin(), s.end(), is_ws);
    auto e = std::find_if_not(s.rbegin(), s.rend(), is_ws).base();
    return (b < e) ? std::string(b, e) : std::string();
}

std::string to_upper_copy(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return (char)std::toupper(c); });
    return r;
}

// C++20 有 std::string::starts_with，但没有"大小写不敏感"那版
bool istarts_with(const std::string& s, const std::string& p) {
    if (s.size() < p.size()) return false;
    for (size_t i = 0; i < p.size(); ++i)
        if (std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)p[i])) return false;
    return true;
}

// 手写 split，还要自己处理空字段（C++ 标准库里根本没有 split）
std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == delim) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

}  // namespace stl_only

//-----------------------------------------------------------------------------
void demo_m1_string() {
    title("M1. 字符串算法：boost::algorithm vs 纯 STL");

    // -------------------------------------------------------------------
    item("1) trim —— 去掉首尾空白");
    {
        std::string raw = "  \t  Hello Boost  \r\n";

        side("STL ", "\"" + stl_only::trim_copy(raw) + "\"   <- 手写 8 行的 find_if + find_if_not");
        std::string b = raw;
        boost::algorithm::trim(b);
        side("Boost", "\"" + b + "\"   <- boost::algorithm::trim(buf) 一行，原地修改");
        side("Boost", "还有 trim_left / trim_right / trim_copy / trim_copy_if(自定义谓词)");
    }

    // -------------------------------------------------------------------
    item("2) 大小写转换");
    {
        std::string s = "Hello Boost vs Standard Library";
        side("STL ", "\"" + stl_only::to_upper_copy(s) + "\"   <- std::transform + std::toupper");
        side("Boost", "\"" + boost::to_upper_copy(s) + "\"   <- boost::to_upper_copy(s)");
        side("Boost", "注意: boost::to_upper() 会原地改 std::string，语义比 transform 更直白");
    }

    // -------------------------------------------------------------------
    item("3) 带谓词的 starts_with / ends_with / contains");
    {
        std::string log = "ERROR: disk full";
        side("STL ", std::string("log.starts_with(\"ERROR\") -> ") +
                        (log.starts_with("ERROR") ? "true" : "false") +
                        "   <- C++20 才有，且区分大小写");
        side("STL ", "大小写不敏感的版本要自己写: " +
                        std::string(stl_only::istarts_with(log, "error") ? "true" : "false"));
        side("Boost", "boost::istarts_with(log, \"error\") -> " +
                          std::string(boost::istarts_with(log, "error") ? "true" : "false") +
                          "   <- 直接给谓词版");
        side("Boost", "boost::contains(log, \"disk\") -> " +
                          std::string(boost::contains(log, "disk") ? "true" : "false") +
                          "   (STL 至今没有 contains，只能 find != npos)");
        side("Boost", "还有 istarts_with / iends_with / istarts_with / contains + find_nth 等一堆");
    }

    // -------------------------------------------------------------------
    item("4) split —— 标准库里根本没有这个函数");
    {
        std::string csv = "id,name,score,";  // 注意结尾还多一个空字段
        std::vector<std::string> std_parts = stl_only::split(csv, ',');
        side("STL ", "手写循环 -> " + std::to_string(std_parts.size()) + " 个字段: ");
        for (auto& p : std_parts) side("     ", "[" + p + "]");

        std::vector<std::string> bparts;
        boost::algorithm::split(bparts, csv, boost::is_any_of(","));
        side("Boost", "boost::split(parts, s, boost::is_any_of(\",\")) -> " +
                          std::to_string(bparts.size()) + " 个字段");
        side("Boost", "但注意: split 会把结尾的空字段丢掉，要保留得用 token_compress_off 变体");

        std::vector<std::string> kept;
        boost::algorithm::split(kept, csv, boost::is_any_of(","),
                                boost::token_compress_off);
        side("Boost", "加 boost::token_compress_off -> " + std::to_string(kept.size()) +
                          " 个字段（保留末尾空字段）");
        side("Boost", "分隔符还能是谓词/字符集: is_any_of(\",;\") / is_space() / is_from_range('a','z')");
    }

    // -------------------------------------------------------------------
    item("5) 分割 + 拼接（join）一条龙");
    {
        std::string path = "C:/Users/Waj/Desktop/Project6";
        std::vector<std::string> segs;
        boost::algorithm::split(segs, path, boost::is_any_of("/"));
        side("Boost", "split 后共 " + std::to_string(segs.size()) + " 段");
        side("Boost", "boost::algorithm::join(segs, \" | \") -> " + boost::algorithm::join(segs, " | "));
        side("STL ", "join 也没有，要自己写循环控制分隔符位置（新手常在这里多打一个分隔符）");
    }

    // -------------------------------------------------------------------
    item("6) replace_all / erase_all —— 一行替换全部");
    {
        std::string t = "a-b-c-d";
        boost::algorithm::replace_all(t, "-", "+");
        side("Boost", "replace_all(t, \"-\", \"+\") -> \"" + t + "\"");
        std::string u = "2024-01-02";
        boost::algorithm::erase_all(u, "-");
        side("Boost", "erase_all(u, \"-\") -> \"" + u + "\"");
        side("STL ", "std::string::replace 只替换一次，replace_all 要自己写 while(find) 循环");
    }

    // -------------------------------------------------------------------
    item("7) 谓词: 判断整串是否符合规则");
    {
        side("Boost", "boost::all(\"abc\", boost::is_lower())          -> " +
                          std::string(boost::all(std::string("abc"), boost::is_lower()) ? "true" : "false"));
        side("Boost", "boost::all(\"aBc\", boost::is_lower())          -> " +
                          std::string(boost::all(std::string("aBc"), boost::is_lower()) ? "true" : "false"));
        side("Boost", "boost::all(\"123\", boost::is_digit())          -> " +
                          std::string(boost::all(std::string("123"), boost::is_digit()) ? "true" : "false"));
        side("STL ", "对应写法: std::all_of(s.begin(), s.end(), ::islower) —— 长度差不多，但少了字符串语义");
    }

    // -------------------------------------------------------------------
    line();
    line("小结(Boost 赢在): 字符串处理是 Boost 相对 STL 优势最明显的地方，");
    line("  因为 STL 的 <string> 只有容器语义，没有文本处理语义。");
    line("  缺点同样明显: <boost/algorithm/string.hpp> 展开后编译变慢、模板报错难读。");
}
