// xinput1_3.dll - loader shell for hidestowed.
// Forwards every system XInput export, then loads hidestowed.dll.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

HMODULE      g_real = nullptr;
std::wstring g_dir;
std::wstring g_ini;

void LogLine(const wchar_t* fmt, ...) {
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, 1024, _TRUNCATE, fmt, ap);
    va_end(ap);
    OutputDebugStringW(buf);
    OutputDebugStringW(L"\n");
}

std::wstring SelfDir(HMODULE mod) {
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(mod, path, MAX_PATH);
    std::wstring s(path);
    const size_t p = s.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? std::wstring(L".") : s.substr(0, p);
}

void LoadReal() {
    if (g_real) return;
    wchar_t sys[MAX_PATH] = {0};
    GetSystemDirectoryW(sys, MAX_PATH);
    const std::wstring real = std::wstring(sys) + L"\\xinput1_3.dll";
    g_real = LoadLibraryW(real.c_str());
}

FARPROC Real(const char* name) {
    if (!g_real) LoadReal();
    return g_real ? GetProcAddress(g_real, name) : nullptr;
}

FARPROC RealOrdinal(int ordinal) {
    if (!g_real) LoadReal();
    return g_real ? GetProcAddress(g_real, MAKEINTRESOURCEA(ordinal)) : nullptr;
}

std::wstring Trim(const std::wstring& s) {
    const size_t b = s.find_first_not_of(L" \t\r\n");
    if (b == std::wstring::npos) return L"";
    const size_t e = s.find_last_not_of(L" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::vector<std::wstring> SplitList(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstring cur;
    for (wchar_t c : s) {
        if (c == L',' || c == L';' || c == L'\n' || c == L'\r') {
            const std::wstring t = Trim(cur);
            if (!t.empty()) out.push_back(t);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    const std::wstring t = Trim(cur);
    if (!t.empty()) out.push_back(t);
    return out;
}

std::wstring ResolvePath(const std::wstring& entry) {
    if (entry.find(L':') != std::wstring::npos || entry.rfind(L"\\\\", 0) == 0)
        return entry;
    return g_dir + L"\\" + entry;
}

void LoadOne(const std::wstring& entry) {
    const std::wstring full = ResolvePath(entry);
    if (GetFileAttributesW(full.c_str()) == INVALID_FILE_ATTRIBUTES) {
        LogLine(L"  %s -- not present, skipped", entry.c_str());
        return;
    }
    HMODULE h = LoadLibraryW(full.c_str());
    LogLine(L"  %s -- %s (handle=0x%p)", entry.c_str(),
            h ? L"loaded" : L"FAILED", (void*)h);
}

void LoadExtraMods() {
    wchar_t buf[2048] = {0};
    GetPrivateProfileStringW(L"loader", L"ExtraMods", L"dkp_loader.dll",
                             buf, 2048, g_ini.c_str());
    const std::vector<std::wstring> entries = SplitList(buf);
    if (entries.empty()) {
        LogLine(L"extra mods: none configured");
        return;
    }
    LogLine(L"extra mods: %zu entr%s", entries.size(),
            entries.size() == 1 ? L"y" : L"ies");
    for (const std::wstring& e : entries) LoadOne(e);
}

DWORD WINAPI DelayedLoadWithSleep(LPVOID param) {
    Sleep((DWORD)(uintptr_t)param);
    LoadExtraMods();
    return 0;
}

void Boot(HMODULE mod) {
    const int loadCore = GetPrivateProfileIntW(L"loader", L"LoadCore", 1, g_ini.c_str());
    if (loadCore) {
        const std::wstring core = g_dir + L"\\hidestowed.dll";
        if (GetFileAttributesW(core.c_str()) == INVALID_FILE_ATTRIBUTES) {
            LogLine(L"core hidestowed.dll -- not present, skipped");
        } else {
            HMODULE h = LoadLibraryW(core.c_str());
            LogLine(L"core hidestowed.dll -- %s (handle=0x%p)",
                    h ? L"loaded" : L"FAILED", (void*)h);
        }
    } else {
        LogLine(L"core loading disabled by ini (LoadCore=0)");
    }

    const int delay = (int)GetPrivateProfileIntW(L"loader", L"ExtraModsDelayMs", 0, g_ini.c_str());
    if (delay > 0) {
        LogLine(L"extra mods will load in %d ms (background thread)", delay);
        if (HANDLE th = CreateThread(nullptr, 0, DelayedLoadWithSleep,
                                     (LPVOID)(uintptr_t)delay, 0, nullptr)) {
            CloseHandle(th);
        }
    } else {
        LoadExtraMods();
    }
}

typedef DWORD(WINAPI* Fn1)(DWORD);
typedef DWORD(WINAPI* Fn2)(DWORD, void*);
typedef DWORD(WINAPI* Fn3)(DWORD, void*, void*);
typedef DWORD(WINAPI* Fn3d)(DWORD, DWORD, void*);
typedef void (WINAPI* FnVoid1)(BOOL);

}

extern "C" BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(mod);
        g_dir = SelfDir(mod);
        g_ini = g_dir + L"\\hidestowed.ini";
        LoadReal();
        LogLine(L"--- mod loader shell attached (pid %lu) ---", GetCurrentProcessId());
        Boot(mod);
    }
    return TRUE;
}

extern "C" BOOL WINAPI ProxyDllMainEntry(HMODULE, DWORD, LPVOID) { return TRUE; }

extern "C" DWORD WINAPI XInputGetState(DWORD index, void* state) {
    const FARPROC p = Real("XInputGetState");
    return p ? ((Fn2)p)(index, state) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI XInputSetState(DWORD index, void* vibration) {
    const FARPROC p = Real("XInputSetState");
    return p ? ((Fn2)p)(index, vibration) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI XInputGetCapabilities(DWORD index, DWORD flags, void* caps) {
    const FARPROC p = Real("XInputGetCapabilities");
    return p ? ((Fn3d)p)(index, flags, caps) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" void WINAPI XInputEnable(BOOL enable) {
    const FARPROC p = Real("XInputEnable");
    if (p) ((FnVoid1)p)(enable);
}

extern "C" DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD index, void* render, void* capture) {
    const FARPROC p = Real("XInputGetDSoundAudioDeviceGuids");
    return p ? ((Fn3)p)(index, render, capture) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI XInputGetBatteryInformation(DWORD index, BYTE devType, void* info) {
    const FARPROC p = Real("XInputGetBatteryInformation");
    return p ? ((Fn3d)p)(index, (DWORD)devType, info) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI XInputGetKeystroke(DWORD index, DWORD reserved, void* keystroke) {
    const FARPROC p = Real("XInputGetKeystroke");
    return p ? ((Fn3d)p)(index, reserved, keystroke) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI XInputGetStateEx(DWORD index, void* state) {
    const FARPROC p = RealOrdinal(100);
    return p ? ((Fn2)p)(index, state) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI XInputWaitForGuideButton(DWORD index, DWORD flag, void* unused) {
    const FARPROC p = RealOrdinal(101);
    return p ? ((Fn3d)p)(index, flag, unused) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI XInputCancelGuideButtonWait(DWORD index) {
    const FARPROC p = RealOrdinal(102);
    return p ? ((Fn1)p)(index) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI XInputPowerOffController(DWORD index) {
    const FARPROC p = RealOrdinal(103);
    return p ? ((Fn1)p)(index) : ERROR_DEVICE_NOT_CONNECTED;
}
