// Crash reporting - see crash.h.
//
// How a C++ exception becomes a report: the link wraps __cxa_throw (-Wl,--wrap=__cxa_throw), so
// every throw records its call stack, thread and activity in a small ring before unwinding
// starts. If nothing catches it, std::terminate runs our handler, which writes the report from
// that record; recovery boundaries (window procedures, render worker, tasks) call
// CrashRecovered() from their catch blocks and get the same report. Hardware faults (access
// violations and the like) reach the unhandled-exception filter, which walks the faulting stack.
// Function names come from the exe's own COFF symbol table (the release build keeps symbols and
// drops only debug info), so a report is readable without any extra files.
#include "crash.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cstdlib>
#include <cxxabi.h>
#include <exception>
#include <typeinfo>

namespace {

// ---- per-thread and recent-activity state ---------------------------------------------------
thread_local wchar_t t_name[40];
thread_local wchar_t t_ctx[220];

struct ThrowRec {
    const void* obj;
    const std::type_info* type;
    DWORD tid;
    USHORT n;
    void* frames[48];
    wchar_t thread[40];
    wchar_t ctx[220];
};
ThrowRec s_throws[8];
volatile LONG s_throwSeq = 0;

struct TrailRec { DWORD ms; wchar_t text[120]; };
TrailRec s_trail[16];
volatile LONG s_trailSeq = 0;

wchar_t s_mode[16] = L"panel";
bool s_interactive = true;
wchar_t s_log[MAX_PATH + 64];
DWORD s_start = 0;
volatile LONG s_fatal = 0, s_recovered = 0;
std::wstring (*s_describe)() = nullptr;
LPTOP_LEVEL_EXCEPTION_FILTER s_prevFilter = nullptr;
CRITICAL_SECTION s_reportCs;

// ---- report text (static buffer: after a fault the heap may be unusable) --------------------
wchar_t s_buf[24576];
size_t s_len = 0;
void out(const wchar_t* fmt, ...) {
    const size_t cap = sizeof(s_buf) / sizeof(s_buf[0]);
    if (s_len + 2 >= cap) return;
    va_list a;
    va_start(a, fmt);
    int n = vswprintf(s_buf + s_len, cap - s_len, fmt, a);
    va_end(a);
    if (n > 0) s_len += (size_t)n;
    else s_buf[s_len] = 0;
}
void widen(const char* s, wchar_t* d, int cap) {
    d[0] = 0;
    if (!s) return;
    if (MultiByteToWideChar(CP_UTF8, 0, s, -1, d, cap) <= 0) {
        int i = 0;   // too long for the buffer: copy what fits (symbols are ASCII)
        for (; s[i] && i < cap - 1; i++) d[i] = (unsigned char)s[i];
        d[i] = 0;
    }
}

// ---- symbols: the exe's own COFF symbol table -----------------------------------------------
struct FnSym { DWORD rva; const char* name; };
FnSym* s_syms = nullptr;
DWORD s_nsyms = 0;
bool s_symTried = false;

void loadSymbols() {
    if (s_symTried) return;
    s_symTried = true;
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    auto* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    DWORD symOff = nt->FileHeader.PointerToSymbolTable, nsym = nt->FileHeader.NumberOfSymbols;
    if (!symOff || !nsym) return;
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    int nsec = nt->FileHeader.NumberOfSections;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    HANDLE f = CreateFileW(exe, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    if (sz.QuadPart <= symOff || sz.QuadPart - symOff > 64 * 1024 * 1024) { CloseHandle(f); return; }
    DWORD len = (DWORD)(sz.QuadPart - symOff), rd = 0;
    // symbol + string table, plus room for NUL-terminated copies of the 8-byte short names
    size_t shortCap = (size_t)nsym * 9;
    BYTE* t = (BYTE*)VirtualAlloc(nullptr, len + shortCap + 16, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    LARGE_INTEGER pos;
    pos.QuadPart = symOff;
    bool ok = t && SetFilePointerEx(f, pos, nullptr, FILE_BEGIN) && ReadFile(f, t, len, &rd, nullptr) && rd == len;
    CloseHandle(f);
    if (!ok || (DWORD64)nsym * 18 + 4 > len) return;
    const char* strtab = (const char*)t + (size_t)nsym * 18;
    DWORD strLen = len - nsym * 18;
    char* shortNames = (char*)t + len + 8;
    s_syms = (FnSym*)VirtualAlloc(nullptr, sizeof(FnSym) * nsym, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!s_syms) return;
    for (DWORD i = 0; i < nsym; i++) {
        const BYTE* s = t + (size_t)i * 18;
        DWORD value, zero, longOff;
        SHORT secNo;
        memcpy(&zero, s, 4);
        memcpy(&value, s + 8, 4);
        memcpy(&secNo, s + 12, 2);
        BYTE cls = s[16], aux = s[17];
        if (secNo >= 1 && secNo <= nsec && (sec[secNo - 1].Characteristics & IMAGE_SCN_CNT_CODE) &&
            (cls == IMAGE_SYM_CLASS_EXTERNAL || cls == IMAGE_SYM_CLASS_STATIC)) {
            const char* nm = "";
            if (zero == 0) {
                memcpy(&longOff, s + 4, 4);
                if (longOff < strLen) nm = strtab + longOff;
            } else {
                memcpy(shortNames, s, 8);
                shortNames[8] = 0;
                nm = shortNames;
                shortNames += 9;
            }
            if (nm[0] && nm[0] != '.') s_syms[s_nsyms++] = {sec[secNo - 1].VirtualAddress + value, nm};
        }
        i += aux;
    }
}
// "function+0x1a" for an address in the exe, "module.dll+0x1234" elsewhere
void describeAddress(void* addr, wchar_t* outBuf, int cap) {
    HMODULE mod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)addr, &mod);
    wchar_t path[MAX_PATH] = L"?";
    if (mod) GetModuleFileNameW(mod, path, MAX_PATH);
    const wchar_t* file = wcsrchr(path, L'\\');
    file = file ? file + 1 : path;
    if (!mod) { swprintf(outBuf, cap, L"0x%llx", (unsigned long long)(ULONG_PTR)addr); return; }
    DWORD64 rva = (DWORD64)((BYTE*)addr - (BYTE*)mod);
    if (mod != GetModuleHandleW(nullptr)) { swprintf(outBuf, cap, L"%ls+0x%llx", file, (unsigned long long)rva); return; }
    loadSymbols();
    const FnSym* best = nullptr;
    for (DWORD i = 0; i < s_nsyms; i++)
        if (s_syms[i].rva <= rva && (!best || s_syms[i].rva > best->rva)) best = &s_syms[i];
    if (!best) { swprintf(outBuf, cap, L"%ls+0x%llx", file, (unsigned long long)rva); return; }
    int st = 0;
    char* dm = abi::__cxa_demangle(best->name, nullptr, nullptr, &st);
    static wchar_t nm[400];
    widen(st == 0 && dm ? dm : best->name, nm, 400);
    free(dm);
    if (wcslen(nm) > 300) wcscpy(nm + 297, L"...");
    swprintf(outBuf, cap, L"%ls+0x%llx  (exe+0x%llx)", nm, (unsigned long long)(rva - best->rva), (unsigned long long)rva);
}
void outFrames(void* const* frames, int n, int skipWrap) {
    static wchar_t line[600];
    int shown = 0;
    for (int i = 0; i < n && frames[i]; i++) {
        describeAddress(frames[i], line, 600);
        if (skipWrap && (wcsstr(line, L"__wrap___cxa_throw") || wcsstr(line, L"recordThrow"))) continue;
        out(L"    %2d  %ls\r\n", shown++, line);
    }
    if (!shown) out(L"    (no frames)\r\n");
}
// walk a faulting thread's stack from its CONTEXT using the x64 unwind tables
int walkContext(CONTEXT ctx, void** frames, int max) {
    int n = 0;
    while (n < max && ctx.Rip) {
        frames[n++] = (void*)ctx.Rip;
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
        if (fn) {
            void* handlerData = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(0 /* UNW_FLAG_NHANDLER */, imageBase, ctx.Rip, fn, &ctx, &handlerData, &establisher, nullptr);
        } else {
            MEMORY_BASIC_INFORMATION mbi;   // leaf function: return address is at [rsp]
            if (!VirtualQuery((void*)ctx.Rsp, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) break;
            ctx.Rip = *(DWORD64*)ctx.Rsp;
            ctx.Rsp += 8;
        }
    }
    return n;
}

// ---- report sections ------------------------------------------------------------------------
void outHeader(const wchar_t* title) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    out(L"==== %ls %ls - %ls ====\r\n", APP_NAME, APP_VERSION, title);
    out(L"When:      %04d-%02d-%02d %02d:%02d:%02d  (%lu s after start)\r\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        (unsigned long)((GetTickCount() - s_start) / 1000));
}
void outThread() {
    out(L"Thread:    %ls (id %lu)\r\n", t_name[0] ? t_name : L"unnamed", (unsigned long)GetCurrentThreadId());
    if (t_ctx[0]) out(L"Doing:     %ls\r\n", t_ctx);
}
BOOL CALLBACK monProc(HMONITOR m, HDC, LPRECT, LPARAM lp) {
    int* idx = (int*)lp;
    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(m, &mi)) return TRUE;
    UINT dx = 0, dy = 0;
    typedef HRESULT(WINAPI * GDFM)(HMONITOR, int, UINT*, UINT*);
    static GDFM f = [] { HMODULE sh = LoadLibraryW(L"shcore.dll"); return sh ? (GDFM)GetProcAddress(sh, "GetDpiForMonitor") : nullptr; }();
    if (f) f(m, 0, &dx, &dy);
    RECT r = mi.rcMonitor, w = mi.rcWork;
    out(L"           #%d %ls%ld,%ld  %ldx%ld  work %ldx%ld  %u dpi (%u%%)\r\n", ++*idx, (mi.dwFlags & MONITORINFOF_PRIMARY) ? L"primary  " : L"",
        r.left, r.top, r.right - r.left, r.bottom - r.top, w.right - w.left, w.bottom - w.top, dx, dx ? dx * 100 / 96 : 0);
    return TRUE;
}
void outSystem() {
    typedef LONG(WINAPI * RGV)(OSVERSIONINFOW*);
    OSVERSIONINFOW v{sizeof(v)};
    if (RGV f = (RGV)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")) f(&v);
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    MEMORYSTATUSEX ms{sizeof(ms)};
    GlobalMemoryStatusEx(&ms);
    out(L"System:    Windows %lu.%lu.%lu, %lu logical processors, %llu MB RAM (%llu MB free)\r\n", v.dwMajorVersion, v.dwMinorVersion,
        v.dwBuildNumber, si.dwNumberOfProcessors, ms.ullTotalPhys >> 20, ms.ullAvailPhys >> 20);
    out(L"Displays:  virtual desktop %d,%d %dx%d\r\n", GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
        GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
    int idx = 0;
    EnumDisplayMonitors(nullptr, nullptr, monProc, (LPARAM)&idx);
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    out(L"Program:   %ls (%ls, built " __DATE__ L")\r\n", exe, s_mode);
}
void outTrail() {
    LONG seq = s_trailSeq;
    if (!seq) return;
    out(L"Recent actions (oldest first):\r\n");
    for (LONG i = seq > 16 ? seq - 16 : 0; i < seq; i++) {
        const TrailRec& t = s_trail[i & 15];
        out(L"    %6.1fs  %ls\r\n", (t.ms - s_start) / 1000.0, t.text);
    }
}
void outSettings() {
    if (!s_describe) return;
    try {
        std::wstring d = s_describe();
        if (!d.empty()) out(L"Settings:  %ls\r\n", d.c_str());
    } catch (...) {
    }
}
// the current C++ exception: type, message, and the throw-site record if we have one
const ThrowRec* outException() {
    const void* obj = nullptr;
    wchar_t type[200] = L"(unknown type)", what[600] = L"";
    if (std::type_info* ti = abi::__cxa_current_exception_type()) {
        int st = 0;
        char* dm = abi::__cxa_demangle(ti->name(), nullptr, nullptr, &st);
        widen(st == 0 && dm ? dm : ti->name(), type, 200);
        free(dm);
    }
    try {
        std::exception_ptr p = std::current_exception();
        if (p) std::rethrow_exception(p);
        wcscpy(type, L"(no active exception: std::terminate called directly)");
    } catch (const std::exception& e) {
        obj = &e;
        int st = 0;
        char* dm = abi::__cxa_demangle(typeid(e).name(), nullptr, nullptr, &st);
        widen(st == 0 && dm ? dm : typeid(e).name(), type, 200);
        free(dm);
        widen(e.what(), what, 600);
    } catch (...) {
    }
    out(L"Exception: %ls\r\n", type);
    if (what[0]) out(L"Message:   %ls\r\n", what);
    // find the record of this throw: same object, else the newest throw on this thread
    const ThrowRec* rec = nullptr;
    LONG seq = s_throwSeq;
    for (LONG i = seq; i > seq - 8 && i > 0 && !rec; i--)
        if (obj && s_throws[i & 7].obj == obj) rec = &s_throws[i & 7];
    for (LONG i = seq; i > seq - 8 && i > 0 && !rec; i--)
        if (s_throws[i & 7].tid == GetCurrentThreadId()) rec = &s_throws[i & 7];
    return rec;
}
void outThrowSite(const ThrowRec* rec) {
    if (!rec) { out(L"Thrown at: (not recorded)\r\n"); return; }
    if (rec->tid != GetCurrentThreadId()) {
        out(L"Thrown on: %ls (id %lu)\r\n", rec->thread[0] ? rec->thread : L"unnamed", (unsigned long)rec->tid);
        if (rec->ctx[0]) out(L"  doing:   %ls\r\n", rec->ctx);
    }
    out(L"Thrown at:\r\n");
    outFrames(rec->frames, rec->n, 1);
}
void writeReport() {
    // UTF-8, appended; the file is rotated once it passes 512 KB
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(s_log, GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 512 * 1024) {
        wchar_t old[MAX_PATH + 64];
        wcscpy(old, s_log);
        wcscpy(wcsrchr(old, L'.'), L".old.log");
        MoveFileExW(s_log, old, MOVEFILE_REPLACE_EXISTING);
    }
    out(L"\r\n");
    static char u8[sizeof(s_buf) * 3 / 2];
    int n = WideCharToMultiByte(CP_UTF8, 0, s_buf, (int)s_len, u8, sizeof(u8), nullptr, nullptr);
    HANDLE f = CreateFileW(s_log, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD wr;
        if (n > 0) WriteFile(f, u8, (DWORD)n, &wr, nullptr);
        CloseHandle(f);
    }
}
void openReport() {
    wchar_t cmd[MAX_PATH + 96];
    swprintf(cmd, MAX_PATH + 96, L"notepad.exe \"%ls\"", s_log);
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi;
    if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
}
void fatalBox(const wchar_t* summary) {
    if (!s_interactive) return;
    wchar_t msg[1400];
    swprintf(msg, 1400,
             APP_NAME L" ran into an internal error and has to close.\n\n%ls\n\nA report was saved to:\n%ls\n\n"
             L"Click OK to open the report. Sending it along helps get the problem fixed.",
             summary, s_log);
    if (MessageBoxW(nullptr, msg, APP_NAME, MB_OKCANCEL | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND | MB_TASKMODAL) == IDOK) openReport();
}
void summaryFromBuffer(wchar_t* sum, int cap) {
    // "Exception: X" + "Message: Y" lines of the report, for the message box
    sum[0] = 0;
    const wchar_t* e = wcsstr(s_buf, L"Exception: ");
    const wchar_t* m = wcsstr(s_buf, L"Message:   ");
    const wchar_t* f = wcsstr(s_buf, L"Fault:     ");
    auto line = [&](const wchar_t* p, int skip) {
        if (!p) return;
        p += skip;
        const wchar_t* q = wcsstr(p, L"\r\n");
        int len = q ? (int)(q - p) : (int)wcslen(p);
        int have = (int)wcslen(sum);
        if (have && have < cap - 2) { wcscat(sum, L"\n"); have++; }
        if (len > cap - have - 1) len = cap - have - 1;
        if (len > 0) { wcsncat(sum, p, len); }
    };
    line(e, 11);
    line(m, 11);
    line(f, 11);
}

// ---- handlers -------------------------------------------------------------------------------
void onTerminate() {
    if (InterlockedExchange(&s_fatal, 1)) TerminateProcess(GetCurrentProcess(), 3);
    EnterCriticalSection(&s_reportCs);
    s_len = 0;
    outHeader(L"crash (unhandled C++ exception)");
    outThread();
    const ThrowRec* rec = outException();
    outThrowSite(rec);
    outTrail();
    outSettings();
    outSystem();
    writeReport();
    wchar_t sum[700];
    summaryFromBuffer(sum, 700);
    LeaveCriticalSection(&s_reportCs);
    fatalBox(sum);
    TerminateProcess(GetCurrentProcess(), 3);
}
const wchar_t* codeName(DWORD c) {
    switch (c) {
    case EXCEPTION_ACCESS_VIOLATION: return L"access violation";
    case EXCEPTION_STACK_OVERFLOW: return L"stack overflow";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return L"integer divide by zero";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return L"illegal instruction";
    case EXCEPTION_IN_PAGE_ERROR: return L"in-page error";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return L"array bounds exceeded";
    case EXCEPTION_PRIV_INSTRUCTION: return L"privileged instruction";
    case 0xC0000409: return L"stack buffer overrun / fail-fast";
    case 0xC0000374: return L"heap corruption";
    }
    return L"hardware or system exception";
}
LONG WINAPI onSeh(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    // GCC C++ exceptions that reach here are handed back to the runtime, which then calls
    // std::terminate (our handler above) with the exception still available.
    if ((code & 0xF0FFFFFFu) == 0x20474343u) return s_prevFilter ? s_prevFilter(ep) : EXCEPTION_CONTINUE_EXECUTION;
    if (InterlockedExchange(&s_fatal, 1)) TerminateProcess(GetCurrentProcess(), 3);
    EnterCriticalSection(&s_reportCs);
    s_len = 0;
    outHeader(L"crash (fault)");
    outThread();
    wchar_t where[600];
    describeAddress(ep->ExceptionRecord->ExceptionAddress, where, 600);
    out(L"Fault:     0x%08lX %ls at %ls\r\n", (unsigned long)code, codeName(code), where);
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        ULONG_PTR op = ep->ExceptionRecord->ExceptionInformation[0], ad = ep->ExceptionRecord->ExceptionInformation[1];
        out(L"           %ls address 0x%llx\r\n", op == 0 ? L"reading" : op == 1 ? L"writing" : L"executing", (unsigned long long)ad);
    }
    static void* frames[48];
    int n = code == EXCEPTION_STACK_OVERFLOW ? 0 : walkContext(*ep->ContextRecord, frames, 48);
    out(L"Stack:\r\n");
    outFrames(frames, n, 0);
    outTrail();
    if (code != EXCEPTION_STACK_OVERFLOW && code != 0xC0000374) outSettings();
    outSystem();
    writeReport();
    wchar_t sum[700];
    summaryFromBuffer(sum, 700);
    LeaveCriticalSection(&s_reportCs);
    fatalBox(sum);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_EXECUTE_HANDLER;
}

__attribute__((noinline)) void recordThrow(void* obj, std::type_info* tinfo) {
    LONG i = InterlockedIncrement(&s_throwSeq);
    ThrowRec& r = s_throws[i & 7];
    r.obj = obj;
    r.type = tinfo;
    r.tid = GetCurrentThreadId();
    r.n = RtlCaptureStackBackTrace(1, 48, r.frames, nullptr);
    wcsncpy(r.thread, t_name, 39);
    r.thread[39] = 0;
    wcsncpy(r.ctx, t_ctx, 219);
    r.ctx[219] = 0;
}

} // namespace

// every C++ throw in the program (including those inside the standard library) passes here
extern "C" {
[[noreturn]] void __real___cxa_throw(void* obj, std::type_info* tinfo, void (*dest)(void*));
[[noreturn]] void __wrap___cxa_throw(void* obj, std::type_info* tinfo, void (*dest)(void*)) {
    recordThrow(obj, tinfo);
    __real___cxa_throw(obj, tinfo, dest);
}
}

void CrashInit(const wchar_t* mode, bool interactive) {
    s_start = GetTickCount();
    InitializeCriticalSection(&s_reportCs);
    wcsncpy(s_mode, mode, 15);
    s_interactive = interactive;
    wchar_t dir[MAX_PATH] = L"";
    DWORD n = GetEnvironmentVariableW(L"APPDATA", dir, MAX_PATH);
    if (!n || n >= MAX_PATH) wcscpy(dir, L".");
    swprintf(s_log, MAX_PATH + 64, L"%ls\\IrixBackground", dir);
    CreateDirectoryW(s_log, nullptr);
    wcscat(s_log, L"\\crash.log");
    CrashThread(L"main");
    std::set_terminate(onTerminate);
    s_prevFilter = SetUnhandledExceptionFilter(onSeh);
}
void CrashThread(const wchar_t* name) {
    wcsncpy(t_name, name, 39);
    t_name[39] = 0;
    t_ctx[0] = 0;
}
void CrashContext(const wchar_t* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vswprintf(t_ctx, 220, fmt, a);
    va_end(a);
}
const wchar_t* CrashGetContext() { return t_ctx; }
void CrashTrail(const wchar_t* fmt, ...) {
    LONG i = InterlockedIncrement(&s_trailSeq) - 1;
    TrailRec& t = s_trail[i & 15];
    t.ms = GetTickCount();
    va_list a;
    va_start(a, fmt);
    vswprintf(t.text, 120, fmt, a);
    va_end(a);
}
std::wstring CrashRecovered(const wchar_t* where) {
    wchar_t sum[700];
    LONG k = InterlockedIncrement(&s_recovered);
    EnterCriticalSection(&s_reportCs);
    s_len = 0;
    outHeader(L"recovered error");
    out(L"Caught in: %ls\r\n", where);
    outThread();
    const ThrowRec* rec = outException();
    outThrowSite(rec);
    if (k <= 20) {   // a repeating failure is logged in full a limited number of times
        outTrail();
        outSettings();
        outSystem();
        writeReport();
    }
    summaryFromBuffer(sum, 700);
    LeaveCriticalSection(&s_reportCs);
    std::wstring s = sum;
    for (auto& c : s) if (c == L'\n') c = L' ';
    return s;
}
std::wstring CrashLogPath() { return s_log; }
int CrashRecoveredCount() { return (int)s_recovered; }
void CrashSetDescriber(std::wstring (*fn)()) { s_describe = fn; }
