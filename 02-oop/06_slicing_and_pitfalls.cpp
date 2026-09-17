// ============================================================================
// 06_slicing_and_pitfalls.cpp
// 演示主题：
//   1. 对象切片（object slicing）：按值传基类会丢掉派生部分
//   2. 正确姿势：用 const Base& / Base* 传参
//   3. vector<Base> 装派生类的陷阱，以及 vector<unique_ptr<Base>> 的写法
//   4. 构造 / 析构函数中调用虚函数不会发生动态绑定（及其原因：vptr 分阶段设置）
//   5. 在构造函数里调用虚函数，拿到的是「还没构造完」的对象
//   6. 隐藏（hiding）vs 覆盖（overriding）：忘记 virtual / override 会被静默隐藏
//
// 关键结论：
//   多态只在「通过指针或引用」时生效；一旦按值拷贝，派生部分就被切掉了。
//   构造/析构期间对象处于「半成品」状态，此时虚函数机制故意退化为静态绑定。
// ============================================================================

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>

namespace {

void PrintLine() { std::cout << "--------------------------------------------------\n"; }

// ===========================================================================
// 公共类型：一个基类 + 一个带额外状态的派生类
// ===========================================================================
class Account {
public:
    Account(std::string owner, double balance)
        : owner_(std::move(owner)), balance_(balance) {}
    virtual ~Account() = default;

    virtual std::string Type() const { return "基础账户"; }
    virtual double MonthlyFee() const { return 0.0; }

    // 按值传参时，只有这些「基类自己的数据」会被复制过去
    const std::string& owner() const { return owner_; }
    double balance() const { return balance_; }

private:
    std::string owner_;
    double balance_;
};

class PremiumAccount : public Account {
public:
    PremiumAccount(std::string owner, double balance, double cashback_rate)
        : Account(std::move(owner), balance), cashback_rate_(cashback_rate) {}

    std::string Type() const override { return "尊享账户"; }
    double MonthlyFee() const override { return 0.0; }          // 免月费，是尊享账户的卖点

    double CashbackRate() const { return cashback_rate_; }      // 派生类特有

private:
    double cashback_rate_;
};

// 编译期证据：派生类就是比基类大 —— 那些多出来的字节正是被切片切掉的东西
static_assert(sizeof(PremiumAccount) > sizeof(Account), "派生类对象比基类对象大");
static_assert(sizeof(PremiumAccount) - sizeof(Account) >= sizeof(double),
              "至少多出一个 double 的成员（还有可能的对齐填充）");

// ===========================================================================
// 1/2. 按值传参 => 切片
// ===========================================================================
// 【错误写法】参数按值接收基类：实参的派生部分在拷贝时被丢弃
double FeeByValue(Account acc) {
    return acc.MonthlyFee();          // 这里调用的是 Account::MonthlyFee（静态绑定在切片后的对象上）
}

// 【正确写法】const 引用：不发生拷贝，虚函数照常动态绑定
double FeeByReference(const Account& acc) {
    return acc.MonthlyFee();
}

// 正确写法之二：指针
double FeeByPointer(const Account* acc) {
    return acc == nullptr ? 0.0 : acc->MonthlyFee();
}

// ===========================================================================
// 4/5. 构造 / 析构期间调用虚函数
// ===========================================================================
class BaseWithVirtualCall {
public:
    BaseWithVirtualCall() {
        std::cout << "    BaseWithVirtualCall 构造函数里调用 Describe()\n";
        // 危险点一：此时对象还在「基类阶段」，动态类型就是 BaseWithVirtualCall
        Describe();
        // 危险点二：typeid 也看不到派生类
        std::cout << "      typeid(*this).name() = " << typeid(*this).name()
                  << "  <- 不是派生类\n";
        // 危险点三：dynamic_cast 也认为「我就是基类」
        std::cout << "      dynamic_cast<BaseWithVirtualCall*>(this) 有效? "
                  << (dynamic_cast<BaseWithVirtualCall*>(this) != nullptr)
                  << "（但在基类构造期间，任何向派生类的 downcast 都不可能成功）\n";
    }
    virtual ~BaseWithVirtualCall() {
        std::cout << "    BaseWithVirtualCall 析构函数里调用 Describe()\n";
        Describe();
    }

