// Crash reporting. An uncaught C++ exception or a hardware fault no longer makes the program
// vanish: it writes %APPDATA%\IrixBackground\crash.log - what went wrong, on which thread, what
// that thread was doing, the recent user actions, the display setup and the call stack with
// function names - and tells the user where the report is.
#pragma once
#include <windows.h>
#include <string>

#include "appinfo.h"

// mode = "panel", "watch", "apply", "selftest"; interactive = may show message boxes
void CrashInit(const wchar_t* mode, bool interactive);
void CrashThread(const wchar_t* name);             // names the calling thread in reports
void CrashContext(const wchar_t* fmt, ...);        // what the calling thread is doing right now
const wchar_t* CrashGetContext();                  // (so worker threads can inherit it)
void CrashTrail(const wchar_t* fmt, ...);          // recent user actions (kept in a small ring)
// Call inside catch(...) at a recovery boundary (window procedure, render worker, task):
// logs the full report as "recovered" and returns a one-line summary for the status line.
std::wstring CrashRecovered(const wchar_t* where);
std::wstring CrashLogPath();
int CrashRecoveredCount();                         // recovered errors so far (the self test expects none)
// Optional: the UI registers a callback that describes the current settings for reports.
void CrashSetDescriber(std::wstring (*fn)());
