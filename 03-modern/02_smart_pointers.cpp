// ============================================================================
//  02_smart_pointers.cpp
// ----------------------------------------------------------------------------
//  演示主题：
//    1. unique_ptr：独占所有权、make_unique、自定义删除器、所有权转移
//    2. shared_ptr：引用计数、控制块、make_shared 一次分配 vs 两次分配
//    3. 循环引用：为什么裸用 shared_ptr 会内存泄漏，weak_ptr 怎么破环
//    4. weak_ptr 的正确用法：lock() / expired() / 缓存与观察者
//    5. enable_shared_from_this：为什么构造函数里不能调 shared_from_this
//    6. auto_ptr 为什么被废弃，以及 shared_ptr(ptr) 两次传同一裸指针的灾难
//    7. 选型决策：默认 unique_ptr，需要共享才 shared_ptr，永不裸 new/delete
//
//  关键结论：
//    - 智能指针解决的唯一核心问题是「所有权归属」：谁负责释放，什么时候释放。
//    - shared_ptr 的引用计数不是免费的：控制块是独立分配、计数是原子操作。
//    - shared_ptr 环形持有就是内存泄漏；能用 unique_ptr 表达的关系不要用 shared_ptr。
//    - 一个裸指针只能交给一个 shared_ptr 或 unique_ptr，交两次就是 double free。
// ============================================================================

#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------- 自定义删除器
// 模拟一个「句柄式」资源：整数 id 代表内核对象，需要显式 close
struct Handle {
    int value{-1};
};

void close_handle(Handle* h) {
    if (h != nullptr) {
        std::cout << "      [deleter] 关闭句柄 " << h->value << " 并释放对象\n";
        delete h;
    }
}

// 用 unique_ptr 管理 C 风格 FILE*：stdlib 已自带 default_delete<FILE> 特化
void file_demo() {
    std::cout << "  (b) 带状态的删除器：lambda 里记录日志 / 归还句柄池\n";
    std::unique_ptr<int, void (*)(int*)> p(new int(7), [](int* q) {
        std::cout << "      [deleter] lambda 删除器被调用，释放 " << *q << "\n";
        delete q;
    });
    std::cout << "      持有值 = " << *p << "，离开作用域自动释放\n";
}

// ---------------------------------------------------------------- 所有权转移
std::unique_ptr<std::string> make_greeting(const std::string& who) {
    // 推荐写法：make_unique + 直接返回，调用方零成本接收所有权
    return std::make_unique<std::string>("你好，" + who);
}

// 按值接收 unique_ptr 表示「我把所有权交给你」
void consume(std::unique_ptr<std::string> p) {
    std::cout << "      consume 收到: " << *p << "（函数结束时释放）\n";
}

// 按引用接收表示「我只是借用，不接管所有权」——这是接口设计的正确默认
void observe(const std::unique_ptr<std::string>& p) {
    std::cout << "      observe 借看: " << *p << "（不接管，调用方仍然拥有）\n";
}

// ---------------------------------------------------------------- 循环引用
struct NodeLeaky {
    std::string name;
    std::shared_ptr<NodeLeaky> next;  // 强引用成环 -> 计数永远不归零
    ~NodeLeaky() { std::cout << "      [dtor] NodeLeaky " << name << " 被销毁\n"; }
};

struct NodeFixed {
    std::string name;
    std::shared_ptr<NodeFixed> next;
    std::weak_ptr<NodeFixed> prev;  // 反向边用 weak_ptr 打破环
    ~NodeFixed() { std::cout << "      [dtor] NodeFixed " << name << " 被销毁\n"; }
};

struct Observer : std::enable_shared_from_this<Observer> {
    std::string name;
    // enable_shared_from_this 让成员函数能安全地把自己「再借出去」一份 shared_ptr
    std::shared_ptr<Observer> self() { return shared_from_this(); }
    ~Observer() { std::cout << "      [dtor] Observer " << name << " 被销毁\n"; }
};

