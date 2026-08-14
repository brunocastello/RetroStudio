#include "DocumentSerializer.h"
#include <Navigation.h>
#include <Carbon.h>
#include <cstring>

// NavGetDefaultDialogOptions is in libCarbonLib.a but absent from the
// Multiversal Navigation.h — forward-declare it directly.
extern "C" OSErr NavGetDefaultDialogOptions(NavDialogOptions* outOptions);

// Nav Services event callbacks.
// NewNavEventUPP() calls NewRoutineDescriptor() internally, creating a proper
// CFM RoutineDescriptor — required because CarbonLib dispatches via
// CallUniversalProc which expects a RoutineDescriptor, not a bare code address.
// We use FrontWindow() instead of params->window to avoid NavCBRec struct
// alignment differences between GCC-PPC and mac68k padding.
static pascal void NavSaveEventProc(NavEventCallbackMessage msg, NavCBRecPtr, void*) {
    if (msg == kNavCBStart) {
        WindowRef w = FrontWindow();
        if (w) SetWTitle(w, "\pSave");
    }
}
static pascal void NavOpenEventProc(NavEventCallbackMessage msg, NavCBRecPtr, void*) {
    if (msg == kNavCBStart) {
        WindowRef w = FrontWindow();
        if (w) SetWTitle(w, "\pOpen");
    }
}

static const OSType kCreator = 'RSTD';
static const OSType kDocType = 'RSD ';
static const UInt32 kMagic   = 0x52535444;  // 'RSTD'
static const UInt16 kVersion = 16;

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
    w.w8(s.locked     ? 1 : 0);
    w.w16(s.strokeWidth);
    w.w8(s.strokeAlign);
    w.w8(s.wSizing);
    w.w8(s.hSizing);
    w.w8(s.opacity);
    if (s.GetType() == Shape::kRectangle) {
        const auto& rs = static_cast<const RectShape&>(s);
        w.w16(static_cast<UInt16>(rs.cornerRadius));
        w.w8(rs.cornerIndividual ? 1 : 0);
        w.w16(static_cast<UInt16>(rs.cornerTL)); w.w16(static_cast<UInt16>(rs.cornerTR));
        w.w16(static_cast<UInt16>(rs.cornerBR)); w.w16(static_cast<UInt16>(rs.cornerBL));
    } else if (s.GetType() == Shape::kText) {
        const TextShape& ts = static_cast<const TextShape&>(s);
        w.w16(static_cast<UInt16>(ts.fontSize));
        w.w8(ts.fontFace);
        w.wStr(ts.text);
        w.wStr(ts.fontFamily);
        w.w8(ts.textAlign);
        w.w16(ts.lineHeight);
        w.w16(static_cast<UInt16>(static_cast<SInt16>(ts.letterSpacing)));
        w.w8(static_cast<UInt8>(ts.textSizing));
    }
    w.wStr(s.name);
}

