// ============================================================================
// 03_cstring_and_memory.cpp  ——  <cstring> / <string.h>：C 字符串与内存操作
//
// 演示主题：
//   1. strcpy / strcat / strlen / strcmp：C 字符串的基本操作与缓冲区风险
//   2. 安全版本 strcpy_s / strcat_s / strncpy_s 与 _TRUNCATE 语义
//   3. strtok 的三宗罪（改原串、内部静态状态、非线程安全）
//       并给出 C++ 的正确替代：std::string_view 手写切分 与 getline + stringstream
//   4. memset / memcpy 的适用边界：只对「平凡可拷贝（trivially copyable）」类型成立
//       对含 std::string 成员的对象 memcpy 是未定义行为，本文件给出可观察的证据
//   5. 悬垂指针演示：为什么「保存了 c_str() 或迭代器」再修改容器会出事
//
// 关键结论：
//   - strtok 会修改原字符串（把分隔符原地改成 '\0'），并且用函数内部的静态
//     状态记录「上次切到哪」。因此：不能对字符串字面量用（只读内存）、
//     不能嵌套使用、多线程同时调用会互相破坏。原素材说它「非线程安全、
//     会改原串」是对的；补充一点：它是 C 标准里少见的「有全局状态」的设计。
//   - MSVC 上 std::strtok 并不触发 C4996，但在意的不是编译警告，
//     而是它本身的设计缺陷。新代码请直接用 string_view 切分。
//   - 注意：MSVC 的安全版是【全局命名空间】的 strtok_s，std::strtok_s 不存在。
//     这本身就是一个「不可移植扩展」的例子。
//   - memcpy 只能用于「可平凡复制」的类型：它的语义是逐字节复制，
//     不调用任何拷贝构造/赋值。对 std::string 这类持有堆指针的类型，
//     逐字节复制会让两个对象指向同一块内存 -> 双重释放 / 悬垂访问。
//
// 说明：本文件定义了 _CRT_SECURE_NO_WARNINGS，理由见 01_cstdio.cpp 头部。
// ============================================================================
#define _CRT_SECURE_NO_WARNINGS  // 教学对照用，生产代码用安全版或 C++ 替代品

#ifdef _WIN32
#  include <crtdbg.h>  // _CrtSetReportMode / _set_invalid_parameter_handler（MSVC 专有）
#endif

#include <cstddef>     // offsetof
#include <cstdint>     // uintptr_t（无效参数处理器的形参类型）
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>    // std::istringstream：另一种切分方式
#include <string>
#include <string_view>  // C++17：零拷贝字符串视图
#include <type_traits>  // std::is_trivially_copyable
#include <vector>

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

// ===========================================================================
// 1. strcpy / strcat / strlen / strcmp
// ===========================================================================
void DemoBasicCString() {
    Section("1. strcpy / strcat / strlen / strcmp");

    char buf[64] = {0};  // ★ 必须初始化：strcat 依赖目标串已经以 '\0' 结尾

    // strcpy：把源串（含结尾 '\0'）复制到目标。
    // 它不知道目的地有多大 —— 目标太小就是经典的缓冲区溢出（栈 smash）。
    std::strcpy(buf, "Hello");
    std::printf("  strcpy  后 buf = \"%s\"\n", buf);

    // strcat：从目标串的 '\0' 位置开始追加，并补新的 '\0'。
    // 同样不做边界检查，目标剩余空间不足就溢出。
    std::strcat(buf, ", C Language");
    std::printf("  strcat  后 buf = \"%s\"\n", buf);

    // strlen：O(n) 遍历到 '\0' 为止，返回长度（不含 '\0'）。
    // ★ 常见性能坑：在 for (i = 0; i < strlen(s); ++i) 里用 strlen，
    //   复杂度从 O(n) 变成 O(n^2)。要么提前存起来，要么用指针遍历。
    std::printf("  strlen  buf 长度 = %zu，sizeof(buf) = %zu（一个是内容长度，一个是容量）\n",
                std::strlen(buf), sizeof(buf));

    // strcmp：逐字节比较（按 unsigned char 的字典序），相等返回 0。
    // ★ 不要写成 if (strcmp(a, b)) —— 那是在判断「不相等」。
    const char* a = "apple";
    const char* b = "banana";
    const int cmp = std::strcmp(a, b);
    std::printf("  strcmp(\"%s\", \"%s\") = %d（%s）\n", a, b, cmp,
                cmp < 0 ? "a 在前" : (cmp > 0 ? "b 在前" : "相等"));

    // 只比较前 n 个字符：strncmp，适合判断前缀
    std::printf("  strncmp(\"apple\", \"apricot\", 2) = %d（前 2 个字符 \"ap\" 相同）\n",
                std::strncmp("apple", "apricot", 2));

    // 查子串：strstr
    const char* found = std::strstr(buf, "C Language");
    std::printf("  strstr(buf, \"C Language\") 偏移 = %lld\n",
                found ? static_cast<long long>(found - buf) : -1LL);

    std::printf("  -> 这些函数都不做边界检查；C++ 里请直接用 std::string（见 06_cpp_string.cpp）。\n");
}

