// ============================================================================
// 06_cpp_string.cpp  ——  std::string：现代 C++ 的字符串
//
// 演示主题：
//   1. 构造方式大全：字面量 / 重复字符 / 子串 / 迭代器区间 / string_view
//   2. size / capacity / reserve / shrink_to_fit / resize 的区别
//   3. SSO（小字符串优化）：用 capacity() 实测证明「短字符串不分配堆内存」
//   4. 拼接与修改：append / operator+= / insert / erase / replace / pop_back
//   5. 查找：find / rfind / find_first_of / find_last_of / find_first_not_of / npos
//   6. 子串与比较：substr / compare / starts_with / ends_with（C++20）
//   7. 数值转换：
//        std::stoi / stod（抛异常，受 locale 影响）
//        std::from_chars / std::to_chars（不抛异常、不受 locale 影响、更快）
//        三者实测性能对比
//   8. std::string_view：零拷贝参数、与 string 互操作、以及最重要的生命周期陷阱
//   9. std::getline：按行读 + 按分隔符切分；与 >> 混用的坑
//  10. std::format（C++20）：类型安全的格式化，替代 sprintf / stringstream
//
// 关键结论：
//   - capacity() 是「不重新分配内存的前提下最多能放多少字符」，size() 是「现在有多少」。
//     两者之间是已分配但未使用的空间。reserve 只改 capacity，不改 size。
//   - SSO：为了不让短字符串也去堆上分配，标准库把一小段缓冲区直接放在 string 对象内部。
//     MSVC 上这个容量是 15（16 字节里留 1 个给 '\0'）。所以 "hello" 的 data() 指针
//     会落在对象自己身上；一旦超过 15 就直接观察到 capacity 跳到 31。
//   - std::string_view 不拥有数据、不保证以 '\0' 结尾，它只是一个 (指针, 长度) 对。
//     因此：【绝对不能】把指向临时 string 的 string_view 存下来。
//   - std::stoi 会抛异常并且受 locale 影响；std::from_chars 走返回值、与 locale 无关，
//     在解析密集的代码里（配置解析、CSV、协议解析）快得多。
//
// 说明：本文件不使用 using namespace std，一律写 std:: 前缀。
// ============================================================================

#include <algorithm>   // std::sort / std::transform
#include <charconv>    // std::from_chars / std::to_chars（C++17）
#include <chrono>      // 性能计时
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <format>      // C++20 格式化
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>   // std::invalid_argument / std::out_of_range
#include <string>
#include <string_view>
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

void SubSection(const char* title) {
    std::printf("\n---- %s ----\n", title);
}

using Clock = std::chrono::steady_clock;

// ===========================================================================
// 1. 构造方式
// ===========================================================================
void DemoConstruction() {
    Section("1. std::string 的构造方式");

    const std::string s1;                              // 空串
    const std::string s2 = "hello";                    // 从字符串字面量
    const std::string s3(5, 'x');                      // 5 个 'x'
    const std::string s4(s2, 1, 3);                    // 从 s2 的位置 1 起取 3 个字符
    const std::string s5(s2);                          // 拷贝构造
    const std::string s6(s2.begin(), s2.begin() + 3);  // 迭代器区间 [first, last)
    const std::string s7 = s2 + ", world";             // operator+ 拼接
    const std::string_view sv = "view-source";
    const std::string s8(sv);  // 从 string_view 构造（C++17）
    const std::string s9 = std::string(3, 'a') + std::string(2, 'b');

    std::printf("  string s1;                      -> \"%s\"（空串）\n", s1.c_str());
    std::printf("  string s2 = \"hello\";           -> \"%s\"\n", s2.c_str());
    std::printf("  string s3(5, 'x');              -> \"%s\"\n", s3.c_str());
    std::printf("  string s4(s2, 1, 3);            -> \"%s\"（从下标 1 取 3 个）\n", s4.c_str());
    std::printf("  string s5(s2);                  -> \"%s\"（拷贝）\n", s5.c_str());
    std::printf("  string s6(s2.begin(), +3);      -> \"%s\"\n", s6.c_str());
    std::printf("  string s7 = s2 + \", world\";    -> \"%s\"\n", s7.c_str());
    std::printf("  string s8(string_view);        -> \"%s\"\n", s8.c_str());
    std::printf("  string(3,'a') + string(2,'b')  -> \"%s\"\n", s9.c_str());

    SubSection("容易踩的两个点");
    // (1) substr 越界会抛 std::out_of_range，而不是返回空串
    try {
        const std::string bad = s2.substr(100);
        std::printf("  s2.substr(100) 居然成功了？长度 %zu\n", bad.size());
    } catch (const std::out_of_range& e) {
        std::printf("  s2.substr(100) 抛出 std::out_of_range：%s\n", e.what());
    }
    // (2) operator[] 越界是 UB；at() 越界抛异常 —— 这是两者唯一的语义差别
    std::printf("  s2[1] = '%c'（不检查边界，越界是 UB）\n", s2[1]);
    try {
        std::printf("  s2.at(100) = ... ");
        std::printf("%c\n", s2.at(100));
    } catch (const std::out_of_range&) {
        std::printf("抛出 std::out_of_range（at 会检查边界）\n");
    }
    std::printf("  ★ 建议：热路径用 []（不检查更省），对外部输入/不确定下标用 at。\n");
    std::printf("  ★ C++11 起 c_str() 与 data() 都保证以 '\\0' 结尾（C++17 前 data() 不保证）。\n");
}

