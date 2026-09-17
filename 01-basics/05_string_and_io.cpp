// ============================================================================
//  05_string_and_io.cpp  —— 01-basics 第 5 篇
// ----------------------------------------------------------------------------
//  演示主题：
//    1. C 风格字符串（char[] / char*）的四个固有风险 vs std::string
//    2. printf 家族的完整用法：转换规范、标志、宽度、精度、动态 * 宽度
//    3. 格式串漏洞：printf(用户数据) 为什么是安全漏洞
//    4. %n 的危险性（可以向任意地址写值）
//    5. snprintf 是安全替代：返回值语义与截断检测
//    6. scanf 的三个问题：返回值、宽度限制、遇空白就停
//    7. fgets 才是更稳的输入方式（含换行符处理）
//    8. printf 与 scanf 的 %%lf 差异：输出可以，输入必须
//    9. std::getline 与 cin >> 混用必须清缓冲区
//   10. std::format（C++20）：类型安全、可读性好的现代替代
//
//  关键结论：
//    * 格式串必须是字面量。用户数据只能出现在参数位置。
//    * C 里没有「安全的字符串函数」，只有「你自己记得带上长度的函数」。
//    * 日常 C++ 用 std::string + std::getline + std::format/std::cout，
//      把 printf/scanf 当作出现在 C 接口边界上的工具。
// ============================================================================

#define NOMINMAX
// strcpy/sprintf/scanf 在 MSVC 里被标记为 C4996（不安全的 CRT 函数）。
// 本文件的目的是【讲解并演示】这些函数的行为和风险，所以就地关掉这个警告，
// 并在每一处说明更好的替代品。真实项目里更推荐：_s 版本（仅 Windows）
// 或者干脆用 std::string / std::format / std::getline。
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <format>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void use_utf8_console() {
    static_cast<void>(SetConsoleOutputCP(CP_UTF8));
}

void title(const char* text) {
    std::cout << "\n==== " << text << " ====\n";
}

// ---------------------------------------------------------------------------
// 原笔记里的 chapter() 函数：它直接用参数做 printf 的格式串，这正是格式串漏洞。
// 这里保留「错误版本」并给出「正确版本」两个函数作对照。
// ---------------------------------------------------------------------------
void chapter_unsafe(const char* text) {
    std::printf("\n");
    for (int i = 0; i < 8; ++i) {
        std::printf("--");
    }
    // 【漏洞】text 被当成格式串。如果 text 里含有 %s / %n，就会按格式规范解释，
    // 去读栈上并不存在的参数，甚至（配合 %n）往内存里写值。
    std::printf(text/*, 没有参数可传*/);
    std::printf("\n");
}

void chapter_safe(const char* text) {
    std::printf("\n");
    for (int i = 0; i < 8; ++i) {
        std::printf("--");
    }
    // 【正确】把内容放进参数位置，格式串永远是 "%s" 这样的字面量。
    std::printf("%s", text);
    std::printf("\n");
}

}  // namespace

