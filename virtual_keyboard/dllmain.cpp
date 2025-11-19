#include <Unknwnbase.h>
#include <Windows.h>
#include <Xinput.h>
#include <basetsd.h>
#include <bcrypt.h>
#include <d3d9.h>
#include <d3d9types.h>
#include <dinput.h>
#include <guiddef.h>
#include <libloaderapi.h>
#include <processthreadsapi.h>
#include <stdlib.h>
#include <sysinfoapi.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <list>
#include <memory>
#include <numeric>
#include <ranges>
#include <string>
#include <unordered_map>
#include <vector>

#include "easyhook.h"
#include "mini.h"
#include "shared.h"
#include "virtual_keyboard.h"

#pragma comment(lib, "d3d9")
#pragma comment(lib, "XInput")
#pragma comment(lib, "dInput8")
#pragma comment(lib, "dxguid")

#pragma comment(lib, "shared_utils.lib")

#ifdef _DEBUG
#pragma comment(lib, "easyhook-x86-v140-debug.lib")
#else
#pragma comment(lib, "easyhook-x86-v140-release.lib")
#endif

static VirtualKeyboard virtualKeyboard;

HWND GetMainWindowOfCurrentProcess();

static void HookFunction(void* funcAddress, void* hookFunc,
                         void** origFuncPtr) {
  HOOK_TRACE_INFO hHook = {NULL};  // keep track of our hook

  if (origFuncPtr) {
    *origFuncPtr = funcAddress;
  }

  // Install hooks
  NTSTATUS result = LhInstallHook(funcAddress, hookFunc, NULL, &hHook);

  if (FAILED(result)) {
    std::wstring s(RtlGetLastErrorString());
    LOG("Failed to install hook: %s", s.c_str());
    exit(1);
  }

  // Enable the hook for all threads.
  ULONG ACLEntries[1] = {0};
  LhSetExclusiveACL(ACLEntries, 0, &hHook);
}

static void HookFunction(std::string module, std::string function,
                         void* hookFunc, void** origFuncPtr) {
  HMODULE moduleHandle = GetModuleHandleA(module.c_str());
  if (moduleHandle == NULL) {
    // Load the library since it may or may not be loaded depending on when this
    // is called.
    moduleHandle = LoadLibraryA(module.c_str());
    if (moduleHandle == NULL) {
      LOG("Failed to load %s", module.c_str());
      exit(1);
      return;
    }
    LOG("Loaded %s", module.c_str());
  } else {
    LOG("%s already loaded", module.c_str());
  }

  FARPROC funcAddress = GetProcAddress(moduleHandle, function.c_str());
  if (funcAddress == NULL) {
    LOG("Failed to find %s", function.c_str());
    exit(1);
    return;
  }
  LOG("Hooked %s", function.c_str());
  HookFunction((void*)funcAddress, hookFunc, origFuncPtr);
}

// This supports all keys on the virtual keyboard (unlike PostMessage) but
// only works when the game window is focused.
// https://devblogs.microsoft.com/oldnewthing/20050530-11/?p=35513
static void EmulateKeyDown(char ch) {
  SHORT vkPacked = VkKeyScanA(ch);
  if (vkPacked == -1) {
    // Character not mappable in current layout
    return;
  }

  BYTE vk = LOBYTE(vkPacked);
  BYTE mod = HIBYTE(vkPacked);  // bit0=Shift, bit1=Ctrl, bit2=Alt

  INPUT inputs[8] = {};
  int idx = 0;

  auto addKey = [&](WORD vkCode, bool down) {
    INPUT& in = inputs[idx++];
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vkCode;
    in.ki.wScan = MapVirtualKey(vkCode, MAPVK_VK_TO_VSC);
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
  };

  // Press modifiers if needed
  if (mod & 1) addKey(VK_SHIFT, true);
  if (mod & 2) addKey(VK_CONTROL, true);
  if (mod & 4) addKey(VK_MENU, true);

  // Main key down + up
  addKey(vk, true);
  addKey(vk, false);

  // Release modifiers
  if (mod & 4) addKey(VK_MENU, false);
  if (mod & 2) addKey(VK_CONTROL, false);
  if (mod & 1) addKey(VK_SHIFT, false);

  SendInput(idx, inputs, sizeof(INPUT));
}

inline static uint64_t NowMs() { return GetTickCount64(); }
inline static uint64_t MsFromNow(uint32_t ms) { return NowMs() + ms; }