// ===========================================================================
// 2. 安全版本：strcpy_s / strcat_s / strncpy_s
// ===========================================================================
//
// ★ 一个必须知道的 MSVC 行为：*_s 函数在「缓冲区太小」这类参数错误发生时，
//   会调用「无效参数处理器（invalid parameter handler）」。默认处理器在
//   Debug 构建下会直接弹断言并中止进程（exit code 3），而不是把错误码返回给你。
//   想让 *_s 老老实实返回 errno_t，必须先换掉处理器。
//   这不只是教学细节：生产环境里「错误码路径」比「弹窗 + 进程消失」有用得多。
void InstallNonFatalCrtHandler() {
#ifdef _WIN32
    // _CrtSetReportMode 只在 Debug CRT 里有效（Release 下是空宏），加 guard 更清晰
#  ifdef _DEBUG
    _CrtSetReportMode(_CRT_ASSERT, 0);  // 不弹断言对话框
    _CrtSetReportMode(_CRT_ERROR, 0);
#  endif
    // 自定义处理器：只记录，然后照常返回，让调用方拿到 errno_t
    _set_invalid_parameter_handler([](const wchar_t* /*expr*/, const wchar_t* /*func*/,
                                      const wchar_t* /*file*/, unsigned int /*line*/,
                                      uintptr_t /*reserved*/) noexcept {
        std::printf("      [CRT 无效参数处理器被调用，函数将返回错误码而不是中止进程]\n");
    });
#endif
}

