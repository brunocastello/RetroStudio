#pragma once
#include "Frame.h"

class Document {
public:
    std::string name = "Untitled";
    std::vector<std::unique_ptr<Frame>> frames;      // top-level frames
    std::vector<std::unique_ptr<Shape>> rootShapes;  // shapes outside any frame
};
