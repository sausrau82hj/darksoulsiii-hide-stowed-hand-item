// hidestowed - Dark Souls III (Steam, 64-bit).
// Points the WepAbsorpPosParam sheathed and off-hand model fields at a parking dummy.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr char     kVersion[]     = "1.0.1";
constexpr char     kParamName[]   = "WEP_ABSORP_POS_PARAM_ST";
constexpr uint64_t kLengthOff     = 0x0A;
constexpr uint64_t kNameOffsetOff = 0x10;
constexpr uint64_t kRowInfoOff    = 0x40;
constexpr uint64_t kRowInfoSize   = 0x18;
constexpr uint32_t kMaxRows       = 40000;
constexpr uint32_t kRowSize       = 0x60;
constexpr uint16_t kInvalidPoly   = 0xFFFF;
constexpr int      kParkPolyDefault = 382;

constexpr uint32_t kScanChunk     = 4u << 20;
constexpr uint64_t kResolveWindow = 0x100000;

constexpr int kFieldCount = 14;
const int  kFieldOff[kFieldCount] = {
    0x0A, 0x14, 0x1E, 0x28,
    0x0C, 0x16, 0x20, 0x2A,
    0x0E, 0x18, 0x22,
    0x10, 0x1A, 0x24,
};

enum FieldKind {
    kLeftHang  = 0,
    kRightHang = 1,
    kPairedR   = 2,
    kPairedL   = 3,
};
const uint8_t kFieldKind[kFieldCount] = {
    kLeftHang, kLeftHang, kLeftHang, kLeftHang,
    kRightHang, kRightHang, kRightHang, kRightHang,
    kPairedR, kPairedR, kPairedR,
    kPairedL, kPairedL, kPairedL,
};

const int kPairedIndex[kFieldCount] = {-1, -1, -1, -1, -1, -1, -1, -1,
                                       0, 1, 2, 0, 1, 2};

struct Config {
    bool hideLeftHang      = true;
    bool hideRightHang     = true;
    bool hidePairedOffhand = true;
    int  intervalMs        = 1000;

    int  hidePoly          = kParkPolyDefault;

    bool pairedR[3]        = {true, true, true};
    bool pairedL[3]        = {true, true, true};

    bool scanMapped        = false;
    bool allowRescan       = true;
    bool probe             = false;
    bool probeAll          = false;
    uint64_t worldChrManOffset = 0x4768E78;
    int  probeLimit        = 500;
    int  probeRearmMs      = 100;
};

struct TableState {
    uint64_t addr       = 0;
    uint16_t len        = 0;
    uint64_t nameOffset = 0;
    bool     valid      = false;
    std::vector<uint8_t>  have;
    std::vector<uint16_t> orig;
};

Config       g_cfg;
std::wstring g_iniPath;
bool         g_loggedConfig = false;

