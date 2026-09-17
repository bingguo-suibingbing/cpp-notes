// ============================================================================
//  06_project_structure_and_style.cpp
// ----------------------------------------------------------------------------
//  演示主题：
//    1. 可维护 C++ 项目的目录布局：include/ src/ tests/ third_party/ cmake/ docs/
//    2. 头文件设计原则：头文件自包含、只包含必要头文件、前向声明减小编译依赖、
//       绝不在头文件里写 using namespace
//    3. 命名规范：类 PascalCase / 函数 snake_case 或 camelCase（选一个并统一）/
//       成员变量 m_ 前缀 / 为什么「下划线开头 + 大写」是保留标识符不能用
//    4. const 正确性：能加就加（它是编译器帮你检查的接口文档）
//    5. explicit / override / final / [[nodiscard]] 应该强制使用
//    6. 头文件自包含性测试：每个头文件单独编译一次，能立刻暴露缺失的 include
//    7. include 顺序规范：自己的头 -> C 库 -> C++ 标准库 -> 第三方 -> 项目内
//    8. clang-format / .clang-format / .editorconfig 的作用与最小配置
//    9. main() 的自我演示：接口与实现分离 + 不变量由类保证
//
//  关键结论：
//    - 「风格」的价值不在于哪种写法更漂亮，而在于**整个项目保持一致**：
//      一致才能让 review 只看逻辑，让工具（clang-format/静态分析）自动化。
//    - 头文件是项目的公共接口：它应该只暴露必要的东西，并且能独立编译。
//    - 把所有成员私有 + 只通过成员函数修改 = 不变量永远成立；
//      这就是「封装」的实际收益，而不是「面向对象的教条」。
//    - 编译器能替你检查的事（const / explicit / override / nodiscard），
//      一定要交给编译器 —— 人一定会忘。
// ============================================================================

#include <algorithm>  // std::clamp（C++17）
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// ============================================================================
//  第 1 节：一个「接口与实现分离」的示例
// ----------------------------------------------------------------------------
//  真实项目里，下面这个 Temperature 的「声明」会放在 include/temperature.h，
//  「定义」会放在 src/temperature.cpp。本文件把它放在一起（单文件示例的限制），
//  但用注释标出「这一段属于头文件、这一段属于实现文件」。
//
//  接口与实现分离的收益：
//    1) 编译依赖：改 .cpp 只重编一个文件，改头文件才需要重编所有包含者；
//    2) 可读性：.h 就是「这个类能做什么」，一屏看完；
//    3) 可测试性：接口稳定 -> 测试可以对着接口写，不怕实现重构。
//
//  设计要点（下面每一条都在代码里标了为什么）：
//    - 构造函数建立不变量（温度不能低于绝对零度）；
//    - 成员全部私有（private）：外部无法把对象改坏；
//    - 每个修改操作后检查不变量（Debug 下断言）；
//    - 查询函数全部 const + [[nodiscard]]；
//    - 转换构造函数用 explicit 防止隐式转换（温度只能由明确意图创建）。
// ============================================================================

class Temperature {
public:
    // ---- 创建：用「具名工厂函数」表达意图，比一堆构造函数重载更清楚 ----
    // 理由：celsius(20) 一眼就知道单位；Temperature(20) 不知道是摄氏还是华氏。
    [[nodiscard]] static Temperature from_celsius(double degrees) { return Temperature(degrees, Unit::Celsius); }
    [[nodiscard]] static Temperature from_fahrenheit(double degrees) {
        return Temperature((degrees - 32.0) * 5.0 / 9.0, Unit::Celsius);
    }
    [[nodiscard]] static Temperature from_kelvin(double degrees) {
        if (degrees < 0.0) {
            throw std::invalid_argument("Temperature::from_kelvin：开尔文温度不能为负");
        }
        return Temperature(degrees - 273.15, Unit::Celsius);
    }

