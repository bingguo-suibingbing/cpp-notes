// ============================================================================
//  09_struct_layout.cpp  —— 01-basics 第 9 篇
// ----------------------------------------------------------------------------
//  演示主题：
//    1. 为什么有 padding：成员对齐 + 整体补齐两条规则
//    2. 用 sizeof / offsetof / 指针差三种手段实测成员偏移
//    3. 成员排列顺序如何影响结构体大小（同数据不同大小的真实案例）
//    4. alignas / alignof：手动控制对齐（缓存行、SIMD）
//    5. #pragma pack：把结构体压紧（网络协议、文件格式）
//    6. 协议/序列化的正确做法：不要依赖内存布局，逐字段显式编解码
//    7. 位域（bit-field）的用途与陷阱
//
//  关键结论：
//    * struct 的大小 != 成员大小之和，中间和末尾都可能有 padding。
//    * 小成员被大成员夹住最浪费；把同类数据聚在一起通常最省。
//    * #pragma pack 能改布局，但会牺牲访问性能、破坏平台兼容性，只在协议边界用。
//    * 跨进程/跨机器传输数据时，永远显式逐字段编解码，不要 sizeof(struct) + write。
// ============================================================================

#define NOMINMAX
#include <windows.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

namespace {

void use_utf8_console() {
    static_cast<void>(SetConsoleOutputCP(CP_UTF8));
}

void print_title(const char* text) {
    std::cout << "\n==== " << text << " ====\n";
}

// ---------------------------------------------------------------------------
// 1. 同一个「两个成员」的结构体，顺序不同 -> 大小相同（尾部补齐吃掉了好处）
// ---------------------------------------------------------------------------
struct CharThenDouble { char c; double d; };  // 1 + 7 pad + 8 = 16
struct DoubleThenChar { double d; char c; };  // 8 + 1 + 7 tail = 16

// ---------------------------------------------------------------------------
// 2. 三个成员时顺序就真的影响大小
// ---------------------------------------------------------------------------
struct BadOrder {   // 1 + 3 pad + 4 + 8 = 16
    char c;
    int i;
    double d;
};

struct GoodOrder {  // 8 + 4 + 1 + 3 tail = 16
    double d;
    int i;
    char c;
};

// ---------------------------------------------------------------------------
// 3. 十几个字节的结构体，顺序能省下可观比例
// ---------------------------------------------------------------------------
struct PoorlyPacked {
    char a;        // 0
    double b;      // 8  （7 字节 padding）
    char c;        // 16
    int d;         // 20 （3 字节 padding）
    char e;        // 24
    double f;      // 32 （7 字节 padding）
};                 // 共 40

struct WellPacked {
    double b;      // 0
    double f;      // 8
    int d;         // 16
    char a;        // 20
    char c;        // 21
    char e;        // 22
};                 // 23 -> 尾部补齐到 24

// ---------------------------------------------------------------------------
// 4. #pragma pack(1)：取消所有 padding，布局由声明顺序决定
//    【警告】只在读别人定义的二进制格式时用。改了自己的结构体布局后，
//    x86/x64 允许非对齐访问（只是慢一点），但某些 ARM 平台会直接异常。
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct PackedHeader {
    std::uint8_t version;    // 0
    std::uint32_t length;    // 1，没有 padding
    std::uint16_t flags;     // 5
    std::uint8_t checksum;   // 7
};                           // 共 8 字节
#pragma pack(pop)

struct NaturalHeader {
    std::uint8_t version;    // 0
    std::uint32_t length;    // 4（3 字节 padding）
    std::uint16_t flags;     // 8
    std::uint8_t checksum;   // 10
};                           // 共 12（尾部补 1 字节到 4 的倍数）

// ---------------------------------------------------------------------------
// 5. alignas：把结构体对齐到缓存行，避免多线程「伪共享（false sharing）」
// ---------------------------------------------------------------------------
// C4324 是「因为对齐说明符而产生了填充」的提示性警告：这里 alignas(64) 让
// struct 从 8 字节涨到 64 字节，正是本节想演示的效果，所以就地关掉这条警告。
// 工程习惯：把这类「有意为之」的警告用 push/pop 局部关闭，并写清理由，
// 不要全局 /wd 掉——那样会连真正的问题一起藏起来。
#pragma warning(push)
#pragma warning(disable : 4324)   // structure was padded due to alignment specifier
struct alignas(64) CacheLineCounter {
    std::int64_t value;
};
#pragma warning(pop)

// 对照：不加 alignas 时两个计数器可能落在同一缓存行
struct PlainCounter {
    std::int64_t value;
};

// ---------------------------------------------------------------------------
// 6. 位域：用「位」而不是「字节」存标志。紧凑但可移植性差。
// ---------------------------------------------------------------------------
struct FlagsWithBitfield {
    unsigned int readable : 1;
    unsigned int writable : 1;
    unsigned int executable : 1;
    unsigned int reserved : 5;
    unsigned int mode : 4;
};

// ---------------------------------------------------------------------------
// 7. 序列化正确做法：显式按字节编解码（小端序示例）
// ---------------------------------------------------------------------------
void write_u32_le(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFU));
}

