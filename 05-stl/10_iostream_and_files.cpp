// ============================================================================
// 10_iostream_and_files.cpp  ——  iostream 输入输出 与 文件读写
//
// 演示主题：
//   1. cin / cout / cerr / clog 的分工与缓冲差异
//   2. <iomanip> 格式化：setw / setprecision / setfill / fixed / scientific /
//      hex / boolalpha / left / right / showpoint / put_money 等
//   3. ★ cin 的四种失败状态（eofbit / failbit / badbit / goodbit）
//      与 clear() / ignore() 的正确配合 —— 新手最常卡住的地方
//   4. getline 与 >> 混用的正确姿势
//   5. ★ 完整可运行的健壮输入函数模板（读整数 / 读整数带范围 / 读整行 / 读菜单选择）
//   6. 文件流：ofstream / ifstream / fstream，文本与二进制，RAII 自动关闭
//   7. std::filesystem（C++17）：读写文件、路径操作、目录遍历、exists / file_size
//   8. stringstream：字符串解析与拼接（含一个简易 CSV 解析器）
//   9. std::format 与 iostream 的对比；什么时候 printf 仍然合适
//  10. 输出缓冲与性能：'\n' vs std::endl、sync_with_stdio、cin.tie
//
// 关键结论：
//   - std::endl 会【强制刷新缓冲】，在循环里用 endl 会显著变慢；只想换行就用 '\n'。
//   - cin >> x 失败后，流进入 fail 状态并【一直保持失败】，后续所有读取都会立即失败。
//     必须 clear() 清状态 + ignore() 清掉导致失败的那些字符，两者缺一不可。
//   - std::getline 遇到「行尾是 eof 但没有换行符」的最后一行会成功返回；但如果在
//     读数字之后直接 getline，会读到残留的 '\n'（得到空串）—— 这是最常见的坑。
//   - 文件流是 RAII 的：析构时自动关闭。所以不要手写 close()，让作用域结束去关。
//   - std::filesystem 的路径操作完全跨平台，不要再手拼 "/" 和 "\\"。
//
// 说明：本文件大量使用 std::istringstream 模拟“用户输入”，
//       这样在没有键盘输入的环境（比如构建脚本用空 stdin 运行）下也能完整演示。
//       真正的交互式读法见第 5 节的函数模板（它们同样可以配合 istringstream 使用）。
// ============================================================================

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>  // C++17：跨平台文件系统
#include <format>      // C++20
#include <fstream>
#include <iomanip>
#include <ios>         // std::streamsize / std::ios_base
#include <iostream>
#include <limits>      // std::numeric_limits
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

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

using Clock = std::chrono::steady_clock;

// ===========================================================================
// 1. 四个标准流
// ===========================================================================
void DemoStandardStreams() {
    Section("1. cin / cout / cerr / clog 的分工");

    std::cout << "  std::cout  : 标准输出，带缓冲，可被重定向（正常结果走这里）\n";
    std::cout << "  std::cerr  : 标准错误，默认【不缓冲】且与 cout 绑定，用于错误信息\n";
    std::cout << "  std::clog  : 标准错误，但【带缓冲】，用于日志（性能比 cerr 好）\n";
    std::cout << "  std::cin   : 标准输入，默认与 cout 绑定（读之前自动 flush cout）\n";

    // 演示 cerr 与 clog 的实际输出（它们走 stderr，控制台里看不到区别）
    std::cerr << "  [cerr] 这条走 stderr，不缓冲：进程崩溃时它还能出来\n";
    std::clog << "  [clog] 这条也走 stderr，但带缓冲：性能更好，崩溃时可能丢\n";

    SubSection("为什么这个区分重要");
    std::cout << "    1) 程序输出被重定向到文件时，cout 的内容进文件；\n";
    std::cout << "       cerr/clog 仍然打到终端 —— 日志和结果可以分流。\n";
    std::cout << "    2) 崩溃排查：cerr 不缓冲，所以崩溃前一刻的错误一定写出来了；\n";
    std::cout << "       clog 缓冲的内容可能还没落盘就丢了。\n";
    std::cout << "    3) CLI 工具约定：正常结果 -> stdout，诊断信息 -> stderr。\n";
    std::cout << "       这样用户可以用 tool > result.txt 只拿结果。\n";

    SubSection("C 与 C++ 的输出混用");
    std::printf("    这行是 printf 写的\n");
    std::cout << "    这行是 cout 写的\n";
    std::cout << "    ★ 默认情况下两者是同步的（sync_with_stdio(true)），顺序正常。\n";
    std::cout << "      但同步有额外开销；如果全程只用 C++ 流，可以关掉换取性能：\n";
    std::cout << "        std::ios::sync_with_stdio(false);\n";
    std::cout << "        std::cin.tie(nullptr);\n";
    std::cout << "      ★ 关掉之后【不要】再混用 printf / cout，输出顺序会乱。\n";

    SubSection("实测：'\\n' vs std::endl 的差距（写入真实文件）");
    std::cout << "    stringstream 是纯内存缓冲，测不出差别；下面写到真实文件里对比。\n";
    {
        constexpr int kN = 20000;
        const fs::path fileA = "05_stl_endl_newline.tmp";
        const fs::path fileB = "05_stl_endl_endl.tmp";

        long long usNl = 0;
        {
            std::ofstream f(fileA);
            const auto t0 = Clock::now();
            for (int i = 0; i < kN; ++i) {
                f << i << '\n';  // 只换行，交给缓冲区
            }
            usNl = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
        }

        long long usEndl = 0;
        {
            std::ofstream f(fileB);
            const auto t0 = Clock::now();
            for (int i = 0; i < kN; ++i) {
                f << i << std::endl;  // ★ 每次都要 flush 到设备
            }
            usEndl =
                std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
        }

        std::cout << "      " << kN << " 次 << i << '\\n'      : " << usNl << " us\n";
        std::cout << "      " << kN << " 次 << i << std::endl : " << usEndl << " us\n";
        if (usNl > 0) {
            std::cout << "      -> std::endl 约为 '\\n' 的 " << std::fixed << std::setprecision(2)
                      << static_cast<double>(usEndl) / static_cast<double>(usNl) << " 倍耗时\n";
            std::cout << std::defaultfloat << std::setprecision(6);
        }
        std::error_code rmEc;
        fs::remove(fileA, rmEc);
        fs::remove(fileB, rmEc);
        std::cout << "    ★ 规则：只在「需要立刻看到输出」时用 std::endl（崩溃前日志、交互提示语），\n";
        std::cout << "      其余一律用 '\\n'。本机实测差距约 3 倍（Debug 与 Release 都是这个量级），磁盘越慢差距越大。\n";
    }
}

