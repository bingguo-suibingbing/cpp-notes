// ============================================================================
// 08_design_patterns_practical.cpp
// 演示主题（只讲实战里真正高频的几个）：
//   1. 工厂：简单工厂（一个函数按参数造对象）+ 抽象工厂（一组相关对象的接口）
//   2. 单例：Meyers Singleton（C++11 起局部 static 初始化线程安全），以及为什么不推荐单例
//   3. 策略模式：运行期用虚接口，编译期用模板 —— 与 04-templates 章的对照
//   4. 观察者模式：虚接口版 + std::function 版
//   5. Pimpl 惯用法：减小编译依赖 + 保证 ABI 稳定
//   6. 组合优于继承：一个真实可判断的判据
//
// 关键结论：
//   设计模式不是「背模板」，而是对「变化点」的封装。
//   先找到会变的地方，再决定用继承（is-a / 运行期替换）、组合（has-a / 委托）还是模板（编译期替换）。
// ============================================================================

#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

void PrintLine() { std::cout << "--------------------------------------------------\n"; }

// ===========================================================================
// 1-a. 简单工厂
// ===========================================================================
class ITransport {
public:
    virtual ~ITransport() = default;
    virtual std::string Name() const = 0;
    virtual double Cost(double km) const = 0;
};

class Truck : public ITransport {
public:
    std::string Name() const override { return "卡车"; }
    double Cost(double km) const override { return 12.0 + 3.5 * km; }   // 固定起步价 + 每公里
};

class Train : public ITransport {
public:
    std::string Name() const override { return "火车"; }
    double Cost(double km) const override { return 5.0 * km; }
};

class Plane : public ITransport {
public:
    std::string Name() const override { return "飞机"; }
    double Cost(double km) const override { return 800.0 + 0.8 * km; }
};

// 简单工厂：把「选择具体类型」这件事收敛到一个地方。
// 返回 unique_ptr<接口> 是标准姿势：调用方拿到的唯一好处是「多态」，不关心具体类型，
// 也不需要自己 delete（RAII 兜底）。
std::unique_ptr<ITransport> CreateTransport(const std::string& kind) {
    if (kind == "truck") {
        return std::make_unique<Truck>();
    }
    if (kind == "train") {
        return std::make_unique<Train>();
    }
    if (kind == "plane") {
        return std::make_unique<Plane>();
    }
    return nullptr;                 // 也可以抛异常，取决于团队的「错误处理风格」
}

// ===========================================================================
// 1-b. 抽象工厂：一组「相关产品」的创建接口
// ===========================================================================
class IButton {
public:
    virtual ~IButton() = default;
    virtual std::string Render() const = 0;
};

class ICheckBox {
public:
    virtual ~ICheckBox() = default;
    virtual std::string Render() const = 0;
};

class LightButton : public IButton {
public:
    std::string Render() const override { return "[浅色按钮]"; }
};
class DarkButton : public IButton {
public:
    std::string Render() const override { return "[深色按钮]"; }
};
class LightCheckBox : public ICheckBox {
public:
    std::string Render() const override { return "[浅色复选框]"; }
};
class DarkCheckBox : public ICheckBox {
public:
    std::string Render() const override { return "[深色复选框]"; }
};

// 抽象工厂：不返回单个对象，而是返回「一整套风格一致的对象」
class IUiFactory {
public:
    virtual ~IUiFactory() = default;
    virtual std::unique_ptr<IButton> MakeButton() const = 0;
    virtual std::unique_ptr<ICheckBox> MakeCheckBox() const = 0;
    virtual std::string ThemeName() const = 0;
};

class LightThemeFactory : public IUiFactory {
public:
    std::unique_ptr<IButton> MakeButton() const override { return std::make_unique<LightButton>(); }
    std::unique_ptr<ICheckBox> MakeCheckBox() const override {
        return std::make_unique<LightCheckBox>();
    }
    std::string ThemeName() const override { return "浅色主题"; }
};

class DarkThemeFactory : public IUiFactory {
public:
    std::unique_ptr<IButton> MakeButton() const override { return std::make_unique<DarkButton>(); }
    std::unique_ptr<ICheckBox> MakeCheckBox() const override {
        return std::make_unique<DarkCheckBox>();
    }
    std::string ThemeName() const override { return "深色主题"; }
};