// ===========================================================================
// 2 & 3. size / capacity / reserve / SSO 实测
// ===========================================================================
void DemoSizeCapacitySso() {
    Section("2/3. size / capacity / reserve / shrink_to_fit 与 SSO 实测");

    SubSection("从一个空串开始，观察 size 与 capacity 的变化");
    std::string s;
    std::printf("  %-28s size=%-4zu capacity=%-4zu\n", "初始（空串）", s.size(), s.capacity());

    // 逐个字符追加：capacity 会在「放不下」时按一定比例跳上去
    std::printf("  -- 连续 push_back 40 次，只看 capacity 变化点 --\n");
    std::size_t lastCap = s.capacity();
    for (int i = 0; i < 40; ++i) {
        s.push_back('a');
        if (s.capacity() != lastCap) {
            std::printf("    size=%2zu 时 capacity 从 %zu 变成 %zu（发生了堆分配 + 元素搬移）\n",
                        s.size(), lastCap, s.capacity());
            lastCap = s.capacity();
        }
    }
    std::printf("  最终：size=%zu capacity=%zu（已分配但未使用的空间 = %zu）\n", s.size(),
                s.capacity(), s.capacity() - s.size());

    SubSection("★ SSO（小字符串优化）：短字符串根本不在堆上分配");
    std::printf("  原理：标准库在 string 对象内部留了一小块缓冲区，短字符串直接放在里面，\n");
    std::printf("        因此「构造短字符串」不需要 malloc，也不需要 free，速度极快。\n");
    std::printf("  实测（MSVC 的实现，不同实现数字不同，但机制相同）：\n");
    std::printf("    %-12s %-10s %-12s %s\n", "内容长度", "capacity", "内容是否太长", "data() 在哪");
    std::string probe = "0123456789";  // 长度 10
    for (const int len : {0, 5, 10, 15, 16, 17, 20, 31, 32, 33}) {
        probe.assign(static_cast<std::size_t>(len), 'a');
        const bool inObject = (probe.data() >= reinterpret_cast<const char*>(&probe) &&
                               probe.data() < reinterpret_cast<const char*>(&probe) + sizeof(probe));
        std::printf("    %-12d %-10zu %-12s %s\n", len, probe.capacity(),
                    inObject ? "否（用内部缓冲区）" : "是（已改用堆）",
                    inObject ? "string 对象内部" : "堆上 malloc 出来的");
    }
    std::printf("  -> 结论：MSVC 的 SSO 容量是 15。长度 <= 15 的字符串不碰堆。\n");
    std::printf("     这解释了为什么「用 string 传小字符串参数」其实没那么贵 —— 但要按值传仍然会拷贝。\n");
    std::printf("     也能解释一个经典 bug：保存了短字符串的 c_str() 指针，之后 append 一下就悬垂了。\n");

    SubSection("reserve：一次性把容量要够，避免反复分配 + 搬移");
    // 用「追加比较长的片段」来放大差异：每次扩容都会把所有已有字符搬一次家。
    // 只追加单字符时差异很小（MSVC 的扩容是 1.5 倍增长，均摊成本本来就低）。
    constexpr int kChunks = 20000;
    constexpr std::size_t kChunkLen = 32;
    const std::string chunk(kChunkLen, 'x');

    const auto t0 = Clock::now();
    std::string noReserve;
    for (int i = 0; i < kChunks; ++i) {
        noReserve += chunk;  // 长度达到约 64 万字节，中途会扩容很多次
    }
    const auto t1 = Clock::now();

    std::string withReserve;
    withReserve.reserve(static_cast<std::size_t>(kChunks) * kChunkLen);  // ★ 一次到位
    const auto t2 = Clock::now();
    for (int i = 0; i < kChunks; ++i) {
        withReserve += chunk;
    }
    const auto t3 = Clock::now();

    const auto usNo = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    const auto usYes = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
    std::printf("  【实测】分 %d 次追加、每次 %zu 字节（共约 %zu 字节，本机实测）：\n", kChunks,
                kChunkLen, static_cast<std::size_t>(kChunks) * kChunkLen);
    std::printf("    不 reserve : %8lld us（capacity 最终 %zu）\n", static_cast<long long>(usNo),
                noReserve.capacity());
    std::printf("    先 reserve : %8lld us（capacity 最终 %zu）\n", static_cast<long long>(usYes),
                withReserve.capacity());
    if (usYes > 0) {
        std::printf("    -> 不 reserve 约为 reserve 的 %.2f 倍耗时\n",
                    static_cast<double>(usNo) / static_cast<double>(usYes));
    } else {
        std::printf("    -> reserve 版本快到测不出来（<1 us 精度）\n");
    }
    std::printf("  ★ 说明：MSVC 的 string 按约 1.5 倍增长，均摊下来每次追加仍是 O(1)，\n");
    std::printf("    所以 reserve 省的是「反复 malloc + 搬移已有数据」这部分常数。\n");
    std::printf("    在 Release 下两者都很快，但 reserve 版本耗时的方差小得多 —— 这才是它的价值：\n");
    std::printf("    实时/低延迟场景怕的不是平均慢，而是偶尔一次分配造成的卡顿。\n");

    SubSection("resize / shrink_to_fit / clear");
    std::string r = "hello";
    std::printf("  resize 前：\"%s\" size=%zu\n", r.c_str(), r.size());
    r.resize(8, '!');  // 变长：用 '!' 填充
    std::printf("  resize(8, '!')：\"%s\" size=%zu（变长会填充）\n", r.c_str(), r.size());
    r.resize(3);  // 变短：直接截断，容量不变
    std::printf("  resize(3)：\"%s\" size=%zu capacity=%zu（变短不动容量）\n", r.c_str(), r.size(),
                r.capacity());

    // shrink_to_fit 是「非强制」请求：实现可以忽略它
    r.shrink_to_fit();
    std::printf("  shrink_to_fit()：capacity=%zu（标准说这是「非强制性请求」，实现可以不理会）\n",
                r.capacity());

    std::string big(1000, 'z');
    const std::size_t capBefore = big.capacity();
    big.clear();
    std::printf("  clear() 后：size=%zu，capacity 从 %zu 变成 %zu\n", big.size(), capBefore,
                big.capacity());
    std::printf("  ★ clear() 只把 size 置 0，【不释放内存】，也不修改已分配缓冲区的内容。\n");
    std::printf("     想真正把内存还给系统要写：std::string().swap(big); （交换技巧）\n");
    std::string{}.swap(big);
    std::printf("     交换技巧之后 capacity=%zu\n", big.capacity());
}