// ===========================================================================
// 2. iomanip 格式化
// ===========================================================================
void DemoManipulators() {
    Section("2. <iomanip> 格式化");

    const double pi = 3.14159265358979;
    const double big = 12345678.9;
    const double tiny = 0.000012345;
    const int num = 255;

    SubSection("浮点精度：setprecision 是「有效数字」，不是「小数位」");
    std::cout << "    默认 setprecision(6)：" << pi << '\n';
    std::cout << "    setprecision(10)     ：" << std::setprecision(10) << pi << '\n';
    std::cout << std::defaultfloat;
    std::cout << "    ★ 不配合 fixed 时，setprecision(3) 是把 3.14159 变成 3.14，\n";
    std::cout << "      但把 1234.5 变成 1.23e+03 —— 有效数字，不是小数位。\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "    fixed + setprecision(2)：" << pi << " / " << big << '\n';
    std::cout << std::scientific << std::setprecision(3);
    std::cout << "    scientific + 3        ：" << pi << " / " << tiny << '\n';
    std::cout << std::defaultfloat << std::setprecision(6);

    SubSection("★ setprecision / fixed 等状态是【粘住】的");
    std::cout << "    上面改了 fixed/scientific 之后，后面所有输出都受影响 ——\n";
    std::cout << "    这就是 iostream 相比 std::format 最大的缺点：有全局（流级）状态。\n";
    std::cout << "    正确习惯：用完就恢复（defaultfloat / setprecision(6) / setfill(' ')）。\n";

    SubSection("setw：宽度（★ 只影响【下一次】输出，不粘）");
    std::cout << "    [" << std::setw(8) << 42 << "]\n";
    std::cout << "    [" << std::setw(8) << 42 << "]\n";  // setw 已失效
    std::cout << "    ★ setw 是 iomanip 里唯一「一次性」的操纵符，每次输出都要重写。\n";
    std::cout << "    [" << std::setw(8) << std::setfill('0') << 42 << "]\n";  // setfill 会粘
    std::cout << "    [" << std::setw(8) << 42 << "]\n";
    std::cout << std::setfill(' ');

    SubSection("对齐");
    std::cout << "    默认（右对齐）：[" << std::setw(10) << "hi" << "]\n";
    std::cout << "    左对齐 left    ：[" << std::left << std::setw(10) << "hi" << "]\n";
    std::cout << "    内部对齐 internal：[" << std::internal << std::setw(10) << -42 << "]\n";
    std::cout << std::right;
    std::cout << "    ★ left / right / internal 都会【粘住】，记得恢复 std::right。\n";

    SubSection("进制与布尔");
    std::cout << "    十进制 dec：" << std::dec << num << '\n';
    std::cout << "    十六进制 hex：" << std::hex << num << '\n';
    std::cout << "    带前缀 showbase：" << std::showbase << num << '\n';
    std::cout << "    大写 hex + uppercase：" << std::uppercase << num << '\n';
    std::cout << "    八进制 oct：" << std::oct << num << '\n';
    std::cout << std::dec << std::noshowbase << std::nouppercase;
    std::cout << "    ★ 进制也是粘住的！混用 std::hex 之后忘了 dec，后面所有整数都变十六进制 ——\n";
    std::cout << "      这是调试时「输出全是 a b c d e f」的经典原因。\n";
    std::cout << "    布尔默认：" << true << " / " << false << '\n';
    std::cout << "    boolalpha：" << std::boolalpha << true << " / " << false << '\n';
    std::cout << std::noboolalpha;
    std::cout << "    ★ 输出 bool 时永远显式用 boolalpha，或者用 std::format(\"{}\", b)。\n";

    SubSection("其它常用操纵符");
    std::cout << "    showpoint（强制显示小数点）：" << std::showpoint << std::fixed
              << std::setprecision(1) << 3.0 << '\n';
    std::cout << std::noshowpoint << std::defaultfloat;
    std::cout << "    noskipws（不跳过空白，配合 >> 读字符时用）\n";
    std::cout << "    quoted（读写带引号的字符串，处理转义）：\n";
    std::cout << "      std::cout << std::quoted(\"a\\\"b\") -> " << std::quoted("a\"b") << '\n';
    std::cout << "    ★ quoted 在写/读 CSV、日志字段时很有用（自动加引号并转义内部引号）。\n";

    SubSection("对齐表格：一个可运行的报告输出");
    std::cout << std::left << std::setw(14) << "名称" << std::right << std::setw(10) << "数量"
              << std::setw(14) << "金额" << '\n';
    std::cout << std::string(38, '-') << '\n';
    const struct {
        const char* name;
        int qty;
        double amount;
    } rows[] = {{"apple", 3, 12.5}, {"banana", 128, 1234.56}, {"cherry", 7, 0.99}};
    for (const auto& r : rows) {
        std::cout << std::left << std::setw(14) << r.name << std::right << std::setw(10) << r.qty
                  << std::fixed << std::setprecision(2) << std::setw(14) << r.amount << '\n';
    }
    std::cout << std::defaultfloat << std::setprecision(6);
    std::cout << "    ★ 对比 std::format 的写法（见第 9 节），后者不需要管流状态。\n";
}

