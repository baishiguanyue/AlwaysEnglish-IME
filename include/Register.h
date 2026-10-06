#pragma once

#include <windows.h>
#include <msctf.h>

#define KM_REGISTER_INSTALL_TIP 0x00000001

HRESULT UnregisterLegacyPlaceholder();

#ifdef __cplusplus
extern "C" {
#endif

HRESULT WINAPI RegisterTextService(LPCWSTR pszDisplayName, LANGID langid, DWORD dwFlags);
HRESULT WINAPI UnregisterTextService(void);
HRESULT WINAPI InstallUserTip(LANGID langid);
HRESULT WINAPI InstallUserTipDefault(LANGID langid);
HRESULT WINAPI UninstallUserTip(LANGID langid);

#ifdef __cplusplus
}
#endif
