# About
This mod lets you type in text chat and in text fields using a controller in Phantasy Star Online Blue Burst (PSOBB) on PC by adding a virtual onscreen keyboard.

![demo image](img/demo.jpg)

## Compatibility
To use this mod, you will need the following:

1. An Xbox controller.

2. "Direct3D 9" selected in the launcher settings:
![required graphics settings](img/settings_graphics.jpg)

3. "Use XInput for gamepad" enabled in the launcher settings:
![required input settings](img/settings_input.jpg)

## Installation
1. Download the latest mod files from [Releases](https://github.com/init-ok/pso_bb_virtual_keyboard/releases/).
2. Extract them to the game directory, e.g. `C:\Users\my_username\EphineaPSO`.
3. Start the game and press the select button on your Xbox controller to open the keyboard.

## Controls
The select button on the Xbox controller will open/close the virtual keyboard. `OK` emulates pressing the enter button on the keyboard, whereas `Exit` emulates presing the escape key on the keyboard and closing chat. 

## Changing the keyboard size
A `config.ini` file is created the first time the game is launched with the mod installed.

You can change the keyboard size by modifying the `virtual_keyboard_scale` variable in this file, for example, changing it to 2:
```
[prefs]
virtual_keyboard_scale = 2
```

## Troubleshooting
The mod and mod loader write logs to a `logs.txt` file that's stored in the `mod` folder. Please share this file (if applicable) when filing Github issues.

## Development
After building a project within the Visual Studio solution, VS will copy the associated built files to the game directory, which is assumed to be `C:\Users\<username>\EphineaPSO`.
