// PNG/JPEG/GIF decode via vendored stb_image.h (src/thirdparty/stb_image.h,
// v2.30, MIT/public-domain). Trimmed to just the three formats this app
// needs -- STBI_ONLY_JPEG/STBI_ONLY_PNG/STBI_ONLY_GIF auto-excludes BMP/
// TGA/PSD/HDR/PIC/PNM at compile time. STBI_NO_STDIO because this app
// reads files via the classic File Manager (FSRead into a buffer, same as
// the PICT import path), never through a POSIX FILE*, so stb_image's own
// stdio path is never used and isn't worth compiling in. STBI_NO_SIMD
// isn't defined explicitly: stb_image's x86/ARM SIMD paths are already
// gated behind __SSE2__/explicit STBI_NEON, neither of which is ever true
// on this PowerPC target, so they compile out on their own.
//
// GIF note: stb_image's stbi_load_from_memory only ever decodes the FIRST
// frame of an animated GIF (there's no frame-sequence API here) -- fine
// for this app, which has no animation/timeline concept anywhere to begin
// with, so a GIF just becomes a static placed image like anything else.
//
// This is the one piece of this app's image pipeline that touches zero
// Mac OS/Carbon/Toolbox API -- pure standard C reading from a memory
// buffer -- so none of this project's usual "missing Retro68 stub" risk
// class applies to it (see project memory: feedback_build_process).
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#include "../thirdparty/stb_image.h"

#include "ImageDecode.h"

// Hard ceiling on decoded pixel count, checked via the cheap stbi_info_from_memory
// header-only probe BEFORE the real decode runs. A raw RGBA buffer is
// width*height*4 bytes fully uncompressed in memory -- unlike PICT's
// compressed/vector opcode data, this can balloon fast, and this target
// has only 64-128MB of RAM total for the whole app (see CLAUDE.md's
// design principles). 2,000,000 pixels is comfortably past any classic
// Mac desktop-picture resolution (640x480-1024x768) while staying under
// 8MB for the decoded buffer itself.
static const long kMaxDecodedPixels = 2000000L;

bool DecodeImageBytes(const std::vector<UInt8>& fileBytes,
                       std::vector<UInt8>& outRGBA, SInt32& outW, SInt32& outH) {
    if (fileBytes.empty()) return false;

    int infoW = 0, infoH = 0, infoComp = 0;
    if (!stbi_info_from_memory(fileBytes.data(), static_cast<int>(fileBytes.size()),
                                &infoW, &infoH, &infoComp)) {
        return false;  // not a PNG/JPEG this build understands
    }
    if (infoW <= 0 || infoH <= 0 || static_cast<long>(infoW) * infoH > kMaxDecodedPixels) {
        return false;
    }

    int w = 0, h = 0, comp = 0;
    stbi_uc* pixels = stbi_load_from_memory(fileBytes.data(), static_cast<int>(fileBytes.size()),
                                             &w, &h, &comp, 4);  // 4: always normalize to RGBA
    if (!pixels) return false;

    outW = w;
    outH = h;
    outRGBA.assign(pixels, pixels + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    stbi_image_free(pixels);
    return true;
}