bool SafeCopy(void* dst, const void* src, size_t n) {
    __try {
        memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeStore16(void* dst, uint16_t v) {
    __try {
        *(uint16_t*)dst = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Log(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

std::wstring ModuleDir(HMODULE mod) {
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(mod, path, MAX_PATH);
    std::wstring s(path);
    size_t p = s.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? std::wstring(L".") : s.substr(0, p);
}

void LoadConfig() {
    Config c;
    c.hideLeftHang      = GetPrivateProfileIntW(L"general", L"HideLeftHang", 1, g_iniPath.c_str()) != 0;

    const UINT bothPairedRaw = GetPrivateProfileIntW(L"general", L"HideBothPaired", 0xFFFFFF, g_iniPath.c_str());
    c.hidePairedOffhand = (bothPairedRaw != 0xFFFFFF)
                              ? (bothPairedRaw != 0)
                              : (GetPrivateProfileIntW(L"general", L"HidePairedOffhand", 1, g_iniPath.c_str()) != 0);

    c.hideRightHang = GetPrivateProfileIntW(L"general", L"HideRightHang", 1, g_iniPath.c_str()) != 0;

    int sec = (int)GetPrivateProfileIntW(L"general", L"IntervalSeconds", 1, g_iniPath.c_str());
    if (sec < 1) sec = 1;
    if (sec > 600) sec = 600;
    c.intervalMs = sec * 1000;

    int poly = (int)GetPrivateProfileIntW(L"general", L"HidePoly", kParkPolyDefault, g_iniPath.c_str());
    if (poly < -1) poly = -1;
    if (poly > 32000) poly = 32000;
    c.hidePoly = poly;

    wchar_t pf[64] = {0};
    GetPrivateProfileStringW(L"general", L"PairedFields", L"1,2,3", pf, 63, g_iniPath.c_str());
    for (int i = 0; i < 3; ++i) c.pairedR[i] = c.pairedL[i] = false;
    for (const wchar_t* q = pf; *q; ++q) {
        if (*q >= L'1' && *q <= L'3') {
            const int i = (int)(*q - L'1');
            c.pairedR[i] = true;
            c.pairedL[i] = true;
        }
    }
    if (!c.pairedR[0] && !c.pairedR[1] && !c.pairedR[2] && wcsstr(pf, L"none") == nullptr) {
        c.pairedR[0] = true;
        c.pairedL[0] = true;
    }

    c.scanMapped   = GetPrivateProfileIntW(L"general", L"ScanMapped", 0, g_iniPath.c_str()) != 0;
    c.allowRescan  = GetPrivateProfileIntW(L"general", L"Rescan", 1, g_iniPath.c_str()) != 0;
    c.probe        = GetPrivateProfileIntW(L"general", L"Probe", 0, g_iniPath.c_str()) != 0;
    c.probeAll     = GetPrivateProfileIntW(L"general", L"ProbeAll", 0, g_iniPath.c_str()) != 0;
    wchar_t wcm[32] = {0};
    GetPrivateProfileStringW(L"general", L"WorldChrManOffset", L"0x4768E78", wcm, 31, g_iniPath.c_str());
    c.worldChrManOffset = wcstoull(wcm, nullptr, 0);
    int pl         = (int)GetPrivateProfileIntW(L"general", L"ProbeLimit", 500, g_iniPath.c_str());
    if (pl < 1) pl = 1;
    if (pl > 5000) pl = 5000;
    c.probeLimit   = pl;
    int pr         = (int)GetPrivateProfileIntW(L"general", L"ProbeRearmMs", 100, g_iniPath.c_str());
    if (pr < 10) pr = 10;
    if (pr > 2000) pr = 2000;
    c.probeRearmMs = pr;

    const bool changed = (c.hideLeftHang != g_cfg.hideLeftHang) ||
                         (c.hideRightHang != g_cfg.hideRightHang) ||
                         (c.hidePairedOffhand != g_cfg.hidePairedOffhand) ||
                         (c.intervalMs != g_cfg.intervalMs) ||
                         (c.hidePoly != g_cfg.hidePoly) ||
                         (c.pairedR[0] != g_cfg.pairedR[0]) ||
                         (c.pairedR[1] != g_cfg.pairedR[1]) ||
                         (c.pairedR[2] != g_cfg.pairedR[2]) ||
                         (c.pairedL[0] != g_cfg.pairedL[0]) ||
                         (c.pairedL[1] != g_cfg.pairedL[1]) ||
                         (c.pairedL[2] != g_cfg.pairedL[2]) ||
                         (c.probe != g_cfg.probe) ||
                         (c.probeAll != g_cfg.probeAll) ||
                         (c.probeLimit != g_cfg.probeLimit);
    g_cfg = c;

    if (changed || !g_loggedConfig) {
        g_loggedConfig = true;
        Log("config: HideLeftHang=%d HideRightHang=%d HideBothPaired=%d PairedFields=R%d%d%d/L%d%d%d IntervalSeconds=%d HidePoly=%d Probe=%d ScanMapped=%d Rescan=%d",
            (int)g_cfg.hideLeftHang, (int)g_cfg.hideRightHang, (int)g_cfg.hidePairedOffhand,
            (int)g_cfg.pairedR[0], (int)g_cfg.pairedR[1], (int)g_cfg.pairedR[2],
            (int)g_cfg.pairedL[0], (int)g_cfg.pairedL[1], (int)g_cfg.pairedL[2],
            g_cfg.intervalMs / 1000, g_cfg.hidePoly, (int)g_cfg.probe,
            (int)g_cfg.scanMapped, (int)g_cfg.allowRescan);
    }
}

bool ResolveTableFromName(uint64_t nameAddr, uint64_t* outTable) {
    if (nameAddr <= kResolveWindow) return false;
    const uint64_t start  = nameAddr - kResolveWindow;
    const size_t   strIdx = (size_t)(nameAddr - start);
    const size_t   n      = strIdx + 16;
    std::vector<uint8_t> buf(n);
    if (!SafeCopy(buf.data(), (const void*)start, n)) return false;

    for (uint64_t delta = kRowInfoOff; delta + 0x18 <= n && delta <= strIdx; ++delta) {
        const size_t base = strIdx - (size_t)delta;
        uint64_t nameOffset = 0;
        memcpy(&nameOffset, buf.data() + base + kNameOffsetOff, 8);
        if (nameOffset != delta) continue;
        uint16_t len = 0;
        memcpy(&len, buf.data() + base + kLengthOff, 2);
        if (len == 0 || len > kMaxRows) continue;
        *outTable = start + base;
        return true;
    }
    return false;
}

bool ValidateTable(TableState& st, char* nameOut, size_t nameOutSize) {
    uint16_t len = 0;
    if (!SafeCopy(&len, (const void*)(st.addr + kLengthOff), 2)) return false;
    if (len == 0 || len > kMaxRows) return false;
    uint64_t nameOffset = 0;
    if (!SafeCopy(&nameOffset, (const void*)(st.addr + kNameOffsetOff), 8)) return false;
    if (nameOffset < kRowInfoOff || nameOffset > kResolveWindow) return false;
    char name[64] = {0};
    if (!SafeCopy(name, (const void*)(st.addr + nameOffset), sizeof(name) - 1)) return false;
    if (strcmp(name, kParamName) != 0) return false;
    st.len = len;
    st.nameOffset = nameOffset;
    st.valid = true;
    if (nameOut) {
        strncpy_s(nameOut, nameOutSize, name, _TRUNCATE);
    }
    return true;
}

int ApplyTable(TableState& st, int* writesOut) {
    if (!ValidateTable(st, nullptr, 0)) return -1;

    if (st.have.size() != st.len) {
        st.have.assign(st.len, 0);
        st.orig.assign((size_t)st.len * kFieldCount, 0);
    }

    const uint64_t dataStart = st.addr + kRowInfoOff + (uint64_t)st.len * kRowInfoSize;
    const uint64_t dataEnd   = st.addr + st.nameOffset;
    (void)dataStart;

    int rows = 0, writes = 0;
    for (uint16_t i = 0; i < st.len; ++i) {
        const uint64_t info = st.addr + kRowInfoOff + (uint64_t)i * kRowInfoSize;
        uint64_t rowId = 0, rowOff = 0;
        if (!SafeCopy(&rowId, (const void*)info, 8)) continue;
        if (!SafeCopy(&rowOff, (const void*)(info + 8), 8)) continue;
        const uint64_t row = st.addr + rowOff;
        if (rowOff < kRowInfoOff + (uint64_t)st.len * kRowInfoSize) continue;
        if (row + kRowSize > dataEnd) continue;

        uint8_t* p = (uint8_t*)row;
        if (!st.have[i]) {
            for (int f = 0; f < kFieldCount; ++f) {
                uint16_t v = 0;
                if (!SafeCopy(&v, p + kFieldOff[f], 2)) { v = 0; }
                st.orig[(size_t)i * kFieldCount + f] = v;
            }
            st.have[i] = 1;
        }
        for (int f = 0; f < kFieldCount; ++f) {

            bool enabled = false;
            switch (kFieldKind[f]) {
                case kLeftHang:  enabled = g_cfg.hideLeftHang;  break;
                case kRightHang: enabled = g_cfg.hideRightHang; break;
                case kPairedR:   enabled = g_cfg.hidePairedOffhand && g_cfg.pairedR[kPairedIndex[f]]; break;
                case kPairedL:   enabled = g_cfg.hidePairedOffhand && g_cfg.pairedL[kPairedIndex[f]]; break;
                default:         break;
            }
            const uint16_t park = (g_cfg.hidePoly < 0)
                                      ? kInvalidPoly
                                      : (uint16_t)(int16_t)g_cfg.hidePoly;
            uint16_t cur = 0;
            if (!SafeCopy(&cur, p + kFieldOff[f], 2)) continue;
            uint16_t& orig = st.orig[(size_t)i * kFieldCount + f];
            if (cur != park && cur != orig) orig = cur;
            const uint16_t target = enabled ? park : orig;
            if (cur == target) continue;
            if (!SafeStore16(p + kFieldOff[f], target)) continue;
            ++writes;
        }
        ++rows;
    }
    if (writesOut) *writesOut += writes;
    return rows;
}

uint64_t g_scanReadBytes  = 0;
uint64_t g_scanSkipBytes  = 0;
uint64_t g_scanTotalBytes = 0;

std::vector<PSAPI_WORKING_SET_EX_INFORMATION> g_wsQuery;

bool QueryResidency(uint64_t base, size_t len, std::vector<uint8_t>& resident) {
    const size_t pages = (len + 0xFFF) / 0x1000;
    if (!pages) return false;
    g_wsQuery.resize(pages);
    for (size_t i = 0; i < pages; ++i)
        g_wsQuery[i].VirtualAddress = (PVOID)(base + i * 0x1000);
    if (!QueryWorkingSetEx(GetCurrentProcess(), g_wsQuery.data(),
                           (DWORD)(pages * sizeof(g_wsQuery[0]))))
        return false;
    resident.assign(pages, 0);
    for (size_t i = 0; i < pages; ++i)
        resident[i] = g_wsQuery[i].VirtualAttributes.Valid ? 1 : 0;
    return true;
}

void SearchRun(const uint8_t* p, size_t n, uint64_t addrBase, std::vector<uint64_t>& found) {
    const size_t nameLen = sizeof(kParamName) - 1;
    size_t i = 0;
    while (i + nameLen <= n) {
        const void* hit = memchr(p + i, kParamName[0], n - i - (nameLen - 1));
        if (!hit) break;
        const size_t idx = (size_t)((const uint8_t*)hit - p);
        if (memcmp(p + idx, kParamName, nameLen) == 0) {
            uint64_t table = 0;
            if (ResolveTableFromName(addrBase + idx, &table)) {
                bool dup = false;
                for (uint64_t t : found) if (t == table) { dup = true; break; }
                if (!dup) found.push_back(table);
            }
        }
        i = idx + 1;
    }
}

std::vector<uint64_t> ScanForTables(bool includeMapped) {
    std::vector<uint64_t> found;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const size_t nameLen = sizeof(kParamName) - 1;
    std::vector<uint8_t> buf(kScanChunk + nameLen);
    std::vector<uint8_t> resident;

    uint8_t* addr = (uint8_t*)si.lpMinimumApplicationAddress;
    const uint8_t* maxAddr = (const uint8_t*)si.lpMaximumApplicationAddress;

    while (addr < maxAddr) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(addr, &mbi, sizeof(mbi))) break;
        uint8_t* base = (uint8_t*)mbi.BaseAddress;
        const size_t size = mbi.RegionSize;

        const bool usable = mbi.State == MEM_COMMIT &&
                            (mbi.Type == MEM_PRIVATE || (includeMapped && mbi.Type == MEM_MAPPED)) &&
                            !(mbi.Protect & PAGE_GUARD) && !(mbi.Protect & PAGE_NOACCESS);
        if (usable && size) {
            for (size_t off = 0; off < size; off += kScanChunk) {
                size_t n = size - off;
                if (n > kScanChunk + nameLen) n = kScanChunk + nameLen;
                g_scanTotalBytes += n;

                if (!QueryResidency((uint64_t)(base + off), n, resident)) {
                    g_scanSkipBytes += n;
                    continue;
                }

                size_t readInChunk = 0;
                size_t pg = 0;
                while (pg < resident.size()) {
                    if (!resident[pg]) { ++pg; continue; }
                    size_t end = pg;
                    while (end < resident.size() && resident[end]) ++end;
                    size_t runStart = pg * 0x1000;
                    size_t runLen   = end * 0x1000;
                    if (runLen > n) runLen = n;
                    if (runLen > runStart) {
                        runLen -= runStart;
                        if (SafeCopy(buf.data(), base + off + runStart, runLen)) {
                            readInChunk += runLen;
                            SearchRun(buf.data(), runLen,
                                      (uint64_t)(base + off + runStart), found);
                        }
                    }
                    pg = end;
                }
                g_scanReadBytes += readInChunk;
                g_scanSkipBytes += n - readInChunk;
            }
        }
        if (!size) break;
        addr = base + size;
    }
    return found;
}

void LogScan(const char* what, size_t tables) {
    PROCESS_MEMORY_COUNTERS pmc = {0};
    unsigned long long wsMB = 0, peakMB = 0;
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        wsMB   = (unsigned long long)(pmc.WorkingSetSize >> 20);
        peakMB = (unsigned long long)(pmc.PeakWorkingSetSize >> 20);
    }
    Log("%s: %zu table(s); read %llu MB resident, left %llu MB untouched of %llu MB seen; working set %llu MB (peak %llu MB)",
        what, tables,
        (unsigned long long)(g_scanReadBytes >> 20),
        (unsigned long long)(g_scanSkipBytes >> 20),
        (unsigned long long)(g_scanTotalBytes >> 20),
        wsMB, peakMB);
    g_scanReadBytes = g_scanSkipBytes = g_scanTotalBytes = 0;
}

struct ProbeRange { uint64_t begin, end, table; };
struct ProbeRow   { uint64_t addr; uint64_t id; uint64_t table; };

std::vector<ProbeRange> g_probeRanges;
std::vector<ProbeRow>   g_probeRows;
std::vector<uint64_t>   g_probePages;
std::vector<DWORD>      g_probeProt;
volatile LONG           g_probeEvents = 0;
volatile LONG           g_probeHits = 0;
volatile LONG           g_probeStackLogged = 0;
uint64_t                g_probeStepPageAddr = 0;
uint64_t                g_exeBase = 0;
PVOID                   g_probeVeh = nullptr;

struct ProbeSeen { uint64_t rip; int field; };
ProbeSeen g_probeSeen[128];
int       g_probeSeenCount = 0;
volatile LONG g_probeOther = 0;

bool ProbeFieldOfInterest(int off) {

    return off >= 0 && off <= 0x30;
}

void ProbeScanPlayerModel() {
    const unsigned char needle[4] = { 0x7E, 0x01, 0x7F, 0x01 };
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uint64_t a = (uint64_t)si.lpMinimumApplicationAddress;
    const uint64_t end = (uint64_t)si.lpMaximumApplicationAddress;
    uint64_t hits[4] = {0};
    int hitCount = 0;
    std::vector<unsigned char> buf(0x10000);
    MEMORY_BASIC_INFORMATION mbi;
    while (a < end && hitCount < 4) {
        if (!VirtualQuery((LPCVOID)a, &mbi, sizeof(mbi))) break;
        const uint64_t regionBase = (uint64_t)mbi.BaseAddress;
        const uint64_t regionSize = mbi.RegionSize;
        const bool readable = mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
            !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE));
        if (readable && regionSize <= 0x40000000) {
            for (uint64_t off = 0; off < regionSize && hitCount < 4; off += buf.size()) {
                const size_t want = (size_t)((regionSize - off) < buf.size() ? (regionSize - off) : buf.size());
                if (!SafeCopy(buf.data(), (const void*)(regionBase + off), want)) continue;
                for (size_t i = 0; i + 4 <= want; ++i) {
                    if (buf[i] == needle[0] && buf[i + 1] == needle[1] &&
                        buf[i + 2] == needle[2] && buf[i + 3] == needle[3]) {
                        hits[hitCount++] = regionBase + off + i;
                        if (hitCount >= 4) break;
                    }
                }
            }
        }
        a = regionBase + regionSize;
    }
    Log("model scan: %d hit(s) for the 382/383 dummy pair", hitCount);
    for (int h = 0; h < hitCount; ++h) {
        const uint64_t hit = hits[h];
        int16_t around[16] = {0};
        if (SafeCopy(around, (const void*)(hit - 16), sizeof(around))) {
            char line[512] = {0};
            int n = 0;
            for (int i = 0; i < 16; ++i)
                n += _snprintf_s(line + n, sizeof(line) - n, _TRUNCATE, "%d ", around[i]);
            Log("  hit %d at 0x%llX, int16 window: %s", h, (unsigned long long)hit, line);
        }

        int refs = 0;
        a = (uint64_t)si.lpMinimumApplicationAddress;
        while (a < end && refs < 6) {
            if (!VirtualQuery((LPCVOID)a, &mbi, sizeof(mbi))) break;
            const uint64_t regionBase = (uint64_t)mbi.BaseAddress;
            const uint64_t regionSize = mbi.RegionSize;
            const bool readable = mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
                !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE));
            if (readable && regionSize <= 0x40000000) {
                for (uint64_t off = 0; off < regionSize && refs < 6; off += buf.size()) {
                    const size_t want = (size_t)((regionSize - off) < buf.size() ? (regionSize - off) : buf.size());
                    if (!SafeCopy(buf.data(), (const void*)(regionBase + off), want)) continue;
                    for (size_t i = 0; i + 8 <= want; i += 8) {
                        uint64_t v = 0;
                        memcpy(&v, buf.data() + i, 8);
                        if (v >= (hit & ~(uint64_t)0xFFF) && v < (hit & ~(uint64_t)0xFFF) + 0x1000) {
                            ++refs;
                            Log("    ref to model page: [0x%llX] = 0x%llX", (unsigned long long)(regionBase + off + i), (unsigned long long)v);
                            uint64_t ctx[16] = {0};
                            if (SafeCopy(ctx, (const void*)((regionBase + off + i) & ~(uint64_t)0x3F), sizeof(ctx)))
                                for (int k = 0; k < 16; k += 4)
                                    Log("      ctx: %llX %llX %llX %llX", (unsigned long long)ctx[k],
                                        (unsigned long long)ctx[k + 1], (unsigned long long)ctx[k + 2],
                                        (unsigned long long)ctx[k + 3]);
                            if (refs >= 6) break;
                        }
                    }
                }
            }
            a = regionBase + regionSize;
        }
    }
}