struct EmulatedKey {
  BYTE code;
  uint64_t expireTimeMs;
  std::function<void()> onExpire;

  EmulatedKey(BYTE code) {
    this->code = code;
    this->expireTimeMs = MsFromNow(200);
    this->onExpire = []() {};
  }

  EmulatedKey(BYTE code, std::function<void()> onExpire) {
    this->code = code;
    this->expireTimeMs = MsFromNow(200);
    this->onExpire = onExpire;
  }
};

static std::vector<EmulatedKey> emulatedKeys;

static char ToChar(wchar_t wch) {
  // Only safe for ASCII range (0–127)
  if (wch >= 0 && wch <= 127) {
    return static_cast<char>(wch);
  } else {
    LOG("Failed to convert wchar to char");
    exit(1);
  }
}

static std::vector<int> ButtonComboFromString(std::string comboString) {
  std::unordered_map<std::string, int> controlMap{
      {"DPAD_UP", XINPUT_GAMEPAD_DPAD_UP},
      {"DPAD_DOWN", XINPUT_GAMEPAD_DPAD_DOWN},
      {"DPAD_LEFT", XINPUT_GAMEPAD_DPAD_LEFT},
      {"DPAD_RIGHT", XINPUT_GAMEPAD_DPAD_RIGHT},
      {"START", XINPUT_GAMEPAD_START},
      {"BACK", XINPUT_GAMEPAD_BACK},
      {"LEFT_THUMB", XINPUT_GAMEPAD_LEFT_THUMB},
      {"RIGHT_THUMB", XINPUT_GAMEPAD_RIGHT_THUMB},
      {"LEFT_SHOULDER", XINPUT_GAMEPAD_LEFT_SHOULDER},
      {"RIGHT_SHOULDER", XINPUT_GAMEPAD_RIGHT_SHOULDER},
      {"A", XINPUT_GAMEPAD_A},
      {"B", XINPUT_GAMEPAD_B},
      {"X", XINPUT_GAMEPAD_X},
      {"Y", XINPUT_GAMEPAD_Y}};

  std::vector<int> comboButtons;
  // Sanitize input
  comboString.erase(
      std::remove_if(comboString.begin(), comboString.end(), ::isspace),
      comboString.end());

  for (auto subrange : comboString | std::views::split('+')) {
    auto buttonName = std::string(subrange.begin(), subrange.end());
    // Convert to uppercase
    std::transform(buttonName.begin(), buttonName.end(), buttonName.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    auto it = controlMap.find(buttonName);
    if (it == controlMap.end()) {
      std::vector<std::string> validButtonNames;
      for (const auto& pair : controlMap) {
        validButtonNames.push_back(pair.first);
      }
      std::string buttonNamesString = std::accumulate(
          std::next(validButtonNames.begin()), validButtonNames.end(),
          validButtonNames[0], [&](const std::string& a, const std::string& b) {
            return a + ", " + b;
          });
      LOG("Unknown button name: %s. Valid button choices: %s",
          buttonName.c_str(), buttonNamesString.c_str());
      exit(1);
    }
    LOG("Adding combo button: %s", buttonName.c_str());
    auto buttonCode = it->second;
    comboButtons.push_back(buttonCode);
  }
  return comboButtons;
}

static std::vector<int> toggleKeyboardButtonCombo;
static bool IsToggleKeyboardButtonPressed(WORD buttons) {
  if (toggleKeyboardButtonCombo.empty()) {
    LOG("The 'Toggle keyboard' button combo was unexpectedly empty.");
    exit(1);
  }
  for (auto key : toggleKeyboardButtonCombo) {
    if ((buttons & key) == 0) {
      return false;
    }
  }
  return true;
}

static DWORD(WINAPI* XInputGetStateOrig)(DWORD dwUserIndex,
                                         XINPUT_STATE* pState) = nullptr;

static DWORD WINAPI XInputGetStateHook(DWORD dwUserIndex,
                                       XINPUT_STATE* pState) {
  DWORD result = XInputGetStateOrig(dwUserIndex, pState);
  if (result != ERROR_SUCCESS || !pState) {
    return result;
  }

  virtualKeyboard.HandleInput(*pState);
  WORD buttons = pState->Gamepad.wButtons;

  // Emulate keyboard keydown if button was pressed down
  static bool aPreviouslyPressed = false;
  bool isAPressed = (buttons & XINPUT_GAMEPAD_A) != 0;

  if (virtualKeyboard.enabled && isAPressed && !aPreviouslyPressed) {
    // Emulate keyboard key being pressed.
    auto pressedKey = virtualKeyboard.GetSelectedKey();
    if (pressedKey == L"Backspace") {
      // Send backspace
      emulatedKeys.push_back(EmulatedKey(DIK_BACKSPACE));
    } else if (pressedKey == L"Exit") {
      LOG("Pressed Exit");
      // Emulate F11 once that finishes to close the virtual keyboard.
      // Escape before that clears the entered text (if we're in the chat
      // box).
      emulatedKeys.push_back(EmulatedKey(DIK_ESCAPE, []() {
        emulatedKeys.push_back(
            EmulatedKey(DIK_F11, []() { virtualKeyboard.enabled = false; }));
      }));
    } else if (pressedKey == L"OK") {
      LOG("Pressed OK");
      // Emulate DirectInput return key
      emulatedKeys.push_back(EmulatedKey(DIK_RETURN, []() {
        // Emulate F11 once that finishes to close the virtual keyboard
        emulatedKeys.push_back(
            EmulatedKey(DIK_F11, []() { virtualKeyboard.enabled = false; }));
      }));
    } else if (pressedKey == L"Space") {
      // Send space
      PostMessageA(GetMainWindowOfCurrentProcess(), WM_KEYDOWN, VK_SPACE, 0);
    } else if (pressedKey.length() == 1) {
      char key = ToChar(pressedKey[0]);
      EmulateKeyDown(key);
    } else {
      LOG("Unhandled key press: %s", pressedKey.c_str());
      exit(1);
    }
  }
  aPreviouslyPressed = isAPressed;

  // Open virtual keyboard if back button was pressed.
  static bool toggleKbPressedPrev = false;
  bool toggleKbPressed = IsToggleKeyboardButtonPressed(buttons);
  if (!virtualKeyboard.enabled && toggleKbPressed && !toggleKbPressedPrev) {
    LOG("Opening keyboard");

    // Emulate DirectInput F11 key
    emulatedKeys.push_back(EmulatedKey(DIK_F11));
  } else if (virtualKeyboard.enabled && toggleKbPressed &&
             !toggleKbPressedPrev) {
    LOG("Closing keyboard");

    // Emulate DirectInput F11 key
    emulatedKeys.push_back(EmulatedKey(DIK_F11));
    virtualKeyboard.enabled = false;
  }
  toggleKbPressedPrev = toggleKbPressed;

  if (virtualKeyboard.enabled) {
    // Filter out button presses from the game.
    *pState = {};
    return 0;
  }
  return result;
}

static HRESULT(STDMETHODCALLTYPE* DInputCreateDevice)(
    IDirectInput8W* self, REFGUID rguid, IDirectInputDevice8W** outDevice,
    LPUNKNOWN unk) = nullptr;

typedef HRESULT(STDMETHODCALLTYPE* GetDeviceState_t)(IDirectInputDevice8W* self,
                                                     DWORD cbData,
                                                     LPVOID lpvData);
GetDeviceState_t GetDeviceState = nullptr;

IDirectInputDevice8W* keyboardDevice = nullptr;

// This function is used for:
// - function keys (e.g. F11 to open/close chat)
// - character controls (e.g. wasd)
// It is not used for entering characters in chat.
static HRESULT STDMETHODCALLTYPE GetDeviceStateHook(IDirectInputDevice8W* self,
                                                    DWORD cbData,
                                                    LPVOID lpvData) {
  HRESULT hr = GetDeviceState(self, cbData, lpvData);
  if (hr != DI_OK) {
    // This is called so often that it doesn't make sense to log a failure.
    return hr;
  }

  if (self != keyboardDevice) {
    return hr;
  }

  // Check if keys are expired.
  auto timeNowMs = NowMs();
  auto expiredKeys = std::vector<EmulatedKey>();
  auto it = emulatedKeys.begin();
  while (it != emulatedKeys.end()) {
    auto& key = *it;
    bool expired = timeNowMs > key.expireTimeMs;
    if (expired) {
      expiredKeys.push_back(key);
      it = emulatedKeys.erase(it);
      continue;
    }
    ++it;
  }

  // Notify observers that keys expired (done in a separate
  // loop since observers sometimes modify the original vector
  // in the onExpire callback).
  for (auto& key : expiredKeys) {
    key.onExpire();
  }

  BYTE* diKeys = reinterpret_cast<BYTE*>(lpvData);
  for (auto& key : emulatedKeys) {
    diKeys[key.code] |= 0x80;
  }

  return hr;
}

static HRESULT STDMETHODCALLTYPE
DInputCreateDeviceHook(IDirectInput8W* self, REFGUID rguid,
                       IDirectInputDevice8W** outDevice, LPUNKNOWN unk) {
  LOG("Called");
  HRESULT hr = DInputCreateDevice(self, rguid, outDevice, unk);
  if (!SUCCEEDED(hr) || !outDevice || !*outDevice) {
    LOG("CreateDevice failed");
    exit(1);
  }

  if (!IsEqualGUID(rguid, GUID_SysKeyboard)) {
    return hr;
  }
  LOG("Created keyboard device");
  keyboardDevice = *outDevice;
  static bool installedHooks = false;
  if (!installedHooks) {
    void** vtable = *(void***)*outDevice;
    HookFunction(vtable[9], (void*)GetDeviceStateHook, (void**)&GetDeviceState);
    LOG("GetDeviceState hooked");
    installedHooks = true;
  }
  return hr;
}

static HRESULT(WINAPI* DirectInput8CreateOrig)(HINSTANCE hinst, DWORD dwVersion,
                                               REFIID riid, LPVOID* ppvOut,
                                               LPUNKNOWN punkOuter) = nullptr;

static HRESULT WINAPI DirectInput8CreateHook(HINSTANCE hinst, DWORD dwVersion,
                                             REFIID riid, LPVOID* ppvOut,
                                             LPUNKNOWN punkOuter) {
  LOG("Called");

  HRESULT hr =
      DirectInput8CreateOrig(hinst, dwVersion, riid, ppvOut, punkOuter);
  if (!SUCCEEDED(hr) || !ppvOut || !*ppvOut) {
    LOG("DirectInput8Create failed");
    exit(1);
  }
  void** vtable = *(void***)*ppvOut;
  HookFunction(vtable[3], (void*)DInputCreateDeviceHook,
               (void**)&DInputCreateDevice);
  LOG("CreateDevice hooked");
  return hr;
}

static HRESULT(STDMETHODCALLTYPE* D3DCreateDevice)(
    IDirect3D9* self, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
    DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters,
    IDirect3DDevice9** ppReturnedDeviceInterface) = nullptr;

static HRESULT(APIENTRY* EndScene)(IDirect3DDevice9* device) = nullptr;

static void DrawVirtualKeyboard(IDirect3DDevice9* device) {
  if (!device) {
    return;
  }

  struct Vertex {
    float x, y, z, rhw;
    DWORD color;
  };

  // Backup the device state before we change anything.
  IDirect3DStateBlock9* state_block = nullptr;
  if (device->CreateStateBlock(D3DSBT_ALL, &state_block) < 0) {
    return;
  }

  if (state_block->Capture() < 0) {
    state_block->Release();
    return;
  }

  // Set up alpha blending for the overlay.
  device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
  device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
  device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
  device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
  device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
  device->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
  device->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA);

  // The overlay turns grayscale without this.
  device->SetRenderState(D3DRS_FOGENABLE, FALSE);

  // The overlay turns completely black without this.
  device->SetTexture(0, nullptr);

  virtualKeyboard.Draw(device);

  // Restore the device state since we changed some stuff.
  state_block->Apply();
  state_block->Release();
}

