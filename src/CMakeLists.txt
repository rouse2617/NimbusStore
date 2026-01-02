# ================================
# NebulaStore 源文件
# ================================

# ================================
# Mongoose 库 (单文件 C 库)
# ================================
add_library(mongoose STATIC
    ${CMAKE_SOURCE_DIR}/third_party/mongoose/mongoose.c
)

target_include_directories(mongoose PUBLIC
    ${CMAKE_SOURCE_DIR}/third_party/mongoose
)

# 设置为 C 语言
set_target_properties(mongoose PROPERTIES
    LINKER_LANGUAGE C
)

# ================================
# 公共库
# ================================
add_library(nebula-common
    common/logger_v2.cpp
)

target_link_libraries(nebula-common
    Threads::Threads
)

# ================================
# 协议库
# ================================
add_library(nebula-protocol
    protocol/http_server.cpp
)

target_include_directories(nebula-protocol PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(nebula-protocol
    nebula-common
    mongoose
    Threads::Threads
)

# ================================
# 主程序
# ================================
add_executable(nebula-master
    master/main.cpp
)

target_link_libraries(nebula-master
    nebula-protocol
    nebula-common
)