// 业务代码只依赖抽象工厂：加一套新主题时，这个函数一行都不用改。
void RenderForm(const IUiFactory& factory) {
    const auto button = factory.MakeButton();
    const auto box = factory.MakeCheckBox();
    std::cout << "    " << factory.ThemeName() << " 渲染出: " << button->Render() << " "
              << box->Render() << "\n";
}

// ===========================================================================
// 2. 单例：Meyers Singleton
// ===========================================================================
class Config {
public:
    // C++11 起，局部 static 变量的初始化是线程安全的（标准保证，编译器负责加锁）。
    // 这个写法同时满足：懒初始化 + 线程安全 + 自动析构 + 无法被外部构造。
    static Config& Instance() {
        static Config instance;         // 关键：函数内的 static，第一次调用时构造
        return instance;
    }

    // 删除拷贝与赋值：确保「只有一个」
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    Config(Config&&) = delete;
    Config& operator=(Config&&) = delete;

    const std::string& Get(const std::string& key) const {
        for (const auto& kv : items_) {
            if (kv.first == key) {
                return kv.second;
            }
        }
        static const std::string kEmpty;
        return kEmpty;
    }

    void Set(std::string key, std::string value) {
        for (auto& kv : items_) {
            if (kv.first == key) {
                kv.second = std::move(value);
                return;
            }
        }
        items_.emplace_back(std::move(key), std::move(value));
    }

private:
    Config() {                                          // 构造函数私有 => 外部无法造第二个
        items_.emplace_back("log_level", "info");
        items_.emplace_back("threads", "4");
    }

    std::vector<std::pair<std::string, std::string>> items_;
};

// ===========================================================================
// 3. 策略模式
// ===========================================================================
class IDiscountStrategy {
public:
    virtual ~IDiscountStrategy() = default;
    virtual std::string Name() const = 0;
    virtual double Apply(double price) const = 0;
};

class NoDiscount : public IDiscountStrategy {
public:
    std::string Name() const override { return "无折扣"; }
    double Apply(double price) const override { return price; }
};

class PercentDiscount : public IDiscountStrategy {
public:
    explicit PercentDiscount(double percent) : percent_(percent) {}
    std::string Name() const override { return std::to_string(percent_) + "% 折扣"; }
    double Apply(double price) const override { return price * (1.0 - percent_ / 100.0); }

private:
    double percent_;
};

class ThresholdDiscount : public IDiscountStrategy {
public:
    ThresholdDiscount(double threshold, double amount) : threshold_(threshold), amount_(amount) {}
    std::string Name() const override { return "满减"; }
    double Apply(double price) const override {
        return price >= threshold_ ? price - amount_ : price;
    }

private:
    double threshold_;
    double amount_;
};

// 运行期策略：策略通过【构造函数注入】，而不是在类内部 new
class PriceCalculator {
public:
    explicit PriceCalculator(std::unique_ptr<IDiscountStrategy> strategy)
        : strategy_(std::move(strategy)) {}

    double FinalPrice(double price) const {
        return strategy_ == nullptr ? price : strategy_->Apply(price);
    }

    std::string StrategyName() const {
        return strategy_ == nullptr ? "无" : strategy_->Name();
    }

    // 允许运行期替换策略（这就是「策略模式」的核心价值：行为可在运行时改变）
    void SetStrategy(std::unique_ptr<IDiscountStrategy> strategy) {
        strategy_ = std::move(strategy);
    }

private:
    std::unique_ptr<IDiscountStrategy> strategy_;
};

// 编译期策略：策略通过【模板参数】传入 => 可以内联，零虚调用开销。
// 代价：策略在编译期固定，无法在运行期切换；每个组合会生成一份代码。
template <typename Strategy>
class StaticPriceCalculator {
public:
    explicit StaticPriceCalculator(Strategy strategy) : strategy_(std::move(strategy)) {}

    double FinalPrice(double price) const { return strategy_.Apply(price); }

private:
    Strategy strategy_;
};

struct StaticPercentOff {
    double percent;
    double Apply(double price) const { return price * (1.0 - percent / 100.0); }
};