// 错误示范：构造函数里调用 shared_from_this —— 运行期抛 std::bad_weak_ptr
struct BadSelf : std::enable_shared_from_this<BadSelf> {
    BadSelf() {
        try {
            // 此刻还没有任何 shared_ptr 拥有 this，控制块里 weak count == 0
            auto p = shared_from_this();
            (void)p;
        } catch (const std::bad_weak_ptr&) {
            std::cout << "      [catch] 构造函数里调用 shared_from_this 抛出了 std::bad_weak_ptr\n";
        }
    }
};

// ---------------------------------------------------------------- weak_ptr 观察者
class CachedResource {
public:
    explicit CachedResource(std::string id) : id_(std::move(id)) {
        std::cout << "      [new  ] CachedResource " << id_ << " 创建（昂贵的构造）\n";
    }
    ~CachedResource() { std::cout << "      [dtor ] CachedResource " << id_ << " 销毁\n"; }
    const std::string& id() const { return id_; }

private:
    std::string id_;
};

// ---------------------------------------------------------------- 性能实测
// 目标：对比 make_shared（1 次分配）与 shared_ptr<T>(new T)（2 次分配）
// 说明：Debug 下 CRT 的堆调试开销会放大绝对数字；Release 下趋势更明显。
template <typename Fn>
double time_ms(Fn&& fn, int repeats) {
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < repeats; ++i) {
        volatile std::size_t sink = fn().size();
        (void)sink;
    }
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

struct Payload {
    int a{1};
    int b{2};
    int c{3};
    double d{4.0};
};

std::vector<std::shared_ptr<Payload>> build_with_make_shared(int n) {
    std::vector<std::shared_ptr<Payload>> v;
    v.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        v.push_back(std::make_shared<Payload>());
    }
    return v;
}

std::vector<std::shared_ptr<Payload>> build_with_new(int n) {
    std::vector<std::shared_ptr<Payload>> v;
    v.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        // 两次分配：一次对象、一次控制块；且异常安全路径更脆
        v.push_back(std::shared_ptr<Payload>(new Payload()));
    }
    return v;
}

