#include <catch2/catch_test_macros.hpp>
#include "utils/FileIO/FileIO.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace swish;

static const std::filesystem::path kTmpPath =
    std::filesystem::temp_directory_path() /
    ("swish_fileio_test_" + std::to_string(std::hash<std::string>{}(__FILE__)) + ".bin");

TEST_CASE("FileIO reads a valid binary file", "[fileio]") {
    {
        std::ofstream f(kTmpPath, std::ios::binary);
        f.write("SWSH", 4);
    }
    auto data = FileIO::readBinaryFile(kTmpPath.string());
    REQUIRE(data.size() == 4);
    REQUIRE(data[0] == 'S');
    REQUIRE(data[1] == 'W');
    REQUIRE(data[2] == 'S');
    REQUIRE(data[3] == 'H');
    std::remove(kTmpPath.string().c_str());
}

TEST_CASE("FileIO preserves all bytes including nulls", "[fileio]") {
    {
        std::ofstream f(kTmpPath, std::ios::binary);
        char buf[4] = {0x00, static_cast<char>(0xFF), 0x0A, 0x7F};
        f.write(buf, 4);
    }
    auto data = FileIO::readBinaryFile(kTmpPath.string());
    REQUIRE(data.size() == 4);
    REQUIRE(static_cast<unsigned char>(data[0]) == 0x00);
    REQUIRE(static_cast<unsigned char>(data[1]) == 0xFF);
    REQUIRE(static_cast<unsigned char>(data[2]) == 0x0A);
    REQUIRE(static_cast<unsigned char>(data[3]) == 0x7F);
    std::remove(kTmpPath.string().c_str());
}

TEST_CASE("FileIO throws on missing file", "[fileio]") {
    REQUIRE_THROWS(FileIO::readBinaryFile("/tmp/swish_no_such_file_xyz_abc.bin"));
}

TEST_CASE("FileIO reads an empty file without error", "[fileio]") {
    {
        std::ofstream f(kTmpPath, std::ios::binary);
        // write nothing
    }
    // readBinaryFile opens with std::ios::ate, reads size 0, returns empty vector.
    auto data = FileIO::readBinaryFile(kTmpPath.string());
    REQUIRE(data.empty());
    std::remove(kTmpPath.string().c_str());
}
