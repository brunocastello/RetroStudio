#pragma once
#include "../core/Document.h"

// Runs the auto-layout pass on all frames in the document (bottom-up, then top-down).
// Call this before every canvas and inspector draw so bounds always reflect the
// current layout configuration.
void RunDocumentLayout(Document* doc);