std::uint32_t read_u32_le(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

}  // namespace

int main() {
    use_utf8_console();

    // ======================================================================
    print_title("1. padding 的两条规则");
    // ======================================================================
    // 规则 A（成员对齐）：每个成员的起始偏移必须是该成员对齐值的整数倍。
    //        对齐值通常等于 sizeof（char=1、int=4、double=8），但也可以用 alignas 改。
    // 规则 B（整体补齐）：结构体的总大小必须是「最大成员对齐值」的整数倍。
    //        这样 struct S arr[10]; 里每个元素都满足规则 A。
    // 两条规则合起来解释了所有「看不见的字节」。
    std::cout << "  struct {char,double}: sizeof = " << sizeof(CharThenDouble) << '\n';
    std::cout << "    offsetof(d) = " << offsetof(CharThenDouble, d)
              << "（规则 A：double 要 8 对齐，所以 char 后面补 7 字节）\n";
    std::cout << "  struct {double,char}: sizeof = " << sizeof(DoubleThenChar) << '\n';
    std::cout << "    offsetof(c) = " << offsetof(DoubleThenChar, c)
              << "（规则 A 满足了，但规则 B 又补了 7 字节到 16）\n";
    std::cout << "  结论：两个成员时，两种顺序都得到 16 字节——尾部补齐抹平了差异\n";
    static_assert(sizeof(CharThenDouble) == 16);
    static_assert(sizeof(DoubleThenChar) == 16);

    // ======================================================================
    print_title("2. 三种手段实测成员偏移");
    // ======================================================================
    BadOrder plain_bad{};
    // 手段 1：offsetof（编译期常量，最推荐）
    std::cout << "  offsetof(BadOrder, c) = " << offsetof(BadOrder, c) << '\n';
    std::cout << "  offsetof(BadOrder, i) = " << offsetof(BadOrder, i) << '\n';
    std::cout << "  offsetof(BadOrder, d) = " << offsetof(BadOrder, d) << '\n';
    // 手段 2：指针差（运行期实测，能验证 offsetof 的结论）
    const std::ptrdiff_t measured_gap =
        reinterpret_cast<const char*>(&plain_bad.i) - reinterpret_cast<const char*>(&plain_bad.c);
    std::cout << "  指针差测 c 到 i 的距离 = " << measured_gap
              << "（char 占 1 字节 + 3 字节 padding）\n";
    // 手段 3：把结构体当字节数组看，直接打印十六进制（调试内存布局最直观）
    BadOrder probe{};
    probe.c = 0x11;
    probe.i = 0x22334455;
    probe.d = 0.0;
    std::cout << "  把 BadOrder 当字节数组看：";
    const auto* raw = reinterpret_cast<const unsigned char*>(&probe);
    for (std::size_t k = 0; k < sizeof(probe); ++k) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned>(raw[k]) << ' ';
    }
    std::cout << std::dec << std::setfill(' ') << '\n';
    std::cout << "    第一个 11 是 c，接着 3 个字节的 00 就是 padding，"
                 "然后是 i 的 55 44 33 22（小端序：低字节在前）\n";
    static_assert(offsetof(BadOrder, c) == 0);
    static_assert(offsetof(BadOrder, i) == 4);
    static_assert(offsetof(BadOrder, d) == 8);

    // ======================================================================
    print_title("3. 成员顺序决定结构体大小");
    // ======================================================================
    std::cout << "  BadOrder  (char,int,double)  sizeof = " << sizeof(BadOrder)
              << "，浪费 " << sizeof(BadOrder) - (1 + 4 + 8) << " 字节\n";
    std::cout << "  GoodOrder (double,int,char)  sizeof = " << sizeof(GoodOrder)
              << "，浪费 " << sizeof(GoodOrder) - (1 + 4 + 8) << " 字节\n";
    std::cout << "  PoorlyPacked sizeof = " << sizeof(PoorlyPacked)
              << "（有效数据 " << (1 + 8 + 1 + 4 + 1 + 8) << " 字节）\n";
    std::cout << "  WellPacked   sizeof = " << sizeof(WellPacked)
              << "（有效数据 " << (8 + 8 + 4 + 1 + 1 + 1) << " 字节）\n";
    std::cout << "  同一批数据，重新排列后省下 "
              << sizeof(PoorlyPacked) - sizeof(WellPacked) << " 字节（"
              << (100.0 * (sizeof(PoorlyPacked) - sizeof(WellPacked)) / sizeof(PoorlyPacked))
              << "%）\n";
    std::cout << "  这种优化在「结构体数组有百万个元素」时才值得做；"
                 "做了就要写注释说明顺序是有意的\n";
    static_assert(sizeof(WellPacked) < sizeof(PoorlyPacked));

    // ======================================================================
    print_title("4. alignas / alignof：手动控制对齐");
    // ======================================================================
    // 用途：把频繁被不同线程写入的计数器各自放到独立的缓存行上，
    //       避免「伪共享」——一个核心改了变量，导致另一个核心的缓存行失效。
    std::cout << "  alignof(char)=" << alignof(char) << " alignof(int)=" << alignof(int)
              << " alignof(double)=" << alignof(double)
              << " alignof(void*)=" << alignof(void*) << '\n';
    std::cout << "  sizeof(CacheLineCounter) = " << sizeof(CacheLineCounter)
              << "，alignof = " << alignof(CacheLineCounter)
              << "（用 alignas(64) 顶到缓存行大小）\n";
    std::cout << "  sizeof(PlainCounter)     = " << sizeof(PlainCounter)
              << "，alignof = " << alignof(PlainCounter) << '\n';
    std::cout << "  代价：每个计数器多占 " << sizeof(CacheLineCounter) - sizeof(PlainCounter)
              << " 字节来换取多线程下的性能。只有在 profiler 指出伪共享时才做\n";
    // 缓存行大小可以用 Win32 的 GetLogicalProcessorInformation 查询（本仓库只做说明）
    std::cout << "  缓存行大小不是语言规定值（x86/x64 常见 64 字节），"
                 "Windows 上可用 GetLogicalProcessorInformation 查询\n";
    static_assert(alignof(CacheLineCounter) == 64);
    static_assert(sizeof(CacheLineCounter) == 64);

    // ======================================================================
    print_title("5. #pragma pack：把结构体压紧");
    // ======================================================================
    std::cout << "  #pragma pack(1) 之后：\n";
    std::cout << "    sizeof(PackedHeader)  = " << sizeof(PackedHeader)
              << "，offsetof(length) = " << offsetof(PackedHeader, length)
              << "（version 占 1 字节，length 紧跟着，无 padding）\n";
    std::cout << "  不 pack 时：\n";
    std::cout << "    sizeof(NaturalHeader) = " << sizeof(NaturalHeader)
              << "，offsetof(length) = " << offsetof(NaturalHeader, length)
              << "（为 4 字节对齐补了 3 字节）\n";
    std::cout << "  省了 " << sizeof(NaturalHeader) - sizeof(PackedHeader) << " 字节\n";
    std::cout << "  【代价】(1) 非对齐访问在 x86/x64 上只是变慢，在部分 ARM 上会异常；\n";
    std::cout << "          (2) 结构体的 alignof 变成 1，拿它当数组元素会失去对齐保证；\n";
    std::cout << "          (3) 布局从此与编译器/平台绑定，换编译选项就可能变。\n";
    static_assert(sizeof(PackedHeader) == 8);
    static_assert(offsetof(PackedHeader, length) == 1);

    // ======================================================================
    print_title("6. 协议 / 序列化的正确做法");
    // ======================================================================
    // 【反模式】把结构体直接当字节流收发：
    //     struct Msg { uint8_t type; uint32_t len; };
    //     send(sock, &msg, sizeof(msg), 0);       // 发送了 padding，还依赖字节序和对齐
    //   问题：padding 内容不确定（可能泄露内存里的旧数据！）、字节序不一致、
    //         不同编译器/平台的布局可能不同、升级协议时字段顺序一动全废。
    // 【正解】明确定义字节级格式，逐字段显式编解码。
    std::vector<std::uint8_t> buffer;
    write_u32_le(buffer, 0x11223344U);
    write_u32_le(buffer, 7U);
    std::cout << "  显式小端编码 0x11223344 得到字节：";
    for (std::uint8_t byte : buffer) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned>(byte) << ' ';
    }
    std::cout << std::dec << std::setfill(' ') << '\n';
    std::cout << "  再解回来：" << std::hex << read_u32_le(buffer.data()) << std::dec
              << "，第二个数：" << read_u32_le(buffer.data() + 4) << '\n';
    // 本机字节序
    if constexpr (std::endian::native == std::endian::little) {
        std::cout << "  本机字节序：小端（x64 Windows）。注意 std::endian 在 C++20 里"
                     "被标记为 deprecated，但仍是标准里最方便的查询方式\n";
    } else {
        std::cout << "  本机字节序：大端\n";
    }
    // C++23 起有 std::byteswap 可以直接翻字节序（MSVC 19.51 已支持）