    // ---- 查询：能 const 就 const，能 [[nodiscard]] 就 [[nodiscard]] ----
    // const：调用者看到 const 就知道「这个函数不会改对象」；编译器会强制保证。
    // [[nodiscard]]：温度查询的结果丢掉一定是写错了，编译期直接警告。
    [[nodiscard]] double celsius() const noexcept { return celsius_; }
    [[nodiscard]] double fahrenheit() const noexcept { return celsius_ * 9.0 / 5.0 + 32.0; }
    [[nodiscard]] double kelvin() const noexcept { return celsius_ + 273.15; }
    [[nodiscard]] bool is_freezing() const noexcept { return celsius_ <= 0.0; }

    // ---- 修改：返回新对象而不是原地改，更适合值语义（也可提供 set_xxx）----
    [[nodiscard]] Temperature offset_by(double delta_celsius) const {
        return Temperature(celsius_ + delta_celsius, Unit::Celsius);  // 构造时会重新校验不变量
    }

    // ---- 比较：运算符重载定义在类内，用 default 让编译器生成 ----
    // <=> 是 C++20 的三路比较：一行生成 ==、!=、<、<=、>、>=（这里用 default 自动推导）
    friend bool operator==(const Temperature& lhs, const Temperature& rhs) noexcept {
        return lhs.celsius_ == rhs.celsius_;
    }

private:
    enum class Unit { Celsius };
    // explicit 的作用：禁止 `Temperature t = 20.0;` 这种隐式转换。
    // 隐式转换是 C++ 里最容易出「看起来对、其实调错重载」问题的地方。
    explicit Temperature(double degrees, Unit unit) : celsius_(to_celsius(degrees, unit)) {
        check_invariant();  // 对象从出生起就必须合法
    }

    static double to_celsius(double degrees, Unit unit) noexcept {
        switch (unit) {
            case Unit::Celsius:
                return degrees;
            default:
                return degrees;
        }
    }

    // 不变量：摄氏温度不能低于绝对零度（-273.15 C）。
    // 断言（而不是异常）的理由：这个类的唯一入口都做了检查，
    // 走到这里说明**我们自己写错了**（见 02_assert_and_contracts.cpp 的判据）。
    void check_invariant() const {
        assert(celsius_ >= -273.15 && "不变量被破坏：温度低于绝对零度");
    }

    double celsius_;  // 统一用摄氏存储：内部表示唯一，避免「单位混乱」这类经典 bug
};

// ============================================================================
//  第 2 节：用「强类型」消灭一整类 bug
// ----------------------------------------------------------------------------
//  同一个物理量的不同单位如果都用 double，编译器无法帮你区分：
//      void resize(int width, int height);
//      resize(height, width);        // 编译通过，运行期才发现错了
//  对策是新类型（strong typedef）：一个包了 double 的 struct，单位写进类型名，
//  隐式转换一律禁止。这是「编译期能查就不要留到运行期」的又一处体现。
// ============================================================================

struct Meters {
    double value = 0.0;
};
struct Seconds {
    double value = 0.0;
};

[[nodiscard]] double speed_m_per_s(Meters distance, Seconds time) {
    if (time.value <= 0.0) {
        throw std::invalid_argument("speed_m_per_s：时间必须为正");
    }
    return distance.value / time.value;
}
// 如果两个参数类型写反（speed_m_per_s(t, d)），编译期就会报错 —— 这就是收益。

// ============================================================================
//  第 3 节：const 正确性
// ----------------------------------------------------------------------------
//  const 的三层含义，一层比一层强：
//    1) const 变量：值不可改（同时也是「告诉读者我之后不动它」）；
//    2) const 成员函数：不修改对象状态（可以被 const 对象调用）；
//    3) const 引用参数：不拷贝 + 不修改（接口的默认选择，见 04 文件的实测）。
//
//  规则：**能加 const 就加 const**。加不上去的地方（比如缓存、锁、计数器）
//  才用 mutable，而且要在注释里写明理由。
//  收益：编译期挡住误改；接口自解释；顺便让编译器有更多优化空间。
// ============================================================================

