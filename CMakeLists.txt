# ============================================================================
# cpp-notes —— 顶层 CMake 配置
# ============================================================================
# 这份 CMakeLists 是「真实项目做法」的示范，和仓库根目录的 build.ps1 是两条并行的路径：
#
#   build.ps1   : 零依赖，用 vswhere 找 VS，直接调 cl.exe。
#                 适合快速验证「所有示例都能编译通过」。日常改完代码跑它最快。
#   CMake       : 真实项目通用的跨平台构建方式，能生成 VS 工程 / Ninja / Makefile。
#                 想练工程能力、想做 CI、想换编译器（GCC/Clang）就用它。
#
# 两者都把每个 .cpp 编成独立可执行文件，因为每个示例都自带 int main()。
#
# 用法（在仓库根目录）：
#   cmake -S . -B build-cmake
#   cmake --build build-cmake --config Release
#   ctest --test-dir build-cmake --output-on-failure
#
# 注意：用 Visual Studio 生成器时 CMAKE_BUILD_TYPE 不生效（那是单配置生成器才用的），
# 要用 --config Release 指定；想用 CMAKE_BUILD_TYPE 请配 -G Ninja。
# ============================================================================

cmake_minimum_required(VERSION 3.20)   # 3.20 起支持 CMakePresets v3 与更完善的工具链处理

project(cpp-notes
    VERSION     1.0.0
    DESCRIPTION "C++ 学习笔记与示例集：基础 / 面向对象 / 现代 C++ / 模板 / STL / 数据结构与算法 / 工程实践"
    LANGUAGES   CXX)

# ---------------------------------------------------------------- 全局设置
# C++20 是必须的：仓库里用了 concepts、ranges、std::format、三路比较等特性。
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)    # 编译器不支持就直接报错，而不是悄悄降级
set(CMAKE_CXX_EXTENSIONS        OFF)   # 关掉编译器扩展，保证代码符合标准、可移植

# 默认构建类型：不指定时单配置生成器会什么都不优化，显式给个默认值避免踩这个坑。
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "构建类型" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# 生成 compile_commands.json，让 clangd / clang-tidy 等工具能理解每个文件的编译方式。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 把可执行文件集中输出，源码目录保持干净。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(_cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_BINARY_DIR}/bin")
endforeach()

# ---------------------------------------------------------------- 警告选项
# 全仓库统一的警告策略：警告是 bug 的早期信号，级别给足。
# 默认不把警告当错误（学习时可能想先把警告看清楚），需要时用下面的开关打开。
add_library(cppnotes_warnings INTERFACE)
if(MSVC)
    target_compile_options(cppnotes_warnings INTERFACE
        /W4              # 高警告级别
        /permissive-     # 关闭宽松模式，强制标准两阶段名字查找
        /utf-8           # 源码按 UTF-8 解析（文件里全是中文注释，不加会 C4819）
        /Zc:__cplusplus  # MSVC 默认把 __cplusplus 报成 199711L，这个开关让它报真实值
        /EHsc            # 标准 C++ 异常模型
        /bigobj          # 模板实例化多时避免 C1128
        /diagnostics:caret)
else()
    target_compile_options(cppnotes_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow)
endif()

# 打开它可以把所有警告升级为错误（提交代码前建议开一次）：
#   cmake -S . -B build-cmake -DCPPNOTES_WARNINGS_AS_ERRORS=ON
option(CPPNOTES_WARNINGS_AS_ERRORS "把编译警告当作错误处理" OFF)
if(CPPNOTES_WARNINGS_AS_ERRORS)
    if(MSVC)
        target_compile_options(cppnotes_warnings INTERFACE /WX)
    else()
        target_compile_options(cppnotes_warnings INTERFACE -Werror)
    endif()
endif()

# ---------------------------------------------------------------- 章节处理
# 每个章节目录里的每个 .cpp 都编成一个独立程序。
# 08-boost 不在这里：它依赖外部 Boost 头文件，用 08-boost/CMakeLists.txt 单独构建。
set(CPPNOTES_CHAPTERS
    01-basics
    02-oop
    03-modern
    04-templates
    05-stl
    06-algorithms
    07-engineering)

set(_all_targets "")
foreach(_chapter IN LISTS CPPNOTES_CHAPTERS)
    set(_dir "${CMAKE_CURRENT_SOURCE_DIR}/${_chapter}")
    if(NOT IS_DIRECTORY "${_dir}")
        message(STATUS "跳过不存在的章节: ${_chapter}")
        continue()
    endif()

    # 只取本章目录第一层的 .cpp。子目录（如 07-engineering/tests）有自己的 CMakeLists。
    file(GLOB _sources CONFIGURE_DEPENDS "${_dir}/*.cpp")
    if(NOT _sources)
        message(STATUS "跳过没有源文件的章节: ${_chapter}")
        continue()
    endif()

    foreach(_src IN LISTS _sources)
        get_filename_component(_name "${_src}" NAME_WE)
        set(_target "${_chapter}_${_name}")
        # 目标名里的连字符换成下划线，避免个别工具处理不了
        string(REPLACE "-" "_" _target "${_target}")

        add_executable(${_target} "${_src}")
        target_link_libraries(${_target} PRIVATE cppnotes_warnings)
        set_target_properties(${_target} PROPERTIES
            FOLDER "${_chapter}"          # 在 VS 解决方案里按章节分组
            VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND _all_targets ${_target})
    endforeach()
endforeach()

if(NOT _all_targets)
    message(WARNING "没有找到任何章节源文件，请检查目录结构。")
endif()

# 子目录里的测试（07-engineering/tests 有自己的 CMakeLists 与 CTest 注册）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/07-engineering/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory("07-engineering/tests")
endif()

# ---------------------------------------------------------------- 便利目标
add_custom_target(run-all
    COMMAND ${CMAKE_COMMAND} -E echo "提示: 用 build.ps1 -Run 可以批量编译并运行所有交互式示例。"
    COMMENT "run-all" VERBATIM)
if(_all_targets)
    add_dependencies(run-all ${_all_targets})
endif()

list(LENGTH _all_targets _n)
message(STATUS "cpp-notes: 共生成 ${_n} 个示例目标 —— 试试: cmake --build build-cmake")