// ===========================================================================
// 3. cin 的失败状态
// ===========================================================================
void DemoStreamStates() {
    Section("3. ★★ cin 的四种状态与 clear() / ignore()");

    SubSection("四个状态位");
    std::cout << "    goodbit : 一切正常（good() == true）\n";
    std::cout << "    eofbit  : 读到输入结束（eof() == true）\n";
    std::cout << "    failbit : 格式错误（比如往 int 里读 \"abc\"）—— 流仍可用，但后续读会立即失败\n";
    std::cout << "    badbit  : 严重错误（设备故障、缓冲损坏），流基本废了\n";
    std::cout << "    ★ 一旦 failbit 或 badbit 被置位，fail() 返回 true，\n";
    std::cout << "      之后所有 >> 操作【立即失败且不消费任何字符】—— 这就是「死循环」的根源。\n";

    SubSection("复现：读整数时输入字母会发生什么");
    // 用「两行」输入，才能演示「清理干净之后还能继续读下一行」
    std::istringstream input("abc\n42\n");
    int n = 0;
    std::cout << "    模拟输入是两行：\"abc\" 和 \"42\"\n";
    std::cout << "    第一次 cin >> n（期望 int）：\n";
    std::cout << "      操作成功？" << ((input >> n) ? "是" : "否") << '\n';
    std::cout << "      n 的值      = " << n << "（★ 未改动，失败的读取不写值）\n";
    std::cout << "      good()=" << input.good() << " eof()=" << input.eof()
              << " fail()=" << input.fail() << " bad()=" << input.bad() << '\n';
    std::cout << "    第二次 cin >> n：\n";
    std::cout << "      操作成功？" << ((input >> n) ? "是" : "否")
              << "  <- ★ 失败，而且 'a' 还在流里没被消费\n";
    std::cout << "    ★ 所以「读失败就 continue 再试」会变成【死循环】：\n";
    std::cout << "       流永远是 fail 状态，'a' 永远消费不掉，每次都是立即失败。\n";

    SubSection("修复：clear() + ignore() 缺一不可（逐步验证）");
    // ★ 把状态机完整走一遍：每一步都打印状态和「下一个字符是什么」
    input.clear();  // ★ 第一步：清状态位（不会动数据）
    std::cout << "    第一步 input.clear()：fail()=" << input.fail() << "，下一个字符 peek()='"
              << static_cast<char>(input.peek()) << "'\n";
    std::cout << "      ★ 只是清了状态位，'a' 还在流里 —— 现在读还是会立刻失败。\n";

    input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');  // ★ 第二步：丢到行尾
    std::cout << "    第二步 input.ignore(最大值, '\\n')：下一个字符 peek()='"
              << static_cast<char>(input.peek()) << "'\n";
    std::cout << "      ★ \"abc\" 和它后面的换行都被丢掉了，现在流指针指向下一行的 '4'。\n";

    if (input >> n) {
        std::cout << "    现在再 >> n：成功，n = " << n << "  <- 正确读到了下一行的 42\n";
    } else {
        std::cout << "    现在再 >> n：仍然失败（不应该发生）\n";
    }
    std::cout << "    ★ 结论：clear() 修的是「状态位」，ignore() 修的是「流里的垃圾字符」，\n";
    std::cout << "      两件事都要做，只做一件都不行。这是所有健壮输入函数的基础。\n";

    SubSection("ignore 的参数含义");
    std::cout << "    ignore(n, delim)：最多丢弃 n 个字符，或者直到遇到 delim（delim 也被丢掉）。\n";
    std::cout << "      ignore()                 -> 丢 1 个字符\n";
    std::cout << "      ignore(1)                -> 同上\n";
    std::cout << "      ignore(100, '\\n')        -> 最多丢 100 个，遇到换行就停\n";
    std::cout << "      ignore(max, '\\n')        -> 丢掉整行（最常用，max 表示「不设上限」）\n";
    std::cout << "    ★ 必须用 numeric_limits<streamsize>::max() 而不是一个魔法数字（比如 1000）：\n";
    std::cout << "      用户可能粘贴一整个 10000 字符的字符串，魔法数字会导致清理不干净。\n";
    std::cout << "    ★ ignore 返回流引用，可以链式调用。\n";

    SubSection("eof 与 fail 的关系（另一个常见误解）");
    std::istringstream fin("1 2");
    int a = 0, b = 0, c = 0;
    fin >> a >> b;
    std::cout << "    从 \"1 2\" 读两个 int：a=" << a << " b=" << b << "，此时 eof()=" << fin.eof()
              << '\n';
    fin >> c;
    std::cout << "    再读第三个：成功？" << ((fin >> c) ? "是" : "否")
              << "，此时 eof()=" << fin.eof() << " fail()=" << fin.fail() << '\n';
    std::cout << "    ★ 关键：读到结尾时 eofbit 和 failbit【同时】被置位。\n";
    std::cout << "      所以 while (!cin.eof()) { cin >> x; ... } 是【错误】写法：\n";
    std::cout << "      最后一次失败后还会进循环，用未更新的 x 处理一遍数据（经典 bug）。\n";
    std::cout << "      正确写法：while (cin >> x) { ... }  —— 让 >> 的返回值当条件。\n";

    SubSection("clear() 的另一个用途：清掉 eof 以复用流");
    fin.clear();
    std::cout << "    fin.clear() 之后 fail()=" << fin.fail() << "（但数据质量没变，位置仍在末尾）\n";
    std::cout << "    ★ 想「重新读同一段数据」必须同时 seekg 回到开头。\n";
}

// ===========================================================================
// 4. getline 与 >> 混用
// ===========================================================================
void DemoGetlineMixing() {
    Section("4. getline 与 >> 混用的正确姿势");

    SubSection("复现问题");
    std::istringstream input("30\nZhang San\n");
    int age = 0;
    std::string name;
    input >> age;               // 只消费了 "30"，'\n' 留在流里
    std::getline(input, name);  // ★ 立刻读到那个残留的 '\n'，name 变成空串
    std::cout << "    >> 读 age 得到 " << age << '\n';
    std::cout << "    紧接着 getline 得到 \"" << name << "\"（长度 " << name.size()
              << "）<- ★ 空的！\n";

    SubSection("修复方法 1：读完 >> 之后丢掉行尾");
    std::istringstream input2("30\nZhang San\n");
    int age2 = 0;
    std::string name2;
    input2 >> age2;
    input2.ignore(std::numeric_limits<std::streamsize>::max(), '\n');  // ★ 关键一行
    std::getline(input2, name2);
    std::cout << "    ignore 之后 getline 得到 \"" << name2 << "\"  <- 正确\n";

    SubSection("修复方法 2（推荐）：全程只用 getline，自己解析");
    std::cout << "    这样永远不会遇到「残留换行」的问题：\n";
    std::istringstream input3("30\nZhang San\n42\nLi Si\n");
    std::string line;
    int count = 0;
    while (std::getline(input3, line)) {
        if (line.empty()) continue;  // 跳过空行
        int parsed = 0;
        const auto res = std::from_chars(line.data(), line.data() + line.size(), parsed);
        if (res.ec == std::errc{} && res.ptr == line.data() + line.size()) {
            std::cout << "      解析到数字：" << parsed << '\n';
            ++count;
        } else {
            std::cout << "      解析到文本：" << line << '\n';
        }
    }
    std::cout << "    共 " << count << " 个数字，另外那些是文本行。\n";
    std::cout << "    ★ 这是配置文件 / 逐行协议解析的标准写法：一行一次处理，状态简单。\n";

    SubSection("★★ 一个更隐蔽的坑：循环条件用 !eof()");
    std::cout << "    错误写法：\n";
    std::cout << "      while (!in.eof()) {\n";
    std::cout << "          std::getline(in, line);\n";
    std::cout << "          process(line);      // ★ 最后一次会处理一个空/重复的 line\n";
    std::cout << "      }\n";
    std::cout << "    正确写法：\n";
    std::cout << "      while (std::getline(in, line)) {\n";
    std::cout << "          process(line);\n";
    std::cout << "      }\n";
    std::cout << "    ★ getline 的返回值是流引用，可转 bool（等价于 !fail()），\n";
    std::cout << "      所以直接把它当条件就同时覆盖了「文件结束」和「读取失败」。\n";

    SubSection("判空行 / 丢注释行的实用过滤");
    std::istringstream cfg("# 这是注释\n\nkey1 = value1\n  \nkey2 = value2\n");
    std::string raw;
    int kept = 0;
    while (std::getline(cfg, raw)) {
        // 去掉行尾 \r（Windows 文件在 Unix 上读、或反之）
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        // 去掉首尾空白
        const auto first = raw.find_first_not_of(" \t");
        if (first == std::string::npos) continue;      // 全空白行
        const std::string_view sv(raw);
        if (sv[first] == '#') continue;                // 注释行
        std::cout << "      保留：" << raw << '\n';
        ++kept;
    }
    std::cout << "    共保留 " << kept << " 行（注释行和空行都被过滤掉了）。\n";
}

