#include "shared.h"

#include <Windows.h>
#include <corecrt.h>
#include <debugapi.h>
#include <libloaderapi.h>
#include <processthreadsapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <vadefs.h>

#include <chrono>
#include <cstdarg>
#include <filesystem>
#include <string>
#include <thread>

#include "Logger.h"

namespace fs = std::filesystem;

inline std::string GetTimestamp() {
  time_t rawtime;
  struct tm timeinfo;

  // Get the current calendar time
  time(&rawtime);

  // Convert to local time using localtime_s
  localtime_s(&timeinfo, &rawtime);

  char buffer[256];
  sprintf_s(buffer, "%d-%02d-%02d %02d:%02d:%02d",
            timeinfo.tm_year + 1900,  // Year is years since 1900
            timeinfo.tm_mon + 1,      // Month is 0-indexed (0-11)
            timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min,
            timeinfo.tm_sec);

  return std::string(buffer);
}

static Logger logger;

void InitializeLogger() {
  auto dir = fs::path(GetModFolderPath());
  auto logPath = dir.append(L"logs.txt");
  logger.Initialize(logPath.wstring().c_str());
}

void TearDownLogger() { logger.Shutdown(); }

void dbgprintf(const char* func, const char* fmt, ...) {
  char buf[1024];

  va_list args;
  va_start(args, fmt);
  _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
  va_end(args);

  auto str =
      GetTimestamp() + " " + std::string(func) + ": " + std::string(buf) + "\n";
  logger.Write(str);
}

std::wstring GetGameDirectory() {
  wchar_t path[MAX_PATH];
  DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (length == 0 || length == MAX_PATH) {
    LOG("Failed to get executable path");
    exit(1);
  }

  auto directory = fs::path(path).parent_path();
  return directory.wstring();
}

std::wstring GetModFolderPath() {
  auto path = fs::path(GetGameDirectory());
  path.append("mod");
  return path;
}

// Useful for debugging
void WaitForDebuggerAndBreak() {
  unsigned int poll_ms = 100;
  // Print PID so you can find the process in Visual Studio's Attach dialog.
  DWORD pid = GetCurrentProcessId();
  LOG("PID=%u waiting for debugger...\n", (unsigned)pid);

  // Spin until a debugger is attached. Sleep to avoid busy-waiting.
  while (!IsDebuggerPresent()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
  }

  // Once a debugger is attached, trigger a breakpoint so the debugger stops
  // here. Use __debugbreak (MSVC intrinsic) or DebugBreak().
#if defined(_MSC_VER)
  __debugbreak();
#else
  DebugBreak();
#endif
}
