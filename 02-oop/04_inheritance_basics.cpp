// ============================================================================
// 04_inheritance_basics.cpp
// 演示主题：
//   1. public / protected / private 继承的真实语义（is-a vs is-implemented-in-terms-of）
//   2. 成员访问权限表：基类成员在派生类内部与外部分别能不能访问
//   3. 构造与析构顺序：基类先构造，派生类先析构
//   4. 派生类构造函数如何调用基类构造（以及默认构造的要求）
//   5. 名字隐藏（name hiding）与 using 声明引入基类重载
//   6. 多重继承与菱形继承问题（数据冗余 + 二义性）
//   7. 虚继承：只是最后手段（有开销，且语义复杂）
//
// 关键结论：
//   继承表达的是「派生类是一种基类」（is-a）；
//   如果只是想在实现里复用基类的代码，应该用「组合」或 private 继承，而不是 public 继承。
// ============================================================================

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

std::vector<std::string> g_log;     // 记录构造/析构顺序

void Reset() { g_log.clear(); }

void PrintLog(const char* title) {
    std::cout << "    " << title << ": ";
    for (std::size_t i = 0; i < g_log.size(); ++i) {
        std::cout << (i == 0 ? "" : " -> ") << g_log[i];
    }
    std::cout << "\n";
}

void PrintLine() { std::cout << "--------------------------------------------------\n"; }

// ===========================================================================
// 1/2. 访问权限：一张表说清楚
// ===========================================================================
class Base {
public:
    int pub = 1;                    // 谁都能访问

protected:
    int prot = 2;                   // 只有「自己 + 派生类」能访问

private:
    int priv = 3;                   // 只有自己（连派生类也不行）
};

// public 继承：保持基类成员的访问级别 => 表达 is-a
class PublicDerived : public Base {
public:
    // 派生类的成员函数里可以访问 public 与 protected，不能访问 private
    int SumInside() const { return pub + prot; }
    // int BadInside() const { return priv; }   // 【错误写法】error C2248: 无法访问 private 成员
};

// protected 继承：基类的 public/protected 成员在派生类里都变成 protected
class ProtectedDerived : protected Base {
public:
    int SumInside() const { return pub + prot; }
};

// private 继承：基类的 public/protected 成员在派生类里都变成 private
// 语义是「用基类的实现来实现自己」，而不是 is-a => 外部不能把它当 Base 用
class PrivateDerived : private Base {
public:
    int SumInside() const { return pub + prot; }
};

// ===========================================================================
// 3/4. 构造 / 析构顺序 + 如何调用基类构造
// ===========================================================================
class Engine {
public:
    Engine() {
        g_log.push_back("Engine()");
        std::cout << "    Engine 默认构造\n";
    }
    explicit Engine(int power) : power_(power) {
        g_log.push_back("Engine(int)");
        std::cout << "    Engine 带参构造, power = " << power_ << "\n";
    }
    ~Engine() {
        g_log.push_back("~Engine()");            // 析构也要记日志，否则看不出「顺序反过来」
        std::cout << "    Engine 析构\n";
    }

    int power() const { return power_; }

private:
    int power_ = 0;
};

class Car : public Engine {
public:
    // 派生类构造函数必须负责基类部分：不写就是调用基类的默认构造
    Car() { g_log.push_back("Car()"); }                       // 隐式调用 Engine()

    // 在初始化列表里显式调用基类构造函数（这是唯一的方式，不能在函数体里调）
    explicit Car(int power, std::string name)
        : Engine(power), name_(std::move(name)) {              // 基类先构造，成员后构造
        g_log.push_back("Car(int,string)");
    }

    // 派生类先析构：所以 ~Car() 的日志会出现在 ~Engine() 之前
    ~Car() { g_log.push_back("~Car()"); }

private:
    std::string name_ = "unnamed";
};

// 如果基类【没有】默认构造函数，派生类就必须在初始化列表里显式调用它，否则编译错误
class NoDefaultBase {
public:
    explicit NoDefaultBase(int v) : v_(v) {}
    int v() const { return v_; }

private:
    int v_;
};

class MustInitBase : public NoDefaultBase {
public:
    MustInitBase() : NoDefaultBase(42) {}     // 【错误写法】去掉这一行 => error C2512: 没有合适的默认构造函数
};

