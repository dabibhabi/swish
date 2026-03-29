// stb_image implementation — compiled exactly once.
// The header is in include/stb_image.h (single-header library by Sean Barrett).
// STB_IMAGE_IMPLEMENTATION tells the preprocessor to include the actual
// function definitions, not just declarations. If you include this define
// in multiple .cpp files, you get duplicate symbol linker errors.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
