// =============================================================================
//  m6_json.cpp —— 结构化数据：Boost 有，STL 完全没有
//
//  结论速览：
//    * C++20 的 STL 里没有任何 JSON/XML/INI 解析器，标准库至今没进。
//      要么引第三方（nlohmann / rapidjson），要么用 Boost。
//    * Boost 提供两代方案：
//        - boost::property_tree  : 泛型"属性树"，一个 API 通吃 JSON/XML/INI/INFO，
//                                  非常省事，但类型信息弱、性能一般、错误信息差。
//        - boost::json           : 1.75 起的新库，严格 JSON 语义、性能好得多。
//    * 这体现了 Boost 的"生态价值"：标准库缺的东西，它给你补齐。
// =============================================================================
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#ifdef DEMO_HAS_BOOST_JSON
#include <boost/json.hpp>
#endif

#include "demo_api.h"
#include "demo_common.h"

using namespace demo;
namespace pt = boost::property_tree;

void demo_m6_json() {
    title("M6. JSON / 配置解析：STL 完全没有的领域");

    // -------------------------------------------------------------------
    item("1) 现成的 JSON 文本（假装是从服务器/配置文件读来的）");
    {
        const std::string json_text = R"({
            "server": {
                "host": "127.0.0.1",
                "port": 8080,
                "tags": ["prod", "cn-north", "v2"]
            },
            "timeout_ms": 3000,
            "features": { "log": true, "cache": false, "retry": true }
        })";
        std::istringstream in(json_text);
        pt::ptree tree;
        pt::read_json(in, tree);

        side("Boost", "pt::read_json(is, tree) —— 只要 2 行就解析完了，不依赖任何第三方库");
    }

    // -------------------------------------------------------------------
    item("2) 用点号路径取值，并自带默认值");
    {
        const std::string json_text = R"({
            "server": { "host": "127.0.0.1", "port": 8080,
                        "tags": ["prod", "cn-north", "v2"] },
            "timeout_ms": 3000
        })";
        std::istringstream in(json_text);
        pt::ptree tree;
        pt::read_json(in, tree);

        // 基本类型转换交给 get<T>(path, default)
        std::string host = tree.get<std::string>("server.host", "localhost");
        int         port = tree.get<int>("server.port", 80);
        int         tout = tree.get<int>("timeout_ms", 1000);
        // 路径不存在也不抛异常 —— 直接拿默认值（配置文件里最有用的一点）
        int         miss = tree.get<int>("server.ssl.enabled", 0);
        std::string deep = tree.get<std::string>("a.b.c.d.e", "(路径不存在)");

        side("Boost", "server.host          = " + host);
        side("Boost", "server.port          = " + std::to_string(port));
        side("Boost", "timeout_ms           = " + std::to_string(tout));
        side("Boost", "server.ssl.enabled   = " + std::to_string(miss) + "  (缺字段，用默认值 0)");
        side("Boost", "a.b.c.d.e            = " + deep);
        line("  >>> 用点号路径 + 默认值，是 property_tree 写配置文件最省事的地方，");
        line("      比手写 if(json.contains(\"server\")) 一路判空要短得多。");
    }

    // -------------------------------------------------------------------
    item("3) 数组的遍历方式（注意：把数组当子节点集合看）");
    {
        const std::string json_text = R"({
            "server": { "tags": ["prod", "cn-north", "v2"] }
        })";
        std::istringstream in(json_text);
        pt::ptree tree;
        pt::read_json(in, tree);

        side("Boost", "数组元素没有名字，key 是空串 \"\"，用 get_child + 遍历:");
        int i = 0;
        for (auto& kv : tree.get_child("server.tags")) {
            side("     ", "[" + std::to_string(i++) + "] key=\"" + kv.first + "\"  value=" + kv.second.data());
        }
        line("  这是 property_tree 最反直觉的设计：JSON 数组被当成 key 为空的子节点列表，");
        line("  property_tree 本身不区分数组和对象，所以类型信息会丢失（见下一条）。");
    }

    // -------------------------------------------------------------------
    item("4) property_tree 的硬伤：类型信息丢失 / 错误信息差");
    {
        // 陷阱一：字符串 "8080" 和数字 8080 在 property_tree 里都是"文本"
        const std::string j1 = R"({"port": 8080})";
        const std::string j2 = R"({"port": "8080"})";
        pt::ptree t1, t2;
        { std::istringstream a(j1); pt::read_json(a, t1); }
        { std::istringstream b(j2); pt::read_json(b, t2); }
        int p1 = t1.get<int>("port");
        int p2 = t2.get<int>("port");
        side("Boost", "{\"port\": 8080}   -> get<int> = " + std::to_string(p1));
        side("Boost", "{\"port\": \"8080\"} -> get<int> = " + std::to_string(p2));
        side("Boost", "两者取出来一模一样 —— 数字/字符串的区别在解析时就没了，");
        side("Boost", "强类型 JSON 场景（比如要区分 \"1\" 和 1）就不够用。");

        // 陷阱二：类型不匹配时的报错（要运行到那一行才知道）
        const std::string j3 = R"({"port": "abc"})";
        pt::ptree t3;
        { std::istringstream c(j3); pt::read_json(c, t3); }
        try {
            (void)t3.get<int>("port");
        } catch (const pt::ptree_bad_data& e) {
            side("Boost", std::string("get<int> 失败: ") + e.what());
            line("  报错只告诉类型转换失败，不给行列号、不给 JSON 路径 —— 排查大配置文件很痛苦。");
        }

        // 陷阱三：JSON 语法错误
        const std::string bad = R"({"a": 1,})";   // 尾随逗号，严格 JSON 非法
        try {
            std::istringstream d(bad);
            pt::ptree t;
            pt::read_json(d, t);
            side("Boost", "尾随逗号竟然被接受了？读到的 a = " + t.get<std::string>("a", "?"));
        } catch (const pt::json_parser_error& e) {
            side("Boost", std::string("语法错误: ") + e.what());
        }
        side("STL  ", "标准库在这一领域是彻底空白 —— 你只能自己写或者引第三方。");
    }

    // -------------------------------------------------------------------
    item("5) 写 JSON（序列化）");
    {
        pt::ptree out;
        out.put("name", "Project6");
        out.put("version", "1.0.3");
        out.put("debug", false);
        out.put("compiler", "MSVC v145");

        pt::ptree arr;
        for (const char* tag : {"boost", "stl", "demo"}) {
            pt::ptree item_node;
            item_node.put("", tag);       // 数组元素：key 为空
            arr.push_back({"", item_node});
        }
        out.add_child("tags", arr);

        std::ostringstream os;
        pt::write_json(os, out, true);    // pretty print
        line("  [Boost] pt::write_json 输出（注意 tags 是数组）:");
        std::istringstream lines(os.str());
        for (std::string l; std::getline(lines, l);) line("      " + l);
    }

    // -------------------------------------------------------------------
    item("6) 那 std:: 这边怎么办？—— 标准库的答案是什么都没有");
    {
        line("  C++17/20/23 标准库里的选择：");
        line("   - 手写解析器        : 有 bug 风险、不支持转义/嵌套/Unicode");
        line("   - 引 nlohmann/json  : 最好用的第三方，但又是一份新的依赖");
        line("   - 引 rapidjson      : 性能最好");
        line("   - 用 Boost.JSON     : 已经在你手里，不用再引新依赖");
        line("");
        line("  所以 M6 这一块就是 Boost 存在的核心理由：");
        line("  标准库没有的能力，Boost 用一个统一的依赖帮你覆盖掉。");
    }

    // -------------------------------------------------------------------
    line();
    line("小结(Boost 赢在): JSON/XML/INI 解析 —— STL 完全空白，Boost 直接补上；");
    line("  但优先用 boost::json（强类型、报错好），property_tree 只在图省事时用。");
    line("  缺点: property_tree 类型信息弱、报错差、性能一般（这也是它被 boost::json 取代的原因）。");
}
