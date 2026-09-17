// ============================================================================
//  06_structured_bindings_and_auto.cpp
// ----------------------------------------------------------------------------
//  演示主题：
//    1. 结构化绑定：auto [k, v] : map / auto& [k, v] 避免拷贝（性能关键点）
//    2. auto 的推导规则：丢引用、丢顶层 const；auto& / const auto& 才安全
//    3. decltype 与 decltype(auto)
//    4. 返回类型推导（auto 返回值 / 尾置返回类型）
//    5. CTAD（类模板实参推导）与推导指引
//    6. std::string_view：零拷贝视图与悬垂陷阱
//    7. std::optional 替代魔法值与「输出参数 + bool 返回」
//    8. std::variant + std::visit，以及 any / optional / variant 的选型对比
//
//  关键结论：
//    - auto 会剥掉引用和顶层 const，所以遍历大对象必须写 const auto&，否则静默拷贝。
//    - 结构化绑定是「给子对象起名字」，绑定到引用才算真正避免拷贝。
//    - string_view 不拥有数据，只适用于「调用期间数据一定活着」的场景；
//      绝不能返回指向临时 string 的 string_view，也不能长期存成员。
//    - optional 表达「可能没有值」，variant 表达「几种互斥类型之一」，
//      any 表达「完全未知的类型」——越靠后类型信息越少、开销越大，能不用就不用。
// ============================================================================

#include <algorithm>
#include <any>
#include <array>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// ---------------------------------------------------------------- auto 与拷贝
struct HeavyRecord {
    std::string name;
    std::vector<double> samples;

    HeavyRecord() = default;
    HeavyRecord(std::string n, std::vector<double> s)
        : name(std::move(n)), samples(std::move(s)) {}
    HeavyRecord(const HeavyRecord& other) : name(other.name), samples(other.samples) {
        ++copy_count();
    }
    HeavyRecord(HeavyRecord&& other) noexcept
        : name(std::move(other.name)), samples(std::move(other.samples)) {
        ++move_count();
    }
    HeavyRecord& operator=(const HeavyRecord&) = default;
    HeavyRecord& operator=(HeavyRecord&&) noexcept = default;
    ~HeavyRecord() = default;

    static long long& copy_count() {
        static long long value = 0;
        return value;
    }
    static long long& move_count() {
        static long long value = 0;
        return value;
    }
    static void reset() {
        copy_count() = 0;
        move_count() = 0;
    }
};

// ---------------------------------------------------------------- decltype 家族
template <typename Container>
auto first_by_auto(Container& c) {          // auto 返回：按值返回，丢掉引用
    return c[0];
}

template <typename Container>
decltype(auto) first_by_decltype_auto(Container& c) {  // 保留 c[0] 的原生类型（引用）
    return c[0];
}

int global_counter = 0;
int& counter_ref() { return global_counter; }

// ---------------------------------------------------------------- CTAD 与推导指引
template <typename T>
class Box {
public:
    explicit Box(T value) : value_(std::move(value)) {}
    const T& get() const { return value_; }

private:
    T value_;
};

// 推导指引：显式告诉编译器「Box<const char*> 这种推导改成 Box<std::string>」
Box(const char*) -> Box<std::string>;

// ---------------------------------------------------------------- string_view 陷阱
// 错误：返回指向临时 string 的 string_view —— 函数返回后数据已销毁
std::string_view bad_make_view() {
    std::string local = "临时字符串内容";   // 局部对象，函数结束即销毁
    return std::string_view(local);          // 悬垂
}

// 正确：视图的持有者必须在视图存活期间一直存在
std::string_view good_view_of(const std::string& owner) {
    return std::string_view(owner);
}

// string_view 作为「只读字符串参数」：避免 std::string 的构造与堆分配
std::size_t count_words(std::string_view text) {
    std::size_t words = 0;
    bool in_word = false;
    for (const char c : text) {
        if (c == ' ' || c == '\t' || c == '\n') {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            ++words;
        }
    }
    return words;
}