// ===========================================================================
// 4. 拼接与修改
// ===========================================================================
void DemoModify() {
    Section("4. append / += / insert / erase / replace / pop_back");

    std::string s = "hello";
    std::printf("  初始             : \"%s\"\n", s.c_str());

    s.append(", ");                       // 追加 C 字符串
    s.append(3, '!');                     // 追加 3 个 '!'
    s.append("world", 2);                 // 追加 "world" 的前 2 个字符
    std::printf("  append 三次后    : \"%s\"\n", s.c_str());

    s += "?";                             // operator+= 和 append 等价，写法更简洁
    std::printf("  += \"?\"          : \"%s\"\n", s.c_str());

    s.insert(5, " INSERTED");             // 在下标 5 处插入
    std::printf("  insert(5, ...)   : \"%s\"\n", s.c_str());

    s.erase(5, 9);                        // 从下标 5 删 9 个字符
    std::printf("  erase(5, 9)      : \"%s\"\n", s.c_str());

    s.replace(0, 5, "HELLO");              // 把 [0,5) 换成 "HELLO"
    std::printf("  replace(0,5,...) : \"%s\"\n", s.c_str());

    s.pop_back();                          // 删最后一个字符（C++11）
    std::printf("  pop_back()       : \"%s\"\n", s.c_str());

    s.push_back('.');                      // 追加一个字符
    std::printf("  push_back('.')   : \"%s\"\n", s.c_str());

    SubSection("复杂度提醒（这是选型时最容易忽略的部分）");
    std::printf("  push_back / pop_back / append 到末尾 : 均摊 O(1)\n");
    std::printf("  operator+= / +=                      : 同上\n");
    std::printf("  insert / erase 在【中间】            : O(n) —— 要把后面的字符整体搬移\n");
    std::printf("  replace 在中间                       : O(n)\n");
    std::printf("  ★ 所以「在循环里往字符串头部插入字符」是 O(n^2)。\n");
    std::printf("    需要大量头部插入时：先往后追加，最后 std::reverse；或改用 std::deque<char>。\n");

    SubSection("性能实测：往头部插入 vs 先追加再反转");
    constexpr int kM = 20000;

    std::string headInsert;
    const auto t0 = Clock::now();
    for (int i = 0; i < kM; ++i) {
        headInsert.insert(0, 1, 'a');  // ★ 每次都是 O(n)
    }
    const auto t1 = Clock::now();

    std::string appendThenReverse;
    appendThenReverse.reserve(static_cast<std::size_t>(kM));
    const auto t2 = Clock::now();
    for (int i = 0; i < kM; ++i) {
        appendThenReverse.push_back('a');  // 均摊 O(1)
    }
    std::reverse(appendThenReverse.begin(), appendThenReverse.end());
    const auto t3 = Clock::now();

    const auto usHead = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    const auto usTail = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
    std::printf("  【实测】往 %d 个字符的字符串里「逐个插到头部」（本机实测，仅供参考）：\n", kM);
    std::printf("    每次都 insert(0, ...)  : %8lld us\n", static_cast<long long>(usHead));
    std::printf("    push_back + reverse    : %8lld us\n", static_cast<long long>(usTail));
    if (usTail > 0) {
        std::printf("    -> 头部插入约为后者的 %.1f 倍耗时\n",
                    static_cast<double>(usHead) / static_cast<double>(usTail));
    }
}

// ===========================================================================
// 5 & 6. 查找、子串、比较
// ===========================================================================
void DemoFind() {
    Section("5/6. find / rfind / find_first_of / substr / compare");

    const std::string text = "path/to/some/file.txt";
    std::printf("  待查字符串：\"%s\"\n", text.c_str());

    std::printf("\n  正向查找 find：\n");
    std::printf("    find(\"/\")       = %zu（第一个 '/' 的下标）\n", text.find('/'));
    std::printf("    find(\"/\", 5)    = %zu（从下标 5 开始找）\n", text.find('/', 5));
    std::printf("    find(\"nope\")    = %zu（找不到返回 npos = %zu）\n", text.find("nope"),
                std::string::npos);
    std::printf("    ★ 判断「找到没有」必须写 it != std::string::npos，\n");
    std::printf("      不能写 if (text.find(x)) —— 返回 0 时是「找到了但在开头」，也会被当成假。\n");

    std::printf("\n  反向查找 rfind（找最后一个）：\n");
    std::printf("    rfind('/')       = %zu（最后一个 '/' 的位置，拿文件名就靠它）\n", text.rfind('/'));
    std::printf("    文件名 = substr(rfind('/') + 1) = \"%s\"\n",
                text.substr(text.rfind('/') + 1).c_str());
    std::printf("    目录名 = substr(0, rfind('/'))   = \"%s\"\n",
                text.substr(0, text.rfind('/')).c_str());

    std::printf("\n  扩展名（用 rfind 而不是 find，否则 a.b.c 会取错）：\n");
    const std::size_t dot = text.rfind('.');
    if (dot != std::string::npos) {
        std::printf("    扩展名 = \"%s\"\n", text.substr(dot + 1).c_str());
    }

    std::printf("\n  find_first_of / find_last_of：查找「字符集合里的任意一个」\n");
    const std::string csv = "a,b;c d";
    std::printf("    \"%s\".find_first_of(\",; \") = %zu\n", csv.c_str(), csv.find_first_of(",; "));
    std::printf("    \"%s\".find_last_of(\",; \")  = %zu\n", csv.c_str(), csv.find_last_of(",; "));
    std::printf("    （区分：find(\",; \") 找的是「子串 \",; \"」，find_first_of(\",; \") 找的是「其中任一字符」）\n");

    std::printf("\n  find_first_not_of / find_last_not_of：做 trim 的标准工具\n");
    const std::string padded = " \t  padded value \r\n";
    const std::size_t b = padded.find_first_not_of(" \t\r\n");
    const std::size_t e = padded.find_last_not_of(" \t\r\n");
    std::printf("    原串 = \"%s\"（长度 %zu）\n", padded.c_str(), padded.size());
    if (b == std::string::npos) {
        std::printf("    整个串都是空白\n");
    } else {
        std::printf("    trim 后 = \"%s\"（长度 %zu）\n", padded.substr(b, e - b + 1).c_str(),
                    e - b + 1);
    }

    std::printf("\n  compare：三路比较，返回 <0 / 0 / >0（和 strcmp 语义一致但更安全）\n");
    const std::string a = "apple", bb = "banana";
    std::printf("    \"%s\".compare(\"%s\") = %d\n", a.c_str(), bb.c_str(), a.compare(bb));
    std::printf("    注意：compare 不是 operator< 的替代品 —— operator< 只给 bool，\n");
    std::printf("          compare 给三态结果，排序时想稳定区分「相等」用它。\n");

    std::printf("\n  C++20 新增：starts_with / ends_with\n");
    std::printf("    text.starts_with(\"path/\")  = %s\n", text.starts_with("path/") ? "true" : "false");
    std::printf("    text.ends_with(\".txt\")     = %s\n", text.ends_with(".txt") ? "true" : "false");
    std::printf("    text.find(\"some\") != npos = %s\n",
                (text.find("some") != std::string::npos) ? "true" : "false");
    std::printf("    ★ 以前写 text.find(x) == 0 来判断前缀，现在直接 starts_with，意图清楚得多。\n");
    std::printf("    ★ 注意：contains() 是 C++23 才加的（MSVC 需要 /std:c++23preview），\n");
    std::printf("      C++20 里判断「包含」仍然要用 find(...) != npos。\n");
}

