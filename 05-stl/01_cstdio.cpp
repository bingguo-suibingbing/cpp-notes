// ============================================================================
// 01_cstdio.cpp  ——  <cstdio> / <stdio.h>：C 风格输入输出与文件读写
//
// 演示主题：
//   1. printf 的格式化能力与类型匹配规则（%d 收 int，%lld 收 long long）
//   2. scanf 的返回值语义、缓冲区残留问题，以及「正确清行」的写法
//   3. fgets 才是读整行的安全函数；gets 已被 C11 删除
//   4. fopen / fprintf / fclose 的最小可用文件写读闭环（含 fopen 判空）
//   5. fwrite / fread 二进制读写与 ftell / fseek 定位
//   6. 读文件的三种写法对比：fgets / std::ifstream / std::format + 流
//
// 关键结论：
//   - scanf 的返回值是「成功赋值的转换项个数」，不是「有没有输入」；
//     只判断 != EOF 是不够的，必须和期望项数比较。
//   - 清空输入行必须用「读到 '\n' 或 EOF 为止」的循环。教科书里那句
//     while (getchar() != '\n') {} 在遇到 EOF 时会变成死循环，
//     这是原素材里最值得修掉的一处写法（详见 NOTES.md 纠错清单）。
//   - read-modify-write 的经典 UB：i = i++; 在 C++17 起有明确规则，
//     但 C++17 之前是未定义行为；教学中不要用这种写法举例。
//   - fopen 不检查返回值 = 空指针解引用；用后不 fclose = 数据可能丢失。
//
// 说明：本文件顶部定义 _CRT_SECURE_NO_WARNINGS，是为了让 MSVC 的 /WX 不把
//       C4996（fopen / scanf / strcpy 等「不安全函数」）当成错误，好让 C 风格
//       写法能编译出来做对照。生产代码应当使用带 _s 后缀的安全版本，或者直接
//       改用 C++ 的 <iostream> / std::format / std::filesystem（见本文件第 7 节
//       和 10_iostream_and_files.cpp）。
// ============================================================================
#define _CRT_SECURE_NO_WARNINGS  // 教学对照用：见文件头说明，生产代码不要这样写

#include <cerrno>    // errno：C 风格错误码
#include <cstdio>
#include <cstdlib>   // std::system
#include <cstring>   // strchr / strcspn / strlen / strerror
#include <fstream>   // std::ifstream：第 6 节做写法对照
#include <iostream>
#include <string>