int main() {
    std::cout << "==== 1. unique_ptr：独占所有权 ====\n";
    {
        auto p = std::make_unique<std::string>("独占数据");
        std::cout << "  *p = " << *p << ", use_count 概念不适用于 unique_ptr（它不可拷贝）\n";

        // auto q = p;             // 编译错误：unique_ptr 删除了拷贝构造，这才是我们要的
        auto q = std::move(p);     // 显式转移所有权
        std::cout << "  move 之后 p == nullptr ? " << (p == nullptr ? "是" : "否")
                  << ", *q = " << *q << "\n";

        std::cout << "  用 release() 放弃所有权（要手工 delete，谨慎）:\n";
        std::string* raw = q.release();
        std::cout << "    release 后 q == nullptr ? " << (q == nullptr ? "是" : "否")
                  << ", 裸指针仍需手工管理\n";
        delete raw;  // 这就是 release 的代价：RAII 断链了
    }

    std::cout << "\n==== 2. unique_ptr 的自定义删除器与所有权传递 ====\n";
    {
        std::cout << "  (a) 函数指针 / 无捕获 lambda 删除器（不占额外空间）:\n";
        std::unique_ptr<Handle, void (*)(Handle*)> h(new Handle{42}, &close_handle);
        std::cout << "      句柄值 = " << h->value
                  << ", sizeof(unique_ptr) = " << sizeof(h)
                  << "（含一个函数指针）\n";
        file_demo();

        std::cout << "  (c) unique_ptr 作函数返回值（工厂的现代写法）:\n";
        auto greeting = make_greeting("world");
        observe(greeting);            // 借用
        consume(std::move(greeting)); // 交出所有权
        std::cout << "      consume 返回后 greeting == nullptr ? "
                  << (greeting == nullptr ? "是" : "否") << "\n";
    }

    std::cout << "\n==== 3. shared_ptr：引用计数与控制块 ====\n";
    {
        auto sp = std::make_shared<std::string>("共享数据");
        std::cout << "  use_count = " << sp.use_count()
                  << "（1 表示只有 sp 自己持有）\n";
        {
            auto sp2 = sp;  // 拷贝：计数 +1（原子操作）
            std::cout << "  拷贝一份后 use_count = " << sp.use_count() << "\n";
            // weak_ptr 不计入 use_count，只增加控制块里的 weak count
            std::weak_ptr<std::string> wp = sp;
            std::cout << "  再建 weak_ptr 后 use_count 仍为 " << sp.use_count()
                      << "（weak 只影响 weak count）\n";
            std::cout << "  wp.expired() = " << (wp.expired() ? "true" : "false") << "\n";
            // 从 weak 提升到 shared 的唯一安全方式：lock()
            if (auto locked = wp.lock()) {
                std::cout << "  wp.lock() 成功，*locked = " << *locked
                          << ", use_count = " << sp.use_count() << "\n";
            }
        }
        std::cout << "  内层作用域结束后 use_count = " << sp.use_count() << "\n";
        std::cout << "  提示：use_count() 只应出现在日志/调试里，业务逻辑不要依赖它\n";
    }

    std::cout << "\n==== 4. 循环引用：shared_ptr 成环就是内存泄漏 ====\n";
    {
        std::cout << "  (a) 错误示范：next 用 shared_ptr 互相指\n";
        {
            auto a = std::make_shared<NodeLeaky>();
            auto b = std::make_shared<NodeLeaky>();
            a->name = "A";
            b->name = "B";
            a->next = b;
            b->next = a;  // 环形成
            std::cout << "      a.use_count = " << a.use_count()
                      << ", b.use_count = " << b.use_count() << "（各为 2）\n";
        }
        std::cout << "      作用域已退出，但没有 [dtor] 输出 -> 两个对象泄漏了\n";

        std::cout << "  (b) 正确示范：反向边改用 weak_ptr\n";
        {
            auto a = std::make_shared<NodeFixed>();
            auto b = std::make_shared<NodeFixed>();
            a->name = "A";
            b->name = "B";
            a->next = b;
            b->prev = a;  // weak 不增加强计数
            std::cout << "      a.use_count = " << a.use_count()
                      << ", b.use_count = " << b.use_count() << "\n";
            if (auto pa = b->prev.lock()) {
                std::cout << "      通过 weak_ptr 访问前驱: " << pa->name << "\n";
            }
        }
        std::cout << "      这次能看到两个 [dtor]，环被打破\n";
        std::cout << "      工程规则：只要存在「双向 / 回指」关系，就有一条边必须是 weak_ptr\n";
    }

    std::cout << "\n==== 5. weak_ptr 缓存与观察者 ====\n";
    {
        std::weak_ptr<CachedResource> cache;  // 缓存持有 weak，不延长生命期
        {
            auto res = std::make_shared<CachedResource>("config.txt");
            cache = res;
            if (auto hit = cache.lock()) {
                std::cout << "      缓存命中: " << hit->id()
                          << ", use_count = " << hit.use_count() << "\n";
            }
        }
        std::cout << "      资源拥有者离开作用域后，cache.expired() = "
                  << (cache.expired() ? "true" : "false") << "\n";
        if (auto miss = cache.lock()) {
            std::cout << "      命中了: " << miss->id() << "\n";
        } else {
            std::cout << "      缓存失效，需要重新构造 -> 这就是 weak_ptr 缓存的标准套路\n";
        }
    }

    std::cout << "\n==== 6. enable_shared_from_this ====\n";
    {
        auto o = std::make_shared<Observer>();
        o->name = "observer-1";
        auto again = o->self();  // 内部正确共享同一个控制块
        std::cout << "  self() 之后 use_count = " << o.use_count()
                  << "（两个 shared_ptr 指向同一控制块）\n";
        (void)again;

        std::cout << "  构造函数里调用 shared_from_this 的结果：\n";
        auto bad = std::make_shared<BadSelf>();
        (void)bad;
        std::cout << "  正确做法：构造函数只做初始化，把「需要 self 的逻辑」放到 init()/start()\n";
        std::cout << "  或者用工厂函数：先 make_shared，再调用两阶段初始化\n";
    }

    std::cout << "\n==== 7. auto_ptr 为什么被废弃，以及 shared_ptr(ptr) 的陷阱 ====\n";
    {
        std::cout << "  auto_ptr 的致命问题：拷贝语义实际是「转移」\n";
        std::cout << "    std::auto_ptr<T> b = a;  // a 变成空指针！\n";
        std::cout << "    于是把 auto_ptr 放进 std::vector 就是在编译期埋雷：\n";
        std::cout << "    容器拷贝元素时会悄悄掏空源对象，随后解引用就是 UB。\n";
        std::cout << "    C++11 用 unique_ptr（拷贝被删除 -> 编译期报错）取代它，C++17 正式移除。\n";

        std::cout << "  同一裸指针交给两个 shared_ptr 会 double free：\n";
        int* raw = new int(5);
        std::shared_ptr<int> s1(raw);
        // std::shared_ptr<int> s2(raw);  // 若打开：两次独立控制块 -> 双重释放
        std::cout << "    s1.use_count = " << s1.use_count()
                  << "（即使是同一 raw，两个独立控制块也各算各的）\n";
        std::cout << "    所以：new 出来的指针只交给一个智能指针，优先用 make_shared/make_unique\n";
        std::cout << "    另外 new int(5) 不能写成 new int[5] 再用 shared_ptr 管理，数组要用 shared_ptr<T[]>\n";
    }

    std::cout << "\n==== 8. 性能实测：make_shared vs shared_ptr<T>(new T) ====\n";
    {
        constexpr int kRepeats = 3000;
        constexpr int kSize = 200;

        const double t_make = time_ms([] { return build_with_make_shared(kSize); }, kRepeats);
        const double t_new = time_ms([] { return build_with_new(kSize); }, kRepeats);

        std::cout << "  每次创建 " << kSize << " 个对象，重复 " << kRepeats << " 次\n";
        std::cout << "  make_shared      : " << t_make << " ms（对象 + 控制块一次分配）\n";
        std::cout << "  shared_ptr(new)  : " << t_new << " ms（对象、控制块两次分配）\n";
        if (t_make > 0.0) {
            std::cout << "  比值              : " << (t_new / t_make) << " 倍\n";
        }
        std::cout << "  结论：\n";
        std::cout << "    - make_shared 少一次堆分配、少一次原子计数初始化，且异常安全更好\n";
        std::cout << "    - 唯一代价：对象和控制块同块内存，weak_ptr 存活时对象内存要等到 weak 归零\n";
        std::cout << "    - 数字随机器 / 配置 / Debug Release 变化很大（Debug 堆调试会放大差距），趋势稳定\n";
    }

    std::cout << "\n==== 9. 选型决策表 ====\n";
    std::cout << "  需求                                  推荐\n";
    std::cout << "  ------------------------------------  ------------------------------\n";
    std::cout << "  单一所有者，生命期随作用域            unique_ptr（默认首选）\n";
    std::cout << "  需要转移所有权但不需要共享            unique_ptr + std::move\n";
    std::cout << "  真正的多方共享同一对象                shared_ptr\n";
    std::cout << "  只观察、不参与生命期决策              weak_ptr + lock()\n";
    std::cout << "  需要打破 shared_ptr 环                反向边用 weak_ptr\n";
    std::cout << "  非拥有型参数                          T& / const T& / T*（明确不拥有）\n";
    std::cout << "  运行时多态但独占                      unique_ptr<Base>（多态删除靠虚析构）\n";
    std::cout << "  ------------------------------------  ------------------------------\n";
    std::cout << "  头号原则：代码里不出现裸 new / delete；出现即说明所有权没想清楚\n";
    std::cout << "  次号原则：shared_ptr 是「共享所有权」的证据，不是「图省事的指针」\n";
    return 0;
}
