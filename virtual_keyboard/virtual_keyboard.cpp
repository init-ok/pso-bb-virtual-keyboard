#include "virtual_keyboard.h"

#include <WinUser.h>
#include <d3d9.h>
#include <d3d9types.h>
#include <d3dx9core.h>
#include <minwindef.h>
#include <sysinfoapi.h>
#include <windef.h>
#include <wingdi.h>
#include <winnt.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "shared.h"

VirtualKeyboard::VirtualKeyboard() {
  enabled = false;
  scale = 1;
  selectedIndex = 0;
  separator = 4;
  originX = originY = 0;
  font = nullptr;
  sprite = nullptr;
  lastInputTime = 0;
  initialized = false;
}

VirtualKeyboard::~VirtualKeyboard() {
  if (font) font->Release();
  if (sprite) sprite->Release();
}

bool VirtualKeyboard::Initialize(LPDIRECT3DDEVICE9 device) {
  D3DXCreateFont(device, 20 * scale, 0, FW_BOLD, 1, FALSE, DEFAULT_CHARSET,
                 OUT_DEFAULT_PRECIS, DEFAULT_QUALITY,
                 DEFAULT_PITCH | FF_DONTCARE, L"Arial", &font);

  D3DXCreateSprite(device, &sprite);

  CreateKeys();
  initialized = true;
  return true;
}

void VirtualKeyboard::CreateKeys() {
  // Layout with only letters, numbers, and symbols
  int cols = std::wstring(L"ABCDEFGHIJKLMN+-").length();

  const std::wstring layout =
      L"ABCDEFGHIJKLMN+-"
      L"OPQRSTUVWXYZ@'()"
      L"abcdefghijklmn;:"
      L"opqrstuvwxyz!?,."
      L"1234567890$%()<>";

  int keyWidth = 24 * scale;
  int keyHeight = 24 * scale;

  keys.clear();
  int x = 0, y = 0;

  // Build main alphanumeric keys
  for (int i = 0; i < (int)layout.size(); ++i) {
    RECT r = {x, y, x + keyWidth, y + keyHeight};
    keys.push_back({std::wstring(1, layout[i]), r});

    x += keyWidth + separator;
    if ((i + 1) % cols == 0) {
      x = 0;
      y += keyHeight + separator;
    }
  }

  // --- Add bottom row special keys ---
  x = 0;

  struct SpecialKey {
    std::wstring label;
    int width;
  };

  std::vector<SpecialKey> specials = {
      {L"Backspace", (keyWidth * 4) + (separator * 3)},
      {L"Space", (keyWidth * 7) + (separator * 6)},
      {L"OK", (keyWidth * 3) + (separator * 2)},
      {L"Exit", (keyWidth * 2) + (separator * 1)},
  };

  for (const auto& sk : specials) {
    RECT r = {x, y, x + sk.width, y + keyHeight};
    keys.push_back({sk.label, r});
    x += sk.width + separator;
  }
}

int VirtualKeyboard::KeyWithOrigin(int x, int y, int defaultValue) {
  for (size_t i = 0; i < keys.size(); ++i) {
    auto& key = keys[i];
    if (key.rect.left == x && key.rect.top == y) {
      return (int)i;
    }
  }
  return defaultValue;
}

int VirtualKeyboard::LastKeyInRow(int y) {
  // Find the key with the given y such that x is maximized.
  int maxX = 0;
  int maxIndex = 0;
  for (size_t i = 0; i < keys.size(); ++i) {
    auto& key = keys[i];
    if (key.rect.top == y) {
      maxX = max(key.rect.left, maxX);
      maxIndex = i;
    }
  }
  return maxIndex;
};

int VirtualKeyboard::KeyContainingPoint(int x, int y, int defaultValue) {
  for (size_t i = 0; i < keys.size(); ++i) {
    auto& key = keys[i];

    auto top = key.rect.top;
    auto bottom = key.rect.bottom;

    auto left = key.rect.left;
    auto right = key.rect.right;

    if (x >= left && x < right && y >= top && y < bottom) {
      return (int)i;
    }
  }
  return defaultValue;
};

