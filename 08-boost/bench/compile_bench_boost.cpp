// =============================================================================
//  compile_bench_boost.cpp
//  编译耗时基准（实验组 B）：用 Boost 做和 A 完全一样的事情。
//
//  与 compile_bench_std.cpp 逐行对应。两者用同一行命令行编译，
//  耗时差就是 Boost 头文件带来的额外编译开销（Boost 最常被吐槽的缺点）。
// =============================================================================
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>
#include <boost/regex.hpp>
#include <boost/unordered_map.hpp>
#include <boost/variant2/variant.hpp>

namespace bench_boost {

using boost::algorithm::is_any_of;
using boost::algorithm::is_space;

std::string trim(const std::string& s) {
    return boost::algorithm::trim_copy(s);
}

std::string upper(const std::string& s) {
    return boost::to_upper_copy(s);
}

bool starts_with_ci(const std::string& s, const std::string& p) {
    return boost::algorithm::istarts_with(s, p);
}

std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> out;
    boost::algorithm::split(out, s, is_any_of(std::string(1, d)), boost::token_compress_off);
    return out;
}

std::string join(const std::vector<std::string>& v, const std::string& sep) {
    return boost::algorithm::join(v, sep);
}

int count_matches(const std::vector<std::string>& lines, const std::string& pat) {
    boost::regex re(pat);
    int n = 0;
    for (auto& l : lines) if (boost::regex_search(l, re)) ++n;
    return n;
}

boost::optional<int> to_int(const std::string& s) {
    try { return boost::lexical_cast<int>(s); } catch (...) { return boost::none; }
}

boost::variant2::variant<int, std::string> parse(const std::string& s) {
    if (s.empty()) return std::string("empty");
    return to_int(s).value_or(0);
}

boost::container::flat_map<int, std::string> sorted_map(const std::vector<int>& keys) {
    boost::container::flat_map<int, std::string> m;
    for (int k : keys) m[k] = std::to_string(k);
    return m;
}

boost::unordered_map<int, std::string> hash_map(const std::vector<int>& keys) {
    boost::unordered_map<int, std::string> m;
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
    acc += boost::variant2::holds_alternative<int>(parse("7")) ? 1 : 0;
    std::vector<int> keys = {3, 1, 2};
    acc += (double)sorted_map(keys).size() + (double)hash_map(keys).size();
    return acc;
}

}  // namespace bench_boost
