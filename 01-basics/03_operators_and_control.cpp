// ============================================================================
//  03_operators_and_control.cpp  —— 01-basics 第 3 篇
// ----------------------------------------------------------------------------
//  演示主题：
//    1. 运算符优先级陷阱（<< 与 +、& 与 ==、赋值与比较的混用）
//    2. 短路求值：&& / || 的求值顺序与副作用
//    3. ++i 与 i++ 的区别，以及「同一表达式里改两次」的 UB
//    4. switch：贯穿（fallthrough）与 [[fallthrough]]、忘记 default
//    5. goto 的合理用法：跳出多层循环
//    6. 范围 for 的坑：遍历时改容器大小 = 迭代器失效
//    7. 悬垂引用：`for (auto x : get_vector())` 与 `for (auto& x : ...)` 的区别
//    8. while 条件里写赋值（= 与 == 混用）的经典事故
//
//  关键结论：
//    * 记不住优先级就加括号；括号是零成本的。
//    * 短路求值是语言保证，可以放心把空指针检查写在 && 左边。
//    * 一个表达式里不要对同一变量做两次修改，那是 UB，不是「看编译器心情」。
//    * 范围 for 是「值拷贝」语义的语法糖，改容器结构会让迭代器失效。
// ============================================================================

#define NOMINMAX
#include <windows.h>

#include <iostream>
#include <string>
#include <vector>

namespace {

void use_utf8_console() {
    static_cast<void>(SetConsoleOutputCP(CP_UTF8));
}

void title(const char* text) {
    std::cout << "\n==== " << text << " ====\n";
}

// 用来证明短路求值的函数：被调用就记一次账。
int call_count = 0;

bool always_true_side_effect() {
    ++call_count;
    return true;
}

bool always_false_side_effect() {
    ++call_count;
    return false;
}

void reset_count() {
    call_count = 0;
}

}  // namespace

