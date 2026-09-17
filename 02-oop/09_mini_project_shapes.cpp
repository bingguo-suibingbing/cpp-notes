// ============================================================================
// 09_mini_project_shapes.cpp
// 演示主题（综合小项目：一个图形库）：
//   1. 接口与实现分离：Shape 抽象基类只描述「能做什么」，派生类决定「怎么做」
//   2. 虚函数计算面积与周长；virtual 析构保证多态删除安全
//   3. std::vector<std::unique_ptr<Shape>> 管理异构对象集合（RAII）
//   4. operator<< 让图形对象直接进日志与断言
//   5. 按面积排序：std::sort + 自定义比较器（lambda 与仿函数各来一遍）
//   6. 用 std::ostringstream 输出，最后用 RAII 文件包装器落盘
//   7. 用 static_assert / 计数器验证：抽象类不可实例化、对象既不泄漏也不 double free
//
// 关键结论：
//   一个「多态容器」的标准工程写法就是：
//     抽象接口 + unique_ptr 存储 + const 引用传参 + 算法用 lambda 定制。
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// 全局计数器：验证「构造与析构一一配对」
// ---------------------------------------------------------------------------
int g_shapes_created = 0;
int g_shapes_destroyed = 0;

void PrintLine() { std::cout << "--------------------------------------------------\n"; }

// 浮点比较：不要把面积直接写进 ==，用相对误差判断（工程里几乎总是这么干）
bool NearlyEqual(double a, double b, double epsilon = 1e-9) {
    const double diff = std::fabs(a - b);
    return diff <= epsilon * std::max(1.0, std::max(std::fabs(a), std::fabs(b)));
}

// ===========================================================================
// 1. 接口：抽象基类只描述能力，不含任何数据成员
// ===========================================================================
class Shape {
public:
    Shape() { ++g_shapes_created; }

    // 多态基类的析构必须 virtual：否则 delete Shape* 不会调用派生类析构（见 05 章）
    virtual ~Shape() { ++g_shapes_destroyed; }

    // 禁止拷贝：图形对象一律通过 unique_ptr 管理，值语义在这里没有意义
    Shape(const Shape&) = delete;
    Shape& operator=(const Shape&) = delete;

    // 纯虚函数：接口的三个方法，任何具体图形都必须实现
    virtual double Area() const = 0;
    virtual double Perimeter() const = 0;
    virtual std::string Name() const = 0;

    // 接口可以带非虚的公共实现：派生类不重复写「打印格式」
    // 注意它调用虚函数 => 通过基类指针调用时会动态绑定（这正是我们想要的）
    std::string Describe() const {
        std::ostringstream os;
        os << std::fixed << std::setprecision(3) << Name() << "(面积=" << Area()
           << ", 周长=" << Perimeter() << ")";
        return os.str();
    }

    // 带默认参数的小工具：不建议在虚函数上加默认参数（默认值是静态绑定的，容易踩坑），
    // 所以这里放一个非虚函数包装。
    double AreaTimes(double factor) const { return Area() * factor; }
};

// 输出运算符：非成员（见 07 章），让 Shape 能直接用于日志
std::ostream& operator<<(std::ostream& os, const Shape& shape) {
    return os << shape.Describe();
}

// 编译期把「接口的约定」固定下来
static_assert(std::is_abstract_v<Shape>, "Shape 含纯虚函数，必须是抽象类，不能被实例化");
static_assert(std::has_virtual_destructor_v<Shape>, "多态基类必须有虚析构");
static_assert(!std::is_copy_constructible_v<Shape>, "Shape 禁止拷贝，只能通过 unique_ptr 持有");
// Shape s;                                       // 【错误写法】error C2259: 无法实例化抽象类

// ===========================================================================
// 具体图形 1：圆
// ===========================================================================
class Circle final : public Shape {
public:
    explicit Circle(double radius) : radius_(radius) {
        if (!(radius > 0.0)) {                     // 构造函数负责建立不变量
            throw std::invalid_argument("圆的半径必须为正数");
        }
    }

    double Area() const override { return kPi * radius_ * radius_; }
    double Perimeter() const override { return 2.0 * kPi * radius_; }
    std::string Name() const override { return "Circle"; }
    double radius() const { return radius_; }

private:
    static constexpr double kPi = 3.14159265358979323846;   // 编译期常量
    double radius_;
};