// The game calls this multiple times per frame, so it isn't the best for
// doing an overlay.
static HRESULT APIENTRY EndSceneHook(IDirect3DDevice9* device) {
  DrawVirtualKeyboard(device);
  return EndScene(device);
}

static HRESULT STDMETHODCALLTYPE D3DCreateDeviceHook(
    IDirect3D9* pThis, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
    DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters,
    IDirect3DDevice9** ppReturnedDeviceInterface) {
  LOG("Called");
  // Call the original CreateDevice implementation first
  HRESULT hr =
      D3DCreateDevice(pThis, Adapter, DeviceType, hFocusWindow, BehaviorFlags,
                      pPresentationParameters, ppReturnedDeviceInterface);

  // If the original failed or didn't return a device pointer, just forward
  // the result
  if (FAILED(hr) || ppReturnedDeviceInterface == nullptr ||
      *ppReturnedDeviceInterface == nullptr) {
    return hr;
  }

  static bool installedHooks = false;
  if (!installedHooks) {
    IDirect3DDevice9* device = *ppReturnedDeviceInterface;
    void** vtbl = *reinterpret_cast<void***>(device);

    HookFunction(vtbl[42], (void*)EndSceneHook, (void**)&EndScene);
    LOG("Installed IDirect3DDevice9::EndScene hook");

    virtualKeyboard.Initialize(device);
    installedHooks = true;
  }
  return hr;
}

