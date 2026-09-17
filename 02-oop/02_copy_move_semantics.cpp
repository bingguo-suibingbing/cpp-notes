// ============================================================================
// 02_copy_move_semantics.cpp
// 演示主题：
//   1. 拷贝构造 / 拷贝赋值 / 析构 —— Rule of Three
//   2. 移动构造 / 移动赋值 —— Rule of Five
//   3. 编译器自动生成特殊成员函数的规则：什么时候不再生成
//   4. 浅拷贝 vs 深拷贝：经典的 double free（用注释演示，不在运行时真的崩）
//   5. std::move 只是「类型转换」，本身不移动任何东西
//   6. RVO / NRVO：返回值优化，以及怎么亲手关掉它来观察拷贝
//   7. copy-and-swap 惯用法：异常安全 + 自赋值安全
//   8. Rule of Zero：优先让编译器生成，用成员自己管理资源
//
// 关键结论：
//   类只要「直接持有」需要手工释放的资源，就必须同时想清楚拷贝/移动/析构三件事；
//   反之，只要用 vector / string / unique_ptr 这些成员，就什么都别写（Rule of Zero）。
// ============================================================================

#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

namespace {

// ---------------------------------------------------------------------------
// 全局计数器：观察拷贝/移动到底发生了几次
// ---------------------------------------------------------------------------
int g_copy_ctor = 0;
int g_copy_assign = 0;
int g_move_ctor = 0;
int g_move_assign = 0;
int g_heap_allocs = 0;   // 累计 new char[] 次数
int g_heap_frees = 0;    // 累计 delete[] 次数
int g_heap_live = 0;     // 当前未被释放的堆缓冲数量

void ResetCounters() {
    g_copy_ctor = g_copy_assign = g_move_ctor = g_move_assign = 0;
}

void PrintCounters(const char* tag) {
    std::cout << "    [" << tag << "] 拷贝构造=" << g_copy_ctor
              << " 拷贝赋值=" << g_copy_assign
              << " 移动构造=" << g_move_ctor
              << " 移动赋值=" << g_move_assign << "\n";
}

void PrintLine() { std::cout << "--------------------------------------------------\n"; }

// ===========================================================================
// 1/2/4/6/7. 一个完整实现 Rule of Five 的类，顺带演示 copy-and-swap 所需的两步
// ===========================================================================
class String {
public:
    String() : data_(nullptr), size_(0) {}

    explicit String(const char* s) : data_(nullptr), size_(s ? std::strlen(s) : 0) {
        if (size_ > 0) {
            data_ = new char[size_ + 1];
            std::memcpy(data_, s, size_ + 1);
            ++g_heap_allocs;
            ++g_heap_live;
        }
    }

    // ---- Rule of Three 第 1 件：析构函数，负责释放自己持有的资源 ----
    ~String() { Release(); }

    // ---- Rule of Three 第 2 件：拷贝构造 —— 必须是「深拷贝」 ----
    // 【错误写法演示】浅拷贝只复制指针，两个对象指向同一块内存：
    //   String(const String& o) : data_(o.data_), size_(o.size_) {}   // 危险！
    // 后果：两个对象析构时对同一指针各 delete[] 一次 => double free，
    //       程序在第二个析构处崩溃（Debug 下 MSVC 会弹 heap corruption 断言）。
    String(const String& o) : data_(nullptr), size_(o.size_) {
        ++g_copy_ctor;
        if (size_ > 0) {
            data_ = new char[size_ + 1];      // 独立申请一块内存
            std::memcpy(data_, o.data_, size_ + 1);
            ++g_heap_allocs;
            ++g_heap_live;
        }
    }

    // ---- 移动构造：接管源对象的资源，并把源对象置为「空但合法」 ----
    // noexcept 很重要：标准容器扩容时会优先选「不会抛」的移动，否则退回复制。
    String(String&& o) noexcept : data_(o.data_), size_(o.size_) {
        ++g_move_ctor;
        o.data_ = nullptr;      // 关键一步：否则源对象析构时会把资源释放掉
        o.size_ = 0;
        // 注意：这里不产生新的堆分配，所以 g_heap_allocs / g_heap_live 都不变，
        // 资源只是换了个主人。
    }

    // ---- 拷贝赋值：copy-and-swap，天然异常安全 + 自赋值安全 ----
    String& operator=(const String& o) {
        ++g_copy_assign;
        String tmp(o);          // 先做一份副本；若此处抛异常，*this 完全没被碰过
        Swap(tmp);              // 只做不抛异常的操作
        return *this;           // tmp 析构，释放旧资源
    }