// ===========================================================================
// 5. 名字隐藏（name hiding）
// ===========================================================================
class Shape {
public:
    virtual ~Shape() = default;

    void Describe() const { std::cout << "    Shape::Describe() 无参版本\n"; }
    void Describe(const std::string& tag) const {
        std::cout << "    Shape::Describe(const string&) 重载版本, tag = " << tag << "\n";
    }

    virtual double Area() const { return 0.0; }
};

class Square : public Shape {
public:
    // 【坑】只要派生类里出现任何名为 Describe 的函数，基类所有 Describe 重载都被隐藏。
    // 这里只定义无参版本 => Shape::Describe(const string&) 在 Square 上不可见。
    void Describe() const { std::cout << "    Square::Describe() 派生类版本\n"; }

    // 用 using 把基类的重载「拉」进派生类的作用域，这才是正解
    using Shape::Describe;

    double Area() const override { return side_ * side_; }

private:
    double side_ = 2.0;
};

// ===========================================================================
// 6. 多重继承 + 菱形继承
// ===========================================================================
class PoweredThing {                  // 菱形顶点
public:
    explicit PoweredThing(int p) : power_(p) { g_log.push_back("PoweredThing(int)"); }
    int power() const { return power_; }

private:
    int power_;                       // 让 sizeof 有意义：非空基类
};

class LandVehicle : public PoweredThing {
public:
    explicit LandVehicle(int p) : PoweredThing(p) { g_log.push_back("LandVehicle(int)"); }
};

class WaterVehicle : public PoweredThing {
public:
    explicit WaterVehicle(int p) : PoweredThing(p) { g_log.push_back("WaterVehicle(int)"); }
};

// 菱形继承：Amphibious 里有两份 PoweredThing 子对象 => 数据冗余 + 访问二义性
class Amphibious : public LandVehicle, public WaterVehicle {
public:
    Amphibious(int p) : LandVehicle(p), WaterVehicle(p) {}
};

// 虚继承：PoweredThing 只保留一份 => 但最派生类必须自己初始化虚基类
class VLandVehicle : public virtual PoweredThing {
public:
    explicit VLandVehicle(int p) : PoweredThing(p) { g_log.push_back("VLandVehicle(int)"); }
};

class VWaterVehicle : public virtual PoweredThing {
public:
    explicit VWaterVehicle(int p) : PoweredThing(p) { g_log.push_back("VWaterVehicle(int)"); }
};

class VAmphibious : public VLandVehicle, public VWaterVehicle {
public:
    // 关键点：虚基类的构造函数由「最派生类」调用，中间类的调用会被忽略。
    // 这里必须显式初始化 PoweredThing，否则它只能被默认构造（而它没有默认构造 => 编译错误）。
    explicit VAmphibious(int p) : PoweredThing(p), VLandVehicle(p), VWaterVehicle(p) {}
};

// 编译期证据：菱形继承让对象「变大」，而虚继承把它压了回去
static_assert(sizeof(Amphibious) == 2 * sizeof(PoweredThing), "非虚菱形继承有两份 PoweredThing");
static_assert(sizeof(VAmphibious) > sizeof(PoweredThing), "虚继承要额外的虚基类偏移信息");
static_assert(!std::is_empty_v<PoweredThing>, "PoweredThing 非空，sizeof 才能反映子对象数量");