void VirtualKeyboard::HandleInput(bool leftDPadPressed, bool rightDPadPressed,
                                  bool upDPadPressed, bool downDPadPressed,
                                  float thumbStickX, float thumbStickY) {
  if (!enabled) return;

  if (!initialized) {
    LOG("unexpectedly called before being initialized");
    exit(1);
  }

  const float speed = 15.0f * scale;  // movement multiplier per frame

  // Move position
  originX += (int)(thumbStickX * speed);
  originY -= (int)(thumbStickY * speed);  // Y inverted for screen coords

  ULONGLONG now = GetTickCount64();
  if (now - lastInputTime < 150) return;  // debounce

  // Start from the top left of the current key.
  auto& selectedKey = keys[selectedIndex];
  int x = selectedKey.rect.left;
  int y = selectedKey.rect.top;
  int keyWidth = selectedKey.rect.right - selectedKey.rect.left;
  int keyHeight = selectedKey.rect.bottom - selectedKey.rect.top;

  if (leftDPadPressed) {
    if (selectedIndex > 0) {
      auto& prevKey = keys[selectedIndex - 1];
      keyWidth = prevKey.rect.right - prevKey.rect.left;
    }
    // Subtract the width of the previous key and the separator.
    x -= keyWidth + separator;

    // Find out which key has this origin.
    selectedIndex = KeyWithOrigin(x, y, LastKeyInRow(y));
    lastInputTime = now;
  } else if (rightDPadPressed) {
    // Add the width of the current key and the separator.
    x += keyWidth + separator;

    // Find out which key has this origin.
    selectedIndex = KeyWithOrigin(x, y, KeyWithOrigin(0, y, 0));
    lastInputTime = now;
  } else if (upDPadPressed) {
    // Subtract the height of the current key and the separator.
    y -= keyHeight + separator;

    // Find out which key on the row above contains this point.
    // Else just keep the selected key.
    selectedIndex = KeyContainingPoint(x, y, selectedIndex);
    lastInputTime = now;
  } else if (downDPadPressed) {
    // Add the height of the current key and the separator.
    y += keyHeight + separator;

    // Find out which key on the next row down contains this point.
    // Else just keep the selected key.
    selectedIndex = KeyContainingPoint(x, y, selectedIndex);
    lastInputTime = now;
  }
}

void VirtualKeyboard::Draw(LPDIRECT3DDEVICE9 device) {
  if (!enabled) {
    return;
  }
  if (!initialized) {
    LOG("unexpectedly called before being initialized");
    exit(1);
  }
  if (!font) {
    LOG("Font is not loaded");
    exit(1);
  }
  if (!sprite) {
    LOG("Sprite is not loaded");
    exit(1);
  }

  sprite->Begin(D3DXSPRITE_ALPHABLEND);

  for (int i = 0; i < (int)keys.size(); ++i) {
    const Key& key = keys[i];

    // Highlight selected key
    bool isSelected = (i == selectedIndex);

    // Background color
    D3DCOLOR bgColor = isSelected ? D3DCOLOR_ARGB(255, 255, 128, 0)  // orange
                                  : D3DCOLOR_ARGB(255, 0, 40, 80);  // dark blue

    // Text color
    D3DCOLOR textColor =
        isSelected ? D3DCOLOR_XRGB(0, 0, 0)         // black for selected key
                   : D3DCOLOR_XRGB(255, 255, 255);  // white for others

    // Draw background
    D3DRECT rect = {originX + key.rect.left, originY + key.rect.top,
                    originX + key.rect.right, originY + key.rect.bottom};
    device->Clear(1, &rect, D3DCLEAR_TARGET, bgColor, 0, 0);

    RECT textRect = key.rect;
    textRect.left += originX;
    textRect.right += originX;
    textRect.top += originY;
    textRect.bottom += originY;

    // Draw text
    font->DrawTextW(sprite, key.label.c_str(),
                    -1,  // -1 to use full string
                    (LPRECT)&textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE,
                    textColor);
  }
  sprite->End();
}

std::wstring VirtualKeyboard::GetSelectedKey() {
  if (!initialized) {
    LOG("unexpectedly called before being initialized");
    exit(1);
  }
  if (selectedIndex >= 0 && selectedIndex < static_cast<int>(keys.size())) {
    return keys[selectedIndex].label;
  }
  LOG("Failed to get selected key");
  exit(1);
}