    // ---- 移动赋值 ----
    String& operator=(String&& o) noexcept {
        ++g_move_assign;
        if (this != &o) {       // 自赋值检查：own = std::move(own) 时必须安全
            Release();
            data_ = o.data_;
            size_ = o.size_;
            o.data_ = nullptr;
            o.size_ = 0;
        }
        return *this;
    }

    // 名字与内容互换，异常安全的核心工具：只交换裸指针和长度，绝不抛异常
    void Swap(String& o) noexcept {
        std::swap(data_, o.data_);
        std::swap(size_, o.size_);
    }

    std::size_t size() const { return size_; }
    const char* c_str() const { return data_ ? data_ : ""; }

private:
    void Release() noexcept {
        if (data_ != nullptr) {
            delete[] data_;
            ++g_heap_frees;
            --g_heap_live;
        }
        data_ = nullptr;
        size_ = 0;
    }

    char* data_;
    std::size_t size_;
};

// ===========================================================================
// 3. 编译器不再自动生成移动操作的两个典型场景
// ===========================================================================
// 对应到本文件的 String：它显式声明了析构函数、拷贝构造、拷贝赋值，
// 所以编译器不会再生成移动操作 —— 但本文件自己补写了移动构造与移动赋值，
// 因此 String 仍然能移动。如果当初漏写，String b(std::move(a)) 会变成一次昂贵的深拷贝。
class HasDestructorOnly {
public:
    HasDestructorOnly() = default;
    ~HasDestructorOnly() {}                  // 只要用户声明了析构函数……
    // ……编译器就不再隐式生成移动构造 / 移动赋值（拷贝操作仍会生成）。
    // 于是「看起来能移动」的代码实际上退化成拷贝 —— 静默的性能损失，编译期没有警告。
    // 这里放一个 std::string 成员，否则成员的拷贝是平凡的、隐式拷贝构造正好也是 noexcept，
    // 就观察不到「退化」这件事了（详见下面的 concept 断言）。
    std::string tag = "payload";
};

class DeletedCopyBecauseMove {
public:
    DeletedCopyBecauseMove() = default;
    // 只声明了移动赋值，没有声明移动构造：拷贝构造被隐式定义为删除，
    // 移动构造也不生成 => 这个类型既不能拷贝也不能移动。
    DeletedCopyBecauseMove& operator=(DeletedCopyBecauseMove&&) noexcept { return *this; }
};

// 编译期证据：把「隐式生成规则」变成可检查的断言
// 陷阱一：is_move_constructible_v 问的是「能不能用右值构造」，不是「有没有移动构造」。
//   拷贝构造的参数是 const T&，能绑定右值，所以「只能拷贝」的类型在这里也是 true！
//   同理，is_nothrow_move_constructible 也会被 noexcept 的拷贝构造骗过（实测 MSVC 这里为 true）。
static_assert(std::is_copy_constructible_v<HasDestructorOnly>, "有析构函数不影响拷贝构造的生成");
static_assert(std::is_move_constructible_v<HasDestructorOnly>,
              "is_move_constructible 只表示「能用右值构造」——这里其实是拷贝构造在干活");

// 陷阱二：真正可靠的判别法是用 concept 做重载决议，看「最优匹配」到底是哪一个。
template <typename T>
concept HasTrueMoveCtor = requires(T&& r) {
    // 最优匹配必须是 noexcept 的真移动构造；只有拷贝构造时这条不成立
    { T(static_cast<T&&>(r)) } noexcept;
};
// 反例校验：Plain 没有用户声明任何特殊成员函数，编译器隐式生成的移动构造是 noexcept 的，
// 所以它确实「真的能移动」——这说明上面的 concept 不是单纯在检查 noexcept。
struct PlainAggregate {
    int x;
    int y;
};
static_assert(HasTrueMoveCtor<PlainAggregate>, "隐式移动构造是 noexcept 的，PlainAggregate 真的能移动");
static_assert(!HasTrueMoveCtor<HasDestructorOnly>,
              "HasDestructorOnly 没有移动构造：std::move 会静默退化成拷贝，没有任何警告");
static_assert(HasTrueMoveCtor<String>, "String 自己写了 noexcept 移动构造，所以真的能移动");
// 陷阱三：声明了移动操作之后，拷贝操作会被定义为 deleted（这条会在编译期直接报错，属于好事）。
static_assert(!std::is_copy_constructible_v<DeletedCopyBecauseMove>, "声明了移动赋值 => 拷贝构造被删除");
static_assert(!std::is_move_constructible_v<DeletedCopyBecauseMove>, "没有移动构造 => 不可移动构造");

// ===========================================================================
// 5/6. 用于观察拷贝/移动/RVO 的小工具类型
// ===========================================================================
class Tracker {
public:
    static int copies;
    static int moves;
    static int ctors;