// ===========================================================================
// 演示入口
// ===========================================================================
void DemoInheritanceKinds() {
    std::cout << "==== 1/2. 三种继承方式的真实语义 ====\n";
    PublicDerived pub;
    std::cout << "    public 继承   ：外部 obj.pub = " << pub.pub << "（可访问，保持 public）\n";
    std::cout << "                    派生类内部 pub + prot = " << pub.SumInside() << "\n";
    ProtectedDerived prot;
    // 【错误写法】protected 继承后，外部访问 b.pub 会报 error C2248
    // std::cout << prot.pub;
    std::cout << "    protected 继承：外部不能访问 prot.pub（error C2248），派生类内部可用 = "
              << prot.SumInside() << "\n";
    PrivateDerived priv;
    // 【错误写法】private 继承后，外部访问 priv.pub 同样报 error C2248
    std::cout << "    private 继承  ：外部完全看不到基类接口（error C2248），内部可用 = "
              << priv.SumInside() << "\n";
    std::cout << "    访问权限表（基类成员 / 继承方式 -> 派生类内部的可见性）：\n";
    std::cout << "      +---------+---------+-----------+----------+\n";
    std::cout << "      | 基类成员 \\ 继承 | public  | protected | private  |\n";
    std::cout << "      +---------+---------+-----------+----------+\n";
    std::cout << "      | public         | public  | protected | private  |\n";
    std::cout << "      | protected      | protected | protected | private |\n";
    std::cout << "      | private        | 不可见  | 不可见    | 不可见   |\n";
    std::cout << "      +---------+---------+-----------+----------+\n";
    std::cout << "    另外还有两条规则：\n";
    std::cout << "      - 基类 private 成员永远对派生类不可见（想让派生类用就设 protected）；\n";
    std::cout << "      - 派生类可以用 public: using Base::member; 把被降低的可见性「提回来」。\n";
    std::cout << "    工程含义：public 继承 = is-a；private 继承 = 「用基类实现自己」（is-implemented-in-terms-of），\n";
    std::cout << "    后者在多数场景下应该直接用「组合」代替。\n";
    PrintLine();
}

void DemoConstructionOrder() {
    std::cout << "==== 3/4. 构造 / 析构顺序 ====\n";
    Reset();
    {
        std::cout << "    --- 建立 Car(200, \"beetle\") ---\n";
        Car c(200, "beetle");
        PrintLog("构造顺序");
        g_log.clear();                             // 清空后再观察析构，两条日志才能对比
        std::cout << "    --- 离开作用域 ---\n";
    }
    PrintLog("析构顺序");
    std::cout << "    结论：构造顺序是 基类 -> 成员 -> 自己的 body；析构完全反过来，\n";
    std::cout << "    所以构造日志读作 [Engine(int), Car(int,string)]，\n";
    std::cout << "        析构日志读作 [~Car(), ~Engine()]。\n";
    std::cout << "    为什么派生类先析构？因为派生类的析构函数可能还要使用基类提供的资源。\n";
    MustInitBase m;
    std::cout << "    基类没有默认构造时，派生类必须在初始化列表里显式调用：NoDefaultBase(42) -> "
              << m.v() << "\n";
    std::cout << "    【错误写法】把 MustInitBase() : NoDefaultBase(42) {} 里的初始化去掉，\n";
    std::cout << "      会报 error C2512: 没有合适的默认构造函数可用。\n";
    PrintLine();
}

void DemoNameHiding() {
    std::cout << "==== 5. 名字隐藏（name hiding） ====\n";
    Square sq;
    sq.Describe();                           // 调用 Square::Describe()
    sq.Describe("带参数的调用");             // 因为写了 using Shape::Describe; 才能编过
    std::cout << "    【错误写法】删掉 Square 里的 using Shape::Describe; 后，\n";
    std::cout << "      sq.Describe(\"x\") 会报 error C2664（无法将参数从 const char 转换为 ...）——\n";
    std::cout << "      注意报的是「参数不匹配」，而不是「找不到函数」，很容易看懵。\n";
    std::cout << "    规则：派生类里只要出现同名函数，基类的【所有】同名重载都被隐藏，与参数无关。\n";
    std::cout << "    正确做法：\n";
    std::cout << "      1. 需要保留基类重载 => 在派生类里写 using Base::func;（本例做法）；\n";
    std::cout << "      2. 希望「覆盖」虚函数 => 写 override，签名不一致时编译器会直接报错；\n";
    std::cout << "      3. 不想被隐藏 => 干脆取不同的名字（最省事的方案）。\n";
    std::cout << "    顺带：如果不写 using 也不小心屏蔽了虚函数，那就是 06 章要讲的「隐藏 vs 覆盖」。\n";
    PrintLine();
}