// ===========================================================================
// 4. 观察者模式
// ===========================================================================
// 4-a. 虚接口版：适合「观察者本身是有状态的复杂对象」
class ITemperatureObserver {
public:
    virtual ~ITemperatureObserver() = default;
    virtual void OnTemperatureChanged(double celsius) = 0;
    virtual std::string ObserverName() const = 0;
};

class TemperatureSensor {
public:
    void Subscribe(ITemperatureObserver* observer) {
        observers_.push_back(observer);       // 只借用，不拥有 => 生命周期由调用方保证
    }

    void SetTemperature(double celsius) {
        temperature_ = celsius;
        // 注意：这里用下标而不是迭代器，因为观察者可能在回调里退订（迭代器会失效）
        for (std::size_t i = 0; i < observers_.size(); ++i) {
            observers_[i]->OnTemperatureChanged(temperature_);
        }
    }

    double temperature() const { return temperature_; }

private:
    std::vector<ITemperatureObserver*> observers_;
    double temperature_ = 0.0;
};

class DisplayPanel : public ITemperatureObserver {
public:
    void OnTemperatureChanged(double celsius) override {
        std::cout << "      [DisplayPanel] 更新显示: " << celsius << " 度\n";
    }
    std::string ObserverName() const override { return "DisplayPanel"; }
};

class AlarmUnit : public ITemperatureObserver {
public:
    explicit AlarmUnit(double threshold) : threshold_(threshold) {}
    void OnTemperatureChanged(double celsius) override {
        if (celsius > threshold_) {
            std::cout << "      [AlarmUnit] 超过阈值 " << threshold_ << " 度，报警\n";
        }
    }
    std::string ObserverName() const override { return "AlarmUnit"; }

private:
    double threshold_;
};

// 4-b. std::function 版：适合「回调就是一个动作」
class EventSource {
public:
    // 返回一个「订阅凭据」，调用方可以用它取消订阅
    std::size_t Subscribe(std::function<void(int)> handler) {
        const std::size_t id = next_id_++;
        handlers_.emplace_back(id, std::move(handler));
        return id;
    }

    void Unsubscribe(std::size_t id) {
        handlers_.erase(std::remove_if(handlers_.begin(), handlers_.end(),
                                       [id](const auto& item) { return item.first == id; }),
                        handlers_.end());
    }

    void Emit(int value) const {
        for (const auto& item : handlers_) {
            item.second(value);               // 调用回调
        }
    }

    std::size_t handler_count() const { return handlers_.size(); }

private:
    std::vector<std::pair<std::size_t, std::function<void(int)>>> handlers_;
    std::size_t next_id_ = 1;
};

// ===========================================================================
// 5. Pimpl：把实现细节藏进 .cpp
// ===========================================================================
// 类的「门面」：只暴露接口，不暴露任何成员（连数据成员的 sizeof 都不暴露）
class ApiClient {
public:
    ApiClient();
    explicit ApiClient(std::string endpoint);
    ~ApiClient();                                    // 必须 out-of-line，因为 Impl 此时不完整

    // 拷贝/移动也要 out-of-line，否则编译器在这里实例化 unique_ptr 的删除器会报「不完整类型」
    ApiClient(const ApiClient& other);
    ApiClient& operator=(const ApiClient& other);
    ApiClient(ApiClient&& other) noexcept;
    ApiClient& operator=(ApiClient&& other) noexcept;

    std::string Get(const std::string& path) const;
    std::size_t CacheSize() const;
    void SetTimeout(int seconds);

private:
    struct Impl;                                     // 只声明，不定义
    std::unique_ptr<Impl> impl_;
};

// 下面是「实现文件」的内容（真实项目里这些会放在 ApiClient.cpp）
struct ApiClient::Impl {
    std::string endpoint;
    int timeout_seconds = 30;
    std::vector<std::string> cache;

    std::string Describe() const {
        return endpoint + " (timeout=" + std::to_string(timeout_seconds) +
               ", cache=" + std::to_string(cache.size()) + ")";
    }
};

ApiClient::ApiClient() : impl_(std::make_unique<Impl>()) { impl_->endpoint = "https://default"; }