// ===========================================================================
// 5. 健壮输入函数模板
// ===========================================================================
// ---------------------------------------------------------------------------
// ★★ 这一节是本文件最重要的部分：可以直接抄进真实项目。
//    每个函数都做到：输入失败不崩、不卡死、能给调用方明确结果。
// ---------------------------------------------------------------------------

// 读一个整数。成功返回 true 并写入 out；失败时清状态 + 清行，返回 false。
// input 参数化是为了能用 istringstream 演示；真实代码里传 std::cin 即可。
bool ReadInt(std::istream& in, int& out) {
    if (in >> out) {
        return true;
    }
    // ★ 失败路径的两步清理，缺一不可：
    in.clear();                                                          // 1) 清状态位
    in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');         // 2) 丢掉这一行剩余内容
    return false;
}

// 读一个整数并限制范围。超出范围也算失败（同样要清行）。
bool ReadIntInRange(std::istream& in, int& out, int lo, int hi) {
    int value = 0;
    if (!ReadInt(in, value)) {
        return false;
    }
    if (value < lo || value > hi) {
        return false;  // 范围错误：ReadInt 已经消费了一整行，这里不需要再清
    }
    out = value;
    return true;
}

// 读一整行（允许空行，只要不是流末尾）。
bool ReadLineSafe(std::istream& in, std::string& out) {
    if (!std::getline(in, out)) {
        return false;
    }
    // 顺手去掉行尾的 '\r'，兼容跨平台文本文件
    if (!out.empty() && out.back() == '\r') {
        out.pop_back();
    }
    return true;
}

// 读一行非空文本（空白行会被拒绝，让调用方重试）。
bool ReadNonEmptyLine(std::istream& in, std::string& out) {
    std::string line;
    if (!ReadLineSafe(in, line)) {
        return false;
    }
    const auto first = line.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return false;  // 全空白
    }
    const auto last = line.find_last_not_of(" \t");
    out = line.substr(first, last - first + 1);  // ★ 顺便 trim
    return true;
}

// 读一个 double。失败同样清状态 + 清行。
bool ReadDouble(std::istream& in, double& out) {
    if (in >> out) {
        return true;
    }
    in.clear();
    in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return false;
}

// 读菜单选择：循环直到拿到合法输入或流结束。
// 返回 false 表示「流结束了」（比如用户按了 Ctrl+Z 或者输入被重定向到空文件）。
bool ReadMenuChoice(std::istream& in, int& choice, int lo, int hi, std::ostream& prompt) {
    std::string line;
    while (true) {
        prompt << "  请输入 [" << lo << ", " << hi << "] 之间的编号（q 退出）：";
        prompt.flush();  // ★ 提示语没有换行，必须 flush，否则重定向时看不到
        if (!std::getline(in, line)) {
            return false;  // 输入结束
        }
        // 去掉首尾空白
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            prompt << "  （空输入，请重新输入）\n";
            continue;
        }
        const auto last = line.find_last_not_of(" \t\r\n");
        const std::string trimmed = line.substr(first, last - first + 1);
        if (trimmed == "q" || trimmed == "Q" || trimmed == "quit") {
            return false;  // 用户主动退出
        }
        // ★ 用 from_chars 而不是 >> : 不会受 locale 影响，也不会留下残留字符
        int value = 0;
        const auto res = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), value);
        if (res.ec != std::errc{} || res.ptr != trimmed.data() + trimmed.size()) {
            prompt << "  「" << trimmed << "」不是合法整数，请重新输入\n";
            continue;
        }
        if (value < lo || value > hi) {
            prompt << "  " << value << " 超出范围 [" << lo << ", " << hi << "]，请重新输入\n";
            continue;
        }
        choice = value;
        return true;
    }
}

void DemoRobustInput() {
    Section("5. ★★ 健壮输入函数模板（可直接抄）");

    SubSection("ReadInt：读整数，失败清状态 + 清行");
    {
        std::istringstream in("abc\n42\n");
        int v = 0;
        std::cout << "    模拟输入 \"abc\" 然后 \"42\"：\n";
        std::cout << "      第一次 ReadInt -> " << (ReadInt(in, v) ? "成功" : "失败") << '\n';
        std::cout << "      第二次 ReadInt -> " << (ReadInt(in, v) ? "成功" : "失败")
                  << "，值 = " << v << '\n';
        std::cout << "    ★ 关键点：失败后流是干净的（状态已清、行已丢），\n";
        std::cout << "      所以下一次读取可以正常工作 —— 这是「重试」能生效的前提。\n";
    }

    SubSection("ReadIntInRange：带范围校验");
    {
        std::istringstream in("999\n3\n");
        int v = 0;
        std::cout << "    要求 [1, 5]，模拟输入 \"999\" 然后 \"3\"：\n";
        std::cout << "      ReadIntInRange(999) -> " << (ReadIntInRange(in, v, 1, 5) ? "通过" : "拒绝")
                  << '\n';
        std::cout << "      ReadIntInRange(3)   -> " << (ReadIntInRange(in, v, 1, 5) ? "通过" : "拒绝")
                  << "，值 = " << v << '\n';
    }

    SubSection("ReadDouble：读浮点");
    {
        std::istringstream in("3.14\nnot-a-number\n2.5\n");
        double d = 0.0;
        for (int i = 0; i < 3; ++i) {
            const bool ok = ReadDouble(in, d);
            std::cout << "      ReadDouble -> " << (ok ? "成功" : "失败");
            if (ok) std::cout << "，值 = " << d;
            std::cout << '\n';
        }
        std::cout << "    ★ 注意 operator>> 对 double 会受 locale 影响（德语下 \"3,14\" 才是合法输入）。\n";
        std::cout << "      要严格解析协议数据，用 std::from_chars（见 06_cpp_string.cpp）。\n";
    }

    SubSection("ReadNonEmptyLine：读非空文本并 trim");
    {
        std::istringstream in("   \n  hello world  \n");
        std::string s;
        std::cout << "      第一次（全空白行） -> " << (ReadNonEmptyLine(in, s) ? "成功" : "拒绝")
                  << '\n';
        const bool ok = ReadNonEmptyLine(in, s);
        std::cout << "      第二次             -> " << (ok ? "成功" : "拒绝") << "，值 = \"" << s
                  << "\"\n";
    }

    SubSection("ReadMenuChoice：完整的菜单输入循环");
    {
        std::istringstream in("abc\n99\n2\n");
        int choice = 0;
        std::cout << "    模拟用户依次输入 \"abc\"、\"99\"、\"2\"（合法范围 [1,3]）：\n";
        // ★ 直接传 std::cin 也完全一样，这里用 istringstream 是为了可重复运行
        if (ReadMenuChoice(in, choice, 1, 3, std::cout)) {
            std::cout << "  最终选择：" << choice << '\n';
        } else {
            std::cout << "  输入结束\n";
        }
        std::cout << "    ★ 这个函数体现了健壮输入的全部要点：\n";
        std::cout << "      1) 用 getline 读整行（不会有残留字符）；\n";
        std::cout << "      2) 手动 trim 后判空；\n";
        std::cout << "      3) 用 from_chars 解析并检查「是否消费了整行」；\n";
        std::cout << "      4) 范围校验；\n";
        std::cout << "      5) 流结束时明确返回 false，而不是死循环；\n";
        std::cout << "      6) 每次给用户明确的错误原因。\n";
    }

    SubSection("★ 为什么不用 while (!(cin >> x)) { cin.clear(); cin.ignore(...); } 一把梭");
    std::cout << "    那种写法能用，但：\n";
    std::cout << "      1) 无法区分「格式错误」「范围错误」「输入结束」；\n";
    std::cout << "      2) 无法给用户提示具体原因；\n";
    std::cout << "      3) 在管道/重定向场景下会无限循环（因为清完之后还是 EOF）。\n";
    std::cout << "    把读入逻辑封装成返回 bool 的函数，调用方就能明确处理这三种情况。\n";

    SubSection("★ 如果真的要「循环重试直到拿到合法输入」");
    std::cout << "    也不能无限循环 —— 必须处理流结束：\n";
    std::istringstream in("bad\nbad\n");
    int v = 0;
    int attempts = 0;
    while (attempts < 5) {
        ++attempts;
        if (ReadInt(in, v)) {
            std::cout << "    读到 " << v << '\n';
            break;
        }
        std::cout << "    第 " << attempts << " 次失败\n";
        if (in.eof()) {
            std::cout << "    检测到输入结束 -> 跳出循环（★ 不写这个判断就是死循环）\n";
            break;
        }
    }
    std::cout << "    ★ 真实程序里的写法：读失败 + eof 就退出（或者用默认值），\n";
    std::cout << "      不能指望用户一定会输入正确的东西。\n";
}

