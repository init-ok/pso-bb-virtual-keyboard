#include "Logger.h"

#include <fileapi.h>
#include <handleapi.h>
#include <minwindef.h>
#include <synchapi.h>
#include <winnt.h>

#include <string>

HANDLE Logger::fileHandle = INVALID_HANDLE_VALUE;
SRWLOCK Logger::lock = SRWLOCK_INIT;

bool Logger::Initialize(const wchar_t* path) {
  fileHandle = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

  return (fileHandle != INVALID_HANDLE_VALUE);
}

void Logger::Shutdown() {
  if (fileHandle != INVALID_HANDLE_VALUE) {
    CloseHandle(fileHandle);
    fileHandle = INVALID_HANDLE_VALUE;
  }
}

void Logger::Write(const std::string& text) {
  if (fileHandle == INVALID_HANDLE_VALUE) return;

  AcquireSRWLockExclusive(&lock);

  DWORD written = 0;
  WriteFile(fileHandle, text.c_str(), (DWORD)text.size(), &written, nullptr);

  ReleaseSRWLockExclusive(&lock);
}
