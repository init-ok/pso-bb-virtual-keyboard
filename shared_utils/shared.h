#pragma once

#include <string>

__declspec(dllexport) void dbgprintf(const char* func, const char* fmt, ...);

#define LOG(...) dbgprintf(__func__, __VA_ARGS__)

__declspec(dllexport) std::wstring GetGameDirectory();

__declspec(dllexport) std::wstring GetModFolderPath();

__declspec(dllexport) void InitializeLogger();

__declspec(dllexport) void TearDownLogger();

__declspec(dllexport) void WaitForDebuggerAndBreak();