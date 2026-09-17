// ============================================================================
//  07_memory_model.cpp  —— 01-basics 第 7 篇
// ----------------------------------------------------------------------------
//  演示主题：
//    1. 四种存储区：栈 / 堆 / 静态区 / 常量区，各自的地址特征与生命周期
//    2. memcpy 的真实签名与参数顺序（原笔记这里注释和示例互相矛盾）
//    3. memcpy vs std::copy vs std::copy_backward（重叠区间怎么办）
//    4. new / delete、new[] / delete[] 必须配对，以及写错的后果
//    5. 内存泄漏与悬垂指针（含 use-after-free）
//    6. 为什么 std::vector / std::string / 智能指针几乎总能取代裸 new
//    7. 结构体对齐与 padding：用 offsetof 实测「看不见的字节」
//    8. 内存排布的工程含义：为什么要按大小降序排列成员
//
//  关键结论：
//    * memcpy(dest, src, n)：第一个参数是【目的地】，第二个是【来源】，
//      第三个是字节数。原笔记把顺序写反了，本篇用实测证明。
//    * memcpy 只适合「可平凡拷贝」的类型；C++ 对象之间请用 std::copy / 赋值。
//    * 有 new 就必须有 delete，有 new[] 就必须有 delete[]，类型和数量都要对上。
//    * 结构体里看不见的 padding 可能占掉一半空间；按大小降序排成员能省内存。
// ============================================================================

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
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

// 用于演示的「可平凡拷贝」结构体
struct PlainPod {
    int x;
    int y;
};

// 用于演示对齐的几组结构体：成员一样，顺序不同，大小就不同
struct LayoutWasteful {   // 差顺序：char 后面紧跟 double
    char flag;            // offset 0，占 1 字节
    double value;         // offset 8：为了 8 字节对齐，中间塞了 7 字节 padding
    int count;            // offset 16
};                        // 1 + 7(padding) + 8 + 4 = 20 -> 补齐到 24

struct LayoutCompact {    // 好顺序：大的在前，小的在后
    double value;         // offset 0
    int count;            // offset 8
    char flag;            // offset 12
};                        // 8 + 4 + 1 = 13 -> 尾部补齐到 16

struct CharIntDoubleA {   // 1 + 3(padding) + 4 + 8 = 16
    char c;
    int i;
    double d;
};

struct CharIntDoubleB {   // 8 + 4 + 1 + 3(tail) = 16，和 A 一样大
    double d;
    int i;
    char c;
};

// 同一批数据、两种排列，这一个例子里顺序真的影响了大小
struct Interleaved {  // 4 + 1+3pad + 4 + 1+3tail = 16
    int a;
    char c;
    int b;
    char d;
};

struct Grouped {      // 4 + 4 + 1 + 1 + 2tail = 12
    int a;
    int b;
    char c;
    char d;
};

int global_initialized = 42;      // 静态存储区（.data）
int global_uninitialized;          // 静态存储区（.bss），零初始化
const int global_const = 7;        // 常量区（通常与只读段合在一起）

// 故意泄漏：返回一块没有被释放的堆内存
int* leak_one_int() {
    int* leaked = new int(123);
    return leaked;  // 调用方如果忘记 delete，这 4 字节就泄漏了
}

// 演示 use-after-free 的函数：故意在 delete 之后读
int use_after_free_demo() {
    int* data = new int(55);
    int before = *data;
    delete data;
    // 【错误】下面这行是 use-after-free（UB）。为了保持程序「看起来正常」并
    // 在 -W4/-WX 下编译，这里不真的解引用，只返回删除前读到的值。
    // int after = *data;   // UB：读到的是已被回收的内存
    return before;
}

}  // namespace