// ===========================================================================
// 6. 文件流
// ===========================================================================
void DemoFileStreams() {
    Section("6. 文件流：ofstream / ifstream / fstream");

    const fs::path textFile = "05_stl_iostream_demo.txt";
    const fs::path binFile = "05_stl_iostream_demo.bin";
    const fs::path csvFile = "05_stl_iostream_demo.csv";

    SubSection("RAII：文件在析构时自动关闭");
    {
        std::ofstream out(textFile);  // 构造函数打开
        if (!out) {                   // ★ 必须检查是否打开成功（用 !out 或 out.is_open()）
            std::cout << "    打开失败：" << textFile.string() << '\n';
            return;
        }
        out << "第一行：纯文本\n";
        out << "第二行：数字 " << 42 << " 和浮点 " << 3.14 << '\n';
        out << "第三行：中文与符号 !@#\n";
        // ★ 不写 out.close()：作用域结束析构时自动关闭并 flush
    }  // <- 这里文件已经关闭
    std::cout << "    写入完成（靠 RAII 自动关闭，不需要手写 close）\n";

    SubSection("读文件：三种逐行读法");
    {
        std::ifstream in(textFile);
        if (!in) {
            std::cout << "    读取失败\n";
            return;
        }
        std::string line;
        int no = 0;
        while (std::getline(in, line)) {  // ★ 推荐写法
            ++no;
            std::cout << "      [" << no << "] " << line << '\n';
        }
        std::cout << "    ★ 注意 while 条件直接用 getline 的返回值（可转 bool）。\n";
    }

    SubSection("打开模式：in / out / app / ate / trunc / binary");
    std::cout << "    std::ios::in      只读（ifstream 默认）\n";
    std::cout << "    std::ios::out     只写（ofstream 默认，且隐含 trunc）\n";
    std::cout << "    std::ios::app     追加（每次写都定位到末尾）\n";
    std::cout << "    std::ios::ate     打开后立即定位到末尾（但可以再 seek 回来）\n";
    std::cout << "    std::ios::trunc   截断（清空文件）\n";
    std::cout << "    std::ios::binary  二进制模式（Windows 上禁止 \\n <-> \\r\\n 转换）\n";
    {
        // app 模式：不会清空已有内容
        std::ofstream app(textFile, std::ios::app);
        app << "第四行：app 模式追加的\n";
    }
    {
        std::ifstream in(textFile);
        std::string line;
        int no = 0;
        while (std::getline(in, line)) {
            ++no;
        }
        std::cout << "    app 追加后文件共 " << no << " 行\n";
    }
    std::cout << "    ★ 文本模式下 Windows 会把 '\\n' 写成 \"\\r\\n\"，读回来时再转回去。\n";
    std::cout << "      所以「用文本模式写、用二进制模式读」会看到多余的 \\r —— 必须模式一致。\n";

    SubSection("二进制读写：write / read");
    {
        struct Record {
            int id;
            double value;
            char tag[8];
        };
        const Record recs[3] = {{1, 1.5, "one"}, {2, 2.5, "two"}, {3, 3.5, "three"}};

        std::ofstream out(binFile, std::ios::binary);  // ★ 二进制写
        out.write(reinterpret_cast<const char*>(recs), sizeof(recs));
        out.close();

        std::ifstream in(binFile, std::ios::binary);
        Record readBack[3] = {};
        in.read(reinterpret_cast<char*>(readBack), sizeof(readBack));
        // gcount() 返回上一次 read 实际读到的字节数 —— 判断是否读完必须用它
        const std::streamsize got = in.gcount();
        std::cout << "    写入 " << sizeof(recs) << " 字节，读回 " << got << " 字节\n";
        for (const auto& r : readBack) {
            std::cout << "      id=" << r.id << " value=" << r.value << " tag=" << r.tag << '\n';
        }
        std::cout << "    ★ 二进制读写只对「平凡可拷贝类型」安全（见 03_cstring_and_memory.cpp）。\n";
        std::cout << "      含 std::string / std::vector 成员的结构体绝不能这样直接 write/read。\n";
    }

    SubSection("定位：seekg / tellg / seekp / tellp");
    {
        std::ifstream in(textFile, std::ios::binary | std::ios::ate);
        const std::streampos size = in.tellg();  // ate 打开 -> 一开始就在末尾，tellg 就是大小
        in.seekg(0);
        std::cout << "    用 ios::ate + tellg() 取文件大小 = " << size << " 字节\n";
        in.seekg(0, std::ios::beg);
        std::string firstLine;
        std::getline(in, firstLine);
        std::cout << "    seekg(0) 之后读第一行：" << firstLine << '\n';
        std::cout << "    当前读取位置 tellg() = " << in.tellg() << '\n';
        std::cout << "    ★ seekg 是「读指针」（g = get），seekp 是「写指针」（p = put）。\n";
        std::cout << "      fstream 同时有两者，ifstream 只有 g，ofstream 只有 p。\n";
    }

    SubSection("错误处理：检查 is_open / fail / bad");
    {
        std::ifstream missing("this_file_does_not_exist_12345.txt");
        std::cout << "    打开一个不存在的文件：\n";
        std::cout << "      is_open() = " << missing.is_open() << '\n';
        std::cout << "      fail()    = " << missing.fail() << '\n';
        std::cout << "    ★ ifstream 打开失败【不抛异常】，只是把 failbit 置位。\n";
        std::cout << "      要让它抛异常可以开异常掩码：\n";
        std::cout << "        in.exceptions(std::ifstream::failbit | std::ifstream::badbit);\n";
        std::cout << "      之后打开失败会抛 std::ios_base::failure。\n";
        std::cout << "    ★ 工程建议：默认不抛异常 + 显式 if (!in) 检查，比异常更好控制。\n";
    }

    SubSection("清理演示文件");
    std::error_code ec;
    std::cout << "    删除文本文件：" << (fs::remove(textFile, ec) ? "成功" : "失败") << '\n';
    std::cout << "    删除二进制文件：" << (fs::remove(binFile, ec) ? "成功" : "失败") << '\n';

    // 生成一个真实 CSV 供第 7、8 节使用
    {
        std::ofstream csv(csvFile);
        csv << "name,qty,price\n";
        csv << "apple,3,12.50\n";
        csv << "banana,128,3.75\n";
        csv << "cherry,7,199.99\n";
    }
    std::cout << "    已生成 " << csvFile.string() << " 供后面两节使用\n";
}

