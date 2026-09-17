// ============================================================================
//  04_arrays_pointers_refs.cpp  —— 01-basics 第 4 篇
// ----------------------------------------------------------------------------
//  演示主题：
//    1. 数组退化（decay）：为什么 sizeof(a) 和 sizeof(&a) 不一样
//    2. a / *a / &a[0] / &a 的地址与类型（原笔记的实测打印，这里讲透）
//    3. 二维数组：int a[10][5] 为什么「元素类型是 int[5]」
//    4. 指针算术：p+n 等价于 p + n*sizeof(*p)，用实测地址证明
//    5. 引用 vs 指针：能否为空、能否重绑、语法差异
//    6. const 与指针的四种组合（读法口诀：const 在 * 左边修饰「指向的值」）
//    7. nullptr 而不是 NULL / 0（重载决议的证据）
//    8. 现代替代：std::array / std::span / std::string_view 不退化
//
//  关键结论：
//    * 数组名在大多数表达式里会退化成「首元素指针」，但 sizeof/&/decltype 不会。
//    * a 和 &a 的「值」相同，但类型不同，所以 a+1 和 &a+1 步长差很多。
//    * 引用必须初始化、不能为空、不能重绑；指针可以三者都行，代价是要检查。
//    * 用 nullptr，别用 NULL 或 0。
// ============================================================================

#define NOMINMAX
#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

void use_utf8_console() {
    static_cast<void>(SetConsoleOutputCP(CP_UTF8));
}

void title(const char* text) {
    std::cout << "\n==== " << text << " ====\n";
}

// 重载：用来证明 nullptr 有独立类型，而 NULL 会退化到 int
void take_pointer(int*) {
    std::cout << "      调用到 take_pointer(int*)：实参是真正的指针\n";
}

void take_integer(int) {
    std::cout << "      调用到 take_integer(int)：实参被当成了整数\n";
}

// 接受数组引用：不会退化，能保住长度信息
template <std::size_t N>
std::size_t array_size_via_reference(const int (&arr)[N]) {
    static_cast<void>(arr);
    return N;
}

}  // namespace