#if defined(__cpp_lib_byteswap)
    std::cout << "  std::byteswap(0x11223344) = " << std::hex
              << std::byteswap(0x11223344U) << std::dec << "（C++23 的字节序翻转）\n";
#else
    std::cout << "  本工具链未提供 std::byteswap，可手写移位或改用协议库\n";
#endif
    std::cout << "  工程建议：优先用成熟的序列化方案（protobuf / flatbuffers / 自研 IDL），"
                 "而不是手抠字节\n";

    // ======================================================================
    print_title("7. 位域（bit-field）");
    // ======================================================================
    FlagsWithBitfield flags{};
    flags.readable = 1;
    flags.writable = 0;
    flags.executable = 1;
    flags.reserved = 0;
    flags.mode = 0b1001;
    std::cout << "  sizeof(FlagsWithBitfield) = " << sizeof(FlagsWithBitfield)
              << "（5 + 4 = 9 个位，用一个 unsigned int 装下）\n";
    std::cout << "  readable=" << flags.readable << " writable=" << flags.writable
              << " executable=" << flags.executable << " mode=" << flags.mode << '\n';
    std::cout << "  位域的用途：硬件寄存器映射、协议里「一个字节拆成几个标志」的场景\n";
    std::cout << "  陷阱：(1) 位域的布局（谁在高位）是【实现定义】的，不能用于跨平台协议；\n";
    std::cout << "        (2) 取地址 &flags.readable 非法；\n";
    std::cout << "        (3) 位域与字节序、有无符号混合时行为容易出乎意料。\n";
    std::cout << "  替代方案：用一个 std::uint32_t + 掩码/移位，或 std::bitset，可移植且可测\n";
    // 位运算替代方案演示
    std::uint32_t raw_flags = 0;
    constexpr std::uint32_t kReadable = 1U << 0;
    constexpr std::uint32_t kExecutable = 1U << 2;
    raw_flags |= kReadable;
    raw_flags |= kExecutable;
    std::cout << "  掩码方案：raw_flags = 0b" << std::hex << raw_flags << std::dec
              << "，判断可读 = " << std::boolalpha
              << ((raw_flags & kReadable) != 0U) << "，判断可写 = "
              << ((raw_flags & (1U << 1)) != 0U) << '\n';
    std::cout << std::noboolalpha;

    std::cout << "\n[09] 结束。下一步：读 NOTES.md，然后按章节顺序练习。\n";
    return 0;
}