// ===========================================================================
// 具体图形 2：矩形
// ===========================================================================
class Rectangle final : public Shape {
public:
    Rectangle(double width, double height) : width_(width), height_(height) {
        if (!(width > 0.0) || !(height > 0.0)) {
            throw std::invalid_argument("矩形的宽高必须为正数");
        }
    }

    double Area() const override { return width_ * height_; }
    double Perimeter() const override { return 2.0 * (width_ + height_); }
    std::string Name() const override { return "Rectangle"; }
    double width() const { return width_; }
    double height() const { return height_; }
    bool IsSquare() const { return NearlyEqual(width_, height_); }

private:
    double width_;
    double height_;
};

// ===========================================================================
// 具体图形 3：三角形（用海伦公式，顺便演示「合法性校验」）
// ===========================================================================
class Triangle final : public Shape {
public:
    Triangle(double a, double b, double c) : a_(a), b_(b), c_(c) {
        if (!(a > 0.0) || !(b > 0.0) || !(c > 0.0)) {
            throw std::invalid_argument("三角形的边长必须为正数");
        }
        // 三角不等式：任意两边之和必须大于第三边，否则这三点构不成三角形。
        // 这正是「类负责维护不变量」的例子：非法对象根本造不出来。
        if (a + b <= c || a + c <= b || b + c <= a) {
            throw std::invalid_argument("三条边不满足三角不等式，无法构成三角形");
        }
    }

    double Area() const override {
        const double s = Perimeter() / 2.0;                    // 半周长
        return std::sqrt(s * (s - a_) * (s - b_) * (s - c_));  // 海伦公式
    }
    double Perimeter() const override { return a_ + b_ + c_; }
    std::string Name() const override { return "Triangle"; }

private:
    double a_;
    double b_;
    double c_;
};

// ===========================================================================
// 工厂：把「按名字造对象」收敛到一处（见 08 章的简单工厂）
// ===========================================================================
std::unique_ptr<Shape> MakeShape(const std::string& kind, const std::vector<double>& args) {
    if (kind == "circle" && args.size() == 1) {
        return std::make_unique<Circle>(args[0]);
    }
    if (kind == "rect" && args.size() == 2) {
        return std::make_unique<Rectangle>(args[0], args[1]);
    }
    if (kind == "triangle" && args.size() == 3) {
        return std::make_unique<Triangle>(args[0], args[1], args[2]);
    }
    return nullptr;
}

// ===========================================================================
// 算法：对多态容器做统计与排序
// ===========================================================================
double TotalArea(const std::vector<std::unique_ptr<Shape>>& shapes) {
    // 注意不能用 std::accumulate 直接对 unique_ptr 求和（无法提取值），用显式循环更清楚
    double total = 0.0;
    for (const auto& s : shapes) {
        total += s->Area();
    }
    return total;
}

double TotalPerimeter(const std::vector<std::unique_ptr<Shape>>& shapes) {
    double total = 0.0;
    for (const auto& s : shapes) {
        total += s->Perimeter();
    }
    return total;
}

// 按面积排序：不修改原容器，返回「指向原对象的指针」序列（避免拷贝多态对象，也不能拷贝）
std::vector<const Shape*> SortedByArea(const std::vector<std::unique_ptr<Shape>>& shapes) {
    std::vector<const Shape*> view;
    view.reserve(shapes.size());
    for (const auto& s : shapes) {
        view.push_back(s.get());
    }
    // lambda 作为比较器：最常用、最直观
    std::sort(view.begin(), view.end(), [](const Shape* lhs, const Shape* rhs) {
        return lhs->Area() < rhs->Area();
    });
    return view;
}

// 同样的事情用仿函数写一遍：好处是可以在头文件里当类型用，也能带状态
struct ByPerimeter {
    bool ascending = true;
    bool operator()(const Shape* lhs, const Shape* rhs) const {
        return ascending ? lhs->Perimeter() < rhs->Perimeter()
                         : lhs->Perimeter() > rhs->Perimeter();
    }
};