static IDirect3D9*(WINAPI* Direct3DCreate9Orig)(UINT SDKVersion) = nullptr;

static IDirect3D9* WINAPI Direct3DCreate9Hook(UINT SDKVersion) {
  LOG("Called");
  IDirect3D9* d3d9 = Direct3DCreate9Orig(SDKVersion);
  if (!d3d9) {
    return nullptr;
  }

  void** vtbl = *reinterpret_cast<void***>(d3d9);
  void* funcAddress = vtbl[16];
  HookFunction(funcAddress, (void*)D3DCreateDeviceHook,
               (void**)&D3DCreateDevice);
  LOG("Installed IDirect3D9::CreateDevice hook");
  return d3d9;
}

HWND GetMainWindowOfCurrentProcess() {
  static HWND gameWindow = nullptr;
  if (gameWindow != nullptr) {
    return gameWindow;
  }
  struct EnumData {
    DWORD pid;
    HWND result;
  };
  EnumData data{GetCurrentProcessId(), NULL};

  EnumWindows(
      [](HWND hwnd, LPARAM lParam) -> BOOL {
        EnumData* d = reinterpret_cast<EnumData*>(lParam);
        DWORD winPid = 0;
        GetWindowThreadProcessId(hwnd, &winPid);
        if (winPid != d->pid) return TRUE;  // keep enumerating

        if (!IsWindowVisible(hwnd)) return TRUE;
        LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        if (exStyle & WS_EX_TOOLWINDOW) return TRUE;
        int len = GetWindowTextLength(hwnd);
        if (len == 0) return TRUE;

        d->result = hwnd;
        return FALSE;  // stop enumerating, we found one
      },
      reinterpret_cast<LPARAM>(&data));
  if (data.result == NULL) {
    LOG("Failed to find game window");
    exit(1);
  }
  gameWindow = data.result;
  return data.result;
}

