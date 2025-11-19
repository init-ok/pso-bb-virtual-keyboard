#pragma once

#include <d3d9.h>
#include <d3dx9core.h>
#include <windef.h>
#include <winnt.h>

#include <string>
#include <vector>

class VirtualKeyboard {
 public:
  VirtualKeyboard();
  ~VirtualKeyboard();

  bool Initialize(LPDIRECT3DDEVICE9 device);
  void Draw(LPDIRECT3DDEVICE9 device);
  std::wstring GetSelectedKey();
  void HandleInput(bool leftDPadPressed, bool rightDPadPressed,
                   bool upDPadPressed, bool downDPadPressed, float thumbStickX,
                   float thumbStickY);

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
  int KeyWithOrigin(int x, int y, int defaultValue);
  int LastKeyInRow(int y);
  int KeyContainingPoint(int x, int y, int defaultValue);

  bool initialized;
};
