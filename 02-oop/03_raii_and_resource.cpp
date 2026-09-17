// ============================================================================
// 03_raii_and_resource.cpp
// 演示主题：
//   1. RAII 的核心：构造获取资源，析构释放资源 —— C++ 最核心的资源管理思想
//   2. 手写 RAII 文件包装器（FileHandle）
//   3. 手写 RAII 锁守卫（LockGuard）与 RAII 内存守卫（BufferGuard）
//   4. 异常抛出时栈展开，析构函数照样执行 —— 对比 Java / C# 的 finally
//   5. 为什么析构函数不该抛异常（析构函数默认是 noexcept，抛了直接 terminate）
//   6. std::lock_guard / std::unique_ptr 就是标准库给的 RAII 实现
//   7. 「构造函数失败只能抛异常」与两阶段构造的取舍
//
// 关键结论：
//   资源的生命周期绑定到对象的生命周期，就不再需要「手动配对释放」；
//   异常安全不是靠 try/finally 堆出来的，而是靠 RAII 成员的析构天然获得的。
// ============================================================================

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

int g_file_open_count = 0;     // 当前打开的文件数
int g_live_guards = 0;         // 当前存活的守卫对象数
int g_live_buffers = 0;        // 当前存活的堆缓冲数

void PrintLine() { std::cout << "--------------------------------------------------\n"; }

// ===========================================================================
// 2. 手写 RAII 文件包装器
//    用 std::ofstream 而不是 std::fopen：C 风格的 fopen / tmpfile 在 MSVC 下会触发
//    C4996「不安全函数」警告，在 /WX 下直接变成编译错误；
//    而 ofstream 本身就已经是 RAII 类型（析构自动 close），正好也说明了这个思想。
// ===========================================================================
class FileHandle {
public:
    // 构造函数负责「获取资源」；获取失败通过抛异常报告，绝不留一个半死不活的对象
    explicit FileHandle(const std::string& path) : path_(path), out_(path, std::ios::binary) {
        if (!out_.is_open()) {
            throw std::runtime_error("无法打开文件: " + path);
        }
        ++g_file_open_count;
    }

    // 析构函数负责「释放资源」，并且必须保证不抛异常
    ~FileHandle() { Close(); }

    // 资源只能有一个主人 => 禁止拷贝。这是 RAII 类型的默认姿势。
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    // 但所有权可以转移 => 提供移动
    FileHandle(FileHandle&& other) noexcept : path_(std::move(other.path_)), out_(std::move(other.out_)) {
        other.moved_from_ = true;        // 源对象标记为「已搬走」，它的析构什么也不做
    }

    FileHandle& operator=(FileHandle&& other) noexcept {
        if (this != &other) {
            Close();                     // 先释放自己已有的资源
            path_ = std::move(other.path_);
            out_ = std::move(other.out_);
            other.moved_from_ = true;
        }
        return *this;
    }

    bool is_open() const { return out_.is_open(); }

    void Write(std::string_view text) {
        if (!is_open()) {
            throw std::logic_error("文件已关闭，不能再写");
        }
        out_ << text;
    }

    // 显式释放：允许提前归还资源，之后的析构是幂等的 no-op
    void Close() noexcept {
        if (out_.is_open()) {
            out_.close();
            --g_file_open_count;         // 计数回退，用来验证没有泄漏
        }
    }

    const std::string& path() const { return path_; }

private:
    std::string path_;
    std::ofstream out_;
    bool moved_from_ = false;
};

// ===========================================================================
// 3-a. 手写 RAII 锁守卫
// ===========================================================================
class LockGuard {
public:
    explicit LockGuard(std::mutex& m) : mtx_(&m) {
        mtx_->lock();
        ++g_live_guards;
    }

    ~LockGuard() {
        // 析构函数里释放资源，即使中途抛异常也会执行到（栈展开）
        mtx_->unlock();
        --g_live_guards;
    }

    LockGuard(const LockGuard&) = delete;             // 守卫不可拷贝：拷贝会导致重复解锁
    LockGuard& operator=(const LockGuard&) = delete;

private:
    std::mutex* mtx_;
};