    explicit Tracker(int id) : id_(id) { ++ctors; }
    ~Tracker() = default;

    Tracker(const Tracker& o) : id_(o.id_) { ++copies; }
    Tracker(Tracker&& o) noexcept : id_(o.id_) { ++moves; o.id_ = -1; }
    Tracker& operator=(const Tracker&) = default;
    Tracker& operator=(Tracker&&) = default;

    int id() const { return id_; }

private:
    int id_;
};
int Tracker::copies = 0;
int Tracker::moves = 0;
int Tracker::ctors = 0;

void ResetTracker() {
    Tracker::copies = 0;
    Tracker::moves = 0;
    Tracker::ctors = 0;
}

// C++17 起「返回纯右值」是保证的拷贝消除：MakeTracker() 直接在调用方空间构造，
// 既不拷贝也不移动。
Tracker MakeTracker() {
    return Tracker(7);              // 不写 std::move！写 return std::move(...) 会适得其反
}

// NRVO（具名返回值优化）：把局部变量直接构造成调用方的对象。
#ifdef DEMO_NO_ELIDE
// 用 /DDEMO_NO_ELIDE 编译时，这条 return 会走一次移动构造（NRVO 被关掉了观察用）
#endif
Tracker MakeNamedTracker() {
    Tracker local(9);
    return local;                   // NRVO 生效时 0 次拷贝 0 次移动
}

// 返回参数（不是值）：标准允许优化，但多数实现（含 MSVC）不会对参数做 NRVO，
// 于是这里通常发生一次移动构造。
Tracker PassThrough(Tracker t) {
    return t;
}

// ===========================================================================
// 8. Rule of Zero：自己不写任何特殊成员函数
// ===========================================================================
class ZeroRuleWidget {
public:
    ZeroRuleWidget(std::string name, int id) : name_(std::move(name)), id_(id) {}

