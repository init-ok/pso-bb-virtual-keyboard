// NOLINTBEGIN
#include <windows.h>
// NOLINTEND

#include <libloaderapi.h>
#include <shared.h>
#include <sysinfoapi.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#pragma comment(lib, "shared_utils.lib")

namespace fs = std::filesystem;

struct d3d9_dll {
  HMODULE dll;
  FARPROC OrignalD3DPERF_BeginEvent;
  FARPROC OrignalD3DPERF_EndEvent;
  FARPROC OrignalD3DPERF_GetStatus;
  FARPROC OrignalD3DPERF_QueryRepeatFrame;
  FARPROC OrignalD3DPERF_SetMarker;
  FARPROC OrignalD3DPERF_SetOptions;
  FARPROC OrignalD3DPERF_SetRegion;
  FARPROC OrignalDebugSetLevel;
  FARPROC OrignalDebugSetMute;
  FARPROC OrignalDirect3D9EnableMaximizedWindowedModeShim;
  FARPROC OrignalDirect3DCreate9;
  FARPROC OrignalDirect3DCreate9Ex;
  FARPROC OrignalDirect3DShaderValidatorCreate9;
  FARPROC OrignalPSGPError;
  FARPROC OrignalPSGPSampleTexture;
} d3d9;

__declspec(naked) void FakeD3DPERF_BeginEvent() {
  _asm { jmp[d3d9.OrignalD3DPERF_BeginEvent] }
}
__declspec(naked) void FakeD3DPERF_EndEvent() {
  _asm { jmp[d3d9.OrignalD3DPERF_EndEvent] }
}
__declspec(naked) void FakeD3DPERF_GetStatus() {
  _asm { jmp[d3d9.OrignalD3DPERF_GetStatus] }
}
__declspec(naked) void FakeD3DPERF_QueryRepeatFrame() {
  _asm { jmp[d3d9.OrignalD3DPERF_QueryRepeatFrame] }
}
__declspec(naked) void FakeD3DPERF_SetMarker() {
  _asm { jmp[d3d9.OrignalD3DPERF_SetMarker] }
}
__declspec(naked) void FakeD3DPERF_SetOptions() {
  _asm { jmp[d3d9.OrignalD3DPERF_SetOptions] }
}
__declspec(naked) void FakeD3DPERF_SetRegion() {
  _asm { jmp[d3d9.OrignalD3DPERF_SetRegion] }
}
__declspec(naked) void FakeDebugSetLevel() {
  _asm { jmp[d3d9.OrignalDebugSetLevel] }
}
__declspec(naked) void FakeDebugSetMute() {
  _asm { jmp[d3d9.OrignalDebugSetMute] }
}
__declspec(naked) void FakeDirect3D9EnableMaximizedWindowedModeShim() {
  _asm { jmp[d3d9.OrignalDirect3D9EnableMaximizedWindowedModeShim] }
}
__declspec(naked) void FakeDirect3DCreate9() {
  _asm { jmp[d3d9.OrignalDirect3DCreate9] }
}
__declspec(naked) void FakeDirect3DCreate9Ex() {
  _asm { jmp[d3d9.OrignalDirect3DCreate9Ex] }
}
__declspec(naked) void FakeDirect3DShaderValidatorCreate9() {
  _asm { jmp[d3d9.OrignalDirect3DShaderValidatorCreate9] }
}
__declspec(naked) void FakePSGPError() {
  _asm { jmp[d3d9.OrignalPSGPError] }
}
__declspec(naked) void FakePSGPSampleTexture() {
  _asm { jmp[d3d9.OrignalPSGPSampleTexture] }
}

template <typename Func>
void EnumerateFiles(const std::wstring& directory, Func&& func) {
  fs::path dir_path = directory;

  // Check if the directory exists
  if (!fs::exists(dir_path) || !fs::is_directory(dir_path)) {
    LOG("Directory doesn't exist: %S", directory.c_str());
    exit(1);
  }

  // Iterate over the entries in the directory
  for (const auto& entry : fs::directory_iterator(dir_path)) {
    // Get the filename or directory name
    std::string filename_str = entry.path().filename().string();
    if (entry.is_regular_file()) {
      func(entry.path());
    }
  }
}

