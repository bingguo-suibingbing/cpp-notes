// ============================================================================
//  03_lambda_and_functional.cpp
// ----------------------------------------------------------------------------
//  演示主题：
//    1. lambda 语法拆解：[捕获](参数) mutable -> 返回类型 { 函数体 }
//    2. 按值 [=] / 按引用 [&] 捕获的真正风险：悬垂引用
//    3. C++14 初始化捕获 [x = std::move(y)]：把 move-only 对象搬进闭包
//    4. mutable lambda：为什么按值捕获的变量默认在 lambda 里是 const
//    5. 泛型 lambda（auto 参数）与它的模板本质
//    6. std::function 的开销：类型擦除 + 可能堆分配（附实测）
//    7. 实战：sort 比较器、find_if、accumulate
//    8. 捕获 this 与 C++17 的 [*this]（按值拷贝整个对象）
//    9. lambda 当作用域退出守卫（scope guard）
//
//  关键结论：
//    - lambda 的捕获列表决定闭包对象的成员：按值捕获是成员拷贝，按引用捕获是成员引用。
//    - 「按引用捕获 + 闭包活得比被捕获变量久」= 悬垂，是 lambda 最常见的致命 bug。
//    - std::function 不是零成本抽象：它要类型擦除，可能堆分配，调用也可能间接跳转。
//      能用 auto/模板就用，只有「必须存进容器 / 必须跨 ABI 边界」时才用 std::function。
//    - 存储 lambda 时一律显式列出捕获项，不要用 [=]/[&]，[=] 在成员函数里还会隐式捕获 this。
// ============================================================================

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------- 悬垂演示
// 返回捕获了局部变量引用的 lambda：闭包活着，被引用的对象已经死了
std::function<int()> make_dangling_lambda() {
    int local = 42;
    // [&local] 让闭包持有一个指向栈变量的引用，函数一返回就是悬垂引用
    return [&local]() { return local; };
}

// 正确做法：按值捕获（或 C++14 起用初始化捕获把值搬进去）
std::function<int()> make_safe_lambda() {
    int local = 42;
    return [local]() { return local; };
}

// ---------------------------------------------------------------- 泛型 lambda
// 泛型 lambda 本质是「带模板 operator() 的匿名类」，编译期实例化，零开销
template <typename Fn, typename T>
auto apply_twice(Fn fn, T value) {
    return fn(fn(value));
}

// ---------------------------------------------------------------- 作用域退出守卫
// 最直白的实现：一个 bool 记住「是否已取消」，析构里调用回调。
// 注意为什么不需要（也不应该）在这里用 unique_ptr：守卫本身不持有资源，
// 它持有的是「一段必须在退出时执行的代码」。真正需要 unique_ptr 的场景，
// 用 unique_ptr 的自定义删除器写会更啰嗦，而且 unique_ptr 对空指针不调用删除器，
// 容易被误用成「守卫失效」。
class ScopeGuard {
public:
    explicit ScopeGuard(std::function<void()> on_exit) : on_exit_(std::move(on_exit)) {}
    ~ScopeGuard() {
        if (armed_ && on_exit_) {
            on_exit_();
        }
    }
    // 禁止拷贝：守卫只能有一个主人，否则会执行两次
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    // 移动：把「武装状态」转移给新对象，源对象随之失效
    ScopeGuard(ScopeGuard&& other) noexcept
        : on_exit_(std::move(other.on_exit_)), armed_(other.armed_) {
        other.armed_ = false;
    }
    ScopeGuard& operator=(ScopeGuard&& other) noexcept {
        if (this != &other) {
            on_exit_ = std::move(other.on_exit_);
            armed_ = other.armed_;
            other.armed_ = false;
        }
        return *this;
    }

    // 取消守卫：析构时不再执行回调
    void dismiss() noexcept { armed_ = false; }

private:
    std::function<void()> on_exit_;
    bool armed_{true};
};

// ---------------------------------------------------------------- 性能实测
// 对比 std::function / 函数指针 / 模板（auto）三种「传可调用对象」的方式
constexpr int kOpCount = 2000;