void ProbeDumpWorldChrMan() {    if (!g_cfg.worldChrManOffset) return;
    uint64_t wcm = 0;
    if (!SafeCopy(&wcm, (const void*)(g_exeBase + g_cfg.worldChrManOffset), 8) || wcm < 0x10000) return;
    uint64_t q[64] = {0};
    if (!SafeCopy(q, (const void*)wcm, sizeof(q))) return;
    Log("wcm graph base=0x%llX", (unsigned long long)wcm);
    for (int i = 0; i < 64; ++i) {
        const uint64_t v = q[i];
        if (v > 0x10000 && v < 0x00007FFFFFFFFFFFull && (v & 7) == 0) {
            uint64_t sub[4] = {0};
            if (SafeCopy(sub, (const void*)v, sizeof(sub)))
                Log("  wcm+%03X -> 0x%llX : %llX %llX %llX %llX", i * 8, (unsigned long long)v,
                    (unsigned long long)sub[0], (unsigned long long)sub[1],
                    (unsigned long long)sub[2], (unsigned long long)sub[3]);
        } else {
            Log("  wcm+%03X = 0x%llX", i * 8, (unsigned long long)v);
        }
    }

    const uint64_t cand[] = { 0x1F90, 0x1E508, 0x1E510, 0x10A0, 0x2C0, 0x14 };
    for (int ci = 0; ci < 6; ++ci) {
        uint64_t v = 0;
        if (!SafeCopy(&v, (const void*)(wcm + cand[ci]), 8) || v < 0x10000) continue;
        Log("  wcm+%llX = 0x%llX", (unsigned long long)cand[ci], (unsigned long long)v);
        uint64_t q[8] = {0};
        if (SafeCopy(q, (const void*)v, sizeof(q)))
            Log("     -> %llX %llX %llX %llX %llX %llX %llX %llX",
                (unsigned long long)q[0], (unsigned long long)q[1], (unsigned long long)q[2],
                (unsigned long long)q[3], (unsigned long long)q[4], (unsigned long long)q[5],
                (unsigned long long)q[6], (unsigned long long)q[7]);
    }
}