ApiClient::ApiClient(std::string endpoint) : impl_(std::make_unique<Impl>()) {
    impl_->endpoint = std::move(endpoint);
}

ApiClient::~ApiClient() = default;                   // 在 Impl 完整的地方定义 => 不会报错

ApiClient::ApiClient(const ApiClient& other) : impl_(std::make_unique<Impl>(*other.impl_)) {}

ApiClient& ApiClient::operator=(const ApiClient& other) {
    if (this != &other) {
        *impl_ = *other.impl_;
    }
    return *this;
}

ApiClient::ApiClient(ApiClient&& other) noexcept = default;
ApiClient& ApiClient::operator=(ApiClient&& other) noexcept = default;

std::string ApiClient::Get(const std::string& path) const {
    impl_->cache.push_back(path);
    return "GET " + impl_->endpoint + path + " -> 200 OK";
}

std::size_t ApiClient::CacheSize() const { return impl_->cache.size(); }

void ApiClient::SetTimeout(int seconds) { impl_->timeout_seconds = seconds; }

// ===========================================================================
// 6. 组合优于继承
// ===========================================================================
// 【反面写法】为了复用「打日志」而继承：
//   class OrderService : public Logger { ... };
// 问题：OrderService「不是一种」Logger；而且 Logger 的接口全部泄漏给了使用者。
class SimpleLogger {
public:
    void Log(const std::string& msg) const { std::cout << "      [log] " << msg << "\n"; }
};

// 【正面写法】组合：需要一个能力就持有一个对象
class OrderService {
public:
    explicit OrderService(SimpleLogger logger) : logger_(std::move(logger)) {}

    void PlaceOrder(const std::string& sku) const {
        logger_.Log("下单: " + sku);          // 委托给成员，而不是继承它的接口
    }

private:
    SimpleLogger logger_;                    // has-a：OrderService 有一个 logger
};

// 更能说明问题的一例：想换日志实现时
class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void Write(const std::string& msg) const = 0;
};

class ConsoleLogger : public ILogger {
public:
    void Write(const std::string& msg) const override {
        std::cout << "      [console] " << msg << "\n";
    }
};

class MemoryLogger : public ILogger {
public:
    void Write(const std::string& msg) const override { messages_.push_back(msg); }
    const std::vector<std::string>& messages() const { return messages_; }

private:
    mutable std::vector<std::string> messages_;
};

// 组合 + 依赖倒置：OrderService 只依赖 ILogger 接口，日志实现可以随意替换（测试时可换成 MemoryLogger）
class OrderServiceWithDi {
public:
    explicit OrderServiceWithDi(const ILogger& logger) : logger_(logger) {}
    void PlaceOrder(const std::string& sku) const { logger_.Write("下单: " + sku); }

private:
    const ILogger& logger_;                  // 不拥有，只借用 => 生命周期由调用方保证
};

// ===========================================================================
// 演示入口
// ===========================================================================
void DemoSimpleFactory() {
    std::cout << "==== 1-a. 简单工厂 ====\n";
    const std::vector<std::string> kinds{"truck", "train", "plane", "rocket"};
    for (const std::string& kind : kinds) {
        const auto transport = CreateTransport(kind);
        if (transport == nullptr) {
            std::cout << "    未知类型 \"" << kind << "\" => 工厂返回 nullptr（调用方决定怎么处理）\n";
            continue;
        }
        std::cout << "    " << kind << " -> " << transport->Name() << "，1000 公里成本 "
                  << transport->Cost(1000.0) << "\n";
    }
    std::cout << "    工厂的价值：把「具体类型的选择」与「使用方式」解耦。\n";
    std::cout << "      调用方只知道 ITransport，新增一种运输方式时，使用侧代码零改动。\n";
    std::cout << "    工程注意：返回 unique_ptr<接口> 而不是裸指针，所有权清晰且异常安全。\n";
    PrintLine();
}

