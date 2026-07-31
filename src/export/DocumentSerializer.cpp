#include "DocumentSerializer.h"
#include "../ui/RenameDialog.h"
#include <Carbon.h>
#include <cstring>

static const OSType kCreator = 'RSTD';
static const OSType kDocType = 'RSD ';
static const UInt32 kMagic   = 0x52535444;  // 'RSTD'
static const UInt16 kVersion = 2;

// Folder Manager constants — defined here because Retro68 Carbon headers
// don't always expose <Folders.h> constants via <Carbon.h>.
static const short  kRsdOnSystemDisk      = static_cast<short>(-32768); // kOnSystemDisk
static const OSType kRsdDesktopFolderType = 'desk';
static const long   kRsdFsRtDirID        = 2L;                          // fsRtDirID

// --------------------------------------------------------------------------
// Byte-stream writer (wraps an open FSSpec data fork)
// --------------------------------------------------------------------------

struct Writer {
    short ref;
    bool  ok = true;

    void write(const void* p, long n) {
        if (!ok) return;
        long cnt = n;
        if (FSWrite(ref, &cnt, p) != noErr || cnt != n) ok = false;
    }
    void w8 (UInt8  v) { write(&v, 1); }
    void w16(UInt16 v) { write(&v, 2); }
    void w32(SInt32 v) { write(&v, 4); }
    void wRGB(const RGBColor& c) { w16(c.red); w16(c.green); w16(c.blue); }
    void wStr(const std::string& s) {
        UInt8 len = static_cast<UInt8>(s.size() > 255 ? 255 : s.size());
        w8(len);
        if (len) write(s.c_str(), len);
    }
};

// --------------------------------------------------------------------------
// Byte-stream reader
// --------------------------------------------------------------------------

struct Reader {
    short ref;
    bool  ok = true;

    void read(void* p, long n) {
        if (!ok) return;
        long cnt = n;
        OSErr err = FSRead(ref, &cnt, p);
        if (err != noErr || cnt != n) ok = false;
    }
    UInt8  r8()  { UInt8  v = 0; read(&v, 1); return v; }
    UInt16 r16() { UInt16 v = 0; read(&v, 2); return v; }
    SInt32 r32() { SInt32 v = 0; read(&v, 4); return v; }
    RGBColor rRGB() { RGBColor c; c.red = r16(); c.green = r16(); c.blue = r16(); return c; }
    std::string rStr() {
        UInt8 len = r8();
        if (!ok || !len) return "";
        char buf[256] = {};
        read(buf, len);
        return std::string(buf, len);
    }
};

// --------------------------------------------------------------------------
// Shape serialization
// --------------------------------------------------------------------------

static void WriteShape(Writer& w, const Shape& s) {
    w.w8(static_cast<UInt8>(s.GetType()));
    w.w32(s.bounds.x); w.w32(s.bounds.y);
    w.w32(s.bounds.w); w.w32(s.bounds.h);
    w.wRGB(s.fillColor);
    w.wRGB(s.strokeColor);
    w.w8(s.hasFill    ? 1 : 0);
    w.w8(s.hasStroke  ? 1 : 0);
    w.w8(s.visible    ? 1 : 0);
    w.w16(s.strokeWidth);
    w.w8(s.strokeAlign);
    if (s.GetType() == Shape::kRectangle)
        w.w16(static_cast<UInt16>(static_cast<const RectShape&>(s).cornerRadius));
    w.wStr(s.name);
}

static std::unique_ptr<Shape> ReadShape(Reader& r) {
    UInt8 type = r.r8();
    Bounds2 b;
    b.x = r.r32(); b.y = r.r32();
    b.w = r.r32(); b.h = r.r32();
    RGBColor fill   = r.rRGB();
    RGBColor stroke = r.rRGB();
    bool hasFill    = r.r8() != 0;
    bool hasStroke  = r.r8() != 0;
    bool visible    = r.r8() != 0;
    UInt16 sw       = r.r16();
    UInt8  sa       = r.r8();

    std::unique_ptr<Shape> shape;
    if (type == Shape::kRectangle) {
        SInt16 cr = static_cast<SInt16>(r.r16());
        auto rs = std::make_unique<RectShape>();
        rs->cornerRadius = cr;
        shape = std::move(rs);
    } else {
        shape = std::make_unique<EllipseShape>();
    }
    if (!r.ok) return nullptr;

    shape->bounds      = b;
    shape->fillColor   = fill;
    shape->strokeColor = stroke;
    shape->hasFill     = hasFill;
    shape->hasStroke   = hasStroke;
    shape->visible     = visible;
    shape->strokeWidth = sw;
    shape->strokeAlign = sa;
    shape->name        = r.rStr();
    return r.ok ? std::move(shape) : nullptr;
}

// --------------------------------------------------------------------------
// Frame serialization (recursive)
// --------------------------------------------------------------------------

static void WriteFrame(Writer& w, const Frame& f) {
    w.wStr(f.name);
    w.w32(f.bounds.x); w.w32(f.bounds.y);
    w.w32(f.bounds.w); w.w32(f.bounds.h);
    w.wRGB(f.backgroundColor);
    w.w8(f.hasStroke   ? 1 : 0);
    w.wRGB(f.strokeColor);
    w.w16(f.strokeWidth);
    w.w8(f.strokeAlign);
    w.w8(f.visible     ? 1 : 0);
    w.w8(f.clipContent ? 1 : 0);

    w.w16(static_cast<UInt16>(f.children.size()));
    for (const auto& s : f.children)
        WriteShape(w, *s);

    w.w16(static_cast<UInt16>(f.childFrames.size()));
    for (const auto& cf : f.childFrames)
        WriteFrame(w, *cf);
}