int main() {
    use_utf8_console();

    // ======================================================================
    title("1. C 风格字符串 vs std::string");
    // ======================================================================
    // C 风格字符串 = 以 '\0' 结尾的 char 数组。它没有「长度」这个字段，
    // 长度靠扫描到 '\0' 才知道。于是产生四个固有风险：
    //   (1) 忘记结尾的 '\0' -> 读越界；(2) 缓冲区固定 -> 溢出；
    //   (3) 长度要额外传 -> 传错；(4) 不能整体赋值/比较/拼接。
    char c_style[] = "Hello";       // sizeof 是 6：5 个字符 + 1 个 '\0'
    const char* c_literal = "Hello";
    std::cout << "  char c_style[] = \"Hello\";          sizeof = " << sizeof(c_style)
              << "（含结尾 \\0）\n";
    std::cout << "  const char* c_literal = \"Hello\";    sizeof = " << sizeof(c_literal)
              << "（这只是个指针，长度信息已经丢了）\n";
    std::cout << "  strlen(c_style) = " << std::strlen(c_style)
              << "   <- 运行期扫描到 \\0 才知道长度\n";
    // 用列表初始化字符数组不会自动补 '\0'：
    char no_terminator[3] = {'h', 'i', '!'};  // 没有 '\0'
    // std::strlen(no_terminator) 是 UB：会一直扫到内存里第一个 0 字节
    std::cout << "  char t[3] = {'h','i','!'}; 没有结尾 \\0，对它调 strlen 是 UB（这里不调）\n";
    std::cout << "  三个字符是: " << no_terminator[0] << no_terminator[1]
              << no_terminator[2] << '\n';
    // std::string 自带长度、自动管理内存、可以赋值/比较/拼接
    std::string s1 = "Hello";
    std::string s2 = s1;             // 深拷贝，和 s1 无关
    s2 += ", world";                 // 自动扩容
    std::cout << "  std::string 可拷贝/拼接：s2 = " << s2 << "，size = " << s2.size()
              << "，结尾一定有 \\0：" << std::boolalpha << (s2.c_str()[s2.size()] == '\0')
              << '\n';
    std::cout << std::noboolalpha;
    // 和 C 接口交互时才用 c_str() / data()
    std::cout << "  需要 const char* 时用 s2.c_str()：strlen = " << std::strlen(s2.c_str()) << '\n';
    // string_view：只读、不拷贝的「窗口」，适合做只读参数
    const std::string_view sv(s2);
    std::cout << "  string_view 切片 substr(0,5) = " << sv.substr(0, 5)
              << "，且不产生任何拷贝\n";

    // ======================================================================
    title("2. printf 的转换规范：完整拆解");
    // ======================================================================
    // 完整形式：  %[标志][最小宽度][.精度][长度指示符]转换操作
    //              |       |         |        |
    //              |       |         |        +-- h hh l ll z j t L
    //              |       |         +----------- 浮点的小数位数 / 字符串的最大字符数
    //              |       +--------------------- 最小字段宽度（不足补空格或 0）
    //              +----------------------------- - + 空格 # 0
    std::printf("  %%d 有符号十进制   : %d\n", -42);
    std::printf("  %%i 有符号十进制   : %i   （和 %%d 在 printf 里没区别，在 scanf 里有）\n", -42);
    std::printf("  %%u 无符号十进制   : %u\n", 42u);
    std::printf("  %%o 八进制         : %o\n", 42u);
    std::printf("  %%x / %%X 十六进制 : %x / %X\n", 255u, 255u);
    std::printf("  %%f 定点浮点       : %f\n", 3.14159);
    std::printf("  %%e / %%E 科学计数 : %e / %E\n", 12346.326, 12346.326);
    std::printf("  %%g 自动紧凑      : %g   （在 %%f 和 %%e 中挑更短的那个）\n", 0.0001234);
    std::printf("  %%a 十六进制浮点   : %a\n", 3.14);
    std::printf("  %%c 单字符         : %c\n", 'A');
    std::printf("  %%s 字符串         : %s\n", "text");
    std::printf("  %%p 指针           : %p\n", static_cast<const void*>(c_literal));
    std::printf("  %%%% 字面量百分号  : %%\n");
    // 长度指示符
    std::printf("  %%hd (short)       : %hd\n", static_cast<short>(-7));
    std::printf("  %%lld (long long)  : %lld\n", -9000000000LL);
    std::printf("  %%zu (size_t)      : %zu   <- sizeof 的返回值必须用它\n", sizeof(int));
    std::printf("  %%td (ptrdiff_t)   : %td\n", static_cast<std::ptrdiff_t>(-3));
    std::printf("  %%.3f 精度三位     : %.3f\n", 1.0023);
    std::printf("  %%10.3f 宽10精度3  : [%10.3f]\n", 3.1415926);
    std::printf("  %%-10.3f 左对齐    : [%-10.3f]\n", 3.1415926);
    std::printf("  %%+d 强制符号      : [%+d] / [%+d]\n", 42, -42);
    std::printf("  %% d 正数留空格    : [% d] / [% d]\n", 42, -42);
    std::printf("  %%05d 零填充       : [%05d]\n", 42);
    std::printf("  %%-05d 左对齐时 0 被忽略 : [%-05d]\n", 42);
    std::printf("  %%#o 八进制前缀    : %#o\n", 42u);
    std::printf("  %%#x 十六进制前缀  : %#x\n", 255u);
    std::printf("  %%#g 保留末尾 0    : %#g\n", 1.5);
    std::printf("  %%*.*f 动态宽度精度: [%*.*f]  （宽度和精度从参数里取）\n", 12, 4, 3.14159265);
    std::printf("  %%-*d 动态宽度左对齐: [%-*d]\n", 8, 42);
    std::printf("  %%.5s 截断字符串   : [%.5s]\n", "Hello, world");

    // ======================================================================
    title("3. %%lf 的真相：输出可以，输入必须");
    // ======================================================================
    // 【原笔记】「double要用%lf 输出有提升不用考虑，但是输入必须考虑！！！」——意思对但太含糊。
    // 【精确说法】
    //   printf ：%f 和 %lf 完全等价。C99 起标准明确写了 l 对 f 没有影响，
    //            因为 float 实参在可变参数里已经被「默认实参提升」成 double 了。
    //            换句话说 printf 里根本不存在「float 版本」的 %f。
    //   scanf  ：必须区分！%f 要求实参是 float*，%lf 要求实参是 double*。
    //            用错会写坏内存（UB），因为 scanf 会按你给的类型写 sizeof 个字节。
    const double d_value = 3.14159265358979;
    const float f_value = 1.5f;
    std::printf("  printf %%.10f 输出 double : %.10f\n", d_value);
    std::printf("  printf %%.10lf 输出同一个 : %.10lf   <- 完全一样\n", d_value);
    std::printf("  printf %%f 里传 float 也安全（可变参数提升成 double）: %f\n",
                static_cast<double>(f_value));
    std::printf("  scanf 侧：读 double 必须 \"%%lf\"，读 float 必须 \"%%f\"，写错是 UB\n");
    std::printf("  常见错误：scanf(\"%%f\", &d) 其中 d 是 double -> 只写 4 字节，d 的高位是垃圾\n");

    // ======================================================================
    title("4. 格式串漏洞：为什么 printf(user_input) 是安全漏洞");
    // ======================================================================
    // 【错误直觉】「printf 第一个参数是『要打印的内容』」。
    // 【正确模型】printf 的第一个参数是【格式模板】，它不是数据，是代码级别的说明。
    //   所以把用户输入当格式串，等于让用户决定程序去栈上取几个参数、按什么类型解释。
    //   攻击者可以用 %x 泄露栈内容、用 %s 解引用任意地址读内存、
    //   用 %n 往任意地址写整数——这就是经典的 format string vulnerability。
    // 【正确写法】永远 printf("%s", user_data); 或者更直接用 std::cout / std::format。
    // 这里用 %d 而不是 %s 来演示：%d 只会读栈上的一个整数（泄露数据），
    // 而 %s 会把那个整数当地址去解引用，极大概率直接让程序崩溃。
    // 这也正说明漏洞的杀伤力：同样一句 printf，格式串里写什么，攻击者说了算。
    const char* user_input = "这是一段用户数据 %d";  // 模拟从网络/文件读来的数据
    chapter_unsafe(user_input);   // 危险演示：内容里的 %d 会被当成格式规范
    chapter_safe(user_input);     // 正确写法：内容进参数位置
    std::cout << "  上面两次输出的区别就是本体：unsafe 版本里的 %d 被当成格式规范，\n";
    std::cout << "  于是 printf 去栈上随便取了一个整数打印出来（泄露内存内容）；\n";
    std::cout << "  如果把 %d 换成 %s，那个整数会被当地址解引用，程序通常会直接崩溃\n";
    // 现代替代：编译器会帮你做静态格式检查
    std::cout << "  C++20 的 std::format 在编译期检查参数类型与数量，见本篇第 7 节\n";

    // ======================================================================
    title("5. %%n 的危险性");
    // ======================================================================
    // %n 不输出任何字符，而是把「到目前为止已输出的字符数」写回你给的 int*。
    // 它本身有正当用途（对齐填充计算），但在「格式串可控」的场景下极度危险：
    //   攻击者用 %n 配合精心构造的宽度（如 %100c）就能向目标地址写入任意整数，
    //   直接改写返回地址或函数指针。
    // 现代对策：
    //   * 永远不让用户数据成为格式串（治本）；
    //   * 启用编译器的格式串检查（-Wformat-security / MSVC 默认会警告非常量格式串）；
    //   * 安全敏感项目里禁用 %n。
    // MSVC 的 CRT 默认就禁用了 %n：如果不禁用就拿去用，进程会直接 abort（退出码 3）。
    // 这里先用 _set_printf_count_output(1) 显式打开，好让你看到它的效果，
    // 同时也要记住：默认关闭正是微软对这类漏洞的回应。
    const int count_output_was_enabled = _set_printf_count_output(1);
    std::cout << "  MSVC 默认是否启用 %n 写入能力：" << std::boolalpha
              << (count_output_was_enabled != 0) << "（0 表示默认关闭）\n";
    std::cout << std::noboolalpha;
    int written = -1;
    const int n_result = std::printf("  ABCDEFG%n <- 到这里已输出 9 个字符\n", &written);
    std::cout << "  %n 写回的计数 = " << written << "（printf 返回值 = " << n_result
              << "，两者含义不同）\n";
    std::cout << "  工程结论：生产代码里一律不要用 %n；真的需要计数就自己累加\n";

    // ======================================================================
    title("6. snprintf：安全替代 sprintf");
    // ======================================================================
    // sprintf 不知道目标缓冲区多大 -> 必然溢出风险。
    // snprintf 多了「容量」参数，并且返回值语义很有用：
    //   返回值 = 「假如缓冲区足够大，本应写入的字符数」（不含结尾 '\0'）。
    //   所以 返回值 >= 容量 就说明发生了截断，可以据此报错或重新分配。
    // 注意：变量名不要取 small / near / far / min / max —— windows.h 里是宏。
    char tiny_buffer[8];
    const int would_be = std::snprintf(tiny_buffer, sizeof(tiny_buffer), "%s-%d", "user", 12345);
    std::cout << "  snprintf 到 8 字节缓冲区：内容 = \"" << tiny_buffer
              << "\"，本应写入 " << would_be << " 字符 -> "
              << (static_cast<std::size_t>(would_be) >= sizeof(tiny_buffer) ? "发生了截断" : "没有截断")
              << '\n';
    std::cout << "  用返回值判断截断比猜大小可靠得多；C++ 里更该直接用 std::string + std::format\n";

    // ======================================================================
    title("7. std::format / std::format_to（C++20，MSVC 支持）");
    // ======================================================================
    // 优点：类型安全（编译期检查参数个数和大致类型）、可读性高、不依赖格式串记忆、
    //       自动处理宽度与对齐、可以格式化任意支持 formatter 的类型。
    std::cout << "  " << std::format("默认：{} {} {}\n", 42, 3.14, "text");
    std::cout << "  " << std::format("位置参数：{1} {0} {1}\n", "A", "B");
    std::cout << "  " << std::format("宽度与填充：|{:>8}|{:<8}|{:^8}|\n", 42, 42, 42);
    std::cout << "  " << std::format("用 0 填充：|{:08.3f}|\n", 3.14159);
    std::cout << "  " << std::format("十六进制：{:#x}  八进制：{:#o}  二进制：{:#b}\n", 255, 255, 255);
    std::cout << "  " << std::format("科学计数：{:.3e}\n", 12346.326);
    std::cout << "  " << std::format("指针：{:p}\n", static_cast<const void*>(c_literal));
    // 格式化到字符串里，而不是直接输出
    const std::string formatted = std::format("{}-{:04d}", "item", 7);
    std::cout << "  std::format 返回 string：\"" << formatted << "\"（长度 "
              << formatted.size() << "）\n";
    // 注意：std::format 的格式串是编译期常量，写错直接编译失败——这就是它安全的原因。
    std::cout << "  std::format 的格式串必须是编译期常量，写错编译期就报错\n";

    // ======================================================================
    title("8. scanf 的三个问题：返回值、宽度、遇空白就停");
    // ======================================================================
    // 问题 1：不检查返回值 -> 输入失败时变量还是旧值/未初始化值。
    //         返回值 = 成功赋值的变量个数，可以拿它和期望个数比较。
    // 问题 2：%s 没有宽度限制 -> 溢出。必须写 "%19s" 给 20 字节缓冲区留 '\0' 的位置。
    // 问题 3：%s 遇空白（空格/制表符/换行）就停 -> 读不到带空格的整行。
    // 另外 %s 也无法读空字符串：连续空白会被跳过，它至少要读到一个非空白字符。
    char name_buffer[20] = {};
    std::cout << "  请输入一个不含空格的单词（直接回车走 EOF 分支也能继续）：\n  > ";
    std::cout.flush();
    const int scanned = std::scanf("%19s", name_buffer);  // 19 = 20 - 1，给 '\0' 留位置
    if (scanned == 1) {
        std::cout << "  scanf 成功读入：" << name_buffer << '\n';
    } else if (scanned == 0) {
        std::cout << "  scanf 返回 0：输入格式不匹配（比如第一个字符就是空白之外的问题）\n";
    } else {
        std::cout << "  scanf 返回 EOF：输入流已经结束（管道/重定向时很常见）\n";
    }
    std::cout << "  无论成功失败，scanf 都可能把换行符留在缓冲区里，"
                 "这就是第 9 节 getline 读出空串的原因\n";

    // ======================================================================
    title("9. fgets 才是更稳的输入方式");
    // ======================================================================
    // fgets(buf, size, stream) 的特点：
    //   * 一定带上限，绝不溢出；
    //   * 读整行（包含空格），遇到 '\n' 或 buf 满为止；
    //   * 会把 '\n' 一起存进缓冲区（这是唯一的缺点，需要自己处理）；
    //   * 返回值是 nullptr 表示失败或 EOF，必须判断。
    std::cout << "  请输入一整行（可以直接回车）：\n  > ";
    std::cout.flush();
    // 这里刻意不动 std::cin：cin.clear() 只清「错误标志」，不会清掉缓冲区里的数据。
    // 上一步 scanf 读走了 "myword"，但把行尾的换行留在了流里，
    // 所以下面这次 fgets 会读到那个换行，得到一行「空内容」。这恰好演示了残留字符问题。
    char line_buffer[64] = {};
    if (std::fgets(line_buffer, sizeof(line_buffer), stdin) != nullptr) {
        std::string line(line_buffer);
        // 去掉结尾的 '\n' 和 '\r'（Windows 文本换行是 CRLF）
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        std::cout << "  fgets 读到整行：\"" << line << "\"（长度 " << line.size() << "）\n";
        if (line.empty()) {
            std::cout << "  ^ 空行是因为 scanf 把行尾换行留在了缓冲区，fgets 直接读到了它\n";
        }
    } else {
        std::cout << "  fgets 返回 nullptr：到达 EOF\n";
    }
    std::cout << "  fgets 的写法：fgets(buf, sizeof buf, stdin) —— size 永远写 sizeof buf，\n";
    std::cout << "  不要手写数字。C++ 里更推荐 std::getline(std::cin, std::string)\n";

    // ======================================================================
    title("10. std::getline 与 cin >> 混用：必须清缓冲区");
    // ======================================================================
    // 【错误直觉】「cin >> 读完数字，getline 自然读下一行」。
    // 【正确模型】operator>> 遇到分隔符（空白）就停，但【不消费】那个分隔符，
    //   换行符还躺在流里。接着 getline 一看第一个字符就是 '\n'，
    //   立刻认为「这一行是空的」并返回。
    // 【修法】三种，任选其一：
    //   (1) std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    //   (2) std::getline(std::cin >> std::ws, line);   // ws 吃掉前导空白
    //   (3) 全部统一用 getline，再自己解析（最不容易出错，推荐给交互式程序）
    std::istringstream fake_input("42\nhello world\n");
    int number = 0;
    fake_input >> number;
    std::string rest;
    std::getline(fake_input, rest);  // 这里会立刻拿到空串（只读到残留的 '\n'）
    std::cout << "  混用演示（用 istringstream 模拟输入 \"42\\nhello world\\n\"）：\n";
    std::cout << "    cin >> number      -> number = " << number << '\n';
    std::cout << "    getline 紧接着读   -> \"" << rest << "\"（长度 " << rest.size()
              << "，因为吃到了残留的换行，看上去是空行）\n";
    // 正确做法：先清掉残留的换行
    std::istringstream fixed_input("42\nhello world\n");
    int number2 = 0;
    fixed_input >> number2;
    std::string rest2;
    std::getline(fixed_input >> std::ws, rest2);  // std::ws 吃掉前导空白（含换行）
    std::cout << "    修法 A：getline(cin >> std::ws, line) -> \"" << rest2 << "\"\n";
    // 修法 B：ignore
    std::istringstream fixed_input2("42\nhello world\n");
    int number3 = 0;
    fixed_input2 >> number3;
    fixed_input2.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
    std::string rest3;
    std::getline(fixed_input2, rest3);
    std::cout << "    修法 B：cin.ignore(max, '\\n') 后再 getline -> \"" << rest3 << "\"\n";
    std::cout << "    修法 C（最稳）：全程用 getline，数字用 std::from_chars / std::stoi 解析\n";
    // 顺便：cin >> 的失败状态会让后续所有输入失效，必须 clear() + 处理残留
    std::istringstream bad_input("abc\n");
    int failed_number = -1;
    bad_input >> failed_number;
    std::cout << "    输入 \"abc\" 到 int：流进入失败状态，fail() = " << std::boolalpha
              << bad_input.fail() << "，变量保持原值 " << failed_number << '\n';
    std::cout << "    正确做法：检查 if (!(cin >> x)) { cin.clear(); cin.ignore(...); }\n";
    std::cout << std::noboolalpha;

    std::cout << "\n[05] 结束。下一步：06_functions_and_scope.cpp\n";
    return 0;
}
