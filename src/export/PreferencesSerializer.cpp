#include "PreferencesSerializer.h"
#include <cstring>
#include <string>

// Folder Manager constant -- Retro68 Carbon headers don't always expose
// <Folders.h> constants via <Carbon.h> (same gap DocumentSerializer.cpp
// already works around for the Desktop folder type).
static const short  kRsdOnSystemDisk          = static_cast<short>(-32768); // kOnSystemDisk
static const OSType kRsdPreferencesFolderType = 'pref';
static const long   kRsdFsRtParID             = 1L;  // fsRtParID: root dir's own "parent" sentinel

static const OSType kCreator  = 'RSTD';
static const OSType kPrefType = 'pref';
static const UInt32 kMagic    = 0x52535450; // 'RSTP'
static const UInt16 kVersion  = 1;
static const char*  kPrefsFileName = "RetroStudio Prefs";

// Builds the full colon-delimited HFS pathname for spec ("Volume:Folder:File")
// by walking parent directory IDs via PBGetCatInfoSync -- the classic,
// pre-Alias-Manager technique for a file reference that survives a reboot.
// A raw FSSpec's vRefNum is only a *session* volume reference number and is
// not guaranteed stable across unmount/remount, which is why a path string
// -- not the FSSpec triple itself -- is what gets persisted to disk.
static bool FSSpecToFullPath(const FSSpec& spec, std::string& outPath) {
    std::string path;
    for (int i = 1; i <= spec.name[0]; ++i) path += static_cast<char>(spec.name[i]);

    long dirID = spec.parID;
    while (dirID != kRsdFsRtParID) {
        CInfoPBRec pb;
        memset(&pb, 0, sizeof(pb));
        Str255 dirName;
        dirName[0] = 0;
        pb.dirInfo.ioNamePtr   = dirName;
        pb.dirInfo.ioVRefNum   = spec.vRefNum;
        pb.dirInfo.ioDrDirID   = dirID;
        pb.dirInfo.ioFDirIndex = -1;  // -1: get info about ioDrDirID itself
        if (PBGetCatInfoSync(&pb) != noErr) return false;

        std::string seg;
        for (int i = 1; i <= dirName[0]; ++i) seg += static_cast<char>(dirName[i]);
        path = seg + ":" + path;
        dirID = pb.dirInfo.ioDrParID;
    }
    outPath = path;
    return true;
}

// Resolves a full colon-delimited pathname back to an FSSpec. vRefNum=0 +
// parID=0 tells FSMakeFSSpec the name is a full path rather than a leaf
// name inside (vRefNum, parID). Only succeeds if the file genuinely exists
// right now -- a stale/moved entry should simply not come back on load.
static bool FullPathToFSSpec(const std::string& path, FSSpec& outSpec) {
    Str255 ps;
    UInt8 len = static_cast<UInt8>(path.size() > 255 ? 255 : path.size());
    ps[0] = len;
    for (int i = 0; i < len; ++i) ps[i + 1] = static_cast<unsigned char>(path[i]);
    return FSMakeFSSpec(0, 0, ps, &outSpec) == noErr;
}

// Locates (and, if requested, creates the folder for) the prefs file spec.
// FSMakeFSSpec legitimately returns fnfErr here on first run -- the file
// just doesn't exist yet -- outSpec is still valid to hand to FSpCreate.
static bool GetPrefsFileSpec(FSSpec& outSpec, bool createFolderIfMissing) {
    short vRefNum;
    long  dirID;
    if (FindFolder(kRsdOnSystemDisk, kRsdPreferencesFolderType, createFolderIfMissing,
                    &vRefNum, &dirID) != noErr) return false;

    Str255 name;
    size_t nameLen = strlen(kPrefsFileName);
    name[0] = static_cast<unsigned char>(nameLen);
    for (size_t i = 0; i < nameLen; ++i) name[i + 1] = static_cast<unsigned char>(kPrefsFileName[i]);

    OSErr err = FSMakeFSSpec(vRefNum, dirID, name, &outSpec);
    return err == noErr || err == fnfErr;
}

void LoadRecentFilesPrefs(std::vector<FSSpec>& outFiles) {
    outFiles.clear();
    FSSpec spec;
    if (!GetPrefsFileSpec(spec, false)) return;

    short refNum;
    if (FSpOpenDF(&spec, fsRdPerm, &refNum) != noErr) return;

    UInt32 magic = 0;
    long cnt = 4;
    bool ok = (FSRead(refNum, &cnt, &magic) == noErr && cnt == 4 && magic == kMagic);
    UInt16 version = 0;
    if (ok) { cnt = 2; ok = (FSRead(refNum, &cnt, &version) == noErr && cnt == 2); }
    UInt16 n = 0;
    if (ok) { cnt = 2; ok = (FSRead(refNum, &cnt, &n) == noErr && cnt == 2); }

    for (UInt16 i = 0; ok && i < n; ++i) {
        UInt8 len = 0;
        cnt = 1;
        if (FSRead(refNum, &cnt, &len) != noErr || cnt != 1) break;
        char buf[256] = {};
        if (len) {
            cnt = len;
            if (FSRead(refNum, &cnt, buf) != noErr || cnt != len) break;
        }
        FSSpec fileSpec;
        if (FullPathToFSSpec(std::string(buf, len), fileSpec))
            outFiles.push_back(fileSpec);
    }
    FSClose(refNum);
}

void SaveRecentFilesPrefs(const std::vector<FSSpec>& files) {
    FSSpec spec;
    if (!GetPrefsFileSpec(spec, true)) return;

    FSpDelete(&spec);  // start clean, same convention WriteDocumentToSpec uses
    if (FSpCreate(&spec, kCreator, kPrefType, smSystemScript) != noErr) return;

    short refNum;
    if (FSpOpenDF(&spec, fsRdWrPerm, &refNum) != noErr) return;

    long cnt;
    cnt = 4; FSWrite(refNum, &cnt, &kMagic);
    UInt16 version = kVersion; cnt = 2; FSWrite(refNum, &cnt, &version);
    UInt16 n = static_cast<UInt16>(files.size()); cnt = 2; FSWrite(refNum, &cnt, &n);

    for (const FSSpec& s : files) {
        std::string path;
        if (!FSSpecToFullPath(s, path)) continue;  // skip defensively rather than corrupt the file
        UInt8 len = static_cast<UInt8>(path.size() > 255 ? 255 : path.size());
        cnt = 1; FSWrite(refNum, &cnt, &len);
        if (len) { cnt = len; FSWrite(refNum, &cnt, path.data()); }
    }
    FSClose(refNum);
}
