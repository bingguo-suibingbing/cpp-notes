// ============================================================================
// 05_virtual_polymorphism.cpp
// 演示主题：
//   1. 虚函数与动态绑定（运行时多态）；对比静态绑定
//   2. override 必写 / final 表达设计意图
//   3. 为什么基类析构函数必须是 virtual（漏掉的后果：派生类析构根本不执行）
//   4. 纯虚函数、抽象类、= 0，抽象基类当接口用
//   5. dynamic_cast 与 static_cast 的区别与适用场景
//   6. typeid 与运行时类型信息
//   7. 性能：vptr / vtable、每次调用一次间接跳转、阻止内联
//   8. 对象布局：带虚函数的对象 sizeof 会多一个指针（实测）
//
// 关键结论：
//   多态 = 「通过基类接口操作派生类对象」，代价是一个隐藏的 vptr 和一次间接调用；
//   凡是打算被继承的基类，析构函数就写成 virtual —— 例外只有：它本身没有虚函数（不打算多态使用）。
// ============================================================================

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <vector>

namespace {

int g_resource_alloc = 0;   // 派生类资源分配次数
int g_resource_free = 0;    // 派生类资源释放次数

void PrintLine() { std::cout << "--------------------------------------------------\n"; }

// ===========================================================================
// 1. 虚函数 + 动态绑定
// ===========================================================================
class Animal {
public:
    virtual ~Animal() = default;                 // 见第 3 节：基类析构必须是 virtual

    // 虚函数：调用哪个实现由「对象的动态类型」决定，而不是指针的静态类型
    virtual std::string Sound() const { return "……"; }

    // 非虚函数：静态绑定，永远调用 Animal 的版本
    std::string Category() const { return "Animal"; }

    // final：表达「这个虚函数不允许再被覆写」的设计意图
    virtual int Legs() const final { return 4; }
};

class Dog : public Animal {
public:
    std::string Sound() const override { return "汪"; }        // override 必写
    // int Legs() const override { return 3; }                 // 【错误写法】
    //   error C3248: "Animal::Legs": 声明为 "final" 的函数无法被重写
    std::string Category() const { return "Dog"; }             // 这不是虚函数覆盖，而是「名字隐藏」
};

class Puppy final : public Dog {                               // final 类：不允许再被继承
public:
    std::string Sound() const override { return "呜"; }
};

// class TinyPuppy : public Puppy {};                          // 【错误写法】
//   error C3246: 无法从 "Puppy" 继承，因为它已被声明为 "final"

// ===========================================================================
// 3. 虚析构：漏掉时的后果
// ===========================================================================
class BaseNoVirtualDtor {
public:
    ~BaseNoVirtualDtor() = default;                 // 非虚析构！
    virtual void Touch() {}
};

class DerivedWithResource : public BaseNoVirtualDtor {
public:
    DerivedWithResource() {
        data_ = new int[64];
        ++g_resource_alloc;
    }
    ~DerivedWithResource() {
        delete[] data_;                              // 如果没人调用这个析构函数 => 泄漏
        ++g_resource_free;
    }
    void Touch() override {}

private:
    int* data_ = nullptr;
};

class BaseWithVirtualDtor {
public:
    virtual ~BaseWithVirtualDtor() = default;       // 正确写法
    virtual void Touch() {}
};

class DerivedWithResourceOk : public BaseWithVirtualDtor {
public:
    DerivedWithResourceOk() {
        data_ = new int[64];
        ++g_resource_alloc;
    }
    ~DerivedWithResourceOk() override {              // 派生类析构自动是虚的，override 可以写也可以不写
        delete[] data_;
        ++g_resource_free;
    }
    void Touch() override {}

private:
    int* data_ = nullptr;
};

// 编译期证据：把「有没有虚析构」变成可断言的接口约束
static_assert(!std::has_virtual_destructor_v<BaseNoVirtualDtor>, "这个基类不该被多态删除");
static_assert(std::has_virtual_destructor_v<BaseWithVirtualDtor>, "多态基类必须有虚析构");
static_assert(std::has_virtual_destructor_v<DerivedWithResourceOk>, "派生类的析构也是虚的");

// ===========================================================================
// 4. 纯虚函数与抽象类（接口）
// ===========================================================================
class IShape {                                   // I 前缀是「接口」的常见命名约定
public:
    virtual ~IShape() = default;