void DemoSecureVersions() {
    Section("2. 安全版本 *_s 与正确用法");

    InstallNonFatalCrtHandler();  // 见上面注释：不装它，下面的溢出场景会让进程中止

    char dst[16] = {0};

    // strcpy_s(dst, 容量, src)：src 太长时返回非 0 错误码，并把 dst[0] 置为 '\0'。
    // 它是 C11 附录 K 的内容，MSVC / Annex K 实现才有，其它平台不一定提供 ——
    // 所以「用 _s 函数」并不是真正的可移植方案。
    const errno_t e1 = strcpy_s(dst, sizeof(dst), "short");
    std::printf("  strcpy_s 成功场景返回 0：实际 %d，dst = \"%s\"\n", e1, dst);

    const errno_t e2 = strcpy_s(dst, sizeof(dst), "this string is too long for 16 bytes");
    std::printf("  strcpy_s 溢出场景返回 %d（34 = ERANGE），dst 被安全清空为 \"%s\"\n", e2, dst);

    // strcat_s 同理；好消息是它会检查目标剩余空间
    char cat[16] = "abc";
    const errno_t e3 = strcat_s(cat, sizeof(cat), "def");
    std::printf("  strcat_s 追加 \"def\"：返回 %d，结果 \"%s\"\n", e3, cat);

    // strncpy 有两个经典陷阱：
    //   1) 源串比 n 短时，它会把剩余空间全部补 '\0'（性能浪费）；
    //   2) 源串比 n 长时，它【不补 '\0'】—— 结果不是合法 C 字符串，printf 会越界读。
    char t1[8];
    std::strncpy(t1, "hello", sizeof(t1));  // 源比 n 短：后面全补 0
    t1[sizeof(t1) - 1] = '\0';              // ★ 保险起见总是手工补一个
    std::printf("  strncpy 短源：\"%s\"\n", t1);

    char t2[8];
    std::strncpy(t2, "0123456789", sizeof(t2));  // 源比 n 长：不补 '\0'
    t2[sizeof(t2) - 1] = '\0';                   // ★ 不加这一行 printf 就是越界读（UB）
    std::printf("  strncpy 长源（必须手工补 '\\0'）：\"%s\"\n", t2);

    // 裁剪式复制：C11 的 _TRUNCATE 让 strncpy_s 在放不下时截断而不是报错，
    // 但截断后依然保证以 '\0' 结尾，这是它比 strncpy 好的地方。
    char t3[8] = {0};
    // _TRUNCATE 是 MSVC 的扩展常量；其它实现里通常写作 ((size_t)-1)
    const errno_t e4 = strncpy_s(t3, sizeof(t3), "0123456789", _TRUNCATE);
    std::printf("  strncpy_s + _TRUNCATE：返回 %d（%s），结果 \"%s\"\n", e4,
                e4 == 0 ? "完全复制" : (e4 == STRUNCATE ? "被截断，但仍是合法 C 字符串" : "其它错误"),
                t3);

    // snprintf：不负责拷贝，但负责「格式化且不越界」，日常最实用
    char t4[8] = {0};
    const int need = std::snprintf(t4, sizeof(t4), "%d-%d", 12345, 678);
    std::printf("  snprintf 到 8 字节：内容 \"%s\"，返回值 %d（>= 8 表示被截断，需要 %d 字节）\n",
                t4, need, need + 1);

    std::printf("\n  -> 结论：*_s 是平台扩展不是通用方案；C++ 里一律用 std::string，\n");
    std::printf("     需要格式化就用 std::format 或 snprintf。\n");
}