static std::unique_ptr<Frame> ReadFrame(Reader& r, Frame* parent) {
    auto f    = std::make_unique<Frame>();
    f->parent = parent;
    f->name   = r.rStr();
    f->bounds.x = r.r32(); f->bounds.y = r.r32();
    f->bounds.w = r.r32(); f->bounds.h = r.r32();
    f->backgroundColor = r.rRGB();
    f->hasStroke   = r.r8() != 0;
    f->strokeColor = r.rRGB();
    f->strokeWidth = r.r16();
    f->strokeAlign = r.r8();
    f->visible     = r.r8() != 0;
    f->clipContent = r.r8() != 0;

    UInt16 nShapes = r.r16();
    for (UInt16 i = 0; i < nShapes && r.ok; ++i) {
        auto s = ReadShape(r);
        if (s) f->children.push_back(std::move(s));
    }

    UInt16 nFrames = r.r16();
    for (UInt16 i = 0; i < nFrames && r.ok; ++i) {
        auto cf = ReadFrame(r, f.get());
        if (cf) f->childFrames.push_back(std::move(cf));
    }

    return r.ok ? std::move(f) : nullptr;
}

// --------------------------------------------------------------------------
// FSSpec helpers — saves/loads relative to the Desktop folder
// --------------------------------------------------------------------------

// Ensure filename ends with ".rsd"
static std::string EnsureRsdExtension(const std::string& name) {
    if (name.size() >= 4 && name.substr(name.size() - 4) == ".rsd")
        return name;
    return name + ".rsd";
}

// Build a Pascal filename from a C++ string (max 31 chars for HFS)
static void ToPStr31(const std::string& src, Str255& dst) {
    dst[0] = 0;
    for (int i = 0; src[i] && i < 31; ++i) {
        dst[i + 1] = static_cast<unsigned char>(src[i]); dst[0]++;
    }
}

// Locate the Desktop folder; falls back to startup disk root on error.
static void GetDesktopSpec(short& outVRefNum, long& outDirID) {
    if (FindFolder(kRsdOnSystemDisk, kRsdDesktopFolderType, false,
                   &outVRefNum, &outDirID) != noErr) {
        outVRefNum = 0;             // default volume
        outDirID   = kRsdFsRtDirID;
    }
}

// --------------------------------------------------------------------------
// Public API
// --------------------------------------------------------------------------

bool SaveDocument(Document* doc) {
    if (!doc) return false;

    // Ask user for a filename via the shared rename-style popup
    Point anchor = { 300, 350 };  // roughly screen center
    std::string suggested = EnsureRsdExtension(doc->name);
    std::string filename  = ShowRenameDialog(suggested, anchor);
    if (filename.empty()) return false;  // user cancelled
    filename = EnsureRsdExtension(filename);

    short vRefNum; long dirID;
    GetDesktopSpec(vRefNum, dirID);

    Str255 pname; ToPStr31(filename, pname);
    FSSpec spec;
    FSMakeFSSpec(vRefNum, dirID, pname, &spec);

    // Replace existing file if present
    FSpDelete(&spec);
    if (FSpCreate(&spec, kCreator, kDocType, smSystemScript) != noErr)
        return false;

    short refNum;
    if (FSpOpenDF(&spec, fsRdWrPerm, &refNum) != noErr) return false;

    Writer w; w.ref = refNum;

    w.write(&kMagic, 4);
    w.w16(kVersion);
    w.wStr(doc->name);

    w.w16(static_cast<UInt16>(doc->rootShapes.size()));
    for (const auto& s : doc->rootShapes) WriteShape(w, *s);

    w.w16(static_cast<UInt16>(doc->frames.size()));
    for (const auto& f : doc->frames) WriteFrame(w, *f);

    FSClose(refNum);

    if (w.ok) doc->name = filename;
    return w.ok;
}

bool LoadDocument(Document*& doc) {
    // Ask user for the filename
    Point anchor = { 300, 350 };
    std::string filename = ShowRenameDialog("", anchor);
    if (filename.empty()) return false;
    filename = EnsureRsdExtension(filename);

    short vRefNum; long dirID;
    GetDesktopSpec(vRefNum, dirID);

    Str255 pname; ToPStr31(filename, pname);
    FSSpec spec;
    if (FSMakeFSSpec(vRefNum, dirID, pname, &spec) != noErr) return false;

    short refNum;
    if (FSpOpenDF(&spec, fsRdPerm, &refNum) != noErr) return false;

    Reader r; r.ref = refNum;

    UInt32 magic = 0; r.read(&magic, 4);
    UInt16 ver   = r.r16();
    if (!r.ok || magic != kMagic || ver != kVersion) { FSClose(refNum); return false; }

    auto newDoc  = std::make_unique<Document>();
    newDoc->name = r.rStr();

    UInt16 nRoots = r.r16();
    for (UInt16 i = 0; i < nRoots && r.ok; ++i) {
        auto s = ReadShape(r);
        if (s) newDoc->rootShapes.push_back(std::move(s));
    }

    UInt16 nFrames = r.r16();
    for (UInt16 i = 0; i < nFrames && r.ok; ++i) {
        auto f = ReadFrame(r, nullptr);
        if (f) newDoc->frames.push_back(std::move(f));
    }

    FSClose(refNum);
    if (!r.ok) return false;

    delete doc;
    doc = newDoc.release();
    return true;
}
