#pragma once
#include "Frame.h"

class Document {
public:
    std::string name = "Untitled";
    std::vector<std::unique_ptr<Frame>> frames;      // top-level frames
    std::vector<std::unique_ptr<Shape>> rootShapes;  // shapes outside any frame
    // Unified z-order for root-level items: rootChildOrder[0] = top of layers panel.
    // isFrame=true → frames[idx], isFrame=false → rootShapes[idx].
    std::vector<ChildRef> rootChildOrder;

    // Set once this document has been saved to or opened from a real file;
    // Save writes straight back to fileSpec (no prompt) when true, otherwise
    // Save behaves like Save As. Revert reloads directly from fileSpec.
    FSSpec fileSpec = {};
    bool   hasFile  = false;
};
