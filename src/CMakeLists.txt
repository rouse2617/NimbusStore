# ================================
# 公共库
# ================================
add_library(yig-common
    common/async.cpp
    common/logger.cpp
    common/config.cpp
    common/utils.cpp
)
target_link_libraries(yig-common
    Threads::Threads
)

# ================================
# 元数据模块
# ================================
add_library(yig-metadata
    metadata/metadata_service_impl.cpp
    metadata/metadata_partition.cpp
    metadata/rocksdb_store.cpp
    metadata/btree_index.cpp
)
target_link_libraries(yig-metadata
    yig-common
    RocksDB::RocksDB  # 需要配置 RocksDB
)

# ================================
# 存储模块
# ================================
add_library(yig-storage
    storage/s3_backend.cpp
    storage/local_backend.cpp
)
target_link_libraries(yig-storage
    yig-common
    OpenSSL::SSL  # 用于 S3 签名
)

# ================================
# 协议模块
# ================================
add_library(yig-protocol
    protocol/s3_gateway.cpp
    protocol/fuse_client.cpp
)
target_link_libraries(yig-protocol
    yig-metadata
    yig-storage
)

# ================================
# 可执行文件
# ================================
add_executable(yig-master
    master/main.cpp
)
target_link_libraries(yig-master
    yig-metadata
)

add_executable(yig-s3-gateway
    protocol/s3_gateway_main.cpp
)
target_link_libraries(yig-s3-gateway
    yig-protocol
)

add_executable(yig-fuse
    protocol/fuse_main.cpp
)
target_link_libraries(yig-fuse
    yig-protocol
    fuse3  # 需要 libfuse3
)