void DemoAbstractFactory() {
    std::cout << "==== 1-b. 抽象工厂 ====\n";
    const std::vector<std::unique_ptr<IUiFactory>> factories = [] {
        std::vector<std::unique_ptr<IUiFactory>> v;
        v.push_back(std::make_unique<LightThemeFactory>());
        v.push_back(std::make_unique<DarkThemeFactory>());
        return v;
    }();
    for (const auto& factory : factories) {
        RenderForm(*factory);
    }
    std::cout << "    简单工厂 vs 抽象工厂：\n";
    std::cout << "      简单工厂：一个函数，按参数造【一个】对象（最常用，也最不容易过度设计）；\n";
    std::cout << "      抽象工厂：一个接口造【一组必须搭配使用】的对象（主题、驱动族、跨平台控件）。\n";
    std::cout << "    抽象工厂真正的收益是「保证产品族一致」：\n";
    std::cout << "      使用侧不可能把深色按钮和浅色复选框混在一起 —— 从类型层面杜绝了。\n";
    PrintLine();
}

void DemoSingleton() {
    std::cout << "==== 2. Meyers Singleton ====\n";
    Config& a = Config::Instance();
    Config& b = Config::Instance();
    std::cout << "    &Config::Instance() 两次调用相同? " << (&a == &b) << "\n";
    std::cout << "    初始 log_level = " << a.Get("log_level") << ", threads = " << a.Get("threads") << "\n";
    a.Set("log_level", "debug");
    std::cout << "    通过 a 改成 debug 后，通过 b 读: " << b.Get("log_level")
              << "（同一个对象）\n";
    // 【错误写法】Config c = Config::Instance();   // error C2280: 尝试引用已删除的函数（拷贝被删除）
    std::cout << "    Meyers Singleton 为什么优于「静态成员 + 手写双检锁」：\n";
    std::cout << "      1. C++11 起局部 static 初始化由编译器保证线程安全（magic static），\n";
    std::cout << "         自己写 double-checked locking 极易漏加内存屏障 => 出错概率远高于收益；\n";
    std::cout << "      2. 懒初始化：第一次调用时才构造，不产生静态初始化顺序问题；\n";
    std::cout << "      3. 程序退出时会自动析构（不是永不释放的指针）。\n";
    std::cout << "    但仍然不推荐滥用单例，理由很实在：\n";
    std::cout << "      - 它就是一个「全局变量」，隐藏了依赖关系：\n";
    std::cout << "        函数签名上看不出它需要什么，测试时无法替换成假对象；\n";
    std::cout << "      - 单元测试之间会互相污染（改过的配置留给下一个测试）；\n";
    std::cout << "      - 并行测试会互相干扰，而且生命周期与销毁顺序难以控制。\n";
    std::cout << "    替代方案：\n";
    std::cout << "      1. 把依赖作为构造参数传进去（依赖注入），测试时传假实现；\n";
    std::cout << "      2. 只在程序入口创建一次，然后向下传递引用（显式所有权）；\n";
    std::cout << "      3. 确实需要全局唯一（日志、配置、连接池）时，用单例但只暴露【窄接口】，\n";
    std::cout << "         并且允许在测试里替换（例如 setInstanceForTest 或依赖注入容器）。\n";
    PrintLine();
}

void DemoStrategy() {
    std::cout << "==== 3. 策略模式（运行期 vs 编译期） ====\n";
    PriceCalculator calc(std::make_unique<NoDiscount>());
    const double price = 1000.0;
    std::cout << "    原始价格 = " << price << "\n";
    std::cout << "    策略 " << calc.StrategyName() << " -> " << calc.FinalPrice(price) << "\n";
    calc.SetStrategy(std::make_unique<PercentDiscount>(20.0));
    std::cout << "    策略 " << calc.StrategyName() << " -> " << calc.FinalPrice(price) << "\n";
    calc.SetStrategy(std::make_unique<ThresholdDiscount>(800.0, 150.0));
    std::cout << "    策略 " << calc.StrategyName() << " -> " << calc.FinalPrice(price) << "\n";

    // 编译期版本：策略类型在编译期确定，没有虚调用
    const StaticPriceCalculator<StaticPercentOff> staticCalc(StaticPercentOff{20.0});
    std::cout << "    编译期策略 StaticPercentOff{20} -> " << staticCalc.FinalPrice(price) << "\n";

    std::cout << "    两种实现的取舍：\n";
    std::cout << "      +------------------+--------------------+----------------------+\n";
    std::cout << "      | 维度             | 虚接口（运行期）   | 模板（编译期）       |\n";
    std::cout << "      +------------------+--------------------+----------------------+\n";
    std::cout << "      | 切换时机         | 运行期任意切换     | 编译期固定           |\n";
    std::cout << "      | 调用开销         | 一次间接跳转       | 可内联，零开销       |\n";
    std::cout << "      | 代码体积         | 一份               | 每个组合一份         |\n";
    std::cout << "      | 能否放进同一个容器| 能                 | 不能（类型不同）     |\n";
    std::cout << "      | 编译依赖         | 低（只要接口）     | 高（要看到实现）     |\n";
    std::cout << "      +------------------+--------------------+----------------------+\n";
    std::cout << "    选型：\n";
    std::cout << "      - 策略需要用户/配置在运行期选择 => 虚接口（本章）；\n";
    std::cout << "      - 策略在编译期就固定、且在热路径上 => 模板（见本仓库 04-templates 章）；\n";
    std::cout << "      - 两者可以结合：外层用虚接口做「粗粒度选择」，内层用模板做「热路径优化」。\n";
    PrintLine();
}