// ===========================================================================
// 3. strtok 的三宗罪，以及 C++ 的正确替代
// ===========================================================================
void DemoStrtok() {
    Section("3. strtok 的问题 与 C++ 替代方案");

    std::printf("  【历史上怎么写】\n");
    // 三个必须注意的点：
    //   1) 原串必须可写：strtok 会把分隔符【原地】改成 '\0'。传字符串字面量 = UB。
    //   2) 第一次传原串，后续传 nullptr —— 因为切分位置存在函数内部的静态变量里。
    //   3) 内部静态状态 => 非线程安全；也无法在两个字符串上交替切分（嵌套调用会互相破坏）。
    char line[] = "apple,banana;orange grape";
    std::printf("    切分前：\"%s\"\n", line);
    const char* kSep = ",; ";
    for (char* tok = std::strtok(line, kSep); tok != nullptr; tok = std::strtok(nullptr, kSep)) {
        std::printf("    [%s]\n", tok);
    }
    std::printf("    切分后原串变成了：\"%s\"（第一段，后面的 ',' 已被改成 '\\0'）\n", line);
    std::printf("    验证：原串第 6 个字节现在是 %d（'\\0'），不是 ',' 的 %d\n", line[5],
                static_cast<int>(','));

    std::printf("\n  【MSVC 的 strtok_s】\n");
    // MSVC 提供全局命名空间的 strtok_s（注意：std::strtok_s 不存在，会报 C2039）。
    // 它把静态状态改成由调用方持有的 save 指针，因此可重入、可嵌套。
    // 但它仍然是 MSVC（更准确说是 Annex K）扩展，不可移植，而且依然修改原串。
    char line2[] = "a=1&b=2&c=3";
    char* save = nullptr;
    std::printf("    ");
    for (char* tok = strtok_s(line2, "&", &save); tok != nullptr;
         tok = strtok_s(nullptr, "&", &save)) {
        std::printf("[%s] ", tok);
    }
    std::printf("\n");

    std::printf("\n  【C++ 正确做法 A：string_view 手写切分（推荐，零拷贝）】\n");
    const std::string_view sv = "apple,banana;orange grape";
    std::printf("    ");
    std::size_t start = 0;
    while (start <= sv.size()) {
        // find_first_of 找到任意一个分隔符出现的位置
        std::size_t pos = sv.find_first_of(",; ", start);
        if (pos == std::string_view::npos) {
            pos = sv.size();  // 没有更多分隔符了，最后一段到末尾
        }
        if (pos > start) {  // 跳过连续分隔符产生的空串
            std::printf("[%.*s] ", static_cast<int>(pos - start), sv.data() + start);
        }
        if (pos == sv.size()) break;
        start = pos + 1;
    }
    std::printf("\n    特点：不修改原串、不分配内存、可重入、可嵌套、O(n)。\n");
    std::printf("    前提：原串的生命周期必须长于所有 string_view（见 06_cpp_string.cpp）。\n");

    std::printf("\n  【C++ 正确做法 B：getline + stringstream（需要字符串化时用）】\n");
    std::istringstream iss("apple,banana,orange,grape");
    std::string item;
    std::printf("    ");
    while (std::getline(iss, item, ',')) {  // 第三个参数就是自定义分隔符
        std::printf("[%s] ", item.c_str());
    }
    std::printf("\n    特点：代码最短、天然处理「跳过空段」的语义差异；代价是构造流对象。\n");

    std::printf("\n  -> 结论：新代码不要用 strtok。切分 string_view 用做法 A；\n");
    std::printf("     已经拿到 string 又要顺便转成数字/结构体时用做法 B。\n");
}

// ===========================================================================
// 4. memset / memcpy 的适用边界
// ===========================================================================
struct PlainOld {
    int id;
    double value;
    char tag[8];
};

// 用类型萃取在编译期把「能不能 memcpy」这件事变成可检查的事实
static_assert(std::is_trivially_copyable<PlainOld>::value,
              "PlainOld 是平凡可拷贝类型，可以安全地 memcpy");
static_assert(!std::is_trivially_copyable<std::string>::value,
              "std::string 不是平凡可拷贝类型，禁止 memcpy");
static_assert(!std::is_trivially_copyable<std::vector<int>>::value,
              "std::vector 不是平凡可拷贝类型，禁止 memcpy");