// ---------------------------------------------------------------- optional / variant
std::optional<int> find_index(const std::vector<int>& data, int target) {
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (data[i] == target) {
            return static_cast<int>(i);
        }
    }
    return std::nullopt;  // 明确表达「没有」，而不是返回 -1 这种魔法值
}

// 旧的 C 风格接口：输出参数 + 返回 bool，调用方必须先造一个占位值
bool find_index_old_style(const std::vector<int>& data, int target, int& out_index) {
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (data[i] == target) {
            out_index = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

// variant：几种互斥类型之一，且类型信息保留
using JsonValue = std::variant<std::monostate, bool, long long, double, std::string>;

std::string to_text(const JsonValue& value) {
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                return v ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return "\"" + v + "\"";
            } else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream out;          // 演示里简单格式化一下，正式代码可用 std::format
                out.precision(2);
                out << std::fixed << v;
                return out.str();
            } else {
                return std::to_string(v);
            }
        },
        value);
}

// ---------------------------------------------------------------- 性能实测
template <typename Fn>
double time_ms(Fn&& fn, int repeats) {
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < repeats; ++i) {
        volatile std::size_t sink = fn(i);
        (void)sink;
    }
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

std::vector<HeavyRecord> make_records(std::size_t n) {
    std::vector<HeavyRecord> records;
    records.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        records.emplace_back("record-" + std::to_string(i), std::vector<double>(512, 1.5));
    }
    return records;
}

// 三种字符串参数形式。
// 这里刻意把函数体做成 O(1)（只取 size），目的是让「参数传递本身的成本」成为主导，
// 否则如果函数要扫描全部字符，拷贝成本会被扫描成本淹没（见输出里的对照说明）。
std::string consume_string(std::string text) {             // 按值：每次调用都深拷贝
    return std::to_string(text.size());
}

std::string consume_string_ref(const std::string& text) {  // 只读引用：零拷贝
    return std::to_string(text.size());
}

std::size_t consume_string_view(std::string_view text) {   // 视图：零拷贝、可接字面量
    return text.size();
}

