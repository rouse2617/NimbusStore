# ================================
# 公共库
# ================================
add_library(nebula-common
    common/async.cpp
    common/logger.cpp
    common/config.cpp
    common/utils.cpp
)
target_link_libraries(nebula-common
    Threads::Threads
)

# ================================
# 元数据模块
# ================================
add_library(nebula-metadata
    metadata/metadata_service_impl.cpp
    metadata/metadata_partition.cpp
    metadata/rocksdb_store.cpp
    metadata/btree_index.cpp
)
target_link_libraries(nebula-metadata
    nebula-common
    RocksDB::RocksDB  # 需要配置 RocksDB
)

# ================================
# 存储模块
# ================================
add_library(nebula-storage
    storage/s3_backend.cpp
    storage/local_backend.cpp
)
target_link_libraries(nebula-storage
    nebula-common
    OpenSSL::SSL  # 用于 S3 签名
)

# ================================
# 协议模块
# ================================
add_library(nebula-protocol
    protocol/s3_gateway.cpp
    protocol/fuse_client.cpp
)
target_link_libraries(nebula-protocol
    nebula-metadata
    nebula-storage
)

# ================================
# 可执行文件
# ================================
add_executable(nebula-master
    master/main.cpp
)
target_link_libraries(nebula-master
    nebula-metadata
)

add_executable(nebula-s3-gateway
    protocol/s3_gateway_main.cpp
)
target_link_libraries(nebula-s3-gateway
    nebula-protocol
)

add_executable(nebula-fuse
    protocol/fuse_main.cpp
)
target_link_libraries(nebula-fuse
    nebula-protocol
    fuse3  # 需要 libfuse3
)
