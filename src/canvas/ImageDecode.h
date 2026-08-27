#pragma once

#include <Carbon.h>
#include <vector>

// Decodes a PNG or JPEG file's raw bytes (already read into memory) into a
// flat top-to-bottom RGBA buffer, 4 bytes/pixel. Rejects (returns false)
// anything whose decoded pixel count would exceed a sane RAM budget for
// this target (see ImageDecode.cpp) before actually decoding, and anything
// stb_image itself can't parse. Wraps the vendored stb_image.h (PNG+JPEG
// only, no OS/Toolbox dependency at all -- see project memory:
// project_image_support_first_pass).
bool DecodeImageBytes(const std::vector<UInt8>& fileBytes,
                       std::vector<UInt8>& outRGBA, SInt32& outW, SInt32& outH);
