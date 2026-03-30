#include "storage/mmap_file.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace tictac {

MmapFile::MmapFile(const std::filesystem::path& path, bool read_only)
    : path_(path), read_only_(read_only)
{
    int flags = read_only ? O_RDONLY : O_RDWR;
    fd_ = ::open(path.c_str(), flags);
    if (fd_ < 0)
        throw std::runtime_error("MmapFile: cannot open " + path.string() + ": " + std::strerror(errno));

    struct stat st{};
    if (::fstat(fd_, &st) < 0) {
        ::close(fd_);
        fd_ = -1;
        throw std::runtime_error("MmapFile: fstat failed: " + std::string(std::strerror(errno)));
    }

    size_ = static_cast<std::size_t>(st.st_size);
    if (size_ == 0) {
        // Empty file -- valid but no mapping
        return;
    }

    int prot = read_only ? PROT_READ : (PROT_READ | PROT_WRITE);
    mapping_ = ::mmap(nullptr, size_, prot, MAP_SHARED, fd_, 0);
    if (mapping_ == MAP_FAILED) {
        mapping_ = nullptr;
        ::close(fd_);
        fd_ = -1;
        throw std::runtime_error("MmapFile: mmap failed: " + std::string(std::strerror(errno)));
    }
}

MmapFile::~MmapFile() {
    unmap();
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

MmapFile::MmapFile(MmapFile&& other) noexcept
    : path_(std::move(other.path_))
    , fd_(std::exchange(other.fd_, -1))
    , mapping_(std::exchange(other.mapping_, nullptr))
    , size_(std::exchange(other.size_, 0))
    , read_only_(other.read_only_)
{}

MmapFile& MmapFile::operator=(MmapFile&& other) noexcept {
    if (this != &other) {
        unmap();
        if (fd_ >= 0) ::close(fd_);

        path_      = std::move(other.path_);
        fd_        = std::exchange(other.fd_, -1);
        mapping_   = std::exchange(other.mapping_, nullptr);
        size_      = std::exchange(other.size_, 0);
        read_only_ = other.read_only_;
    }
    return *this;
}

void MmapFile::remap() {
    if (fd_ < 0)
        throw std::runtime_error("MmapFile: cannot remap -- file not open");

    struct stat st{};
    if (::fstat(fd_, &st) < 0)
        throw std::runtime_error("MmapFile: fstat failed: " + std::string(std::strerror(errno)));

    auto new_size = static_cast<std::size_t>(st.st_size);

    // Unmap old
    if (mapping_) {
        ::munmap(mapping_, size_);
        mapping_ = nullptr;
    }

    size_ = new_size;
    if (size_ == 0)
        return;

    int prot = read_only_ ? PROT_READ : (PROT_READ | PROT_WRITE);
    mapping_ = ::mmap(nullptr, size_, prot, MAP_SHARED, fd_, 0);
    if (mapping_ == MAP_FAILED) {
        mapping_ = nullptr;
        throw std::runtime_error("MmapFile: remap mmap failed: " + std::string(std::strerror(errno)));
    }
}

void MmapFile::unmap() {
    if (mapping_) {
        ::munmap(mapping_, size_);
        mapping_ = nullptr;
        size_ = 0;
    }
}

} // namespace tictac