// RAII 文件包装器：把 03 章的结论用在这个项目里（析构自动关闭，异常也安全）
class ReportFile {
public:
    explicit ReportFile(const std::string& path) : path_(path), out_(path, std::ios::binary) {
        if (!out_.is_open()) {
            throw std::runtime_error("无法写入报表文件: " + path);
        }
    }
    ~ReportFile() { out_.close(); }                    // 不需要 try/catch，也不需要 finally

    ReportFile(const ReportFile&) = delete;
    ReportFile& operator=(const ReportFile&) = delete;

    void WriteLine(const std::string& text) { out_ << text << "\n"; }
    const std::string& path() const { return path_; }

private:
    std::string path_;
    std::ofstream out_;
};

// ===========================================================================
// 演示入口
// ===========================================================================
void PrintShapeTable(const std::vector<std::unique_ptr<Shape>>& shapes) {
    std::cout << "    +----+------------+------------+------------+\n";
    std::cout << "    |  # | 名称       | 面积       | 周长       |\n";
    std::cout << "    +----+------------+------------+------------+\n";
    std::cout << std::fixed << std::setprecision(3);
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        const Shape& s = *shapes[i];                   // 引用：绝不按值取，否则切片（见 06 章）
        std::cout << "    | " << std::setw(2) << (i + 1) << " | " << std::setw(10) << std::left
                  << s.Name() << std::right << " | " << std::setw(10) << s.Area() << " | "
                  << std::setw(10) << s.Perimeter() << " |\n";
    }
    std::cout << "    +----+------------+------------+------------+\n";
}

void DemoBuildAndDescribe() {
    std::cout << "==== 1/2/4. 建立图形集合并打印（多态 + operator<<） ====\n";
    std::vector<std::unique_ptr<Shape>> shapes;
    shapes.push_back(std::make_unique<Circle>(1.0));
    shapes.push_back(std::make_unique<Rectangle>(3.0, 4.0));
    shapes.push_back(std::make_unique<Triangle>(3.0, 4.0, 5.0));
    shapes.push_back(std::make_unique<Circle>(2.5));
    shapes.push_back(std::make_unique<Rectangle>(2.0, 2.0));

    PrintShapeTable(shapes);
    std::cout << "    用 operator<< 逐个打印（等价于 Describe）：\n";
    for (const auto& s : shapes) {
        std::cout << "      " << *s << "\n";            // 一次虚调用 + 一次 operator<<
    }
    std::cout << "    验证两个已知结果：\n";
    std::cout << "      边长为 3,4,5 的直角三角形面积应为 6.000，实际 = "
              << shapes[2]->Area() << "，NearlyEqual 判定: "
              << (NearlyEqual(shapes[2]->Area(), 6.0) ? "一致" : "不一致") << "\n";
    std::cout << "      半径 2.5 的圆面积应为 " << (3.14159265358979323846 * 2.5 * 2.5)
              << "，实际 = " << shapes[3]->Area() << "\n";
    std::cout << "    工程要点：\n";
    std::cout << "      - 遍历时必须用 const Shape& 或 Shape*；写成 Shape s = *shapes[i] 会切片；\n";
    std::cout << "      - 所有成员函数都是 const，说明「测量」不改变图形本身（逻辑常量性）。\n";
    PrintLine();
}

void DemoStatistics() {
    std::cout << "==== 3/5. 统计与排序 ====\n";
    std::vector<std::unique_ptr<Shape>> shapes;
    shapes.push_back(std::make_unique<Circle>(1.0));
    shapes.push_back(std::make_unique<Rectangle>(3.0, 4.0));
    shapes.push_back(std::make_unique<Triangle>(3.0, 4.0, 5.0));
    shapes.push_back(std::make_unique<Circle>(2.5));
    shapes.push_back(std::make_unique<Rectangle>(2.0, 2.0));

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "    图形个数 = " << shapes.size() << "\n";
    std::cout << "    总面积   = " << TotalArea(shapes) << "\n";
    std::cout << "    总周长   = " << TotalPerimeter(shapes) << "\n";

    std::cout << "    按面积升序（lambda 比较器）：\n";
    for (const Shape* s : SortedByArea(shapes)) {
        std::cout << "      " << std::setw(9) << s->Area() << "  " << s->Name() << "\n";
    }

    std::cout << "    按周长降序（仿函数比较器 ByPerimeter{false}）：\n";
    std::vector<const Shape*> by_perimeter;
    for (const auto& s : shapes) {
        by_perimeter.push_back(s.get());
    }
    std::sort(by_perimeter.begin(), by_perimeter.end(), ByPerimeter{false});
    for (const Shape* s : by_perimeter) {
        std::cout << "      " << std::setw(9) << s->Perimeter() << "  " << s->Name() << "\n";
    }

    std::cout << "    为什么排序的是「指针」而不是「对象」：\n";
    std::cout << "      1. Shape 是抽象类，根本无法按值拷贝；\n";
    std::cout << "      2. 即使能拷贝，多态对象按值拷贝会切片；\n";
    std::cout << "      3. 排指针只搬 8 字节，不碰对象本身，快且不产生副作用。\n";
    std::cout << "    lambda vs 仿函数怎么选：\n";
    std::cout << "      - 就地写、用完即弃 => lambda（99% 的场景）；\n";
    std::cout << "      - 需要带状态、需要在多处复用、需要当模板参数 => 仿函数结构体。\n";
    PrintLine();
}

