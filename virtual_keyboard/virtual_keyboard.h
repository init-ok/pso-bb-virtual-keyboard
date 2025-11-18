#pragma once

#include <d3d9.h>
#include <d3dx9core.h>
#include <windef.h>
#include <winnt.h>
#include <xinput.h>

#include <string>
#include <vector>

class VirtualKeyboard {
 public:
  VirtualKeyboard();
  ~VirtualKeyboard();

  bool Initialize(LPDIRECT3DDEVICE9 device);
  void HandleInput(XINPUT_STATE state);
  void Draw(LPDIRECT3DDEVICE9 device);
  std::wstring GetSelectedKey();
  bool enabled;
  int scale;

 private:
  struct Key {
    std::wstring label;
    RECT rect;
  };

  std::vector<Key> keys;
  int selectedIndex;
  int separator;
  int originX, originY;

  LPD3DXFONT font;
  LPD3DXSPRITE sprite;

  ULONGLONG lastInputTime;

  void CreateKeys();
  bool initialized;
};