void DemoObserver() {
    std::cout << "==== 4-a. 观察者模式（虚接口版） ====\n";
    TemperatureSensor sensor;
    DisplayPanel panel;
    AlarmUnit alarm(30.0);
    sensor.Subscribe(&panel);
    sensor.Subscribe(&alarm);
    std::cout << "    设置温度 25 度:\n";
    sensor.SetTemperature(25.0);
    std::cout << "    设置温度 35 度:\n";
    sensor.SetTemperature(35.0);
    std::cout << "    注意：这里存的是裸指针（只借用，不拥有）。\n";
    std::cout << "      真实项目里观察者的生命周期往往更短，必须保证退订比析构早，\n";
    std::cout << "      否则回调会打到已销毁的对象上（悬空指针）。\n";
    std::cout << "      需要更强保证时，用 shared_ptr/weak_ptr 组合：\n";
    std::cout << "        传感器持有 weak_ptr，回调前 lock() 一下，对象没了就自动跳过。\n";

    std::cout << "    ==== 4-b. 观察者模式（std::function 版） ====\n";
    EventSource source;
    const std::size_t id1 = source.Subscribe([](int v) {
        std::cout << "      [handler-1] 收到 " << v << "\n";
    });
    const std::size_t id2 = source.Subscribe([](int v) {
        std::cout << "      [handler-2] 收到 " << v << " 的平方 = " << v * v << "\n";
    });
    std::cout << "    当前订阅数 = " << source.handler_count() << "\n";
    std::cout << "    广播 5:\n";
    source.Emit(5);
    source.Unsubscribe(id1);
    std::cout << "    退订 handler-1 后订阅数 = " << source.handler_count() << "，再广播 6:\n";
    source.Emit(6);
    static_cast<void>(id2);

    std::cout << "    两种写法的选择：\n";
    std::cout << "      - 观察者是有状态、有多个方法的对象 => 虚接口（可读性好，能被 IDE 跳转）；\n";
    std::cout << "      - 回调只是一个动作（几行代码）=> std::function（省掉一个类）；\n";
    std::cout << "      - std::function 的代价：类型擦除 => 一次间接调用 + 可能堆分配（捕获较大时）。\n";
    std::cout << "    观察者模式的常见坑：\n";
    std::cout << "      1. 回调中退订 => 遍历用的迭代器失效（本示例用下标遍历规避）；\n";
    std::cout << "      2. 回调中抛异常 => 后面的观察者不会被通知（需要 try/catch 隔离）；\n";
    std::cout << "      3. 通知顺序不确定 => 不要让业务逻辑依赖顺序。\n";
    PrintLine();
}