    virtual void Describe() const {
        std::cout << "      [BaseWithVirtualCall::Describe] 基类版本\n";
    }
};

class DerivedWithVirtualCall : public BaseWithVirtualCall {
public:
    DerivedWithVirtualCall() : payload_(new int[32]) {
        std::cout << "    DerivedWithVirtualCall 构造函数开始（基类已构造完，派生成员刚初始化）\n";
        // 到这一步，在构造函数体内调用虚函数【才会】走到派生类版本
        Describe();
    }
    ~DerivedWithVirtualCall() override {
        std::cout << "    DerivedWithVirtualCall 析构函数（派生部分还完整）\n";
        Describe();
        delete[] payload_;
    }

    void Describe() const override {
        std::cout << "      [DerivedWithVirtualCall::Describe] 派生类版本\n";
    }

private:
    int* payload_;
};

// 基类构造函数里调用「非纯虚但派生类依赖成员」的虚函数 => 派生成员还没初始化
class FragileBase {
public:
    // 【反例】基类构造函数里调用虚函数：它只会走到基类自己的实现，
    // 如果它去读「派生类才有的成员」，读到的就是尚未初始化的值。
    FragileBase() {
        std::cout << "    FragileBase 构造中，调用 DerivedValue() 得到 "
                  << DerivedValue() << "（派生类成员【尚未】初始化）\n";
    }
    virtual ~FragileBase() = default;
    virtual int DerivedValue() const { return -1; }        // 基类版本：-1，用来暴露「调错了」
};

class FragileDerived : public FragileBase {
public:
    FragileDerived() : value_(99) {
        std::cout << "    FragileDerived 构造完成后，DerivedValue() = " << DerivedValue() << "\n";
    }
    int DerivedValue() const override { return value_; }

private:
    int value_;
};

// ===========================================================================
// 6. 隐藏 vs 覆盖
// ===========================================================================
class Document {
public:
    virtual ~Document() = default;

    // 注意参数类型：int
    virtual void Print(int copies) const {
        std::cout << "      [Document::Print(int)] 打印 " << copies << " 份\n";
    }
    void Save() const { std::cout << "      [Document::Save] 已保存\n"; }
};

class Report : public Document {
public:
    // 这个是正确的覆盖：签名一致 + override
    void Print(int copies) const override {
        std::cout << "      [Report::Print(int)] 正确覆盖，打印 " << copies << " 份\n";
    }

    // 【错误写法演示】把参数类型写成 double：
    //   void Print(double copies) const { ... }
    // 这不是覆盖，而是「隐藏」。编译器【不会】报错，多态会静默失效：
    //   通过 Document& 调用时，永远走 Document::Print(int)；
    //   通过 Report 对象直接调用时，才会选出 Report::Print(double)。
    // 只要在它后面写上 override，编译器立刻报：
    //   error C3668: "Report::Print": 带有重写说明符 "override" 的方法没有重写任何基类方法
    void PrintAsDouble(double copies) const {
        std::cout << "      [Report::PrintAsDouble(double)] 打印 " << copies
                  << " 份（注意：这个函数不是覆盖）\n";
    }

