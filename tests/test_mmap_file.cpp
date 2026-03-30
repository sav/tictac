#include <catch2/catch_test_macros.hpp>
#include "storage/mmap_file.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static fs::path create_temp_file(const std::string& content) {
    auto path = fs::temp_directory_path() / "tictac_test_mmap.tmp";
    std::ofstream ofs(path, std::ios::binary);
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    return path;
}

TEST_CASE("MmapFile reads file contents", "[mmap]") {
    std::string data = "Hello, mmap world!";
    auto path = create_temp_file(data);

    tictac::MmapFile mf(path);
    REQUIRE(mf.is_open());
    REQUIRE(mf.size() == data.size());
    REQUIRE(std::memcmp(mf.data(), data.data(), data.size()) == 0);

    fs::remove(path);
}

TEST_CASE("MmapFile typed span access", "[mmap]") {
    // Write 4 uint32_t values
    std::vector<std::uint32_t> values = {10, 20, 30, 40};
    auto path = fs::temp_directory_path() / "tictac_test_mmap_span.tmp";
    {
        std::ofstream ofs(path, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(values.data()),
                  static_cast<std::streamsize>(values.size() * sizeof(std::uint32_t)));
    }

    const tictac::MmapFile mf(path);
    auto span = mf.as_span<std::uint32_t>();
    REQUIRE(span.size() == 4);
    REQUIRE(span[0] == 10);
    REQUIRE(span[1] == 20);
    REQUIRE(span[2] == 30);
    REQUIRE(span[3] == 40);

    fs::remove(path);
}

TEST_CASE("MmapFile remap after file grows", "[mmap]") {
    auto path = create_temp_file("AAAA");

    tictac::MmapFile mf(path, false);
    REQUIRE(mf.size() == 4);

    // Grow the file
    {
        std::ofstream ofs(path, std::ios::binary | std::ios::app);
        ofs.write("BBBB", 4);
    }

    mf.remap();
    REQUIRE(mf.size() == 8);
    REQUIRE(std::memcmp(mf.data(), "AAAABBBB", 8) == 0);

    fs::remove(path);
}

TEST_CASE("MmapFile handles empty file", "[mmap]") {
    auto path = create_temp_file("");

    tictac::MmapFile mf(path);
    REQUIRE(mf.size() == 0);
    REQUIRE(mf.data() == nullptr);

    fs::remove(path);
}

TEST_CASE("MmapFile move semantics", "[mmap]") {
    auto path = create_temp_file("test data");

    tictac::MmapFile mf1(path);
    REQUIRE(mf1.is_open());

    tictac::MmapFile mf2(std::move(mf1));
    REQUIRE(mf2.is_open());
    REQUIRE(!mf1.is_open());
    REQUIRE(mf2.size() == 9);

    fs::remove(path);
}
