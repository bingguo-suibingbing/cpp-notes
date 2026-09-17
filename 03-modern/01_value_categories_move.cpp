// ============================================================================
//  01_value_categories_move.cpp
// ----------------------------------------------------------------------------
//  演示主题：
//    1. 值类别三分类：左值（lvalue）/ 纯右值（prvalue）/ 将亡值（xvalue）
//    2. std::move 的本质：一次 static_cast<T&&>，运行期什么都不做
//    3. 右值引用 T&& 与「万能引用 / 转发引用」
//    4. 引用折叠规则：& && -> &，其余全部折叠成 &&
//    5. std::forward 与完美转发
//    6. 移动语义什么时候真正生效（移动构造被调用 vs 只是绑定了引用）
//    7. std::move 误用清单：const 对象、move 之后再使用、return std::move(局部变量)
//
//  关键结论：
//    - std::move 不移动任何东西，它只是把表达式「标记」成可被移动的将亡值。
//    - 「能不能移动」取决于类型：const T 被 move 后仍然走拷贝构造（因为 const T&&
//      只能绑定到 const T& 拷贝构造函数）。
//    - 只有模板里形如 T&& 且 T 真的被推导时才是万能引用；vector<T>&& 是普通右值引用。
//    - 返回局部变量时不要写 return std::move(x)，那会破坏 NRVO（返回值优化）。
// ============================================================================

#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------- 观测用类型
// 一个会大声汇报自己「被拷贝 / 被移动」的类型，用来观察语义是否真的发生
class Tracker {
public:
    explicit Tracker(std::string name) : name_(std::move(name)) {
        std::cout << "  [ctor ] " << name_ << " 构造\n";
    }
    Tracker(const Tracker& other) : name_(other.name_) {
        std::cout << "  [copy ] " << name_ << " 拷贝构造\n";
    }
    Tracker(Tracker&& other) noexcept : name_(std::move(other.name_)) {
        other.name_ = "(已搬空)";
        std::cout << "  [move ] 移动构造\n";
    }
    Tracker& operator=(const Tracker& other) {
        name_ = other.name_;
        std::cout << "  [copy=] " << name_ << " 拷贝赋值\n";
        return *this;
    }
    Tracker& operator=(Tracker&& other) noexcept {
        name_ = std::move(other.name_);
        other.name_ = "(已搬空)";
        std::cout << "  [move=] 移动赋值\n";
        return *this;
    }
    ~Tracker() = default;

    const std::string& name() const { return name_; }

private:
    std::string name_;
};

// ---------------------------------------------------------------- 值类别演示
void take_by_value(Tracker) { std::cout << "      -> 形参按值接收（发生了一次构造/拷贝/移动）\n"; }
void take_by_lref(Tracker&) { std::cout << "      -> 形参是左值引用：没有新对象\n"; }
void take_by_rref(Tracker&&) { std::cout << "      -> 形参是右值引用：没有新对象，但标记为可移动\n"; }

// 万能引用：T 由实参推导出来，T&& 才能真正「转发」
template <typename T>
void universal_ref(T&& value) {
    // T 的推导结果直接暴露了实参的值类别：
    //   实参是左值  -> T 推导为 U&      ，T&& 经引用折叠变回 U&
    //   实参是右值  -> T 推导为 U       ，T&& 就是真正的右值引用
    // 注意顺序：const 左值时 T = const U&，所以「先判 const」才能正确分类
    if constexpr (std::is_const_v<std::remove_reference_t<T>>) {
        std::cout << "      -> T 推导为 const 左值引用（实参是 const 左值）\n";
    } else if constexpr (std::is_lvalue_reference_v<T>) {
        std::cout << "      -> T 推导为 左值引用（说明实参是左值）\n";
    } else {
        std::cout << "      -> T 推导为 非引用类型（说明实参是右值）\n";
    }
    (void)value;  // 这里只是为了演示推导结果，不使用 value
}