static std::string GetConfigValue(mINI::INIMap<std::string>& prefs,
                                  std::string name, std::string defaultValue) {
  // Create it if needed
  if (!prefs.has(name)) {
    prefs[name] = defaultValue;
  }
  auto& value = prefs[name];
  LOG("Loaded from config name: %s, value: %s", name.c_str(), value.c_str());
  return value;
}

static void LoadConfig() {
  auto modPath = GetModFolderPath();
  auto configPath =
      std::filesystem::path(modPath).append("config.ini").string();
  LOG("Loading config at %s", configPath.c_str());

  // Create/read config
  mINI::INIFile file(configPath);
  mINI::INIStructure ini;
  file.read(ini);

  mINI::INIMap prefs = ini["prefs"];
  virtualKeyboard.scale =
      std::stoi(GetConfigValue(prefs, "virtual_keyboard_scale", "1"));

  auto comboString = GetConfigValue(prefs, "toggle_keyboard_combo", "back");
  toggleKeyboardButtonCombo = ButtonComboFromString(comboString);

  // Write the config back to disk
  ini["prefs"] = prefs;
  file.write(ini, true);
}

extern "C" __declspec(dllexport) void WINAPI LoadMod() {
  LoadConfig();

  // Hook xinput for virtual keyboard controls.
  HookFunction("XInput1_4.dll", "XInputGetState", (void*)XInputGetStateHook,
               (void**)&XInputGetStateOrig);

  // Hook dinput for the keyboard emulation.
  HookFunction("dinput8.dll", "DirectInput8Create",
               (void*)DirectInput8CreateHook, (void**)&DirectInput8CreateOrig);

  // Hook d3d9 for the virtual keyboard overlay.
  HookFunction("d3d9.dll", "Direct3DCreate9", (void*)Direct3DCreate9Hook,
               (void**)&Direct3DCreate9Orig);
}
