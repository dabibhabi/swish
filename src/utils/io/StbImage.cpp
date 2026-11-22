// Single translation unit that compiles the stb_image implementation (include/stb_image.h).
// Every other file includes stb_image.h WITHOUT the define, so the implementation exists once.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
