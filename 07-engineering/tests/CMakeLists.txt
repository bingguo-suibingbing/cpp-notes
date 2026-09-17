# =============================================================================
#  07-engineering/tests/CMakeLists.txt
# -----------------------------------------------------------------------------
#  极简但真实的单元测试构建脚本。
#
#  设计要点：
#    1) 不引入任何第三方依赖（没有 GoogleTest/Catch2），clone 下来就能跑；
#    2) 一个测试 = 一个可执行文件（每个自带 main），和示例代码风格一致；
#    3) 用 CTest 注册，`ctest` 一条命令跑全部，退出码可直接用于 CI；
#    4) 测试代码继承顶层 project_warnings（/W4 /WX），所以测试本身也零警告。
#
#  运行方式：
#      cmake --build --preset debug
#      ctest --preset debug --output-on-failure
#    或在构建目录里直接跑（不通过 CTest，能看到完整输出）：
#      ./bin/test_checked_int.exe
# =============================================================================

# -----------------------------------------------------------------------------
#  第 1 节：一个「测试专用」的辅助静态库
# -----------------------------------------------------------------------------
#  为什么把被测实现（checked_int.cpp）做成库而不是直接塞进每个 test：
#    - 多个测试文件可以共用同一份实现，不用重复编译；
#    - 链接关系清晰：test -> sut -> （无依赖）；
#    - 真实项目里 SUT 通常是 src/ 下的库，测试只是它的消费者。
#
#  STATIC：静态库（不导出符号，链接进每个测试可执行文件，部署最简单）。
#  PRIVATE：project_warnings 只用于编译这个库自己，不向链接它的人传播
#           （因为这个库是「实现」，不是「接口」）。
#  target_include_directories(... PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})：
#    PUBLIC 表示「我自己要这个 include 路径，链接我的人也要」——
#    于是 test_checked_int.cpp 里可以直接写 #include "checked_int.h"。
#    CMake 最佳实践：用「基于 target 的 include 路径」，不要用全局
#    include_directories()（那样会把路径泄漏给所有 target）。
# -----------------------------------------------------------------------------
add_library(eng07_sut STATIC
    checked_int.cpp)

target_link_libraries(eng07_sut PRIVATE project_warnings)
target_include_directories(eng07_sut PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(eng07_sut PROPERTIES FOLDER "07-engineering/tests")

# -----------------------------------------------------------------------------
#  第 2 节：每个测试一个可执行文件 + 用 CTest 注册
# -----------------------------------------------------------------------------
#  foreach 的两个列表必须**一一对应**（这是最简单的写法；复杂项目里
#  可以用「一个列表放 源文件:测试名」或者干脆给每个测试写三行）。
#
#  add_test(NAME ... COMMAND ...)：
#    - NAME 是 ctest 里的测试名（ctest -R <正则> 可以只跑一部分）；
#    - COMMAND 用 $<TARGET_FILE:...> 这种生成器表达式，而不是手写 exe 路径 ——
#      这样多配置生成器（VS）会自动指向当前配置的 Debug/Release 产物。
#
#  WORKING_DIRECTORY：测试运行时的工作目录。设成 CMAKE_CURRENT_BINARY_DIR
#    可以避免测试往源码目录里扔临时文件（真实项目里很常见的一条纪律）。
#
#  注意 ring_buffer 是纯头文件模板库，所以 test_ring_buffer 不需要额外链接
#  任何 .cpp —— 它只依赖头文件（这也是模板库的常见形态）。
# -----------------------------------------------------------------------------
set(TEST_SOURCES
    test_checked_int.cpp
    test_ring_buffer.cpp)

set(TEST_NAMES
    checked_int
    RingBuffer)

list(LENGTH TEST_SOURCES test_count)
math(EXPR test_last_index "${test_count} - 1")

foreach(index RANGE ${test_last_index})
    list(GET TEST_SOURCES ${index} test_source)
    list(GET TEST_NAMES ${index} test_name)

    get_filename_component(target_name "${test_source}" NAME_WE)
    add_executable(${target_name} "${test_source}")

    # 所有测试都链接 project_warnings（统一警告级别）
    target_link_libraries(${target_name} PRIVATE project_warnings)

    # 需要被测实现的测试才链接 sut；ring_buffer 是纯头文件，链接了也无害，
    # 这里为了演示「按需链接」的写法做了区分。
    if(test_name STREQUAL "checked_int")
        target_link_libraries(${target_name} PRIVATE eng07_sut)
    endif()

    set_target_properties(${target_name} PROPERTIES
        FOLDER "07-engineering/tests"
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

    add_test(NAME "07-engineering.${test_name}"
             COMMAND $<TARGET_FILE:${target_name}>
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

    # 给测试设置超时：CI 上「测试卡死」比「测试失败」更麻烦（会一直占着机器）。
    set_tests_properties("07-engineering.${test_name}" PROPERTIES TIMEOUT 60)
endforeach()

# -----------------------------------------------------------------------------
#  第 3 节：可选 —— 用 ASan 跑测试
# -----------------------------------------------------------------------------
#  测试是开 ASan 最合适的地方：覆盖率高、可重复、能接受 2 倍慢。
#  用法：cmake -S .. -B build-asan -DENGINEERING_ENABLE_ASAN=ON
#  想「ASan 只给测试开、示例不开」，把下面这段的 target 列表换成测试目标即可。
#  （本文件里如果顶层已开 ASan，测试也自动继承；这里不再重复设置。）
# -----------------------------------------------------------------------------
if(ENGINEERING_ENABLE_ASAN)
    message(STATUS "  [tests] AddressSanitizer 已启用（由顶层选项控制）")
endif()