namespace {

// ---------------------------------------------------------------------------
// 控制台编码：本仓库源码是 UTF-8（编译时带 /utf-8），如果控制台活动代码页不是
// 65001，中文就会变成乱码。改成 UTF-8 是纯 Windows 的补救措施，跨平台不需要。
// ---------------------------------------------------------------------------
void EnableUtf8Console() {
#ifdef _WIN32
    std::system("chcp 65001 > nul");  // 系统调用，仅 Windows 有效；其它平台是空操作
#else
    (void)0;  // 非 Windows 平台不做任何事
#endif
}

void Section(const char* title) {
    std::printf("\n================ %s ================\n", title);
}

// ---------------------------------------------------------------------------
// 健壮输入函数模板：读一整行，成功返回 true。
// 这是 scanf 场景下唯一可靠的清行方式，注意退出条件同时包含 '\n' 和 EOF。
// ---------------------------------------------------------------------------
bool ReadLine(char* buf, std::size_t cap) {
    if (buf == nullptr || cap == 0) return false;
    if (std::fgets(buf, static_cast<int>(cap), stdin) == nullptr) return false;

    // 情况 A：读到了换行符，说明整行读完了
    char* nl = std::strchr(buf, '\n');
    if (nl != nullptr) {
        *nl = '\0';
        return true;
    }
    // 情况 B：缓冲区满了还没见到 '\n'，把这一行剩余部分丢掉，避免污染下一次输入
    int c = 0;
    while ((c = std::getchar()) != '\n' && c != EOF) {
    }
    return true;
}

// ---------------------------------------------------------------------------
// 把行首的空白字符跳过后判断有没有内容：全是空白视作空输入
// ---------------------------------------------------------------------------
bool IsBlankLine(const char* buf) {
    for (const char* p = buf; *p != '\0'; ++p) {
        if (*p != ' ' && *p != '\t' && *p != '\r') return false;
    }
    return true;
}

// ===========================================================================
// 1. printf：格式化输出
// ===========================================================================
void DemoPrintf() {
    Section("1. printf 格式化");

    const int i = 42;
    const long long big = 9000000000LL;      // 超过 int 范围，必须用 long long
    const std::size_t sz = sizeof(int);      // size_t 是无符号类型
    const double pi = 3.14159265358979;

    // 每一行都标注了「转换说明符 ↔ 实参类型」的对应关系。
    // 类型不匹配是本函数最危险的坑：printf 是可变参数函数，编译期拿不到类型信息
    // （现代编译器会用 C4477 之类的警告提醒，但只对字面量格式串有效）。
    std::printf("  %%d   <- int          : %d\n", i);
    std::printf("  %%lld <- long long    : %lld\n", big);                 // 用 %d 打印 big 会截断
    std::printf("  %%zu  <- size_t      : %zu\n", sz);                   // %zu 是 size_t 的正确写法
    std::printf("  %%f   <- double      : %f\n", pi);                     // 默认 6 位小数
    std::printf("  %%.3f <- 3 位小数     : %.3f\n", pi);
    std::printf("  %%e   <- 科学计数     : %e\n", pi);
    std::printf("  %%g   <- 自动选择     : %g\n", pi);
    std::printf("  %%c   <- char        : %c\n", 'A');
    std::printf("  %%s   <- 字符串       : %s\n", "hello");
    std::printf("  %%-6d <- 左对齐宽 6    : [%-6d]\n", i);
    std::printf("  %%06d <- 补 0 宽 6     : [%06d]\n", i);
    std::printf("  %%.4s <- 只打印前 4 字符: [%.4s]\n", "truncated");
    std::printf("  %%p   <- 指针         : %p\n", static_cast<const void*>(&i));
    std::printf("  %%%%   <- 百分号本身    : 100%%\n");

    // 常用技巧：把「已写出的字符数」当作写入长度用（snprintf 的返回值语义）
    char buf[32];
    const int written = std::snprintf(buf, sizeof(buf), "%d-%s", i, "x");
    std::printf("  snprintf 返回值 = %d，内容 = \"%s\"\n", written, buf);
    // snprintf 一定会在 buf 里补 '\0'，所以它比 sprintf 安全，永远不会越界写；
    // 返回值 >= sizeof(buf) 表示「被截断了」，需要告诉调用者。
}

// ===========================================================================
// 2. scanf：按格式读取
// ===========================================================================
void DemoScanf() {
    Section("2. scanf 的返回值与缓冲区残留");

    std::printf("  请输入一个整数（直接回车 / 输入非数字 都会走错误分支）：");
    std::fflush(stdout);  // 提示语没有换行符，主动刷新，否则重定向到管道时看不到

    int num = 0;
    // 关键点：scanf 返回「成功赋值的转换项个数」。
    //   返回 1 -> 读到一个整数；返回 0 -> 输入了非数字（比如 "abc"）；
    //   返回 EOF -> 遇到输入结束或读错误。
    // 只判断 == EOF 是错的：输入 "abc" 时返回 0，会把垃圾值当成有效数据。
    const int n = std::scanf("%d", &num);
    if (n == 1) {
        std::printf("  读到整数：%d\n", num);
    } else {
        std::printf("  读取失败（scanf 返回 %d：%s），改用默认值 42\n", n,
                    n == EOF ? "输入已结束" : "输入不是合法整数的开头");
        num = 42;
    }

    // ★ 本文件最重要的修正：清行要同时处理 '\n' 与 EOF。
    //   教科书常见写法 while (getchar() != '\n') {} 在输入遇到 EOF 时
    //   getchar 永远返回 EOF（-1），条件永远为真 -> 死循环。
    //   在 Windows 控制台按 Ctrl+Z 回车、或程序被管道喂空输入时就会触发。
    int c = 0;
    while ((c = std::getchar()) != '\n' && c != EOF) {
        // 丢掉这一行的剩余字符
    }

    // 顺带演示：用 fgets 读一整行（推荐做法）
    char line[128] = {0};
    std::printf("  请输入一行文字（含空格也行）：");
    std::fflush(stdout);
    if (ReadLine(line, sizeof(line))) {
        if (IsBlankLine(line)) {
            std::printf("  你输入的是空行。\n");
        } else {
            std::printf("  这一行共 %zu 个字符：%s\n", std::strlen(line), line);
        }
    } else {
        std::printf("  没有读到任何输入（stdin 已结束）。\n");
    }
}

// ===========================================================================
// 3. fgets：安全的读行函数
// ===========================================================================
void DemoFgets() {
    Section("3. fgets 读行");

    std::printf("  为什么不用 gets？gets 无法限制长度，输入超长就栈溢出，已被 C11 删除。\n");
    std::printf("  fgets(buf, n, fp) 最多写 n-1 个字符并补 '\\0'，永远不会越界。\n");
    std::printf("  fgets 会把换行符一起读进来（这是它和 C++ getline 的最大差别）。\n");

    // 从内存缓冲区读，演示 fgets 的行为，不依赖真实文件
    const char* src = "第一行\n第二行没有换行符";
    char buf[64];
    std::FILE* fp = std::tmpfile();  // tmpfile 建临时文件，程序结束自动删除
    if (fp == nullptr) {
        std::printf("  tmpfile 创建失败，跳过本演示。\n");
        return;
    }
    std::fputs(src, fp);
    std::rewind(fp);  // 回到文件开头才能读

    int lineNo = 0;
    while (std::fgets(buf, sizeof(buf), fp) != nullptr) {
        ++lineNo;
        // 去掉行尾的 '\n'：strcspn 返回「第一个出现的 '\n' 的下标」
        buf[std::strcspn(buf, "\r\n")] = '\0';  // 一并去掉 Windows 的 \r
        std::printf("  第 %d 行：[%s]\n", lineNo, buf);
    }
    std::fclose(fp);
}

// ===========================================================================
// 4. 文本文件写 + 读：fopen / fprintf / fgets / fclose
// ===========================================================================
void DemoTextFile() {
    Section("4. fopen + fprintf + fgets + fclose");

    const char* kFile = "05_stl_cstdio_demo.txt";

    // "w"：截断写入（文件不存在则创建）。注意必须判空：
    // 路径不存在、没有写权限、文件被占用都会返回 nullptr。
    std::FILE* fp = std::fopen(kFile, "w");
    if (fp == nullptr) {
        std::printf("  打开 %s 失败！错误原因：%s\n", kFile, std::strerror(errno));
        return;
    }
    std::fprintf(fp, "由 01_cstdio.cpp 写入的文本。\n");
    std::fprintf(fp, "整数 = %d，浮点 = %.2f\n", 42, 3.14159);
    // fclose 绝不能省：它负责把用户态缓冲刷进操作系统。
    // 不调用 fclose 而直接退出进程，数据有丢失的可能。
    if (std::fclose(fp) != 0) {
        std::printf("  fclose 失败！\n");
    }
    std::printf("  已写入 %s\n", kFile);

    // 读回：用 fgets 逐行读
    fp = std::fopen(kFile, "r");  // "r"：只读，文件不存在返回 nullptr
    if (fp == nullptr) {
        std::printf("  打开 %s 失败（读模式）！\n", kFile);
        return;
    }
    std::printf("  读回内容：\n");
    char line[256];
    while (std::fgets(line, sizeof(line), fp) != nullptr) {
        std::printf("    | %s", line);  // line 里自带 '\n'，不用再加
    }
    std::fclose(fp);
    std::printf("  读取完毕。\n");

    // "a" 追加模式：不会清空原内容，常用于写日志
    fp = std::fopen(kFile, "a");
    if (fp != nullptr) {
        std::fprintf(fp, "追加的一行（模式 \"a\"）。\n");
        std::fclose(fp);
        std::printf("  已用 \"a\" 模式追加一行。\n");
    }
    std::printf("  删除演示文件：%s\n", std::remove(kFile) == 0 ? "成功" : "失败");
}

// ===========================================================================
// 5. 二进制读写：fwrite / fread / ftell / fseek
// ===========================================================================
struct Record {
    int id;
    double value;
};

void DemoBinaryFile() {
    Section("5. fwrite / fread 二进制读写");

    const char* kBin = "05_stl_cstdio_demo.bin";
    const Record records[3] = {{1, 1.5}, {2, 2.5}, {3, 3.5}};

    std::FILE* fp = std::fopen(kBin, "wb");  // 二进制写：加 'b'
    if (fp == nullptr) {
        std::printf("  二进制文件写入失败！\n");
        return;
    }
    // fwrite(元素地址, 单个元素字节数, 元素个数, 文件指针)，返回「成功写入的元素个数」
    const std::size_t put = std::fwrite(records, sizeof(Record), 3, fp);
    std::fclose(fp);
    std::printf("  fwrite 写入 %zu 个元素，每个 %zu 字节（无内存对齐填充）\n", put, sizeof(Record));

    // fseek 定位到文件末尾，ftell 拿总字节数：这是最常用的「取文件大小」写法
    fp = std::fopen(kBin, "rb");
    if (fp == nullptr) {
        std::printf("  二进制文件打开失败！\n");
        return;
    }
    std::fseek(fp, 0, SEEK_END);       // 从末尾偏移 0 -> 定位到末尾
    const long size = std::ftell(fp);  // 返回当前偏移量 = 文件大小
    std::rewind(fp);                   // 回到开头
    std::printf("  文件大小 = %ld 字节，恰好等于 3 * sizeof(Record) = %zu\n",
                size, 3 * sizeof(Record));

    Record readBack[3] = {};
    const std::size_t got = std::fread(readBack, sizeof(Record), 3, fp);
    std::fclose(fp);
    std::printf("  fread 读回 %zu 个元素：\n", got);
    for (std::size_t k = 0; k < got; ++k) {
        std::printf("    id = %d, value = %.1f\n", readBack[k].id, readBack[k].value);
    }

    // 工程提醒：二进制格式和「结构体内存布局」绑死。换编译器、换平台、
    // 改结构体字段顺序、甚至只改一个字段类型，文件就读不出来了。
    // 跨平台持久化请用文本格式（JSON / CSV）或显式逐字段序列化。
    std::remove(kBin);
}

// ===========================================================================
// 6. 读文件的三种写法对照
// ===========================================================================
void DemoReadThreeWays() {
    Section("6. 读文件三种写法对照");

    std::printf("  写法 A：fgets 逐行读（C 风格）\n");
    std::printf("    - 需要自己管缓冲区大小、自己 fclose；优点是不依赖 C++ 运行时。\n");
    std::printf("  写法 B：std::ifstream + std::getline（C++ 推荐）\n");
    std::printf("    - RAII 自动关闭文件，std::string 自动扩容，没有缓冲区长度问题。\n");
    std::printf("  写法 C：std::format 生成内容 + 流写出（C++20）\n");
    std::printf("    - 格式化比 fprintf 更安全：{} 占位符会在编译期做类型检查。\n");

    // 这里只演示 C++ 写法的可运行片段，完整的文件流讲解见 10_iostream_and_files.cpp
    const char* kFile = "05_stl_cstdio_demo2.txt";
    {
        std::FILE* f = std::fopen(kFile, "w");
        if (f != nullptr) {
            std::fprintf(f, "%d\n", 7);
            std::fclose(f);
        }
    }

    // 写法 B 的最小示例（用 C 接口写、C++ 接口读，方便对照）
    std::printf("  开始用 ifstream 读回内容：\n");
    {
        std::ifstream in(kFile);  // 构造函数里就打开，析构函数里自动关闭（RAII）
        if (!in) {
            std::printf("  打开文件失败。\n");
        } else {
            std::string eachLine;
            int lineIdx = 0;
            while (std::getline(in, eachLine)) {  // getline 不会把 '\n' 留在字符串里
                ++lineIdx;
                std::printf("    [ifstream] 第 %d 行：\"%s\"\n", lineIdx, eachLine.c_str());
            }
        }
    }  // ★ 这里 ifstream 析构，文件句柄已释放

    // ★ Windows 上不能删除「仍被打开」的文件，所以必须等流析构（上面的大括号）之后再删。
    //   这是跨平台文件操作里很常见的一个坑：Linux 允许 unlink 已打开的文件，Windows 不允许。
    if (std::remove(kFile) == 0) {
        std::printf("  已删除演示文件 %s\n", kFile);
    } else {
        std::printf("  删除 %s 失败（在 Windows 上通常是因为文件还被打开着）\n", kFile);
    }

    std::printf("  结论：新代码一律优先写法 B / C；只有在对接纯 C 库或极致启动性能\n");
    std::printf("        要求时才用 fgets。注意 fprintf 与 C++ 流混用时要小心缓冲顺序。\n");
}

}  // namespace

int main() {
    EnableUtf8Console();

    std::printf("==========================================================\n");
    std::printf(" 01_cstdio.cpp —— <cstdio> / <stdio.h>\n");
    std::printf("==========================================================\n");

    DemoPrintf();
    DemoScanf();
    DemoFgets();
    DemoTextFile();
    DemoBinaryFile();
    DemoReadThreeWays();

    std::printf("\n================ 小结 ================\n");
    std::printf("1. printf 的格式串必须和实参类型严格匹配；size_t 用 %%zu，long long 用 %%lld。\n");    std::printf("2. scanf 返回「成功赋值项数」，要和期望项数比较，不能只比 EOF。\n");
    std::printf("3. 清行必须写成 while ((c = getchar()) != '\\n' && c != EOF)，否则 EOF 下死循环。\n");
    std::printf("4. 读整行用 fgets / std::getline，永远不要用已被删除的 gets。\n");
    std::printf("5. fopen 必须判空，fclose 必须调用；二进制格式与内存布局绑死，慎用于持久化。\n");
    return 0;
}