// ===========================================================================
// 7. 数值转换
// ===========================================================================
void DemoNumberConversion() {
    Section("7. 数值转换：stoi / stod vs from_chars / to_chars");

    SubSection("写法 A：std::stoi / std::stod（C++11，抛异常）");
    const char* inputs[] = {"2026", "  -42", "12abc", "abc", "", "99999999999999999999", "3.14"};
    for (const char* in : inputs) {
        try {
            std::size_t pos = 0;
            const int v = std::stoi(in, &pos);  // pos 返回消费了多少字符
            std::printf("    stoi(\"%s\") = %d，消费 %zu 个字符\n", in, v, pos);
        } catch (const std::invalid_argument&) {
            std::printf("    stoi(\"%s\") 抛 std::invalid_argument（压根没有数字）\n", in);
        } catch (const std::out_of_range&) {
            std::printf("    stoi(\"%s\") 抛 std::out_of_range（数字超出了 int 范围）\n", in);
        }
    }
    std::printf("    ★ stoi 的三个特点：\n");
    std::printf("      1) 会跳过前导空白，\"  -42\" 也能成功（很多场合这反而是意外行为）；\n");
    std::printf("      2) 遇到非法字符【不报错】，停在第一个非数字处并返回已解析的部分（\"12abc\" -> 12）；\n");
    std::printf("      3) 越界和非法输入靠【抛异常】报告 —— 异常在热路径上很贵，也不适合 noexcept 代码。\n");

    SubSection("写法 B：std::from_chars（C++17，不抛异常）");
    for (const char* in : inputs) {
        const std::string_view sv(in);
        int v = 0;
        const auto r = std::from_chars(sv.data(), sv.data() + sv.size(), v);
        const std::size_t used = static_cast<std::size_t>(r.ptr - sv.data());
        if (r.ec == std::errc{}) {
            std::printf("    from_chars(\"%s\") = %d，消费 %zu/%zu 个字符%s\n", in, v, used, sv.size(),
                        used == sv.size() ? "（全部解析）" : "（后面还有垃圾字符）");
        } else if (r.ec == std::errc::result_out_of_range) {
            std::printf("    from_chars(\"%s\") -> result_out_of_range（不抛异常，靠返回值）\n", in);
        } else if (r.ec == std::errc::invalid_argument) {
            std::printf("    from_chars(\"%s\") -> invalid_argument（第一个字符就不是数字/符号）\n", in);
        } else {
            std::printf("    from_chars(\"%s\") -> 其它错误\n", in);
        }
    }
    std::printf("    ★ 注意 \"  -42\" 的差别：stoi 会跳过前导空白并成功，from_chars 直接\n");
    std::printf("      报 invalid_argument。哪一种是「对」的取决于你的需求：\n");
    std::printf("      严格解析（协议/配置）要 from_chars 这种行为；宽容解析（人输的）可以先 trim。\n");
    std::printf("    ★ from_chars 的五个优点（这就是它存在的理由）：\n");
    std::printf("      1) 错误通过返回值报告，不抛异常 -> 可用于 noexcept 函数与热路径；\n");
    std::printf("      2) 完全不受 locale 影响。std::stod 在德语等 locale 下小数点会变成逗号，\n");
    std::printf("         协议解析会直接崩掉；from_chars 永远是 \".\" 和千位不进位；\n");
    std::printf("      3) 不需要构造 std::string / istringstream，没有内存分配；\n");
    std::printf("      4) 接受 [first, last) 区间，不需要 '\\0' 结尾，天然配合 string_view。\n");
    std::printf("      5) 不做任何「跳过空白」或「多消费字符」的事 —— 行为是你看到的那样。\n");
    std::printf("    ★ 唯一的限制：C++17 的 from_chars 对浮点支持是「可选」的，\n");
    std::printf("      MSVC / libstdc++ / libc++ 现在都支持，但老编译器可能只支持整数。\n");

    SubSection("浮点转换：from_chars / to_chars");
    const char* kFloatText = "3.14159e2;rest";
    double d = 0.0;
    const std::string_view fsv(kFloatText);
    const auto fr = std::from_chars(fsv.data(), fsv.data() + fsv.size(), d);
    std::printf("    from_chars(\"%s\") = %.6f，停在 \"%s\"\n", kFloatText, d, fr.ptr);

    // to_chars：把数字写成字符，不分配内存、不受 locale 影响
    char buf[64] = {};
    const auto tr = std::to_chars(buf, buf + sizeof(buf), 3.14159265358979, std::chars_format::fixed, 4);
    std::printf("    to_chars(3.14159265..., fixed, 4) = \"%s\"\n",
                std::string(buf, tr.ptr).c_str());

    const auto tr2 = std::to_chars(buf, buf + sizeof(buf), 255, 16);  // 十六进制
    std::printf("    to_chars(255, 基 16)             = \"%s\"\n", std::string(buf, tr2.ptr).c_str());

    const auto tr3 = std::to_chars(buf, buf + sizeof(buf), 1234567);
    std::printf("    to_chars(1234567)                = \"%s\"\n", std::string(buf, tr3.ptr).c_str());
    std::printf("    ★ 和 snprintf 的区别：to_chars 不查 locale、不返回「需要多少字节」，\n");
    std::printf("      而是返回 {ptr, ec}，你自己决定缓冲区。配合 buf + std::string(buf, ptr) 使用。\n");

    SubSection("性能实测：from_chars vs stoi（本机实测，仅供参考）");
    constexpr int kRounds = 100000;
    const std::vector<std::string> numbers = [] {
        std::vector<std::string> v;
        v.reserve(1000);
        for (int i = 0; i < 1000; ++i) {
            v.push_back(std::to_string(i * 7919));
        }
        return v;
    }();

    volatile long long sink = 0;
    const auto t0 = Clock::now();
    for (int i = 0; i < kRounds; ++i) {
        sink += std::stoi(numbers[static_cast<std::size_t>(i) % numbers.size()]);
    }
    const auto t1 = Clock::now();

    const auto t2 = Clock::now();
    for (int i = 0; i < kRounds; ++i) {
        const std::string& num = numbers[static_cast<std::size_t>(i) % numbers.size()];
        int v = 0;
        const auto r = std::from_chars(num.data(), num.data() + num.size(), v);
        if (r.ec == std::errc{}) {
            sink += v;
        }
    }
    const auto t3 = Clock::now();

    const auto usStoi = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    const auto usFc = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
    std::printf("    %d 次整数解析：\n", kRounds);
    std::printf("      std::stoi        : %8lld us\n", static_cast<long long>(usStoi));
    std::printf("      std::from_chars  : %8lld us\n", static_cast<long long>(usFc));
    if (usFc > 0) {
        std::printf("      -> stoi 约为 from_chars 的 %.2f 倍耗时\n",
                    static_cast<double>(usStoi) / static_cast<double>(usFc));
    }
    std::printf("      sink = %lld（防止优化）\n", sink);

    // 浮点解析的差距通常更大，因为 stod 必须先构造 std::string 再走 strtod
    const std::vector<std::string> floats = [] {
        std::vector<std::string> v;
        for (int i = 0; i < 500; ++i) {
            v.push_back(std::to_string(i) + "." + std::to_string(i * 3 % 1000));
        }
        return v;
    }();
    volatile double fsink = 0.0;
    const auto t4 = Clock::now();
    for (int i = 0; i < kRounds; ++i) {
        fsink += std::stod(floats[static_cast<std::size_t>(i) % floats.size()]);
    }
    const auto t5 = Clock::now();
    const auto t6 = Clock::now();
    for (int i = 0; i < kRounds; ++i) {
        const std::string& f = floats[static_cast<std::size_t>(i) % floats.size()];
        double v = 0.0;
        const auto r = std::from_chars(f.data(), f.data() + f.size(), v);
        if (r.ec == std::errc{}) {
            fsink += v;
        }
    }
    const auto t7 = Clock::now();
    const auto usStod = std::chrono::duration_cast<std::chrono::microseconds>(t5 - t4).count();
    const auto usFcf = std::chrono::duration_cast<std::chrono::microseconds>(t7 - t6).count();
    std::printf("\n    %d 次浮点解析：\n", kRounds);
    std::printf("      std::stod        : %8lld us\n", static_cast<long long>(usStod));
    std::printf("      std::from_chars  : %8lld us\n", static_cast<long long>(usFcf));
    if (usFcf > 0) {
        std::printf("      -> stod 约为 from_chars 的 %.2f 倍耗时\n",
                    static_cast<double>(usStod) / static_cast<double>(usFcf));
    }
    std::printf("      fsink = %.6f（防止优化）\n", fsink);
    std::printf("    ★ 差距来源：stod 内部要先构造临时 std::string、再调 strtod、还要查 locale、\n");
    std::printf("      并用异常报告越界；from_chars 是一条直路。\n");
    std::printf("    ★ 工程建议：解析配置文件 / 协议报文 / CSV 一律用 from_chars；\n");
    std::printf("      只在「输入来自人类、希望拿到异常」的一次性交互代码里用 stoi / stod。\n");
}

