# =============================================================================
#  07-engineering/CMakeLists.txt
# -----------------------------------------------------------------------------
#  这一章所有示例的 CMake 构建脚本。
#
#  它同时是一份「CMake 最小可用模板 + 逐行讲解」，所以注释比代码多。
#  阅读顺序建议：先看第 1~4 节（必需的四块），再看第 5 节以后（工程化）。
#
#  和仓库根目录 build.ps1 的关系（详见 NOTES.md 第 4 节）：
#    - build.ps1  ：零依赖、一条命令编译本章每个 .cpp（一个 .cpp = 一个 exe），
#                   用来快速验证「语法/警告」，不生成 IDE 工程。
#    - CMakeLists ：贴近真实项目的方式：生成 VS 工程 / Ninja 构建、支持 Debug 与
#                   Release 两套配置、支持 CTest、支持 IDE 智能提示。
#    两者互不依赖，可以同时存在。（build.ps1 不会调用 CMake，本文件也不会调用 build.ps1）
#
#  快速开始（在 07-engineering 目录下）：
#      cmake --preset debug                 # 用 CMakePresets.json 配置（推荐）
#      cmake --build --preset debug         # 构建
#      ctest --preset debug                 # 跑测试
#      cmake --preset release && cmake --build --preset release
#
#  或者完全手写命令：
#      cmake -S . -B build-cmake -G "Visual Studio 18 2026" -A x64
#      cmake --build build-cmake --config Debug
#      ctest --test-dir build-cmake -C Debug --output-on-failure
# =============================================================================

# -----------------------------------------------------------------------------
#  第 1 节：cmake_minimum_required —— 声明「本脚本至少需要哪个 CMake」
# -----------------------------------------------------------------------------
#  为什么必须写：
#    1) 让旧版 CMake 直接报「版本太老」，而不是在一堆奇怪的语法错误里挣扎；
#    2) 它还会设置 policy 的默认行为（新版 CMake 里会启用对应版本的新策略），
#       所以这个数字会影响构建行为，不只是「打个招呼」。
#  写多少：取「团队里最低的那个版本」。3.20 是常见的安全线（Presets v3、
#  MSVC 运行时库抽象 MSVC_RUNTIME_LIBRARY、CUDA 支持等都在 3.15~3.20 稳定）。
#  注意：不要写 4.x 或很高的版本，除非确定所有机器都装了 ——
#        那等于把「构建不了」的问题留给别人。
# -----------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.20)

# -----------------------------------------------------------------------------
#  第 2 节：project —— 项目名、版本、语言
# -----------------------------------------------------------------------------
#  LANGUAGES CXX：只启用 C++，不启用 C。
#    好处：配置阶段更快，也不会意外去找 C 编译器。
#    （如果项目里确实有 .c 文件，就必须写 C CXX，否则那些文件会被忽略。）
#  VERSION：给下游用（比如生成 config.h、打包文件名）。这里只是示例。
#  DESCRIPTION / HOMEPAGE_URL：会写进生成的 package 元数据里，可选。
# -----------------------------------------------------------------------------
project(cpp_notes_07_engineering
        VERSION 1.0.0
        DESCRIPTION "C++ 工程实践章节示例（编译警告/断言/日志/性能/调试/风格）"
        LANGUAGES CXX)