    // 把基类的 Save 藏起来（同名不同签名），并引入一个新的重载
    void Save(int backup_slots) const {
        std::cout << "      [Report::Save(int)] 备份槽位 = " << backup_slots << "\n";
    }
    using Document::Save;                              // 用 using 把基类版本拉回来
};

// ===========================================================================
// 演示入口
// ===========================================================================
void DemoSlicing() {
    std::cout << "==== 1/2. 对象切片（object slicing） ====\n";
    PremiumAccount premium("Alice", 1000.0, 0.05);
    std::cout << "    premium.Type() = " << premium.Type()
              << ", CashbackRate() = " << premium.CashbackRate() << "\n";
    std::cout << "    sizeof(Account) = " << sizeof(Account)
              << ", sizeof(PremiumAccount) = " << sizeof(PremiumAccount) << "\n";

    std::cout << "    --- 错误写法：按值传参 ---\n";
    const double fee_by_value = FeeByValue(premium);         // 发生切片！
    std::cout << "      FeeByValue(premium) 里 Type() 会变成 \"基础账户\"，因为 acc 已经是 Account 对象；\n";
    std::cout << "      返回值 = " << fee_by_value
              << "（巧合相同，因为本例两者月费都是 0，但类型信息已经丢了）\n";
    std::cout << "      更明显的证据：\n";
    Account sliced = premium;                                // 拷贝构造，只拷基类部分
    std::cout << "        Account sliced = premium;\n";
    std::cout << "        sliced.Type()       = " << sliced.Type() << "  <- 变成基类\n";
    std::cout << "        premium.Type()      = " << premium.Type() << "  <- 原对象不受影响\n";
    std::cout << "        sliced 的 CashbackRate() 已经不存在了 —— 那 8 个字节根本没被复制。\n";
    // sliced.CashbackRate();                                // 【错误写法】error C2039: 不是 Account 的成员

    std::cout << "    --- 正确写法：const 引用 / 指针 ---\n";
    std::cout << "      FeeByReference(premium) = " << FeeByReference(premium)
              << "，Type() = " << static_cast<const Account&>(premium).Type() << "\n";
    std::cout << "      FeeByPointer(&premium)  = " << FeeByPointer(&premium) << "\n";
    std::cout << "      引用/指针不产生新对象，虚表指针仍是 PremiumAccount 的 => 多态保持完整。\n";

    std::cout << "    【错误写法演示】把对象塞进值语义容器：\n";
    std::cout << "      std::vector<Account> v; v.push_back(premium);   // 同样发生切片\n";
    std::vector<Account> boxed;
    boxed.push_back(premium);                                // 真的做一次，观察类型
    std::cout << "      实际执行后 v[0].Type() = " << boxed[0].Type()
              << "（已经退化成基类，且没有任何警告）\n";
    std::cout << "    工程教训：只要函数/容器「按值」接收多态基类，多态就到此为止。\n";
    std::cout << "    判定方法：看到形参是 Base（没有 & 或 *）就要警惕。\n";
    PrintLine();
}

void DemoSlicingInVector() {
    std::cout << "==== 3. vector<Base> 装派生类的陷阱 ====\n";

    std::cout << "    --- 错误写法：std::vector<Account> ---\n";
    std::vector<Account> wrong;
    wrong.reserve(2);                       // 说明一下：即使预留了空间也救不了切片
    wrong.push_back(Account("Bob", 10.0));
    PremiumAccount carol("Carol", 20.0, 0.08);
    std::cout << "      入容器之前：carol.Type() = " << carol.Type()
              << "，CashbackRate() = " << carol.CashbackRate() << "\n";
    wrong.push_back(carol);                 // 切片发生在这里（拷贝的是 Account 部分）
    std::cout << "      入容器之后逐项读出：\n";
    for (const Account& a : wrong) {
        std::cout << "        owner = " << a.owner() << ", Type() = " << a.Type() << "\n";
    }
    std::cout << "      两个元素都是 Account，第二个已经查不到尊享身份了；\n";
    std::cout << "      这里不会编译报错，也不会运行报错，只是【业务逻辑静默出错】。\n";

    std::cout << "    --- 另一种错误写法：std::vector<Account*> ---\n";
    std::cout << "      std::vector<Account*> v; v.push_back(new PremiumAccount(...));\n";
    std::cout << "      多态是对的，但谁负责 delete？容器析构不会释放指针 => 必须手工遍历删除，\n";
    std::cout << "      一旦中途异常或提前 return 就泄漏（这正是 03 章 RAII 要解决的问题）。\n";

    std::cout << "    --- 正确写法：std::vector<std::unique_ptr<Account>> ---\n";
    std::vector<std::unique_ptr<Account>> right;
    right.push_back(std::make_unique<Account>("Bob", 10.0));
    right.push_back(std::make_unique<PremiumAccount>("Carol", 20.0, 0.08));
    for (const auto& a : right) {
        std::cout << "      owner = " << a->owner() << ", Type() = " << a->Type()
                  << ", 月费 = " << a->MonthlyFee() << "\n";
    }
    std::cout << "      多态保持完整，而且离开作用域时 unique_ptr 自动释放（RAII），异常也安全。\n";
    std::cout << "    需要共享所有权时用 shared_ptr；只在函数内部借用时用 const Account& 或 Account*。\n";
    PrintLine();
}

void DemoVirtualInCtorDtor() {
    std::cout << "==== 4/5. 构造 / 析构期间调用虚函数 ====\n";
    std::cout << "    --- 建立一个 DerivedWithVirtualCall ---\n";
    {
        DerivedWithVirtualCall obj;
        std::cout << "    对象构造完成后，正常调用：\n";
        obj.Describe();
        std::cout << "    离开作用域，开始析构：\n";
    }
    std::cout << "    观察结论（顺序即真相）：\n";
    std::cout << "      1. 基类构造期间调用虚函数 -> 走【基类】版本；\n";
    std::cout << "      2. 基类构造函数执行完、派生成员初始化完之后，在【派生类构造函数体内】\n";
    std::cout << "         调用虚函数 -> 走【派生类】版本；\n";
    std::cout << "      3. 派生类析构函数体内调用虚函数 -> 走【派生类】版本（派生部分还完整）；\n";
    std::cout << "      4. 进入基类析构阶段后再调用虚函数 -> 又退回【基类】版本。\n";
    std::cout << "    原因（实现机制）：构造函数依次执行 基类 -> 成员 -> 派生类 body，\n";
    std::cout << "      每进入一层，编译器就把 vptr 改成【当前这一层】的虚表；\n";
    std::cout << "      析构时反过来，vptr 逐层退回基类。于是「当前层」永远看不到更派生的实现。\n";
    std::cout << "    标准为什么这么规定：如果基类构造里能调到派生类实现，\n";
    std::cout << "      那个实现就可能访问【还没初始化】的派生成员 => 未定义行为。\n";
    std::cout << "      语言选择了「安全但不直观」：退化为静态绑定。\n";

    std::cout << "    --- 「还没构造完」的具体后果 ---\n";
    FragileDerived fd;
    std::cout << "    基类构造时调用 DerivedValue() 得到的是基类版本（-1），\n";
    std::cout << "    只有等派生部分构造完成，它才会返回派生类的 99。\n";
    std::cout << "    工程禁令：\n";
    std::cout << "      - 构造函数 / 析构函数里不要调用虚函数（除非它不依赖任何派生状态）；\n";
    std::cout << "      - 需要「对象构造完成后做初始化」=> 提供显式的 Init()/Start()，\n";
    std::cout << "        或者用工厂函数在对象完全构造后再调用它；\n";
    std::cout << "      - 纯虚函数更危险：在构造/析构中直接调用它会调用到未定义的纯虚实现，\n";
    std::cout << "        运行时报 \"pure virtual function call\" 并立即终止进程（R6025）。\n";
    PrintLine();
}

void DemoHidingVsOverriding() {
    std::cout << "==== 6. 隐藏（hiding）vs 覆盖（overriding） ====\n";
    Report rep;
    Document& ref = rep;

    std::cout << "    rep.Print(3)  ->  ";
    rep.Print(3);                            // 静态类型 Report，精确匹配 Print(int)
    std::cout << "    ref.Print(3)  ->  ";
    ref.Print(3);                            // 动态绑定到 Report::Print(int)：正确的覆盖
    std::cout << "    ref.Save()    ->  ";
    ref.Save();
    std::cout << "    rep.Save(2)   ->  ";
    rep.Save(2);                             // 派生类自己的重载
    std::cout << "    rep.PrintAsDouble(2.5) ->  ";
    rep.PrintAsDouble(2.5);                  // 名字不同，所以不会被误认为覆盖

    std::cout << "    --- 关键实验：通过基类引用调用 ---\n";
    std::cout << "      ref 是 Document&，ref.Print(3) 一定调用 Document 声明的 Print(int)，\n";
    std::cout << "      而「参数写成 double 的 Print」只是【隐藏】了基类版本，不是覆盖，\n";
    std::cout << "      通过基类引用时它根本不会被考虑 —— 这正是「多态静默失效」的现场。\n";
    std::cout << "      更糟的是：如果派生类的签名「恰好」能匹配，编译器也可能选择隐藏版本，\n";
    std::cout << "      导致同一行代码在 ref.Print() 与 rep.Print() 下行为完全不同。\n";
    std::cout << "    --- 用 override 让编译器帮你抓错 ---\n";
    std::cout << "      给那个 double 版本加上 override：\n";
    std::cout << "      error C3668: \"Report::Print\": 带有重写说明符 \"override\" 的方法没有重写任何基类方法\n";
    std::cout << "    --- 三种隐蔽的「签名不一致」导致隐藏 ---\n";
    std::cout << "      1. 参数类型不同：Print(int) vs Print(double)          <- 本例\n";
    std::cout << "      2. 缺 const：Base::F() const vs Derived::F()\n";
    std::cout << "      3. 基类漏写 virtual：那么连「覆盖」的前提都不存在\n";
    std::cout << "    --- 与名字隐藏的关系 ---\n";
    std::cout << "      无论是否虚函数，派生类中的同名函数都会隐藏基类的所有同名重载；\n";
    std::cout << "      区别只在于：签名一致 + virtual 才能构成「覆盖」，\n";
    std::cout << "      而「隐藏」会让基类版本在派生类作用域里彻底不可见（用 using 才能拉回）。\n";
    PrintLine();
}

}  // namespace

int main() {
    std::cout << "################ 06 切片与多态陷阱 ################\n\n";
    DemoSlicing();
    DemoSlicingInVector();
    DemoVirtualInCtorDtor();
    DemoHidingVsOverriding();
    std::cout << "本章一句话总结：多态只走指针和引用；构造/析构期间没有多态；\n";
    std::cout << "签名不一致时 override 是你唯一能指望的报警器。\n";
    return 0;
}