int main() {
    use_utf8_console();

    // ======================================================================
    title("1. 声明语法与数组退化（decay）");
    // ======================================================================
    // 声明公式： 元素类型 数组名 [元素个数]
    //   int a[10];       元素类型 int，元素个数 10
    //   int m[10][5];    元素类型 int[5]，元素个数 10   <- 注意元素类型自带一维
    //   int c[10][6][5]; 元素类型 int[6][5]，元素个数 10
    // 原笔记写「int[5] a[10] 经过调整得到 int a[10][5]」，方向是对的：
    // 这是 C 的「声明模仿使用（declaration mimics use）」原则——
    //   a[i][j] 的类型是 int，所以 a[i] 的类型必须是 int[5]，于是 a 的类型是 int[5][10]…
    // 也就是说：先按「怎么用」写出表达式，再把表达式改写成声明，就能得到正确的类型。
    int a10[10] = {0};
    int matrix[10][5] = {{0}};
    std::cout << "  sizeof(int[10])       = " << sizeof(a10) << "  = 10 * " << sizeof(int) << '\n';
    std::cout << "  sizeof(int[10][5])    = " << sizeof(matrix) << "  = 10 * 5 * " << sizeof(int) << '\n';
    std::cout << "  sizeof(matrix[0])     = " << sizeof(matrix[0])
              << "   <- matrix[0] 的类型是 int[5]，不是 int*\n";
    std::cout << "  sizeof(*matrix)       = " << sizeof(*matrix) << "   <- *matrix 就是 matrix[0]\n";
    static_assert(sizeof(matrix[0]) == 5 * sizeof(int), "matrix[0] 是一个 int[5]");
    static_assert(std::is_same_v<decltype(matrix[0]), int(&)[5]>, "matrix[0] 的类型是 int[5] 的引用");

    // 模板 + 数组引用可以保住长度信息（数组不会退化）
    std::cout << "  通过 const int(&)[N] 保住长度：array_size_via_reference(a10) = "
              << array_size_via_reference(a10) << '\n';

    // ======================================================================
    title("2. a / *a / &a[0] / &a 的地址与类型（原笔记的实测）");
    // ======================================================================
    // 原笔记打印了这一组地址，但没解释「为什么有的只差 1、有的差一行」。
    // 关键在于区分两个概念：
    //   * 地址值：它们全都是同一个地址（数组首字节的地址）。
    //   * 类型  ：决定了 +1 走多远，也就是「指针算术的步长」。
    // 指针算术规则： p + n 的地址 = p 的地址 + n * sizeof(*p)
    char grid[2][3] = {
        {'h', 'e', 'l'},
        {'l', 'o', '!'},
    };
    std::printf("  a        = %p   类型 char(*)[3]（退化后的首元素指针，步长 3 字节）\n",
                static_cast<const void*>(grid));
    std::printf("  *a       = %p   类型 char*      （解引用一次，步长 1 字节）\n",
                static_cast<const void*>(*grid));
    std::printf("  &a[0]    = %p   类型 char(*)[3]（和 a 完全等价）\n",
                static_cast<const void*>(&grid[0]));
    std::printf("  &a       = %p   类型 char(*)[2][3]（整个数组的指针，步长 6 字节）\n",
                static_cast<const void*>(&grid));
    std::printf("  a[0]     = %p   类型 char*      （就是 *a 的另一种写法）\n",
                static_cast<const void*>(grid[0]));
    std::printf("  a[1]     = %p   第二行起始，比 a 大 3 字节\n",
                static_cast<const void*>(grid[1]));
    std::cout << "  ---- 步长实测（用差值看，避免编译器把地址运算折叠掉）----\n";
    {
        const char (*row_ptr)[3] = grid;      // 指向「char[3]」的指针
        const char* cell_ptr = *grid;         // 指向 char
        const char (*whole_ptr)[2][3] = &grid;  // 指向整个二维数组
        std::ptrdiff_t step_row = reinterpret_cast<const char*>(row_ptr + 1) -
                                  reinterpret_cast<const char*>(row_ptr);
        std::ptrdiff_t step_cell = (cell_ptr + 1) - cell_ptr;
        std::ptrdiff_t step_whole = reinterpret_cast<const char*>(whole_ptr + 1) -
                                    reinterpret_cast<const char*>(whole_ptr);
        std::cout << "  a + 1      前进 " << step_row << " 字节（= sizeof(*a) = sizeof(char[3])）\n";
        std::cout << "  *a + 1     前进 " << step_cell << " 字节（= sizeof(**a) = sizeof(char)）\n";
        std::cout << "  &a[0] + 1  前进 " << step_row << " 字节（与 a + 1 相同）\n";
        std::cout << "  &a + 1     前进 " << step_whole << " 字节（= sizeof(a) 整个数组）\n";
        static_assert(sizeof(grid) == 6);
        static_assert(sizeof(*grid) == 3);
        static_assert(sizeof(**grid) == 1);
    }

    // ======================================================================
    title("3. 数组退化：什么时候会发生");
    // ======================================================================
    // 会退化（数组名 -> 首元素指针）：赋值给指针、传给函数参数、参与算术。
    // 不会退化：sizeof、&（取地址）、decltype、绑定到数组引用、模板推导。
    int numbers[4] = {10, 20, 30, 40};
    int* decayed = numbers;  // 退化成 int*
    std::cout << "  sizeof(numbers) = " << sizeof(numbers) << "  （没有退化，是 16）\n";
    std::cout << "  sizeof(decayed) = " << sizeof(decayed) << "  （指针本身的大小，8）\n";
    // 传参时数组会退化成指针，长度信息丢失——这是 C 风格 API 的根源问题。
    auto sum_with_pointer = [](const int* data, std::size_t count) {
        int total = 0;
        for (std::size_t i = 0; i < count; ++i) {
            total += data[i];
        }
        return total;
    };
    std::cout << "  退化成指针后必须额外传长度：sum(data, 4) = "
              << sum_with_pointer(numbers, 4) << '\n';
    std::cout << "  现代做法：用 std::span 把「指针 + 长度」打包成一个参数\n";

    // ======================================================================
    title("4. 指针算术：p + n 的真实含义");
    // ======================================================================
    int values[5] = {1, 2, 3, 4, 5};
    int* base = values;
    std::cout << "  *(values + 3)   = " << *(values + 3)
              << "   等价于 values[3] = " << values[3] << '\n';
    std::cout << "  3[values]       = " << 3[values]
              << "   <- 下标运算符会被改写成 *(a+b)，所以交换也行（能跑但别这么写）\n";
    std::cout << "  (base + 3) - base = " << (base + 3) - base
              << "   指针相减得到的是「元素个数」而不是字节数\n";
    std::ptrdiff_t byte_diff = reinterpret_cast<const char*>(base + 3) -
                               reinterpret_cast<const char*>(base);
    std::cout << "  转成字节看 = " << byte_diff << " 字节 = 3 * sizeof(int)\n";
    // 合法的指针范围是「数组首元素到最后一个元素的下一位置」，越界读写是 UB。
    int* past_end = values + 5;  // 合法：可以指向末尾之后，但不能解引用
    static_cast<void>(past_end);
    std::cout << "  values + 5 是合法的「尾后指针」，但 *（values+5) 是 UB，不能解引用\n";

    // ======================================================================
    title("5. 引用 vs 指针");
    // ======================================================================
    // 引用可以理解为「对象的别名」：
    //   引用必须初始化、不能为「空」、一旦绑定不能改绑（改的是被引用对象的值）。
    //   指针可以为空、可以改指、可以做算术，代价是每次使用都要判空。
    // 选择原则：
    //   * 参数：确定一定有对象 -> 引用（只读用 const&，要改用 &）；可能没有 -> 指针。
    //   * 返回值：能返回引用（对象活得比你久）就返回引用；否则返回值或智能指针。
    int original = 42;
    int& alias = original;
    int* pointer = &original;
    alias = 100;  // 改的是 original
    std::cout << "  int& alias = original; alias = 100; -> original = " << original << '\n';
    *pointer = 200;  // 同样改的是 original
    std::cout << "  int* pointer = &original; *pointer = 200; -> original = " << original << '\n';
    // 引用「改绑」的错觉：alias = other 不是让 alias 指向 other，而是把 other 的值拷给 original
    int other = 7;
    alias = other;
    std::cout << "  alias = other; 之后 original = " << original
              << "，other = " << other << "   <- 引用没有改绑，只是赋值\n";
    // 注意：语言层面引用「不是对象」，所以标准没规定 sizeof(T&) 是多少。
    // MSVC 在这里给出 sizeof(int&) == sizeof(int)，GCC/Clang 常给出指针大小，
    // 两套行为都合规——这也说明「引用就是指针的语法糖」只是实现层面的近似，
    // 不要写依赖 sizeof(T&) 的代码。sizeof(int) 与 sizeof(int*) 实测如下：
    std::cout << "  sizeof(int)=" << sizeof(int) << " sizeof(int*)=" << sizeof(int*)
              << " sizeof(int&)=" << sizeof(int&) << "（后者的值是实现定义的，别依赖）\n";

    // 避免拷贝：const 引用是 C++ 里最重要的性能习惯之一
    const std::string long_text(200, 'x');
    const std::string copied = long_text;         // 200 字节拷贝
    const std::string& referred = long_text;      // 零拷贝
    std::cout << "  传大对象用 const& 避免拷贝：copied.size()=" << copied.size()
              << " referred.size()=" << referred.size() << '\n';

    // ======================================================================
    title("6. const 与指针的四种组合");
    // ======================================================================
    // 读法口诀：从右往左读，const 修饰它左边最近的那个类型。
    //   const int* p          读作「p 是 指向 const int 的指针」-> 值不能改，指针能改
    //   int const* p          同上（const 在类型名前或在类型名后等价）
    //   int* const p          读作「p 是 const 指针，指向 int」-> 指针不能改，值能改
    //   const int* const p    两者都不能改
    // 工程建议：能加 const 就加。const 是最便宜的自文档化工具。
    int x = 1;
    int y = 2;
    const int* ptr_to_const = &x;   // 不能通过它改 x，但它可以改指到 y
    ptr_to_const = &y;
    std::cout << "  const int* p   ：可改指向（现在指向 y=" << *ptr_to_const << "），不可改值\n";
    int* const const_ptr = &x;      // 只能一直指着 x，但可以通过它改 x
    *const_ptr = 111;
    std::cout << "  int* const p   ：不可改指向，可改值（x 现在是 " << x << "）\n";
    const int* const both = &y;
    std::cout << "  const int* const p：两者都不可改，*both = " << *both << '\n';
    // 把非 const 指针赋给 const 指针是允许的（增加限制），反过来不行（需要强转）。
    std::cout << "  int* -> const int* 隐式可行；const int* -> int* 必须 const_cast（别用）\n";

    // ======================================================================
    title("7. nullptr 而不是 NULL / 0");
    // ======================================================================
    // NULL 在 C++ 里通常就是 0（或者 0L），它是整数，不是指针类型。
    // 这会导致重载决议选错分支、模板推导出 int 等问题。
    std::cout << "  nullptr 的类型是 std::nullptr_t：is_same<decltype(nullptr), std::nullptr_t> = "
              << std::boolalpha << std::is_same_v<decltype(nullptr), std::nullptr_t> << '\n';
    std::cout << "  调用 take_pointer(nullptr)：\n";
    take_pointer(nullptr);  // 一定选指针重载
    std::cout << "  调用 take_pointer(NULL)：\n";
    take_pointer(NULL);  // 在某些平台 NULL 是 0，会选中 int 重载
    std::cout << "  调用 take_pointer(0)：\n";
    take_pointer(0);  // 0 是 int，选中 int 重载
    std::cout << std::noboolalpha;
    std::cout << "  结论：用 nullptr，它在任何重载/模板场景下都只当指针看\n";
    // 用 0 或 NULL 表示空指针还会丧失类型信息：
    //   auto p = NULL;  ->  int（不是指针！） 这是最容易埋雷的写法
    std::cout << "  auto p = NULL; 会把 p 推导成整数类型，这是真实的埋雷写法\n";

    // ======================================================================
    title("8. 现代替代：尽量别用裸数组");
    // ======================================================================
    // C 数组的三个固有缺陷：长度信息会丢失、不能整体赋值、越界不检查。
    std::array<int, 4> std_arr = {1, 2, 3, 4};
    std::array<int, 4> std_arr_copy = std_arr;  // 可整体赋值（C 数组不行）
    std::cout << "  std::array 可以整体赋值：std_arr_copy.size() = " << std_arr_copy.size()
              << "，std_arr.size() = " << std_arr.size() << "（长度是类型的一部分）\n";
    std::cout << "  std::array 有 at() 做边界检查：std_arr.at(2) = " << std_arr.at(2) << '\n';
    // std::span：不拥有数据，把「指针 + 长度」安全地传下去（C++20）
    auto sum_span = [](std::span<const int> data) {
        int total = 0;
        for (int value : data) {
            total += value;
        }
        return total;
    };
    std::cout << "  std::span 一次传完指针和长度：sum_span(std_arr) = " << sum_span(std_arr) << '\n';
    std::vector<int> dyn{5, 6, 7};
    std::cout << "  std::span 也接受 vector：sum_span(dyn) = " << sum_span(dyn) << '\n';
    // std::string_view：不拥有字符数据，零拷贝地看一段字符串
    const std::string_view view("Hello, string_view");
    std::cout << "  std::string_view 不做拷贝：view.substr(7, 11) = " << view.substr(7, 11) << '\n';
    std::cout << "  注意：string_view / span 都是「借用」语义，被借的数据必须活得更久\n";

    std::cout << "\n[04] 结束。下一步：05_string_and_io.cpp\n";
    return 0;
}