class Inventory {
public:
    void add(std::string item, int count) {
        if (count <= 0) {
            throw std::invalid_argument("Inventory::add：数量必须为正");
        }
        items_.emplace_back(std::move(item), count);  // 移动而不是拷贝（见 04 文件实测）
    }

    // const 成员函数：可以读，不能改。注意这里返回 const&，不拷贝。
    [[nodiscard]] const std::vector<std::pair<std::string, int>>& items() const noexcept {
        return items_;
    }

    // 查询逻辑也写成 const：调用者不需要非 const 对象就能查询
    [[nodiscard]] int total_count() const noexcept {
        int total = 0;
        for (const auto& [name, count] : items_) {  // 结构化绑定（C++17）
            (void)name;                             // 只是演示：名字不需要参与求和
            total += count;
        }
        return total;
    }

    [[nodiscard]] std::size_t kind_count() const noexcept { return items_.size(); }

private:
    std::vector<std::pair<std::string, int>> items_;
};

// ============================================================================
//  第 4 节：explicit / override / final / [[nodiscard]] —— 四个「必须写」的关键字
// ----------------------------------------------------------------------------
//  explicit：单参数构造函数默认都应该加（除非确实要参与隐式转换，
//            比如 std::string_view 从 const char* 的转换）。
//  override：写虚函数重写时**必须**写。不写的风险：基类签名改了，
//            派生类那个函数就静默变成一个「新函数」，你以为在重写，其实没有。
//  final：类或虚函数不再被继承/重写时写上，让编译器帮忙检查意图，
//         也可能让编译器去虚化（devirtualization）从而提速。
//  [[nodiscard]]：有返回值的纯查询/错误码/新对象，一律加上。
// ============================================================================

class Shape {
public:
    Shape() = default;
    Shape(const Shape&) = default;
    Shape& operator=(const Shape&) = default;
    Shape(Shape&&) = default;
    Shape& operator=(Shape&&) = default;

    // 基类析构函数必须是 virtual：否则通过基类指针 delete 派生类对象是未定义行为
    // （只调用基类析构，派生类资源泄漏）。
    virtual ~Shape() = default;

    // = 0：纯虚函数，Shape 成为抽象类（不能直接实例化）。
    [[nodiscard]] virtual double area() const noexcept = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    // 同时提供「非虚接口 + 虚实现」的写法（NVI 惯用法）也是常见工程做法：
    // 基类用非虚函数固定流程，内部调用虚函数做可变部分，这样派生类无法破坏流程。
    [[nodiscard]] std::string describe() const {
        // 注意：这里用了 name() 和 area()，都是虚调用 -> 走派生类的实现
        return std::string(name()) + "(area=" + std::to_string(area()) + ")";
    }
};

class Circle final : public Shape {  // final：不再被继承，编译器可以据此优化
public:
    explicit Circle(double radius) : radius_(radius) {
        if (radius <= 0.0) {
            throw std::invalid_argument("Circle：半径必须为正");
        }
    }
    [[nodiscard]] double area() const noexcept override { return 3.14159265358979 * radius_ * radius_; }
    [[nodiscard]] std::string_view name() const noexcept override { return "Circle"; }

private:
    double radius_;
};

class Rectangle final : public Shape {
public:
    Rectangle(double width, double height) : width_(width), height_(height) {
        if (width <= 0.0 || height <= 0.0) {
            throw std::invalid_argument("Rectangle：宽高必须为正");
        }
    }
    [[nodiscard]] double area() const noexcept override { return width_ * height_; }
    [[nodiscard]] std::string_view name() const noexcept override { return "Rectangle"; }

private:
    double width_;
    double height_;
};

