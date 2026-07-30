#pragma once
#include "../core/Document.h"

// Prompts with StandardPutFile; writes binary .rsd to the chosen FSSpec.
// Returns true on success (user confirmed + write succeeded).
bool SaveDocument(Document* doc);

// Prompts with StandardGetFile; reads binary .rsd and replaces *doc.
// On success *doc is replaced with the loaded document and true is returned.
// On cancel or error, *doc is left unchanged and false is returned.
bool LoadDocument(Document*& doc);