// ===========================================================================
// 3-b. 手写 RAII 内存守卫：这就是 std::unique_ptr 的最小版本
// ===========================================================================
class BufferGuard {
public:
    explicit BufferGuard(std::size_t n) : data_(new int[n]()), size_(n) { ++g_live_buffers; }

    ~BufferGuard() {
        delete[] data_;
        --g_live_buffers;
    }

    BufferGuard(const BufferGuard&) = delete;
    BufferGuard& operator=(const BufferGuard&) = delete;

    int& operator[](std::size_t i) { return data_[i]; }
    const int& operator[](std::size_t i) const { return data_[i]; }
    std::size_t size() const { return size_; }

private:
    int* data_;
    std::size_t size_;
};

// ===========================================================================
// 3-c. 通用作用域守卫：把「析构时执行任意清理动作」抽象出来
// ===========================================================================
class ScopeGuard {
public:
    explicit ScopeGuard(std::string name, int& counter)
        : name_(std::move(name)), counter_(&counter) {}

    ~ScopeGuard() noexcept {
        ++(*counter_);      // 代表任意清理动作：解锁 / 关闭 / 回滚事务
        std::cout << "      [ScopeGuard] 执行清理动作: " << name_ << "\n";
    }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

private:
    std::string name_;
    int* counter_;
};

// ===========================================================================
// 4-b. 演示：一个会在「已经持有资源」之后抛异常的类
// ===========================================================================
class Session {
public:
    Session(const std::string& name, bool fail_after_acquire)
        : cleanup_count_(0), guard_(name + "-lock", cleanup_count_), name_(name) {
        std::cout << "      [Session] 构造函数已获取锁（成员 guard_ 构造完成）\n";
        if (fail_after_acquire) {
            // 关键点：此时 guard_ 已经构造完成，抛异常会让它析构 => 锁被正确释放。
            // 如果锁是裸的 mutex::lock()，这里就会永久死锁。
            throw std::runtime_error("Session 初始化失败");
        }
        std::cout << "      [Session] 构造完成\n";
    }

    // 注意：构造函数抛异常时，这个析构函数【不会】被调用（对象从未完成构造），
    // 但已构造完成的成员 guard_ 会被逐个析构 —— 这正是 RAII 兜底的原因。
    ~Session() { std::cout << "      [Session] 析构 body 执行\n"; }

    int cleanup_count() const { return cleanup_count_; }

private:
    int cleanup_count_;    // 必须声明在 guard_ 之前：guard_ 要引用它
    ScopeGuard guard_;     // 成员按声明顺序构造，按相反顺序析构
    std::string name_;
};

// ===========================================================================
// 7. 两阶段构造：构造函数只做不会失败的事，失败留给 Init
// ===========================================================================
class TwoPhaseDevice {
public:
    // 构造函数不碰硬件，因此不会失败 => 不需要异常，也不会留下半成品
    TwoPhaseDevice() = default;

    // 真正的初始化放在显式方法里，失败用返回值表达（也可用 std::optional / std::expected）
    bool Init(int hardware_id) {
        if (hardware_id < 0) {
            return false;               // 常用在禁用异常或需要返回错误码的场景
        }
        opened_ = true;
        return true;
    }

    bool opened() const { return opened_; }

private:
    bool opened_ = false;
};

