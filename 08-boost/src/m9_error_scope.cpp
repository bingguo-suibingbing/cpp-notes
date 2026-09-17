// =============================================================================
//  m9_error_scope.cpp —— 错误处理与作用域守卫
//
//  结论速览：
//    * std::error_code (C++11) 是从 boost::system::error_code 来的，现在用标准的。
//    * boost::system 仍有 std 没有的：与 errno 的集成、error_condition 分组，
//      以及被广泛使用的 boost::system::result<T>（C++23 才有 std::expected）。
//    * BOOST_SCOPE_EXIT：离开作用域时执行清理，天然逆序。STL 没有对应物。
// =============================================================================
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <boost/scope_exit.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/result.hpp>

#include "demo_api.h"
#include "demo_common.h"

using namespace demo;

// =============================================================================
// 自定义错误码：Boost 版和 STL 版写法几乎一样，正好直接对照
// =============================================================================
enum class AppErr { ok = 0, file_missing = 1, permission_denied = 2, disk_full = 3 };

// --- STL 版本
struct StlAppCategory : std::error_category {
    const char* name() const noexcept override { return "app"; }
    std::string message(int ev) const override {
        switch (static_cast<AppErr>(ev)) {
            case AppErr::ok:                return "成功";
            case AppErr::file_missing:      return "文件不存在";
            case AppErr::permission_denied: return "权限不足";
            case AppErr::disk_full:         return "磁盘已满";
        }
        return "未知错误";
    }
};
const std::error_category& stl_app_category() {
    static StlAppCategory c;
    return c;
}
std::error_code stl_make_error(AppErr e) {
    return std::error_code(static_cast<int>(e), stl_app_category());
}

// --- Boost 版本
struct BootAppCategory : boost::system::error_category {
    const char* name() const noexcept override { return "app"; }
    std::string message(int ev) const override {
        switch (static_cast<AppErr>(ev)) {
            case AppErr::ok:                return "成功";
            case AppErr::file_missing:      return "文件不存在";
            case AppErr::permission_denied: return "权限不足";
            case AppErr::disk_full:         return "磁盘已满";
        }
        return "未知错误";
    }
};
const boost::system::error_category& boot_app_category() {
    static BootAppCategory c;
    return c;
}
// 这里显式构造再返回，避免 MSVC 对 boost::system::error_code 的
// template operator T&() 产生推导歧义（C2783）
boost::system::error_code boot_make_error(AppErr e) {
    const boost::system::error_code ec(static_cast<int>(e), boot_app_category());
    return ec;
}

// =============================================================================
// "失败就返回 error_code"的函数 —— 真实项目里最常见的错误处理风格
// =============================================================================
std::error_code stl_open(const std::string& path, int& out_fd) {
    if (path.empty())           return stl_make_error(AppErr::file_missing);
    if (path == "/root/secret") return stl_make_error(AppErr::permission_denied);
    out_fd = 3;
    return std::error_code{};
}

boost::system::error_code boot_open(const std::string& path, int& out_fd) {
    if (path.empty())           return boot_make_error(AppErr::file_missing);
    if (path == "/root/secret") return boot_make_error(AppErr::permission_denied);
    out_fd = 3;
    return boost::system::error_code{};
}

