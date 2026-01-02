# Makefile for NebulaStore
# 添加了新的日志系统

CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O2 -g -pthread

# RocksDB 路径
ROCKSDB_DIR = /home/hrp/yig/rocksdb-9.1.0
ROCKSDB_INCLUDE = $(ROCKSDB_DIR)/include
ROCKSDB_LIB = $(ROCKSDB_DIR)/librocksdb.a

# Mongoose 路径
MONGOOSE_DIR = third_party/mongoose
MONGOOSE_SRC = $(MONGOOSE_DIR)/mongoose.c
MONGOOSE_OBJ = $(BUILD_DIR)/mongoose.o

CXXFLAGS += -I$(ROCKSDB_INCLUDE) -I$(MONGOOSE_DIR)
LDFLAGS = $(ROCKSDB_LIB) -lstdc++fs -lz -lm -ldl -lssl -lcrypto -lpthread

# 源目录
SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build

# 目标
TARGET = nebula-master
TEST_TARGET = nebula-test
LOGGER_TEST = logger-test

# 源文件
COMMON_SRCS = $(SRC_DIR)/common/logger.cpp
METADATA_SRCS = $(SRC_DIR)/metadata/metadata_partition.cpp \
                $(SRC_DIR)/metadata/rocksdb_store.cpp \
                $(SRC_DIR)/metadata/metadata_service_impl.cpp \
                $(SRC_DIR)/metadata/slice_tree.cpp
STORAGE_SRCS = $(SRC_DIR)/storage/local_backend.cpp
NAMESPACE_SRCS = $(SRC_DIR)/namespace/service.cpp
PROTOCOL_SRCS = $(SRC_DIR)/protocol/http_server.cpp

MAIN_SRCS = $(SRC_DIR)/master/main.cpp
TEST_SRCS = tests/basic_test.cpp
LOGGER_TEST_SRCS = tests/logger_test.cpp
MODULE_TEST_SRCS = tests/module_test.cpp

# 目标文件
COMMON_OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(COMMON_SRCS))
METADATA_OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(METADATA_SRCS))
STORAGE_OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(STORAGE_SRCS))
PROTOCOL_OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(PROTOCOL_SRCS))
MAIN_OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(MAIN_SRCS))
TEST_OBJS = $(patsubst tests/%.cpp,$(BUILD_DIR)/test_%.o,$(TEST_SRCS))
LOGGER_TEST_OBJS = $(patsubst tests/%.cpp,$(BUILD_DIR)/test_%.o,$(LOGGER_TEST_SRCS))

NAMESPACE_OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(NAMESPACE_SRCS))

MODULE_TEST_OBJS = $(patsubst tests/%.cpp,$(BUILD_DIR)/test_%.o,$(MODULE_TEST_SRCS))

S3_TEST_SRCS = tests/s3_test.cpp
S3_TEST_OBJS = $(patsubst tests/%.cpp,$(BUILD_DIR)/test_%.o,$(S3_TEST_SRCS))

# 基础对象（不含协议层）
BASE_OBJS = $(COMMON_OBJS) $(METADATA_OBJS) $(STORAGE_OBJS) $(NAMESPACE_OBJS)
# 完整对象（含协议层）
ALL_OBJS = $(BASE_OBJS) $(PROTOCOL_OBJS)

.PHONY: all clean test run-test logger-test module-test

all: $(BUILD_DIR)/$(TARGET) $(BUILD_DIR)/$(TEST_TARGET) $(BUILD_DIR)/module-test $(BUILD_DIR)/s3-test

logger-test: $(BUILD_DIR)/$(LOGGER_TEST)
	@echo "Running logger test..."
	@./$(BUILD_DIR)/$(LOGGER_TEST)

$(BUILD_DIR)/$(LOGGER_TEST): $(LOGGER_TEST_OBJS) $(COMMON_OBJS)
	@echo "Linking $@..."
	@$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/$(TARGET): $(MAIN_OBJS) $(ALL_OBJS) $(MONGOOSE_OBJ)
	@echo "Linking $@..."
	@$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/$(TEST_TARGET): $(TEST_OBJS) $(BASE_OBJS)
	@echo "Linking $@..."
	@$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/module-test: $(MODULE_TEST_OBJS) $(BASE_OBJS)
	@echo "Linking $@..."
	@$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

module-test: $(BUILD_DIR)/module-test
	@echo "Running module tests..."
	@./$(BUILD_DIR)/module-test

$(BUILD_DIR)/s3-test: $(S3_TEST_OBJS) $(COMMON_OBJS)
	@echo "Linking $@..."
	@$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

s3-test: $(BUILD_DIR)/s3-test
	@echo "Running S3 tests..."
	@./$(BUILD_DIR)/s3-test

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -c $< -o $@

$(BUILD_DIR)/test_%.o: tests/%.cpp
	@mkdir -p $(@D)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -c $< -o $@

$(BUILD_DIR)/mongoose.o: $(MONGOOSE_SRC)
	@mkdir -p $(@D)
	@echo "Compiling mongoose..."
	@gcc -c $< -o $@ -DMG_ENABLE_LINES=1

test: $(BUILD_DIR)/$(TEST_TARGET)
	@echo "Running tests..."
	@./$(BUILD_DIR)/$(TEST_TARGET)

run-test: test

clean:
	@echo "Cleaning build..."
	@rm -rf $(BUILD_DIR)
