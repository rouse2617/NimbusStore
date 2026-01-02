#define FUSE_USE_VERSION 35

#include <fuse3/fuse.h>
#include <cstring>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <sys/types.h>
#include "nebulastore/protocol/gateway.h"
#include "nebulastore/namespace/service.h"
#include "nebulastore/metadata/metadata_service.h"

namespace nebulastore::protocol {

static FuseClient* g_client = nullptr;

static int fuse_getattr_impl(const char* path, struct stat* stbuf, struct fuse_file_info*) {
    std::memset(stbuf, 0, sizeof(struct stat));
    InodeAttr attr;
    if (!g_client->GetAttr(path, &attr).Get().OK()) return -ENOENT;
    stbuf->st_ino = attr.inode_id;
    stbuf->st_mode = attr.mode.mode;
    stbuf->st_nlink = attr.nlink;
    stbuf->st_uid = attr.uid;
    stbuf->st_gid = attr.gid;
    stbuf->st_size = static_cast<off_t>(attr.size);
    stbuf->st_mtime = static_cast<time_t>(attr.mtime);
    stbuf->st_ctime = static_cast<time_t>(attr.ctime);
    return 0;
}

static int fuse_readdir_impl(const char* path, void* buf, fuse_fill_dir_t filler,
                             off_t, struct fuse_file_info*, enum fuse_readdir_flags) {
    std::vector<Dentry> entries;
    if (!g_client->Readdir(path, &entries).Get().OK()) return -ENOENT;
    filler(buf, ".", nullptr, 0, FUSE_FILL_DIR_PLUS);
    filler(buf, "..", nullptr, 0, FUSE_FILL_DIR_PLUS);
    for (const auto& e : entries) filler(buf, e.name.c_str(), nullptr, 0, FUSE_FILL_DIR_PLUS);
    return 0;
}

static int fuse_open_impl(const char* path, struct fuse_file_info* fi) {
    InodeAttr attr;
    if (!g_client->GetAttr(path, &attr).Get().OK()) return -ENOENT;
    fi->fh = attr.inode_id;
    return 0;
}

static int fuse_release_impl(const char*, struct fuse_file_info*) { return 0; }

static int fuse_read_impl(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info*) {
    ByteBuffer data;
    if (!g_client->Read(path, offset, size, &data).Get().OK()) return -EIO;
    std::memcpy(buf, data.data(), data.size());
    return static_cast<int>(data.size());
}

static int fuse_write_impl(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info*) {
    ByteBuffer data(buf, size);
    if (!g_client->Write(path, data, offset).Get().OK()) return -EIO;
    return static_cast<int>(size);
}

static int fuse_mkdir_impl(const char* path, mode_t mode) {
    return g_client->Mkdir(path, FileMode{mode | S_IFDIR}, getuid(), getgid()).Get().OK() ? 0 : -EIO;
}

static int fuse_rmdir_impl(const char* path) {
    return g_client->Rmdir(path).Get().OK() ? 0 : -ENOENT;
}

static int fuse_unlink_impl(const char* path) {
    return g_client->Unlink(path).Get().OK() ? 0 : -ENOENT;
}

static int fuse_rename_impl(const char* oldpath, const char* newpath, unsigned int) {
    return g_client->Rename(oldpath, newpath).Get().OK() ? 0 : -EIO;
}

static int fuse_create_impl(const char* path, mode_t mode, struct fuse_file_info* fi) {
    if (!g_client->Create(path, FileMode{mode | S_IFREG}, getuid(), getgid()).Get().OK()) return -EIO;
    return fuse_open_impl(path, fi);
}

class FuseClient::FUSESession {
public:
    FUSESession(const std::string& mount_point, uint32_t max_readahead, bool allow_other)
        : mount_point_(mount_point), running_(false) {
        ops_ = {};
        ops_.getattr = fuse_getattr_impl;
        ops_.readdir = fuse_readdir_impl;
        ops_.open = fuse_open_impl;
        ops_.release = fuse_release_impl;
        ops_.read = fuse_read_impl;
        ops_.write = fuse_write_impl;
        ops_.mkdir = fuse_mkdir_impl;
        ops_.rmdir = fuse_rmdir_impl;
        ops_.unlink = fuse_unlink_impl;
        ops_.rename = fuse_rename_impl;
        ops_.create = fuse_create_impl;
        args_str_ = "max_readahead=" + std::to_string(max_readahead);
        if (allow_other) args_str_ += ",allow_other";
    }