# -----------------------------------------------------------------------------
#  第 3 节：语言标准 —— 用「目标属性」而不是全局变量
# -----------------------------------------------------------------------------
#  CXX_STANDARD 20          ：要求 C++20（会加上 /std:c++20 或 -std=c++20）
#  CXX_STANDARD_REQUIRED ON ：不允许自动降级到更老的标准（很重要！）
#                             不开的话，编译器不支持时会**静默**用旧标准编译，
#                             于是你会在运行期发现「概念/ ranges 不能用」。
#  CXX_EXTENSIONS OFF       ：禁止编译器扩展（MSVC 上是 /permissive- 的一部分，
#                             GCC 上是 -std=c++20 而不是 -std=gnu++20）。
#                             收益：代码更可移植，换编译器时踩坑更少。
#
#  注意：下面这组变量是「初始化 target 属性」的默认值，
#        单目录项目这么写完全够用；多 target 项目应该改用
#        target_compile_features(<target> PUBLIC cxx_std_20)，效果更精确。
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
#  第 4 节：输出目录与编译数据库
# -----------------------------------------------------------------------------
#  CMAKE_EXPORT_COMPILE_COMMANDS=ON
#    生成 build 目录下的 compile_commands.json：里面记录了「每个源文件用了
#    哪些完整编译命令」。clangd / clang-tidy / VS Code C++ 插件 / Qt Creator
#    都靠它做智能提示和静态检查。
#    只对 Makefile / Ninja 生成器有效；Visual Studio 生成器会忽略它（VS 有自己的
#    .vcxproj）。想要这个文件又用 VS 生成器时，可以额外配一个 Ninja 构建目录。
#
#  CMAKE_RUNTIME_OUTPUT_DIRECTORY
#    把可执行文件集中到 build/bin 下，源码目录保持干净（和 build.ps1 的思路一致）。
#    子目录（tests）会通过 add_subdirectory 继承这个设置。
# -----------------------------------------------------------------------------
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# -----------------------------------------------------------------------------
#  第 5 节：默认构建类型
# -----------------------------------------------------------------------------
#  CMake 的默认构建类型是**空**（既不是 Debug 也不是 Release），
#  于是 MSVC 会用默认的 /MD 且不带调试信息，GCC 会用 -O0 且没有 NDEBUG ——
#  结果就是「Debug 和 Release 都不像」的第三种配置，最容易出怪问题。
#
#  所以推荐显式定一个默认值：单配置生成器（Ninja/Makefile）下有效；
#  多配置生成器（Visual Studio）会忽略它，改用 --config 指定。
#  这里默认 Release：因为本仓库主要用途包含性能对比，Release 才有参考价值；
#  如果你主要工作是调试，改成 Debug 更顺手。
# -----------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "构建类型：Debug / Release / RelWithDebInfo / MinSizeRel" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# -----------------------------------------------------------------------------
#  第 6 节：把「警告」集中成一份接口
# -----------------------------------------------------------------------------
#  为什么不直接写死在 add_executable 里：
#    这里用 INTERFACE 库（纯头文件、不产出任何二进制）把「编译选项」打包成
#    一个可以被 target_link_libraries 的「用法依赖」。
#    好处：所有目标一行 PRIVATE project_warnings 就继承同一套选项，
#          改一处全局生效，不会漏掉某个新加的目标。
#
#  generator expression（$<CXX_COMPILER_ID:MSVC> 这种）：
#    在**生成阶段**根据编译器/配置展开，所以同一份 CMakeLists 能同时支持
#    MSVC 和 GCC/Clang。
#
#  各组选项的理由：
#    MSVC
#      /W4                 对应 GCC 的 -Wall -Wextra：工程最低要求
#      /permissive-        标准两阶段查找；不写就可能在 GCC/Clang 上编不过
#      /utf-8              源码和执行字符集都按 UTF-8（本仓库源码里有中文注释）
#      /Zc:__cplusplus     让 __cplusplus 报真实值（否则恒为 199711L）
#      /EHsc               标准 C++ 异常模型（不开会有 C4530，且 catch 行为可能不符标准）
#      /diagnostics:caret  错误定位到列，模板报错时好读很多
#    GCC / Clang
#      -Wall -Wextra       基本警告
#      -Wpedantic          严格标准符合性（对应 /permissive- 的一部分）
#      -Wshadow            变量遮蔽（对应 MSVC 的 C4456/C4458）
#      -Wconversion        隐式窄化转换（对应 C4244）—— 很吵，但历史坑多
#      -Wnon-virtual-dtor  基类析构不是虚函数（本仓库 06 文件讲的坑）
#
#  ENGINEERING_WARNINGS_AS_ERRORS：
#    顶层开关，默认 ON。理由：/WX 应该在「警告已清零」的项目上一直开着，
#    否则警告会慢慢涨回来。想临时容忍警告：-DENGINEERING_WARNINGS_AS_ERRORS=OFF。
# -----------------------------------------------------------------------------
add_library(project_warnings INTERFACE)

option(ENGINEERING_WARNINGS_AS_ERRORS "把编译警告当作错误（推荐 ON）" ON)

