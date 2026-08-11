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
};
