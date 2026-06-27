#pragma once

#include <string>
#include <vector>

namespace swish {

class FileIO {
public:
    // TODO: Read a binary file (SPIR-V shader) into a byte vector.
    // Used by Pipeline to load compiled .spv shaders from disk.
    // Throw if file not found.
    [[nodiscard]] static std::vector<char> readBinaryFile(const std::string& filepath);
};

}  // namespace swish