target_compile_options(project_warnings INTERFACE
    # ---- MSVC (cl.exe) ----
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:MSVC>:/permissive->
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:/EHsc>
    $<$<CXX_COMPILER_ID:MSVC>:/diagnostics:caret>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<BOOL:${ENGINEERING_WARNINGS_AS_ERRORS}>>:/WX>

    # ---- GCC / Clang ----
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wpedantic>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wshadow>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wnon-virtual-dtor>
    $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<BOOL:${ENGINEERING_WARNINGS_AS_ERRORS}>>:-Werror>)

# GCC/Clang 下的额外选项：把「未使用参数」等也报出来（用分号分隔的字符串）
# 这里示范两种写法：上面的生成器表达式和下面的列表。
target_compile_options(project_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU>:-Wconversion>
    $<$<CXX_COMPILER_ID:Clang>:-Wconversion>)

# -----------------------------------------------------------------------------
#  第 7 节：Debug / Release 的差异（每一处都影响运行结果，不只是快慢）
# -----------------------------------------------------------------------------
#  CMAKE_BUILD_TYPE=Debug
#    - 不优化（MSVC /Od，GCC -O0），保留调试信息
#    - 定义 _DEBUG（MSVC）/ 不定义 NDEBUG  -> assert 生效
#    - MSVC 用 /MDd（调试版运行时库）；CMake 通过 MSVC_RUNTIME_LIBRARY 自动处理
#  CMAKE_BUILD_TYPE=Release
#    - 优化（MSVC /O2，GCC -O3）
#    - 定义 NDEBUG -> assert 被消除，[[nodiscard]] 仍然生效
#    - MSVC 用 /MD
#
#  为什么这里用「生成器表达式」而不是 if(CMAKE_BUILD_TYPE STREQUAL "Debug")：
#    多配置生成器（Visual Studio）在配置阶段**不知道**当前是哪个配置，
#    每个源文件可能同时按 Debug 和 Release 编译。生成器表达式在生成/构建阶段
#    才展开，所以两种生成器都能正确工作。这是 CMake 里最常见的错误之一。
# -----------------------------------------------------------------------------
target_compile_options(project_warnings INTERFACE
    # Debug：关优化 + 开更多运行期检查
    $<$<CONFIG:Debug>:$<$<CXX_COMPILER_ID:MSVC>:/Od>>
    $<$<CONFIG:Debug>:$<$<CXX_COMPILER_ID:MSVC>:/RTC1>>   # 运行期检查（栈帧/未初始化变量）
    $<$<CONFIG:Debug>:$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O0>>
    # Release：开优化
    $<$<CONFIG:Release>:$<$<CXX_COMPILER_ID:MSVC>:/O2>>
    $<$<CONFIG:Release>:$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O3>>)

# 运行时库：CMake 3.15+ 通过这个变量/属性控制 /MD /MDd。
# 默认行为（不设置）已经完全正确：Debug -> /MDd，Release -> /MD。
# 这里显式写出来是为了「让规则可见」，也顺便演示为什么**不能手动混用**：
# 一旦某个第三方库用 /MDd 而主程序用 /MD，跨模块 free 会直接崩（堆不同）。
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# -----------------------------------------------------------------------------
#  第 8 节：可选功能开关 —— 用 option() 而不是注释掉代码
# -----------------------------------------------------------------------------
#  addr2line/ASan 这种「只在排查时开」的东西，做成开关最实用：
#      cmake -S . -B build-asan -DENGINEERING_ENABLE_ASAN=ON
#  它的代价：运行时约慢 2 倍、内存约 2~3 倍，所以默认 OFF。
#  注意 MSVC 上 ASan 与 /RTC1、增量链接、编辑继续(EnC) 冲突，
#  所以开启时要把 /RTC1 去掉（下面用生成器表达式处理）。
# -----------------------------------------------------------------------------
option(ENGINEERING_ENABLE_ASAN "启用 AddressSanitizer（仅推荐在测试/CI 中使用）" OFF)

