// ============================================================================
// 01_class_basics.cpp
// 演示主题：
//   1. class 与 struct 的唯一差别是默认访问权限
//   2. 成员函数与 this 指针
//   3. 访问控制（public / protected / private）与「封装」的真实含义
//   4. 构造函数 / 析构函数：调用时机
//   5. 初始化列表 vs 构造函数体内赋值（本质区别：直接构造 vs 先默认构造再赋值）
//   6. const 成员和引用成员只能用初始化列表
//   7. 成员初始化顺序由「声明顺序」决定，而不是初始化列表的书写顺序（经典坑）
//   8. explicit 防止隐式转换
//   9. mutable
//  10. = default / = delete
//
// 关键结论：
//   构造函数体内的 "=" 是赋值，不是初始化；
//   初始化列表是「直接构造」，少一次默认构造 + 一次赋值的开销。
//   成员永远按声明顺序初始化，初始化列表的顺序不影响结果，写反了只会误导读者。
// ============================================================================

#include <cstddef>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

namespace {

// ---------------------------------------------------------------------------
// 全局计数：把「构造 / 析构发生了几次」变成可见的数字
// ---------------------------------------------------------------------------
int g_ctor = 0;
int g_dtor = 0;

void PrintLine() { std::cout << "--------------------------------------------------\n"; }

// ===========================================================================
// 1. class 与 struct：唯一差别 = 默认访问权限
// ===========================================================================
struct PointStruct {       // 默认 public
    int x;                 // 这里不写 public 也是 public
    int y;
};

class PointClass {         // 默认 private
    int x_;                // 这里不写 private 也是 private
    int y_;

public:
    PointClass(int x, int y) : x_(x), y_(y) {}
    int X() const { return x_; }
    int Y() const { return y_; }
};

// 用 static_assert 把「默认访问权限」这件事变成编译期证据：
//   std::is_standard_layout 对本例不区分，改用更直接的方式——
//   如果 PointStruct 的成员默认是 private，下面的聚合初始化就无法编译。
constexpr PointStruct kOrigin{0, 0};                       // 聚合初始化成功 => 成员是 public
static_assert(kOrigin.x == 0, "struct 的成员默认是 public");

// ===========================================================================
// 2. 成员函数与 this 指针
// ===========================================================================
class Counter {
public:
    // 成员函数都有一个隐藏参数：this 指针
    void Add(int n) {
        // 下面两种写法完全等价，this->value_ 更明确地表达了「这是本对象的成员」
        value_ += n;
        this->calls_ += 1;
    }

    // 返回 *this 的引用可以支持链式调用：c.Add(1).Add(2)
    Counter& AddChain(int n) {
        value_ += n;
        return *this;
    }

    // SetSelf 用来证明 this 就是「本对象的地址」
    const Counter* Self() const { return this; }

    int value() const { return value_; }
    int calls() const { return calls_; }

private:
    int value_ = 0;
    int calls_ = 0;
};

// ===========================================================================
// 3. 访问控制 + 封装：类的职责是维护「不变量」
// ===========================================================================
class BankAccount {
public:
    BankAccount(std::string owner, double balance) : owner_(std::move(owner)), balance_(balance) {
        // 构造函数负责建立不变量：余额不能是负数
        if (balance_ < 0.0) {
            balance_ = 0.0;   // 真实项目里这里更应该抛异常，见 03_raii_and_resource.cpp
        }
    }

    // 只读接口：const 成员函数承诺「不修改对象状态」
    double balance() const { return balance_; }
    const std::string& owner() const { return owner_; }

    // 写接口：唯一能修改余额的入口，所有规则集中在这里
    bool Withdraw(double amount) {
        if (amount <= 0.0 || amount > balance_) {
            return false;                     // 拒绝非法操作，对象始终处于合法状态
        }
        balance_ -= amount;
        return true;
    }

