# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a **Windows Text Service Framework (TSF) DLL** that implements an always-English input method profile. Product/display name: **AlwaysEnglish-IME** (formerly `KeyboardMethod`); file names, code identifiers, the DLL, and the CMake target use `AlwaysEnglishIME` (no hyphen). It intentionally does not intercept keyboard input; Windows and the application handle keys through the registered US QWERTY substitute layout.

## Build Commands

```bash
# Build Release x64 DLL + imeinst, then compile Setup.exe
./build.bat

# Manual build (cmake may live under Visual Studio, not PATH)
cmake -S . -B out/build/x64-Release -G "Visual Studio 17 2022" -A x64
cmake --build out/build/x64-Release --config Release
./installer/build.bat
```

**Build outputs:**
- `out/build/x64-Release/bin/Release/AlwaysEnglishIME.dll`
- `out/build/x64-Release/bin/Release/imeinst.exe`
- `out/installer/AlwaysEnglish-IME-Setup.exe`

**Installation:** install and uninstall only via `out/installer/AlwaysEnglish-IME-Setup.exe` (and its uninstaller). The installer drives registration through `imeinst.exe`; there are no manual registration scripts, and `regsvr32` is not part of the supported workflow.

`scripts/Remove-DefaultUserTip.ps1` is a standalone helper that removes this TIP's leftovers from the `.Default` profile (logon/lock screen). It is a dry run by default; `-Apply` (elevated) actually removes entries. Keep its hardcoded GUIDs in sync with `include/Globals.h`.

## Architecture

### Component Structure

The project follows a standard COM-based TSF architecture with these key components:

| Component | Interface | Purpose |
|-----------|-----------|---------|
| **CTextService** | `ITfTextInputProcessorEx`, `ITfThreadMgrEventSink`, `ITfThreadFocusSink`, `ITfCompartmentEventSink` | Main text service; manages lifecycle and keeps IME state closed/alphanumeric |
| **CClassFactory** | `IClassFactory` | COM factory that creates CTextService instances |
| **Compartment helpers** | `ITfCompartment`, `ITfSource` | Read/write TSF mode state and subscribe to changes |
| **Register** | - | COM/TSF registration and unregistration helpers |
| **imeinst** | - | Installer-facing executable that calls exported registration helpers |

### COM Entry Points

The DLL exports four standard COM functions (defined in `AlwaysEnglishIME.def.in`):
- `DllCanUnloadNow` - Check if DLL can be unloaded
- `DllGetClassObject` - Create class factory
- `DllRegisterServer` - Register COM server and TSF profile
- `DllUnregisterServer` - Unregister COM server and TSF profile

### TSF Integration Flow

1. **Activation**: Windows calls `ITfTextInputProcessor::Activate()` with `ITfThreadMgr` and `TfClientId`
2. **Initialization**: `CTextService` subscribes to thread manager, thread focus, and compartment events
3. **State enforcement**: The service keeps IME open/close off, conversion alphanumeric, and sentence mode off
4. **Keyboard input**: No key event sink is registered; Windows and the application handle keys directly
5. **Deactivation**: `ITfTextInputProcessor::Deactivate()` removes subscriptions and releases the thread manager

### Input Model

This service intentionally does not modify text. It has no `ITfKeyEventSink`, composition, candidate UI, or edit sessions. Its "always English" behavior comes from registering the profile with the US QWERTY substitute layout and leaving keyboard events to Windows/the application.

### Registration System

Registration uses the TSF profile/category COM APIs and writes these project-owned registry locations:
- `HKCR\CLSID\` - COM class registration
- `HKLM\Software\bsgy\AlwaysEnglishIME` - Installed display name and LANGID. Reads fall back to the legacy pre-rename key `HKLM\Software\bsgy\KeyboardMethod`; registration deletes the legacy key after writing the new one, and uninstall deletes both.

Upgrade from the pre-rename `KeyboardMethod` 1.1.x is supported: the Inno `AppId`, CLSID, and profile GUID are unchanged; the installer removes the old `{app}\KeyboardMethod.dll` after `imeinst register` succeeds (delete-on-reboot if it is in use), and the uninstaller also cleans it up.

`InstallLayoutOrTip` adds/removes the profile from the current user's language list and the `.Default` secure-desktop profile.

GUIDs defined in `Globals.h`:
- `g_clsidTextService` - COM CLSID for the text service
- `g_guidProfile` - TSF profile GUID

## Development Notes

- **Language**: C++17
- **Build System**: CMake 3.20+
- **Target**: Windows x64 DLL
- **Dependencies**: Windows SDK (msctf.h for TSF API)
- **Requires Admin**: Installation/uninstallation requires administrator privileges (Setup.exe elevates)

After installation, the text service appears in Windows language settings and can be enabled/disabled via standard Windows input method switching.