int main() {
    use_utf8_console();

    // ======================================================================
    title("1. 运算符优先级陷阱");
    // ======================================================================
    // 优先级从高到低（常用部分）：
    //   ::  ->  . [] () 后置 ++/--  ->  前置 ++/-- ! ~ 一元 +/- * & sizeof
    //   ->  .* ->  * / %  ->  + -  ->  << >>  ->  < <= > >=
    //   ->  == !=  ->  &  ->  ^  ->  |  ->  &&  ->  ||  ->  ?:  ->  = += ...
    //   ->  ,（逗号）
    // 记住三件事就够：算术 > 移位 > 关系 > 相等 > 位运算 > 逻辑 > 赋值 > 逗号。
    {
        // 陷阱 A：<< >> 的优先级比 + - 低！
        //   a << b + c  等价于  a << (b + c)，不是 (a << b) + c
        // MSVC 会给出 C4554「check operator precedence」，这就是编译器在帮你兜底。
        // 下面按错误写法原样保留，并临时关掉这个警告，好让程序能跑起来给你看结果。
        const int a = 1, b = 2, c = 3;
#pragma warning(push)
#pragma warning(disable : 4554)  // 故意保留可疑写法以演示，正常代码不要关
        std::cout << "  陷阱 A：1 << 2 + 3 = " << (a << b + c)
                  << "  （编译器对此会报 C4554：check operator precedence）\n";
#pragma warning(pop)
        std::cout << "          它等于 1 << (2+3) = " << (a << (b + c))
                  << "，而不是 (1<<2)+3 = " << ((a << b) + c) << '\n';
    }
    {
        // 陷阱 B：位运算 & ^ | 的优先级比 == 低！
        //   a & b == c  等价于  a & (b == c)
        const int flags = 0b110;  // 6
        const int mask = 0b010;   // 2
#pragma warning(push)
#pragma warning(disable : 4554)  // 同上：演示可疑写法
        std::cout << "  陷阱 B：flags & mask == mask 的结果 = " << (flags & mask == mask)
                  << "  （编译器同样会报 C4554）\n";
#pragma warning(pop)
        std::cout << "          实际算的是 flags & (mask == mask) = flags & 1\n";
        std::cout << "          想表达「掩码位是否都是 1」要写 (flags & mask) == mask -> "
                  << ((flags & mask) == mask) << '\n';
    }
    {
        // 陷阱 C：流插入运算符 << 与关系运算符混用。
        //   std::cout << a < b  会被解析成 (std::cout << a) < b，编译报错。
        const int left = 1, right = 2;
        std::cout << "  陷阱 C：std::cout << (1 < 2) 要加括号，"
                     "否则 << 优先级高于 < 会解析成 (cout << 1) < 2\n";
        std::cout << "          加括号后结果 = " << (left < right) << '\n';
    }
    {
        // 陷阱 D：条件运算符 ?: 的优先级低于算术但高于赋值，嵌套时务必加括号。
        const int score = 75;
        const std::string grade = (score >= 90)   ? "A"
                                  : (score >= 80) ? "B"
                                  : (score >= 60) ? "C"
                                                  : "D";
        std::cout << "  陷阱 D：嵌套 ?: 建议写成阶梯形并加括号，score=" << score
                  << " -> " << grade << '\n';
    }
    {
        // 陷阱 E：逗号运算符。左边先求值并丢弃，整体结果是右边的值。
        int i = 0;
        int j = 0;
        const int comma_result = (++i, ++j, i + j);  // i=1, j=1, 结果 2
        std::cout << "  陷阱 E：逗号运算符左边丢弃、右边为结果：i=" << i << " j=" << j
                  << " (++i, ++j, i+j) = " << comma_result << '\n';
        std::cout << "          注意：函数实参之间的逗号不是逗号运算符，是分隔符\n";
    }

    // ======================================================================
    title("2. 短路求值：&& 和 || 的求值顺序是语言保证");
    // ======================================================================
    // && ：左边为 false 就立刻整体为 false，右边不执行
    // || ：左边为 true  就立刻整体为 true ，右边不执行
    // 这个保证非常有用：可以把「前提条件」放在左边，把「可能崩溃的操作」放在右边。
    reset_count();
    const bool r1 = always_false_side_effect() && always_true_side_effect();
    std::cout << "  false && ... 结果=" << std::boolalpha << r1
              << "，副作用执行次数=" << call_count << "（左边就为假，右边没跑）\n";
    reset_count();
    const bool r2 = always_true_side_effect() || always_false_side_effect();
    std::cout << "  true  || ... 结果=" << r2
              << "，副作用执行次数=" << call_count << "（左边就为真，右边没跑）\n";
    reset_count();
    const bool r3 = always_true_side_effect() && always_false_side_effect();
    std::cout << "  true  && ... 结果=" << r3
              << "，副作用执行次数=" << call_count << "（这次两边都跑了）\n";
    std::cout << std::noboolalpha;

    // 工程用法：把空指针检查放在左边，右边才敢解引用
    const int* maybe_null = nullptr;
    if (maybe_null != nullptr && *maybe_null > 0) {
        std::cout << "  这行不会执行\n";
    }
    std::cout << "  if (p != nullptr && *p > 0) 是安全的：短路保证了解引用不会发生\n";

    // ======================================================================
    title("3. ++i 与 i++：语义、性能与 UB");
    // ======================================================================
    // 前缀 ++i：先自增，返回自增后的引用（左值）
    // 后缀 i++：先取旧值做一份拷贝，再自增，返回旧值（右值）
    // 对 int 来说两者性能一样（编译器都优化掉）；对迭代器/自定义类型，
    // 后缀版本多一次拷贝，所以循环里习惯写 ++it。
    {
        int i = 1;
        std::cout << "  i 初始 = 1\n";
        std::cout << "  i++ 的值 = " << i++ << "，之后 i = " << i
                  << "（返回旧值，但变量已自增）\n";
        std::cout << "  ++i 的值 = " << ++i << "，之后 i = " << i
                  << "（先自增，再返回自增后的值）\n";
    }
    {
        // 未定义行为：同一个表达式里对 i 修改两次，没有任何顺序保证。
        // 下面这行如果打开，MSVC /W4 会报 C4624 之类的警告，
        // 具体结果在不同编译器/不同优化级别下不一样，所以直接不写。
        //     int i = 0;
        //     i = i++;        // UB
        //     int x = i++ + ++i;  // UB
        std::cout << "  【不要写】i = i++; 或 i++ + ++i：同一表达式内对 i 多次修改是 UB\n";
        std::cout << "  正确写法：拆成两条语句，顺序就明确了\n";
    }

    // ======================================================================
    title("4. switch：贯穿、[[fallthrough]] 与初始化");
    // ======================================================================
    auto describe_command = [](char cmd) -> std::string {
        std::string result;
        switch (cmd) {
            case 'q':
            case 'Q':  // 故意不写 break：两个标签共用一个分支，这是合法且常见的写法
                result = "退出（q 和 Q 共用一个分支，属于有意的贯穿）";
                break;
            case 'h':
                result = "帮助";
                [[fallthrough]];  // 故意落进 'H' 分支：加属性告诉编译器「我知道我在干什么」
            case 'H':
                result += "（大写 H 也会执行到这里）";
                break;
            default:
                result = "未知命令";
                break;
        }
        return result;
    };
    std::cout << "  'q' -> " << describe_command('q') << '\n';
    std::cout << "  'h' -> " << describe_command('h') << '\n';
    std::cout << "  'H' -> " << describe_command('H') << '\n';
    std::cout << "  'z' -> " << describe_command('z') << '\n';

    // switch 里声明变量必须用大括号限定作用域，否则会报「跨越初始化跳转」错误。
    auto classify = [](int value) -> std::string {
        switch (value) {
            case 0: {
                const std::string zero_text = "零";
                return zero_text;
            }
            case 1: {
                const std::string one_text = "一";
                return one_text;
            }
            default:
                return "其它";
        }
    };
    std::cout << "  switch 分支里定义变量要加 {}，否则报「跳过了变量初始化」错误："
              << classify(1) << '\n';

    // ======================================================================
    title("5. goto 的合理用法：一次跳出多层循环");
    // ======================================================================
    // goto 被骂是因为它会造成「面条代码」。但有一种场景它比任何替代方案都清晰：
    // 从深层嵌套循环里直接跳出到外层。用 break 只能跳一层，用标志位会让代码变形。
    const int matrix[3][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}};
    const int target = 7;
    int found_row = -1;
    int found_col = -1;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) {
            if (matrix[row][col] == target) {
                found_row = row;
                found_col = col;
                goto found;  // 一次跳出两层
            }
        }
    }