// ===========================================================================
// 演示入口
// ===========================================================================
void DemoRaiiFile() {
    std::cout << "==== 1/2. RAII 文件包装器 ====\n";
    const std::string path = ".raii_demo.txt";
    std::cout << "    当前打开的文件数 = " << g_file_open_count << "\n";
    {
        FileHandle fh(path);                      // 构造 = 打开
        fh.Write("RAII: 构造获取，析构释放\n");
        std::cout << "    作用域内：打开文件数 = " << g_file_open_count
                  << "，is_open = " << fh.is_open() << "\n";
        fh.Close();                               // 显式提前释放
        std::cout << "    显式 Close 之后：打开文件数 = " << g_file_open_count
                  << "，is_open = " << fh.is_open() << "\n";
    }
    std::cout << "    离开作用域后：打开文件数 = " << g_file_open_count
              << "（析构是幂等的，重复 Close 不会出问题）\n";
    std::cout << "    【错误写法演示】把 fopen/fclose 手写配对，任何一条提前 return 都会漏掉 fclose：\n";
    std::cout << "      std::FILE* fp = std::fopen(path, \"wb\");\n";
    std::cout << "      if (something_bad) { return; }   // 漏了 fclose => 句柄泄漏\n";
    std::cout << "      std::fclose(fp);\n";
    std::cout << "    （这也是 MSVC 的 C4996 警告想提醒的事情，但在 /WX 下它会直接变成编译错误，\n";
    std::cout << "      所以本文件改用 std::ofstream —— 它本身就是 RAII 类型。）\n";
    std::cout << "    RAII 版本把「成对调用」变成了「作用域规则」，从语法上就写不出漏掉释放的分支。\n";
    std::remove(path.c_str());
    PrintLine();
}

void DemoRaiiLock() {
    std::cout << "==== 3-a. RAII 锁守卫 ====\n";
    std::mutex mtx;
    std::cout << "    进入前：存活守卫数 = " << g_live_guards << "\n";
    {
        LockGuard lg(mtx);
        std::cout << "    进入临界区：存活守卫数 = " << g_live_guards << "\n";
        std::cout << "    【错误写法】混用裸锁定：\n";
        std::cout << "      mtx.lock();     // 同一线程对 std::mutex 二次 lock => 未定义行为 / 死锁\n";
        std::cout << "      所以这行在本文件里被注释掉了，不能真的执行。\n";
    }
    std::cout << "    离开作用域：存活守卫数 = " << g_live_guards << "（自动解锁）\n";
    std::cout << "    标准库实现：std::lock_guard<std::mutex> 与本类几乎一样；\n";
    std::cout << "    C++17 还有 std::scoped_lock，可一次锁多个互斥量且避免死锁。\n";
    PrintLine();
}

void DemoRaiiMemory() {
    std::cout << "==== 3-b. RAII 内存守卫（手写 unique_ptr） ====\n";
    std::cout << "    进入前：存活缓冲数 = " << g_live_buffers << "\n";
    {
        BufferGuard buf(4);
        buf[0] = 10;
        buf[1] = 20;
        std::cout << "    作用域内：buf[0] = " << buf[0] << ", size = " << buf.size()
                  << "，存活缓冲数 = " << g_live_buffers << "\n";
    }
    std::cout << "    离开作用域：存活缓冲数 = " << g_live_buffers << "（自动 delete[]）\n";
    {
        // 标准库版本：功能完全一样，而且能做所有权转移（BufferGuard 为了演示没有做）
        auto up = std::make_unique<int[]>(4);
        up[0] = 7;
        auto up2 = std::move(up);                 // 所有权转移，up 变成 nullptr
        std::cout << "    std::unique_ptr 版本：转移后 up 为空? " << (up == nullptr)
                  << "，up2[0] = " << up2[0] << "\n";
        static_assert(!std::is_copy_constructible_v<std::unique_ptr<int[]>>,
                      "unique_ptr 不可拷贝：资源只能有一个主人，这正是 RAII 的语义要求");
    }
    std::cout << "    工程建议：能用 unique_ptr / vector / string 就不要手写守卫类。\n";
    PrintLine();
}

void DemoExceptionUnwinding() {
    std::cout << "==== 4. 抛异常时析构照样执行（对比 Java / C# 的 finally） ====\n";
    int cleanup = 0;
    try {
        std::cout << "    try 块开始\n";
        ScopeGuard g("rollback-transaction", cleanup);
        std::cout << "    业务逻辑执行中……\n";
        throw std::runtime_error("模拟业务异常");
        // 注意：后面这行永远不会执行，但 g 的析构依然会发生
    } catch (const std::exception& e) {
        std::cout << "    catch 捕获: " << e.what() << "\n";
    }
    std::cout << "    清理动作执行次数 = " << cleanup
              << "（异常穿过作用域时，编译器插入的栈展开代码调用了析构函数）\n";
    std::cout << "    与 Java / C# 的对比：\n";
    std::cout << "      Java  : try { ... } finally { cleanup(); }   —— 清理与业务写在两处，靠人保证\n";
    std::cout << "      C#    : using (var x = ...) { ... }          —— 仅对 IDisposable 生效\n";
    std::cout << "      Python: with open(...) as f:                 —— 语义上最接近 RAII\n";
    std::cout << "      C++   : 什么都不用写，成员/局部对象的析构函数就是清理点，\n";
    std::cout << "              而且对「所有」类型都生效，不要求实现某个接口。\n";
    PrintLine();
}