void DemoMemFunctions() {
    Section("4. memset / memcpy 只对「平凡可拷贝」类型成立");

    // --- memset：逐字节填同一个值 ---
    char raw[16];
    std::memset(raw, 'A', sizeof(raw) - 1);  // 前 15 字节填 'A'
    raw[15] = '\0';                          // ★ 尾部补 '\0'，否则 printf("%s") 越界读
    std::printf("  memset 填 'A'：\"%s\"（%zu 个字符）\n", raw, std::strlen(raw));

    // memset 清零是安全的（全 0 位模式对所有平凡类型都是有效表示）
    PlainOld pod{};
    std::memset(&pod, 0, sizeof(pod));
    std::printf("  memset(&pod, 0, sizeof(pod)) 后：id=%d value=%g（全 0 位模式有效）\n",
                pod.id, pod.value);

    // ★ 但 memset 非 0 值只对「单字节类型」有意义：
    //   memset(arr, 1, sizeof(arr)) 不会把 int 设成 1，
    //   而是把每个字节设成 0x01，得到 0x01010101 = 16843009。
    int arr[1] = {0};
    std::memset(arr, 1, sizeof(arr));
    std::printf("  ★ memset(int 数组, 1, ...) 的实际结果 = %d（不是 1！）\n", arr[0]);
    std::printf("     正确做法：std::fill(std::begin(arr), std::end(arr), 1);\n");

    // --- memcpy：逐字节复制，只对平凡可拷贝类型安全 ---
    const PlainOld src{7, 3.14, "tag"};
    PlainOld dst{};
    std::memcpy(&dst, &src, sizeof(PlainOld));
    std::printf("  memcpy 平凡结构体：id=%d value=%.2f tag=%s\n", dst.id, dst.value, dst.tag);

    // memmove：允许源和目标重叠；memcpy 在重叠时是 UB
    char overlap[] = "0123456789";
    std::memmove(overlap + 2, overlap, 5);  // 把前 5 字节右移 2 格（重叠！）
    std::printf("  memmove 处理重叠区间：\"%s\"（重叠时必须用 memmove 而非 memcpy）\n", overlap);

    // --- 危险演示：对含 std::string 的对象做 memcpy ---
    std::printf("\n  ★★ 对含 std::string 的对象 memcpy：未定义行为 ★★\n");
    struct WithString {
        int id;
        std::string name;  // 内部持有堆指针（或 SSO 缓冲区），有析构函数
    };
    static_assert(!std::is_trivially_copyable<WithString>::value, "含 string 成员 => 非平凡");
    static_assert(std::is_trivially_copyable<PlainOld>::value, "PlainOld 才允许 memcpy");

    WithString a;
    a.id = 1;
    a.name = "a-long-enough-name-to-avoid-sso";  // 超过 SSO 容量，走堆分配
    std::printf("    原对象 a：id=%d name=\"%s\"\n", a.id, a.name.c_str());

    // 【正确做法】走编译器生成的拷贝构造：std::string 会自己深拷贝一份堆内存
    WithString good = a;
    std::printf("    正确拷贝 WithString good = a; -> name=\"%s\"，与 a 共享缓冲区？%s\n",
                good.name.c_str(),
                (good.name.data() == a.name.data()) ? "是" : "否（各自独立，安全）");

    // 【错误做法】逐字节 memcpy：没有调用拷贝构造
    // 为了「既看到错误、又不让进程真的崩掉」，这里把结果放进一块原始字节缓冲区，
    // 而不是一个真正的 WithString 对象 —— 那样会有两个对象去析构同一块内存。
    alignas(WithString) unsigned char badBytes[sizeof(WithString)] = {};
    std::memcpy(badBytes, &a, sizeof(WithString));  // ★ UB：没有调用 std::string 的拷贝构造

    // 用「原始字节快照」做对照，不通过对象调用任何 string 成员函数
    unsigned char srcBytes[sizeof(WithString)] = {};
    std::memcpy(srcBytes, &a, sizeof(WithString));

    const std::size_t kNameOff = offsetof(WithString, name);  // name 字段在对象里的偏移
    const bool sameName = (std::memcmp(badBytes + kNameOff, srcBytes + kNameOff,
                                       sizeof(std::string)) == 0);
    std::printf("    memcpy 副本里 name 子对象的字节与源对象完全一致？%s\n", sameName ? "是" : "否");
    std::printf("    -> 这说明 memcpy 把 string 子对象【按字节】整体搬了过去，\n");
    std::printf("       里面记录「数据在哪」的那个指针字段也被原样复制，\n");
    std::printf("       但指向的堆内容没有被复制：这就是浅拷贝。\n");
    std::printf("    -> 后果（真实代码里一定会发生）：\n");
    std::printf("       a 和副本的 string 指向同一块堆内存；\n");
    std::printf("       谁先析构谁就把内存还给堆，另一个再用就是 use-after-free；\n");
    std::printf("       两个都析构 = double free（Windows 上直接弹堆损坏断言）。\n");
    std::printf("       典型症状：Debug 下报「堆已损坏」，Release 下偶发崩溃、换编译器变样。\n");
    std::printf("    安全写法总结：\n");
    std::printf("      - memcpy 只用于 trivially copyable 类型（int/double/POD/数组）；\n");
    std::printf("      - 类类型一律用拷贝构造/赋值，或 std::copy / std::copy_n；\n");
    std::printf("      - 需要「移动」大对象时用 std::move + 移动构造，而不是 memcpy；\n");
    std::printf("      - 需要按字节比较时用 std::memcmp；判断能否 memcpy 用\n");
    std::printf("        std::is_trivially_copyable 在编译期断言 + static_assert。\n");
}