// ===========================================================================
// 8. std::string_view
// ===========================================================================
void DemoStringView() {
    Section("8. std::string_view：零拷贝的字符串视图");

    const std::string owned = "hello, string_view world";

    // string_view 只是 (指针, 长度)，构造它不拷贝任何字符
    const std::string_view v1 = owned;              // 从 std::string 构造
    const std::string_view v2 = "字面量也是 view";    // 从字符串字面量构造（不拷贝）
    const std::string_view v3 = v1.substr(7, 11);   // substr 也是 O(1)，不拷贝

    std::printf("  string_view 的 sizeof = %zu 字节（就是「指针 + 长度」两个字段）\n",
                sizeof(std::string_view));
    std::printf("  sizeof(std::string)   = %zu 字节（MSVC 上含 SSO 缓冲区）\n", sizeof(std::string));
    std::printf("  v1 = \"%.*s\"（长度 %zu）\n", static_cast<int>(v1.size()), v1.data(), v1.size());
    std::printf("  v2 = \"%.*s\"\n", static_cast<int>(v2.size()), v2.data());
    std::printf("  v3 = v1.substr(7, 11) = \"%.*s\"（O(1)，无拷贝）\n", static_cast<int>(v3.size()),
                v3.data());

    SubSection("string_view 能做的操作（都是只读的）");
    std::printf("    v1.find(\"view\")        = %zu\n", v1.find("view"));
    std::printf("    v1.starts_with(\"hello\") = %s\n", v1.starts_with("hello") ? "true" : "false");
    std::printf("    v1.compare(0, 5, \"hello\") = %d\n", v1.compare(0, 5, "hello"));
    std::printf("    v1.front() = '%c'，v1.back() = '%c'\n", v1.front(), v1.back());
    std::printf("    v1[0] = '%c'（operator[] 在 C++20 起才做边界检查，越界即 UB/异常）\n", v1[0]);

    SubSection("★ 最重要的差别：string_view 不拥有数据，也不保证 '\\0' 结尾");
    const std::string_view mid = v1.substr(7, 4);
    std::printf("    v1.substr(7, 4) 得到的 view 内容 = \"%.*s\"（长度 %zu）\n",
                static_cast<int>(mid.size()), mid.data(), mid.size());
    std::printf("    但它后面的字符仍然存在：mid.data()[4] = '%c'\n", mid.data()[4]);
    std::printf("    所以 string_view 没有 c_str()，也不能直接传给 printf(\"%%s\")：\n");
    std::printf("      正确写法：printf(\"%%.*s\", (int)sv.size(), sv.data())\n");
    std::printf("      或者        std::cout << sv;（<iostream> 有 operator<< 重载）\n");

    SubSection("★ 生命周期陷阱：不要保存指向临时对象的 string_view");
    std::printf("    危险代码（真实项目里非常常见）：\n");
    std::printf("      std::string_view Bad() {\n");
    std::printf("          std::string tmp = \"temporary\";\n");
    std::printf("          return tmp;        // tmp 在函数返回时被销毁\n");
    std::printf("      }                      // 返回的 view 指向已释放的内存\n");
    std::printf("    这是「悬垂 view」，读它就是 UB。编译器通常【不会】报警告。\n");
    std::printf("    规则：\n");
    std::printf("      1) string_view 作为【函数参数】是安全且推荐的（调用期间对象活着）；\n");
    std::printf("      2) 作为【返回值】或【成员变量】时必须确认被指向者的生命周期更长；\n");
    std::printf("      3) 表达式里创建临时 string 再取 view，是最容易踩的坑：\n");
    std::printf("           const std::string_view v = std::string(\"abc\") + \"def\";  // 悬垂！\n");
    std::printf("      4) 需要长期持有 -> 老老实实存 std::string。\n");

    SubSection("与 string 的互操作");
    std::printf("    string -> string_view : 隐式转换，零成本\n");
    std::printf("    string_view -> string : 必须显式 std::string(sv)（因为它要分配内存）\n");
    std::printf("    实测：std::string(v1) = \"%s\"\n", std::string(v1).c_str());
    std::printf("    ★ 这也是为什么「把 API 参数从 const std::string& 改成 std::string_view」\n");
    std::printf("      常常能省掉调用方的临时 string 构造（比如传字面量时不再分配内存）。\n");

    SubSection("零拷贝切分实战：把 CSV 一行切成字段");
    const std::string_view line = "42,hello world,3.14,,end";
    std::printf("    待切分：\"%.*s\"\n", static_cast<int>(line.size()), line.data());
    std::size_t start = 0;
    int fieldIdx = 0;
    while (start <= line.size()) {
        const std::size_t comma = line.find(',', start);
        const std::size_t end = (comma == std::string_view::npos) ? line.size() : comma;
        std::printf("      字段[%d] = \"%.*s\"\n", fieldIdx, static_cast<int>(end - start),
                    line.data() + start);
        ++fieldIdx;
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    std::printf("    ★ 全程零分配：没有一个字段被拷贝成新的 std::string。\n");
    std::printf("      这是解析大文件时最有效的优化之一（对比：用 getline + istringstream 会分配 5 次）。\n");
}

// ===========================================================================
// 9. getline
// ===========================================================================
void DemoGetline() {
    Section("9. std::getline：按行读与按分隔符切分");

    SubSection("按行读（从流里）");
    std::istringstream input("第一行\n第二行\n\n第四行（上面是空行）\n");
    std::string line;
    int no = 0;
    while (std::getline(input, line)) {  // ★ getline 不会把 '\n' 留在结果里
        ++no;
        std::printf("    第 %d 行：\"%s\"（长度 %zu）\n", no, line.c_str(), line.size());
    }
    std::printf("    ★ while (std::getline(...)) 的退出条件同时覆盖「文件结束」和「读取出错」。\n");
    std::printf("      不要写 while (!input.eof()) { getline(...); 用 line; } ——\n");
    std::printf("      那样最后一次失败后 line 内容未定义，会把最后一行处理两次。这是经典 bug。\n");

    SubSection("按分隔符切分（第三个参数）");
    std::istringstream csv("apple,banana,,orange");
    std::string item;
    std::printf("    ");
    while (std::getline(csv, item, ',')) {
        std::printf("[\"%s\"] ", item.c_str());  // 连续分隔符会产生空串，注意这是有意的语义
    }
    std::printf("\n    ★ 对比 string_view 切分：getline 会为每一段分配一个 std::string。\n");
    std::printf("      需要「切出来再转成数字」时 getline 更方便；只是「看一下」时 string_view 更省。\n");

    SubSection("★ getline 与 >> 混用：新手最容易卡住的坑");
    std::printf("    问题复现：\n");
    std::printf("      int n; std::cin >> n;          // 只读走数字，'\\n' 还留在缓冲区里\n");
    std::printf("      std::string s; std::getline(std::cin, s);  // 立刻读到那个 '\\n'，s 变成空串\n");
    std::printf("    正确做法（两种）：\n");
    std::printf("      (1) 读完后丢弃行尾：std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\\n');\n");
    std::printf("      (2) 全程只用 getline，再自己解析数字（更稳，推荐）：\n");
    std::printf("            std::string line; std::getline(std::cin, line);\n");
    std::printf("            int n = std::stoi(line);\n");
    std::printf("    这里做一个可运行的复现（用 istringstream 模拟 cin）：\n");
    {
        std::istringstream fakeCin("42\nhello world\n");
        int n = 0;
        fakeCin >> n;
        std::string s;
        std::getline(fakeCin, s);  // ★ 这里拿到的是空串（只吃掉了残留的 '\n'）
        std::printf("      >> 读 n = %d；接着 getline 得到 \"%s\"（长度 %zu）<- 空的！\n", n, s.c_str(),
                    s.size());
        std::getline(fakeCin, s);  // 再读一次才拿到真正那一行
        std::printf("      再 getline 一次才拿到：\"%s\"\n", s.c_str());
    }
    std::printf("    详细的健壮输入函数模板见 10_iostream_and_files.cpp。\n");

    SubSection("把整行按空白切成若干 token（等价于 >> 的行为）");
    std::istringstream tokens("  alpha   42   -3.5  omega ");
    std::string tok;
    std::printf("    用 >> 逐个读（自动跳过空白）：");
    while (tokens >> tok) {
        std::printf(" [%s]", tok.c_str());
    }
    std::printf("\n    ★ operator>> 对 std::string 的语义就是「读一个空白分隔的词」。\n");
}

// ===========================================================================
// 10. std::format
// ===========================================================================
void DemoFormat() {
    Section("10. std::format（C++20）：类型安全 + 不分配中间流");

    const std::string name = "张三";
    const int age = 30;
    const double score = 92.5678;

    // 位置占位符 {} 会自动推导类型，不需要 %d/%s 那种手工配对
    std::printf("  %s\n", std::format("  你好，{}！你今年 {} 岁。", name, age).c_str());
    // 带索引的占位符：{0} {1}，可以重复使用同一个参数
    std::printf("  %s\n", std::format("  {0} 是 {1}，{0} 的成绩是 {2}", "张三", 30, 92.5).c_str());

    SubSection("格式说明符（冒号后面）：填充 / 对齐 / 宽度 / 精度 / 类型");
    std::printf("  默认         : %s\n", std::format("[{}]", 42).c_str());
    std::printf("  宽度 8       : %s\n", std::format("[{:8}]", 42).c_str());
    std::printf("  左对齐 8     : %s\n", std::format("[{:<8}]", 42).c_str());
    std::printf("  右对齐 8     : %s\n", std::format("[{:>8}]", 42).c_str());
    std::printf("  居中 8       : %s\n", std::format("[{:^8}]", 42).c_str());
    std::printf("  填充星号 8   : %s\n", std::format("[{:*^8}]", 42).c_str());
    std::printf("  补零 8 位    : %s\n", std::format("[{:08d}]", 42).c_str());
    std::printf("  十六进制     : %s\n", std::format("[{:x}] [{:#x}]", 255, 255).c_str());
    std::printf("  二进制       : %s\n", std::format("[{:b}]", 10).c_str());
    std::printf("  浮点 .2f     : %s\n", std::format("[{:.2f}]", score).c_str());
    std::printf("  科学计数 .3e : %s\n", std::format("[{:.3e}]", score).c_str());
    std::printf("  百分比占位符 : %s\n",
                std::format("{:.1f}%%", 92.56).c_str());  // 百分号本身要写两个
    std::printf("  布尔         : %s\n", std::format("[{}] [{:s}]", true, false).c_str());
    std::printf("  指针         : %s\n", std::format("[{}]", static_cast<const void*>(&age)).c_str());
    std::printf("  字符         : %s\n", std::format("[{}] [{:c}]", 'A', 65).c_str());
    std::printf("\n  ★ MSVC 实测的两个坑（本机 14.51 / /std:c++20 验证）：\n");
    std::printf("    std::format(\"{:,}\", 1234567)  千分位分隔符  -> 编译失败 C7595\n");
    std::printf("    std::format(\"{:.1%%}\", 0.9256) 百分号类型    -> 编译失败 C7595\n");
    std::printf("    这两个说明符在 MSVC 的实现里【尚未支持】，虽然标准里写了。\n");
    std::printf("    变通写法：千分位自己插（或者用 std::locale + iostream）；\n");
    std::printf("              百分号写成 \"{:.1f}%%\"，即乘 100 再手工加 %% 。\n");

    SubSection("居中制表对齐（写报告时非常好用）");
    std::printf("    %s\n", std::format("  {:<10}{:>8}{:>12}", "名称", "数量", "金额").c_str());
    std::printf("    %s\n", std::format("  {:<10}{:>8}{:>12.2f}", "苹果", 3, 12.5).c_str());
    std::printf("    %s\n", std::format("  {:<10}{:>8}{:>12.2f}", "进口车厘子", 128, 12345.678).c_str());
    std::printf("    ★ 注意：宽度按【字节】算，中文一个字 3 字节，所以中文列会看起来更宽。\n");
    std::printf("      要做真正的 Unicode 对齐需要额外计算显示宽度（第三方库如 utf8proc）。\n");

    SubSection("std::format vs 三种老写法");
    // (1) printf：类型不安全，参数错位是 UB
    char pbuf[128] = {};
    std::snprintf(pbuf, sizeof(pbuf), "  printf      : %s 是 %d 岁，成绩 %.1f", name.c_str(), age, score);
    std::printf("%s\n", pbuf);
    // (2) iostream：类型安全，但写法啰嗦且容易被 iomanip 状态污染
    std::ostringstream os;
    os << "  iostream    : " << name << " 是 " << age << " 岁，成绩 " << std::fixed
       << std::setprecision(1) << score;
    std::printf("%s\n", os.str().c_str());
    // (3) std::format：类型安全 + 单行 + 无状态
    std::printf("%s\n", std::format("  std::format : {} 是 {} 岁，成绩 {:.1f}", name, age, score).c_str());

    std::printf("\n  三者对比：\n");
    std::printf("    printf      : 最快，但 %%d/%%s 与实参不匹配是 UB，也不支持自定义类型\n");
    std::printf("    iostream    : 类型安全，但 << 链很长，iomanip 的状态会「粘住」后面的输出\n");
    std::printf("    std::format : 类型安全 + 位置无关 + 无状态 + 支持自定义类型，推荐默认使用\n");
    std::printf("    ★ std::format 在编译期就会检查格式串（格式串是 consteval 参数），\n");
    std::printf("      写错占位符直接编译失败，而不是运行期打印出乱码。\n");

    // 编译期检查演示：下面这行如果取消注释会编译失败
    // std::format("{:d}", "字符串");  // error: 类型不匹配，编译期就报错

    SubSection("性能实测：format vs snprintf vs stringstream");
    constexpr int kFmtRounds = 20000;
    volatile std::size_t sinkLen = 0;

    const auto t0 = Clock::now();
    for (int i = 0; i < kFmtRounds; ++i) {
        const std::string out = std::format("id={} name={} value={:.3f}", i, "item", i * 0.5);
        sinkLen += out.size();
    }
    const auto t1 = Clock::now();

    const auto t2 = Clock::now();
    for (int i = 0; i < kFmtRounds; ++i) {
        char b[128] = {};
        const int n = std::snprintf(b, sizeof(b), "id=%d name=%s value=%.3f", i, "item", i * 0.5);
        sinkLen += static_cast<std::size_t>(n);
    }
    const auto t3 = Clock::now();

    const auto t4 = Clock::now();
    for (int i = 0; i < kFmtRounds; ++i) {
        std::ostringstream oss;
        oss << "id=" << i << " name=" << "item" << " value=" << std::fixed << std::setprecision(3)
            << i * 0.5;
        sinkLen += oss.str().size();
    }
    const auto t5 = Clock::now();

    const auto usFmt = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    const auto usSnp = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
    const auto usSs = std::chrono::duration_cast<std::chrono::microseconds>(t5 - t4).count();
    std::printf("  【实测】%d 次「拼一段带数字和浮点的字符串」（本机实测，仅供参考）：\n", kFmtRounds);
    std::printf("    std::format        : %8lld us\n", static_cast<long long>(usFmt));
    std::printf("    snprintf           : %8lld us\n", static_cast<long long>(usSnp));
    std::printf("    ostringstream      : %8lld us  <- 最慢，每轮都构造流对象\n",
                static_cast<long long>(usSs));
    if (usFmt > 0) {
        std::printf("    -> ostringstream 约为 std::format 的 %.1f 倍耗时\n",
                    static_cast<double>(usSs) / static_cast<double>(usFmt));
    }
    std::printf("    sinkLen = %zu（防止优化）\n", sinkLen);
    std::printf("  ★ 结论：新代码默认用 std::format；只在「必须复用同一个格式化缓冲区」\n");
    std::printf("    或对接 C 接口时才用 snprintf；stringstream 留给「拼接类型不固定的内容」。\n");

    SubSection("实用小函数：用 format 做数值格式化");
    // 注意：千分位 {:,} 在 MSVC 的 std::format 里还不支持（编译失败 C7595），
    // 所以这里手工实现一个最简版本：从右往左每三位插一个逗号。
    const auto FormatMoney = [](double yuan) {
        const std::string raw = std::format("{:.2f}", yuan);
        const std::size_t dot = raw.find('.');
        const std::string intPart = raw.substr(0, dot);
        const std::string fracPart = raw.substr(dot);  // 含小数点
        std::string grouped;
        const int len = static_cast<int>(intPart.size());
        for (int i = 0; i < len; ++i) {
            if (i > 0 && ((len - i) % 3 == 0) && intPart[i - 1] != '-') {
                grouped.push_back(',');
            }
            grouped.push_back(intPart[static_cast<std::size_t>(i)]);
        }
        return "￥" + grouped + fracPart;
    };
    const auto FormatPercent = [](double ratio) { return std::format("{:.2f}%", ratio * 100.0); };
    std::printf("    FormatMoney(1234567.891)  = %s\n", FormatMoney(1234567.891).c_str());
    std::printf("    FormatPercent(0.0731)     = %s\n", FormatPercent(0.0731).c_str());
}

}  // namespace

