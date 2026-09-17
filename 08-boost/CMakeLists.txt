# ============================================================================
# 08-boost —— Boost vs STL 对照演示（独立工程）
# ============================================================================
# 这个目录的示例依赖外部 Boost 头文件，所以没有并进顶层 CMakeLists。
# 用法：
#   cmake -S 08-boost -B build-cmake-boost -DCMAKE_BUILD_TYPE=Release -DBOOST_ROOT=D:/boost/boost-1.92.0-b2-nodocs/boost-1.92.0
#   cmake --build build-cmake-boost --config Release
#
# 也可以继续用原来的 VS 工程 Project6.slnx 打开（推荐在 VS 里 Ctrl+F5 跑，
# 因为程序有菜单交互和 ANSI 颜色输出）。
# ============================================================================

cmake_minimum_required(VERSION 3.20)
project(boost_vs_stl LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Boost 是 header-only 用法，所以只需要头文件目录，不需要 find_package / 链接库。
# 换机器时传 -DBOOST_ROOT=<你的 Boost 路径> 即可，默认值与 boost.props 保持一致。
if(NOT BOOST_ROOT)
    set(BOOST_ROOT "D:/boost/boost-1.92.0-b2-nodocs/boost-1.92.0" CACHE PATH "Boost 根目录（含 boost/version.hpp）")
endif()

if(NOT EXISTS "${BOOST_ROOT}/boost/version.hpp")
    message(FATAL_ERROR
        "找不到 Boost 头文件: ${BOOST_ROOT}/boost/version.hpp\n"
        "请用 -DBOOST_ROOT=<路径> 指定正确的 Boost 目录。")
endif()

file(GLOB _srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_executable(boost_vs_stl main.cpp ${_srcs})

target_include_directories(boost_vs_stl PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${BOOST_ROOT}")

if(MSVC)
    target_compile_options(boost_vs_stl PRIVATE
        /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc /bigobj /diagnostics:caret)
    # 说明：Boost 1.92 的 BOOST_SCOPE_EXIT 宏在 m9_error_scope.cpp 里会触发
    # C4459（局部声明隐藏全局声明），这是 Boost 自身的宏展开造成的，不是示例代码的问题，
    # 这里单点关闭以免影响 /WX 流程；其余警告照常保留。
    target_compile_options(boost_vs_stl PRIVATE /wd4459)
else()
    target_compile_options(boost_vs_stl PRIVATE -Wall -Wextra -Wpedantic)
endif()

message(STATUS "08-boost: Boost 头文件目录 = ${BOOST_ROOT}")