static std::unique_ptr<Shape> ReadShape(Reader& r, UInt16 ver) {
    UInt8 type = r.r8();
    Bounds2 b;
    b.x = r.r32(); b.y = r.r32();
    b.w = r.r32(); b.h = r.r32();
    RGBColor fill   = r.rRGB();
    RGBColor stroke = r.rRGB();
    bool hasFill    = r.r8() != 0;
    bool hasStroke  = r.r8() != 0;
    bool visible    = r.r8() != 0;
    bool locked     = r.r8() != 0;
    UInt16 sw       = r.r16();
    UInt8  sa       = r.r8();
    UInt8  wsz      = r.r8();
    UInt8  hsz      = r.r8();
    UInt8  opac     = (ver >= 16) ? r.r8() : 100;

    std::unique_ptr<Shape> shape;
    if (type == Shape::kRectangle) {
        auto rs = std::make_unique<RectShape>();
        rs->cornerRadius = static_cast<SInt16>(r.r16());
        if (ver >= 15) {
            rs->cornerIndividual = r.r8() != 0;
            rs->cornerTL = static_cast<SInt16>(r.r16()); rs->cornerTR = static_cast<SInt16>(r.r16());
            rs->cornerBR = static_cast<SInt16>(r.r16()); rs->cornerBL = static_cast<SInt16>(r.r16());
        }
        shape = std::move(rs);
    } else if (type == Shape::kText) {
        auto ts          = std::make_unique<TextShape>();
        ts->fontSize     = static_cast<SInt16>(r.r16());
        ts->fontFace     = r.r8();
        ts->text         = r.rStr();
        ts->fontFamily   = r.rStr();
        ts->textAlign    = r.r8();
        ts->lineHeight   = r.r16();
        ts->letterSpacing = static_cast<SInt16>(r.r16());
        ts->textSizing    = static_cast<TextSizing>(r.r8());
        shape = std::move(ts);
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
    shape->locked      = locked;
    shape->strokeWidth = sw;
    shape->strokeAlign = sa;
    shape->wSizing     = wsz;
    shape->hSizing     = hsz;
    shape->opacity     = opac;
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
    w.w8(f.locked      ? 1 : 0);
    w.w8(f.clipContent ? 1 : 0);

    // Auto Layout
    w.w8(static_cast<UInt8>(f.layoutMode));
    w.w8(f.layoutWrap ? 1 : 0);
    w.w16(f.layoutCounterGap);
    w.w8(static_cast<UInt8>((f.strokesInLayout      ? 1 : 0) |
                             (f.canvasStackReverse   ? 2 : 0) |
                             (f.alignTextBaseline    ? 4 : 0) |
                             (f.layoutCounterGapAuto ? 8 : 0)));
    w.w16(f.layoutGap);
    w.w8(f.paddingTop); w.w8(f.paddingRight);
    w.w8(f.paddingBottom); w.w8(f.paddingLeft);
    w.w8(static_cast<UInt8>(f.primaryAlign));
    w.w8(static_cast<UInt8>(f.crossAlign));
    w.w8(static_cast<UInt8>(f.widthSizing));
    w.w8(static_cast<UInt8>(f.heightSizing));
    w.w16(static_cast<UInt16>(f.cornerRadius));
    w.w8(f.cornerIndividual ? 1 : 0);
    w.w16(static_cast<UInt16>(f.cornerTL)); w.w16(static_cast<UInt16>(f.cornerTR));
    w.w16(static_cast<UInt16>(f.cornerBR)); w.w16(static_cast<UInt16>(f.cornerBL));
    w.w8(f.opacity);

    // Interleaved child serialization preserving childOrder z-ordering.
    // If childOrder is empty (legacy), fall back to shapes-then-frames.
    UInt16 nTotal = static_cast<UInt16>(f.children.size() + f.childFrames.size());
    w.w16(nTotal);
    if (!f.childOrder.empty()) {
        for (const auto& cr : f.childOrder) {
            w.w8(cr.isFrame ? 1 : 0);
            if (cr.isFrame) WriteFrame(w, *f.childFrames[cr.idx]);
            else            WriteShape(w, *f.children[cr.idx]);
        }
    } else {
        // Legacy fallback: shapes first, then frames
        for (const auto& s  : f.children)    { w.w8(0); WriteShape(w, *s); }
        for (const auto& cf : f.childFrames) { w.w8(1); WriteFrame(w, *cf); }
    }
}

static std::unique_ptr<Frame> ReadFrame(Reader& r, Frame* parent, UInt16 ver) {
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
    f->locked      = r.r8() != 0;
    f->clipContent = r.r8() != 0;

    // Auto Layout
    f->layoutMode         = static_cast<LayoutMode>(r.r8());
    f->layoutWrap         = r.r8() != 0;
    f->layoutCounterGap   = r.r16();
    { UInt8 fl = r.r8();
      f->strokesInLayout      = (fl & 1) != 0;
      f->canvasStackReverse   = (fl & 2) != 0;
      f->alignTextBaseline    = (fl & 4) != 0;
      f->layoutCounterGapAuto = (fl & 8) != 0; }
    f->layoutGap          = r.r16();
    f->paddingTop    = r.r8(); f->paddingRight  = r.r8();
    f->paddingBottom = r.r8(); f->paddingLeft   = r.r8();
    f->primaryAlign  = static_cast<PrimaryAlign>(r.r8());
    f->crossAlign    = static_cast<CrossAlign>(r.r8());
    f->widthSizing   = static_cast<SizingMode>(r.r8());
    f->heightSizing  = static_cast<SizingMode>(r.r8());
    if (ver >= 14) f->cornerRadius = static_cast<SInt16>(r.r16());
    if (ver >= 15) {
        f->cornerIndividual = r.r8() != 0;
        f->cornerTL = static_cast<SInt16>(r.r16()); f->cornerTR = static_cast<SInt16>(r.r16());
        f->cornerBR = static_cast<SInt16>(r.r16()); f->cornerBL = static_cast<SInt16>(r.r16());
    }
    if (ver >= 16) f->opacity = r.r8();

    // Interleaved child deserialization (v12+). Each entry: type byte (0=shape,1=frame)
    // followed by the serialized child. Rebuilds childOrder alongside typed vectors.
    UInt16 nTotal = r.r16();
    for (UInt16 i = 0; i < nTotal && r.ok; ++i) {
        UInt8 type = r.r8();
        if (type == 1) {
            auto cf = ReadFrame(r, f.get(), ver);
            if (cf) {
                f->childOrder.push_back({ true, static_cast<int>(f->childFrames.size()) });
                f->childFrames.push_back(std::move(cf));
            }
        } else {
            auto s = ReadShape(r, ver);
            if (s) {
                f->childOrder.push_back({ false, static_cast<int>(f->children.size()) });
                f->children.push_back(std::move(s));
            }
        }
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

// Extract FSSpec from a NavReplyRecord selection using AEGetNthPtr (available
// in the Multiversal headers, unlike AEGetDescData which is not).
static OSErr NavReplyToFSSpec(NavReplyRecord& reply, FSSpec& outSpec) {
    AEKeyword keyword;
    DescType  typeCode;
    SInt32    actualSize = 0;
    return AEGetNthPtr(&reply.selection, 1, typeFSS,
                       &keyword, &typeCode,
                       &outSpec, static_cast<SInt32>(sizeof(FSSpec)),
                       &actualSize);
}

bool SaveDocument(Document* doc) {
    if (!doc) return false;

    std::string suggested = EnsureRsdExtension(doc->name);

    NavDialogOptions options = {};
    NavGetDefaultDialogOptions(&options);
    options.version = 0;
    ToPStr31("Save",         options.windowTitle);
    ToPStr31(suggested,      options.savedFileName);
    ToPStr31("RetroStudio",  options.clientName);

    NavEventUPP saveUPP = NewNavEventUPP(NavSaveEventProc);
    NavReplyRecord reply = {};
    OSErr err = NavPutFile(nullptr, &reply, &options, saveUPP, kDocType, kCreator, nullptr);
    DisposeNavEventUPP(saveUPP);
    if (err != noErr || !reply.validRecord) { NavDisposeReply(&reply); return false; }

    FSSpec spec;
    err = NavReplyToFSSpec(reply, spec);
    NavDisposeReply(&reply);
    if (err != noErr) return false;

    FSpDelete(&spec);
    if (FSpCreate(&spec, kCreator, kDocType, smSystemScript) != noErr) return false;

    short refNum;
    if (FSpOpenDF(&spec, fsRdWrPerm, &refNum) != noErr) return false;

    std::string filename;
    for (int i = 1; i <= spec.name[0]; ++i)
        filename += static_cast<char>(spec.name[i]);

    Writer w; w.ref = refNum;

    w.write(&kMagic, 4);
    w.w16(kVersion);
    w.wStr(filename);

    w.w16(static_cast<UInt16>(doc->rootShapes.size()));
    for (const auto& s : doc->rootShapes) WriteShape(w, *s);

    w.w16(static_cast<UInt16>(doc->frames.size()));
    for (const auto& f : doc->frames) WriteFrame(w, *f);

    w.w16(static_cast<UInt16>(doc->rootChildOrder.size()));
    for (const auto& ref : doc->rootChildOrder) {
        w.w8(ref.isFrame ? 1 : 0);
        w.w16(static_cast<UInt16>(ref.idx));
    }

    FSClose(refNum);

    if (w.ok) doc->name = filename;
    return w.ok;
}

bool LoadDocument(Document*& doc) {
    NavDialogOptions options = {};
    NavGetDefaultDialogOptions(&options);
    options.version = 0;
    ToPStr31("Open",         options.windowTitle);
    ToPStr31("RetroStudio",  options.clientName);

    NavEventUPP openUPP = NewNavEventUPP(NavOpenEventProc);
    NavReplyRecord reply = {};
    OSErr err = NavGetFile(nullptr, &reply, &options, openUPP, nullptr, nullptr, nullptr, nullptr);
    DisposeNavEventUPP(openUPP);
    if (err != noErr || !reply.validRecord) { NavDisposeReply(&reply); return false; }

    FSSpec spec;
    err = NavReplyToFSSpec(reply, spec);
    NavDisposeReply(&reply);
    if (err != noErr) return false;

    short refNum;
    if (FSpOpenDF(&spec, fsRdPerm, &refNum) != noErr) return false;

    Reader r; r.ref = refNum;

    UInt32 magic = 0; r.read(&magic, 4);
    UInt16 ver   = r.r16();
    if (!r.ok || magic != kMagic || (ver != 12 && ver != 13 && ver != 14 && ver != 15 && ver != 16)) { FSClose(refNum); return false; }

    auto newDoc  = std::make_unique<Document>();
    newDoc->name = r.rStr();

    UInt16 nRoots = r.r16();
    for (UInt16 i = 0; i < nRoots && r.ok; ++i) {
        auto s = ReadShape(r, ver);
        if (s) newDoc->rootShapes.push_back(std::move(s));
    }

    UInt16 nFrames = r.r16();
    for (UInt16 i = 0; i < nFrames && r.ok; ++i) {
        auto f = ReadFrame(r, nullptr, ver);
        if (f) newDoc->frames.push_back(std::move(f));
    }

    if (ver >= 13) {
        UInt16 nOrder = r.r16();
        for (UInt16 i = 0; i < nOrder && r.ok; ++i) {
            UInt8  isf = r.r8();
            UInt16 idx = r.r16();
            newDoc->rootChildOrder.push_back({ isf != 0, static_cast<int>(idx) });
        }
    } else {
        for (int i = 0; i < (int)newDoc->rootShapes.size(); ++i)
            newDoc->rootChildOrder.push_back({ false, i });
        for (int i = 0; i < (int)newDoc->frames.size(); ++i)
            newDoc->rootChildOrder.push_back({ true, i });
    }

    FSClose(refNum);
    if (!r.ok) return false;

    delete doc;
    doc = newDoc.release();
    return true;
}
