# ================================
# 测试配置
# ================================

# Logger 测试
add_executable(logger-test
    logger_test.cpp
)

target_link_libraries(logger-test
    nebula-common
    Threads::Threads
)

# 添加测试
add_test(NAME LoggerTest COMMAND logger-test)