// ===========================================================================
// 7. std::filesystem
// ===========================================================================
void DemoFilesystem() {
    Section("7. std::filesystem（C++17）：跨平台文件操作");

    SubSection("路径操作：完全跨平台，不要手拼分隔符");
    const fs::path p = "data/logs/app.log";
    std::cout << "    path = " << p.string() << '\n';
    std::cout << "      .filename()          = " << p.filename().string() << '\n';
    std::cout << "      .stem()              = " << p.stem().string() << "（不带扩展名）\n";
    std::cout << "      .extension()         = " << p.extension().string() << '\n';
    std::cout << "      .parent_path()       = " << p.parent_path().string() << '\n';
    std::cout << "      .has_extension()     = " << p.has_extension() << "（返回 bool 打印成 0/1）\n";
    std::cout << "    用 / 运算符拼路径（推荐，自动处理分隔符）：\n";
    const fs::path joined = fs::path("data") / "logs" / "app.log";
    std::cout << "      fs::path(\"data\") / \"logs\" / \"app.log\" = " << joined.string() << '\n';
    std::cout << "    ★ 在 Windows 上 .string() 给出的分隔符可能是 '\\'，\n";
    std::cout << "      打印给人看用 .string()；写进配置文件/协议时用 .generic_string()（统一 '/'）。\n";
    std::cout << "      .generic_string() = " << joined.generic_string() << '\n';

    SubSection("读写文件（整块读写，比流更省事）");
    const fs::path csvFile = "05_stl_iostream_demo.csv";
    if (fs::exists(csvFile)) {
        // 读：一次性把整个文件读进 string，比逐行 read 更快（一次系统调用）
        std::ifstream in(csvFile, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();  // ★ rdbuf() 是「把整个流拷到另一个流」的惯用法
        std::string content = ss.str();
        const auto crCount = static_cast<std::size_t>(
            std::count(content.begin(), content.end(), '\r'));
        std::cout << "    fs::file_size() = " << fs::file_size(csvFile) << " 字节\n";
        std::cout << "    内容里有 " << crCount << " 个 '\\r'";
        if (crCount > 0) {
            std::cout << "（★ 文本模式写出来的是 \\r\\n 行尾）\n";
        } else {
            std::cout << "（本机流实现没有做 \\n -> \\r\\n 转换）\n";
        }
        // ★ 顺手统一换行：这一步在跨平台读文本文件时几乎总要写
        content.erase(std::remove(content.begin(), content.end(), '\r'), content.end());
        std::cout << "    去掉 '\\r' 之后 " << content.size() << " 字节，内容：\n";
        std::istringstream lines(content);
        std::string line;
        while (std::getline(lines, line)) {
            std::cout << "      " << line << '\n';
        }
        std::cout << "    ★ 一次性读完适合小文件（几 MB 以内）；大文件应该流式逐行处理，\n";
        std::cout << "      否则内存会被整个文件撑爆。\n";
        std::cout << "    ★ 教训：文本模式写出来的文件在 Windows 上是 \\r\\n 行尾；\n";
        std::cout << "      用 binary 模式读回来就会带着 '\\r' —— 要么读写模式一致，\n";
        std::cout << "      要么读完统一去掉 '\\r'（推荐后者，能同时兼容别人给的文件）。\n";
    }

    SubSection("exists / is_regular_file / is_directory / file_size");
    std::cout << "    fs::exists(csv)             = " << fs::exists(csvFile) << '\n';
    std::cout << "    fs::is_regular_file(csv)    = " << fs::is_regular_file(csvFile) << '\n';
    std::cout << "    fs::is_directory(csv)       = " << fs::is_directory(csvFile) << '\n';
    std::cout << "    fs::is_directory(\".\")       = " << fs::is_directory(".") << '\n';
    std::cout << "    fs::file_size(csv)          = " << fs::file_size(csvFile) << '\n';
    std::cout << "    ★ 这些函数都有「抛异常」和「带 error_code」两个版本：\n";
    std::cout << "        fs::exists(p)        // 出错抛 filesystem_error\n";
    std::cout << "        fs::exists(p, ec)    // 出错写 ec，不抛\n";
    std::cout << "      在「文件可能不存在/没权限」的正常分支里，用 ec 版本更合适。\n";
    std::error_code ec;
    const bool existsNoThrow = fs::exists("nope_nope.txt", ec);
    std::cout << "    带 ec 的版本：exists(\"nope_nope.txt\") = " << existsNoThrow
              << "，ec = " << ec.message() << '\n';

    SubSection("目录操作：create_directories / 遍历");
    const fs::path tempDir = "05_stl_fs_demo/sub";
    std::error_code dirEc;
    fs::create_directories(tempDir, dirEc);  // ★ 会递归创建所有缺失的父目录
    std::cout << "    create_directories(\"" << tempDir.generic_string() << "\") -> "
              << (dirEc ? dirEc.message() : "成功") << '\n';
    std::cout << "    is_directory = " << fs::is_directory(tempDir) << '\n';

    // 造几个文件用于遍历
    for (int i = 1; i <= 3; ++i) {
        std::ofstream f(tempDir / ("file" + std::to_string(i) + ".txt"));
        f << "content " << i << '\n';
    }
    std::cout << "    目录遍历（directory_iterator，非递归）：\n";
    for (const auto& entry : fs::directory_iterator(tempDir)) {
        std::cout << "      " << entry.path().filename().string()
                  << "  (" << entry.file_size() << " 字节)\n";
    }
    std::cout << "    递归遍历（recursive_directory_iterator）：\n";
    for (const auto& entry : fs::recursive_directory_iterator("05_stl_fs_demo")) {
        std::cout << "      " << entry.path().generic_string();
        if (entry.is_directory()) std::cout << "  [目录]";
        std::cout << '\n';
    }
    std::cout << "    ★ 遍历时如果目录里文件很多，建议用带 error_code 的版本，\n";
    std::cout << "      否则碰到没权限的子目录会直接抛异常中断整个遍历。\n";

    SubSection("copy / rename / remove / remove_all");
    const fs::path copyTarget = tempDir / "file1_copy.txt";
    fs::copy_file(tempDir / "file1.txt", copyTarget, fs::copy_options::overwrite_existing, dirEc);
    std::cout << "    copy_file -> " << (dirEc ? dirEc.message() : "成功") << '\n';
    const fs::path renamed = tempDir / "file2_renamed.txt";
    fs::rename(tempDir / "file2.txt", renamed, dirEc);
    std::cout << "    rename    -> " << (dirEc ? dirEc.message() : "成功") << '\n';
    fs::remove(copyTarget, dirEc);
    std::cout << "    remove    -> " << (dirEc ? dirEc.message() : "成功") << '\n';
    const std::uintmax_t removed = fs::remove_all("05_stl_fs_demo", dirEc);
    std::cout << "    remove_all 递归删除 " << removed << " 个文件/目录 -> "
              << (dirEc ? dirEc.message() : "成功") << '\n';
    std::cout << "    ★ remove 只能删空目录；删非空目录要用 remove_all（★ 小心使用）。\n";

    SubSection("常用封装：安全的「读整个文本文件」");
    const auto ReadWholeFile = [](const fs::path& path) -> std::optional<std::string> {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };
    const auto text = ReadWholeFile(csvFile);
    std::cout << "    ReadWholeFile(csv) 成功？" << (text.has_value() ? "是" : "否")
              << "，长度 " << (text ? text->size() : 0) << " 字节\n";
    std::cout << "    ★ 返回 optional<string> 而不是靠异常或空串表示失败：\n";
    std::cout << "      空文件（合法）和打开失败（非法）能区分开。\n";

    std::error_code cleanupEc;
    fs::remove(csvFile, cleanupEc);
    std::cout << "    清理演示 CSV 文件：" << (cleanupEc ? "失败" : "成功") << '\n';
}

// ===========================================================================
// 8. stringstream
// ===========================================================================
void DemoStringStream() {
    Section("8. stringstream：字符串解析与拼接");

    SubSection("ostringstream：拼接（尤其是「拼数字」）");
    std::ostringstream oss;
    oss << "用户 " << 42 << " 的分数是 " << std::fixed << std::setprecision(2) << 95.5;
    std::cout << "    " << oss.str() << '\n';
    std::cout << "    ★ 注意：std::fixed / setprecision 会粘在这个流上，\n";
    std::cout << "      所以一个 ostringstream 里最好只做一种格式的输出。\n";
    std::cout << "    ★ 现代替代：std::format（见第 9 节），无状态、更快、更短。\n";

    SubSection("istringstream：解析");
    std::istringstream iss("2026 3.14 hello");
    int year = 0;
    double version = 0.0;
    std::string word;
    iss >> year >> version >> word;
    std::cout << "    从 \"2026 3.14 hello\" 解析：year=" << year << " version=" << version
              << " word=" << word << '\n';
    std::cout << "    ★ >> 会自动跳过空白，所以空格分隔的字段解析起来很省事。\n";
    std::cout << "    ★ 但 >> 对错误很敏感且不留残留信息，严格解析请用 from_chars 逐字段处理。\n";

    SubSection("★ 实用：一个完整的 CSV 行解析器");
    // 把 "apple,3,12.50" 解析成 {name, qty, price}
    struct Row {
        std::string name;
        int qty = 0;
        double price = 0.0;
        bool ok = false;
    };
    const auto ParseCsvLine = [](const std::string& line) -> Row {
        Row r;
        std::istringstream ls(line);
        std::string field;
        // 第 1 个字段：名字
        if (!std::getline(ls, field, ',')) return r;
        r.name = field;
        // 第 2 个字段：整数
        if (!std::getline(ls, field, ',')) return r;
        {
            const auto res = std::from_chars(field.data(), field.data() + field.size(), r.qty);
            if (res.ec != std::errc{} || res.ptr != field.data() + field.size()) return r;
        }
        // 第 3 个字段：浮点
        if (!std::getline(ls, field, ',')) return r;
        {
            const auto res = std::from_chars(field.data(), field.data() + field.size(), r.price);
            if (res.ec != std::errc{} || res.ptr != field.data() + field.size()) return r;
        }
        r.ok = true;
        return r;
    };

    const std::string csvText =
        "name,qty,price\napple,3,12.50\nbanana,128,3.75\nBADROW,abc,1.0\ncherry,7,199.99\n";
    std::istringstream csv(csvText);
    std::string line;
    int lineNo = 0;
    double total = 0.0;
    std::cout << "    逐行解析：\n";
    while (std::getline(csv, line)) {
        ++lineNo;
        if (lineNo == 1) {
            std::cout << "      跳过表头：" << line << '\n';
            continue;
        }
        if (line.empty()) continue;
        const Row r = ParseCsvLine(line);
        if (!r.ok) {
            std::cout << "      第 " << lineNo << " 行格式错误，跳过：" << line << '\n';
            continue;  // ★ 单行错误不中断整个解析 —— 这是解析器的基本素质
        }
        const double amount = r.qty * r.price;
        total += amount;
        std::cout << "      " << r.name << "  " << r.qty << " x " << r.price << " = " << amount
                  << '\n';
    }
    std::cout << "    总计 = " << std::fixed << std::setprecision(2) << total << std::defaultfloat
              << std::setprecision(6) << '\n';
    std::cout << "    ★ 三个要点：\n";
    std::cout << "      1) getline(ls, field, ',') 按分隔符切字段，比手写 find/substr 短；\n";
    std::cout << "      2) 数字转换用 from_chars 并检查「是否消费了整个字段」；\n";
    std::cout << "      3) 单行出错只跳过该行，不放弃整个文件。\n";
    std::cout << "    ★ 更省内存的版本：不构造 field 字符串，直接用 string_view 切（见 06 章）。\n";

    SubSection("stringstream 的复用：clear() 清状态，str(\"\") 清内容");
    std::ostringstream reused;
    reused << "first";
    std::cout << "    第一次：" << reused.str() << '\n';
    reused.str("");    // ★ 清内容（str("") 会重置缓冲区）
    reused.clear();    // ★ 清状态位（如果之前遇到过错误/eof）
    reused << "second";
    std::cout << "    清空后：" << reused.str() << '\n';
    std::cout << "    ★ 复用 stringstream 能省下重复分配的开销，\n";
    std::cout << "      在「循环里解析大量行」的热路径上值得这么做。\n";
    std::cout << "    ★ 但两个 clear 都要做：只做 str(\"\") 不清状态位，\n";
    std::cout << "      流可能还停在 fail/eof 状态，后续读写会立即失败。\n";
}

// ===========================================================================
// 9. format 对比与 printf 的适用场景
// ===========================================================================
void DemoFormatVsIostream() {
    Section("9. std::format vs iostream vs printf");

    const std::string name = "张三";
    const int age = 30;
    const double score = 92.5678;

    SubSection("同一个输出，三种写法");
    // 1) printf
    std::printf("    printf   : %s 是 %d 岁，成绩 %.1f\n", name.c_str(), age, score);
    // 2) iostream（注意要处理状态污染）
    std::cout << "    iostream : " << name << " 是 " << age << " 岁，成绩 " << std::fixed
              << std::setprecision(1) << score << '\n';
    std::cout << std::defaultfloat << std::setprecision(6);
    // 3) format
    std::cout << "    format   : " << std::format("{} 是 {} 岁，成绩 {:.1f}", name, age, score)
              << '\n';

    SubSection("对齐表格：三种写法的可读性差距");
    struct Row {
        std::string name;
        int qty;
        double amount;
    };
    const std::vector<Row> rows = {{"apple", 3, 12.5}, {"banana", 128, 1234.56}, {"cherry", 7, 0.99}};

    std::cout << "    ---- iostream 版 ----\n";
    std::cout << "      " << std::left << std::setw(10) << "name" << std::right << std::setw(8)
              << "qty" << std::setw(12) << "amount" << '\n';
    for (const auto& r : rows) {
        std::cout << "      " << std::left << std::setw(10) << r.name << std::right << std::setw(8)
                  << r.qty << std::fixed << std::setprecision(2) << std::setw(12) << r.amount
                  << '\n';
    }
    std::cout << std::defaultfloat << std::setprecision(6);

    std::cout << "    ---- std::format 版 ----\n";
    std::cout << std::format("      {:<10}{:>8}{:>12}\n", "name", "qty", "amount");
    for (const auto& r : rows) {
        std::cout << std::format("      {:<10}{:>8}{:>12.2f}\n", r.name, r.qty, r.amount);
    }
    std::cout << "    ★ format 版本每行独立，没有「忘了恢复 setw/setprecision」的风险。\n";

    SubSection("性能与安全对比");
    std::cout << "    printf   : 最快（但格式串与实参不匹配是 UB，且不支持自定义类型）\n";
    std::cout << "    iostream : 类型安全，但 << 链冗长、有流状态、默认区域设置影响格式\n";
    std::cout << "    format   : 类型安全 + 编译期检查格式串 + 无状态；\n";
    std::cout << "               本机实测比 ostringstream 快数倍（见 06 章：Release 下 7 倍），\n";
    std::cout << "               但仍比 snprintf 慢。\n";
    std::cout << "               补充：本机 Release 实测里 format 反而略快于 snprintf，\n";
    std::cout << "                     说明「谁快」取决于实现版本与格式复杂度，别当铁律。\n";

    SubSection("★ 什么时候 printf 仍然合适");
    std::cout << "    1) 对接 C 代码或第三方 C 库的日志回调；\n";
    std::cout << "    2) 极致性能且格式简单（比如逐字节的二进制协议编码）；\n";
    std::cout << "    3) 需要「先算长度再写」的场景：snprintf(nullptr, 0, ...) 返回所需长度；\n";
    std::cout << "    4) 只在 C++17 及以前的项目里（没有 std::format），又嫌 iostream 啰嗦；\n";
    std::cout << "    5) 需要 %*d 这种「宽度由参数给出」的变长格式（format 用 {:{}d} 也行但更绕）。\n";
    std::cout << "    ★ 反面：任何「格式串来自用户/配置文件」的场景都【不要】用 printf，\n";
    std::cout << "      那是格式化字符串漏洞（%n / %s 越界读）。\n";

    SubSection("一个实用组合：用 format 生成字符串，再交给 C 接口");
    const std::string msg = std::format("[{}] {} 连接失败，错误码 {}", "WARN", "127.0.0.1", 10061);
    // 假设这里有个 C 接口要求 const char*
    std::printf("    C 接口收到的消息：%s\n", msg.c_str());
    std::cout << "    ★ 这是最常见的混用方式：C++ 侧用 format 组装，边界处 .c_str() 传出去。\n";
}

}  // namespace