void DemoFactoryAndErrors() {
    std::cout << "==== 附 1. 用工厂构造 + 错误处理 ====\n";
    std::vector<std::unique_ptr<Shape>> shapes;
    struct Request {
        std::string kind;
        std::vector<double> args;
    };
    const std::vector<Request> requests{
        {"circle", {2.0}},
        {"rect", {5.0, 2.0}},
        {"triangle", {1.0, 1.0, 10.0}},     // 违反三角不等式 => 构造函数抛异常
        {"hexagon", {1.0}},                 // 工厂不认识 => 返回 nullptr
        {"circle", {-1.0}},                 // 半径非法 => 构造函数抛异常
    };
    for (const Request& req : requests) {
        try {
            auto shape = MakeShape(req.kind, req.args);
            if (shape == nullptr) {
                std::cout << "    " << req.kind << " -> 工厂返回 nullptr（未知类型）\n";
                continue;
            }
            std::cout << "    " << req.kind << " -> " << *shape << "\n";
            shapes.push_back(std::move(shape));      // 所有权转移进容器
        } catch (const std::invalid_argument& e) {
            std::cout << "    " << req.kind << " -> 构造被拒绝: " << e.what()
                      << "（非法对象根本造不出来）\n";
        }
    }
    std::cout << "    最终容器里有 " << shapes.size() << " 个合法图形。\n";
    std::cout << "    这个模式的三个好处：\n";
    std::cout << "      1. 校验集中在构造函数里，任何路径都绕不过去（不变量有保证）；\n";
    std::cout << "      2. 失败用异常报告，调用方不可能忘记检查（不像错误码可以忽略）；\n";
    std::cout << "      3. shape 是 unique_ptr => 中途抛异常也不会泄漏（RAII）。\n";
    PrintLine();
}

void DemoRaiiReport() {
    std::cout << "==== 附 2. 用 RAII 写报表文件 ====\n";
    std::vector<std::unique_ptr<Shape>> shapes;
    shapes.push_back(std::make_unique<Circle>(1.0));
    shapes.push_back(std::make_unique<Rectangle>(3.0, 4.0));
    shapes.push_back(std::make_unique<Triangle>(3.0, 4.0, 5.0));

    const std::string path = ".shapes_report.txt";
    {
        ReportFile report(path);                      // 构造函数打开文件
        std::ostringstream header;
        header << std::fixed << std::setprecision(3);
        header << "图形报表：共 " << shapes.size() << " 个，总面积 " << TotalArea(shapes);
        report.WriteLine(header.str());
        for (const Shape* s : SortedByArea(shapes)) {
            report.WriteLine("  " + s->Describe());
        }
        std::cout << "    已写入 " << path << "（离开作用域时析构函数自动 close）\n";
    }

    // 读回来验证：用另一个 RAII 对象（std::ifstream）读取
    {
        std::ifstream in(path);
        if (in) {
            std::cout << "    读回内容：\n";
            std::string line;
            while (std::getline(in, line)) {
                std::cout << "      " << line << "\n";
            }
        }
        std::cout << "    【错误写法提醒】如果不用 RAII 而是手写 open/close，\n";
        std::cout << "      任何提前 return 或异常都会让文件句柄留在打开状态（Windows 下甚至删不掉）。\n";
    }
    std::remove(path.c_str());
    PrintLine();
}