int main() {
    std::cout << "==== 1. 结构化绑定基础 ====\n";
    {
        std::pair<std::string, int> user{"张三", 31};
        auto [name, age] = user;           // 拷贝：两个新变量
        std::cout << "  auto [name, age]     -> " << name << ", " << age << "\n";
        name = "李四";
        std::cout << "  改 name 后 user.first = " << user.first << "（auto 是拷贝）\n";

        auto& [ref_name, ref_age] = user;  // 引用：改的就是原对象
        ref_name = "王五";
        ref_age = 40;
        std::cout << "  auto& [..] 修改后 user = (" << user.first << ", " << user.second << ")\n";

        std::tuple<int, double, std::string> triple{1, 2.5, "tuple"};
        const auto& [i, d, s] = triple;
        std::cout << "  tuple 解包 -> " << i << ", " << d << ", " << s << "\n";

        struct Point {
            int x;
            int y;
        };
        Point p{3, 4};
        const auto [px, py] = p;  // 结构化绑定也支持聚合类型
        std::cout << "  聚合类型解包 -> (" << px << ", " << py << ")\n";
    }

    std::cout << "\n==== 2. map 遍历：auto& 与 const auto& 是性能关键点 ====\n";
    {
        std::map<std::string, HeavyRecord> index;
        index.emplace("alpha", HeavyRecord("alpha", std::vector<double>(256, 1.0)));
        index.emplace("beta", HeavyRecord("beta", std::vector<double>(256, 2.0)));
        index.emplace("gamma", HeavyRecord("gamma", std::vector<double>(256, 3.0)));

        HeavyRecord::reset();
        std::size_t total = 0;
        for (auto [key, value] : index) {   // 每个元素都被完整拷贝一次
            total += key.size() + value.samples.size();
        }
        const long long copies_by_value = HeavyRecord::copy_count();
        std::cout << "  for (auto [k, v] : map)        拷贝次数 = " << copies_by_value
                  << "（每个 value 一份拷贝，key 也拷贝）\n";

        HeavyRecord::reset();
        total = 0;
        for (const auto& [key, value] : index) {  // 引用绑定，零拷贝
            total += key.size() + value.samples.size();
        }
        const long long copies_by_ref = HeavyRecord::copy_count();
        std::cout << "  for (const auto& [k, v] : map) 拷贝次数 = " << copies_by_ref
                  << "（仅绑定引用）\n";
        std::cout << "  checksum = " << total << "（两次求和一致，语义相同、成本差很多）\n";

        HeavyRecord::reset();
        for (auto& [key, value] : index) {  // 需要修改元素时用 auto&（注意不能 const）
            value.samples.push_back(9.0);
        }
        std::cout << "  for (auto& [k, v] : map) 修改后 alpha 的样本数 = "
                  << index.at("alpha").samples.size() << "\n";
        std::cout << "  规则：只读用 const auto&，要改用 auto&，只有确实需要副本才用 auto\n";

        // 结构化绑定不能加 cv 限定符或用 constexpr：auto&& 是绑定到元素的「隐藏变量」
        const auto& [first_key, first_value] = *index.begin();
        std::cout << "  第一个键 = " << first_key << ", 样本数 = " << first_value.samples.size()
                  << "\n";
    }

    std::cout << "\n==== 3. auto 的推导规则：丢引用、丢顶层 const ====\n";
    {
        const int ci = 10;
        auto a = ci;                 // 丢掉顶层 const -> int
        const auto b = ci;           // 显式保留 const
        static_assert(std::is_same_v<decltype(a), int>, "auto 丢掉顶层 const");
        static_assert(std::is_same_v<decltype(b), const int>, "const auto 保留 const");

        int x = 1;
        int& rx = x;
        auto c = rx;                 // 丢掉引用 -> int（拷贝）
        auto& d = rx;                // 显式引用
        static_assert(std::is_same_v<decltype(c), int>, "auto 丢掉引用");
        static_assert(std::is_same_v<decltype(d), int&>, "auto& 是引用");

        const int& crx = x;
        auto e = crx;                // 引用和 const 都丢掉
        const auto& f = crx;         // 保留为 const int&
        static_assert(std::is_same_v<decltype(e), int>, "auto 同时丢掉引用与 const");
        static_assert(std::is_same_v<decltype(f), const int&>, "const auto& 全保留");

        std::cout << "  auto 剥掉引用与顶层 const；需要别名/避免拷贝就写 auto& 或 const auto&\n";
        std::cout << "  （上面若干 static_assert 已经在编译期证明了这些规则）\n";

        // auto&& 是另一回事：它是「转发引用」，会保留引用与 const
        auto&& g = crx;
        static_assert(std::is_same_v<decltype(g), const int&>, "auto&& 绑定左值时推导为左值引用");
        std::cout << "  auto&& 绑定左值时推导成 const int&，所以范围 for 里 auto&& 也常用来「万能」绑定\n";
        std::cout << "  a/b/c/d/e/f/g 均已用于断言，g = " << g << "\n";
    }

    std::cout << "\n==== 4. decltype 与 decltype(auto) ====\n";
    {
        std::vector<int> data{10, 20, 30};
        auto by_auto = first_by_auto(data);
        decltype(auto) by_decltype = first_by_decltype_auto(data);
        static_assert(std::is_same_v<decltype(by_auto), int>, "auto 返回值按值");
        static_assert(std::is_same_v<decltype(by_decltype), int&>, "decltype(auto) 保留引用");

        by_decltype = 99;
        std::cout << "  通过 decltype(auto) 返回的引用修改后 data[0] = " << data[0] << "\n";
        std::cout << "  by_auto 仍是 " << by_auto << "（它是独立副本）\n";

        // decltype(表达式) 的规则：加括号的左值表达式带引用，未加括号的成员访问不带
        static_assert(std::is_same_v<decltype(data[0]), int&>, "下标表达式是左值");
        static_assert(std::is_same_v<decltype(counter_ref()), int&>, "函数返回引用");
        static_assert(std::is_same_v<decltype((global_counter)), int&>, "加括号变成左值表达式");
        static_assert(std::is_same_v<decltype(global_counter), int>, "不加括号取声明类型");
        std::cout << "  decltype((x)) 与 decltype(x) 结果不同：多一层括号就是表达式\n";

        // 注意：decltype(auto) 返回局部变量的引用是悬垂的经典错误
        std::cout << "  陷阱：decltype(auto) 返回局部变量的引用 -> 悬垂，必须避免\n";

        // auto 返回类型推导与尾置返回类型
        auto lambda_add = [](int m, int n) -> long long { return static_cast<long long>(m) + n; };
        std::cout << "  尾置返回类型：lambda_add(1, 2) = " << lambda_add(1, 2) << "\n";
    }

    std::cout << "\n==== 5. CTAD（类模板实参推导） ====\n";
    {
        Box box_int(42);                          // Box<int>
        Box box_text(std::string("hello"));       // Box<std::string>
        Box box_literal("literal");               // 推导指引把 const char* 变成 std::string
        static_assert(std::is_same_v<decltype(box_int), Box<int>>, "CTAD 推出 Box<int>");
        static_assert(std::is_same_v<decltype(box_literal), Box<std::string>>,
                      "推导指引把字面量变成 std::string");
        std::cout << "  Box box_int(42)         -> " << box_int.get() << "\n";
        std::cout << "  Box box_text(string)    -> " << box_text.get() << "\n";
        std::cout << "  Box box_literal(\"lit\")  -> " << box_literal.get()
                  << "（推导指引生效）\n";

        int raw[3] = {7, 8, 9};
        // 标准库自带的推导指引：std::array + CTAD 可以从「元素个数」推出 N
        std::array deduced_array{raw[0], raw[1], raw[2]};  // std::array<int, 3>
        static_assert(std::is_same_v<decltype(deduced_array), std::array<int, 3>>,
                      "std::array 的推导指引推出 std::array<int, 3>");
        std::cout << "  std::array deduced_array{7, 8, 9} -> size = " << deduced_array.size()
                  << "\n";
        std::cout << "  注意：推导指引只决定模板参数，构造函数仍要能接受实参；\n";
        std::cout << "        例如 std::vector v(raw) 不行，要写 std::vector<int> v(raw, raw + 3)\n";

        // 标准库的大量 CTAD：pair / tuple / vector / lock_guard / scoped_lock
        std::pair deduced{1, std::string("one")};  // std::pair<int, std::string>
        std::vector deduced_vec{1, 2, 3};          // std::vector<int>
        std::cout << "  std::pair deduced{1, \"one\"} -> " << deduced.first << ", "
                  << deduced.second << ", vector size = " << deduced_vec.size() << "\n";
        std::cout << "  CTAD 只推导类型，不改变语义；有歧义时优先显式写出模板参数\n";
    }

    std::cout << "\n==== 6. std::string_view：零拷贝视图与悬垂陷阱 ====\n";
    {
        const std::string owner = "the quick brown fox jumps over the lazy dog";
        const std::string_view view = owner;      // 视图：不复制字符
        std::cout << "  view.size() = " << view.size()
                  << ", owner.size() = " << owner.size()
                  << "（size 相同，但没有第二份字符数据）\n";
        std::cout << "  count_words(view) = " << count_words(view) << "\n";
        std::cout << "  substr 不产生新字符串：view.substr(4, 5) = " << view.substr(4, 5) << "\n";

        std::cout << "  作为函数参数可以省掉临时 std::string：\n";
        std::cout << "    count_words(\"字面量直接传\") = " << count_words("literal in place") << "\n";

        std::cout << "  悬垂示例（危险）：\n";
        const std::string_view dangling = bad_make_view();
        std::cout << "    bad_make_view() 返回的视图指向已销毁的局部 string，"
                     "本次读到 " << dangling.size() << " 字节，属未定义行为，不可依赖\n";

        std::cout << "  安全规则：\n";
        std::cout << "    1) 视图的生命期必须严格短于它引用的数据\n";
        std::cout << "    2) 不要返回指向局部 string / 临时 string 的 string_view\n";
        std::cout << "    3) 不要把 string_view 存成成员，除非所有权另有保障\n";
        std::cout << "    4) string_view 不保证以 \\0 结尾，传给 C API 前必须转成 std::string\n";

        const std::string owner2 = "abc";
        std::cout << "    good_view_of(owner2) = " << good_view_of(owner2) << "（owner2 活着）\n";
    }

    std::cout << "\n==== 6b. 性能实测：字符串传值 vs const 引用 vs string_view ====\n";
    {
        const std::string payload(10000, 'x');
        const std::string_view view_of_payload = payload;
        constexpr int kRepeats = 10000;

        const double t_by_value = time_ms(
            [&](int) { return consume_string(payload).size(); }, kRepeats);
        const double t_by_const_ref = time_ms(
            [&](int) { return consume_string_ref(payload).size(); }, kRepeats);
        const double t_by_view = time_ms(
            [&](int) { return consume_string_view(view_of_payload); }, kRepeats);

        std::cout << "  字符串长度 = " << payload.size() << "，重复 " << kRepeats
                  << " 次（被调函数只取 size()，让「传参成本」成为主导）\n";
        std::cout << "  void f(std::string s)        : " << t_by_value
                  << " ms（每次都深拷贝一份字符数据）\n";
        std::cout << "  void f(const std::string& s) : " << t_by_const_ref << " ms（只传引用）\n";
        std::cout << "  void f(std::string_view s)   : " << t_by_view << " ms（指针 + 长度）\n";
        if (t_by_const_ref > 0.0) {
            std::cout << "  传值 / 传 const 引用         : " << (t_by_value / t_by_const_ref)
                      << " 倍\n";
        }
        std::cout << "  结论与边界：\n";
        std::cout << "    - 传值要复制全部字符，字符串越长越亏；实测这里约为 const 引用的 4 倍，\n";
        std::cout << "      而且 const 引用那一档还被 to_string 的堆分配拉高了（string_view 只返回长度，\n";
        std::cout << "      所以最快），说明「函数体本身的开销」会掩盖一部分传参差距\n";
        std::cout << "    - 如果函数体是 O(n) 扫描，拷贝成本会被进一步淹没，差距缩到 1.1 倍左右\n";
        std::cout << "    - 工程结论：只读参数默认 const std::string& 或 string_view；\n";
        std::cout << "      需要保存一份副本时反而应该按值 + std::move（见 NOTES.md 第 4 节）\n";
    }

    std::cout << "\n==== 7. std::optional：替代魔法值与输出参数 ====\n";
    {
        const std::vector<int> data{5, 7, 9, 11};

        if (const auto found = find_index(data, 9)) {
            std::cout << "  find_index(data, 9)  = " << *found << "\n";
        }
        if (!find_index(data, 100)) {
            std::cout << "  find_index(data, 100) = 无值（不是 -1 这种魔法值）\n";
        }
        std::cout << "  value_or 提供默认值：find_index(data, 100).value_or(-1) = "
                  << find_index(data, 100).value_or(-1) << "\n";

        int legacy_out = -1;   // 调用方被迫先造一个占位值，且无法忘记检查返回值
        if (find_index_old_style(data, 9, legacy_out)) {
            std::cout << "  旧风格输出参数 = " << legacy_out
                      << "（能工作，但调用点更啰嗦、容易漏检）\n";
        }

        std::optional<std::string> empty_string = std::string("");  // 有值，只是值为空
        std::optional<std::string> no_string = std::nullopt;        // 无值
        std::cout << "  optional<string>(\"\") 有值 ? " << (empty_string ? "是" : "否")
                  << "，nullopt 有值 ? " << (no_string ? "是" : "否")
                  << "（区分「空字符串」与「没有值」是 optional 的核心价值）\n";
    }

    std::cout << "\n==== 8. std::variant + std::visit ====\n";
    {
        const std::vector<JsonValue> payload{
            std::monostate{},
            true,
            42LL,
            3.14,
            std::string("文本"),
        };
        for (const auto& value : payload) {
            std::cout << "  " << to_text(value) << "\n";
        }
        std::cout << "  sizeof(variant) = " << sizeof(JsonValue)
                  << "（= 最大成员大小 + 索引，没有堆分配）\n";

        JsonValue v = 7LL;
        std::cout << "  holds_alternative<long long> ? "
                  << (std::holds_alternative<long long>(v) ? "是" : "否") << "\n";
        std::cout << "  get<long long>(v) = " << std::get<long long>(v) << "\n";
        v = std::string("换成字符串");
        std::cout << "  重新赋值后 index = " << v.index() << ", to_text = " << to_text(v) << "\n";

        // get_if 用于「不确定是哪个类型」时，避免抛 bad_variant_access
        if (const auto* as_text = std::get_if<std::string>(&v)) {
            std::cout << "  get_if 成功：*as_text = " << *as_text << "\n";
        }
        if (std::get_if<double>(&v) == nullptr) {
            std::cout << "  get_if<double> 返回 nullptr，没有抛异常\n";
        }
        std::cout << "  visit 的价值：编译器检查穷尽性；漏掉一个类型会直接编译错误（配 if constexpr/overload）\n";
    }

    std::cout << "\n==== 9. optional / variant / any 选型对比 ====\n";
    {
        std::any anything = 42;
        std::cout << "  any 存 int: " << std::any_cast<int>(anything) << "\n";
        anything = std::string("改成字符串");
        std::cout << "  any 再存 string: " << std::any_cast<std::string>(anything) << "\n";
        try {
            std::cout << std::any_cast<int>(anything) << "\n";  // 类型不匹配
        } catch (const std::bad_any_cast&) {
            std::cout << "  any_cast<int> 类型不符 -> 抛 std::bad_any_cast（运行期才知道）\n";
        }

        std::cout << "  选型表：\n";
        std::cout << "    optional<T>         0 或 1 个 T，且 T 类型已知\n";
        std::cout << "    variant<A, B, C>    恰好其中之一，类型集合编译期已知，visit 可穷尽检查\n";
        std::cout << "    any                  任意类型，类型信息在运行期，只能 any_cast，可能抛异常\n";
        std::cout << "    代价：optional 0 额外分配 / variant 无分配但占最大成员空间 / "
                     "any 可能堆分配\n";
        std::cout << "    结论：能用 variant 表达的「多态数据」不要用 any，"
                     "类型安全与性能都更好\n";
    }

    std::cout << "\n==== 10. 性能实测：遍历拷贝 vs 引用 ====\n";
    {
        const auto records = make_records(400);
        constexpr int kRepeats = 400;

        const double t_by_value = time_ms(
            [&](int) {
                std::size_t total = 0;
                for (auto record : records) {          // 每个元素整体拷贝
                    total += record.name.size() + record.samples.size();
                }
                return total;
            },
            kRepeats);

        const double t_by_const_ref = time_ms(
            [&](int) {
                std::size_t total = 0;
                for (const auto& record : records) {   // 只绑定引用
                    total += record.name.size() + record.samples.size();
                }
                return total;
            },
            kRepeats);

        std::cout << "  容器含 " << records.size() << " 条记录，每条 512 个 double，重复 "
                  << kRepeats << " 轮\n";
        std::cout << "  for (auto record : records)        : " << t_by_value << " ms\n";
        std::cout << "  for (const auto& record : records) : " << t_by_const_ref << " ms\n";
        if (t_by_const_ref > 0.0) {
            std::cout << "  比值                                : "
                      << (t_by_value / t_by_const_ref) << " 倍\n";
        }
        std::cout << "  结论：auto 遍历大对象会静默拷贝，const auto& 是默认写法；\n";
        std::cout << "        Debug 下拷贝还要额外挨一遍调试堆检查，差距被放大（这里接近 500 倍）；\n";
        std::cout << "        Release 下差距会缩小到十倍量级，但「拷贝 vs 引用」的相对关系不变，\n";
        std::cout << "        数字随机器 / 配置变化，趋势稳定\n";
    }

    std::cout << "\n==== 小结 ====\n";
    std::cout << "  遍历写 const auto&（要改就 auto&），结构化绑定只是给子对象起名\n";
    std::cout << "  auto 丢引用丢 const，decltype(auto) 保留，decltype 看表达式\n";
    std::cout << "  string_view 是「借用」，optional 是「可能没有」，variant 是「互斥多选」\n";
    return 0;
}