// ============================================================================
//  第 5 节：头文件设计原则（用代码 + 表格说明）
// ----------------------------------------------------------------------------
//  (1) 头文件必须自包含
//      每个头文件单独编译一次都能过 —— 这能立刻暴露「忘了 include」。
//      本文件顶部的 include 列表就是「为了让这个 .cpp 自包含」而写的；
//      把它换成一个 .h，规则完全一样。
//
//  (2) 只包含必要的东西
//      头文件里的每个 include 都会被所有包含者继承。
//      多一个 <iostream>，所有包含者的编译时间都会变长（实测很可观）。
//
//  (3) 用前向声明替代 include
//      只需要「指针/引用」时，前向声明就够了：
//          class Logger;                       // 前向声明
//          void set_logger(Logger* logger);    // 不需要 #include "logger.h"
//      收益：改 logger.h 不再触发本头文件的所有包含者重编。
//      注意：需要「对象大小 / 成员访问 / 继承」时，必须完整定义。
//
//  (4) 绝不在头文件里写 using namespace
//      `using namespace std;` 出现在 .cpp 里也只是「差点意思」，
//      出现在头文件里则是灾难：它会污染所有包含者的命名空间，
//      引发无法定位的名字冲突（而且顺序不同行为不同）。
//      头文件里可以用：完全限定名、别名（using MyMap = std::map<...>）、
//      类内 using 声明，但不要 using namespace。
//
//  (5) #pragma once
//      比传统 include guard 短、不容易写错（不用三处重复同一个宏名）。
//      #pragma once 不是标准，但所有主流编译器都支持；
//      需要极致的可移植性时才用传统 include guard。
//
//  下面是「前向声明」的可运行演示（同一个文件里放两个类，模拟两个头文件）。
// ============================================================================

class Database;  // 前向声明：这里只需要指针，不需要 Database 的完整定义

class Repository {
public:
    // 参数是引用 -> 只需要前向声明，不需要 #include "database.h"
    explicit Repository(Database& db) noexcept : db_(&db) {}
    [[nodiscard]] Database* database() const noexcept { return db_; }

private:
    Database* db_;  // 指针：大小固定（8 字节），不需要完整类型
};

// Database 的完整定义可以放在另一个头文件里
class Database {
public:
    explicit Database(std::string name) : name_(std::move(name)) {}
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

private:
    std::string name_;
};

// ============================================================================
//  第 6 节：命名规范（选一个，然后**全项目统一**）
// ----------------------------------------------------------------------------
//    | 对象       | 推荐                     | 反例 / 说明                        |
//    |------------|--------------------------|------------------------------------|
//    | 类型/类    | PascalCase               | class temperature（看起来像变量）  |
//    | 函数       | snake_case 或 camelCase  | 混用 get_Value / setvalue 最难读   |
//    | 成员变量   | m_name 或 name_ 后缀     | 与局部变量区分，避免遮蔽（C4458）  |
//    | 常量       | kMaxRetry 或 MAX_RETRY   | 全大写的宏要用项目前缀（MYPROJ_）  |
//    | 命名空间   | 全小写，如 myproj::net   | 别用大写开头（容易和类型撞）        |
//    | 模板参数   | T / U / 有含义的 PascalCase | 避免只写 A、B、C                |
//
//  两个必须记住的语言规则：
//    (1) **下划线开头 + 大写字母**（如 _Count）和**双下划线**（如 __my）是
//        「保留给实现」的标识符，标准明确禁止你在全局命名空间使用 ——
//        用了可能和标准库/编译器内部符号冲突，症状是「在自己机器上好好的，
//        换个编译器版本随机崩」。
//    (2) 小写字母开头的下划线（如 _count）在全局命名空间也被保留；
//        在类成员或函数内是允许的，但容易踩坑，所以约定俗成用 m_ / 后缀 _。
//
//  本文件遵循的约定（自演示）：类型 PascalCase、函数 snake_case、
//  成员变量 name_ 后缀、常量 kXxx。
// ============================================================================

