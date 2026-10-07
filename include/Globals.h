#pragma once

#include <windows.h>
#include <msctf.h>

// COM CLSID and TSF profile GUID. Do not change after a public release.
// AlwaysEnglish-IME Text Service (product formerly named KeyboardMethod; GUIDs unchanged)
static const GUID g_clsidTextService =
    {0xfc452b85, 0x19f4, 0x47e5, {0xad, 0x40, 0xfd, 0x52, 0x32, 0x98, 0xa8, 0xc7}};
// AlwaysEnglish-IME Profile
static const GUID g_guidProfile =
    {0x0bf7dd25, 0x41dc, 0x400f, {0x88, 0x7d, 0x0a, 0x80, 0xac, 0x37, 0xf0, 0x62}};

// Placeholder GUIDs from earlier builds; unregister these on install/uninstall.
static const GUID g_clsidTextServiceLegacy =
    {0xA1B2C3D4, 0xE5F6, 0x7890, {0xAB, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89}};
static const GUID g_guidProfileLegacy =
    {0xB1C2D3E4, 0xF5A6, 0x7890, {0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB}};

static const LANGID g_langidProfile = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED); // 0x0804

extern const wchar_t g_wszClassName[];
extern const wchar_t g_wszInstallKey[];
extern const wchar_t g_wszLegacyInstallKey[];
extern const wchar_t g_wszSidecarIconName[];

extern HMODULE g_hInst;
extern LONG g_cRefDll;

BOOL GetSidecarIconPath(wchar_t *pszPath, size_t cchPath);