int main() {
    EnableUtf8Console();

    std::printf("==========================================================\n");
    std::printf(" 06_cpp_string.cpp —— std::string / string_view / format\n");
    std::printf("==========================================================\n");

    DemoConstruction();
    DemoSizeCapacitySso();
    DemoModify();
    DemoFind();
    DemoNumberConversion();
    DemoStringView();
    DemoGetline();
    DemoFormat();

    std::printf("\n================ 小结 ================\n");
    std::printf("1. size 是内容长度，capacity 是不重新分配的上限；reserve 只改 capacity。\n");
    std::printf("2. SSO 让长度 <= 15（MSVC）的字符串不碰堆；这也解释了短串 c_str() 悬垂的经典 bug。\n");
    std::printf("3. string 中间 insert/erase 是 O(n)；循环里往头部插入是 O(n^2)，改成「尾部追加 + reverse」。\n");
    std::printf("4. find 找不到返回 npos，比较必须写 != std::string::npos。\n");
    std::printf("5. 数值解析优先 from_chars（不抛异常、不受 locale 影响、更快）；\n");
    std::printf("   需要异常语义的一次性交互代码再用 stoi/stod。\n");
    std::printf("6. string_view 零拷贝，但绝对不要保存指向临时 string 的 view。\n");
    std::printf("7. getline 与 >> 混用前必须清掉行尾换行；更稳的做法是全程 getline。\n");
    std::printf("8. 格式化首选 std::format：编译期检查格式串、无状态、可读性最好。\n");
    return 0;
}