void DemoPimpl() {
    std::cout << "==== 5. Pimpl 惯用法 ====\n";
    ApiClient client("https://api.example.com");
    std::cout << "    " << client.Get("/users") << "\n";
    std::cout << "    " << client.Get("/orders") << "\n";
    client.SetTimeout(5);
    std::cout << "    缓存条目数 = " << client.CacheSize() << "\n";

    ApiClient copy = client;                       // 深拷贝：拷贝的是 Impl
    std::cout << "    拷贝一个 client 后，拷贝体的缓存条目数 = " << copy.CacheSize() << "\n";
    ApiClient moved = std::move(client);           // 移动：只搬指针
    std::cout << "    移动之后 moved.CacheSize() = " << moved.CacheSize() << "\n";

    std::cout << "    Pimpl 带来的三个实际收益：\n";
    std::cout << "      1. 编译依赖：Impl 里用到的头文件（vector/string/网络库）只出现在 .cpp 里，\n";
    std::cout << "         改实现不需要重新编译所有包含该头文件的翻译单元；\n";
    std::cout << "      2. ABI 稳定：sizeof(ApiClient) 固定为一个指针，头文件不变，\n";
    std::cout << "         往 Impl 里加成员不会破坏二进制兼容 => 动态库升级不用重新编译调用方；\n";
    std::cout << "      3. 隐藏实现细节（真私有），头文件干净，别人无法依赖你的内部数据。\n";
    std::cout << "    代价：\n";
    std::cout << "      - 多一次指针间接访问（缓存不友好，热路径上要谨慎）；\n";
    std::cout << "      - 析构 / 拷贝 / 移动都必须 out-of-line 定义（因为 Impl 不完整），代码更长；\n";
    std::cout << "      - 调试时多一层跳转。\n";
    std::cout << "    用不用？判断标准：\n";
    std::cout << "      - 这是要交付给别人的【库】接口（尤其动态库）=> 用，收益远大于成本；\n";
    std::cout << "      - 内部小类、头文件本来就要包含这些类型 => 不用，纯属增复杂度。\n";
    PrintLine();
}

void DemoCompositionOverInheritance() {
    std::cout << "==== 6. 组合优于继承 ====\n";
    OrderService service(SimpleLogger{});
    service.PlaceOrder("SKU-001");

    MemoryLogger memoryLogger;
    OrderServiceWithDi service2(memoryLogger);
    service2.PlaceOrder("SKU-002");
    service2.PlaceOrder("SKU-003");
    std::cout << "    用 MemoryLogger 注入后，记录数 = " << memoryLogger.messages().size()
              << "（测试里直接断言它，不需要解析控制台输出）\n";

    std::cout << "    为什么组合优先：\n";
    std::cout << "      1. 继承是「最强耦合」：派生类依赖基类的实现细节，\n";
    std::cout << "         基类一改（哪怕只是加个重载），派生类可能被名字隐藏等问题悄悄影响；\n";
    std::cout << "      2. 继承会把基类的【全部】接口暴露给使用者（public 继承），\n";
    std::cout << "         而组合只暴露你愿意转发的那几个方法；\n";
    std::cout << "      3. 继承是编译期固定的，组合可以在运行期替换（上例换成 MemoryLogger）；\n";
    std::cout << "      4. 继承层次一深，构造/析构顺序、虚函数分派、菱形问题全部压过来，\n";
    std::cout << "         调试成本指数上升；组合是「一层套一层」，关系始终局部、可预测。\n";
    std::cout << "    什么时候仍然该用继承：\n";
    std::cout << "      - 需要运行期多态（通过基类指针统一处理一族对象）；\n";
    std::cout << "      - 派生类【确实】是基类的一种（is-a），且基类是为继承设计的\n";
    std::cout << "        （有虚析构、有明确的覆写点、文档说明了继承约定）；\n";
    std::cout << "      - 需要覆写虚函数来定制行为（模板方法模式），此时用 public 或 private 继承。\n";
    std::cout << "    一句话判据：为了「复用代码」而继承基本都是错的；\n";
    std::cout << "                 为了「被统一处理（多态）」而继承才是对的。\n";
    PrintLine();
}

}  // namespace

int main() {
    std::cout << "################ 08 实战设计模式 ################\n\n";
    DemoSimpleFactory();
    DemoAbstractFactory();
    DemoSingleton();
    DemoStrategy();
    DemoObserver();
    DemoPimpl();
    DemoCompositionOverInheritance();
    std::cout << "本章一句话总结：先找变化点，再选工具 ——\n";
    std::cout << "  运行期替换用虚接口，编译期替换用模板，只是复用代码用组合。\n";
    return 0;
}