// 非万能引用：类型已经确定是 vector<int>，&& 只是普通的右值引用
void rref_to_concrete(std::vector<int>&& v) {
    std::cout << "      -> vector<int>&& 是普通右值引用，v 本身仍是左值，大小 "
              << v.size() << "\n";
}

// ---------------------------------------------------------------- 完美转发
class Sink {
public:
    // 完美转发：把实参的值类别原样传给成员，左值仍然拷贝、右值仍然移动
    template <typename T>
    explicit Sink(T&& value) : data_(std::forward<T>(value)) {
        std::cout << "      -> Sink 构造完成，data_ = " << data_ << "\n";
    }
    const std::string& data() const { return data_; }

private:
    std::string data_;
};

// 如果没有 std::forward，右值实参在函数内部会退化成具名左值，于是被拷贝而不是移动。
// 用 Tracker 当载荷，就能从 [copy] / [move] 日志直接看出值类别有没有被保留。
template <typename T>
Tracker bad_forward(T&& value) {
    return Tracker(value);  // value 是具名变量，是左值 -> 一定调用拷贝构造
}

template <typename T>
Tracker good_forward(T&& value) {
    return Tracker(std::forward<T>(value));  // 原样转发 -> 右值走移动构造
}

// ---------------------------------------------------------------- 返回值与 NRVO
Tracker make_local_normal() {
    Tracker t("nrvo-候选人");
    return t;  // NRVO：通常直接在调用者的存储里构造，零次拷贝/移动
}

Tracker make_local_moved() {
    Tracker t("被-move-的局部变量");
    return std::move(t);  // 强制成右值：破坏 NRVO，多做一次移动，纯属负优化
}

// 注意：返回「函数参数」时 std::move 是合理的，因为参数不参与 NRVO
Tracker take_and_return(Tracker t) {
    return t;  // 这里其实是移动（隐式 move on return 只对局部变量和参数生效）
}

// ---------------------------------------------------------------- 性能实测
// 用 std::chrono::steady_clock 对比三种「返回大对象」的写法
// 说明：本仓库默认以 Debug 编译，Debug 下数字仅供参考；Release 下趋势更明显。
template <typename Fn>
double time_it_ms(Fn&& fn, int repeats) {
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < repeats; ++i) {
        // 把结果吃掉，避免被优化掉（Debug 下本来也不会优化，但习惯要好）
        volatile std::size_t sink = fn(i).size();
        (void)sink;
    }
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

std::vector<int> return_by_value(int n) {
    std::vector<int> v(static_cast<std::size_t>(n), 7);
    return v;  // 最推荐的写法：借 NRVO / 移动，调用方零拷贝
}

std::vector<int> return_by_move(int n) {
    std::vector<int> v(static_cast<std::size_t>(n), 7);
    return std::move(v);  // 阻止 NRVO 的经典误用
}

std::vector<int> return_by_reserve(int n) {
    std::vector<int> v;
    v.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        v.push_back(i);
    }
    return v;
}

// vector 扩容 vs reserve：这是 move 语义之外最常见的性能习惯
std::vector<int> push_without_reserve(int n) {
    std::vector<int> v;
    for (int i = 0; i < n; ++i) {
        v.push_back(i);
    }
    return v;
}

// 把 std::move 用在 const 对象上：仍然拷贝，因为 const T&& 匹配不上 T&&
void const_move_trap() {
    std::cout << "\n[7] 误用一：对 const 对象 std::move —— 它仍然会拷贝\n";
    const Tracker c("const 对象");
    std::cout << "  执行 Tracker moved(std::move(c));\n";
    Tracker moved(std::move(c));  // const Tracker&& -> 只能调用 const Tracker&
    std::cout << "  结果：moved.name() = " << moved.name()
              << "（源对象没被搬空，说明调用的是拷贝构造）\n";
}

