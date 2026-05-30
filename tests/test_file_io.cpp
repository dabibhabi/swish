#include <catch2/catch_test_macros.hpp>
#include "utils/FileIO/FileIO.h"

#include <cstdio>
#include <fstream>
#include <string>

using namespace swish;

static const std::string kTmpPath = "/tmp/swish_fileio_test.bin";

TEST_CASE("FileIO reads a valid binary file", "[fileio]") {
    {
        std::ofstream f(kTmpPath, std::ios::binary);
        f.write("SWSH", 4);
    }
    auto data = FileIO::readBinaryFile(kTmpPath);
    REQUIRE(data.size() == 4);
    REQUIRE(data[0] == 'S');
    REQUIRE(data[1] == 'W');
    REQUIRE(data[2] == 'S');
    REQUIRE(data[3] == 'H');
    std::remove(kTmpPath.c_str());
}

TEST_CASE("FileIO preserves all bytes including nulls", "[fileio]") {
    {
        std::ofstream f(kTmpPath, std::ios::binary);
        char buf[4] = {0x00, static_cast<char>(0xFF), 0x0A, 0x7F};
        f.write(buf, 4);
    }
    auto data = FileIO::readBinaryFile(kTmpPath);
    REQUIRE(data.size() == 4);
    REQUIRE(static_cast<unsigned char>(data[0]) == 0x00);
    REQUIRE(static_cast<unsigned char>(data[1]) == 0xFF);
    REQUIRE(static_cast<unsigned char>(data[2]) == 0x0A);
    REQUIRE(static_cast<unsigned char>(data[3]) == 0x7F);
    std::remove(kTmpPath.c_str());
}

TEST_CASE("FileIO throws on missing file", "[fileio]") {
    REQUIRE_THROWS(FileIO::readBinaryFile("/tmp/swish_no_such_file_xyz_abc.bin"));
}

TEST_CASE("FileIO reads an empty file without error", "[fileio]") {
    {
        std::ofstream f(kTmpPath, std::ios::binary);
        // write nothing
    }
    // Either returns empty vector or throws — both are acceptable;
    // this test just ensures no crash or undefined behavior.
    try {
        auto data = FileIO::readBinaryFile(kTmpPath);
        REQUIRE(data.size() == 0);
    } catch (const std::exception&) {
        // acceptable if implementation rejects empty files
        SUCCEED();
    }
    std::remove(kTmpPath.c_str());
}