static std::vector<std::string> GetExportedFunctions(
    const std::wstring& dllPath) {
  std::vector<std::string> exports;

  // Load the DLL without running DllMain
  HMODULE hMod =
      LoadLibraryExW(dllPath.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
  if (!hMod) {
    // Optional: handle error with GetLastError()
    return exports;
  }

  auto base = reinterpret_cast<const BYTE*>(hMod);

  // DOS header
  auto dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
    FreeLibrary(hMod);
    return exports;
  }

  // NT headers
  auto ntHeaders =
      reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dosHeader->e_lfanew);
  if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
    FreeLibrary(hMod);
    return exports;
  }

  const IMAGE_DATA_DIRECTORY& exportDirData =
      ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];

  if (exportDirData.VirtualAddress == 0 || exportDirData.Size == 0) {
    // No export table
    FreeLibrary(hMod);
    return exports;
  }

  auto exportDir = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(
      base + exportDirData.VirtualAddress);

  auto nameRvas =
      reinterpret_cast<const DWORD*>(base + exportDir->AddressOfNames);

  // Iterate over function names
  for (DWORD i = 0; i < exportDir->NumberOfNames; ++i) {
    const char* funcName = reinterpret_cast<const char*>(base + nameRvas[i]);
    exports.emplace_back(funcName);
  }

  FreeLibrary(hMod);
  return exports;
}

static void LoadModDll(std::wstring path, std::string loadFuncName) {
  LOG("Loading %S", path.c_str());
  HMODULE modDll =
      LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);

  if (modDll == NULL) {
    LOG("Failed to load mod library");
    exit(1);
  }
  auto funcPtr = GetProcAddress(modDll, loadFuncName.c_str());
  if (funcPtr == NULL) {
    LOG("Failed to find Load() function");
    exit(1);
  }
  funcPtr();
}

static void MaybeLoadModDll(std::wstring path) {
  auto exports = GetExportedFunctions(path);
  // name from dumpbin /exports mydll.dll
  auto loadFuncName = std::string("_LoadMod@0");
  auto it = std::find(exports.begin(), exports.end(), loadFuncName);
  if (it == exports.end()) {
    LOG("Skipping dll without LoadMod() function %S", path.c_str());
    return;
  }
  LoadModDll(path, loadFuncName);
}

static void LoadModDlls() {
  LOG("Loading mod DLLs...");
  auto modPath = GetModFolderPath();
  EnumerateFiles(modPath, [](std::wstring path) {
    auto extension = std::filesystem::path(path).extension().wstring();
    if (extension != L".dll") {
      return;
    }
    MaybeLoadModDll(path);
  });
}

static void SetUpProxy() {
  wchar_t system_dir[MAX_PATH];
  GetSystemDirectory(system_dir, MAX_PATH);
  auto path_str = std::wstring(system_dir) + L"\\d3d9.dll";

  d3d9.dll = LoadLibrary(path_str.c_str());
  if (d3d9.dll == NULL) {
    LOG("Failed to load system d3d9.dll");
    exit(1);
  }

  // Forward functions to original dll
  d3d9.OrignalD3DPERF_BeginEvent =
      GetProcAddress(d3d9.dll, "D3DPERF_BeginEvent");
  d3d9.OrignalD3DPERF_EndEvent = GetProcAddress(d3d9.dll, "D3DPERF_EndEvent");
  d3d9.OrignalD3DPERF_GetStatus = GetProcAddress(d3d9.dll, "D3DPERF_GetStatus");
  d3d9.OrignalD3DPERF_QueryRepeatFrame =
      GetProcAddress(d3d9.dll, "D3DPERF_QueryRepeatFrame");
  d3d9.OrignalD3DPERF_SetMarker = GetProcAddress(d3d9.dll, "D3DPERF_SetMarker");
  d3d9.OrignalD3DPERF_SetOptions =
      GetProcAddress(d3d9.dll, "D3DPERF_SetOptions");
  d3d9.OrignalD3DPERF_SetRegion = GetProcAddress(d3d9.dll, "D3DPERF_SetRegion");
  d3d9.OrignalDebugSetLevel = GetProcAddress(d3d9.dll, "DebugSetLevel");
  d3d9.OrignalDebugSetMute = GetProcAddress(d3d9.dll, "DebugSetMute");
  d3d9.OrignalDirect3D9EnableMaximizedWindowedModeShim =
      GetProcAddress(d3d9.dll, "Direct3D9EnableMaximizedWindowedModeShim");
  d3d9.OrignalDirect3DCreate9 = GetProcAddress(d3d9.dll, "Direct3DCreate9");
  d3d9.OrignalDirect3DCreate9Ex = GetProcAddress(d3d9.dll, "Direct3DCreate9Ex");
  d3d9.OrignalDirect3DShaderValidatorCreate9 =
      GetProcAddress(d3d9.dll, "Direct3DShaderValidatorCreate9");
  d3d9.OrignalPSGPError = GetProcAddress(d3d9.dll, "PSGPError");
  d3d9.OrignalPSGPSampleTexture = GetProcAddress(d3d9.dll, "PSGPSampleTexture");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call,
                      LPVOID lpReserved) {
  switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
      InitializeLogger();
      SetUpProxy();
      LoadModDlls();
      break;
    case DLL_PROCESS_DETACH:
      LOG("Tearing down logger");
      TearDownLogger();
      break;
  }
  return TRUE;
}