int main() {
    use_utf8_console();

    // ======================================================================
    print_title("1. 四种存储区");
    // ======================================================================
    // | 区域   | 存放什么                       | 何时分配/释放     | 特点             |
    // | 栈     | 局部变量、函数参数、返回地址   | 进入/离开作用域   | 快、有大小上限   |
    // | 堆     | new / malloc 出来的对象        | 手动或智能指针    | 灵活、慢、要管理 |
    // | 静态区 | 全局变量、static 变量          | 程序启动/结束时   | 生命周期 = 进程  |
    // | 常量区 | 字符串字面量、const 全局       | 程序启动/结束时   | 只读，写入会崩   |
    int stack_local = 1;
    int* heap_int = new int(2);
    static int static_local = 3;
    const char* literal = "literal string";

    std::cout << "  栈上局部变量   &stack_local   = " << static_cast<const void*>(&stack_local) << '\n';
    std::cout << "  堆上对象       heap_int       = " << static_cast<const void*>(heap_int) << '\n';
    std::cout << "  静态区变量     &static_local  = " << static_cast<const void*>(&static_local) << '\n';
    std::cout << "  全局（已初始化）&global_initialized = "
              << static_cast<const void*>(&global_initialized) << '\n';
    std::cout << "  全局（未初始化）&global_uninitialized = "
              << static_cast<const void*>(&global_uninitialized)
              << "，值是 " << global_uninitialized << "（.bss 段自动清零）\n";
    std::cout << "  常量/字面量    literal       = " << static_cast<const void*>(literal) << '\n';
    std::cout << "  经验：栈地址和堆地址通常相差很远；同一次运行里栈变量之间很紧凑\n";
    // 栈的增长方向可以实测：同一函数内两个局部变量的地址差
    int stack_a = 0;
    int stack_b = 0;
    std::cout << "  同函数内 &stack_a - &stack_b = " << (&stack_a - &stack_b)
              << "（x64 上通常为负，说明栈向低地址增长）\n";
    delete heap_int;  // 别忘了配对释放

    // ======================================================================
    print_title("2. memcpy 的真实签名与参数顺序（原笔记纠错）");
    // ======================================================================
    // 【原笔记】注释写 memory(目标地址,数据来源地址,赋值大小)，
    //          但示例写 memcpy(a,b,sizeof(a)) 且上下文说「把 b 拷到 a」。
    //          注释是对的、示例的名字容易让人误解，而原笔记的注释函数名还写成了
    //          memory（实际是 memcpy，声明在 <cstring> 里，不在 <memory.h> 的 C++ 版本里）。
    // 【正确】在 C++ 里的签名是：
    //          void* memcpy(void* dest, const void* src, std::size_t count);
    //          语义：把 src 指向的 count 个【字节】复制到 dest。
    //          dest 是目的地、src 是来源、count 是字节数。memcpy 返回 dest。
    int dest_array[3] = {0, 0, 0};
    const int src_array[3] = {7, 8, 9};
    std::memcpy(dest_array, src_array, sizeof(dest_array));  // dest <- src
    std::cout << "  memcpy(dest, src, sizeof(dest)) 之后 dest = "
              << dest_array[0] << ' ' << dest_array[1] << ' ' << dest_array[2] << '\n';
    std::cout << "  参数 1 = 目的地 dest_array，参数 2 = 来源 src_array，参数 3 = 字节数\n";
    // 反过来写会把源数据改坏（同时读越界），这里只在注释里说明，不真的执行：
    //     std::memcpy(src_array, dest_array, sizeof(src_array));  // 想写目的地的写成了源
    std::cout << "  如果把两个参数写反，就是「拿目标去覆盖来源」，属于写错方向的经典事故\n";
    // sizeof 用对很关键：sizeof(指针) 是 8，不是数组长度！
    const int* src_ptr = src_array;
    std::cout << "  sizeof(dest_array) = " << sizeof(dest_array)
              << "（数组），sizeof(src_ptr) = " << sizeof(src_ptr)
              << "（指针）——写成 sizeof(指针) 就只拷 8 字节，是常见 bug\n";
    // memcpy 只会做「字节搬运」，对象的构造/析构一概不管：
    // 对含有 std::string 成员的对象用 memcpy 会产生两个对象共享同一块内存 -> double free。
    std::cout << "  memcpy 不做构造/析构：对含 std::string 成员的对象使用会造成双重释放\n";

    // ======================================================================
    print_title("3. memcpy vs std::copy vs std::copy_backward");
    // ======================================================================
    // memcpy ：按字节拷贝，要求区间不重叠（重叠要用 memmove），只对平凡可拷贝类型安全。
    // std::copy：按元素赋值，走类型的拷贝赋值运算符，对非平凡类型也正确。
    //            但对【重叠】区间同样要求「目标不在源的右侧」。
    // std::copy_backward：从后往前拷，专门解决「目标在源右侧且重叠」的情况。
    std::array<int, 5> plain = {1, 2, 3, 4, 5};
    std::vector<int> via_std_copy(5, 0);
    std::copy(plain.begin(), plain.end(), via_std_copy.begin());
    std::cout << "  std::copy 结果：";
    for (int value : via_std_copy) {
        std::cout << value << ' ';
    }
    std::cout << '\n';
    // 重叠区间：把 [1,2,3] 往右平移一位 -> 期望 1 1 2 3 5
    // copy_backward(first, last, d_last) 的语义是：把 [first,last) 拷到「以 d_last
    // 为结尾」的区间。要从右往左移动 3 个元素，d_last 必须是 end()。
    std::array<int, 5> overlap = {1, 2, 3, 4, 5};
    std::copy_backward(overlap.begin(), overlap.begin() + 3, overlap.end());
    std::cout << "  copy_backward 处理重叠（整体右移一位）：";    for (int value : overlap) {
        std::cout << value << ' ';
    }
    std::cout << '\n';
    std::array<int, 5> overlap2 = {1, 2, 3, 4, 5};
    std::memmove(overlap2.data() + 1, overlap2.data(), 3 * sizeof(int));
    std::cout << "  memmove 处理重叠（同样效果）：";
    for (int value : overlap2) {
        std::cout << value << ' ';
    }
    std::cout << '\n';
    // 非平凡类型：std::string 数组只能整体赋值/拷贝构造，不能 memcpy
    std::vector<std::string> names = {"alpha", "beta"};
    std::vector<std::string> names_copy = names;  // 走拷贝构造，正确
    std::cout << "  std::string 只能用 std::copy 或赋值：names_copy[0] = " << names_copy[0] << '\n';
    std::cout << "  工程建议：C++ 里优先 std::copy / 容器赋值，memcpy 只留给内存缓冲区和 C 接口\n";

    // ======================================================================
    print_title("4. new / delete 与 new[] / delete[] 必须配对");
    // ======================================================================
    // 规则：
    //   new T      <-> delete p
    //   new T[n]   <-> delete[] p
    // 两者的「分配记录」不同：数组形式的 new 会额外记录元素个数（以便析构每个元素
    // 并回收正确大小），delete（非数组）不会去读这个记录，于是行为未定义。
    int* single = new int(10);
    std::cout << "  new int(10)      -> *single = " << *single << '\n';
    delete single;
    int* array_of_int = new int[5]{1, 2, 3, 4, 5};
    std::cout << "  new int[5]{...}  -> array_of_int[2] = " << array_of_int[2] << '\n';
    delete[] array_of_int;  // 必须带方括号
    // 常见错误（这里只在注释里列出，不执行——都会破坏堆）：
    //   delete array_of_int;      // 错：用 delete 释放数组 -> UB
    //   delete[] single;          // 错：用 delete[] 释放单个对象 -> UB
    //   delete single; delete single;  // 错：重复释放（double free）
    //                            //    Debug 堆会直接报 heap corruption 并 abort
    std::cout << "  错误示范（注释里列出，不执行）：delete 数组、delete[] 单对象、重复 delete\n";
    // 正确做法：不用裸 new/delete，用容器或智能指针
    auto managed = std::make_unique<int[]>(5);  // C++14 起支持数组形式的 make_unique
    managed[0] = 99;
    std::cout << "  std::make_unique<int[]>(5) 自动配对释放：managed[0] = " << managed[0] << '\n';

    // ======================================================================
    print_title("5. 内存泄漏与悬垂指针");
    // ======================================================================
    // 内存泄漏：new 了没 delete，进程运行期间这块内存再也不能被复用。
    //   短命小工具里可能无所谓，但长期运行的服务里会持续增长直到 OOM。
    //   排查工具：Visual Studio 的「诊断工具 / 内存使用量」快照对比、
    //             CRT 调试堆 _CrtDumpMemoryLeaks()、AddressSanitizer（/fsanitize=address）。
    int* leaked = leak_one_int();
    std::cout << "  leak_one_int() 返回了 " << *leaked
              << "，但调用方没 delete -> 4 字节泄漏（这里我们故意演示泄漏）\n";
    delete leaked;  // 演示完补上，避免程序退出时 CRT 报告泄漏干扰读者
    // 悬垂指针：指针还在，指向的对象已经没了。
    //   三种典型来源：(1) delete 之后继续用（use-after-free）；
    //                (2) 指向局部变量（见 06 篇的 return &local）；
    //                (3) 容器扩容后旧的迭代器/指针（见 03 篇的范围 for 坑）。
    std::cout << "  use_after_free_demo() = " << use_after_free_demo()
              << "（真要在 delete 后解引用就是 UB，可能拿到旧值也可能是垃圾）\n";
    std::cout << "  防御习惯：delete 之后把指针置空 p = nullptr；"
                 "更好的做法是根本不用裸指针拥有对象\n";

    // ======================================================================
    print_title("6. 为什么容器与智能指针几乎总能取代裸 new");
    // ======================================================================
    // 裸 new 的负担：要记得 delete、要处理异常安全、要处理拷贝语义、
    // 要处理数组长度、要处理 self-assignment……这些都能被标准库接管。
    {
        // 动态数量的元素 -> std::vector（连续内存，缓存友好，自动扩容）
        std::vector<int> v(3, 7);
        v.push_back(8);
        std::cout << "  std::vector：size=" << v.size() << " capacity=" << v.capacity()
                  << "（自动扩容，析构时自动释放）\n";
        // 对象的所有权需要共享/转移 -> 智能指针
        auto exclusive = std::make_unique<PlainPod>(PlainPod{1, 2});  // 独占所有权
        std::shared_ptr<PlainPod> shared = std::make_shared<PlainPod>(PlainPod{3, 4});  // 共享所有权
        std::weak_ptr<PlainPod> observer = shared;  // 观察但不增加引用计数，打破循环引用
        std::cout << "  unique_ptr 独占：(*exclusive).x = " << exclusive->x
                  << "，use_count 不存在（零开销）\n";
        std::cout << "  shared_ptr 共享：shared->x = " << shared->x
                  << "，use_count = " << shared.use_count() << '\n';
        std::cout << "  weak_ptr 观察：observer.lock() 有效吗？" << std::boolalpha
                  << (observer.lock() != nullptr) << "，且不影响 use_count（"
                  << shared.use_count() << "）\n";
        std::cout << std::noboolalpha;
    }
    // 所有权选择表：
    //   独占 -> std::unique_ptr（默认选择，零额外开销，只可移动）
    //   共享 -> std::shared_ptr（有原子引用计数开销，慎用）
    //   借用 -> 裸指针或引用（明确「我不负责释放」）
    //   循环引用 -> 其中一边用 std::weak_ptr
    std::cout << "  选择顺序：栈对象 > unique_ptr > shared_ptr > 裸 new。绝大多数代码用不到裸 new\n";

    // ======================================================================
    print_title("7. 对齐与 padding：offsetof 实测看不见的字节");
    // ======================================================================
    // 为什么会有 padding：
    //   * 每个类型的「对齐要求」通常等于它的大小（char=1、int=4、double=8）。
    //   * 结构体成员的起始地址必须是该成员对齐值的整数倍。
    //   * 结构体整体大小必须是「最大成员对齐值」的整数倍，这样数组里每个元素都对齐。
    // 所以编译器会在成员之间和末尾插入不上不下的字节，就是你看到的内存「空洞」。
    std::cout << "  LayoutWasteful（char,double,int 的顺序）：\n";
    std::cout << "    sizeof = " << sizeof(LayoutWasteful) << '\n';
    std::cout << "    offsetof(flag)  = " << offsetof(LayoutWasteful, flag) << '\n';
    std::cout << "    offsetof(value) = " << offsetof(LayoutWasteful, value)
              << "   <- 前面有 7 字节 padding（char 占 1，double 要 8 对齐）\n";
    std::cout << "    offsetof(count) = " << offsetof(LayoutWasteful, count) << '\n';
    std::cout << "    有效数据 1+8+4 = 13 字节，实际占 " << sizeof(LayoutWasteful)
              << " 字节，浪费 " << sizeof(LayoutWasteful) - 13 << " 字节\n";
    std::cout << "\n  LayoutCompact（double,int,char 的顺序，成员完全相同）：\n";
    std::cout << "    sizeof = " << sizeof(LayoutCompact) << '\n';
    std::cout << "    offsetof(value) = " << offsetof(LayoutCompact, value) << '\n';
    std::cout << "    offsetof(count) = " << offsetof(LayoutCompact, count) << '\n';
    std::cout << "    offsetof(flag)  = " << offsetof(LayoutCompact, flag) << '\n';
    std::cout << "    有效数据也是 13 字节，padding 只剩末尾的 "
              << sizeof(LayoutCompact) - 13 << " 字节（为了让整体大小为 8 的倍数）\n";
    static_assert(offsetof(LayoutCompact, value) == 0);
    static_assert(offsetof(LayoutCompact, count) == 8);
    static_assert(offsetof(LayoutCompact, flag) == 12);
    static_assert(sizeof(LayoutCompact) == 16, "尾部补齐到 8 的倍数");

    // 用指针差实测「成员之间的真实距离」
    LayoutWasteful probe{};
    const std::ptrdiff_t gap_flag_to_value =
        reinterpret_cast<const char*>(&probe.value) - reinterpret_cast<const char*>(&probe.flag);
    std::cout << "\n  实测 flag 到 value 的字节距离 = " << gap_flag_to_value
              << "（char 只占 1 字节，但它后面必须空出 7 字节让 double 落在 8 的倍数地址上）\n";

    // 只有两个成员 char / double 时，顺序对大小的影响
    std::cout << "\n  两个成员时的对比：\n";
    struct CharThenDouble { char c; double d; };
    struct DoubleThenChar { double d; char c; };
    std::cout << "    struct {char,double} sizeof = " << sizeof(CharThenDouble)
              << "（1 + 7 padding + 8）\n";
    std::cout << "    struct {double,char} sizeof = " << sizeof(DoubleThenChar)
              << "（8 + 1 + 7 尾部补齐）  <- 恰好一样大，尾部补齐把好处吃掉了\n";
    static_assert(sizeof(CharThenDouble) == 16);
    static_assert(sizeof(DoubleThenChar) == 16, "尾部补齐让两者相同");

    // 补一个真正能省下空间的例子：把同类数据聚在一起
    std::cout << "    struct {int,char,int,char} = " << sizeof(Interleaved)
              << "，struct {int,int,char,char} = " << sizeof(Grouped)
              << "   <- 这个例子才真的省下 "
              << sizeof(Interleaved) - sizeof(Grouped) << " 字节\n";

    // 对齐值与查看方式
    std::cout << "  alignof(char)=" << alignof(char) << " alignof(int)=" << alignof(int)
              << " alignof(double)=" << alignof(double)
              << " alignof(void*)=" << alignof(void*) << '\n';
    std::cout << "  alignof 与 std::alignment_of 在 C++11 起可用；"
                 "C++11 还引入了 alignas / alignof 关键字\n";
    // 用 alignas 手动指定（比如为了配合 SIMD 或缓存行）。
    // 这么做会让结构体变大，MSVC 会提醒你：C4324「structure was padded due to
    // alignment specifier」。这不是错误，而是编译器确认「你确实造成了填充」。
#pragma warning(push)
#pragma warning(disable : 4324)  // 这里就是要演示 alignas 造成的填充
    struct alignas(32) CacheLineAligned {
        int value;
    };
#pragma warning(pop)
    std::cout << "  struct alignas(32) 示例：sizeof = " << sizeof(CacheLineAligned)
              << "，alignof = " << alignof(CacheLineAligned)
              << "（多线程下避免伪共享常用 64 字节缓存行对齐）\n";

    // ======================================================================
    print_title("8. 工程含义：为什么要按大小降序排列成员");
    // ======================================================================
    // 结论：把大对齐要求的成员放前面、小的放后面，能显著减少 padding。
    //   直观类比：先放大的箱子，再往缝里塞小的。反过来先塞小的就到处是洞。
    // 注意事项：
    //   * 这类手工优化只在结构体数量巨大、或结构体非常热（缓存敏感）时才值得做。
    //   * 一旦这么做，要写注释说明「顺序是有意的」，否则后人重排会踩回来。
    //   * 二进制序列化协议（网络包、文件格式）里【不能】依赖 padding：必须显式
    //     逐字段读写，或者用 #pragma pack / 序列化库，并注意字节序。
    std::cout << "  LayoutWasteful sizeof = " << sizeof(LayoutWasteful)
              << "；LayoutCompact sizeof = " << sizeof(LayoutCompact)
              << "，同成员不同顺序，省了 "
              << sizeof(LayoutWasteful) - sizeof(LayoutCompact) << " 字节\n";
    std::cout << "  序列化场景禁止依赖内存布局：必须逐字段读写，或用 #pragma pack + 字节序处理\n";
    // 检查一个常见误区：bool 是 1 字节，但三个 bool 加一个 int 并不会省
    struct ThreeBools { bool a; bool b; bool c; int n; };
    std::cout << "  struct {bool,bool,bool,int} sizeof = " << sizeof(ThreeBools)
              << "（3 + 1 padding + 4 = 8）\n";

    std::cout << "\n[07] 结束。下一步：08_build_and_debug.cpp\n";
    return 0;
}
