#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <stdexcept>

namespace tictac {

class MmapFile {
public:
    MmapFile() = default;
    explicit MmapFile(const std::filesystem::path& path, bool read_only = true);
    ~MmapFile();

    MmapFile(MmapFile&& other) noexcept;
    MmapFile& operator=(MmapFile&& other) noexcept;

    MmapFile(const MmapFile&) = delete;
    MmapFile& operator=(const MmapFile&) = delete;

    [[nodiscard]] const std::byte* data() const { return static_cast<const std::byte*>(mapping_); }
    [[nodiscard]] std::byte*       data()       { return static_cast<std::byte*>(mapping_); }
    [[nodiscard]] std::size_t      size() const { return size_; }
    [[nodiscard]] bool         is_open() const { return mapping_ != nullptr; }

    /// Re-map after the underlying file has grown.
    void remap();

    /// Typed span access to the mapped region.
    template<typename T>
    [[nodiscard]] std::span<const T> as_span() const {
        if (size_ % sizeof(T) != 0)
            throw std::runtime_error("MmapFile size not aligned for requested type");
        return {reinterpret_cast<const T*>(mapping_), size_ / sizeof(T)};
    }

    template<typename T>
    [[nodiscard]] std::span<T> as_span() {
        if (read_only_)
            throw std::runtime_error("Cannot get mutable span for read-only mapping");
        if (size_ % sizeof(T) != 0)
            throw std::runtime_error("MmapFile size not aligned for requested type");
        return {reinterpret_cast<T*>(mapping_), size_ / sizeof(T)};
    }

private:
    void unmap();

    std::filesystem::path path_;
    int       fd_        = -1;
    void*     mapping_   = nullptr;
    std::size_t size_    = 0;
    bool      read_only_ = true;
};

} // namespace tictac
