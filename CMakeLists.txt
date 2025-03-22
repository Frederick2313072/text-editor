cmake_minimum_required(VERSION 3.10)
project(kilo C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 编译选项
add_compile_options(-Wall -Wextra -pedantic -O2)

# 源文件
set(SOURCES
    src/main.c
    src/terminal.c
    src/buffer.c
    src/editor.c
    src/row.c
    src/highlight.c
    src/fileio.c
    src/find.c
    src/vim.c
)

# 头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)

# 可执行文件
add_executable(kilo ${SOURCES})

# 链接库
target_link_libraries(kilo PRIVATE)

# 安装
install(TARGETS kilo DESTINATION bin)

# 输出目录
set_target_properties(kilo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# 自定义构建目标
add_custom_target(run
    COMMAND kilo
    DEPENDS kilo
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
) 