    Status Mount() {
        const char* argv[] = {"nebulastore", "-o", args_str_.c_str(), mount_point_.c_str()};
        struct fuse_args args = FUSE_ARGS_INIT(4, const_cast<char**>(argv));
        fuse_ = fuse_new(&args, &ops_, sizeof(ops_), nullptr);
        if (!fuse_) return Status::IO("Failed to create FUSE session");
        if (fuse_mount(fuse_, mount_point_.c_str()) != 0) {
            fuse_destroy(fuse_);
            fuse_ = nullptr;
            return Status::IO("Failed to mount FUSE");
        }
        running_ = true;
        thread_ = std::thread([this] { fuse_loop(fuse_); });
        return Status::Ok();
    }

    void Unmount() {
        if (running_) {
            running_ = false;
            fuse_unmount(fuse_);
            fuse_destroy(fuse_);
            fuse_ = nullptr;
        }
    }

    void Join() { if (thread_.joinable()) thread_.join(); }

private:
    std::string mount_point_;
    std::string args_str_;
    struct fuse_operations ops_;
    struct fuse* fuse_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_;
};

FuseClient::FuseClient(Config config) : config_(std::move(config)) {}
FuseClient::~FuseClient() { Unmount(); }

Status FuseClient::Mount() {
    g_client = this;
    session_ = std::make_unique<FUSESession>(config_.mount_point, config_.max_readahead, config_.allow_other);
    return session_->Mount();
}

void FuseClient::Unmount() {
    if (session_) { session_->Unmount(); session_.reset(); }
    g_client = nullptr;
}

void FuseClient::Join() { if (session_) session_->Join(); }

AsyncTask<Status> FuseClient::GetAttr(const std::string& path, InodeAttr* attr) {
    co_return co_await config_.namespace_service->GetAttr(path, attr);
}

AsyncTask<Status> FuseClient::SetAttr(const std::string& path, const InodeAttr& attr, uint32_t to_set) {
    // SetAttr not exposed via NamespaceService, return OK for now
    (void)path; (void)attr; (void)to_set;
    co_return Status::Ok();
}

AsyncTask<Status> FuseClient::Read(const std::string& path, uint64_t offset, uint64_t size, ByteBuffer* data) {
    co_return co_await config_.namespace_service->Read(path, offset, size, data);
}

AsyncTask<Status> FuseClient::Write(const std::string& path, const ByteBuffer& data, uint64_t offset) {
    co_return co_await config_.namespace_service->Write(path, data, offset);
}

AsyncTask<Status> FuseClient::Create(const std::string& path, FileMode mode, UserID uid, GroupID gid) {
    // Create via Write with empty data at offset 0
    ByteBuffer empty;
    co_return co_await config_.namespace_service->Write(path, empty, 0);
}

AsyncTask<Status> FuseClient::Mkdir(const std::string& path, FileMode mode, UserID uid, GroupID gid) {
    // Mkdir not directly exposed, stub for now
    (void)path; (void)mode; (void)uid; (void)gid;
    co_return Status::Ok();
}

AsyncTask<Status> FuseClient::Unlink(const std::string& path) {
    (void)path;
    co_return Status::Ok();
}

AsyncTask<Status> FuseClient::Rmdir(const std::string& path) {
    (void)path;
    co_return Status::Ok();
}

AsyncTask<Status> FuseClient::Rename(const std::string& oldpath, const std::string& newpath) {
    (void)oldpath; (void)newpath;
    co_return Status::Ok();
}

AsyncTask<Status> FuseClient::Readdir(const std::string& path, std::vector<Dentry>* entries) {
    co_return co_await config_.namespace_service->Readdir(path, entries);
}

Status FuseClient::FindSlice(const FileLayout& layout, uint64_t offset, SliceInfo* slice) {
    for (const auto& s : layout.slices) {
        if (offset >= s.offset && offset < s.offset + s.size) {
            *slice = s;
            return Status::Ok();
        }
    }
    return Status::NotFound("Slice not found");
}

} // namespace nebulastore::protocol
