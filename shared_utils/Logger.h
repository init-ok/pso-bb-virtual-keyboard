#pragma once
#include <windows.h>

#include <string>

class Logger {
 public:
  static bool Initialize(const wchar_t* path);
  static void Shutdown();
  static void Write(const std::string& text);

 private:
  static HANDLE fileHandle;
  static SRWLOCK lock;
};