found:
    std::cout << "  在矩阵中找 " << target << " -> 行 " << found_row
              << " 列 " << found_col << "（goto 只用于跳出，不用来构造循环）\n";
    // 现代替代方案：把内层循环抽成一个函数直接 return，或者用 std::ranges::find。
    std::cout << "  现代做法：把查找抽成函数用 return；或者在 C++20 里用 std::ranges 算法\n";

    // ======================================================================
    title("6. 范围 for 的坑：遍历时修改容器结构");
    // ======================================================================
    // 【错误直觉】「范围 for 是编译器魔法，边遍历边删应该也行」。
    // 【正确模型】范围 for 展开后等价于用 begin()/end() 迭代器遍历：
    //       auto __begin = v.begin(); auto __end = v.end();
    //       for (; __begin != __end; ++__begin) { auto& x = *__begin; ... }
    //   一旦 push_back 触发扩容，所有迭代器（包括缓存的 __end）全部失效，
    //   继续用它们就是 UB——可能崩溃，也可能「看起来正常」。
    std::vector<int> numbers = {1, 2, 3};
    std::cout << "  反例：for (int x : numbers) { numbers.push_back(x); } -> 迭代器失效，UB\n";
    // 安全写法 1：先遍历、把要加的元素收集起来，遍历结束后统一处理
    {
        std::vector<int> data = {1, 2, 3};
        std::vector<int> pending;
        for (int value : data) {
            pending.push_back(value * 10);
        }
        data.insert(data.end(), pending.begin(), pending.end());
        std::cout << "  安全写法 1（先收集后插入）：";
        for (int value : data) {
            std::cout << value << ' ';
        }
        std::cout << "  大小 " << data.size() << '\n';
    }
    // 安全写法 2：确实要边遍历边删，用迭代器并接收 erase 的返回值
    {
        std::vector<int> data = {1, 2, 3, 4, 5, 6};
        for (auto it = data.begin(); it != data.end();) {
            if (*it % 2 == 0) {
                it = data.erase(it);  // erase 返回下一个有效位置
            } else {
                ++it;
            }
        }
        std::cout << "  安全写法 2（迭代器 + erase 返回值）删偶数后：";
        for (int value : data) {
            std::cout << value << ' ';
        }
        std::cout << '\n';
    }
    // 安全写法 3：C++20 的 std::erase_if 一行搞定（推荐）
    {
        std::vector<int> data = {1, 2, 3, 4, 5, 6};
        const auto removed = std::erase_if(data, [](int v) { return v % 2 == 0; });
        std::cout << "  安全写法 3（C++20 std::erase_if）删掉 " << removed << " 个元素，剩下：";
        for (int value : data) {
            std::cout << value << ' ';
        }
        std::cout << '\n';
    }
    // 注意：只「修改元素的值」是安全的，只有改「容器大小/结构」才失效。
    {
        std::vector<int> data = {1, 2, 3};
        for (int& value : data) {
            value *= 2;  // 用引用，改的就是容器里的元素
        }
        std::cout << "  只改值不改结构是安全的（注意用 int& 而不是 int）：";
        for (int value : data) {
            std::cout << value << ' ';
        }
        std::cout << '\n';
    }

    // ======================================================================
    title("7. 悬垂引用：范围 for 绑定的临时对象");
    // ======================================================================
    // 【错误直觉】「范围 for 会先把范围求值成一个变量存起来，所以安全」。
    // 【正确模型】C++ 标准规定：范围 for 里 `auto&& __range = 范围表达式;`，
    //   这个临时对象的生命周期会被延长到整个循环结束——所以
    //       for (int x : make_vector())  是【安全】的。
    //   但如果你把「引用」绑到一个临时对象上，或者让 range 表达式返回引用，
    //   情况就变了。最典型的坑是把返回引用绑到 auto& 上：
    //       auto& ref = make_vector();    // 编译错误：不能把临时绑定到非 const 左值引用
    //   另一个真实坑：对返回引用的函数做范围 for，而那个引用在循环中失效。
    auto make_vector = []() -> std::vector<int> { return {7, 8, 9}; };
    std::cout << "  安全：for (int x : make_vector()) 中临时 vector 的生命周期被延长到循环结束 -> ";
    // 注意：范围 for 会尽力延长临时对象的生命周期，所以这行本身是安全的。
    // 但它同时也是一个「活到循环结束」的隐藏对象：如果 make_vector 返回的是
    // 指向函数内部静态/局部的引用，问题就转移到那个引用上了。
    for (int value : make_vector()) {
        std::cout << value << ' ';
    }
    std::cout << '\n';
    // 危险：写成引用绑定
    //     std::vector<int>& bad = make_vector();   // 编译错误（好事）
    //     const std::vector<int>& ok = make_vector();  // 合法，const 引用延长生命周期
    const std::vector<int>& extended = make_vector();  // const 引用绑定临时，寿命被延长
    std::cout << "  const 引用绑定临时对象会延长其生命周期：extended 有 "
              << extended.size() << " 个元素\n";
    // 用 auto& 遍历一个「返回引用」的函数结果时要小心：如果那个引用指向的是
    // 函数内部已经销毁的对象，就是悬垂引用（见 06_functions_and_scope.cpp）。
    std::cout << "  真正的悬垂引用来自「返回局部变量的引用」，见 06 篇的实测\n";

    // ======================================================================
    title("8. while 条件里写赋值：= 与 == 混用的经典事故");
    // ======================================================================
    // 【事故现场】想写 while (ch == 'y')，手滑写成 while (ch = 'y')。
    //   后者把 'y' 赋给 ch，表达式的值是 'y'（非 0），条件恒真 -> 死循环。
    // MSVC 在这种情况下会给出 C4706（assignment within conditional expression）。
    std::cout << "  反例：while (ch = 'y') 会把赋值结果当条件，恒为真 -> 死循环\n";
    // 正确写法 1：比较写完整（推荐）
    // 正确写法 2：把常量写在左边（Yoda 条件）：while ('y' == ch)
    //             这样手滑写成 = 会直接编译报错，因为不能给常量赋值
    // 正确写法 3：显式说明意图，比如读文件时的
    //             while ((c = stream.get()) != EOF) —— 这里刻意加一层括号
    char stop_flag = 'n';
    std::cout << "  正确写法 A：while ('y' == ch)：把常量放左边，手滑写 = 会编译失败\n";
    std::cout << "  正确写法 B：确实要用赋值时写成 while ((ch = next()) != 0)，多一层括号表达意图\n";
    std::cout << "  当前 stop_flag = " << stop_flag << "，比较 'y' == stop_flag 的结果 = "
              << std::boolalpha << ('y' == stop_flag) << '\n';
    std::cout << std::noboolalpha;

    // ======================================================================
    title("9. 指针与 ++ 的优先级（原笔记的打印实验，结论在 04 篇展开）");
    // ======================================================================
    int arr[3] = {10, 20, 30};
    int* p = arr;
    std::cout << "  *p      = " << *p << "   （先解引用）\n";
    {
        // 后置 ++ 的优先级高于一元 *，所以 *p++ 是 *(p++)：先取旧值再移动指针。
        // 这里用单独的变量接收，避免在一条输出语句里塞多个带副作用的表达式。
        const int old_deref = *p++;
        std::cout << "  *p++    = " << old_deref
                  << "   等价于 *(p++)：先交出 arr[0] 的值，再把 p 移到 arr[1]\n";
        std::cout << "  现在 p 指向 arr[1] = " << *p << '\n';
    }
    {
        // (*p)++ 是「把 p 指向的元素自增」，指针本身不动。
        int local[3] = {10, 20, 30};
        int* q = local;
        const int before = (*q)++;
        std::cout << "  (*q)++  = " << before
                  << "   先返回旧值，再把 local[0] 自增；现在 local[0] = " << local[0]
                  << "，q 仍然指向 local[0]\n";
    }

    std::cout << "\n[03] 结束。下一步：04_arrays_pointers_refs.cpp\n";
    return 0;
}