void demo_m9_error_scope() {
    title("M9. 错误处理 / 作用域守卫：一半打平，scope_exit 是 STL 空白");

    // ===================================================================
    item("1) error_code：两种库的写法对照");
    {
        int fd = -1;
        auto sec = stl_open("/root/secret", fd);
        side("STL  ", "std::error_code          : value=" + std::to_string(sec.value()) +
                          " category=" + sec.category().name() + " message=" + sec.message());

        auto bec = boot_open("/root/secret", fd);
        side("Boost", "boost::system::error_code: value=" + std::to_string(bec.value()) +
                          " category=" + bec.category().name() + " message=" + bec.message());

        auto ok = stl_open("/tmp/a.txt", fd);
        side("STL  ", "成功时 std::error_code 转 bool 为 false: " +
                          std::string(ok ? "true(异常)" : "false(没有错误)"));
        line("");
        line("  结论：std::error_code 就是 Boost 版搬进标准库的，新代码用 std:: 即可。");
    }

    // ===================================================================
    item("2) 异常 vs 错误码：性能差距（异常不是免费的）");
    {
        constexpr int N = 200000;
        volatile int sink = 0;

        auto via_exception = [](const std::string& p) {
            if (p.empty()) throw std::runtime_error("file_missing");
            return 1;
        };
        auto via_ec = [](const std::string& p, int& out) {
            if (p.empty()) return stl_make_error(AppErr::file_missing);
            out = 1;
            return std::error_code{};
        };

        // ---- 成功路径
        double t_exc = time_ms([&] {
            for (int i = 0; i < N; ++i) sink += via_exception("/tmp/a");
        });
        double t_ec = time_ms([&] {
            int v = 0;
            for (int i = 0; i < N; ++i) { auto e = via_ec("/tmp/a", v); sink += v; (void)e; }
        });
        side("STL  ", "成功路径 抛异常风格  x" + std::to_string(N) + ": " + ms(t_exc));
        side("STL  ", "成功路径 error_code   x" + std::to_string(N) + ": " + ms(t_ec));
        line("  成功路径上两者接近（异常只要不抛就没有栈展开开销）。");
        line("");

        // ---- 失败路径
        double t_exc_fail = time_ms([&] {
            for (int i = 0; i < N; ++i) { try { sink += via_exception(""); } catch (...) { ++sink; } }
        });
        double t_ec_fail = time_ms([&] {
            int v = 0;
            for (int i = 0; i < N; ++i) { auto e = via_ec("", v); if (e) ++sink; }
        });
        side("STL  ", "失败路径 抛异常风格  x" + std::to_string(N) + ": " + ms(t_exc_fail));
        side("STL  ", "失败路径 error_code   x" + std::to_string(N) + ": " + ms(t_ec_fail));

        char buf[192];
        std::snprintf(buf, sizeof buf,
                      "  ==> 高频失败场景下，异常比 error_code 慢 %.0f 倍", t_exc_fail / t_ec_fail);
        line(buf);
        line("  注意看绝对值：每次抛异常大约 1~2 微秒。如果失败是罕见事件（比如 100 万次调用");
        line("  才失败一次），这点开销完全可以忽略，用异常的代码更清爽。");
        line("  只有失败率高（解析用户输入、探测性调用、网络重试）时，这个倍数才会变成真问题。");
        line("  所以：错误是常态的接口（解析、探测、网络重试）应该返回 error_code；");
        line("        错误是异常情况的接口（构造函数、不可恢复错误）才用异常。");
        (void)sink;
    }

    // ===================================================================
    item("3) boost::system::result<T>：C++23 std::expected 的先行版");
    {
        auto parse_port = [](const std::string& s) -> boost::system::result<int> {
            if (s.empty()) return boot_make_error(AppErr::file_missing);
            try {
                int v = std::stoi(s);
                if (v < 1 || v > 65535) return boot_make_error(AppErr::permission_denied);
                return v;
            } catch (...) {
                return boot_make_error(AppErr::disk_full);
            }
        };

        for (const char* in : {"8080", "99999", "abc"}) {
            auto r = parse_port(in);
            if (r.has_value())
                side("Boost", std::string("\"") + in + "\" -> 值 " + std::to_string(r.value()));
            else
                side("Boost", std::string("\"") + in + "\" -> 错误 " + r.error().message());
        }
        line("  std::expected 要 C++23 才有，且部分标准库实现还不完整；");
        line("  Boost.System 的 result<T> 在 C++11 起就能用，语义基本一致（还有 result<T&> 等扩展）。");
    }

    // ===================================================================
    item("4) BOOST_SCOPE_EXIT：逆序清理（STL 完全没有）");
    {
        std::vector<std::string> trace;

        {
            trace.push_back("enter");
            // 无论正常离开、return、还是抛异常都会执行；多个守卫按声明逆序执行
            BOOST_SCOPE_EXIT(&trace) {
                trace.push_back("cleanup#1 (最后声明 -> 最先执行)");
            } BOOST_SCOPE_EXIT_END

            BOOST_SCOPE_EXIT(&trace) {
                trace.push_back("cleanup#2");
            } BOOST_SCOPE_EXIT_END

            trace.push_back("body");
        }

        for (auto& t : trace) side("Boost", "trace: " + t);
        line("");
        line("  对比 STL 的替代方案：");
        line("   1) 手写 RAII 类：正确但啰嗦（每种清理都要一个类）；");
        line("   2) try/catch 包一层：容易漏、还可能吞掉异常；");
        line("   3) std::unique_ptr + 自定义删除器：只适合释放资源这一种形态。");
        line("  BOOST_SCOPE_EXIT 把离开作用域时执行的清理变成一行声明，");
        line("  并且天然满足逆序清理（后申请的先释放）这个正确性要求。");
    }

    // ===================================================================
    line();
    line("小结: error_code 用标准的，性能取舍上面已经量化；");
    line("      BOOST_SCOPE_EXIT / result<T> 是 STL 至今没有或才刚有的东西。");
}
