# SAO Lost Song DualSense Support

Play the Steam version of **Sword Art Online: Lost Song** with a PS5 DualSense controller, with PlayStation button icons.

The game has a built-in module for PS4 controllers, but it does not recognize the DualSense. This mod presents the DualSense to that module as a DualShock 4. `SAOLS.exe` and the game data are not modified on disk.

## Status

| | |
|---|---|
| Game version tested | Steam build 3383128 |
| DualSense over Bluetooth | Tested in game: icons, buttons, sticks |
| DualSense over USB | Report conversion covered by unit tests, not tried in game |
| DualSense Edge | Recognized by the code, not tested |

## Build

Requires Visual Studio 2022 Community with the C++ desktop tools. If your edition or install path differs, edit the `vcvars64.bat` path in `build.bat`.

```bat
build.bat
```

The DLLs are written to `dist\`.

## Install

1. Copy `xinput9_1_0.dll` and `dinput8.dll` from `dist\` to the game folder, next to `SAOLS.exe`. It is usually `steamapps\common\Sword Art Online Lost Song`.
2. Turn off Steam Input for the game: in Steam, open the game's Properties, then Controller, and choose to disable Steam Input.
3. Pair or plug in the controller and start the game.

## Uninstall

Delete `xinput9_1_0.dll`, `dinput8.dll` and `sao_ps5.log` from the game folder.

## How it works

- **`xinput9_1_0.dll`** reads the DualSense over HID. It intercepts the game's HID calls and answers as a DualShock 4 over USB: identity, device capabilities, feature reports and 64-byte input reports. Rumble commands from the game are converted to DualSense output reports.
- **`dinput8.dll`** hides the DualSense from DirectInput so that the game does not read it a second time with the wrong button layout.
- **Fallback:** until the game starts reading the controller in PS4 mode, the DualSense is exposed as XInput pad 0, which shows Xbox icons.

Other controllers are passed through unchanged.

## Limitations

- The light bar color requested by the game is not applied.
- The game sees a controller on USB with a full battery, whatever the real state.
- Only one DualSense is supported.

## Troubleshooting

The mod writes `sao_ps5.log` next to the game. `Le jeu utilise la manette en mode PS4` means that PS4 mode is active. Please attach this file when reporting a problem.

## Tests

`build.bat` also produces two test programs in `build\`:

```bat
build\test.exe
build\test.exe dist\xinput9_1_0.dll
build\dinput_test.exe dist\dinput8.dll
```

The first command checks the report conversions. The other two load the DLLs and read from a connected controller.

## Disclaimer

Unofficial fan project, not affiliated with Bandai Namco, Sony or Microsoft. You need your own copy of the game. This repository contains no game files.