using OpFnPtr = int (*)(int);

int add_three(int x) { return x + 3; }

int run_std_function(const std::function<int(int)>& fn, int rounds) {
    int acc = 0;
    for (int i = 0; i < rounds; ++i) {
        acc = fn(acc % 1000);
    }
    return acc;
}

int run_fn_pointer(OpFnPtr fn, int rounds) {
    int acc = 0;
    for (int i = 0; i < rounds; ++i) {
        acc = fn(acc % 1000);
    }
    return acc;
}

// 模板版本：每个 lambda 类型在编译期单态化，可以内联
template <typename Fn>
int run_template(Fn fn, int rounds) {
    int acc = 0;
    for (int i = 0; i < rounds; ++i) {
        acc = fn(acc % 1000);
    }
    return acc;
}

template <typename Fn>
double time_ms(Fn&& fn, int repeats) {
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < repeats; ++i) {
        volatile int sink = fn();
        (void)sink;
    }
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

// 大闭包：捕获 8 个 int，超过 std::function 常见的小对象缓冲（MSVC 为 16 字节左右）
struct BigClosure {
    int a{1};
    int b{2};
    int c{3};
    int d{4};
    int e{5};
    int f{6};
    int g{7};
    int h{8};
    int operator()(int x) const { return x + a + b + c + d + e + f + g + h; }
};

