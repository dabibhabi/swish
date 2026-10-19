#pragma once

// ── File.h ───────────────────────────────────────────────────────────
// Purpose   : read a whole binary file (SPIR-V shaders, baked configs) into memory.
// Units     : bytes.
// Complexity: O(file size), one allocation; implemented in File.cpp (pulls <fstream>).
// Errors    : throws std::runtime_error if the file cannot be opened or a read is short.
// Example   : auto spv = swish::util::io::read_binary(swish::util::paths::shader_dir() + "taa.frag.spv");
// See also  : renderer/Pipeline/Pipeline.cpp, renderer/DepthOnlyPipeline/DepthOnlyPipeline.cpp,
//             vulkan/ShaderModule.h (consumes the byte vector)
// Test      : tests/utils/test_io_File.cpp

#include <string>
#include <vector>

namespace swish::util::io {

[[nodiscard]] std::vector<char> read_binary(const std::string& filepath);

}  // namespace swish::util::io