int main() {
    std::cout << "==== 1. 值类别三分类 ====\n";
    {
        int a = 10;             // a 是左值：有名字、可取地址
        int&& r = std::move(a); // 把 a 的表达式类别转成将亡值，再绑定到右值引用
        std::cout << "  左值 a 的地址      = " << static_cast<const void*>(&a) << "\n";
        std::cout << "  右值引用 r 的地址  = " << static_cast<const void*>(&r) << "\n";
        std::cout << "  两者相同 -> std::move 没有创建新对象，也没有搬数据\n";
        r = 42;  // r 是具名引用，本身是左值，可以写
        std::cout << "  a = " << a << "（通过 r 改到的就是 a）\n";

        Tracker t("t");
        std::cout << "  std::move(t) 之后 t 还在原地，未发生任何移动：\n";
        [[maybe_unused]] Tracker&& ref = std::move(t);
        std::cout << "  t.name() = " << t.name() << "\n";
    }

    std::cout << "\n==== 2. std::move 的本质就是 static_cast<T&&> ====\n";
    {
        Tracker t("cast 演示");
        std::cout << "  auto&& x = static_cast<Tracker&&>(t);  // 与 std::move(t) 完全等价\n";
        [[maybe_unused]] Tracker&& x = static_cast<Tracker&&>(t);
        std::cout << "  到这里都没有 [move] 输出，证明 move 本身没有运行期动作\n";
    }

    std::cout << "\n==== 3. 移动构造什么时候真正被调用 ====\n";
    {
        Tracker t("源对象");
        std::cout << "  (a) 绑定引用：Tracker&& r = std::move(t);\n";
        [[maybe_unused]] Tracker&& r = std::move(t);
        std::cout << "      没有构造发生，只是多了一个名字\n";

        std::cout << "  (b) 真正构造新对象：Tracker u(std::move(t));\n";
        Tracker u(std::move(t));
        std::cout << "      这次才有 [move]；t.name() = " << t.name() << "（源对象已被搬空）\n";
        (void)u;
        std::cout << "  结论：move 只是「允许被移动」，有构造/赋值发生时才真的移动\n";
    }

    std::cout << "\n==== 4. 按值 / 左值引用 / 右值引用三种形参 ====\n";
    {
        Tracker t("实参");
        std::cout << "  take_by_lref(t):\n";
        take_by_lref(t);
        std::cout << "  take_by_rref(std::move(t)):\n";
        take_by_rref(std::move(t));
        std::cout << "  take_by_value(std::move(t))（会多一次移动构造）:\n";
        take_by_value(std::move(t));
    }

    std::cout << "\n==== 5. 万能引用 vs 普通右值引用 ====\n";
    {
        int n = 1;
        const int cn = 2;
        std::cout << "  universal_ref(n)  —— 传左值\n";
        universal_ref(n);
        std::cout << "  universal_ref(cn) —— 传 const 左值\n";
        universal_ref(cn);
        std::cout << "  universal_ref(3)  —— 传右值\n";
        universal_ref(3);

        std::vector<int> v{1, 2, 3};
        std::cout << "  rref_to_concrete(v) 不行：编译期就要求右值\n";
        std::cout << "  rref_to_concrete(std::move(v))  —— vector<int>&& 不是万能引用\n";
        rref_to_concrete(std::move(v));
        std::cout << "  注意：v 已被 moving-from，此后不能再使用它\n";
    }

    std::cout << "\n==== 6. 引用折叠与完美转发 ====\n";
    {
        std::cout << "  引用折叠规则：T& &、T& &&、T&& & 都折叠成 T&；只有 T&& && 折叠成 T&&\n";
        std::cout << "  所以万能引用里 T 推导成 U& 时，T&& 折叠回 U&，左值因此可以绑定\n\n";

        std::string text = "一个相当长的字符串，拷贝它是有代价的";
        std::cout << "  左值实参走 Sink（应看到拷贝）:\n";
        Sink s1(text);
        std::cout << "  右值实参走 Sink（应看到移动，没有字符串堆分配拷贝）:\n";
        Sink s2(std::move(text));
        std::cout << "  s2.data() = " << s2.data() << "\n";

        std::cout << "\n  不写 std::forward 会怎样：\n";
        Tracker payload("载荷");
        std::cout << "  bad_forward(std::move(payload))（丢掉右值性 -> 拷贝）:\n";
        [[maybe_unused]] Tracker r1 = bad_forward(std::move(payload));
        std::cout << "    payload.name() = " << payload.name()
                  << "（没有被搬空 -> 走的是拷贝构造，白拷了一份）\n";
        Tracker payload2("载荷2");
        std::cout << "  good_forward(std::move(payload2))（保留右值性 -> 移动）:\n";
        [[maybe_unused]] Tracker r2 = good_forward(std::move(payload2));
        std::cout << "    payload2.name() = " << payload2.name()
                  << "（被搬空 -> 走的是移动构造）\n";
        (void)s1;
    }

    std::cout << "\n==== 7. 误用清单 ====\n";
    const_move_trap();

    std::cout << "\n[7.2] 误用二：move 之后继续使用源对象（moved-from 状态未定义但合法）\n";
    {
        std::string s = "内容";
        std::string moved = std::move(s);
        std::cout << "  moved = " << moved << "\n";
        std::cout << "  s.size() = " << s.size()
                  << "（标准只保证「有效但未指定」，不能依赖具体内容）\n";
        s = "重新赋值才安全";  // 想让源对象复活，必须显式赋值
        std::cout << "  重新赋值后 s = " << s << "\n";
    }

    std::cout << "\n[7.3] 误用三：return std::move(局部变量) 会阻止 NRVO\n";
    {
        std::cout << "  make_local_normal():\n";
        [[maybe_unused]] Tracker a = make_local_normal();
        std::cout << "  make_local_moved():\n";
        [[maybe_unused]] Tracker b = make_local_moved();
        std::cout << "  上面多出来的那一次 [move] 就是 std::move 的代价\n";

        std::cout << "  但「返回函数参数」时移动是正确的，因为参数不参与 NRVO\n";
        std::cout << "  （下面两次 [move] 分别来自「构造形参」和「构造返回值」，都省不掉）:\n";
        Tracker src("参数场景");
        [[maybe_unused]] Tracker c = take_and_return(std::move(src));
    }

    std::cout << "\n==== 8. 性能实测（Debug 构建，数字仅供参考）====\n";
    {
        constexpr int kRepeats = 20000;
        constexpr int kSize = 64;

        const double t_value = time_it_ms([](int n) { return return_by_value(n); }, kRepeats);
        const double t_move = time_it_ms([](int n) { return return_by_move(n); }, kRepeats);

        // 扩容对比要放大规模才看得出差距：小规模时分配开销互相掩盖
        constexpr int kGrowRepeats = 200;
        constexpr int kGrowSize = 20000;
        const double t_reserve = time_it_ms([](int n) { return return_by_reserve(n); }, kGrowRepeats);
        const double t_grow = time_it_ms([](int n) { return push_without_reserve(n); }, kGrowRepeats);

        std::cout << "  试验 A：返回对象（" << kSize << " 个 int，重复 " << kRepeats << " 次）\n";
        std::cout << "    返回局部变量（NRVO）        : " << t_value << " ms\n";
        std::cout << "    return std::move(局部变量)  : " << t_move << " ms\n";
        std::cout << "  试验 B：逐元素填充（" << kGrowSize << " 个 int，重复 " << kGrowRepeats << " 次）\n";
        std::cout << "    push_back 前 reserve        : " << t_reserve << " ms\n";
        std::cout << "    push_back 不 reserve        : " << t_grow << " ms\n";
        std::cout << "  结论：\n";
        std::cout << "    - return std::move(局部变量) 不会更快，通常更慢（多一次移动构造）\n";
        std::cout << "    - reserve 减少扩容与元素搬迁次数，规模越大差距越明显\n";
        std::cout << "    - 数字随机器 / 编译器 / 优化级别变化，趋势稳定，不要当绝对真理\n";
    }

    std::cout << "\n==== 小结 ====\n";
    std::cout << "  std::move   = 无条件转成右值（static_cast<T&&>）\n";
    std::cout << "  std::forward= 有条件转成右值（保留原本的值类别）\n";
    std::cout << "  移动语义是「所有权转移的许可」，不是「一定发生的动作」\n";
    return 0;
}
