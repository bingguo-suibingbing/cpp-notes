// =============================================================================
//  main.cpp —— Boost vs STL 对比演示（VS 工程入口）
//
//  用法：
//    1. 用 Visual Studio 打开 Project6.slnx，配置选 Release | x64（跑性能对比）
//    2. Ctrl+F5 运行（不要用 F5 的调试窗口，ANSI 颜色显示不出来）
//    3. 按菜单选主题，或直接回车跑全部
//
//  Boost 路径配置见 boost.props（默认 D:\boost\boost-1.92.0-b2-nodocs\boost-1.92.0）
// =============================================================================
#include <iostream>
#include <string>

#include "demo_api.h"
#include "demo_common.h"

using namespace demo;

namespace {

// 每个演示模块包一层，任何一个模块出问题都不影响后续模块
template <class F>
void guard(const char* name, F&& f) {
    say(std::string("  >>> 进入模块 ") + name + " ...\n");
    try {
        f();
    } catch (const std::exception& e) {
        say(std::string("  !!! 模块 ") + name + " 抛出异常: " + e.what() + "\n");
    } catch (...) {
        say(std::string("  !!! 模块 ") + name + " 抛出未知异常\n");
    }
    say(std::string("  <<< 模块 ") + name + " 结束\n");
}

void print_banner() {
    say("\n");
    say("###############################################################################\n");
    say("#                                                                             #\n");
    say("#            Boost  vs  STL  —— 优缺点对照演示                                #\n");
    say("#            (Visual Studio / MSVC, C++20, Boost 1.92)                        #\n");
    say("#                                                                             #\n");
    say("###############################################################################\n");
    say("\n");
    say("  这个演示把 Boost 和 STL 放在一起跑，逐主题给出");
    say("  优点、缺点、以及\"实际项目该选哪个\"的结论。\n");
    say("\n");
    say("  说明：标 [Boost] 的行是用 Boost 实现的；标 [STL  ] 的行是用标准库实现的。");
    say("        两边都出现在同一个主题里，方便你直接对比代码量和运行结果。\n");
    say("\n");
    say("  注意：性能相关的小节请用 Release|x64 配置运行，Debug 下数据没有意义。\n");
}

void print_menu() {
    say("\n");
    say("-------------------------------------------------------------------------------\n");
    say("  请选择要演示的主题：\n");
    say("\n");
    say("    1. 字符串算法          boost::algorithm::string  vs  手写 STL      (Boost 赢)\n");
    say("    2. 格式化 / 数字转换   boost::format / lexical_cast  vs  std::format (STL 赢)\n");
    say("    3. 智能指针            intrusive_ptr / local_shared_ptr            (各有千秋)\n");
    say("    4. 容器                multi_index / circular_buffer / bimap       (Boost 独有)\n");
    say("    5. optional/variant    optional / variant2 / any                  (各有千秋)\n");
    say("    6. JSON / 配置解析     property_tree                               (Boost 独有)\n");
    say("    7. 正则 / 分词         boost::regex / tokenizer                    (Boost 赢)\n");
    say("    8. 堆 / 容器补充       heap / flat_map / small_vector             (Boost 独有)\n");
    say("    9. 错误处理 / 守卫     error_code 性能 / BOOST_SCOPE_EXIT          (各有千秋)\n");
    say("   10. 日期时间            Date_Time  vs  std::chrono                  (各有千秋)\n");
    say("   11. 总结               优缺点全景 + 决策指南 + 编译耗时实测\n");
    say("\n");
    say("    0 或直接回车 = 依次运行全部主题\n");
    say("    q = 退出\n");
    say("-------------------------------------------------------------------------------\n");
    say("  你的选择: ");
}

void run_all() {
    guard("m1_string",           demo_m1_string);
    guard("m2_format",           demo_m2_format);
    guard("m3_smart_ptr",        demo_m3_smart_ptr);
    guard("m4_container",        demo_m4_container);
    guard("m5_optional_variant", demo_m5_optional_variant);
    guard("m6_json",             demo_m6_json);
    guard("m7_regex_tokenizer",  demo_m7_regex_tokenizer);
    guard("m8_heap_flatmap",     demo_m8_heap_flatmap);
    guard("m9_error_scope",      demo_m9_error_scope);
    guard("m10_datetime",        demo_m10_datetime);
    guard("m11_summary",         demo_m11_summary);
}

void run_one(int choice) {
    switch (choice) {
        case 1:  demo_m1_string();            break;
        case 2:  demo_m2_format();            break;
        case 3:  demo_m3_smart_ptr();         break;
        case 4:  demo_m4_container();         break;
        case 5:  demo_m5_optional_variant();  break;
        case 6:  demo_m6_json();              break;
        case 7:  demo_m7_regex_tokenizer();   break;
        case 8:  demo_m8_heap_flatmap();      break;
        case 9:  demo_m9_error_scope();       break;
        case 10: demo_m10_datetime();         break;
        case 11: demo_m11_summary();          break;
        default:
            say("\n  无效选择，请输入 1-11。\n");
            break;
    }
}

}  // namespace

int main() {
    ensure_utf8_console();
    print_banner();

    for (;;) {
        print_menu();

        std::string input;
        if (!std::getline(std::cin, input)) break;

        // 去掉首尾空白
        while (!input.empty() && (input.back() == '\r' || input.back() == ' ')) input.pop_back();
        size_t i = 0;
        while (i < input.size() && input[i] == ' ') ++i;
        input = input.substr(i);

        if (input == "q" || input == "Q") {
            say("\n  再见。\n\n");
            break;
        }

        if (input.empty() || input == "0") {
            say("\n  >>> 依次运行全部主题（内容较长，建议用 Ctrl+F5 并向上翻看）...\n");
            run_all();
            say("\n  >>> 全部主题演示完毕。\n");
            say("\n  按回车继续（q 退出）: ");
            std::getline(std::cin, input);
            if (input == "q" || input == "Q") break;
            continue;
        }

        try {
            run_one(std::stoi(input));
        } catch (...) {
            say("\n  无效输入，请输入数字 1-11、0 或 q。\n");
        }

        say("\n  按回车继续（q 退出）: ");
        if (!std::getline(std::cin, input)) break;
        if (input == "q" || input == "Q") break;
    }
    return 0;
}