// ===========================================================================
// 5. 悬垂指针：保存了 c_str() / 迭代器再修改容器
// ===========================================================================
void DemoDanglingPointer() {
    Section("5. 悬垂指针：c_str() 与迭代器的有效期");

    // --- string 版本 ---
    std::string s = "short";  // 长度 5，会落在 SSO 的栈内缓冲区里
    const char* p = s.c_str();
    std::printf("  s = \"%s\"，c_str() 指针 = %p\n", s.c_str(), static_cast<const void*>(p));

    // 追加到超过 SSO 容量后，string 必须搬到堆上，原指针立刻失效
    s.append(64, 'x');
    std::printf("  追加 64 个字符后：指针 = %p，%s\n", static_cast<const void*>(s.c_str()),
                (s.c_str() == p) ? "地址没变（还在 SSO 里）" : "地址变了（老指针 p 已悬垂！）");
    std::printf("  ★ 用 c_str() 得到指针后，任何可能触发扩容的操作都会让它失效：\n");
    std::printf("    append / push_back / insert / reserve / resize / += / 赋新值 ……\n");
    std::printf("    规则：指针只在「下一次非 const 操作之前」有效，用完即弃。\n");

    // --- vector 版本 ---
    std::vector<int> v = {1, 2, 3};
    const int* vp = v.data();
    const std::size_t capBefore = v.capacity();
    std::printf("\n  vector 初始：size=%zu capacity=%zu data()=%p\n", v.size(), capBefore,
                static_cast<const void*>(vp));
    v.push_back(4);
    std::printf("  push_back 之后：size=%zu capacity=%zu data()=%p -> %s\n", v.size(), v.capacity(),
                static_cast<const void*>(v.data()),
                (v.data() == vp) ? "地址未变，老指针仍有效" : "发生扩容，老指针/迭代器全部失效");

    // 迭代器同理：下面这段注释掉的代码是经典的崩溃写法
    // auto it = v.begin();
    // v.push_back(5);          // 可能扩容
    // *it = 99;                // ★ UB：it 已失效
    std::printf("  ★ 迭代器失效规则详见 07_sequence_containers.cpp 的失效规则表。\n");
    std::printf("    最实用的一条：不要在「持有迭代器的循环体里」做可能扩容的操作。\n");
}

}  // namespace

int main() {
    EnableUtf8Console();

    std::printf("==========================================================\n");
    std::printf(" 03_cstring_and_memory.cpp —— <cstring> 与内存操作\n");
    std::printf("==========================================================\n");

    DemoBasicCString();
    DemoSecureVersions();
    DemoStrtok();
    DemoMemFunctions();
    DemoDanglingPointer();

    std::printf("\n================ 小结 ================\n");
    std::printf("1. strcpy / strcat / sprintf 都不检查边界，是缓冲区溢出的主要来源。\n");
    std::printf("2. *_s 是平台扩展（C11 附录 K），不是可移植方案；C++ 请用 std::string。\n");
    std::printf("3. strtok 改原串 + 用内部静态状态，非线程安全、不能嵌套 —— 改用 string_view 切分。\n");
    std::printf("4. memset/memcpy 只对平凡可拷贝类型安全；对 std::string 用 memcpy 是 UB（共享堆内存）。\n");
    std::printf("5. c_str() 与迭代器都有有效期，任何扩容操作都会让它们失效。\n");
    return 0;
}
