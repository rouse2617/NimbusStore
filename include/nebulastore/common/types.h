#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>

namespace nebulastore {

// ================================
// 基础类型定义
// ================================

using InodeID = uint64_t;
using UserID = uint32_t;
using GroupID = uint32_t;
using Timestamp = uint64_t;

// ================================
// 文件类型
// ================================
enum class FileType : uint32_t {
    kRegular = 1,      // 普通文件
    kDirectory = 2,     // 目录
    kSymlink = 3,       // 符号链接
};

// ================================
// 文件权限
// ================================
struct FileMode {
    uint32_t mode;

    // 权限位
    bool IsReadable() const { return mode & 0400; }
    bool IsWritable() const { return mode & 0200; }
    bool IsExecutable() const { return mode & 0100; }

    // 类型判断
    bool IsRegular() const { return (mode & 0170000) == 0100000; }
    bool IsDirectory() const { return (mode & 0170000) == 0040000; }
    bool IsSymlink() const { return (mode & 0170000) == 0120000; }

    static FileMode FromUint(uint32_t m) { return FileMode{m}; }
};

// ================================
// Inode: 文件元数据
// ================================
struct InodeAttr {
    InodeID inode_id;      // inode 编号
    FileMode mode;          // 权限
    UserID uid;             // 用户 ID
    GroupID gid;            // 组 ID
    uint64_t size;          // 文件大小
    uint64_t mtime;         // 修改时间
    uint64_t ctime;         // 创建时间
    uint64_t nlink;         // 硬链接数
};

// ================================
// Dentry: 目录项
// ================================
struct Dentry {
    std::string name;       // 文件名
    InodeID inode_id;       // inode 编号
    FileType type;          // 文件类型
};

// ================================
// Slice: JuiceFS 风格的数据切片
// ================================
struct SliceInfo {
    uint64_t slice_id;      // slice 唯一 ID
    uint64_t offset;        // 在文件中的偏移
    uint64_t size;          // slice 大小
    std::string storage_key; // 存储键: chunks/{inode}/{slice}
};

// ================================
// FileLayout: 文件布局
// ================================
struct FileLayout {
    InodeID inode_id;
    uint64_t chunk_size;          // chunk 大小 (默认 4MB)
    std::vector<SliceInfo> slices; // slice 列表
};

// ================================
// 错误码
// ================================
enum class ErrorCode : int {
    kOK = 0,
    kNotFound = 2,
    kPermissionDenied = 13,
    kExist = 17,
    kIsDirectory = 21,
    kNotDirectory = 20,
    kInvalidArgument = 22,
    kIOError = 5,
    kNoSpace = 28,
};

class Status {
public:
    Status() : code_(ErrorCode::kOK) {}
    Status(ErrorCode code, const std::string& msg)
        : code_(code), msg_(msg) {}

    bool OK() const { return code_ == ErrorCode::kOK; }
    ErrorCode code() const { return code_; }
    const std::string& message() const { return msg_; }

    static Status Ok() { return Status(); }
    static Status NotFound(const std::string& msg = "") {
        return Status(ErrorCode::kNotFound, msg);
    }
    static Status Exist(const std::string& msg = "") {
        return Status(ErrorCode::kExist, msg);
    }
    static Status InvalidArgument(const std::string& msg = "") {
        return Status(ErrorCode::kInvalidArgument, msg);
    }
    static Status NotDirectory(const std::string& msg = "") {
        return Status(ErrorCode::kNotDirectory, msg);
    }
    static Status IO(const std::string& msg = "") {
        return Status(ErrorCode::kIOError, msg);
    }

private:
    ErrorCode code_;
    std::string msg_;
};

// ================================
// ByteBuffer
// ================================
class ByteBuffer {
public:
    ByteBuffer() = default;
    ByteBuffer(const void* data, size_t size) {
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        data_.assign(ptr, ptr + size);
    }
    ByteBuffer(std::vector<uint8_t>&& data) : data_(std::move(data)) {}

    const uint8_t* data() const { return data_.data(); }
    uint8_t* data() { return data_.data(); }
    size_t size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }

    void assign(const void* ptr, size_t size) {
        const uint8_t* p = static_cast<const uint8_t*>(ptr);
        data_.assign(p, p + size);
    }
    void assign(std::vector<uint8_t>&& vec) { data_ = std::move(vec); }

    std::string ToString() const {
        return std::string(reinterpret_cast<const char*>(data_.data()), data_.size());
    }

private:
    std::vector<uint8_t> data_;
};

// ================================
// 时间工具
// ================================
inline uint64_t NowInSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

inline uint64_t NowInMilliSeconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

} // namespace nebulastore
