# CMake 最低版本号要求
cmake_minimum_required (VERSION 4.0)

# 项目信息
project (main)

# 设置 C/C++ 标准
# set(CMAKE_C_COMPILER /opt/homebrew/opt/ccache/libexec/gcc-14)
set(CMAKE_C_STANDARD 11)

# set(CMAKE_CXX_COMPILER /opt/homebrew/opt/ccache/libexec/g++-14)
set(CMAKE_CXX_STANDARD 17)


# 包含 CppUTest 头文件路径
include_directories(
    ${CMAKE_SOURCE_DIR}/thirdparty/cpputest/include
)

# 添加 CppUTest 子项目（生成静态库）
add_subdirectory(thirdparty/cpputest)

# 定义主程序可执行文件
add_executable(main main.cpp)

target_include_directories(main PRIVATE 
    ${CMAKE_SOURCE_DIR}/thirdparty/cpputest/include
)

# 链接 CppUTest 库和标准库
target_link_libraries(main
    PRIVATE 
        CppUTest       # 来自 add_subdirectory 生成的库
        CppUTestExt    # 来自 add_subdirectory 生成的库
        pthread        # 如果 CppUTest 需要线程支持
)