    void Deposit(double amount) {
        if (amount > 0.0) {
            balance_ += amount;
        }
    }

private:
    std::string owner_;
    double balance_;      // 不变量：balance_ >= 0
};

// ===========================================================================
// 4/5. 构造函数体内赋值 vs 初始化列表
// ===========================================================================
class Named {
public:
    // 【错误写法演示】先默认构造 name_，再在函数体里赋值 => 1 次默认构造 + 1 次赋值
    // 取消注释并配合下面的 NamedInit 对比计数即可看到差异。
    // Named(const std::string& n) {
    //     name_ = n;                       // 这是赋值，不是初始化
    // }

    // 【正确写法】初始化列表：name_ 由 n 直接拷贝构造，只有 1 次构造
    // 注意：初始化列表里的 ++g_ctor 给 id_ 用，函数体里再自增一次表示「又构造了一个对象」。
    explicit Named(const std::string& n) : name_(n), id_(g_ctor + 1) { ++g_ctor; }

    ~Named() { ++g_dtor; }

    const std::string& name() const { return name_; }
    int id() const { return id_; }

private:
    std::string name_;   // 声明顺序：name_ 先，id_ 后
    int id_;
};

// ===========================================================================
// 6. const 成员与引用成员：只能用初始化列表
// ===========================================================================
class ConstAndRefMember {
public:
    // const 成员和引用成员没有「默认构造」这一步，所以必须在初始化列表里直接构造。
    // 【错误写法】下面的写法无法编译：
    //   ConstAndRefMember(int v, int& ext) { const_value_ = v; ext_ = ext; }
    //   编译器报错大意：error C2789: 必须是初始化一个 const 限定的对象
    //                  error C2530: 引用必须初始化
    ConstAndRefMember(int v, int& ext) : const_value_(v), ext_(ext) {}

    int const_value() const { return const_value_; }
    int& external() const { return ext_; }   // 引用成员本身是 const 的（不能再绑定别的对象），但它指向的对象可改

private:
    const int const_value_;   // 只能在构造时确定，之后不可改
    int& ext_;                // 必须绑定到某个已存在的对象，且终身不再改变绑定
};

// ===========================================================================
// 7. 成员初始化顺序由「声明顺序」决定（经典坑）
// ===========================================================================
class OrderTrap {
public:
    // 初始化列表故意写成 second_ 在前、first_ 在后，看起来 second_ 先算。
    // 实际上：first_ 先初始化（因为它在类里先声明），second_ = first_ + 1 = 101。
    // 若反过来依赖 second_，就会读到未初始化的值（UB）。
    OrderTrap() : second_(first_ + 1), first_(100) {
        std::cout << "    初始化列表书写顺序: second_, first_\n";
        std::cout << "    实际结果: first_ = " << first_ << ", second_ = " << second_ << "\n";
    }

    int first() const { return first_; }
    int second() const { return second_; }

private:
    int first_;    // 先声明 => 先初始化
    int second_;   // 后声明 => 后初始化
};

// ===========================================================================
// 8. explicit：阻止「隐式转换」
// ===========================================================================
class Meters {
public:
    // 单参数构造函数不加 explicit 时，编译器允许 double 隐式转换成 Meters，
    // 于是 printMeters(3.5) 这种「看起来传错了类型」的代码也能编译通过。
    // 【错误写法演示】若去掉 explicit，下面这条会编译失败（好）：
    //   printMeters(3.5);   // double 隐式变成 Meters
    explicit Meters(double v) : value_(v) {}

    double value() const { return value_; }

private:
    double value_;
};

void PrintMeters(const Meters& m) { std::cout << "    length = " << m.value() << " m\n"; }

// ===========================================================================
// 9. mutable：const 成员函数里也能改的成员
// ===========================================================================
class CachedValue {
public:
    explicit CachedValue(int base) : base_(base) {}

    // 逻辑上「查询」不该改变对象状态，所以标记 const；
    // 但缓存是内部实现细节，允许在 const 函数里改动 => 用 mutable
    int Compute() const {
        if (!cache_valid_) {
            cache_ = base_ * 2;      // 没有 mutable 的话这里编译不过
            cache_valid_ = true;
            ++compute_count_;        // mutable 计数器，用来观察缓存是否真的生效
        }
        return cache_;
    }