LONG CALLBACK ProbeHandler(PEXCEPTION_POINTERS ep) {
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {

        if (g_probeStepPageAddr) {
            for (size_t i = 0; i < g_probePages.size(); ++i)
                if (g_probePages[i] == g_probeStepPageAddr) {
                    DWORD old = 0;
                    VirtualProtect((LPVOID)g_probePages[i], 0x1000, g_probeProt[i] | PAGE_GUARD, &old);
                    break;
                }
            g_probeStepPageAddr = 0;
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_GUARD_PAGE) return EXCEPTION_CONTINUE_SEARCH;
    const uint64_t fault = (uint64_t)ep->ExceptionRecord->ExceptionInformation[1];
    const uint64_t page = fault & ~(uint64_t)0xFFF;
    bool ours = false;
    for (size_t i = 0; i < g_probePages.size(); ++i) if (g_probePages[i] == page) { ours = true; break; }
    if (!ours) return EXCEPTION_CONTINUE_SEARCH;
    const LONG seq = InterlockedIncrement(&g_probeEvents);

    uint64_t rowId = 0;
    int off = -1;
    for (size_t i = 0; i < g_probeRows.size(); ++i) {
        const ProbeRow& r = g_probeRows[i];
        if (fault >= r.addr && fault < r.addr + kRowSize) {
            rowId = r.id;
            off = (int)(fault - r.addr);
            break;
        }
    }

    const CONTEXT* c = ep->ContextRecord;

    if (off == 0x0A && (uint64_t)(c->Rip - g_exeBase) == 0xE316AC &&
        InterlockedIncrement(&g_probeStackLogged) <= 3) {
        Log("STACK selector rsp=0x%llX rbx=%llX rbp=%llX rdi=%llX rsi=%llX r10=0x%llX r12=%llX r13=%llX r14=%llX r15=%llX",
            (unsigned long long)c->Rsp, (unsigned long long)c->Rbx, (unsigned long long)c->Rbp,
            (unsigned long long)c->Rdi, (unsigned long long)c->Rsi, (unsigned long long)c->R10,
            (unsigned long long)c->R12, (unsigned long long)c->R13,
            (unsigned long long)c->R14, (unsigned long long)c->R15);
        uint64_t st[32] = {0};
        if (SafeCopy(st, (const void*)c->Rsp, sizeof(st)))
            for (int i = 0; i < 32; i += 4)
                Log("  stack+%02X: 0x%llX 0x%llX 0x%llX 0x%llX", i * 8,
                    (unsigned long long)st[i], (unsigned long long)st[i + 1],
                    (unsigned long long)st[i + 2], (unsigned long long)st[i + 3]);
        ProbeDumpWorldChrMan();
    }
    if (ProbeFieldOfInterest(off) && seq <= (LONG)g_cfg.probeLimit) {
        uint32_t val32 = 0;
        SafeCopy(&val32, (const void*)fault, 4);
        uint64_t ret0 = 0, ret1 = 0, ret2 = 0;
        SafeCopy(&ret0, (const void*)c->Rsp, 8);
        SafeCopy(&ret1, (const void*)(c->Rsp + 8), 8);
        SafeCopy(&ret2, (const void*)(c->Rsp + 16), 8);
        Log("SEQ %ld row=0x%llX off=+0x%X val32=0x%08X RIP=0x%llX (exe+0x%llX) rcx=0x%llX rdx=0x%llX r8=0x%llX r9=0x%llX ret=0x%llX,0x%llX,0x%llX",
            (long)seq, (unsigned long long)rowId, off < 0 ? 0 : off, val32,
            (unsigned long long)c->Rip, (unsigned long long)(g_exeBase ? c->Rip - g_exeBase : 0),
            (unsigned long long)c->Rcx, (unsigned long long)c->Rdx,
            (unsigned long long)c->R8, (unsigned long long)c->R9,
            (unsigned long long)ret0, (unsigned long long)ret1, (unsigned long long)ret2);
    } else if (g_cfg.probeAll && InterlockedIncrement(&g_probeOther) <= (LONG)g_cfg.probeLimit) {
        Log("PROBE-ALL fault=0x%llX row=0x%llX off=0x%X RIP=0x%llX (exe+0x%llX) rcx=0x%llX rdx=0x%llX",
            (unsigned long long)fault, (unsigned long long)rowId, off < 0 ? 0 : off,
            (unsigned long long)c->Rip, (unsigned long long)(g_exeBase ? c->Rip - g_exeBase : 0),
            (unsigned long long)c->Rcx, (unsigned long long)c->Rdx);
    }

    if (g_probeStepPageAddr == 0 && seq <= (LONG)g_cfg.probeLimit * 4) {
        g_probeStepPageAddr = page;
        ep->ContextRecord->EFlags |= 0x100;
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

void ArmProbePages() {
    for (size_t i = 0; i < g_probePages.size(); ++i) {
        DWORD old = 0;
        VirtualProtect((LPVOID)g_probePages[i], 0x1000, g_probeProt[i] | PAGE_GUARD, &old);
    }
}

int PrepareProbe(std::vector<TableState>& tables) {
    for (TableState& st : tables) {
        if (!ValidateTable(st, nullptr, 0)) continue;
        const uint64_t dataStart = st.addr + kRowInfoOff + (uint64_t)st.len * kRowInfoSize;
        const uint64_t dataEnd   = st.addr + st.nameOffset;
        if (dataEnd <= dataStart) continue;
        ProbeRange r = { dataStart, dataEnd, st.addr };
        g_probeRanges.push_back(r);
        for (uint16_t i = 0; i < st.len; ++i) {
            uint64_t id = 0, rowOff = 0;
            const uint64_t info = st.addr + kRowInfoOff + (uint64_t)i * kRowInfoSize;
            if (!SafeCopy(&id, (const void*)info, 8)) continue;
            if (!SafeCopy(&rowOff, (const void*)(info + 8), 8)) continue;
            const uint64_t row = st.addr + rowOff;
            if (row < dataStart || row + kRowSize > dataEnd) continue;
            ProbeRow pr = { row, id, st.addr };
            g_probeRows.push_back(pr);
        }
        for (uint64_t a = dataStart & ~(uint64_t)0xFFF; a < dataEnd; a += 0x1000) {
            bool dup = false;
            for (size_t k = 0; k < g_probePages.size(); ++k) if (g_probePages[k] == a) { dup = true; break; }
            if (!dup) g_probePages.push_back(a);
        }
    }
    for (size_t i = 0; i < g_probePages.size(); ++i) {
        MEMORY_BASIC_INFORMATION mbi;
        DWORD prot = PAGE_READWRITE;
        if (VirtualQuery((LPCVOID)g_probePages[i], &mbi, sizeof(mbi)))
            prot = mbi.Protect & ~(DWORD)PAGE_GUARD;
        g_probeProt.push_back(prot);
    }
    if (!g_probeVeh) g_probeVeh = AddVectoredExceptionHandler(1, ProbeHandler);
    ArmProbePages();
    Log("probe armed: %zu table range(s), %zu row(s), %zu page(s), ProbeAll=%d",
        g_probeRanges.size(), g_probeRows.size(), g_probePages.size(), (int)g_cfg.probeAll);
    return (int)g_probePages.size();
}

DWORD WINAPI Worker(LPVOID) {
    std::vector<TableState> tables;

    for (int attempt = 0; attempt < 600; ++attempt) {
        std::vector<uint64_t> found = ScanForTables(g_cfg.scanMapped);

        if (!found.empty() || (attempt % 10) == 0) LogScan("scan", found.size());
        if (!found.empty()) {
            tables.clear();
            for (uint64_t t : found) {
                TableState st;
                st.addr = t;
                tables.push_back(st);
            }
            break;
        }
        Sleep(1000);
    }
    Log("param tables located: %zu", tables.size());
    if (tables.empty()) Log("WARNING: %s not found (params not loaded?)", kParamName);

    if (g_cfg.probe) {
        const int pages = PrepareProbe(tables);
        Log("probe mode: writing NOTHING; re-arming every %d ms", g_cfg.probeRearmMs);
        int wcmDumps = 0;
        for (int tick = 0;; ++tick) {
            Sleep((DWORD)g_cfg.probeRearmMs);
            if (pages > 0 && g_probeStepPageAddr == 0) ArmProbePages();

            if (tick == 30 || tick == 60 || tick == 100 || tick == 150 || tick == 200 ||
                tick == 300 || tick == 400 || tick == 600 || tick == 1200) {
                std::vector<uint64_t> found = ScanForTables(g_cfg.scanMapped);
                LogScan("probe rescan", found.size());
                std::vector<TableState> extra;
                for (uint64_t t : found) {
                    bool have = false;
                    for (TableState& st : tables) if (st.addr == t) { have = true; break; }
                    if (!have) { TableState st; st.addr = t; extra.push_back(st); }
                }
                Log("probe: rescan found %zu table(s), %zu new", found.size(), extra.size());
                if (!extra.empty()) { PrepareProbe(extra); for (TableState& st : extra) tables.push_back(st); }
            }

            if (tick == 200 || tick == 600 || tick == 1500) ProbeScanPlayerModel();

            if (g_cfg.worldChrManOffset && wcmDumps < 3 && (tick % 50) == 49) {
                ProbeDumpWorldChrMan();
                ++wcmDumps;
            }
        }
    }

    for (;;) {
        LoadConfig();
        int live = 0, rowsTotal = 0, writes = 0;
        for (TableState& st : tables) {
            const int n = ApplyTable(st, &writes);
            if (n > 0) { ++live; rowsTotal += n; }
            else { st.valid = false; }
        }
        if (writes > 0) {
            Log("applied %d field write(s) over %d row(s) in %d table(s)",
                writes, rowsTotal, live);
        }
        if (!tables.empty() && live == 0 && g_cfg.allowRescan) {
            Log("tables went stale, rescanning");
            std::vector<uint64_t> found = ScanForTables(g_cfg.scanMapped);
            LogScan("rescan", found.size());
            tables.clear();
            for (uint64_t t : found) {
                TableState st;
                st.addr = t;
                tables.push_back(st);
            }
            Log("rescan found %zu", tables.size());
        }
        Sleep((DWORD)g_cfg.intervalMs);
    }
    return 0;
}

}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(mod);
        g_exeBase = (uint64_t)GetModuleHandleW(nullptr);
        const std::wstring dir = ModuleDir(mod);
        g_iniPath = dir + L"\\hidestowed.ini";
        LoadConfig();
        Log("--- hidestowed %s loaded (pid %lu) ---", kVersion, GetCurrentProcessId());
        if (HANDLE th = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr)) {
            CloseHandle(th);
        }
    }
    return TRUE;
}