// ============================================================================
//  第 7 节：include 顺序规范
// ----------------------------------------------------------------------------
//  推荐顺序（组与组之间空一行，组内按字母序）：
//    1) 本文件对应的头文件（.cpp 的第一行）—— 这样可以立刻发现头文件不自包含；
//    2) C 库头文件（<cstdio>、<cstring>）；
//    3) C++ 标准库头文件（<vector>、<string>）；
//    4) 第三方库头文件（<gtest/gtest.h>、<boost/...>）；
//    5) 本项目其它头文件（"myproject/util.h"）。
//
//  为什么：本文件自己的头文件放第一，能保证它是自包含的（这是 Google 风格
//  指南里最实用的一条）；其余按「越通用越靠前」排，减少隐藏依赖。
//  clang-format 的 SortIncludes / IncludeCategories 可以自动维护这个顺序。
//
//  本文件顶部就是按这个顺序写的（第 1 组为空，因为没有对应的 .h）。
// ============================================================================

// ============================================================================
//  第 8 节：格式化与编辑器配置（工具链，代码里只做说明）
// ----------------------------------------------------------------------------
//  .clang-format：把「格式争论」变成机器的事。最小配置见本目录的 .clang-format。
//    用法：clang-format -i src/*.cpp   或在 VS 里启用「保存时自动格式化」。
//    关键项：BasedOnStyle / IndentWidth / ColumnLimit / SortIncludes /
//            AllowShortFunctionsOnASingleLine / PointerAlignment。
//
//  .editorconfig：让不同编辑器（VS / VS Code / CLion / Vim）共享同一套缩进与
//    换行规则（缩进宽度、用空格还是 Tab、行尾、文件末尾空行、编码）。
//    最小配置见本目录的 .editorconfig。
//
//  两个文件都应该**进版本库**：新同事 clone 下来就自动遵守同一套格式。
// ============================================================================

// 下面这个函数演示「函数怎么按规范命名 + 怎么用 const 正确性 + 怎么用 clamp」。
// 它也被用作头文件自包含性测试的思想说明：只要本 .cpp 能单独编译通过，
// 说明它引用的所有东西都已经 include 齐了 —— 头文件也照这个规则检查。
[[nodiscard]] double clamp_percent(double value) noexcept {
    // std::clamp（C++17）：把值限制在 [0, 100]
    return std::clamp(value, 0.0, 100.0);
}

