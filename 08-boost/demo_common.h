// =============================================================================
//  demo_common.h  —— 演示用的公共小工具
//  支持 VS 的 Debug / Release，如要跑性能对比请用 Release(x64) 配置。
// =============================================================================
#pragma once

#include <windows.h>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

namespace demo {

//-----------------------------------------------------------------------------
// 让控制台正确显示中文 + 支持 ANSI 颜色（VS 的"输出"窗口不认 ANSI，
// 所以请用 Ctrl+F5 在控制台窗口里跑，颜色才好看）
//-----------------------------------------------------------------------------
inline void ensure_utf8_console() {
    static const bool once = [] {
        SetConsoleOutputCP(CP_UTF8);
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        if (h && h != INVALID_HANDLE_VALUE) {
            DWORD mode = 0;
            if (GetConsoleMode(h, &mode))
                SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
        return true;
    }();
    (void)once;
}

// VS 的输出窗口 / 重定向到文件时是 GBK，直接输出 UTF-8 会乱码，
// 这里统一转成宽字符再交给 Windows 控制台。
inline void say(const std::string& utf8) {
    if (GetConsoleOutputCP() != CP_UTF8) {
        int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
        std::wstring w((size_t)n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), w.data(), n);
        std::wcout << w;
    } else {
        std::cout << utf8;
    }
    std::cout.flush();
}

//-----------------------------------------------------------------------------
// 输出排版
//-----------------------------------------------------------------------------
inline void title(const std::string& text) {
    ensure_utf8_console();
    say("\n");
    say("===============================================================================\n");
    say("  " + text + "\n");
    say("===============================================================================\n");
}

inline void item(const std::string& text) { say("\n--- " + text + " ---\n"); }
inline void line(const std::string& text = "") { say(text + "\n"); }

// 把"哪边是 Boost、哪边是 STL"标出来，方便对照
inline void side(const std::string& who, const std::string& text) {
    say("  [" + who + "] " + text + "\n");
}

// 计时：返回毫秒
template <class F>
double time_ms(F&& f) {
    auto t0 = std::chrono::steady_clock::now();
    f();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

inline std::string ms(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.1f ms", v);
    return buf;
}

}  // namespace demo
