// ================================
// 简化版 Value 编码实现
// ================================

#include <cstring>
#include <arpa/inet.h>

namespace yig::metadata {

// ================================
// Dentry 编码
// ================================

std::string RocksDBStore::EncodeDentryValue(const Dentry& dentry) {
    // 简单编码: name_len(4) + name + inode_id(8) + type(4)
    std::string value;
    uint32_t name_len = htonl(dentry.name.size());
    uint64_t inode_id = htobe64(dentry.inode_id);
    uint32_t type = htonl(static_cast<uint32_t>(dentry.type));

    value.append(reinterpret_cast<const char*>(&name_len), 4);
    value.append(dentry.name);
    value.append(reinterpret_cast<const char*>(&inode_id), 8);
    value.append(reinterpret_cast<const char*>(&type), 4);

    return value;
}

Dentry RocksDBStore::DecodeDentryValue(const std::string& value) {
    Dentry dentry;

    if (value.size() < 16) return dentry;  // 无效数据

    const char* ptr = value.data();

    uint32_t name_len;
    std::memcpy(&name_len, ptr, 4);
    name_len = ntohl(name_len);
    ptr += 4;

    if (value.size() < 4 + name_len + 12) return dentry;

    dentry.name.assign(ptr, name_len);
    ptr += name_len;

    uint64_t inode_id;
    std::memcpy(&inode_id, ptr, 8);
    dentry.inode_id = be64toh(inode_id);
    ptr += 8;

    uint32_t type;
    std::memcpy(&type, ptr, 4);
    dentry.type = static_cast<FileType>(ntohl(type));

    return dentry;
}

// ================================
// Inode 编码
// ================================

std::string RocksDBStore::EncodeInodeValue(const InodeAttr& inode) {
    // 简单编码: inode_id(8) + mode(4) + uid(4) + gid(4) + size(8) + mtime(8) + ctime(8) + nlink(8)
    std::string value;
    uint64_t inode_id = htobe64(inode.inode_id);
    uint32_t mode = htonl(inode.mode.mode);
    uint32_t uid = htonl(inode.uid);
    uint32_t gid = htonl(inode.gid);
    uint64_t size = htobe64(inode.size);
    uint64_t mtime = htobe64(inode.mtime);
    uint64_t ctime = htobe64(inode.ctime);
    uint64_t nlink = htobe64(inode.nlink);

    value.append(reinterpret_cast<const char*>(&inode_id), 8);
    value.append(reinterpret_cast<const char*>(&mode), 4);
    value.append(reinterpret_cast<const char*>(&uid), 4);
    value.append(reinterpret_cast<const char*>(&gid), 4);
    value.append(reinterpret_cast<const char*>(&size), 8);
    value.append(reinterpret_cast<const char*>(&mtime), 8);
    value.append(reinterpret_cast<const char*>(&ctime), 8);
    value.append(reinterpret_cast<const char*>(&nlink), 8);

    return value;
}

InodeAttr RocksDBStore::DecodeInodeValue(const std::string& value) {
    InodeAttr inode{};

    if (value.size() < 52) return inode;  // 无效数据

    const char* ptr = value.data();

    uint64_t inode_id;
    std::memcpy(&inode_id, ptr, 8);
    inode.inode_id = be64toh(inode_id);
    ptr += 8;

    uint32_t mode;
    std::memcpy(&mode, ptr, 4);
    inode.mode.mode = ntohl(mode);
    ptr += 4;

    uint32_t uid;
    std::memcpy(&uid, ptr, 4);
    inode.uid = ntohl(uid);
    ptr += 4;

    uint32_t gid;
    std::memcpy(&gid, ptr, 4);
    inode.gid = ntohl(gid);
    ptr += 4;

    uint64_t size;
    std::memcpy(&size, ptr, 8);
    inode.size = be64toh(size);
    ptr += 8;

    uint64_t mtime;
    std::memcpy(&mtime, ptr, 8);
    inode.mtime = be64toh(mtime);
    ptr += 8;

    uint64_t ctime;
    std::memcpy(&ctime, ptr, 8);
    inode.ctime = be64toh(ctime);
    ptr += 8;

    uint64_t nlink;
    std::memcpy(&nlink, ptr, 8);
    inode.nlink = be64toh(nlink);

    return inode;
}

// ================================
// Layout 编码
// ================================

std::string RocksDBStore::EncodeLayoutValue(const FileLayout& layout) {
    // 简单编码: inode_id(8) + chunk_size(8) + slice_count(4) + slices...
    std::string value;
    uint64_t inode_id = htobe64(layout.inode_id);
    uint64_t chunk_size = htobe64(layout.chunk_size);
    uint32_t slice_count = htonl(layout.slices.size());

    value.append(reinterpret_cast<const char*>(&inode_id), 8);
    value.append(reinterpret_cast<const char*>(&chunk_size), 8);
    value.append(reinterpret_cast<const char*>(&slice_count), 4);

    for (const auto& slice : layout.slices) {
        uint64_t slice_id = htobe64(slice.slice_id);
        uint64_t offset = htobe64(slice.offset);
        uint64_t size = htobe64(slice.size);
        uint32_t key_len = htonl(slice.storage_key.size());

        value.append(reinterpret_cast<const char*>(&slice_id), 8);
        value.append(reinterpret_cast<const char*>(&offset), 8);
        value.append(reinterpret_cast<const char*>(&size), 8);
        value.append(reinterpret_cast<const char*>(&key_len), 4);
        value.append(slice.storage_key);
    }

    return value;
}

FileLayout RocksDBStore::DecodeLayoutValue(const std::string& value) {
    FileLayout layout{};

    if (value.size() < 20) return layout;  // 无效数据

    const char* ptr = value.data();

    uint64_t inode_id;
    std::memcpy(&inode_id, ptr, 8);
    layout.inode_id = be64toh(inode_id);
    ptr += 8;

    uint64_t chunk_size;
    std::memcpy(&chunk_size, ptr, 8);
    layout.chunk_size = be64toh(chunk_size);
    ptr += 8;

    uint32_t slice_count;
    std::memcpy(&slice_count, ptr, 4);
    slice_count = ntohl(slice_count);
    ptr += 4;

    for (uint32_t i = 0; i < slice_count && ptr + 28 <= value.data() + value.size(); ++i) {
        SliceInfo slice;

        uint64_t slice_id;
        std::memcpy(&slice_id, ptr, 8);
        slice.slice_id = be64toh(slice_id);
        ptr += 8;

        uint64_t offset;
        std::memcpy(&offset, ptr, 8);
        slice.offset = be64toh(offset);
        ptr += 8;

        uint64_t size;
        std::memcpy(&size, ptr, 8);
        slice.size = be64toh(size);
        ptr += 8;

        uint32_t key_len;
        std::memcpy(&key_len, ptr, 4);
        key_len = ntohl(key_len);
        ptr += 4;

        if (ptr + key_len <= value.data() + value.size()) {
            slice.storage_key.assign(ptr, key_len);
            ptr += key_len;
            layout.slices.push_back(slice);
        }
    }

    return layout;
}

} // namespace yig::metadata