    int compute_count() const { return compute_count_; }

private:
    int base_;
    mutable int cache_ = 0;              // 可变的缓存
    mutable bool cache_valid_ = false;
    mutable int compute_count_ = 0;
};

// ===========================================================================
// 10. = default / = delete
// ===========================================================================
class NonCopyable {
public:
    NonCopyable() = default;                                  // 要编译器生成默认构造
    explicit NonCopyable(int v) : v_(v) {}

    // 明确删除：这类对象代表独占资源（文件句柄、锁、socket），复制语义本身就是错的
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;

    // 但移动是合理的：把资源所有权转交出去
    NonCopyable(NonCopyable&&) noexcept = default;
    NonCopyable& operator=(NonCopyable&&) noexcept = default;

    int value() const { return v_; }

private:
    int v_ = 0;
};

// 编译期证据：这些「语言规则」可以写成断言，接口约束被固化下来
static_assert(!std::is_copy_constructible_v<NonCopyable>, "NonCopyable 必须禁止拷贝");
static_assert(std::is_move_constructible_v<NonCopyable>, "NonCopyable 必须允许移动");
static_assert(std::is_copy_constructible_v<BankAccount>, "BankAccount 的成员都可拷贝，所以它可拷贝");

// ===========================================================================
// 演示入口
// ===========================================================================
void DemoClassVsStruct() {
    std::cout << "==== 1. class 与 struct：唯一差别是默认访问权限 ====\n";
    PointStruct ps{1, 2};
    PointClass pc{1, 2};
    std::cout << "    struct PointStruct{int x; int y;};  直接访问 ps.x = " << ps.x << "（默认 public）\n";
    std::cout << "    class  PointClass{int x_; int y_;}; 只能通过 pc.X() = " << pc.X()
              << "（默认 private）\n";
    std::cout << "    sizeof(PointStruct) = " << sizeof(PointStruct)
              << ", sizeof(PointClass) = " << sizeof(PointClass)
              << "（都是 2 个 int，访问权限不占空间）\n";
    PrintLine();
}

void DemoThisPointer() {
    std::cout << "==== 2. 成员函数与 this 指针 ====\n";
    Counter c;
    c.Add(5);
    c.AddChain(1).AddChain(2).AddChain(3);   // 链式调用靠返回 *this 的引用
    std::cout << "    value = " << c.value() << ", calls = " << c.calls() << "\n";
    std::cout << "    &c        = " << static_cast<const void*>(&c) << "\n";
    std::cout << "    c.Self()  = " << static_cast<const void*>(c.Self())
              << "  <- 成员函数里的 this 就是本对象地址\n";
    std::cout << "    注意：this 是指针，*this 才是对象本身；返回引用才能链式调用。\n";
    PrintLine();
}

void DemoEncapsulation() {
    std::cout << "==== 3. 访问控制与不变量 ====\n";
    BankAccount acc("Alice", 100.0);
    std::cout << "    初始余额 = " << acc.balance() << "\n";
    std::cout << "    取款 30 成功? " << std::boolalpha << acc.Withdraw(30.0)
              << " -> 余额 " << acc.balance() << "\n";
    std::cout << "    取款 1000 成功? " << acc.Withdraw(1000.0)
              << " -> 余额 " << acc.balance() << "（被拒绝，不变量 balance_ >= 0 保住了）\n";
    std::cout << "    取款 -5 成功? " << acc.Withdraw(-5.0) << "（负数也被拒绝）\n";
    std::cout << "    工程含义：把 balance_ 设为 private，违反不变量的路径就不存在了。\n";
    PrintLine();
}

void DemoInitializerList() {
    std::cout << "==== 4/5. 初始化列表 vs 构造函数体内赋值 ====\n";
    std::cout << "    成员是「类类型」时，函数体内赋值会多出一次默认构造：\n";
    std::cout << "      Named(const std::string& n) { name_ = n; }  // 默认构造 + operator=\n";
    std::cout << "      Named(const std::string& n) : name_(n) {}   // 直接拷贝构造（本例采用）\n";
    const int before = g_ctor;
    {
        Named a("alpha");
        Named b("beta");
        std::cout << "    构造后 g_ctor 增量 = " << (g_ctor - before) << "（每个对象 1 次构造）\n";
    }
    std::cout << "    离开作用域后 g_dtor 增量 = " << g_dtor << "（构造与析构一一配对）\n";
    PrintLine();
}

void DemoConstAndRefMember() {
    std::cout << "==== 6. const 成员 / 引用成员只能用初始化列表 ====\n";
    int external = 42;
    ConstAndRefMember obj(7, external);
    std::cout << "    const 成员 const_value_ = " << obj.const_value() << "（只能构造时确定）\n";
    obj.external() = 99;   // 引用成员仍可改它绑定的那个外部对象
    std::cout << "    通过引用成员改写外部变量: external = " << external << "\n";
    std::cout << "    取消注释构造函数体里对 const_value_ / ext_ 的赋值，编译器会直接报错。\n";
    PrintLine();
}

void DemoInitOrder() {
    std::cout << "==== 7. 成员初始化顺序由声明顺序决定 ====\n";
    OrderTrap t;
    std::cout << "    结论：初始化列表把 second_ 写在前面也没用，先声明的 first_ 先初始化。\n";
    std::cout << "    正确做法：让初始化列表顺序与声明顺序一致，并在 /W4 下把顺序警告当错误看。\n";
    PrintLine();
}

void DemoExplicit() {
    std::cout << "==== 8. explicit 防止隐式转换 ====\n";
    PrintMeters(Meters(3.5));                 // 显式构造，OK
    // PrintMeters(3.5);                      // 【错误写法】去掉 explicit 后这条也能编译，隐患极大
    // Meters m = 3.5;                        // 【错误写法】同上：听起来就不该成立的隐式转换
    std::cout << "    Meters 的单参构造标了 explicit，所以 PrintMeters(3.5) 现在编译不过。\n";
    std::cout << "    工程习惯：所有单参数构造函数默认加 explicit，除非确实要做隐式转换。\n";
    PrintLine();
}

void DemoMutable() {
    std::cout << "==== 9. mutable：const 函数里的可变成员 ====\n";
    const CachedValue cv(21);                 // const 对象，只能调用 const 成员函数
    std::cout << "    第一次 Compute() = " << cv.Compute() << "\n";
    std::cout << "    第二次 Compute() = " << cv.Compute() << "\n";
    std::cout << "    实际计算次数 = " << cv.compute_count() << "（缓存生效，第 2 次没重算）\n";
    std::cout << "    mutable 的代价：const 函数不再保证「字节完全没变」，只保证逻辑状态没变。\n";
    PrintLine();
}

void DemoDefaultDelete() {
    std::cout << "==== 10. = default / = delete ====\n";
    NonCopyable a(5);
    NonCopyable b(std::move(a));
    std::cout << "    移动后 b.value() = " << b.value() << "\n";
    // NonCopyable c = b;                     // 【错误写法】编译错误：
    //   error C2280: 尝试引用已删除的函数
    std::cout << "    NonCopyable c = b; 会报 error C2280（尝试引用已删除的函数），错误发生在编译期。\n";
    std::cout << "    static_assert(!is_copy_constructible_v<NonCopyable>) 已在编译期验证这一点。\n";
    PrintLine();
}

}  // namespace

int main() {
    std::cout << "################ 01 class 基础 ################\n\n";
    DemoClassVsStruct();
    DemoThisPointer();
    DemoEncapsulation();
    DemoInitializerList();
    DemoConstAndRefMember();
    DemoInitOrder();
    DemoExplicit();
    DemoMutable();
    DemoDefaultDelete();
    std::cout << "总构造次数 g_ctor = " << g_ctor << ", 总析构次数 g_dtor = " << g_dtor << "\n";
    std::cout << "（两者不必相等：CachedValue 等类型没有参与计数，只统计了 Named）\n";
    return 0;
}