void DemoSessionUnwind() {
    std::cout << "==== 4-b. 成员已获取资源后构造失败：资源自动回流 ====\n";
    try {
        Session s("db-session", true);           // 构造中途抛异常
        std::cout << "    这行不会执行\n";
        static_cast<void>(s);
    } catch (const std::exception& e) {
        std::cout << "    catch 捕获: " << e.what() << "\n";
    }
    std::cout << "    顺序很关键：成员按声明顺序构造，异常时按相反顺序析构（guard_ 被销毁 => 锁归还），\n";
    std::cout << "    而 Session 自己的析构函数【不会】被执行（对象从未构造完成）。\n";
    std::cout << "    工程结论：把「获取 + 释放」都放进成员对象的构造/析构里，\n";
    std::cout << "    不要写在宿主类的构造函数体内，否则中途失败必然泄漏。\n";
    PrintLine();
}

void DemoDtorMustNotThrow() {
    std::cout << "==== 5. 为什么析构函数不该抛异常 ====\n";
    struct Base {
        virtual ~Base() = default;
    };
    struct Derived : Base {
        ~Derived() override = default;
    };
    std::cout << "    C++11 起，析构函数默认带 noexcept（隐式 noexcept(true)）：\n";
    static_assert(std::is_nothrow_destructible_v<Derived>, "析构函数默认 noexcept");
    static_assert(std::has_virtual_destructor_v<Base>, "基类析构必须是 virtual，否则派生类析构漏调");
    std::cout << "      static_assert(is_nothrow_destructible_v<Derived>) 已在编译期验证。\n";
    std::cout << "    如果析构函数真的抛出异常，运行时会调用 std::terminate，进程直接结束：\n";
    std::cout << "      - 栈展开过程中再抛异常 => 两个异常同时活跃 => terminate；\n";
    std::cout << "      - 即使不在展开中，noexcept 析构里抛出的异常也会直接 terminate。\n";
    std::cout << "    【错误写法演示】下面这段如果取消注释并运行，进程会立刻终止：\n";
    std::cout << "      struct Bad { ~Bad() noexcept(false) { throw std::runtime_error(\"boom\"); } };\n";
    std::cout << "      { Bad b; }   // -> terminate called after throwing an instance of ...\n";
    std::cout << "    正确做法：析构里只做「不会失败」的释放动作；\n";
    std::cout << "    必须报告错误的收尾工作（提交、刷新）请显式提供 Close()/Commit()，\n";
    std::cout << "    让调用方在对象还活着的时候处理失败。\n";
    PrintLine();
}

void DemoStandardRaii() {
    std::cout << "==== 6. 标准库里的 RAII 实现清单 ====\n";
    std::cout << "    std::lock_guard / std::unique_lock / std::scoped_lock  —— 互斥量\n";
    std::cout << "    std::unique_ptr / std::shared_ptr                     —— 堆内存\n";
    std::cout << "    std::vector / std::string                             —— 动态数组\n";
    std::cout << "    std::fstream / std::ifstream / std::ofstream          —— 文件\n";
    std::cout << "    std::jthread（C++20）                                 —— 线程 + 自动 join\n";
    std::cout << "    std::shared_ptr 的删除器（deleter）还能把「任意资源」纳入 RAII，\n";
    std::cout << "    甚至能让 shared_ptr 管理「不是 new 出来」的对象：\n";
    {
        const std::string shared_path = ".raii_shared.txt";
        std::shared_ptr<std::ofstream> sp(
            new std::ofstream(shared_path, std::ios::binary),
            [&shared_path](std::ofstream* f) {
                if (f != nullptr) {
                    // 删除器里做「非 delete」的清理动作，这是 RAII 泛化能力的关键
                    f->close();
                    std::remove(shared_path.c_str());
                    delete f;
                    std::cout << "      [deleter] 已关闭并删除 " << shared_path << "\n";
                }
            });
        *sp << "shared_ptr + custom deleter\n";
        std::cout << "      shared_ptr<ofstream> 引用计数 = " << sp.use_count()
                  << "，作用域结束自动执行删除器\n";
    }
    PrintLine();
}