void DemoMultipleInheritance() {
    std::cout << "==== 6. 多重继承与菱形继承 ====\n";
    Reset();
    {
        Amphibious amp(100);
        std::cout << "    非虚菱形继承：sizeof(Amphibious) = " << sizeof(Amphibious)
                  << "，sizeof(PoweredThing) = " << sizeof(PoweredThing)
                  << " => 里面有两份 PoweredThing\n";
        PrintLog("构造顺序");
        std::cout << "    访问 amp.power() 会怎样？\n";
        // 【错误写法】下面这行会编译失败，因为 Amphibious 有两份 power：
        // std::cout << amp.power() << "\n";
        //   error C2385: 对 "power" 的访问不明确
        //   error C3861: "power": 找不到标识符
        std::cout << "      amp.power()  -> error C2385: 对 power 的访问不明确（两份基类子对象）\n";
        std::cout << "      只能显式指定：amp.LandVehicle::power() = " << amp.LandVehicle::power()
                  << "，amp.WaterVehicle::power() = " << amp.WaterVehicle::power() << "\n";
        std::cout << "      两个值还可能不一致 => 数据不一致的根源，必须避免。\n";
    }
    PrintLog("析构顺序");
    std::cout << "    构造顺序按基类声明顺序（LandVehicle 先于 WaterVehicle），与初始化列表顺序无关。\n";
    PrintLine();
}

void DemoVirtualInheritance() {
    std::cout << "==== 7. 虚继承：只是最后手段 ====\n";
    Reset();
    {
        VAmphibious vamp(100);
        std::cout << "    虚继承：sizeof(VAmphibious) = " << sizeof(VAmphibious)
                  << "（只有一份 PoweredThing，但要额外存虚基类偏移）\n";
        PrintLog("构造顺序");
        std::cout << "    vamp.power() = " << vamp.power()
                  << "（不再二义，因为只有一份基类子对象）\n";
        std::cout << "    虚继承的代价与约束：\n";
        std::cout << "      - 对象里有额外的虚基类偏移信息（本机 sizeof 从 " << sizeof(PoweredThing)
                  << " 涨到 " << sizeof(VAmphibious) << "）；\n";
        std::cout << "      - 虚基类的构造由「最派生类」负责，中间类写的初始化会被忽略，\n";
        std::cout << "        这让很多人对「谁初始化了什么」产生误判；\n";
        std::cout << "      - 派生类指针转虚基类指针需要运行期调整，不能像普通继承那样做静态偏移；\n";
        std::cout << "      - 只要在任意一层写了 virtual，后续所有派生都继承这个特性，改动会传染。\n";
        std::cout << "    工程建议：能用组合就用组合；\n";
        std::cout << "    需要「一个对象实现多个接口」时，优先用「一个实现基类 + 多个纯接口」的写法，\n";
        std::cout << "    接口本身无数据，就不会有菱形数据冗余（详见 05 章的抽象基类）。\n";
    }
    PrintLog("析构顺序");
    PrintLine();
}

void DemoIsAVsUsesA() {
    std::cout << "==== 附：is-a 与 is-implemented-in-terms-of ====\n";
    std::cout << "    反面教材：让 Stack 继承 std::vector<int>（或 std::deque）。\n";
    std::cout << "      class Stack : public std::vector<int> { ... };\n";
    std::cout << "      问题一：vector 的 insert / erase / operator[] 全部对外可见，\n";
    std::cout << "             使用者可以绕过栈的规则直接改中间元素 => 不变量失效；\n";
    std::cout << "      问题二：vector 没有虚析构函数，多态删除是未定义行为；\n";
    std::cout << "      问题三：栈与 vector 的关系不是 is-a：栈「不是一种」vector。\n";
    std::cout << "    正解一（组合）：\n";
    std::cout << "      class Stack { public: void push(int v); int pop(); private: std::vector<int> data_; };\n";
    std::cout << "      只暴露自己愿意承诺的接口，不变量完全可控 —— 这就是「组合优于继承」。\n";
    std::cout << "    正解二（private 继承，只在需要覆写虚函数时用）：\n";
    std::cout << "      class Stack : private std::vector<int> { ... };  // 复用实现但隐藏接口\n";
    std::cout << "    判定问题：如果「B 是一种 A」这句话让你犹豫，就用组合。\n";
    PrintLine();
}

}  // namespace

int main() {
    std::cout << "################ 04 继承基础 ################\n\n";
    DemoInheritanceKinds();
    DemoConstructionOrder();
    DemoNameHiding();
    DemoMultipleInheritance();
    DemoVirtualInheritance();
    DemoIsAVsUsesA();
    std::cout << "本章一句话总结：public 继承 = is-a；只为复用代码请用组合。\n";
    return 0;
}