int main() {
    EnableUtf8Console();

    std::printf("==========================================================\n");
    std::printf(" 10_iostream_and_files.cpp —— iostream 与文件读写\n");
    std::printf("==========================================================\n");
    std::printf("（本文件用 std::istringstream 模拟键盘输入，所以可重复、可自动化运行）\n");

    DemoStandardStreams();
    DemoManipulators();
    DemoStreamStates();
    DemoGetlineMixing();
    DemoRobustInput();
    DemoFileStreams();
    DemoFilesystem();
    DemoStringStream();
    DemoFormatVsIostream();

    std::cout << "\n================ 小结 ================\n";
    std::cout << "1. cout=结果，cerr=不缓冲的错误，clog=带缓冲的日志；换行用 '\\n' 而不是 endl。\n";
    std::cout << "2. iomanip 的 setprecision/fixed/hex/left 等会【粘住】，setw 只作用于一次输出。\n";
    std::cout << "3. ★ cin 失败后必须 clear() 清状态 + ignore() 清行，两者缺一不可。\n";
    std::cout << "4. >> 之后立刻 getline 会读到残留的 '\\n'；要么先 ignore，要么全程用 getline。\n";
    std::cout << "5. 输入循环永远用 while (getline(...)) 或 while (cin >> x)，不要用 !eof()。\n";
    std::cout << "6. 把输入封装成返回 bool 的函数（ReadInt / ReadIntInRange / ReadMenuChoice），\n";
    std::cout << "   能明确区分「格式错误」「范围错误」「输入结束」三种情况。\n";
    std::cout << "7. 文件流是 RAII 的，不要手写 close()；打开后必须检查 if (!f)。\n";
    std::cout << "8. 二进制模式与文本模式不能混用；二进制读写仅对平凡可拷贝类型安全。\n";
    std::cout << "9. std::filesystem 提供了跨平台路径操作与目录遍历，优先用带 error_code 的重载。\n";
    std::cout << "10. stringstream 适合解析；format 适合格式化输出；printf 只在对接 C 或极致性能时用。\n";
    return 0;
}
