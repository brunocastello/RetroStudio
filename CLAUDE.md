# RetroStudio — Mac OS 9 Vector Design & Prototyping Application

RetroStudio is a native, offline 2D vector design and interactive prototyping application built specifically for Classic Mac OS (Mac OS 9 / Carbon PowerPC). It combines modern vector canvas capabilities with interactive frame-to-frame prototype linking, offline HTML/JS client previews, and modern Figma interoperability via SVG + JSON manifests.

---

## 1. Project Overview & Vision

* **App Name:** RetroStudio
* **Target System:** Mac OS 9.2.2 (PowerPC G3/G4/G5) / Carbon Library 1.6+
* **Primary Paradigm:** 100% Local-First & Completely Offline
* **Key Capabilities:**
  * Vector canvas editing (rectangles, ellipses, paths, typography, layers, groups).
  * Interactive prototyping state machine (triggers, hotspots, screen transitions).
  * Offline hybrid web export (static HTML `<map>` fallback + ES3 JS image swapper) compatible with vintage browsers (IE5 Mac, Classilla, Netscape, iCab) and PowerPC browsers (PowerFox).
  * Fidelity export pipeline to modern Figma via layered SVG + `manifest.json` accompanied by a custom Figma plugin script.

---

## 2. Architecture & Technical Stack

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           RETROSTUDIO ARCHITECTURE                          │
├──────────────────────┬──────────────────────────────────────────────────────┤
│ Core Engine          │ C++17 (compiled to PPC PEF via Retro68 GCC)           │
│ User Interface       │ Mac OS Carbon Toolbox APIs / QuickDraw              │
│ Canvas Rendering     │ Offscreen GWorld buffers + dirty-rect rasterization   │
│ Prototyping Engine   │ Native C++ Event Graph & State Machine              │
│ Local Storage        │ Custom binary document format + XML/JSON bundle      │
│ Web Export Generator │ Hybrid HTML <map> + ES3 JS progressive enhancement  │
│ Figma Sync Pipeline  │ SVG vector stream + manifest.json metadata bundle   │
└──────────────────────┴──────────────────────────────────────────────────────┘
```

### Core Design Rules
* **No Cloud / No External Dependencies:** Storage uses local filesystems (`FSSpec` / `FSRef`).
* **Cooperative Multitasking Friendly:** Yield execution gracefully via `WaitNextEvent()` during heavy vector calculations or bitmap exports.
* **Memory Safety & Efficiency:** Fixed-point math for transformations; optimize for 60-300 MHz PowerPC G3/G4 systems with limited RAM (64MB–128MB).
* **Modern C++ Standard:** Use clean, standard C++17 abstractions supported by GCC inside Retro68, avoiding platform-specific non-standard compiler extensions.

---

## 3. Toolchain & CI Pipeline

* **Cross-Compiler:** [Retro68](https://github.com/autc04/Retro68) (GCC Toolchain targeting Classic Macintosh PowerPC).
* **Build System:** CMake (utilizing `retro68.toolchain.cmake`).
* **Automation:** GitHub Actions running Retro68 Docker container (`ghcr.io/autc04/retro68`).
* **Output Artifact:** Mountable `.img` floppy/hard disk image containing the Macintosh PEF executable (`RetroStudio.bin` / `RetroStudio.app`).

---

## 4. Repository Structure

```
RetroStudio/
├── .github/
│   └── workflows/
│       └── build.yml             # GitHub Actions CI build pipeline
├── CMakeLists.txt                # CMake project configuration
├── CLAUDE.md                     # Project blueprint & instructions for Claude
├── LICENSE                       # License file
├── README.md                     # General repository description
├── docs/                         # Specifications & architecture docs
│   ├── Figma-Plugin-Spec.md
│   ├── Prototype-Export-Format.md
│   └── RetroStudio-Format.md
├── src/                          # C++ Source Code
│   ├── main.cpp                  # Application entry point & Mac OS event loop
│   ├── core/                     # Core data structures (Vector, Color, Matrix)
│   ├── canvas/                   # Scene graph, layers, and rasterizer
│   ├── prototype/                # Interactive state machine & hotspots
│   ├── export/                   # Web exporter & SVG/JSON serializers
│   └── ui/                       # Carbon window, menu, and palette management
└── figma-plugin/                 # Companion plugin script for Modern Figma
    ├── manifest.json
    └── code.js