    virtual double Area() const = 0;             // 纯虚函数：本类不需要给实现
    virtual std::string Name() const = 0;

    // 抽象类也可以有「非纯虚」的公共实现，派生类直接复用
    void PrintInfo() const {
        // 关键点：抽象类的成员函数里也能调用纯虚函数（通过 this 做动态绑定）
        std::cout << "      " << Name() << " 的面积 = " << Area() << "\n";
    }
};

class RectImpl : public IShape {
public:
    // 额外带一个 std::string 成员：让这个类成为「非平凡拷贝」类型，
    // 这是 C++20 里避免编译器隐式生成赋值运算符引发 C5267 的标准做法。
    RectImpl(double w, double h, std::string label = "rect")
        : w_(w), h_(h), label_(std::move(label)) {}
    double Area() const override { return w_ * h_; }
    std::string Name() const override { return "矩形"; }
    const std::string& label() const { return label_; }

private:
    double w_;
    double h_;
    std::string label_;
};

static_assert(std::is_abstract_v<IShape>, "含有纯虚函数的类是抽象类，不能实例化");
static_assert(!std::is_abstract_v<RectImpl>, "覆写了全部纯虚函数之后就能实例化");
// IShape s;                                     // 【错误写法】error C2259: 无法实例化抽象类

// 带实现的纯虚函数：纯虚函数也可以有函数体，派生类可以显式用 Base::Func() 调用它。
class IWithDefault {
public:
    virtual ~IWithDefault() = default;
    virtual int Value() const = 0;
};

int IWithDefault::Value() const { return 0; }    // 纯虚函数也能给定义

class UsesDefault : public IWithDefault {
public:
    // 派生类不想自己算的时候，可以显式复用基类那份（注意必须写全限定名）
    int Value() const override { return IWithDefault::Value() + 7; }
};

// ===========================================================================
// 5. dynamic_cast / static_cast
// ===========================================================================
class Media {
public:
    virtual ~Media() = default;
    virtual std::string Kind() const = 0;
};

class Audio : public Media {
public:
    std::string Kind() const override { return "audio"; }
    void PlaySound() const { std::cout << "      [Audio] 播放声音\n"; }
    int sample_rate = 44100;                     // Audio 特有的数据
};

class Video : public Media {
public:
    std::string Kind() const override { return "video"; }
    void RenderFrame() const { std::cout << "      [Video] 渲染帧\n"; }
    int width = 1920;                            // Video 特有的数据
};

// ===========================================================================
// 6/7/8. 性能与对象布局
// ===========================================================================
struct PlainStruct {
    int a;
    int b;
};

class PlainClass {
public:
    int Value() const { return a_ + b_; }        // 非虚成员函数不占对象空间

private:
    int a_ = 0;
    int b_ = 0;
};

class VirtualClass {
public:
    virtual ~VirtualClass() = default;
    int Value() const { return a_ + b_; }

private:
    int a_ = 0;
    int b_ = 0;
};

class VirtualClass3 {
public:
    virtual ~VirtualClass3() = default;
    virtual int F1() const { return 1; }
    virtual int F2() const { return 2; }
    virtual int F3() const { return 3; }

private:
    int a_ = 0;
};

// 虚函数调用 vs 直接调用的对比入口
double g_sink = 0.0;

double SumByVirtual(const std::vector<std::unique_ptr<IShape>>& shapes) {
    double total = 0.0;
    for (const auto& s : shapes) {
        total += s->Area();                      // 每次都是一次间接跳转（除非编译器能去虚化）
    }
    return total;
}

double SumByStatic(const std::vector<RectImpl>& shapes) {
    double total = 0.0;
    for (const auto& s : shapes) {
        total += s.Area();                       // 静态绑定，可以被内联
    }
    return total;
}

static_assert(sizeof(PlainClass) == 2 * sizeof(int), "非虚类：只有数据成员");
// 关键结论：多一个虚函数表指针，sizeof 就多一个指针大小（x64 上 8 字节）
static_assert(sizeof(VirtualClass) == 2 * sizeof(int) + sizeof(void*), "虚函数给对象加了一个 vptr");
// 虚函数的「个数」不影响对象大小：vtable 每个类一份，对象只存一个指针。
// VirtualClass（2 个 int）与 VirtualClass3（1 个 int）都塞得进「1 个 vptr + 4 字节数据 + 4 字节填充」，
// 所以两者大小相同 —— 这正是「对象里只有 vptr，没有虚函数数组」的直接证据。
static_assert(sizeof(VirtualClass) == sizeof(VirtualClass3),
              "虚函数个数从 1 个涨到 3 个，对象大小不变 => vtable 是每个类一份");
static_assert(sizeof(VirtualClass3) == 2 * sizeof(void*),
              "1 个 vptr + 1 个 int + 4 字节对齐填充 = 16 字节");

// ===========================================================================
// 演示入口
// ===========================================================================
void DemoDynamicVsStatic() {
    std::cout << "==== 1. 动态绑定 vs 静态绑定 ====\n";
    Dog dog;
    Animal& ref = dog;                            // 静态类型 Animal&，动态类型 Dog
    Animal* ptr = &dog;

    std::cout << "    Animal& ref = dog;\n";
    std::cout << "      ref.Sound()    = " << ref.Sound() << "（虚函数 => 动态绑定，调用 Dog::Sound）\n";
    std::cout << "      ref.Category() = " << ref.Category()
              << "（非虚函数 => 静态绑定，永远调用 Animal::Category）\n";
    std::cout << "      ptr->Sound()   = " << ptr->Sound() << "\n";
    std::cout << "      dog.Sound()    = " << dog.Sound() << "（对象直接调用，同类型无需动态分派）\n";
    std::cout << "    注意 Dog::Category 并不是「覆盖」，只是「隐藏」了 Animal::Category；\n";
    std::cout << "    通过 Animal& 调用时它根本不会被考虑 —— 这就是 06 章的主题之一。\n";

    Animal a;
    Animal& aref = a;
    std::cout << "    Animal a; Animal& aref = a; aref.Sound() = " << aref.Sound()
              << "（基类版本）\n";

    Puppy puppy;
    Animal& pref = puppy;
    std::cout << "    Puppy 继承 Dog 并覆写 Sound：pref.Sound() = " << pref.Sound()
              << "（虚调用按最派生的实现走）\n";
    std::cout << "    pref.Legs() = " << pref.Legs()
              << "（Animal::Legs 被标成 final，Puppy 无法再改）\n";
    PrintLine();
}

void DemoOverrideAndFinal() {
    std::cout << "==== 2. override 必写，final 表达意图 ====\n";
    std::cout << "    只写 virtual 不写 override 的三种典型翻车：\n";
    std::cout << "      1. 参数写错：Base::F(int) / Derived::F(double) —— 以为覆盖，其实是隐藏；\n";
    std::cout << "      2. 忘了 const：Base::F() const / Derived::F()      —— 同上；\n";
    std::cout << "      3. 基类那个函数根本不是虚函数（比如漏写了 virtual）—— 同上。\n";
    std::cout << "    这三种情况编译器都不会报错，只会在运行时表现「多态失效」。\n";
    std::cout << "    加上 override 之后它们全部变成编译错误：\n";
    std::cout << "      error C3668: \"Derived::F\": 带有重写说明符 \"override\" 的方法没有重写任何基类方法\n";
    std::cout << "    工程习惯：虚函数一律写 override；不想被继续覆写的写 final；\n";
    std::cout << "             不打算被继承的类标 final（编译器还能顺带做去虚化优化）。\n";
    PrintLine();
}

void DemoVirtualDestructor() {
    std::cout << "==== 3. 虚析构函数：漏掉时的后果 ====\n";
    g_resource_alloc = 0;
    g_resource_free = 0;
    {
        std::cout << "    --- 场景 A：基类析构【不是】virtual ---\n";
        BaseNoVirtualDtor* p = new DerivedWithResource();
        p->Touch();
        delete p;                                 // 静态类型是 BaseNoVirtualDtor*
        std::cout << "      分配次数 = " << g_resource_alloc << ", 释放次数 = " << g_resource_free;
        std::cout << "  => 派生类析构【没有】被调用，内存泄漏 " << (g_resource_alloc - g_resource_free)
                  << " 次\n";
        std::cout << "      这是未定义行为：真实项目里还可能出现「释放了派生类资源却仍在用」的崩溃。\n";
        std::cout << "      MSVC 不会为此报警告（除非开 /we4265 之类的额外检查）。\n";
    }
    {
        std::cout << "    --- 场景 B：基类析构是 virtual ---\n";
        g_resource_alloc = 0;                     // 重新计数，两个场景才能逐项对比
        g_resource_free = 0;
        BaseWithVirtualDtor* p = new DerivedWithResourceOk();
        p->Touch();
        delete p;                                 // 动态绑定到派生类析构，再自动调用基类析构
        std::cout << "      分配次数 = " << g_resource_alloc << ", 释放次数 = " << g_resource_free
                  << "  => 配对成功，无泄漏\n";
    }
    std::cout << "    判定标准（工程版）：\n";
    std::cout << "      - 类里有虚函数（哪怕只有一个）=> 析构函数必须 virtual；\n";
    std::cout << "      - 类没有任何虚函数、也不打算被多态使用 => 不要加 virtual（会平白多一个 vptr）；\n";
    std::cout << "      - 拿不准时就加：代价只是一个 vptr，漏掉的代价是内存泄漏 + 未定义行为。\n";
    std::cout << "    关于 shared_ptr：用 make_shared/unique_ptr 时是通过具体类型删除的，\n";
    std::cout << "    即使基类没有虚析构也不会漏；但只要你可能写出 delete base_ptr，就必须加。\n";
    PrintLine();
}

void DemoAbstractInterface() {
    std::cout << "==== 4. 纯虚函数与抽象类（接口） ====\n";
    std::cout << "    抽象类 = 至少有一个纯虚函数 => 不能实例化，只能通过指针/引用使用。\n";
    std::cout << "    static_assert(is_abstract_v<IShape>) 已在编译期验证；IShape s; 会报 error C2259。\n";
    std::vector<std::unique_ptr<IShape>> shapes;
    shapes.push_back(std::make_unique<RectImpl>(3.0, 4.0, "big"));
    shapes.push_back(std::make_unique<RectImpl>(1.0, 1.0, "small"));
    for (const auto& s : shapes) {
        s->PrintInfo();                           // 抽象基类提供公共逻辑，派生类只填数据
    }
    std::cout << "    接口设计要点：\n";
    std::cout << "      - 只放「所有实现都能承诺」的操作；宁少勿多，接口一旦发布很难改；\n";
    std::cout << "      - 析构必须是 virtual（或 protected 非虚，禁止通过基类指针删除）；\n";
    std::cout << "      - 可以在抽象类里给出默认实现，派生类用 Base::Func() 显式复用；\n";
    std::cout << "      - 接口不要暴露数据成员，也不要有需要 protected 的状态。\n";
    UsesDefault demo;
    std::cout << "    纯虚函数也能有函数体：UsesDefault::Value() = " << demo.Value()
              << "  ( = IWithDefault::Value() + 7 )\n";
    std::cout << "    本仓库把「运行期多态用虚接口」与「编译期多态用模板」分两章讲：\n";
    std::cout << "      本章（02-oop）是运行期多态；04-templates 的策略模板是编译期多态，\n";
    std::cout << "      两者在「策略可替换」这个需求上是竞争方案，取舍见 NOTES.md。\n";
    PrintLine();
}

void DemoCasts() {
    std::cout << "==== 5. dynamic_cast 与 static_cast ====\n";
    std::vector<std::unique_ptr<Media>> items;
    items.push_back(std::make_unique<Audio>());
    items.push_back(std::make_unique<Video>());

    for (const auto& m : items) {
        std::cout << "    处理 " << m->Kind() << "：\n";
        // dynamic_cast：运行期检查，失败返回 nullptr（指针）或抛 bad_cast（引用）
        if (auto* a = dynamic_cast<Audio*>(m.get())) {
            a->PlaySound();
            std::cout << "      dynamic_cast<Audio*> 成功，sample_rate = " << a->sample_rate << "\n";
        } else if (auto* v = dynamic_cast<Video*>(m.get())) {
            v->RenderFrame();
            std::cout << "      dynamic_cast<Video*> 成功，width = " << v->width << "\n";
        }
    }

    std::cout << "    用 static_cast 做同样的下行转换会怎样：\n";
    Media* raw = items[0].get();                  // 实际是 Audio
    Audio* correct = static_cast<Audio*>(raw);    // 恰好对了：不做任何检查，纯编译期偏移
    std::cout << "      static_cast<Audio*>(audio 对象) -> Kind() = " << correct->Kind()
              << "（这次是对的）\n";
    // 【错误写法演示】把 Audio 对象 static_cast 成 Video*，编译器不报错，运行期直接 UB：
    //   Video* wrong = static_cast<Video*>(raw);
    //   wrong->RenderFrame();   // 访问不存在的成员 => 崩溃或读到垃圾数据
    std::cout << "      Video* wrong = static_cast<Video*>(audio 对象); 编译通过，\n";
    std::cout << "      但 wrong->width 读的是对象里不存在的位置 => 未定义行为（崩溃 / 垃圾值）。\n";
    std::cout << "      （这行在本文件里被注释掉了，因为它是真的 UB。）\n";

    std::cout << "    选择标准：\n";
    std::cout << "      - 向上转换（派生 -> 基类）：直接赋值或用 static_cast，绝对安全；\n";
    std::cout << "      - 向下转换且「你确实不确定」：用 dynamic_cast（要求基类有虚函数）；\n";
    std::cout << "      - 向下转换且「你已经用别的方式确认过类型」：static_cast 更快；\n";
    std::cout << "      - 频繁 dynamic_cast 往往是设计信号：应该在接口上直接加虚函数，\n";
    std::cout << "        而不是先转换再调用（把「类型判断」交给多态，而不是交给 if-else）。\n";
    PrintLine();
}

void DemoTypeid() {
    std::cout << "==== 6. typeid 与运行时类型信息（RTTI） ====\n";
    Dog dog;
    Animal& ref = dog;
    std::cout << "    Animal& ref = dog;\n";
    std::cout << "      typeid(ref).name()  = " << typeid(ref).name()
              << "  <- 多态引用，拿到的是【动态类型】\n";
    std::cout << "      typeid(dog).name()  = " << typeid(dog).name() << "\n";
    std::cout << "      typeid(Animal).name() = " << typeid(Animal).name()
              << "  <- 类型本身，静态类型\n";
    std::cout << "      ref 的动态类型是 Dog? " << (typeid(ref) == typeid(Dog)) << "\n";
    std::cout << "    非多态类型上 typeid 无法看到动态类型：\n";
    PlainClass pc;
    PlainClass& pcRef = pc;
    std::cout << "      typeid(pcRef) == typeid(PlainClass)? " << (typeid(pcRef) == typeid(PlainClass))
              << "（没有虚函数 => 只有静态类型信息）\n";
    std::cout << "    工程建议：typeid 适合日志与断言，不适合做业务分支；\n";
    std::cout << "    它是 RTTI 的一部分，某些嵌入式工程会开 /GR- 关掉 RTTI，\n";
    std::cout << "    那时 dynamic_cast 与 typeid 都不可用 —— 设计时不要依赖它们。\n";
    PrintLine();
}

void DemoPerformanceAndLayout() {
    std::cout << "==== 7/8. 虚函数开销与对象布局（实测） ====\n";
    std::cout << "    sizeof(PlainStruct)    = " << sizeof(PlainStruct) << "\n";
    std::cout << "    sizeof(PlainClass)     = " << sizeof(PlainClass)
              << "  (2 个 int，非虚成员函数不占空间)\n";
    std::cout << "    sizeof(VirtualClass)   = " << sizeof(VirtualClass)
              << "  (2 个 int + 1 个 vptr，x64 下指针 8 字节)\n";
    std::cout << "    sizeof(VirtualClass3)  = " << sizeof(VirtualClass3)
              << "  (1 个 int + 1 个 vptr：虚函数【个数】不影响对象大小)\n";
    std::cout << "    sizeof(IShape)         = " << sizeof(IShape)
              << "  (抽象类也有 vptr，所以不是 1)\n";
    std::cout << "    内存模型：\n";
    std::cout << "      对象: [vptr][数据...]        vptr 指向本类的虚函数表\n";
    std::cout << "      vtable: 每个【类】一份，包含虚函数地址（含 RTTI 指针）\n";
    std::cout << "      虚调用: 读 vptr -> 查表偏移 -> 间接 call，无法内联\n";
    std::cout << "    开销构成：\n";
    std::cout << "      1. 空间：每个对象多一个指针（本例 " << sizeof(void*) << " 字节）；\n";
    std::cout << "      2. 时间：一次间接跳转 + 破坏指令流水 / 无法内联 / 阻止常量传播；\n";
    std::cout << "      3. 去虚化（devirtualization）：编译器在能确定动态类型时会优化掉，\n";
    std::cout << "         比如对 final 类、final 函数、或局部具体对象的调用。\n";

    // 用同一批数据做两个版本的求和，直观感受一下（数字只是参考，不同机器差异很大）
    std::vector<std::unique_ptr<IShape>> vshapes;
    std::vector<RectImpl> sshapes;
    for (int i = 0; i < 1000; ++i) {
        vshapes.push_back(std::make_unique<RectImpl>(2.0, 3.0, "r"));
        sshapes.push_back(RectImpl(2.0, 3.0, "r"));   // 有非平凡成员，这里会调用拷贝/移动构造
    }
    double tv = 0.0;
    for (int round = 0; round < 20000; ++round) {
        tv += SumByVirtual(vshapes);
        tv += SumByStatic(sshapes);
    }
    g_sink = tv;
    std::cout << "    两个版本各算了一遍（结果 " << g_sink
              << "），本示例不打印耗时：\n";
    std::cout << "      微基准很容易被编译器优化和 CPU 分支预测误导，正确做法是用 Release + 真实分布测。\n";
    std::cout << "    工程取舍：\n";
    std::cout << "      - 虚函数开销在「调用次数少、每次工作量大」时完全可以忽略；\n";
    std::cout << "      - 只有在极热的内层循环里，才考虑改成模板（编译期多态）或 std::variant +\n";
    std::cout << "        std::visit（封闭集合的另一种多态）。\n";
    PrintLine();
}

}  // namespace

int main() {
    std::cout << "################ 05 虚函数与运行时多态 ################\n\n";
    DemoDynamicVsStatic();
    DemoOverrideAndFinal();
    DemoVirtualDestructor();
    DemoAbstractInterface();
    DemoCasts();
    DemoTypeid();
    DemoPerformanceAndLayout();
    std::cout << "本章一句话总结：多态换来「可扩展」，代价是一个指针 + 一次间接调用；\n";
    std::cout << "只要打算多态使用，基类析构就必须是 virtual。\n";
    return 0;
}