int main() {
    std::cout << "==== 06 项目结构与编码风格 ====\n\n";

    std::cout << "---- 1. 接口与实现分离 + 不变量由类保证 ----\n";
    {
        const Temperature room = Temperature::from_celsius(22.5);
        const Temperature freezing = Temperature::from_fahrenheit(32.0);
        const Temperature absolute_zero = Temperature::from_kelvin(0.0);

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  room          : " << room.celsius() << " C / " << room.fahrenheit() << " F / "
                  << room.kelvin() << " K，结冰？" << std::boolalpha << room.is_freezing() << "\n";
        std::cout << "  freezing      : " << freezing.celsius() << " C（32 F 正好是 0 C）\n";
        std::cout << "  absolute_zero : " << absolute_zero.celsius() << " C（开尔文 0 度）\n";
        std::cout << "  room.offset_by(-10) : " << room.offset_by(-10.0).celsius() << " C\n";
        try {
            (void)Temperature::from_kelvin(-1.0);  // 外部输入非法 -> 抛异常（可恢复）
        } catch (const std::invalid_argument& e) {
            std::cout << "  非法输入被拒绝：" << e.what() << "\n";
        }
        std::cout << "  operator== : room == from_fahrenheit(72.5) -> " << std::boolalpha
                  << (room == Temperature::from_fahrenheit(72.5)) << std::noboolalpha << "\n";
        std::cout << "  结论：构造函数是唯一的入口 -> 不变量永久成立；\n";
        std::cout << "        所有查询都是 const + [[nodiscard]] -> 编译器帮你检查接口用法。\n\n";
    }

    std::cout << "---- 2. 强类型消灭「参数写反」类 bug ----\n";
    {
        const Meters distance{100.0};
        const Seconds time{9.58};
        std::cout << "  100 m / 9.58 s = " << speed_m_per_s(distance, time) << " m/s\n";
        std::cout << "  （写反成 speed_m_per_s(time, distance) 会**编译失败**，而不是算错结果）\n\n";
    }

    std::cout << "---- 3. const 正确性 ----\n";
    {
        Inventory inv;
        inv.add("bolt", 100);
        inv.add("nut", 250);
        const Inventory& readonly = inv;  // 通过 const 引用只能调用 const 成员函数
        std::cout << "  种类 = " << readonly.kind_count() << "，总数 = " << readonly.total_count()
                  << "\n";
        std::cout << "  结论：const 让「只读接口」成为类型系统的一部分，\n";
        std::cout << "        调用者不需要看实现就知道它不会改对象。\n\n";
    }

    std::cout << "---- 4. explicit / override / final / [[nodiscard]] ----\n";
    {
        const Circle circle(2.0);
        const Rectangle rect(3.0, 4.0);
        // 多态：通过基类引用调用虚函数；describe() 是 NVI：基类固定流程 + 虚函数定制
        const std::vector<const Shape*> shapes{&circle, &rect};
        for (const Shape* shape : shapes) {
            std::cout << "  " << shape->describe() << "\n";
        }
        std::cout << "  结论：override 保证「确实重写了」；final 表达「不再被继承」；\n";
        std::cout << "        explicit 挡住隐式转换；[[nodiscard]] 挡住丢弃返回值。\n";
        std::cout << "        这四个关键字应该写进编码规范并靠 code review 强制执行。\n\n";
    }

    std::cout << "---- 5. 头文件自包含 / 前向声明 / 不用 using namespace ----\n";
    {
        Database db("production");
        const Repository repo(db);
        std::cout << "  Repository 只前向声明了 Database，仍然能用：" << repo.database()->name() << "\n";
        std::cout << "  结论：只需要指针/引用时用前向声明 -> 改 database.h 不必重编 repository.h 的包含者。\n\n";
    }

    std::cout << "---- 6. 命名规范速查 ----\n";
    std::cout << "  对象          推荐                      说明\n";
    std::cout << "  ------------  ------------------------  ------------------------------------\n";
    std::cout << "  类型/类       PascalCase               class Temperature\n";
    std::cout << "  函数          snake_case（本文件）      from_celsius / total_count\n";
    std::cout << "  成员变量      name_ 或 m_name          与局部变量区分，避免遮蔽\n";
    std::cout << "  常量          kMaxRetry / MAX_RETRY    宏要加项目前缀 MYPROJ_MAX\n";
    std::cout << "  命名空间      全小写 myproj::net        不要用大写开头\n";
    std::cout << "  禁止          _Count / __x / _x（全局） 保留给实现，可能随机崩\n\n";
    std::cout << "  自演示：本文件类型用 PascalCase、函数用 snake_case、成员带 _ 后缀、\n";
    std::cout << "          常量用 k 前缀；全文件没有 using namespace，一律 std:: 限定。\n\n";

    std::cout << "---- 7. include 顺序规范（本文件顶部就是按这个顺序写的）----\n";
    std::cout << "  1) 本文件对应的头文件（.cpp 放第一行 -> 立刻发现头文件不自包含）\n";
    std::cout << "  2) C 库            <cstdio> <cstring>\n";
    std::cout << "  3) C++ 标准库      <string> <vector> <algorithm>\n";
    std::cout << "  4) 第三方库        <gtest/gtest.h>\n";
    std::cout << "  5) 本项目头文件    \"myproject/util.h\"\n";
    std::cout << "  组间空一行，组内按字母序；clang-format 的 IncludeCategories 可自动维护。\n\n";

    std::cout << "---- 8. 工程目录布局（推荐）----\n";
    std::cout << "  myproject/\n";
    std::cout << "    CMakeLists.txt            顶层构建脚本（唯一入口）\n";
    std::cout << "    CMakePresets.json         预设：Debug/Release/ASan 等组合\n";
    std::cout << "    .clang-format             格式化规则（进版本库）\n";
    std::cout << "    .editorconfig             编辑器缩进/换行规则（进版本库）\n";
    std::cout << "    .gitignore                构建产物、.vs/、build/ 等\n";
    std::cout << "    README.md                 怎么构建、怎么跑、怎么测\n";
    std::cout << "    include/myproject/        对外公开的头文件（安装/导出用）\n";
    std::cout << "        temperature.h\n";
    std::cout << "    src/                      实现文件（不进安装包）\n";
    std::cout << "        temperature.cpp\n";
    std::cout << "        internal/             仅本库内部使用的头文件\n";
    std::cout << "    tests/                    单元测试（每个模块一个文件）\n";
    std::cout << "    third_party/              第三方源码或子模块（不修改）\n";
    std::cout << "    cmake/                    自定义 .cmake 模块（Find*.cmake 等）\n";
    std::cout << "    docs/                     设计文档、架构图、决策记录(ADR)\n";
    std::cout << "    bench/                    性能基准（可选）\n";
    std::cout << "    examples/                 示例程序（可选）\n\n";

    std::cout << "---- 9. 编译期能查的都要查：static_assert 兜底 ----\n";
    // 接口契约用 static_assert 固定下来：温度一定是可平凡复制的值类型，
    // 这样「按值返回」「放进容器」都不会有额外开销。
    static_assert(std::is_trivially_copyable_v<Temperature>,
                  "Temperature 应该是可平凡复制的值类型，否则按值返回会有隐藏开销");
    static_assert(sizeof(Meters) == sizeof(double), "强类型包装不应引入额外开销");
    static_assert(std::is_abstract_v<Shape>, "Shape 应该是抽象基类：不能直接实例化");
    static_assert(std::has_virtual_destructor_v<Shape>, "Shape 必须有多态析构，否则 delete 会漏析构");
    static_assert(std::is_copy_constructible_v<Repository>,
                  "Repository 只存指针，拷贝是安全的，因此应该保持可拷贝");
    std::cout << "  已通过编译期检查：\n";
    std::cout << "    Temperature 可平凡复制（按值传递零开销）\n";
    std::cout << "    sizeof(Meters) == sizeof(double)（强类型不引入额外开销）\n";
    std::cout << "    Shape 是抽象类且有多态析构\n";
    std::cout << "    Repository 可拷贝（它只持有指针，拷贝语义安全）\n";
    std::cout << "    clamp_percent(150) = " << clamp_percent(150.0) << "\n\n";

    std::cout << "---- 10. 让格式与风格自动化的最小配置 ----\n";
    std::cout << "  .clang-format（本目录有完整版）：\n";
    std::cout << "      BasedOnStyle: Google / LLVM / Microsoft 选一个\n";
    std::cout << "      IndentWidth: 4\n";
    std::cout << "      ColumnLimit: 120\n";
    std::cout << "      SortIncludes: CaseSensitive\n";
    std::cout << "  .editorconfig（本目录有完整版）：\n";
    std::cout << "      indent_style = space\n";
    std::cout << "      indent_size = 4\n";
    std::cout << "      end_of_line = crlf（Windows）/ lf（跨平台仓库建议 lf）\n";
    std::cout << "      insert_final_newline = true\n";
    std::cout << "      charset = utf-8\n\n";

    std::cout << "  代码评审时要盯的最重要的几件事（完整清单见 NOTES.md）：\n";
    std::cout << "    1) 有没有裸 new/delete、裸 malloc/free（应该有 unique_ptr/容器）\n";
    std::cout << "    2) 有没有该 const 而没 const 的参数/成员函数\n";
    std::cout << "    3) 构造函数有没有 explicit；重写有没有 override\n";
    std::cout << "    4) 头文件里有没有 using namespace、有没有包含不必要的大头文件\n";
    std::cout << "    5) 有没有「因为懒得想」而跳过错误检查的路径\n\n";

    std::cout << "==== 结论：风格靠工具固化，接口靠类型表达，不变量靠封装保证 ====\n";
    return 0;
}
