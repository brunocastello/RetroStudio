#pragma once
#include "../core/Document.h"

// "Save" semantics: writes straight to doc->fileSpec with no prompt if
// doc->hasFile is already true, otherwise behaves exactly like
// SaveDocumentAs (prompts, then remembers the chosen file). Returns true
// on success.
bool SaveDocument(Document* doc);

// "Save As..." semantics: always prompts with Nav Services, then writes
// and remembers the chosen file as doc's new fileSpec (so a later plain
// Save writes straight back to it). Returns true on success.
bool SaveDocumentAs(Document* doc);

// "Save a Copy..." semantics: always prompts and writes, but does NOT
// change doc->fileSpec/hasFile/name -- the document stays associated with
// whatever file it already had (or none). Returns true on success.
bool SaveDocumentCopy(Document* doc);

// "Revert" semantics: reloads doc's content directly from doc->fileSpec
// with no prompt, replacing frames/shapes/rootChildOrder in place (same
// Document*, so callers holding gDocument don't need to re-point it).
// Fails (returns false, doc left unchanged) if doc->hasFile is false.
bool RevertDocument(Document* doc);

// Prompts with Nav Services; reads binary .rsd and replaces *doc.
// On success *doc is replaced with the loaded document (with hasFile set
// and fileSpec pointing at the opened file) and true is returned.
// On cancel or error, *doc is left unchanged and false is returned.
bool LoadDocument(Document*& doc);

// Opens a specific file directly (no prompt) -- used by Open Recent Files.
// Same replace-*doc semantics as LoadDocument.
bool LoadDocumentFromSpec(Document*& doc, const FSSpec& spec);