int main() {
    std::cout << "==== 1. lambda 语法拆解 ====\n";
    {
        int base = 100;
        //            capture    params  trailing-return   body
        auto add = [base](int x) -> int { return base + x; };
        std::cout << "  [base](int x) -> int { return base + x; }  -> add(1) = " << add(1) << "\n";
        std::cout << "  sizeof(闭包对象) = " << sizeof(add)
                  << "（按值捕获的 base 是闭包的一个成员）\n";

        auto no_capture = [](int x) { return x * 2; };
        std::cout << "  sizeof(无捕获闭包) = " << sizeof(no_capture)
                  << "，且可以隐式转换成函数指针：\n";
        int (*fp)(int) = no_capture;  // 无捕获 lambda 可转函数指针（可用于 C 回调）
        std::cout << "    fp(21) = " << fp(21) << "\n";
    }

    std::cout << "\n==== 2. 按值 [=] / 按引用 [&] 捕获与悬垂 ====\n";
    {
        int x = 1;
        int y = 2;
        auto by_value = [=] { return x + y; };
        auto by_ref = [&] { return x + y; };
        x = 10;
        std::cout << "  改 x 之后：by_value() = " << by_value()
                  << "（捕获瞬间的快照）, by_ref() = " << by_ref() << "（看到最新值）\n";
        std::cout << "  注意：标准里 [=] 已经没有意义了（C++20 弃用隐式捕获 this），"
                     "工程规范应当显式写 [x, y] / [&x, &y]\n";

        std::cout << "  危险示例：返回捕获局部变量引用的 lambda\n";
        auto dangling = make_dangling_lambda();
        std::cout << "    调用悬垂 lambda 得到 " << dangling()
                  << "（本次侥幸读到旧栈内容，行为未定义，不可依赖）\n";
        auto safe = make_safe_lambda();
        std::cout << "    按值捕获的版本得到 " << safe() << "（永远正确）\n";
        std::cout << "  工程规则：只要闭包会被存储、返回、投递到别的线程/异步回调，"
                     "就绝不要按引用捕获\n";
    }

    std::cout << "\n==== 3. C++14 初始化捕获 [x = std::move(y)] ====\n";
    {
        auto buffer = std::make_unique<std::vector<int>>(std::vector<int>{1, 2, 3, 4});
        std::cout << "  捕获前 buffer 是否有效: " << (buffer ? "是" : "否") << "\n";
        // move-only 类型无法被 [=] 拷贝捕获，初始化捕获才能把它搬进闭包
        auto sum_later = [data = std::move(buffer)] {
            return std::accumulate(data->begin(), data->end(), 0);
        };
        std::cout << "  捕获后 buffer 是否有效: " << (buffer ? "是" : "否")
                  << "（所有权已经转移进闭包）\n";
        std::cout << "  闭包内部求和 = " << sum_later() << "\n";
        std::cout << "  [] 里还可以做任意表达式： [n = 6 * 7]() { return n; }\n";
        auto forty_two = [n = 6 * 7] { return n; };
        std::cout << "    结果 = " << forty_two() << "\n";
    }

    std::cout << "\n==== 4. mutable lambda ====\n";
    {
        int counter = 0;
        // 按值捕获的成员在 operator() 里默认是 const，想改必须写 mutable
        auto bump = [counter]() mutable { return ++counter; };
        std::cout << "  bump() = " << bump() << ", bump() = " << bump()
                  << "（改的是闭包自己的副本）\n";
        std::cout << "  外部 counter 仍然是 " << counter << "（这就是按值捕获）\n";
        auto bump_ref = [&counter] { return ++counter; };
        std::cout << "  bump_ref() = " << bump_ref() << "，外部 counter 变成 " << counter << "\n";
        std::cout << "  提示：mutable 让 lambda 不再是 const 可调用，放进 const 上下文会编译失败\n";
    }

    std::cout << "\n==== 5. 泛型 lambda（auto 参数） ====\n";
    {
        auto twice = [](auto v) { return v + v; };
        std::cout << "  twice(3)      = " << twice(3) << "\n";
        std::cout << "  twice(2.5)    = " << twice(2.5) << "\n";
        std::cout << "  twice(std::string(\"ab\")) = " << twice(std::string("ab")) << "\n";
        std::cout << "  本质：编译器为每个实参类型生成一个 operator() 实例，"
                     "与函数模板等价\n";
        std::cout << "  apply_twice(twice, 5) = " << apply_twice(twice, 5) << "\n";

        // C++20 还可以用 explicit 模板参数列表约束参数
        auto only_int = []<typename T>(T v) { return static_cast<T>(v); };
        std::cout << "  C++20 显式模板参数：only_int(9) = " << only_int(9) << "\n";
    }

    std::cout << "\n==== 6. std::function 的开销（实测） ====\n";
    {
        constexpr int kRepeats = 3000;
        long long checksum = 0;

        const double t_std_function = time_ms(
            [&] {
                checksum += run_std_function([](int v) { return v + 3; }, kOpCount);
                return 0;
            },
            kRepeats);
        const double t_fn_pointer = time_ms(
            [&] {
                checksum += run_fn_pointer(&add_three, kOpCount);
                return 0;
            },
            kRepeats);
        const double t_template = time_ms(
            [&] {
                checksum += run_template([](int v) { return v + 3; }, kOpCount);
                return 0;
            },
            kRepeats);

        // 小闭包 vs 大闭包：超过内部缓冲就会堆分配
        const double t_small_capture = time_ms(
            [&] {
                int k = 3;
                checksum += run_std_function([k](int v) { return v + k; }, kOpCount);
                return 0;
            },
            kRepeats);
        const double t_big_capture = time_ms(
            [&] {
                BigClosure big;
                checksum += run_std_function(big, kOpCount);
                return 0;
            },
            kRepeats);

        std::cout << "  每次 " << kOpCount << " 次调用，重复 " << kRepeats << " 轮"
                  << "（checksum 防优化 = " << checksum << "）\n";
        std::cout << "  std::function（小闭包） : " << t_std_function << " ms\n";
        std::cout << "  函数指针                : " << t_fn_pointer << " ms\n";
        std::cout << "  模板 / auto             : " << t_template << " ms\n";
        std::cout << "  std::function（捕获 1 个 int）: " << t_small_capture << " ms\n";
        std::cout << "  std::function（捕获 8 个 int）: " << t_big_capture << " ms\n";
        std::cout << "  结论：\n";
        std::cout << "    - 模板版本能被内联，通常最快；函数指针次之；std::function 有间接调用开销\n";
        std::cout << "    - 闭包大于 std::function 的内部缓冲时会额外堆分配，构造/销毁明显变慢\n";
        std::cout << "    - Debug 下三者的绝对差距被放大，Release 下趋势不变但差距收窄\n";
        std::cout << "    - 结论不是「禁用 std::function」，而是「性能敏感的内循环别用它」\n";
    }

    std::cout << "\n==== 7. 实战：sort / find_if / accumulate ====\n";
    {
        struct Employee {
            std::string name;
            int age;
            double salary;
        };
        std::vector<Employee> staff{
            {"张三", 31, 21000.0},
            {"李四", 25, 18000.0},
            {"王五", 42, 35000.0},
            {"赵六", 25, 19500.0},
        };

        // 比较器：先按年龄升序，年龄相同按薪资降序
        std::sort(staff.begin(), staff.end(), [](const Employee& a, const Employee& b) {
            if (a.age != b.age) {
                return a.age < b.age;
            }
            return a.salary > b.salary;
        });
        std::cout << "  按（年龄升序，薪资降序）排序：\n";
        for (const auto& e : staff) {
            std::cout << "    " << e.name << " / " << e.age << " 岁 / " << e.salary << "\n";
        }

        // 注意：比较器必须满足严格弱序，用 <= 会触发运行期断言甚至未定义行为
        auto first_senior = std::find_if(staff.begin(), staff.end(), [](const Employee& e) {
            return e.age >= 40;
        });
        if (first_senior != staff.end()) {
            std::cout << "  第一个 40 岁以上的是 " << first_senior->name << "\n";
        }

        // accumulate 的 lambda 必须是「无副作用 / 可结合」的纯函数式写法才安全
        const double payroll = std::accumulate(
            staff.begin(), staff.end(), 0.0,
            [](double acc, const Employee& e) { return acc + e.salary; });
        std::cout << "  薪资总和 = " << payroll << "\n";

        const auto count_of = std::count_if(staff.begin(), staff.end(),
                                            [](const Employee& e) { return e.age == 25; });
        std::cout << "  25 岁人数 = " << count_of << "\n";
    }

    std::cout << "\n==== 8. 捕获 this 与 [*this] ====\n";
    {
        struct Widget {
            std::string label{"widget"};
            int value{7};

            // 危险写法：捕获 this 指针。若对象先死、lambda 后活 -> 悬垂 this
            auto risky_later() const {
                return [this] { return label + "/" + std::to_string(value); };
            }

            // C++17 写法：[*this] 把整个对象按值拷进闭包，对象死了也安全
            auto safe_later() const {
                return [*this] { return label + "/" + std::to_string(value); };
            }
        };

        std::function<std::string()> risky;
        std::function<std::string()> safe;
        {
            Widget w;
            risky = w.risky_later();
            safe = w.safe_later();
        }  // w 在这里销毁
        std::cout << "  [*this] 版本（安全）: " << safe() << "\n";
        std::cout << "  [this]  版本：对象已销毁，此调用是未定义行为，"
                     "所以这里不实际调用它，只说明风险\n";
        std::cout << "  规则：只要闭包可能比对象活得久，就用 [*this] 或按值捕获需要的成员\n";
    }

    std::cout << "\n==== 9. lambda 作用域退出守卫 ====\n";
    {
        std::cout << "  进入作用域\n";
        ScopeGuard guard([] { std::cout << "    [guard] 作用域退出，执行清理（无论正常返回还是异常）\n"; });
        std::cout << "  业务代码执行中\n";

        {
            std::cout << "  嵌套作用域：提前 dismiss 取消守卫\n";
            ScopeGuard inner([] { std::cout << "    [guard] 这一行不会打印\n"; });
            inner.dismiss();
        }
        std::cout << "  离开作用域\n";
    }

    std::cout << "\n==== 小结 ====\n";
    std::cout << "  lambda = 编译器生成的匿名类 + 捕获列表变成成员 + operator()\n";
    std::cout << "  捕获方式决定生命期语义：按值=拥有副本，按引用=借用，[*this]=拷贝对象\n";
    std::cout << "  std::function 是「类型擦除的容器」，不是「更快的函数指针」\n";
    return 0;
}