void DemoTwoPhaseConstruction() {
    std::cout << "==== 7. 构造函数失败只能抛异常？两阶段构造的取舍 ====\n";
    std::cout << "    C++ 构造函数没有返回值，所以「失败」只有三条路：\n";
    std::cout << "      1. 抛异常（默认单选）：对象根本不存在，宿主析构不会被调用，\n";
    std::cout << "         但已经构造完成的成员会被正确析构 —— 这就是上一条演示的机制。\n";
    std::cout << "      2. 两阶段构造：构造只做不会失败的事，另加 Init()/Open() 返回错误码。\n";
    std::cout << "         代价：可能出现「已构造但未初始化」的中间状态，每个方法都要判断是否可用。\n";
    std::cout << "      3. 工厂函数返回 std::optional / std::expected（C++23），把失败放进返回值。\n";
    TwoPhaseDevice dev;
    const bool ok = dev.Init(7);
    const bool bad = dev.Init(-1);
    std::cout << "    TwoPhaseDevice Init(7)  = " << ok << "，opened = " << dev.opened() << "\n";
    std::cout << "    TwoPhaseDevice Init(-1) = " << bad << "（错误码路线，不抛异常）\n";
    std::cout << "    选型建议：\n";
    std::cout << "      - 库代码 / 业务代码：优先异常 + RAII，语义清晰且不会出现半初始化对象；\n";
    std::cout << "      - 禁用异常或跨 C ABI 的边界：用工厂函数返回 Result，而不是两阶段构造；\n";
    std::cout << "      - 只有在必须「先构造再依赖外部条件初始化」时（如嵌入式硬件）才用两阶段。\n";
    PrintLine();
}

void DemoLeakWithoutRaii() {
    std::cout << "==== 附：没有 RAII 时异常会漏掉资源（只讲不跑） ====\n";
    std::cout << "    void bad() {\n";
    std::cout << "        int* p = new int[100];        // 获取资源\n";
    std::cout << "        mayThrow();                   // 一旦抛异常……\n";
    std::cout << "        delete[] p;                   // 这行永远不执行 => 泄漏\n";
    std::cout << "    }\n";
    std::cout << "    【错误写法演示】取消注释就会真的泄漏（本文件不执行它，避免污染进程）：\n";
    std::cout << "      int* p = new int[100];\n";
    std::cout << "      try { throw std::runtime_error(\"x\"); } catch (...) {}\n";
    std::cout << "      // 没有 delete[] p => 这块内存再也找不回来\n";
    std::cout << "    RAII 版本：auto p = std::make_unique<int[]>(100); 无论怎么退出都不会漏。\n";
    PrintLine();
}

}  // namespace

int main() {
    std::cout << "################ 03 RAII 与资源管理 ################\n\n";
    DemoRaiiFile();
    DemoRaiiLock();
    DemoRaiiMemory();
    DemoExceptionUnwinding();
    DemoSessionUnwind();
    DemoDtorMustNotThrow();
    DemoStandardRaii();
    DemoTwoPhaseConstruction();
    DemoLeakWithoutRaii();
    std::cout << "收尾自检：打开文件数 = " << g_file_open_count
              << ", 存活守卫数 = " << g_live_guards
              << ", 存活缓冲数 = " << g_live_buffers << "\n";
    std::cout << "全部为 0 才说明没有资源泄漏。\n";
    return 0;
}
