// =============================================================================
//  compile_bench_std.cpp
//  编译耗时基准（对照组 A）：只使用标准库的等价功能。
//
//  与 compile_bench_boost.cpp 逐行对应，唯一区别是做同样的事情时
//  A 用 std::，B 用 boost::。
//  用同一行命令行编译两个文件，耗时差就是 Boost 头文件带来的编译开销。
// =============================================================================
#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace bench_std {

std::string trim(const std::string& s) {
    auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };
    auto b = std::find_if_not(s.begin(), s.end(), is_ws);
    auto e = std::find_if_not(s.rbegin(), s.rend(), is_ws).base();
    return (b < e) ? std::string(b, e) : std::string();
}

std::string upper(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return (char)std::toupper(c); });
    return r;
}

bool starts_with_ci(const std::string& s, const std::string& p) {
    if (s.size() < p.size()) return false;
    for (size_t i = 0; i < p.size(); ++i)
        if (std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)p[i])) return false;
    return true;
}

std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, d)) out.push_back(tok);
    return out;
}

std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string r;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) r += sep;
        r += v[i];
    }
    return r;
}

int count_matches(const std::vector<std::string>& lines, const std::string& pat) {
    std::regex re(pat);
    int n = 0;
    for (auto& l : lines) if (std::regex_search(l, re)) ++n;
    return n;
}

std::optional<int> to_int(const std::string& s) {
    try { return std::stoi(s); } catch (...) { return std::nullopt; }
}

std::variant<int, std::string> parse(const std::string& s) {
    if (s.empty()) return std::string("empty");
    return to_int(s).value_or(0);
}

std::map<int, std::string> sorted_map(const std::vector<int>& keys) {
    std::map<int, std::string> m;
    for (int k : keys) m[k] = std::to_string(k);
    return m;
}

std::unordered_map<int, std::string> hash_map(const std::vector<int>& keys) {
    std::unordered_map<int, std::string> m;
    for (int k : keys) m[k] = std::to_string(k);
    return m;
}

double use_everything() {
    std::vector<std::string> lines = {"  Hello ", " Boost ", " vs STL "};
    double acc = 0;
    for (auto& l : lines) {
        auto t = trim(l);
        acc += (double)upper(t).size();
        if (starts_with_ci(t, "hello")) acc += 1;
        auto parts = split(t, ' ');
        acc += (double)join(parts, "|").size();
    }
    acc += count_matches(lines, R"(\w+)");
    acc += to_int("42").value_or(0);
    acc += std::holds_alternative<int>(parse("7")) ? 1 : 0;
    std::vector<int> keys = {3, 1, 2};
    acc += (double)sorted_map(keys).size() + (double)hash_map(keys).size();
    return acc;
}

}  // namespace bench_std