```

---

## 5. File Specifications & Code Snippets

### 5.1 `CMakeLists.txt`
```cmake
cmake_minimum_required(VERSION 3.15)
project(RetroStudio C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
file(GLOB_RECURSE SOURCES
    "src/*.cpp"
    "src/*.c"
)

# Retro68 target executable setup
add_application(RetroStudio ${SOURCES}
    CONSOLE
    CREATOR "RSTD"
    TYPE "APPL"
)

target_include_directories(RetroStudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Mac OS Carbon / QuickDraw linking flags managed by Retro68 toolchain
```

### 5.2 `.github/workflows/build.yml`
```yaml
name: Build RetroStudio for Mac OS 9

on:
  push:
    branches: [ main, master ]
  pull_request:
    branches: [ main, master ]
  workflow_dispatch:

jobs:
  build:
    runs-on: ubuntu-latest
    container:
      image: ghcr.io/autc04/retro68:latest

    steps:
      - name: Checkout Code
        uses: actions/checkout@v4

      - name: Configure CMake with Retro68 Toolchain
        run: |
          mkdir build && cd build
          cmake ..             -DCMAKE_TOOLCHAIN_FILE=/Retro68-build/toolchain/retro68.toolchain.cmake             -DCMAKE_BUILD_TYPE=Release

      - name: Build Executable and Disk Image
        run: |
          cd build
          make RetroStudio

      - name: Package Disk Image Artifact (.img)
        run: |
          mkdir -p output
          # Copy generated disk image file from Retro68 build output
          find build -name "*.img" -exec cp {} output/ \; || true
          find build -name "*.bin" -exec cp {} output/ \; || true
          find build -name "*.dsk" -exec cp {} output/ \; || true

      - name: Upload Build Artifacts
        uses: actions/upload-artifact@v4
        with:
          name: RetroStudio-OS9-DiskImage
          path: output/*
```

### 5.3 Starter `src/main.cpp`
```cpp
#include <Carbon/Carbon.h>
#include <iostream>

// RetroStudio Application Entry Point for Mac OS 9 Carbon
void InitializeMacintosh() {
    InitCursor();
}

int main(int argc, char* argv[]) {
    InitializeMacintosh();
    
    // Core Event Loop Skeleton
    Boolean quitFlag = false;
    EventRecord event;
    
    while (!quitFlag) {
        if (WaitNextEvent(everyEvent, &event, 15, NULL)) {
            switch (event.what) {
                case mouseDown:
                    // Handle window clicks, toolbars, canvas interaction
                    break;
                case keyDown:
                case autoKey:
                    // Handle keyboard shortcuts
                    if ((event.modifiers & cmdKey) && ((event.message & charCodeMask) == 'q')) {
                        quitFlag = true;
                    }
                    break;
                case updateEvt:
                    // Redraw dirty rects in offscreen GWorld
                    break;
            }
        }
    }
    
    return 0;
}
```

---

## 6. Guidelines for Claude (AI Assistant Instructions)

When generating, modifying, or refactoring code for **RetroStudio**, adhere strictly to these rules:

1. **Carbon API Compliance:** Always use Classic Mac OS Carbon / Toolbox functions (`NewWindow`, `GetPort`, `CopyBits`, `WaitNextEvent`, `FSSpec`). Do NOT use modern OS X/macOS APIs (`NSView`, `Cocoa`, `AppKit`, `CoreGraphics`).
2. **Standard C++ Math & Data Structures:** Use Standard Library constructs (`std::vector`, `std::string`, `std::unique_ptr`, `std::map`) for engine architecture. Retro68 compiles these seamlessly for PowerPC.
3. **No Dynamic JS Dependencies:** All web exports generated by C++ must emit plain ES3 JavaScript paired with vanilla HTML `<map>` fallback structures.
4. **Memory Management:** Avoid excessive dynamic allocations inside the main event loop to preserve memory stability on low-RAM PowerPC machines. Use offscreen `GWorld` pointers safely and unlock handles when done.
5. **Git & Build Diagnostics:** When build failures occur in GitHub Actions, analyze the compiler output from GCC/Retro68 line-by-line and fix syntax or missing Carbon headers accordingly.
