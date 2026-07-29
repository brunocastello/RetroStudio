#pragma once
#include "Frame.h"

class Document {
public:
    std::string name = "Untitled";
    std::vector<std::unique_ptr<Frame>> frames;
};