    // 不写析构 / 拷贝 / 移动：编译器生成的全都是正确的，
    // 因为 std::string 自己已经正确实现了它们。
    const std::string& name() const { return name_; }
    int id() const { return id_; }

private:
    std::string name_;   // 成员自己管理资源
    int id_;
};

static_assert(std::is_copy_constructible_v<ZeroRuleWidget>, "Rule of Zero 的类型天然可拷贝");
static_assert(std::is_move_constructible_v<ZeroRuleWidget>, "Rule of Zero 的类型天然可移动");
static_assert(std::is_nothrow_move_constructible_v<ZeroRuleWidget>, "string 的移动是 noexcept");

// ===========================================================================
// 演示入口
// ===========================================================================
void DemoThreeAndFive() {
    std::cout << "==== 1/2. Rule of Three 与 Rule of Five ====\n";
    ResetCounters();
    {
        String a("hello");
        std::cout << "    构造 a            : a = \"" << a.c_str() << "\", size = " << a.size() << "\n";
        String b(a);                       // 拷贝构造
        std::cout << "    String b(a)       : b = \"" << b.c_str() << "\"（深拷贝，两块独立内存）\n";
        String c(std::move(a));            // 移动构造
        std::cout << "    String c(move(a)) : c = \"" << c.c_str() << "\", a 变成空串 \"" << a.c_str()
                  << "\"（size = " << a.size() << "）\n";
        String d;
        d = b;                             // 拷贝赋值（copy-and-swap）
        std::cout << "    d = b             : d = \"" << d.c_str() << "\"\n";
        String e;
        e = std::move(c);                  // 移动赋值
        std::cout << "    e = move(c)       : e = \"" << e.c_str() << "\"\n";
        d = d;                             // 自赋值：copy-and-swap 让它天然安全
        std::cout << "    d = d（自赋值）   : 仍然正确 = \"" << d.c_str() << "\"\n";
        PrintCounters("离开作用域前");
        std::cout << "    堆分配总次数 g_heap_allocs = " << g_heap_allocs
                  << "（移动只换指针，不新增分配）\n";
    }
    PrintCounters("离开作用域后");
    PrintLine();
}

void DemoShallowCopyDisaster() {
    std::cout << "==== 4. 浅拷贝 => double free（只讲不跑） ====\n";
    std::cout << "    错误实现：String(const String& o) : data_(o.data_), size_(o.size_) {}\n";
    std::cout << "      a ---+\n";
    std::cout << "            +--> [同一个堆缓冲区]\n";
    std::cout << "      b ---+\n";
    std::cout << "    析构顺序：b 先 delete[]，a 再 delete[] 同一地址 => double free。\n";
    std::cout << "    现象：Debug 下 MSVC 报 heap corruption / CRT 断言；Release 下随机崩溃或静默数据损坏。\n";
    std::cout << "    本文件采用深拷贝，所以不会崩。若想看崩溃，把拷贝构造函数改成上面那行。\n";
    std::cout << "    判定标准：只要类里出现裸 new / delete / 文件描述符 / 锁，就先想拷贝语义。\n";
    PrintLine();
}

void DemoMoveIsJustACast() {
    std::cout << "==== 5. std::move 只是类型转换，不是「移动」本身 ====\n";
    std::string s1 = "abcdefghijklmnop";     // 长度超过 SSO 阈值，一定会走堆
    const char* before = s1.data();
    std::string s2 = std::move(s1);
    std::cout << "    s2 = \"" << s2 << "\", s1 = \"" << s1 << "\"（s1 被掏空，但对象本身仍合法）\n";
    std::cout << "    std::move(s1) 的静态类型是 std::string&&，它唯一做的事是把表达式变成右值，\n";
    std::cout << "    真正「搬东西」的是被选中的移动构造函数。\n";
    std::cout << "    std::move 对 const 对象无效：\n";
    const std::string cs = "const-string";
    std::string s3 = std::move(cs);          // const 右值只能匹配 const&，走拷贝
    std::cout << "      const string cs -> string s3 = std::move(cs)：cs 仍为 \"" << cs
              << "\"，实际发生的是拷贝。\n";
    std::cout << "    另一个常见误解：std::move 之后的对象「不该再读值」，但可以安全地重新赋值或析构。\n";
    // 用 sizeof / 类型断言把「右值引用是类型」这件事写下来
    static_assert(std::is_same_v<decltype(std::move(s1)), std::string&&>,
                  "std::move 的返回类型是右值引用");
    static_assert(!std::is_same_v<decltype((s1)), std::string&&>,
                  "具名变量 s1 本身永远是左值");
    (void)before;
    (void)s3;
    PrintLine();
}

void DemoRvo() {
    std::cout << "==== 6. RVO / NRVO 与「返回值不要写 std::move」 ====\n";
    ResetTracker();
    {
        Tracker t1 = MakeTracker();                     // 保证的拷贝消除
        std::cout << "    Tracker t1 = MakeTracker();   -> 构造 " << Tracker::ctors
                  << " 次, 拷贝 " << Tracker::copies << " 次, 移动 " << Tracker::moves << " 次\n";
    }
    std::cout << "      C++17 起，返回纯右值必须在调用方原地构造（guaranteed copy elision），是语言保证而非优化。\n";

    ResetTracker();
    {
        Tracker t2 = MakeNamedTracker();                // NRVO
        std::cout << "    Tracker t2 = MakeNamedTracker(); -> 构造 " << Tracker::ctors
                  << " 次, 拷贝 " << Tracker::copies << " 次, 移动 " << Tracker::moves << " 次\n";
    }
    std::cout << "      NRVO 是「允许的优化」，不是保证：本机 Debug 下 MSVC 依然做了 NRVO（0 拷贝 0 移动）。\n";
    std::cout << "      想观察「没有 NRVO 的样子」，可加 /Od /DDEMO_NO_ELIDE 再编译，会看到 1 次移动构造。\n";

    ResetTracker();
    {
        Tracker seed(3);
        Tracker t3 = PassThrough(seed);                 // 传值 + 返回参数
        std::cout << "    Tracker t3 = PassThrough(seed);  -> 构造 " << Tracker::ctors
                  << " 次, 拷贝 " << Tracker::copies << " 次, 移动 " << Tracker::moves << " 次\n";
    }
    std::cout << "      其中那 1 次拷贝发生在「实参 seed 拷贝给形参 t」，无法消除；\n";
    std::cout << "      返回时因为 Tracker 的移动构造是 noexcept，走的是移动而不是又一次拷贝。\n";
    std::cout << "    【错误写法】return std::move(local); —— 把一个右值引用返回出去，\n";
    std::cout << "      会阻止 NRVO，反而多一次移动，是典型的「负优化」。\n";
    PrintLine();
}

void DemoImplicitGeneration() {
    std::cout << "==== 3. 编译器什么时候不再生成特殊成员函数 ====\n";
    std::cout << "    规则速查（C++20）：\n";
    std::cout << "      - 声明了析构函数 / 拷贝构造 / 拷贝赋值 之一 => 不再隐式生成移动操作，\n";
    std::cout << "        这些「看起来是移动」的代码会静默退化成拷贝（性能陷阱，不报错）。\n";
    std::cout << "      - 声明了移动操作 => 拷贝构造与拷贝赋值被定义为 deleted（编译期报错，安全）。\n";
    HasDestructorOnly h;
    HasDestructorOnly h2(std::move(h));   // 这里其实是拷贝！编译器不会提醒
    DeletedCopyBecauseMove d;
    static_cast<void>(h2);
    static_cast<void>(d);
    std::cout << "    HasDestructorOnly h2(std::move(h)); 编译通过，但调用的是拷贝构造（is_move_constructible = "
              << std::boolalpha << std::is_move_constructible_v<HasDestructorOnly> << "）。\n";
    std::cout << "    static_assert 已经把这两条规则固定成编译期证据。\n";
    PrintLine();
}

void DemoCopyAndSwap() {
    std::cout << "==== 7. copy-and-swap：异常安全 + 自赋值安全 ====\n";
    std::cout << "    先构造副本 tmp（在这里可能抛异常），此时 *this 尚未被修改；\n";
    std::cout << "    然后用 noexcept 的 swap 交换内容。要么完全成功，要么完全没变（强异常保证）。\n";
    std::cout << "    自赋值 a = a：先做副本、再 swap，结果依然是原值，不需要额外的 this != &o 判断。\n";
    std::cout << "    代价：多一次拷贝；对性能敏感的热路径可以改写成「先分配新资源再析构旧的」。\n";
    std::cout << "    注意：移动赋值里必须自己写 this != &o 检查，因为移动不经过 copy-and-swap。\n";
    PrintLine();
}

void DemoRuleOfZero() {
    std::cout << "==== 8. Rule of Zero：优先一个都不写 ====\n";
    ZeroRuleWidget w("widget", 42);
    ZeroRuleWidget v = w;                 // 编译器生成的拷贝构造正确
    ZeroRuleWidget u = std::move(w);      // 编译器生成的移动构造正确（noexcept）
    std::cout << "    v = (" << v.name() << ", " << v.id() << ")\n";
    std::cout << "    u = (" << u.name() << ", " << u.id() << ")；被移动后的 w.name() = \""
              << w.name() << "\"\n";
    std::cout << "    结论：类里出现 vector / string / unique_ptr / shared_ptr 等「自己会管资源」的成员时，\n";
    std::cout << "    一个特殊成员函数都不要写（写了反而容易漏掉或写错）；\n";
    std::cout << "    只有当类直接持有裸资源（裸指针、句柄、锁）时，才需要 Rule of Three / Five。\n";
    std::cout << "    工程顺序建议：优先 Rule of Zero -> 不行就 Rule of Five -> 用 copy-and-swap 写赋值。\n";
    PrintLine();
}

}  // namespace

int main() {
    std::cout << "################ 02 拷贝 / 移动语义 ################\n\n";
    DemoThreeAndFive();
    DemoShallowCopyDisaster();
    DemoMoveIsJustACast();
    DemoRvo();
    DemoImplicitGeneration();
    DemoCopyAndSwap();
    DemoRuleOfZero();
    std::cout << "存活资源对象（String 的堆缓冲）计数 g_heap_live = " << g_heap_live
              << "，累计分配 g_heap_allocs = " << g_heap_allocs
              << "，累计释放 g_heap_frees = " << g_heap_frees << "\n";
    std::cout << "（分配次数等于释放次数、存活数回到 0，说明既没有泄漏也没有 double free）\n";
    if (g_heap_live == 0 && g_heap_allocs == g_heap_frees) {
        std::cout << "    自检通过：所有堆缓冲都被释放且只释放了一次。\n";
    } else {
        std::cout << "    自检失败：资源计数不平衡。\n";
    }
    return 0;
}