# -----------------------------------------------------------------------------
#  第 9 节：本章每个 .cpp 各建一个可执行目标
# -----------------------------------------------------------------------------
#  设计前提：本章每个 .cpp 都自带 int main()，所以「一个源文件 = 一个程序」。
#  （这正是 build.ps1 的做法，CMake 这边保持一致，方便两边结果对照。）
#
#  foreach + add_executable 的理由：
#    1) 加新示例只要往 SOURCES 里加一行，不用复制粘贴一段 CMake；
#    2) 不会出现「加了文件忘了加 target」；
#    3) NAME_WE 自动取「去掉目录和扩展名」的文件名作为 target 名。
#
#  每个 target 都 link 了 project_warnings，所以自动得到第 6 节的警告选项。
#  PRIVATE 的含义：这些选项只用于编译这个 target 自己，不向依赖它的人传播
#  （对比 INTERFACE / PUBLIC，见第 10 节）。
# -----------------------------------------------------------------------------
set(ENGINEERING_SOURCES
    01_compiler_warnings_and_tools.cpp
    02_assert_and_contracts.cpp
    03_logging_and_error_handling.cpp
    04_performance_measurement.cpp
    05_debugging_techniques.cpp
    06_project_structure_and_style.cpp)

foreach(source IN LISTS ENGINEERING_SOURCES)
    # NAME_WE = Without Extension，即 01_compiler_warnings_and_tools
    get_filename_component(target_name "${source}" NAME_WE)

    add_executable(${target_name} "${source}")

    # 继承统一的警告/标准选项
    target_link_libraries(${target_name} PRIVATE project_warnings)

    # 让 VS 的解决方案里有个整齐的筛选器目录（不影响构建，只影响 IDE 显示）
    set_target_properties(${target_name} PROPERTIES
        FOLDER "07-engineering/examples"
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")   # F5 调试时的工作目录

    # ASan 是可选的，只对「示例」生效（测试目标在子目录里单独处理）
    if(ENGINEERING_ENABLE_ASAN)
        if(MSVC)
            target_compile_options(${target_name} PRIVATE /fsanitize=address)
            # /RTC1 与 ASan 不兼容，这里显式去掉
            target_compile_options(${target_name} PRIVATE /RTC-)
        else()
            target_compile_options(${target_name} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
            target_link_options(${target_name} PRIVATE -fsanitize=address)
        endif()
    endif()
endforeach()

# -----------------------------------------------------------------------------
#  第 10 节：把 tests 加进来
# -----------------------------------------------------------------------------
#  add_subdirectory(tests)：让子目录用自己的 CMakeLists.txt 管理测试。
#  好处：示例和测试的构建选项、依赖可以完全不同（测试要链测试框架、
#        要注册 CTest、可能还要单独开 ASan）。
#
#  enable_testing() 通常写在**顶层**（要在 add_subdirectory 之前），
#  这样 CTest 才能看到所有子目录注册的测试。
#  用 include(CTest) 也行：它会调用 enable_testing() 并定义 BUILD_TESTING 选项
#  （默认 ON），这样使用者可以用 -DBUILD_TESTING=OFF 一键关掉所有测试。
# -----------------------------------------------------------------------------
include(CTest)          # 提供 BUILD_TESTING（默认 ON）并调用 enable_testing()
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# -----------------------------------------------------------------------------
#  第 11 节：收尾 —— 把关键配置打印出来，方便排查「为什么编译选项不对」
# -----------------------------------------------------------------------------
#  configure 结束时打印一行摘要，能省掉很多「我明明改了选项」的困惑。
#  真实项目里还常打印：编译器路径与版本、依赖库版本、是否找到可选组件。
# -----------------------------------------------------------------------------
message(STATUS "==========================================================")
message(STATUS "  ${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "  构建类型        : ${CMAKE_BUILD_TYPE}${CMAKE_CONFIGURATION_TYPES}")
message(STATUS "  C++ 标准        : ${CMAKE_CXX_STANDARD}（必须: ${CMAKE_CXX_STANDARD_REQUIRED}）")
message(STATUS "  警告当错误      : ${ENGINEERING_WARNINGS_AS_ERRORS}")
message(STATUS "  AddressSanitizer: ${ENGINEERING_ENABLE_ASAN}")
message(STATUS "  构建测试        : ${BUILD_TESTING}")
message(STATUS "  编译器          : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  生成器          : ${CMAKE_GENERATOR}")
message(STATUS "  可执行输出目录  : ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "  配置命令        : cmake --preset debug / release（见 CMakePresets.json）")
message(STATUS "==========================================================")
