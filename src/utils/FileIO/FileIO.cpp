#include "FileIO.h"

#include <fstream>
#include <stdexcept>

namespace swish {

std::vector<char> FileIO::readBinaryFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + filepath);
    }

    size_t            fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
    if (file.gcount() != static_cast<std::streamsize>(fileSize)) {
        throw std::runtime_error("FileIO: partial read of " + filepath + " (expected " + std::to_string(fileSize) +
                                 " bytes)");
    }

    return buffer;
}

}  // namespace swish