void DemoObjectLifetime() {
    std::cout << "==== 附 3. 生命周期自检（RAII 是否真的配对） ====\n";
    const int created_before = g_shapes_created;
    const int destroyed_before = g_shapes_destroyed;
    {
        std::vector<std::unique_ptr<Shape>> shapes;
        shapes.push_back(std::make_unique<Circle>(1.0));
        shapes.push_back(std::make_unique<Rectangle>(2.0, 3.0));
        std::cout << "    容器内有 " << shapes.size() << " 个图形\n";
        shapes.clear();                               // 手动清空 => unique_ptr 立刻析构对象
        std::cout << "    clear() 之后，容器 size = " << shapes.size() << "\n";
        shapes.push_back(std::make_unique<Triangle>(3.0, 4.0, 5.0));
        std::cout << "    重新加入一个三角形\n";
    }                                                 // 离开作用域，剩余的 unique_ptr 自动释放
    const int created = g_shapes_created - created_before;
    const int destroyed = g_shapes_destroyed - destroyed_before;
    std::cout << "    本轮构造 = " << created << "，本轮析构 = " << destroyed << "\n";
    std::cout << "    配对结果: " << (created == destroyed ? "完全配对，无泄漏" : "不配对，存在泄漏") << "\n";
    std::cout << "    累计：构造 " << g_shapes_created << "，析构 " << g_shapes_destroyed << "\n";
    std::cout << "    这就是「接口与实现分离 + unique_ptr 存储」带来的确定性：\n";
    std::cout << "      使用方永远不需要写 delete，也不可能漏写。\n";
    PrintLine();
}

void DemoInterfaceDesignNotes() {
    std::cout << "==== 附 4. 这个库的接口设计复盘 ====\n";
    std::cout << "    1. 抽象基类 Shape 只放「所有图形都能承诺」的三件事：\n";
    std::cout << "       Area / Perimeter / Name；没有任何数据成员，也没有 protected 状态。\n";
    std::cout << "    2. 虚析构 + 禁止拷贝：明确「这些对象由 unique_ptr 独占持有」。\n";
    std::cout << "    3. 所有查询都标 const：接口层面就说明了「测量不修改对象」。\n";
    std::cout << "    4. 自由函数（TotalArea / SortedByArea）而不是塞进成员函数：\n";
    std::cout << "       算法与数据解耦，将来想加「按颜色排序」不必改 Shape 的头文件。\n";
    std::cout << "    5. 对象校验放在构造函数里：非法图形无法存在，后续代码不必到处判断。\n";
    std::cout << "    6. 工厂返回 unique_ptr<Shape>：调用方不需要知道具体类型（这正是多态的价值）。\n";
    std::cout << "    扩展练习（建议自己动手改一遍）：\n";
    std::cout << "      - 加一个 Square 类：思考它该继承 Rectangle 还是 Shape（提示：正方形确实是矩形，\n";
    std::cout << "        但可变的宽高会让「正方形」的不变量失效 —— 这就是经典的 LSP 问题）；\n";
    std::cout << "      - 加一个 Draw() 虚函数，体会「改接口要动所有派生类」的代价；\n";
    std::cout << "      - 把 SortedByArea 改成模板版，让它同时支持 vector<Shape*> 与 vector<Circle>。\n";
    PrintLine();
}

}  // namespace

int main() {
    std::cout << "################ 09 综合小项目：图形库 ################\n\n";
    std::cout << std::fixed << std::setprecision(3);
    DemoBuildAndDescribe();
    DemoStatistics();
    DemoFactoryAndErrors();
    DemoRaiiReport();
    DemoObjectLifetime();
    DemoInterfaceDesignNotes();

    std::cout << "总计：构造 " << g_shapes_created << " 个图形对象，析构 " << g_shapes_destroyed
              << " 个。\n";
    if (g_shapes_created == g_shapes_destroyed) {
        std::cout << "生命周期自检通过：没有泄漏，也没有重复释放。\n";
    } else {
        std::cout << "生命周期自检失败：构造与析构数量不一致。\n";
    }
    return 0;
}
